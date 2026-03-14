#include <QApplication>
#include <QCoreApplication>
#include <QSocketNotifier>
#include <QUrl>
#include <csignal>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include "appwindow.h"

// Signal bridge for graceful shutdown on SIGINT and SIGTERM
static int signalPipeFileDescriptors[2] = {-1, -1};
static QSocketNotifier *signalSocketNotifier = nullptr;

static void InstallSignalHandlers(QCoreApplication *application);
static bool ConfigureSignalPipe();
static void CloseSignalPipe();
static void HandleSignal(int signalNumber);
static void HandleSignalNotification();
static QUrl ResolveInitialUrl();

int main(int argc, char *argv[]) {
  // Create the GUI app before any WebEngine objects are touched
  QApplication app(argc, argv);

  // Stable application identity for persistent storage paths
  QCoreApplication::setOrganizationName(QStringLiteral("chatgpt-desktop-unix"));
  QCoreApplication::setOrganizationDomain(QStringLiteral("local"));
  QCoreApplication::setApplicationName(QStringLiteral("chatgpt-desktop-unix"));

  // Map Ctrl+C and service stop signals into a normal Qt quit
  InstallSignalHandlers(&app);

  // Tests can point the first window at a small local page
  // Normal runs still use the built-in default start page
  AppWindow window(ResolveInitialUrl());
  window.show();

  return app.exec();
}

static QUrl ResolveInitialUrl() {
  const QString overrideValue = qEnvironmentVariable("CHATGPT_DESKTOP_START_URL");
  if (overrideValue.trimmed().isEmpty()) {
    return QUrl();
  }

  // Accept plain URLs and encoded data URLs from the test harness
  const QUrl directUrl(overrideValue);
  if (directUrl.isValid() && !directUrl.isEmpty()) {
    return directUrl;
  }

  const QUrl userInputUrl = QUrl::fromUserInput(overrideValue);
  if (userInputUrl.isValid() && !userInputUrl.isEmpty()) {
    return userInputUrl;
  }

  return QUrl();
}

static void InstallSignalHandlers(QCoreApplication *application) {
  if (application == nullptr) {
    return;
  }

  // Route signals through a pipe into the Qt event loop
  if (!ConfigureSignalPipe()) {
    return;
  }

  signalSocketNotifier =
      new QSocketNotifier(signalPipeFileDescriptors[0], QSocketNotifier::Read, application);
  // Pipe activity means one of the watched signals arrived
  QObject::connect(signalSocketNotifier, &QSocketNotifier::activated,
                   []([[maybe_unused]] int socketDescriptor) { HandleSignalNotification(); });

  struct sigaction action{};
  action.sa_handler = HandleSignal;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;

  if (sigaction(SIGINT, &action, nullptr) != 0) {
    CloseSignalPipe();
    return;
  }
  if (sigaction(SIGTERM, &action, nullptr) != 0) {
    CloseSignalPipe();
    return;
  }

  // Always close both pipe ends before the process exits
  QObject::connect(application, &QCoreApplication::aboutToQuit, application, []() { CloseSignalPipe(); });
}

static bool ConfigureSignalPipe() {
  // Create the self-pipe used by the async-signal-safe bridge
  if (::pipe(signalPipeFileDescriptors) != 0) {
    return false;
  }

  // Read/write descriptors must be nonblocking to prevent signal-handler stalls
  const int readFlags = ::fcntl(signalPipeFileDescriptors[0], F_GETFL, 0);
  const int writeFlags = ::fcntl(signalPipeFileDescriptors[1], F_GETFL, 0);
  if (readFlags == -1 || writeFlags == -1) {
    CloseSignalPipe();
    return false;
  }

  const bool readConfigured = (::fcntl(signalPipeFileDescriptors[0], F_SETFL, readFlags | O_NONBLOCK) == 0);
  const bool writeConfigured = (::fcntl(signalPipeFileDescriptors[1], F_SETFL, writeFlags | O_NONBLOCK) == 0);
  if (!readConfigured || !writeConfigured) {
    CloseSignalPipe();
    return false;
  }

  // Close-on-exec avoids leaking descriptors into spawned subprocesses
  const bool readCloseOnExec = (::fcntl(signalPipeFileDescriptors[0], F_SETFD, FD_CLOEXEC) == 0);
  const bool writeCloseOnExec = (::fcntl(signalPipeFileDescriptors[1], F_SETFD, FD_CLOEXEC) == 0);
  if (!readCloseOnExec || !writeCloseOnExec) {
    CloseSignalPipe();
    return false;
  }

  return true;
}

static void CloseSignalPipe() {
  if (signalPipeFileDescriptors[0] != -1) {
    ::close(signalPipeFileDescriptors[0]);
    signalPipeFileDescriptors[0] = -1;
  }
  if (signalPipeFileDescriptors[1] != -1) {
    ::close(signalPipeFileDescriptors[1]);
    signalPipeFileDescriptors[1] = -1;
  }
}

static void HandleSignal(int signalNumber) {
  if (signalPipeFileDescriptors[1] == -1) {
    return;
  }

  // Signal context only writes a byte marker to the pipe
  const char signalByte = static_cast<char>(signalNumber);
  const ssize_t bytesWritten = ::write(signalPipeFileDescriptors[1], &signalByte, sizeof(signalByte));
  // Full pipe during bursts is expected and can be ignored
  if (bytesWritten < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
    return;
  }
}

static void HandleSignalNotification() {
  if (signalSocketNotifier == nullptr) {
    return;
  }

  // Stop notifier callbacks while the pipe gets drained
  signalSocketNotifier->setEnabled(false);

  char signalByte = 0;
  // Drain all pending signal bytes so repeated signals collapse into one quit
  while (::read(signalPipeFileDescriptors[0], &signalByte, sizeof(signalByte)) > 0) {
  }

  // One Qt quit is enough even if several signals arrived
  QCoreApplication::quit();
  signalSocketNotifier->setEnabled(true);
}
