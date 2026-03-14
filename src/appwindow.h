#pragma once
#include <QMainWindow>
#include <QUrl>

class ChatView;
class QString;

class AppWindow : public QMainWindow {
public:
  explicit AppWindow(const QUrl &initialUrl = QUrl(), QWidget *parent = nullptr);
  ChatView *GetChatView() const;

private:
  // Keep the window title close to the active page title
  void UpdateWindowTitle(const QString &pageTitle);

  // Qt owns this child after setCentralWidget
  ChatView *chatView;
};
