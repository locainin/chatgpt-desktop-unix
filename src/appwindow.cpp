#include "appwindow.h"
#include "chatview.h"
#include <QString>

namespace {
// Keep the fallback title short and stable
const QString kDefaultWindowTitle = QStringLiteral("ChatGPT Desktop");
// Add a small suffix so branch windows still look native
const QString kWindowTitleSuffix = QStringLiteral(" - ChatGPT Desktop");
} // namespace

AppWindow::AppWindow(const QUrl &initialUrl, QWidget *parent) : QMainWindow(parent) {
  // Every top level window owns one web view
  chatView = new ChatView(initialUrl, this);
  setCentralWidget(chatView);

  // Follow the active page title so each branch is easy to spot
  connect(chatView, &QWebEngineView::titleChanged, this, [this](const QString &pageTitle) {
    UpdateWindowTitle(pageTitle);
  });

  UpdateWindowTitle(QString());
  resize(1000, 700);
}

ChatView *AppWindow::GetChatView() const { return chatView; }

void AppWindow::UpdateWindowTitle(const QString &pageTitle) {
  // Empty titles show up during early page load
  if (pageTitle.trimmed().isEmpty()) {
    setWindowTitle(kDefaultWindowTitle);
    return;
  }

  setWindowTitle(pageTitle + kWindowTitleSuffix);
}
