#include "tanara/foundation/secrets/detail/CachedSecretStore.h"
#include "tanara/foundation/log/Logging.h"

namespace tanara::secrets::detail {

CachedSecretStore::CachedSecretStore(ReadOp read, WriteOp write, RemoveOp remove,
                                     const QStringList& knownKeys, QString backendName)
    : m_read(std::move(read)), m_write(std::move(write)), m_remove(std::move(remove)),
      m_backendName(std::move(backendName))
{
    for (const QString& k : knownKeys) {
        const OpResult r = m_read(k);
        if (r.outcome == OpOutcome::Failed)
            qCWarning(lcApp).noquote() << "Kulcstartó: olvasási hiba" << k << r.error;
        else
            m_cache.insert(k, r.text);   // NotFound -> "" (known to be missing)
    }
}

QString CachedSecretStore::get(const QString& key) const
{
    const auto it = m_cache.constFind(key);
    if (it != m_cache.constEnd()) return it.value();
    // An unknown key (not in the preload list): read on demand, then cached, "not found" included.
    const OpResult r = m_read(key);
    if (r.outcome != OpOutcome::Failed) m_cache.insert(key, r.text);
    return r.text;
}

bool CachedSecretStore::set(const QString& key, const QString& value)
{
    const OpResult r = m_write(key, value);
    if (r.outcome != OpOutcome::Ok) {
        qCWarning(lcApp).noquote() << "Kulcstartó: írási hiba" << key << r.error;
        return false;
    }
    m_cache.insert(key, value);
    return true;
}

bool CachedSecretStore::remove(const QString& key)
{
    const OpResult r = m_remove(key);
    if (r.outcome == OpOutcome::Failed) {
        m_cache.remove(key);             // state unknown: the next get() reads it again
        return false;
    }
    m_cache.insert(key, QString());
    return true;
}

} // namespace tanara::secrets::detail
