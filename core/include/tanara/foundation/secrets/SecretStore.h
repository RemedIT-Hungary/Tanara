#pragma once
//
// Secret storage (API keys, tokens) behind one port.
//
//  - SecretStore: the synchronous port (get / set / remove / contains / backendName).
//  - FileSecretStore: <data>/secrets.json, chmod 600, atomic writes (the fallback).
//  - KeychainSecretStore: the OS keychain via QtKeychain (Linux Secret Service / KWallet, Windows
//    Credential Manager, macOS Keychain), service name "Tanara". QtKeychain is asynchronous; the
//    known keys are read once into a memory cache at startup (each job waits at most 5 s in a local
//    event loop), "not found" is cached too, writes go to the keychain and the cache at once (the
//    cache rules: detail/CachedSecretStore.h). Built only with TANARA_HAVE_KEYCHAIN.
//  - openSecretStore(): picks the backend from the "secrets" settings section.
//    TANARA_HOME or the Qt test mode -> ALWAYS the file backend: sandboxes and tests never reach
//    the real keychain.
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace tanara {

class SecretStore {
public:
    virtual ~SecretStore() = default;
    // The stored value, or an empty string if there is none.
    virtual QString get(const QString& key) const = 0;
    // Sets / overwrites a key and persists at once. false: could not be stored.
    virtual bool set(const QString& key, const QString& value) = 0;
    // Deletes a key (if it exists) and persists.
    virtual bool remove(const QString& key) = 0;
    virtual bool contains(const QString& key) const = 0;
    // "file" | "keychain"
    virtual QString backendName() const = 0;
};

class FileSecretStore final : public SecretStore {
public:
    explicit FileSecretStore(const QString& filePath);
    QString get(const QString& key) const override;
    bool set(const QString& key, const QString& value) override;
    bool remove(const QString& key) override;
    bool contains(const QString& key) const override;
    QString backendName() const override { return QStringLiteral("file"); }

    QString filePath() const { return m_filePath; }
    QHash<QString, QString> all() const { return m_cache; }

private:
    void load();
    bool persist() const;

    QString m_filePath;
    QHash<QString, QString> m_cache;
};

// The "secrets" section of settings.json.
struct SecretSettings {
    QString backend{QStringLiteral("auto")};   // "auto" | "keychain" | "file"
    QJsonObject toJson() const;
    static SecretSettings fromJson(const QJsonObject& o);
    bool operator==(const SecretSettings&) const = default;
};

namespace secrets {

inline const QString KeychainService = QStringLiteral("Tanara");

// Whether the binary was built with QtKeychain.
bool keychainCompiled();
// Whether the keychain may be used at all in this process (not with TANARA_HOME / test mode).
bool keychainAllowed();

struct OpenResult {
    std::unique_ptr<SecretStore> store;
    QString warning;     // non-empty: why the keys are in a file although the keychain was wanted
    int imported = 0;    // keys moved from secrets.json into the keychain at this start
};

// Opens the backend chosen by `settings`:
//  - "file" -> FileSecretStore(secretsFile);
//  - "keychain" / "auto" -> KeychainSecretStore if allowed + compiled + a probe succeeds, with the
//    `knownKeys` preloaded; otherwise the file (+ warning).
// With the keychain active and an existing secretsFile, its keys are imported once and the file is
// renamed to "secrets.json.imported" (the user deletes it).
OpenResult open(const SecretSettings& settings, const QString& secretsFile, const QStringList& knownKeys);

} // namespace secrets

} // namespace tanara
