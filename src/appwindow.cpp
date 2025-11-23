#include "appwindow.h"
#include "chatview.h"

AppWindow::AppWindow(QWidget *parent)
    : QMainWindow(parent)
{
    chatView = new ChatView(this);
    setCentralWidget(chatView);

    setWindowTitle("ChatGPT Desktop (Unofficial)");
    resize(1000, 700);
}
