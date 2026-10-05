#include "tanara/embedding/EmbeddingProviderRegistry.h"

namespace tanara {

EmbeddingProviderRegistry& EmbeddingProviderRegistry::instance()
{
    static EmbeddingProviderRegistry s_instance;
    return s_instance;
}

void EmbeddingProviderRegistry::registerProvider(const ProviderDescriptor& desc, Factory factory)
{
    m_map.insert(desc.id, qMakePair(desc, std::move(factory)));
}

QVector<ProviderDescriptor> EmbeddingProviderRegistry::all() const
{
    QVector<ProviderDescriptor> result;
    result.reserve(m_map.size());
    for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it)
        result.append(it.value().first);
    return result;
}

bool EmbeddingProviderRegistry::has(const QString& id) const
{
    return m_map.contains(id);
}

ProviderDescriptor EmbeddingProviderRegistry::descriptor(const QString& id) const
{
    const auto it = m_map.constFind(id);
    return it == m_map.constEnd() ? ProviderDescriptor{} : it.value().first;
}

IEmbeddingProvider* EmbeddingProviderRegistry::create(const QString& id, const ProviderConfig& cfg,
                                                      QObject* parent) const
{
    const auto it = m_map.constFind(id);
    if (it == m_map.constEnd() || !it.value().second) return nullptr;
    return it.value().second(cfg, parent);
}

} // namespace tanara
