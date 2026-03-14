#include "trustedorigins.h"

#include <QString>

namespace TrustedOrigins {

bool IsTrustedHost(const QString &host) {
  if (host.isEmpty()) {
    return false;
  }

  const QString normalizedHost = host.toLower();
  if (normalizedHost == QStringLiteral("chatgpt.com") ||
      normalizedHost.endsWith(QStringLiteral(".chatgpt.com"))) {
    return true;
  }
  if (normalizedHost == QStringLiteral("openai.com") ||
      normalizedHost.endsWith(QStringLiteral(".openai.com"))) {
    return true;
  }
  if (normalizedHost == QStringLiteral("oaistatic.com") ||
      normalizedHost.endsWith(QStringLiteral(".oaistatic.com"))) {
    return true;
  }

  return false;
}

bool IsTrustedHttpsUrl(const QUrl &url) {
  // Plain HTTP and local pages never get privileged browser access
  if (!url.isValid()) {
    return false;
  }
  if (url.scheme() != QStringLiteral("https")) {
    return false;
  }
  return IsTrustedHost(url.host());
}

bool IsTrustedClipboardOrigin(const QUrl &origin, const QUrl &fallbackPageUrl) {
  // Invalid origins can show up during frame swaps
  if (!origin.isValid()) {
    return IsTrustedHost(fallbackPageUrl.host());
  }

  // Normal page loads use HTTPS origins directly
  if (origin.scheme() == QStringLiteral("https")) {
    return IsTrustedHost(origin.host());
  }

  // Blob URLs wrap an HTTPS origin in the URL text
  if (origin.scheme() == QStringLiteral("blob")) {
    const QString originString = origin.toString();
    const QString blobPrefix = QStringLiteral("blob:https://");
    if (originString.startsWith(blobPrefix)) {
      const QUrl embeddedOrigin(QStringLiteral("https://") + originString.mid(blobPrefix.size()));
      return IsTrustedHost(embeddedOrigin.host());
    }
  }

  // Same-document jumps can report these light-weight schemes
  if (origin.scheme() == QStringLiteral("about") || origin.scheme() == QStringLiteral("data") ||
      origin.scheme().isEmpty()) {
    return IsTrustedHost(fallbackPageUrl.host());
  }

  return false;
}

} // namespace TrustedOrigins
