#pragma once
#include <QWebEngineView>
#include <QWebEngineProfile>
#include <QWebEngineCookieStore>
#include <memory>

class QWebEngineDownloadRequest;
class QLockFile;
class QTimer;

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
    void MarkPersistentStateDirty();
    void FlushPersistentStateAsync();

    QWebEngineProfile *m_profile = nullptr;
    QWebEngineCookieStore *m_cookieStore = nullptr;
    // Debounce timer collapses bursty cookie updates
    QTimer *m_persistenceDebounceTimer = nullptr;
    // Lock prevents concurrent writes to shared profile databases
    std::unique_ptr<QLockFile> m_profileLock;
    // Dirty tracks pending persistence writes
    bool m_persistenceDirty = false;
    // Shutdown guard avoids repeated sync waits
    bool m_shutdownFlushComplete = false;
    // Flush guard prevents re-entrant persistence calls
    bool m_flushInProgress = false;
};
