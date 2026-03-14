#include "browserprofile.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QLockFile>
#include <QNetworkCookie>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>
#include <QWebEngineCookieStore>
#include <QWebEngineProfile>
#include <QUuid>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <limits>
#include <memory>

namespace {
constexpr auto kProfileName = "chatgpt-desktop-unix";
constexpr int kCookieDrainWindowMs = 350;
constexpr int kMinimumQuitDelayMs = 120;

// Random bridge text makes the prompt channel hard to guess from page code
QString BuildClipboardBridgePrefix() {
  return QStringLiteral("__CHATGPT_DESKTOP_COPY__%1__")
      .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

bool IsProcessAlive(qint64 processId) {
  // Reject bad ids before calling into the kernel
  if (processId <= 0 || processId > std::numeric_limits<pid_t>::max()) {
    return false;
  }

  const pid_t nativePid = static_cast<pid_t>(processId);
  // EPERM still means the process exists but cannot be signaled
  return ::kill(nativePid, 0) == 0 || errno == EPERM;
}
} // namespace

BrowserProfile &BrowserProfile::Instance() {
  // Function static gives one profile manager for the whole process
  static BrowserProfile instance;
  return instance;
}

BrowserProfile::BrowserProfile() { InitializeProfile(); }

QWebEngineProfile *BrowserProfile::Profile() const { return m_profile; }

const QString &BrowserProfile::ClipboardBridgePrefix() const { return m_clipboardBridgePrefix; }

void BrowserProfile::InitializeProfile() {
  if (m_profile != nullptr) {
    // The shared profile should only be built once
    return;
  }

  // Stable disk paths let login state survive restarts
  const QString storageRoot = ResolveStorageRoot();
  const QString cacheRoot = ResolveCacheRoot();

  if (!QDir().mkpath(storageRoot)) {
    qWarning() << "Failed to create profile storage path:" << storageRoot;
  }
  if (!QDir().mkpath(cacheRoot)) {
    qWarning() << "Failed to create profile cache path:" << cacheRoot;
  }

  QString activeStoragePath = storageRoot;
  QString activeCachePath = cacheRoot;

  // One lock decides which process owns the main Chromium files
  const QString lockPath = QDir(storageRoot).filePath(QStringLiteral("profile.lock"));
  std::unique_ptr<QLockFile> profileLock = std::make_unique<QLockFile>(lockPath);
  profileLock->setStaleLockTime(0);
  bool hasProfileLock = profileLock->tryLock(0);

  if (!hasProfileLock) {
    // First check if the old owner is gone before giving up on the main profile
    qint64 lockPid = 0;
    QString lockHost;
    const bool hasLockInfo = profileLock->getLockInfo(&lockPid, &lockHost, nullptr);
    const QString localHost = QSysInfo::machineHostName().toLower();
    const bool lockIsLocal = hasLockInfo && lockHost.toLower() == localHost;
    const bool lockOwnerAlive = lockIsLocal && IsProcessAlive(lockPid);

    if (lockIsLocal && !lockOwnerAlive && profileLock->removeStaleLockFile()) {
      hasProfileLock = profileLock->tryLock(0);
      if (hasProfileLock) {
        qWarning() << "Recovered stale profile lock file";
      }
    }
  }

  // Fall back to isolated paths only when another live process owns the main profile
  if (!hasProfileLock) {
    // This keeps a second app process usable without corrupting shared Chromium state
    const QString isolatedSuffix = QStringLiteral("isolated-%1-%2")
                                       .arg(QCoreApplication::applicationPid())
                                       .arg(QDateTime::currentMSecsSinceEpoch());
    activeStoragePath = QDir(storageRoot).filePath(isolatedSuffix);
    activeCachePath = QDir(cacheRoot).filePath(isolatedSuffix);

    if (!QDir().mkpath(activeStoragePath) || !QDir().mkpath(activeCachePath)) {
      qWarning() << "Failed to create isolated profile paths:" << activeStoragePath << activeCachePath;
    }

    qWarning() << "Profile storage lock is held by another process, using isolated profile paths";
  }

  m_clipboardBridgePrefix = BuildClipboardBridgePrefix();
  // The profile object is parented to QCoreApplication for normal app lifetime ownership
  m_profile = new QWebEngineProfile(QString::fromLatin1(kProfileName), QCoreApplication::instance());
  m_profile->setPersistentStoragePath(activeStoragePath);
  m_profile->setCachePath(activeCachePath);
  // Disk cache is important for repeat launches and large page loads
  m_profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
  // Force persistent cookies so login state survives a restart
  m_profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);

