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
// Default to the main ChatGPT home page on a fresh window
QUrl DefaultStartupUrl() { return QUrl(QStringLiteral("https://chatgpt.com")); }
} // namespace

ChatView::ChatView(const QUrl &initialUrl, QWidget *parent) : QWebEngineView(parent) {
  // One shared profile keeps every native window logged into the same session
  BrowserProfile &browserProfile = BrowserProfile::Instance();
  m_profile = browserProfile.Profile();
  const QString clipboardBridgePrefix = browserProfile.ClipboardBridgePrefix();

  // Each window still gets its own page object and injected scripts
  ChatWebPage *webPage = new ChatWebPage(m_profile, clipboardBridgePrefix, this);
  setPage(webPage);

  QWebEngineScript trustedOriginsScript;
  trustedOriginsScript.setName(QStringLiteral("chatgpt-desktop-trusted-origins"));
  trustedOriginsScript.setInjectionPoint(QWebEngineScript::DocumentCreation);
  trustedOriginsScript.setRunsOnSubFrames(false);
  trustedOriginsScript.setWorldId(QWebEngineScript::ApplicationWorld);
  // Load the shared browser-side trust helper before the feature scripts
  trustedOriginsScript.setSourceCode(ChatInjections::BuildTrustedOriginsScriptSource());
  webPage->scripts().insert(trustedOriginsScript);

  QWebEngineScript codeCopyBridgeScript;
  codeCopyBridgeScript.setName(QStringLiteral("chatgpt-desktop-code-copy-bridge"));
  codeCopyBridgeScript.setInjectionPoint(QWebEngineScript::DocumentCreation);
  codeCopyBridgeScript.setRunsOnSubFrames(false);
  codeCopyBridgeScript.setWorldId(QWebEngineScript::ApplicationWorld);
  // Load JS text from resource files
  codeCopyBridgeScript.setSourceCode(ChatInjections::BuildCodeCopyBridgeScriptSource(clipboardBridgePrefix));
  webPage->scripts().insert(codeCopyBridgeScript);

  QWebEngineScript longChatPerfScript;
  longChatPerfScript.setName(QStringLiteral("chatgpt-desktop-long-chat-perf"));
  longChatPerfScript.setInjectionPoint(QWebEngineScript::DocumentReady);
  longChatPerfScript.setRunsOnSubFrames(false);
  longChatPerfScript.setWorldId(QWebEngineScript::ApplicationWorld);
  // Run performance JS after the page is ready
  longChatPerfScript.setSourceCode(ChatInjections::BuildLongChatPerformanceScriptSource());
  webPage->scripts().insert(longChatPerfScript);

  QWebEngineSettings *webSettings = settings();
  auto updateClipboardPermissions = [webSettings](const QUrl &url) {
    if (webSettings == nullptr) {
      return;
    }

    // Clipboard access stays off until the current page is trusted
    const bool isTrusted = TrustedOrigins::IsTrustedHttpsUrl(url);
    webSettings->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, isTrusted);
    webSettings->setAttribute(QWebEngineSettings::JavascriptCanPaste, isTrusted);
  };

  if (webSettings != nullptr) {
    updateClipboardPermissions(webPage->url());
    QObject::connect(webPage, &QWebEnginePage::urlChanged, this, updateClipboardPermissions);
    webSettings->setUnknownUrlSchemePolicy(QWebEngineSettings::DisallowUnknownUrlSchemes);
    webSettings->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, false);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    webSettings->setImageAnimationPolicy(QWebEngineSettings::ImageAnimationPolicy::AnimateOnce);
#endif
  }

  // Route all browser-triggered downloads through native save handling.
  QObject::connect(m_profile, &QWebEngineProfile::downloadRequested, this,
                   [this](QWebEngineDownloadRequest *download) {
                     // The shared profile reports downloads for every page
                     // Only the page that started the download should show the save dialog
                     if (download == nullptr || download->page() != page()) {
                       return;
                     }
                     HandleDownloadRequest(download);
                   });

  load(initialUrl.isValid() ? initialUrl : DefaultStartupUrl());

  // Keep lifecycle active while visible, and freeze only when hidden/minimized
  QTimer::singleShot(0, this, [this]() { UpdatePageLifecycleState(); });
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
  // Qt can emit the signal even if a page goes away during shutdown
  if (download == nullptr) {
    return;
  }

  const QString suggestedName =
      download->downloadFileName().isEmpty() ? QStringLiteral("download") : download->downloadFileName();
  const QString suggestedPath = QDir(DownloadDirectoryPath()).filePath(suggestedName);

  const QString selectedPath = QFileDialog::getSaveFileName(this, tr("Save File"), suggestedPath);
  if (selectedPath.isEmpty()) {
    // Closing the dialog means the download should stop cleanly
    download->cancel();
    return;
  }

  QObject::connect(download, &QWebEngineDownloadRequest::stateChanged, this,
                   [download](QWebEngineDownloadRequest::DownloadState state) {
                     if (state == QWebEngineDownloadRequest::DownloadInterrupted ||
                         state == QWebEngineDownloadRequest::DownloadCancelled) {
                       // Explicit diagnostics prevent silent failed downloads
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
  download->setDownloadDirectory(selectedInfo.absolutePath());
  download->setDownloadFileName(selectedInfo.fileName());
  download->accept();
}

QWebEngineView *ChatView::createWindow(QWebEnginePage::WebWindowType type) {
  Q_UNUSED(type);

  // Some ChatGPT actions ask the browser for a new window
  // Create another native window so those flows stay inside the app
  AppWindow *branchWindow = new AppWindow(QUrl(QStringLiteral("about:blank")));
  // Popup windows are heap owned, so close can delete them safely
  branchWindow->setAttribute(Qt::WA_DeleteOnClose);
  branchWindow->resize(window() != nullptr ? window()->size() : QSize(1000, 700));
  branchWindow->show();
  branchWindow->raise();
  branchWindow->activateWindow();
  return branchWindow->GetChatView();
}

void ChatView::showEvent(QShowEvent *event) {
  QWebEngineView::showEvent(event);
  UpdatePageLifecycleState();
}

void ChatView::hideEvent(QHideEvent *event) {
  QWebEngineView::hideEvent(event);
  UpdatePageLifecycleState();
}

void ChatView::changeEvent(QEvent *event) {
  QWebEngineView::changeEvent(event);
  if (event != nullptr && event->type() == QEvent::WindowStateChange) {
    UpdatePageLifecycleState();
  }
}

void ChatView::UpdatePageLifecycleState() {
  // Pages that keep rendering in hidden windows waste CPU and memory
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
