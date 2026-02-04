#include "appwindow.h"
#include "chatview.h"
#include <QCloseEvent>

AppWindow::AppWindow(QWidget *parent)
    : QMainWindow(parent)
{
    chatView = new ChatView(this);
    setCentralWidget(chatView);

    setWindowTitle("ChatGPT Desktop (Unofficial)");
    resize(1000, 700);
}

void AppWindow::closeEvent(QCloseEvent *event)
{
    if (chatView != nullptr) {
        // Final flush for persistence before accepting close
        chatView->FlushPersistentStateSync();
    }
    event->accept();
}
