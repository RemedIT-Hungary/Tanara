#include "tanara/provider/ProviderRegistry.h"

namespace tanara {

// ── SttProviderRegistry ───────────────────────────────────────────────────

SttProviderRegistry& SttProviderRegistry::instance()
{
    static SttProviderRegistry s_instance;
    return s_instance;
}

void SttProviderRegistry::registerProvider(const ProviderDescriptor& desc, Factory factory)
{
    m_map.insert(desc.id, qMakePair(desc, std::move(factory)));
}

QVector<ProviderDescriptor> SttProviderRegistry::all() const
{
    QVector<ProviderDescriptor> result;
    result.reserve(m_map.size());
    for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it)
        result.append(it.value().first);
    return result;
}

bool SttProviderRegistry::has(const QString& id) const
{
    return m_map.contains(id);
}

ProviderDescriptor SttProviderRegistry::descriptor(const QString& id) const
{
    auto it = m_map.constFind(id);
    if (it == m_map.constEnd())
        return ProviderDescriptor{};
    return it.value().first;
}

ISttProvider* SttProviderRegistry::create(const QString& id, const ProviderConfig& cfg, QObject* parent) const
{
    auto it = m_map.constFind(id);
    if (it == m_map.constEnd())
        return nullptr;
    const Factory& factory = it.value().second;
    if (!factory)
        return nullptr;
    return factory(cfg, parent);
}

// ── LlmProviderRegistry ───────────────────────────────────────────────────

LlmProviderRegistry& LlmProviderRegistry::instance()
{
    static LlmProviderRegistry s_instance;
    return s_instance;
}

void LlmProviderRegistry::registerProvider(const ProviderDescriptor& desc, Factory factory)
{
    m_map.insert(desc.id, qMakePair(desc, std::move(factory)));
}

QVector<ProviderDescriptor> LlmProviderRegistry::all() const
{
    QVector<ProviderDescriptor> result;
    result.reserve(m_map.size());
    for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it)
        result.append(it.value().first);
    return result;
}

bool LlmProviderRegistry::has(const QString& id) const
{
    return m_map.contains(id);
}

ProviderDescriptor LlmProviderRegistry::descriptor(const QString& id) const
{
    auto it = m_map.constFind(id);
    if (it == m_map.constEnd())
        return ProviderDescriptor{};
    return it.value().first;
}

ILlmProvider* LlmProviderRegistry::create(const QString& id, const ProviderConfig& cfg, QObject* parent) const
{
    auto it = m_map.constFind(id);
    if (it == m_map.constEnd())
        return nullptr;
    const Factory& factory = it.value().second;
    if (!factory)
        return nullptr;
    return factory(cfg, parent);
}

} // namespace tanara
