#include "chatview.h"
#include "chatinjections.h"
#include "chatwebpage.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QHideEvent>
#include <QLockFile>
#include <QNetworkCookie>
#include <QShowEvent>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QWebEngineDownloadRequest>
#include <QWebEnginePage>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QtGlobal>

namespace {
// Per-process prefix blocks prompt forging from unrelated page scripts
QString BuildClipboardBridgePrefix()
{
    return QStringLiteral("__CHATGPT_DESKTOP_COPY__%1__")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}
}

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
    if (!QDir().mkpath(profileStoragePath)) {
        qWarning() << "Failed to create profile storage path:" << profileStoragePath;
    }
    if (!QDir().mkpath(profileCachePath)) {
        qWarning() << "Failed to create profile cache path:" << profileCachePath;
    }

    QString activeStoragePath = profileStoragePath;
    QString activeCachePath = profileCachePath;

    // Lock the persistent profile to prevent concurrent process corruption
    const QString lockPath = QDir(profileStoragePath).filePath(QStringLiteral("profile.lock"));
    m_profileLock = std::make_unique<QLockFile>(lockPath);
    m_profileLock->setStaleLockTime(0);
    if (!m_profileLock->tryLock(0)) {
        // Isolated profile keeps this process writable when another instance is active
        const QString isolatedSuffix = QStringLiteral("isolated-%1-%2")
            .arg(QCoreApplication::applicationPid())
            .arg(QDateTime::currentMSecsSinceEpoch());
        activeStoragePath = QDir(profileStoragePath).filePath(isolatedSuffix);
        activeCachePath = QDir(profileCachePath).filePath(isolatedSuffix);

        if (!QDir().mkpath(activeStoragePath) || !QDir().mkpath(activeCachePath)) {
            qWarning() << "Failed to create isolated profile paths:"
                       << activeStoragePath << activeCachePath;
        }

        qWarning() << "Profile storage lock is held by another process, using isolated profile paths";
    }

    // Use a dedicated persistent profile to avoid off-the-record defaults
    m_profile = new QWebEngineProfile(QStringLiteral("chatgpt-desktop-unix"));
    m_profile->setPersistentStoragePath(activeStoragePath);
    m_profile->setCachePath(activeCachePath);
    m_profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
    m_profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);

    // Per-process secret used by JS/native bridge for prompt validation
    const QString clipboardBridgePrefix = BuildClipboardBridgePrefix();

    // Bind the persistent profile to the view
    ChatWebPage *webPage = new ChatWebPage(m_profile, clipboardBridgePrefix, this);
    m_profile->setParent(webPage);
    setPage(webPage);

    QWebEngineScript codeCopyBridgeScript;
    codeCopyBridgeScript.setName(QStringLiteral("chatgpt-desktop-code-copy-bridge"));
    codeCopyBridgeScript.setInjectionPoint(QWebEngineScript::DocumentCreation);
    codeCopyBridgeScript.setRunsOnSubFrames(true);
    codeCopyBridgeScript.setWorldId(QWebEngineScript::MainWorld);
    // Load JS text from resource files
    codeCopyBridgeScript.setSourceCode(
        ChatInjections::BuildCodeCopyBridgeScriptSource(clipboardBridgePrefix));
    webPage->scripts().insert(codeCopyBridgeScript);

    QWebEngineScript longChatPerfScript;
    longChatPerfScript.setName(QStringLiteral("chatgpt-desktop-long-chat-perf"));
    longChatPerfScript.setInjectionPoint(QWebEngineScript::DocumentReady);
    longChatPerfScript.setRunsOnSubFrames(false);
    longChatPerfScript.setWorldId(QWebEngineScript::MainWorld);
    // Run performance JS after the page is ready
    longChatPerfScript.setSourceCode(ChatInjections::BuildLongChatPerformanceScriptSource());
    webPage->scripts().insert(longChatPerfScript);

    QWebEngineSettings *webSettings = settings();
    if (webSettings != nullptr) {
        webSettings->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, true);
        webSettings->setAttribute(QWebEngineSettings::JavascriptCanPaste, true);
        webSettings->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, false);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        webSettings->setImageAnimationPolicy(QWebEngineSettings::ImageAnimationPolicy::AnimateOnce);
#endif
    }

    // Route all browser-triggered downloads through native save handling.
    QObject::connect(m_profile, &QWebEngineProfile::downloadRequested, this,
        [this](QWebEngineDownloadRequest *download) {
            HandleDownloadRequest(download);
        });

    m_persistenceDebounceTimer = new QTimer(this);
    m_persistenceDebounceTimer->setSingleShot(true);
    // Cookie churn can be high during login and model switches
    m_persistenceDebounceTimer->setInterval(1200);
    QObject::connect(m_persistenceDebounceTimer, &QTimer::timeout, this, [this]() {
        FlushPersistentStateAsync();
    });

    // Load existing cookies immediately
    m_cookieStore = m_profile->cookieStore();
    if (m_cookieStore != nullptr) {
        m_cookieStore->loadAllCookies();

        QObject::connect(m_cookieStore, &QWebEngineCookieStore::cookieAdded, this,
            [this](const QNetworkCookie &) {
                MarkPersistentStateDirty();
            });
        QObject::connect(m_cookieStore, &QWebEngineCookieStore::cookieRemoved, this,
            [this](const QNetworkCookie &) {
                MarkPersistentStateDirty();
            });
    }

    // Flush before shutdown to ensure cookies reach disk
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this]() {
        FlushPersistentStateSync();
    });

    load(QUrl(QStringLiteral("https://chatgpt.com")));

    // Keep lifecycle active while visible, and freeze only when hidden/minimized
    QTimer::singleShot(0, this, [this]() {
        UpdatePageLifecycleState();
    });
}

