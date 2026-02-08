#pragma once

#include <QString>
#include <QWebEnginePage>

class ChatWebPage final : public QWebEnginePage {
    Q_OBJECT
public:
    explicit ChatWebPage(QWebEngineProfile *profile,
                         const QString &clipboardBridgePrefix,
                         QObject *parent = nullptr);

protected:
    bool javaScriptPrompt(const QUrl &securityOrigin,
                          const QString &msg,
                          const QString &defaultValue,
                          QString *result) override;

private:
    // Validate prompt sender before accepting clipboard payloads
    bool IsTrustedClipboardOrigin(const QUrl &origin) const;
    // Commit decoded text to native clipboard targets
    void CommitClipboardText(const QString &text);

    // Runtime bridge prefix blocks forged prompt payloads from arbitrary page scripts
    QString m_clipboardBridgePrefix;
};
