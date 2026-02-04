#include <QApplication>
#include <QCoreApplication>
#include <QSocketNotifier>
#include <csignal>
#include <unistd.h>
#include "appwindow.h"

// Signal bridge for graceful shutdown on SIGINT and SIGTERM
static int signalPipeFileDescriptors[2] = {-1, -1};
static QSocketNotifier *signalSocketNotifier = nullptr;

static void InstallSignalHandlers(QCoreApplication *application);
static void HandleSignal(int signalNumber);
static void HandleSignalNotification();

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Stable application identity for persistent storage paths
    QCoreApplication::setOrganizationName(QStringLiteral("chatgpt-desktop-unix"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("local"));
    QCoreApplication::setApplicationName(QStringLiteral("chatgpt-desktop-unix"));

    InstallSignalHandlers(&app);

    AppWindow window;
    window.show();

    return app.exec();
}

static void InstallSignalHandlers(QCoreApplication *application)
{
    if (application == nullptr) {
        return;
    }

    // Route signals through a pipe into the Qt event loop
    if (::pipe(signalPipeFileDescriptors) != 0) {
        return;
    }

    signalSocketNotifier = new QSocketNotifier(signalPipeFileDescriptors[0], QSocketNotifier::Read, application);
    QObject::connect(signalSocketNotifier, &QSocketNotifier::activated, []([[maybe_unused]] int socketDescriptor) {
        HandleSignalNotification();
    });

    struct sigaction action;
    action.sa_handler = HandleSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
}

static void HandleSignal(int signalNumber)
{
    if (signalPipeFileDescriptors[1] == -1) {
        return;
    }

    const char signalByte = static_cast<char>(signalNumber);
    const ssize_t bytesWritten = ::write(signalPipeFileDescriptors[1], &signalByte, sizeof(signalByte));
    (void)bytesWritten;
}

static void HandleSignalNotification()
{
    if (signalSocketNotifier == nullptr) {
        return;
    }

    signalSocketNotifier->setEnabled(false);

    char signalByte = 0;
    const ssize_t bytesRead = ::read(signalPipeFileDescriptors[0], &signalByte, sizeof(signalByte));
    (void)bytesRead;

    QCoreApplication::quit();
    signalSocketNotifier->setEnabled(true);
}