  // Keep the lock object alive for as long as this process owns the profile
  if (hasProfileLock) {
    m_profileLock = std::move(profileLock);
  }

  QWebEngineCookieStore *cookieStore = m_profile->cookieStore();
  if (cookieStore != nullptr) {
    // Pull saved cookies in before the first page tries to use them
    cookieStore->loadAllCookies();

    auto markCookieMutation = [this](const QNetworkCookie &) {
      // Qt does not expose a real sync flush call here
      // Track the last write time so shutdown can wait a short drain window
      m_lastCookieMutationAtMs = QDateTime::currentMSecsSinceEpoch();
    };

    QObject::connect(cookieStore, &QWebEngineCookieStore::cookieAdded, m_profile, markCookieMutation);
    QObject::connect(cookieStore, &QWebEngineCookieStore::cookieRemoved, m_profile, markCookieMutation);
  }

  // Only normal app shutdown waits for cookie writes
  QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, m_profile,
                   [this]() { FlushPersistentStateSync(); });
}

QString BrowserProfile::ResolveStorageRoot() const {
  const QString appDataSuffix = QString::fromLatin1(kProfileName);
  const QString homeRoot = QDir::homePath();

  // XDG data is the first choice when the platform gives a stable path
  QString storageRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (storageRoot.isEmpty()) {
    storageRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  }
  // Skip volatile temp paths so restarts do not lose login state
  if (storageRoot.isEmpty() || storageRoot.startsWith(QStringLiteral("/tmp")) ||
      storageRoot.startsWith(QStringLiteral("/run/")) || storageRoot.startsWith(QStringLiteral("/var/tmp"))) {
    storageRoot =
        homeRoot + QDir::separator() + QStringLiteral(".local") + QDir::separator() + QStringLiteral("share");
  }
  if (!storageRoot.endsWith(appDataSuffix)) {
    // Always end under one app named folder for predictable layout
    storageRoot += QDir::separator() + appDataSuffix;
  }

  return storageRoot;
}

QString BrowserProfile::ResolveCacheRoot() const {
  const QString appDataSuffix = QString::fromLatin1(kProfileName);
  const QString homeRoot = QDir::homePath();

  // Cache can use the XDG cache path when it is stable
  QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if (cacheRoot.isEmpty() || cacheRoot.startsWith(QStringLiteral("/tmp")) ||
      cacheRoot.startsWith(QStringLiteral("/run/")) || cacheRoot.startsWith(QStringLiteral("/var/tmp"))) {
    cacheRoot = homeRoot + QDir::separator() + QStringLiteral(".cache");
  }
  if (!cacheRoot.endsWith(appDataSuffix)) {
    // Match the storage layout so both paths are easy to find
    cacheRoot += QDir::separator() + appDataSuffix;
  }

  return cacheRoot;
}

void BrowserProfile::FlushPersistentStateSync() {
  if (m_profile == nullptr) {
    return;
  }

  // Chromium writes profile data in the background
  // Wait a short grace window after the last cookie change instead of faking a flush
  qint64 waitMs = kMinimumQuitDelayMs;
  if (m_lastCookieMutationAtMs > 0) {
    const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - m_lastCookieMutationAtMs;
    waitMs = std::max<qint64>(waitMs, kCookieDrainWindowMs - elapsedMs);
  }

  if (waitMs <= 0) {
    return;
  }

  // Sleep here instead of spinning a nested event loop during shutdown
  QThread::msleep(static_cast<unsigned long>(waitMs));
}
