#include "chatview.h"

ChatView::ChatView(QWidget *parent)
    : QWebEngineView(parent)
{
    load(QUrl("https://chatgpt.com"));  // placeholder
}
