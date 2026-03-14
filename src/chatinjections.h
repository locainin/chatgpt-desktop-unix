#pragma once

#include <QString>

namespace ChatInjections {

QString BuildTrustedOriginsScriptSource();
QString BuildCodeCopyBridgeScriptSource(const QString &clipboardBridgePrefix);
QString BuildLongChatPerformanceScriptSource();

} // namespace ChatInjections
