#pragma once
#include <QUrl>
#include <QWebEngineView>
#include <QWebEngineProfile>

class QWebEngineDownloadRequest;
class QEvent;
class QHideEvent;
class QShowEvent;

class ChatView : public QWebEngineView {
public:
  explicit ChatView(const QUrl &initialUrl = QUrl(), QWidget *parent = nullptr);
  ~ChatView() override = default;

protected:
  // Open site requested windows inside another native app window
  QWebEngineView *createWindow(QWebEnginePage::WebWindowType type) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  void changeEvent(QEvent *event) override;

private:
  // Coalesce repeated window events into one lifecycle update
  void SchedulePageLifecycleStateUpdate();
  // Freeze the page only when the window is hidden or minimized
  void UpdatePageLifecycleState();
  // Keep downloads in a native save dialog
  void HandleDownloadRequest(QWebEngineDownloadRequest *download);
  // Pick a stable download folder before the save dialog opens
  QString DownloadDirectoryPath() const;

  // Shared profile is owned by the app level profile manager
  QWebEngineProfile *m_profile = nullptr;
  bool m_lifecycleUpdateScheduled = false;
};
