#pragma once

#include <QString>

namespace ChatInjections {

QString BuildCodeCopyBridgeScriptSource(const QString &clipboardBridgePrefix);
QString BuildLongChatPerformanceScriptSource();

} // namespace ChatInjections
