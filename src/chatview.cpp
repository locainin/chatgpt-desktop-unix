#include "chatview.h"
#include "appwindow.h"
#include "browserprofile.h"
#include "chatinjections.h"
#include "chatwebpage.h"
#include "trustedorigins.h"
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHideEvent>
#include <QShowEvent>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QWebEngineDownloadRequest>
#include <QWebEnginePage>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QtGlobal>

namespace {
// Fresh windows start at the normal ChatGPT home page
QUrl DefaultStartupUrl() { return QUrl(QStringLiteral("https://chatgpt.com")); }
} // namespace

ChatView::ChatView(const QUrl &initialUrl, QWidget *parent) : QWebEngineView(parent) {
  // One shared profile keeps every native window on the same login state
  BrowserProfile &browserProfile = BrowserProfile::Instance();
  m_profile = browserProfile.Profile();
  const QString clipboardBridgePrefix = browserProfile.ClipboardBridgePrefix();

  // Each window still owns its own page object
  // That keeps window state separate while storage stays shared
  ChatWebPage *webPage = new ChatWebPage(m_profile, clipboardBridgePrefix, this);
  setPage(webPage);

  QWebEngineScript trustedOriginsScript;
  trustedOriginsScript.setName(QStringLiteral("chatgpt-desktop-trusted-origins"));
  trustedOriginsScript.setInjectionPoint(QWebEngineScript::DocumentCreation);
  trustedOriginsScript.setRunsOnSubFrames(false);
  trustedOriginsScript.setWorldId(QWebEngineScript::ApplicationWorld);
  // Load the trust helper first so later scripts can ask one shared source
  trustedOriginsScript.setSourceCode(ChatInjections::BuildTrustedOriginsScriptSource());
  webPage->scripts().insert(trustedOriginsScript);

  QWebEngineScript codeCopyBridgeScript;
  codeCopyBridgeScript.setName(QStringLiteral("chatgpt-desktop-code-copy-bridge"));
  codeCopyBridgeScript.setInjectionPoint(QWebEngineScript::DocumentCreation);
  codeCopyBridgeScript.setRunsOnSubFrames(false);
  codeCopyBridgeScript.setWorldId(QWebEngineScript::ApplicationWorld);
  // Keep the injected bridge logic in JS files instead of hard coding it here
  codeCopyBridgeScript.setSourceCode(ChatInjections::BuildCodeCopyBridgeScriptSource(clipboardBridgePrefix));
  webPage->scripts().insert(codeCopyBridgeScript);

  QWebEngineScript longChatPerfScript;
  longChatPerfScript.setName(QStringLiteral("chatgpt-desktop-long-chat-perf"));
  longChatPerfScript.setInjectionPoint(QWebEngineScript::DocumentReady);
  longChatPerfScript.setRunsOnSubFrames(false);
  longChatPerfScript.setWorldId(QWebEngineScript::ApplicationWorld);
  // Wait for the page tree before touching long chat nodes
  longChatPerfScript.setSourceCode(ChatInjections::BuildLongChatPerformanceScriptSource());
  webPage->scripts().insert(longChatPerfScript);

  QWebEngineSettings *webSettings = settings();
  auto updateClipboardPermissions = [webSettings](const QUrl &url) {
    if (webSettings == nullptr) {
      return;
    }

    // Clipboard access stays off until the current page matches the allow list
    const bool isTrusted = TrustedOrigins::IsTrustedHttpsUrl(url);
    webSettings->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, isTrusted);
    webSettings->setAttribute(QWebEngineSettings::JavascriptCanPaste, isTrusted);
  };

  if (webSettings != nullptr) {
    // Apply the first page policy before any navigation happens
    updateClipboardPermissions(webPage->url());
    // Keep permissions in sync as the page moves across origins
    QObject::connect(webPage, &QWebEnginePage::urlChanged, this, updateClipboardPermissions);
    // Unknown schemes should not jump out to unsafe handlers
    webSettings->setUnknownUrlSchemePolicy(QWebEngineSettings::DisallowUnknownUrlSchemes);
    // Smooth scrolling costs extra paint work in very long chats
    webSettings->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, false);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // Let animated images settle after one run instead of looping forever
    webSettings->setImageAnimationPolicy(QWebEngineSettings::ImageAnimationPolicy::AnimateOnce);
#endif
  }

  // Route browser downloads through one native save path
  QObject::connect(m_profile, &QWebEngineProfile::downloadRequested, this,
                   [this](QWebEngineDownloadRequest *download) {
                     // The shared profile reports downloads for every window
                     // Only the page that started the download should own the dialog
                     if (download == nullptr || download->page() != page()) {
                       return;
                     }
                     HandleDownloadRequest(download);
                   });

  load(initialUrl.isValid() ? initialUrl : DefaultStartupUrl());

  // Start with one lifecycle pass so hidden startup cases do the right thing
  SchedulePageLifecycleStateUpdate();
}

