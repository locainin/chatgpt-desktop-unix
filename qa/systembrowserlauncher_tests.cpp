#include "../src/systembrowserlauncher.h"

#include <QCoreApplication>
#include <QDebug>
#include <QUrl>

namespace {

void Check(bool condition, const QString &message, int &failures) {
  if (condition) {
    return;
  }
  qWarning() << "Test failed:" << message;
  failures++;
}

} // namespace

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);
  int failures = 0;
  const QUrl chatGptUrl(QStringLiteral("https://chatgpt.com"));
  const QStringList arguments = SystemBrowserLauncher::BuildArguments(chatGptUrl);

  // Product behavior depends on using browser app mode rather than Qt login
  Check(arguments.contains(QStringLiteral("--app=https://chatgpt.com")),
        QStringLiteral("App-mode argument should load ChatGPT"), failures);
  Check(arguments.contains(QStringLiteral("--class=chatgpt-desktop-unix")),
        QStringLiteral("Window class should match desktop integration"), failures);
  // Separate profiles and debugging ports brought back the blocked sign-in path
  Check(!arguments.join(QLatin1Char(' ')).contains(QStringLiteral("--user-data-dir")),
        QStringLiteral("Launcher should reuse the normal Chromium profile"), failures);
  Check(!arguments.contains(QStringLiteral("--remote-debugging-port=0")),
        QStringLiteral("Normal use should not expose browser debugging"), failures);

  if (failures != 0) {
    qWarning() << "System browser launcher tests failed count:" << failures;
    return 1;
  }
  qInfo() << "All system browser launcher tests passed";
  return 0;
}
