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

// Random bridge text blocks hostile page code from guessing the prompt channel
QString BuildClipboardBridgePrefix() {
  return QStringLiteral("__CHATGPT_DESKTOP_COPY__%1__")
      .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

bool IsProcessAlive(qint64 processId) {
  // Ignore invalid ids before calling into the kernel
  if (processId <= 0 || processId > std::numeric_limits<pid_t>::max()) {
    return false;
  }

  const pid_t nativePid = static_cast<pid_t>(processId);
  // EPERM still means the process exists but cannot be signaled
  return ::kill(nativePid, 0) == 0 || errno == EPERM;
}
} // namespace

BrowserProfile &BrowserProfile::Instance() {
  // Function static keeps one profile manager per process without heap tricks
  static BrowserProfile instance;
  return instance;
}

BrowserProfile::BrowserProfile() { InitializeProfile(); }

QWebEngineProfile *BrowserProfile::Profile() const { return m_profile; }

const QString &BrowserProfile::ClipboardBridgePrefix() const { return m_clipboardBridgePrefix; }

void BrowserProfile::InitializeProfile() {
  if (m_profile != nullptr) {
    return;
  }

  // Storage and cache roots stay stable so logins survive restarts
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

  // One lock per process keeps separate app launches from sharing Chromium files
  const QString lockPath = QDir(storageRoot).filePath(QStringLiteral("profile.lock"));
  std::unique_ptr<QLockFile> profileLock = std::make_unique<QLockFile>(lockPath);
  profileLock->setStaleLockTime(0);
  bool hasProfileLock = profileLock->tryLock(0);

  if (!hasProfileLock) {
    // Try to recover from a dead process before falling back to isolation
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

  // Use isolated paths only when another live process owns the main profile
  if (!hasProfileLock) {
    // Isolated paths keep the current run usable when another process owns the profile
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
  m_profile = new QWebEngineProfile(QString::fromLatin1(kProfileName), QCoreApplication::instance());
  m_profile->setPersistentStoragePath(activeStoragePath);
  m_profile->setCachePath(activeCachePath);
  m_profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
  m_profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);

  // Keep the lock alive for the whole process lifetime
  if (hasProfileLock) {
    m_profileLock = std::move(profileLock);
  }

  QWebEngineCookieStore *cookieStore = m_profile->cookieStore();
  if (cookieStore != nullptr) {
    // Load existing login state before the first page render starts
    cookieStore->loadAllCookies();

    auto markCookieMutation = [this](const QNetworkCookie &) {
      // Qt does not expose a synchronous cookie flush API, so the app tracks
      // the last mutation time and gives Chromium a short drain window on exit
      m_lastCookieMutationAtMs = QDateTime::currentMSecsSinceEpoch();
    };

    QObject::connect(cookieStore, &QWebEngineCookieStore::cookieAdded, m_profile, markCookieMutation);
    QObject::connect(cookieStore, &QWebEngineCookieStore::cookieRemoved, m_profile, markCookieMutation);
  }

  // Only the app shutdown path waits for cookie writes
  QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, m_profile,
                   [this]() { FlushPersistentStateSync(); });
}

QString BrowserProfile::ResolveStorageRoot() const {
  const QString appDataSuffix = QString::fromLatin1(kProfileName);
  const QString homeRoot = QDir::homePath();

  // XDG data is the first choice when the platform reports it
  QString storageRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (storageRoot.isEmpty()) {
    storageRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  }
  // Fall back to a normal home path if the reported path is volatile
  if (storageRoot.isEmpty() || storageRoot.startsWith(QStringLiteral("/tmp")) ||
      storageRoot.startsWith(QStringLiteral("/run/")) || storageRoot.startsWith(QStringLiteral("/var/tmp"))) {
    storageRoot =
        homeRoot + QDir::separator() + QStringLiteral(".local") + QDir::separator() + QStringLiteral("share");
  }
  if (!storageRoot.endsWith(appDataSuffix)) {
    storageRoot += QDir::separator() + appDataSuffix;
  }

  return storageRoot;
}

QString BrowserProfile::ResolveCacheRoot() const {
  const QString appDataSuffix = QString::fromLatin1(kProfileName);
  const QString homeRoot = QDir::homePath();

  // Cache can use the XDG cache path when it is safe
  QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if (cacheRoot.isEmpty() || cacheRoot.startsWith(QStringLiteral("/tmp")) ||
      cacheRoot.startsWith(QStringLiteral("/run/")) || cacheRoot.startsWith(QStringLiteral("/var/tmp"))) {
    cacheRoot = homeRoot + QDir::separator() + QStringLiteral(".cache");
  }
  if (!cacheRoot.endsWith(appDataSuffix)) {
    cacheRoot += QDir::separator() + appDataSuffix;
  }

  return cacheRoot;
}

void BrowserProfile::FlushPersistentStateSync() {
  if (m_profile == nullptr) {
    return;
  }

  // Chromium persists profile data asynchronously, so shutdown keeps a short
  // grace window after the most recent cookie mutation instead of pretending
  // there is an explicit flush call available
  qint64 waitMs = kMinimumQuitDelayMs;
  if (m_lastCookieMutationAtMs > 0) {
    const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - m_lastCookieMutationAtMs;
    waitMs = std::max<qint64>(waitMs, kCookieDrainWindowMs - elapsedMs);
  }

  if (waitMs <= 0) {
    return;
  }

  // Sleeping avoids a nested event loop while windows are being destroyed
  QThread::msleep(static_cast<unsigned long>(waitMs));
}
