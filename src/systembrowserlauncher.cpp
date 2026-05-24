#include "systembrowserlauncher.h"

#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace {
// The installed launcher always opens the production ChatGPT origin
const QString kChatGptUrl = QStringLiteral("https://chatgpt.com");
} // namespace

namespace SystemBrowserLauncher {

QString ResolveExecutable() {
  // Prefer the common Chromium family names available on Linux desktops
  const QStringList candidates = {QStringLiteral("chromium"), QStringLiteral("google-chrome-stable"),
                                  QStringLiteral("google-chrome"), QStringLiteral("chrome"),
                                  QStringLiteral("brave-browser")};
  for (const QString &candidate : candidates) {
    // Store the resolved path so no shell parsing is involved later
    const QString executablePath = QStandardPaths::findExecutable(candidate);
    if (!executablePath.isEmpty()) {
      return executablePath;
    }
  }
  return QString();
}

QStringList BuildArguments(const QUrl &url) {
  // App mode keeps browser controls out while preserving the regular browser session
  return {QStringLiteral("--no-first-run"), QStringLiteral("--no-default-browser-check"),
          QStringLiteral("--class=chatgpt-desktop-unix"),
          QStringLiteral("--app=%1").arg(url.toString())};
}

bool LaunchChatGpt(QString *errorMessage) {
  // Fail visibly instead of silently falling back to the blocked embedded login page
  const QString executablePath = ResolveExecutable();
  if (executablePath.isEmpty()) {
    if (errorMessage != nullptr) {
      *errorMessage = QStringLiteral("Chromium or Google Chrome is required to run ChatGPT Desktop.");
    }
    return false;
  }

  qint64 processId = 0;
  // Use the normal browser profile so sign-in matches regular Chromium behavior
  // Detached launch lets the application window live after this small launcher exits
  if (!QProcess::startDetached(executablePath, BuildArguments(QUrl(kChatGptUrl)), QString(), &processId)) {
    if (errorMessage != nullptr) {
      *errorMessage = QStringLiteral("Could not start Chromium for ChatGPT Desktop.");
    }
    return false;
  }
  return true;
}

} // namespace SystemBrowserLauncher
