#pragma once
//
// CachedSecretStore: the memory cache in front of a slow, synchronous secret backend (the OS
// keychain, whose every call waits in a local event loop). The backend calls are injected, so the
// cache rules are testable without a real keychain.
//
// Cache rules: the known keys are read once (preload). After a successful read, "not found" is
// cached too (as an empty value): get() / contains() never reach the backend again for a key that
// was seen. Reason: a keychain read spins a nested event loop on the GUI thread, which runs queued
// deleteLater()s inside whatever QML handler asked for the key (crash: "Object destroyed while one
// of its QML signal handlers is in progress"). A FAILED read is not cached; the next get() retries.
#include "tanara/foundation/secrets/SecretStore.h"

#include <functional>

namespace tanara::secrets::detail {

enum class OpOutcome { Ok, NotFound, Failed };

struct OpResult {
    OpOutcome outcome = OpOutcome::Failed;
    QString text;    // read: the value
    QString error;   // Failed: why
};

class CachedSecretStore final : public SecretStore {
public:
    using ReadOp = std::function<OpResult(const QString& key)>;
    using WriteOp = std::function<OpResult(const QString& key, const QString& value)>;
    using RemoveOp = std::function<OpResult(const QString& key)>;

    CachedSecretStore(ReadOp read, WriteOp write, RemoveOp remove, const QStringList& knownKeys,
                      QString backendName);

    QString get(const QString& key) const override;
    bool set(const QString& key, const QString& value) override;
    bool remove(const QString& key) override;
    bool contains(const QString& key) const override { return !get(key).isEmpty(); }
    QString backendName() const override { return m_backendName; }

private:
    ReadOp m_read;
    WriteOp m_write;
    RemoveOp m_remove;
    QString m_backendName;
    mutable QHash<QString, QString> m_cache;   // key -> value; "" = known to be missing
};

} // namespace tanara::secrets::detail
