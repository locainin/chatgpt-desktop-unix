#pragma once

#include <QString>
#include <QStringList>

class QUrl;

namespace SystemBrowserLauncher {

// Find a full browser binary path without invoking a shell
QString ResolveExecutable();
// Keep command-line policy separate so launch behavior can be regression tested
QStringList BuildArguments(const QUrl &url);
// Start the browser-backed application window for normal desktop use
bool LaunchChatGpt(QString *errorMessage);

} // namespace SystemBrowserLauncher
