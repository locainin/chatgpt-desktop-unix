#pragma once
#include <QWebEngineView>
#include <QWebEngineProfile>
#include <QWebEngineCookieStore>

class ChatView : public QWebEngineView {
    Q_OBJECT
public:
    explicit ChatView(QWidget *parent = nullptr);
    ~ChatView() override;

    // Force a short synchronous flush of WebEngine persistent data
    void FlushPersistentStateSync();

private:
    QWebEngineProfile *m_profile = nullptr;
    QWebEngineCookieStore *m_cookieStore = nullptr;
};
