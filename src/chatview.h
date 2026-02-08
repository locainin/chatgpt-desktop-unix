#pragma once
#include <QWebEngineView>
#include <QWebEngineProfile>
#include <QWebEngineCookieStore>

class QWebEngineDownloadRequest;

class ChatView : public QWebEngineView {
    Q_OBJECT
public:
    explicit ChatView(QWidget *parent = nullptr);
    ~ChatView() override;

    // Force a short synchronous flush of WebEngine persistent data
    void FlushPersistentStateSync();

private:
    void HandleDownloadRequest(QWebEngineDownloadRequest *download);
    QString DownloadDirectoryPath() const;

    QWebEngineProfile *m_profile = nullptr;
    QWebEngineCookieStore *m_cookieStore = nullptr;
};
