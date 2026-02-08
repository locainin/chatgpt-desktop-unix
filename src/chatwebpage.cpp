#include "chatwebpage.h"

#include <QByteArray>
#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QMetaObject>
#include <QTimer>
#include <QUrl>

namespace {
// Hard cap prevents oversized prompt payloads from exhausting memory
constexpr qsizetype kMaxClipboardBytes = 8 * 1024 * 1024;
// Prompt prefix distinguishes native bridge messages from site prompts
const QString kCopyPrefix = QStringLiteral("__CHATGPT_DESKTOP_COPY__");

bool IsTrustedClipboardHost(const QString &host)
{
    if (host.isEmpty()) {
        return false;
    }

    const QString normalizedHost = host.toLower();
    if (normalizedHost == QStringLiteral("chatgpt.com")
        || normalizedHost.endsWith(QStringLiteral(".chatgpt.com"))) {
        return true;
    }
    if (normalizedHost == QStringLiteral("openai.com")
        || normalizedHost.endsWith(QStringLiteral(".openai.com"))) {
        return true;
    }
    if (normalizedHost == QStringLiteral("oaistatic.com")
        || normalizedHost.endsWith(QStringLiteral(".oaistatic.com"))) {
        return true;
    }

    return false;
}
}

ChatWebPage::ChatWebPage(QWebEngineProfile *profile, QObject *parent)
    : QWebEnginePage(profile, parent)
{
}

bool ChatWebPage::javaScriptPrompt(const QUrl &securityOrigin,
                                   const QString &msg,
                                   const QString &defaultValue,
                                   QString *result)
{
    Q_UNUSED(defaultValue);

    // Non-bridge prompts follow default WebEngine behavior
    if (!msg.startsWith(kCopyPrefix)) {
        return QWebEnginePage::javaScriptPrompt(securityOrigin, msg, defaultValue, result);
    }

    // Reject clipboard bridge calls from untrusted origins
    if (!IsTrustedClipboardOrigin(securityOrigin)) {
        if (result != nullptr) {
            *result = QStringLiteral("rejected");
        }
        return true;
    }

    const QString encodedText = msg.mid(kCopyPrefix.size());
    if (encodedText.isEmpty()) {
        if (result != nullptr) {
            *result = QStringLiteral("empty");
        }
        return true;
    }

    // Base64 decode from prompt transport
    const QByteArray decodedPayload = QByteArray::fromBase64(encodedText.toLatin1());
    // Empty decode or oversized payload is treated as invalid
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

bool ChatWebPage::IsTrustedClipboardOrigin(const QUrl &origin) const
{
    // Normal HTTPS frames
    if (origin.isValid() && origin.scheme() == QStringLiteral("https")) {
        return IsTrustedClipboardHost(origin.host());
    }

    // Blob URLs can wrap trusted HTTPS origins
    if (origin.isValid() && origin.scheme() == QStringLiteral("blob")) {
        const QString originString = origin.toString();
        const QString blobPrefix = QStringLiteral("blob:https://");
        if (originString.startsWith(blobPrefix)) {
            const QUrl embeddedOrigin(QStringLiteral("https://")
                                      + originString.mid(blobPrefix.size()));
            return IsTrustedClipboardHost(embeddedOrigin.host());
        }
    }

    // about/data/empty scheme can appear during frame transitions
    if (origin.scheme() == QStringLiteral("about")
        || origin.scheme() == QStringLiteral("data")
        || origin.scheme().isEmpty()) {
        return IsTrustedClipboardHost(url().host());
    }

    // Final fallback uses the current page host
    return IsTrustedClipboardHost(url().host());
}

void ChatWebPage::CommitClipboardText(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return;
    }

    QCoreApplication *application = QGuiApplication::instance();
    if (application == nullptr) {
        return;
    }

    // Queue onto the GUI event loop to satisfy clipboard backend requirements
    QMetaObject::invokeMethod(application,
        [clipboardText = text]() {
            QClipboard *clipboard = QGuiApplication::clipboard();
            if (clipboard == nullptr) {
                return;
            }

            // Clipboard target covers standard Ctrl+V paste behavior
            clipboard->setText(clipboardText, QClipboard::Clipboard);
            // Selection target improves middle-click paste on Linux
            clipboard->setText(clipboardText, QClipboard::Selection);

            // Short re-assert helps against late competing writes
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
