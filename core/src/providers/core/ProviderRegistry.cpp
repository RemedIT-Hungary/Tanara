#include "tanara/providers/core/ProviderRegistry.h"

namespace tanara::providers {

QString capabilityName(CapabilityKind kind)
{
    switch (kind) {
    case CapabilityKind::Transcription:             return QStringLiteral("transcription");
    case CapabilityKind::Chat:                      return QStringLiteral("chat");
    case CapabilityKind::TextEmbedding:             return QStringLiteral("text-embedding");
    case CapabilityKind::VoiceEmbedding:            return QStringLiteral("voice-embedding");
    case CapabilityKind::Summarization:             return QStringLiteral("summarization");
    case CapabilityKind::TagSuggestion:             return QStringLiteral("tag-suggestion");
    case CapabilityKind::ParticipantIdentification: return QStringLiteral("participant-identification");
    case CapabilityKind::ModelCatalog:              return QStringLiteral("model-catalog");
    case CapabilityKind::LocalModelServer:          return QStringLiteral("local-model-server");
    }
    return QString();
}

void ProviderRegistry::addErased(const ProviderDescriptor& desc, ErasedFactory factory)
{
    const Key key{int(desc.capability), desc.id};
    if (!m_entries.contains(key)) m_order.append(key);
    m_entries.insert(key, Entry{desc, std::move(factory)});
}

void* ProviderRegistry::createErased(CapabilityKind kind, const QString& id, const ProviderContext& ctx,
                                     const ProviderConfig& cfg, QObject* parent) const
{
    const auto it = m_entries.constFind(Key{int(kind), id});
    if (it == m_entries.constEnd() || !it->factory) return nullptr;
    return it->factory(ctx, cfg, parent);
}

QVector<ProviderDescriptor> ProviderRegistry::descriptors(CapabilityKind kind) const
{
    QVector<ProviderDescriptor> out;
    for (const Key& key : m_order)
        if (key.first == int(kind)) out.append(m_entries.value(key).desc);
    return out;
}

bool ProviderRegistry::has(CapabilityKind kind, const QString& id) const
{
    return m_entries.contains(Key{int(kind), id});
}

ProviderDescriptor ProviderRegistry::descriptor(CapabilityKind kind, const QString& id) const
{
    const auto it = m_entries.constFind(Key{int(kind), id});
    return it == m_entries.constEnd() ? ProviderDescriptor{} : it->desc;
}

void ProviderRegistry::remove(CapabilityKind kind, const QString& id)
{
    const Key key{int(kind), id};
    m_entries.remove(key);
    m_order.removeAll(key);
}

void ProviderRegistry::clear()
{
    m_entries.clear();
    m_order.clear();
}

} // namespace tanara::providers
