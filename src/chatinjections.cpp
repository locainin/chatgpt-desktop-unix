#include "chatinjections.h"

#include <QDebug>
#include <QFile>
#include <QIODevice>

namespace {

QString LoadScriptFromResource(const QString &resourcePath) {
  // Read JS from app resources so paths always work
  QFile scriptFile(resourcePath);
  if (!scriptFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qWarning() << "Failed to open script resource:" << resourcePath;
    return QString();
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
