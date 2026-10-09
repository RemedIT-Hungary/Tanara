#include "tanara/voiceid/VoiceEmbedderSet.h"

#include "tanara/Logging.h"
#include "tanara/voiceid/VoiceEmbedder.h"

#include <QFileInfo>

#include <algorithm>

namespace tanara {

VoiceEmbedderSet::VoiceEmbedderSet(QVector<VoiceModelEntry> models, PcmEmbedderLoader loader)
    : m_models(std::move(models)), m_loader(loader ? std::move(loader) : defaultLoader())
{
    std::sort(m_models.begin(), m_models.end(),
              [](const VoiceModelEntry& a, const VoiceModelEntry& b) { return a.spec.id < b.spec.id; });
    m_slots.resize(m_models.size());
}

VoiceEmbedderSet VoiceEmbedderSet::fromSettings(const QStringList& enabledIds, const QString& metaDir,
                                                const QString& appDir, PcmEmbedderLoader loader)
{
    QVector<VoiceModelEntry> models;
    for (const VoiceModelSpec& s : VoiceModelRegistry::active(enabledIds, metaDir, appDir))
        models.append({s, VoiceModelRegistry::resolvePath(s, metaDir, appDir)});
    return VoiceEmbedderSet(models, std::move(loader));
}

PcmEmbedderLoader VoiceEmbedderSet::defaultLoader()
{
    return [](const VoiceModelSpec& spec, const QString& path, QString* error) -> PcmEmbedder {
        if (!QFileInfo::exists(path)) {
            if (error) *error = QStringLiteral("Hiányzik a hangmodell: %1").arg(path);
            return {};
        }
        auto e = std::make_shared<VoiceEmbedder>(path, spec.features, spec.dim);
        if (!e->isValid()) {
            if (error) *error = e->lastError();
            return {};
        }
        return [e](const QVector<float>& pcm) { return e->embedPcm(pcm); };
    };
}

VoiceEmbedderSet VoiceEmbedderSet::freshCopy() const
{
    return VoiceEmbedderSet(m_models, m_loader);
}

QStringList VoiceEmbedderSet::modelIds() const
{
    QStringList ids;
    for (const VoiceModelEntry& m : m_models) ids << m.spec.id;
    return ids;
}

VoiceEmbedderSet::Slot& VoiceEmbedderSet::slot(int i) const
{
    Slot& s = m_slots[i];
    if (!s.tried) {
        s.tried = true;
        QString error;
        s.fn = m_loader(m_models[i].spec, m_models[i].path, &error);
        if (!s.fn) {
            m_error = error;
            qCWarning(lcVoice).noquote() << "Hangmodell nem tölthető be:" << m_models[i].spec.id << error;
        }
    }
    return s;
}

bool VoiceEmbedderSet::ensureLoaded() const
{
    bool any = false;
    for (int i = 0; i < m_models.size(); ++i)
        if (slot(i).fn) any = true;
    return any;
}

QStringList VoiceEmbedderSet::loadedModelIds() const
{
    QStringList ids;
    for (int i = 0; i < m_models.size(); ++i)
        if (slot(i).fn) ids << m_models[i].spec.id;
    return ids;
}

EmbeddingSet VoiceEmbedderSet::embedPcm(const QVector<float>& mono16k) const
{
    EmbeddingSet out;
    if (mono16k.isEmpty()) return out;
    for (int i = 0; i < m_models.size(); ++i) {
        const Slot& s = slot(i);
        if (!s.fn) continue;
        const QVector<float> v = s.fn(mono16k);
        if (!v.isEmpty()) out.insert(m_models[i].spec.id, v);
    }
    return out;
}

EmbeddingSet VoiceEmbedderSet::embedPcmWith(const QVector<float>& mono16k, const QStringList& modelIds) const
{
    EmbeddingSet out;
    for (int i = 0; i < m_models.size(); ++i) {
        if (!modelIds.contains(m_models[i].spec.id)) continue;
        const Slot& s = slot(i);
        if (!s.fn) continue;
        out.insert(m_models[i].spec.id, mono16k.isEmpty() ? QVector<float>() : s.fn(mono16k));
    }
    return out;
}

} // namespace tanara
