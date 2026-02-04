#include "chatview.h"
#include <QDir>
#include <QEventLoop>
#include <QNetworkCookie>
#include <QStandardPaths>
#include <QTimer>
#include <QWebEnginePage>
#include <QCoreApplication>

ChatView::ChatView(QWidget *parent)
    : QWebEngineView(parent)
{
    // Resolve stable data roots for persistent profile data
    const QString appDataSuffix = QStringLiteral("chatgpt-desktop-unix");
    const QString homeRoot = QDir::homePath();

    QString storageRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (storageRoot.isEmpty()) {
        storageRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    }
    // Guard against volatile mounts that can drop session data
    if (storageRoot.isEmpty() || storageRoot.startsWith(QStringLiteral("/tmp"))
        || storageRoot.startsWith(QStringLiteral("/run/"))
        || storageRoot.startsWith(QStringLiteral("/var/tmp"))) {
        storageRoot = homeRoot + QDir::separator() + QStringLiteral(".local")
            + QDir::separator() + QStringLiteral("share");
    }
    if (!storageRoot.endsWith(appDataSuffix)) {
        storageRoot += QDir::separator() + appDataSuffix;
    }

    QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    // Keep cache off volatile paths for consistency across restarts
    if (cacheRoot.isEmpty() || cacheRoot.startsWith(QStringLiteral("/tmp"))
        || cacheRoot.startsWith(QStringLiteral("/run/"))
        || cacheRoot.startsWith(QStringLiteral("/var/tmp"))) {
        cacheRoot = homeRoot + QDir::separator() + QStringLiteral(".cache");
    }
    if (!cacheRoot.endsWith(appDataSuffix)) {
        cacheRoot += QDir::separator() + appDataSuffix;
    }

    // Separate storage and cache paths to avoid collisions
    const QString profileStoragePath = storageRoot;
    const QString profileCachePath = cacheRoot;

    // Ensure paths exist before creating the profile
    QDir().mkpath(profileStoragePath);
    QDir().mkpath(profileCachePath);

    // Use a dedicated persistent profile to avoid off-the-record defaults
    m_profile = new QWebEngineProfile(QStringLiteral("chatgpt-desktop-unix"), this);
    m_profile->setPersistentStoragePath(profileStoragePath);
    m_profile->setCachePath(profileCachePath);
    m_profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
    m_profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);

    // Bind the persistent profile to the view
    QWebEnginePage *webPage = new QWebEnginePage(m_profile, this);
    setPage(webPage);

    // Load existing cookies immediately
    m_cookieStore = m_profile->cookieStore();
    if (m_cookieStore != nullptr) {
        m_cookieStore->loadAllCookies();

        // Schedule a flush after cookie changes to persist login state promptly
        auto scheduleFlush = [this]() {
            QTimer::singleShot(200, this, [this]() {
                FlushPersistentStateSync();
            });
        };

        QObject::connect(m_cookieStore, &QWebEngineCookieStore::cookieAdded, this,
            [scheduleFlush](const QNetworkCookie &) {
                scheduleFlush();
            });
        QObject::connect(m_cookieStore, &QWebEngineCookieStore::cookieRemoved, this,
            [scheduleFlush](const QNetworkCookie &) {
                scheduleFlush();
            });
    }

    // Flush before shutdown to ensure cookies reach disk
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this]() {
        FlushPersistentStateSync();
    });

    load(QUrl(QStringLiteral("https://chatgpt.com")));
}

ChatView::~ChatView()
{
    FlushPersistentStateSync();
}

void ChatView::FlushPersistentStateSync()
{
    if (m_profile == nullptr) {
        return;
    }

    // Trigger a store write before teardown to avoid async loss
    QWebEngineCookieStore *store = m_profile->cookieStore();
    if (store != nullptr) {
        // Dummy delete can trigger a disk flush in some QtWebEngine versions
        QNetworkCookie dummy;
        store->deleteCookie(dummy);
    }

    // Allow a short window for async WebEngine writes to complete
    QEventLoop loop;
    QTimer::singleShot(600, &loop, &QEventLoop::quit);
    loop.exec();
}
