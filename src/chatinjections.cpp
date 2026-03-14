#include "chatinjections.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QIODevice>

namespace {
QString ResolveFilesystemScriptPath(const QString &resourcePath) {
  // Direct g++ builds do not run rcc, so the scripts need a disk fallback
  if (resourcePath == QStringLiteral(":/scripts/code-copy-bridge.js")) {
    return QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("../resources/scripts/code-copy-bridge.js"));
  }
  if (resourcePath == QStringLiteral(":/scripts/trusted-hosts.js")) {
    return QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("../resources/scripts/trusted-hosts.js"));
  }
  if (resourcePath == QStringLiteral(":/scripts/long-chat-performance.js")) {
    return QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("../resources/scripts/long-chat-performance.js"));
  }
  return QString();
}

QString LoadScriptFromResource(const QString &resourcePath) {
  // Read JS from app resources so paths always work
  QFile scriptFile(resourcePath);
  if (!scriptFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    const QString fallbackPath = ResolveFilesystemScriptPath(resourcePath);
    if (fallbackPath.isEmpty()) {
      qWarning() << "Failed to open script resource:" << resourcePath;
      return QString();
    }

    QFile fallbackFile(fallbackPath);
    if (!fallbackFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
      qWarning() << "Failed to open script resource and fallback path:" << resourcePath << fallbackPath;
      return QString();
    }

    const QByteArray fallbackBytes = fallbackFile.readAll();
    if (fallbackBytes.isEmpty()) {
      qWarning() << "Loaded empty fallback script file:" << fallbackPath;
      return QString();
    }

    return QString::fromUtf8(fallbackBytes);
  }

  const QByteArray scriptBytes = scriptFile.readAll();
  if (scriptBytes.isEmpty()) {
    qWarning() << "Loaded empty script resource:" << resourcePath;
    return QString();
  }

  return QString::fromUtf8(scriptBytes);
}

} // namespace

namespace ChatInjections {

QString BuildTrustedOriginsScriptSource() {
  // Load the shared trust helper before other injected scripts run
  return LoadScriptFromResource(QStringLiteral(":/scripts/trusted-hosts.js"));
}

QString BuildCodeCopyBridgeScriptSource(const QString &clipboardBridgePrefix) {
  // Replace placeholder with a random prefix for this app run
  QString script = LoadScriptFromResource(QStringLiteral(":/scripts/code-copy-bridge.js"));
  if (script.isEmpty()) {
    return QString();
  }

  script.replace(QStringLiteral("__CHATGPT_DESKTOP_COPY_PREFIX_PLACEHOLDER__"), clipboardBridgePrefix);
  return script;
}

QString BuildLongChatPerformanceScriptSource() {
  // Load the long-chat JS from resources
  return LoadScriptFromResource(QStringLiteral(":/scripts/long-chat-performance.js"));
}

} // namespace ChatInjections
