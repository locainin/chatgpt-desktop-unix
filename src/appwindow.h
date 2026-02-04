#pragma once
#include <QMainWindow>

class ChatView;
class QCloseEvent;

class AppWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit AppWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    ChatView *chatView;
};
