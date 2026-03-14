#pragma once

#include <QString>
#include <QUrl>

namespace TrustedOrigins {

// Keep the small allowlist in one native place
bool IsTrustedHost(const QString &host);
// Gate browser settings that should only run on trusted HTTPS pages
bool IsTrustedHttpsUrl(const QUrl &url);
// Accept the same origin forms the page bridge can emit
bool IsTrustedClipboardOrigin(const QUrl &origin, const QUrl &fallbackPageUrl);

} // namespace TrustedOrigins
