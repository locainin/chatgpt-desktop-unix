#include "chatview.h"
#include "chatwebpage.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QLockFile>
#include <QNetworkCookie>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QWebEngineDownloadRequest>
#include <QWebEnginePage>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>

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
    QString codeCopyBridgeSource = QStringLiteral(R"JS(
(() => {
  const host = window.location.hostname || "";
  const trusted = /(^|\.)chatgpt\.com$/i.test(host)
    || /(^|\.)openai\.com$/i.test(host)
    || /(^|\.)oaistatic\.com$/i.test(host);
  if (!trusted) {
    return;
  }
  if (window.__chatgptDesktopCodeCopyInstalled) {
    return;
  }
  window.__chatgptDesktopCodeCopyInstalled = true;

  // Capture prompt early so later page monkeypatching cannot spoof bridge behavior
  const nativePrompt = (typeof window.prompt === "function")
    ? window.prompt.bind(window)
    : null;
  const copyPrefix = "__CHATGPT_DESKTOP_COPY_PREFIX_PLACEHOLDER__";

  const hasNearbyCodeBlock = (control) => {
    const container = control.closest("article,[data-testid*='conversation-turn'],li[data-message-author-role],div[data-message-author-role],div")
      || control.parentElement
      || document;
    return !!container.querySelector("pre code, pre");
  };

  const isProbablyCopyControl = (control) => {
    if (!(control instanceof Element)) {
      return false;
    }

    const testId = (control.getAttribute("data-testid") || "").toLowerCase();
    const ariaLabel = (control.getAttribute("aria-label") || "").toLowerCase();
    const text = (control.textContent || "").toLowerCase();
    const looksLikeCopy = testId.includes("copy")
      || ariaLabel.includes("copy")
      || text.includes("copy");
    return looksLikeCopy && hasNearbyCodeBlock(control);
  };

  const findControlFromEvent = (event) => {
    if (typeof event.composedPath === "function") {
      const path = event.composedPath();
      for (const node of path) {
        if (!(node instanceof Element)) {
          continue;
        }
        const isControl = node.tagName === "BUTTON"
          || (node.getAttribute("role") || "").toLowerCase() === "button";
        if (isControl && isProbablyCopyControl(node)) {
          return node;
        }
      }
    }

    if (event.target instanceof Element) {
      const candidate = event.target.closest("button,[role='button']");
      if (candidate instanceof Element && isProbablyCopyControl(candidate)) {
        return candidate;
      }
    }

    return null;
  };

  const findTurnContainer = (control) => {
    return control.closest("article,[data-testid*='conversation-turn'],li[data-message-author-role],div[data-message-author-role]")
      || document;
  };

  const findPreByAncestor = (control) => {
    let node = control;
    for (let index = 0; index < 10 && node; ++index, node = node.parentElement) {
      const preOrCode = node.querySelector?.("pre code, pre");
      if (preOrCode) {
        return preOrCode.closest("pre") || preOrCode;
      }
    }
    return null;
  };

  const findNearestVisiblePre = (control) => {
    const root = findTurnContainer(control);
    const pres = Array.from(root.querySelectorAll("pre"));
    if (pres.length === 0) {
      return null;
    }

    const controlRect = control.getBoundingClientRect();
    const controlCenterX = controlRect.left + controlRect.width / 2;
    const controlCenterY = controlRect.top + controlRect.height / 2;

    let best = null;
    let bestDistance = Number.POSITIVE_INFINITY;
    for (const pre of pres) {
      const rect = pre.getBoundingClientRect();
      if (rect.width === 0 || rect.height === 0) {
        continue;
      }

      const preCenterX = rect.left + rect.width / 2;
      const preCenterY = rect.top + rect.height / 2;
      const dx = controlCenterX - preCenterX;
      const dy = controlCenterY - preCenterY;
      const distance = dx * dx + dy * dy;
      if (distance < bestDistance) {
        bestDistance = distance;
        best = pre;
      }
    }
    return best;
  };

  const extractCodeText = (control) => {
    const pre = findPreByAncestor(control) || findNearestVisiblePre(control);
    if (!pre) {
      return "";
    }
    const code = pre.querySelector("code");
    const text = code ? (code.textContent || "") : (pre.textContent || "");
    return text.replace(/\r\n/g, "\n");
  };

  const encodeTextAsBase64 = (text) => {
    if (typeof text !== "string" || text.length === 0) {
      return "";
    }

    const utf8 = new TextEncoder().encode(text);
    let binary = "";
    const chunkSize = 0x4000;
    for (let start = 0; start < utf8.length; start += chunkSize) {
      const end = Math.min(start + chunkSize, utf8.length);
      let chunk = "";
      for (let index = start; index < end; ++index) {
        chunk += String.fromCharCode(utf8[index]);
      }
      binary += chunk;
    }
    return btoa(binary);
  };

  const sendNativeCopy = (text) => {
    const base64 = encodeTextAsBase64(text);
    if (!base64 || !nativePrompt) {
      return false;
    }

    try {
      // Native bridge returns "ok" after validated clipboard commit
      const response = nativePrompt(`${copyPrefix}${base64}`, "");
      return response === "ok";
    } catch (_) {
      return false;
    }
  };

  document.addEventListener("pointerdown", (event) => {
    const control = findControlFromEvent(event);
    if (!control) {
      return;
    }

    const codeText = extractCodeText(control);
    if (!codeText || !codeText.trim()) {
      return;
    }

    // Only suppress the site handler when the native bridge succeeded
    const wasCopied = sendNativeCopy(codeText);
    if (!wasCopied) {
      return;
    }

    event.preventDefault();
    event.stopImmediatePropagation();

    setTimeout(() => {
      sendNativeCopy(codeText);
    }, 150);
  }, true);
})();
)JS");
    // Prefix substitution keeps the JS source static while rotating secrets per process
    codeCopyBridgeSource.replace(QStringLiteral("__CHATGPT_DESKTOP_COPY_PREFIX_PLACEHOLDER__"),
                                 clipboardBridgePrefix);
    codeCopyBridgeScript.setSourceCode(codeCopyBridgeSource);
    webPage->scripts().insert(codeCopyBridgeScript);

    QWebEngineSettings *webSettings = settings();
    if (webSettings != nullptr) {
        webSettings->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, true);
        webSettings->setAttribute(QWebEngineSettings::JavascriptCanPaste, true);
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
