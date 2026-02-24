#include "chatwebpage.h"

#include <QByteArray>
#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QMetaObject>
#include <QTimer>
#include <QUrl>

namespace {
// Hard cap keeps clipboard payloads at a safe size
constexpr qsizetype kMaxClipboardBytes = 8 * 1024 * 1024;
constexpr qsizetype kMaxClipboardEncodedChars = ((kMaxClipboardBytes + 2) / 3) * 4;
// Fallback prefix is used only if setup fails
const QString kFallbackCopyPrefix = QStringLiteral("__CHATGPT_DESKTOP_COPY__");

bool IsTrustedClipboardHost(const QString &host) {
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

bool IsAllowedNavigationScheme(const QString &scheme) {
  return scheme == QStringLiteral("https") || scheme == QStringLiteral("http") ||
         scheme == QStringLiteral("about") || scheme == QStringLiteral("blob") ||
         scheme == QStringLiteral("data");
}
} // namespace

ChatWebPage::ChatWebPage(QWebEngineProfile *profile, const QString &clipboardBridgePrefix, QObject *parent)
    : QWebEnginePage(profile, parent),
      m_clipboardBridgePrefix(clipboardBridgePrefix.isEmpty() ? kFallbackCopyPrefix : clipboardBridgePrefix) {
}

bool ChatWebPage::acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) {
  if (!url.isValid()) {
    return false;
  }

  const QString scheme = url.scheme().toLower();
  if (scheme == QStringLiteral("javascript")) {
    return false;
  }

  if (IsAllowedNavigationScheme(scheme)) {
    return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
  }

  if (isMainFrame && type == QWebEnginePage::NavigationTypeLinkClicked) {
    QDesktopServices::openUrl(url);
  }
  return false;
}

bool ChatWebPage::javaScriptPrompt(const QUrl &securityOrigin, const QString &msg,
                                   const QString &defaultValue, QString *result) {
  Q_UNUSED(defaultValue);

  // Non-bridge prompts follow default WebEngine behavior
  if (!msg.startsWith(m_clipboardBridgePrefix)) {
    return QWebEnginePage::javaScriptPrompt(securityOrigin, msg, defaultValue, result);
  }

  // Reject clipboard bridge calls from untrusted origins
  if (!IsTrustedClipboardOrigin(securityOrigin)) {
    if (result != nullptr) {
      *result = QStringLiteral("rejected");
    }
    return true;
  }

  const QString encodedText = msg.mid(m_clipboardBridgePrefix.size());
  if (encodedText.isEmpty()) {
    if (result != nullptr) {
      *result = QStringLiteral("empty");
    }
    return true;
  }
  if (encodedText.size() > kMaxClipboardEncodedChars) {
    if (result != nullptr) {
      *result = QStringLiteral("too-large");
    }
    return true;
  }

  // Decode from the prompt payload
  const QByteArray decodedPayload = QByteArray::fromBase64(encodedText.toLatin1());
  // Empty decode or oversized payload is invalid
  if (decodedPayload.isEmpty() || decodedPayload.size() > kMaxClipboardBytes) {
    if (result != nullptr) {
      *result = QStringLiteral("invalid");
    }
    return true;
  }

  // Clipboard writes only accept meaningful text content
  const QString text = QString::fromUtf8(decodedPayload);
  if (text.trimmed().isEmpty()) {
    if (result != nullptr) {
      *result = QStringLiteral("empty-text");
    }
    return true;
  }

  CommitClipboardText(text);
  if (result != nullptr) {
    *result = QStringLiteral("ok");
  }
  return true;
}

bool ChatWebPage::IsTrustedClipboardOrigin(const QUrl &origin) const {
  // Invalid origins can happen during frame changes
  if (!origin.isValid()) {
    return IsTrustedClipboardHost(url().host());
  }

  // Normal HTTPS frames
  if (origin.scheme() == QStringLiteral("https")) {
    return IsTrustedClipboardHost(origin.host());
  }

  // Blob URLs can wrap trusted HTTPS origins
  if (origin.scheme() == QStringLiteral("blob")) {
    const QString originString = origin.toString();
    const QString blobPrefix = QStringLiteral("blob:https://");
    if (originString.startsWith(blobPrefix)) {
      const QUrl embeddedOrigin(QStringLiteral("https://") + originString.mid(blobPrefix.size()));
      return IsTrustedClipboardHost(embeddedOrigin.host());
    }
  }

  // These schemes can appear during same-document navigation
  if (origin.scheme() == QStringLiteral("about") || origin.scheme() == QStringLiteral("data") ||
      origin.scheme().isEmpty()) {
    return IsTrustedClipboardHost(url().host());
  }

  // Any other scheme is rejected
  return false;
}

void ChatWebPage::CommitClipboardText(const QString &text) {
  if (text.trimmed().isEmpty()) {
    return;
  }

  QCoreApplication *application = QGuiApplication::instance();
  if (application == nullptr) {
    return;
  }

  // Queue into the GUI loop for clipboard safety
  QMetaObject::invokeMethod(
      application,
      [clipboardText = text]() {
        QClipboard *clipboard = QGuiApplication::clipboard();
        if (clipboard == nullptr) {
          return;
        }

        // Standard clipboard target
        clipboard->setText(clipboardText, QClipboard::Clipboard);
        // Selection target for middle-click paste on Linux
        clipboard->setText(clipboardText, QClipboard::Selection);

        // Retry once in case another write races this one
        QTimer::singleShot(150, QGuiApplication::instance(), [clipboardText]() {
          QClipboard *retryClipboard = QGuiApplication::clipboard();
          if (retryClipboard == nullptr) {
            return;
          }
          retryClipboard->setText(clipboardText, QClipboard::Clipboard);
          retryClipboard->setText(clipboardText, QClipboard::Selection);
        });
      },
      Qt::QueuedConnection);
}