QString ChatView::DownloadDirectoryPath() const {
  QString downloadDirectory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  if (downloadDirectory.isEmpty()) {
    // Some setups do not report a download folder through XDG paths
    downloadDirectory = QDir::homePath() + QDir::separator() + QStringLiteral("Downloads");
  }
  if (!QDir().mkpath(downloadDirectory)) {
    qWarning() << "Failed to create download directory:" << downloadDirectory;
  }
  return downloadDirectory;
}

void ChatView::HandleDownloadRequest(QWebEngineDownloadRequest *download) {
  // Qt can still fire this while a page is shutting down
  if (download == nullptr) {
    return;
  }

  const QString suggestedName =
      download->downloadFileName().isEmpty() ? QStringLiteral("download") : download->downloadFileName();
  // Build the first file path from the browser suggestion and native download dir
  const QString suggestedPath = QDir(DownloadDirectoryPath()).filePath(suggestedName);

  const QString selectedPath = QFileDialog::getSaveFileName(this, tr("Save File"), suggestedPath);
  if (selectedPath.isEmpty()) {
    // A closed dialog means the download should stop
    download->cancel();
    return;
  }

  QObject::connect(download, &QWebEngineDownloadRequest::stateChanged, this,
                   [download](QWebEngineDownloadRequest::DownloadState state) {
                     if (state == QWebEngineDownloadRequest::DownloadInterrupted ||
                         state == QWebEngineDownloadRequest::DownloadCancelled) {
                       // Keep a clear log line when the browser stops a download
                       qWarning() << "Download failed:" << download->url() << "state:" << state
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
  // Split the chosen path back into the pieces Qt expects
  download->setDownloadDirectory(selectedInfo.absolutePath());
  download->setDownloadFileName(selectedInfo.fileName());
  download->accept();
}

QWebEngineView *ChatView::createWindow(QWebEnginePage::WebWindowType type) {
  Q_UNUSED(type);

  // Some page actions ask Chromium for a new top level window
  // Keep that inside the app instead of handing it off to the desktop browser
  AppWindow *branchWindow = new AppWindow(QUrl(QStringLiteral("about:blank")));
  // Popup windows live on the heap, so close can delete them safely
  branchWindow->setAttribute(Qt::WA_DeleteOnClose);
  // Match the current window size so the branch feels like a continuation
  branchWindow->resize(window() != nullptr ? window()->size() : QSize(1000, 700));
  branchWindow->show();
  branchWindow->raise();
  branchWindow->activateWindow();
  return branchWindow->GetChatView();
}

void ChatView::showEvent(QShowEvent *event) {
  QWebEngineView::showEvent(event);
  SchedulePageLifecycleStateUpdate();
}

void ChatView::hideEvent(QHideEvent *event) {
  QWebEngineView::hideEvent(event);
  SchedulePageLifecycleStateUpdate();
}

void ChatView::changeEvent(QEvent *event) {
  QWebEngineView::changeEvent(event);
  if (event != nullptr && event->type() == QEvent::WindowStateChange) {
    SchedulePageLifecycleStateUpdate();
  }
}

void ChatView::SchedulePageLifecycleStateUpdate() {
  if (m_lifecycleUpdateScheduled) {
    return;
  }

  m_lifecycleUpdateScheduled = true;
  // Show, hide, and minimize often land as a small burst
  // One queued pass is enough to see the final window state
  QTimer::singleShot(0, this, [this]() {
    m_lifecycleUpdateScheduled = false;
    UpdatePageLifecycleState();
  });
}

void ChatView::UpdatePageLifecycleState() {
  // Hidden pages do not need to keep repainting and running full speed
  QWebEnginePage *currentPage = page();
  if (currentPage == nullptr) {
    return;
  }

  bool shouldFreeze = !isVisible();
  QWidget *topLevelWindow = window();
  if (topLevelWindow != nullptr && topLevelWindow->isMinimized()) {
    // Minimized windows are also out of view, so freeze them too
    shouldFreeze = true;
  }

  const QWebEnginePage::LifecycleState currentState = currentPage->lifecycleState();
  if (shouldFreeze && currentState == QWebEnginePage::LifecycleState::Active) {
    // Move to Frozen only when the page is still active
    currentPage->setLifecycleState(QWebEnginePage::LifecycleState::Frozen);
    return;
  }

  if (!shouldFreeze && currentState != QWebEnginePage::LifecycleState::Active) {
    // Wake the page back up when the window is visible again
    currentPage->setLifecycleState(QWebEnginePage::LifecycleState::Active);
  }
}