ChatView::~ChatView()
{
    FlushPersistentStateSync();
}

void ChatView::MarkPersistentStateDirty()
{
    // Dirty flag coalesces frequent cookie notifications
    m_persistenceDirty = true;
    if (m_persistenceDebounceTimer != nullptr) {
        m_persistenceDebounceTimer->start();
    }
}

void ChatView::FlushPersistentStateAsync()
{
    if (m_flushInProgress || m_profile == nullptr || !m_persistenceDirty) {
        return;
    }

    // Guard against overlapping async flush requests
    m_flushInProgress = true;

    // Trigger a store write to push pending persistence work
    QWebEngineCookieStore *store = m_profile->cookieStore();
    if (store != nullptr) {
        QNetworkCookie dummy;
        store->deleteCookie(dummy);
    }

    m_persistenceDirty = false;
    m_flushInProgress = false;
}

void ChatView::FlushPersistentStateSync()
{
    if (m_profile == nullptr || m_shutdownFlushComplete) {
        return;
    }

    if (m_persistenceDebounceTimer != nullptr && m_persistenceDebounceTimer->isActive()) {
        m_persistenceDebounceTimer->stop();
    }

    // Force a final write path even if the last debounce has not fired yet
    m_persistenceDirty = true;
    FlushPersistentStateAsync();

    // Allow a bounded window for async WebEngine writes to complete
    QEventLoop flushLoop;
    QTimer::singleShot(120, &flushLoop, &QEventLoop::quit);
    flushLoop.exec(QEventLoop::ExcludeUserInputEvents);

    m_shutdownFlushComplete = true;
}

QString ChatView::DownloadDirectoryPath() const
{
    QString downloadDirectory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (downloadDirectory.isEmpty()) {
        downloadDirectory = QDir::homePath() + QDir::separator() + QStringLiteral("Downloads");
    }
    if (!QDir().mkpath(downloadDirectory)) {
        qWarning() << "Failed to create download directory:" << downloadDirectory;
    }
    return downloadDirectory;
}

void ChatView::HandleDownloadRequest(QWebEngineDownloadRequest *download)
{
    if (download == nullptr) {
        return;
    }

    const QString suggestedName = download->downloadFileName().isEmpty()
        ? QStringLiteral("download")
        : download->downloadFileName();
    const QString suggestedPath = QDir(DownloadDirectoryPath()).filePath(suggestedName);

    const QString selectedPath = QFileDialog::getSaveFileName(
        this,
        tr("Save File"),
        suggestedPath);
    if (selectedPath.isEmpty()) {
        download->cancel();
        return;
    }

    QObject::connect(download, &QWebEngineDownloadRequest::stateChanged, this,
        [download](QWebEngineDownloadRequest::DownloadState state) {
            if (state == QWebEngineDownloadRequest::DownloadInterrupted
                || state == QWebEngineDownloadRequest::DownloadCancelled) {
                // Explicit diagnostics prevent silent failed downloads
                qWarning() << "Download failed:" << download->url()
                           << "state:" << state
                           << "reason:" << download->interruptReasonString();
            }
        });

    const QFileInfo selectedInfo(selectedPath);
    if (!QDir().mkpath(selectedInfo.absolutePath())) {
        qWarning() << "Failed to create target directory:" << selectedInfo.absolutePath();
        download->cancel();
        return;
    }
    if (selectedInfo.fileName().isEmpty()) {
        qWarning() << "Invalid target filename for download path:" << selectedPath;
        download->cancel();
        return;
    }
    download->setDownloadDirectory(selectedInfo.absolutePath());
    download->setDownloadFileName(selectedInfo.fileName());
    download->accept();
}

void ChatView::showEvent(QShowEvent *event)
{
    QWebEngineView::showEvent(event);
    UpdatePageLifecycleState();
}

void ChatView::hideEvent(QHideEvent *event)
{
    QWebEngineView::hideEvent(event);
    UpdatePageLifecycleState();
}

void ChatView::changeEvent(QEvent *event)
{
    QWebEngineView::changeEvent(event);
    if (event != nullptr && event->type() == QEvent::WindowStateChange) {
        UpdatePageLifecycleState();
    }
}

void ChatView::UpdatePageLifecycleState()
{
    QWebEnginePage *currentPage = page();
    if (currentPage == nullptr) {
        return;
    }

    bool shouldFreeze = !isVisible();
    QWidget *topLevelWindow = window();
    if (topLevelWindow != nullptr && topLevelWindow->isMinimized()) {
        shouldFreeze = true;
    }

    const QWebEnginePage::LifecycleState currentState = currentPage->lifecycleState();
    if (shouldFreeze && currentState == QWebEnginePage::LifecycleState::Active) {
        currentPage->setLifecycleState(QWebEnginePage::LifecycleState::Frozen);
        return;
    }

    if (!shouldFreeze && currentState != QWebEnginePage::LifecycleState::Active) {
        currentPage->setLifecycleState(QWebEnginePage::LifecycleState::Active);
    }
}
