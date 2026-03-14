#pragma once

#include <QString>
#include <memory>

class QLockFile;
class QWebEngineProfile;

class BrowserProfile final {
public:
  // Reuse one profile for every native window in this process
  static BrowserProfile &Instance();

  QWebEngineProfile *Profile() const;
  const QString &ClipboardBridgePrefix() const;
  // Give Chromium a short quiet window during app shutdown
  void FlushPersistentStateSync();

private:
  BrowserProfile();
  ~BrowserProfile() = default;

  // Build the shared profile only once per process
  void InitializeProfile();
  // Keep profile storage on disk across restarts
  QString ResolveStorageRoot() const;
  // Keep cache away from volatile paths when possible
  QString ResolveCacheRoot() const;

  // QCoreApplication owns the profile through QObject parenting
  QWebEngineProfile *m_profile = nullptr;
  // Lock object must stay alive while this process owns the profile files
  std::unique_ptr<QLockFile> m_profileLock;
  QString m_clipboardBridgePrefix;
  qint64 m_lastCookieMutationAtMs = 0;
};
