#pragma once
//
// VoiceEmbedderSet: the set of speaker models in use (enabled intersect available): one lazily
// (on first use) loaded VoiceEmbedder per model. From one PCM slice it gives every model's embedding
// (EmbeddingSet). Not thread-safe: a background thread gets its own instance with freshCopy().
//
// Test seam: the loader (PcmEmbedderLoader) is replaceable, so a fake "model" can be injected without
// ONNX. By default the real VoiceEmbedder loads (in a build without voice ID nothing loads).
#include "tanara/processing/voiceid/EmbeddingSet.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace tanara::processing {

// A loaded model: 16 kHz mono PCM ([-1,1]) -> L2-normalized vector (empty = failed).
using PcmEmbedder = std::function<QVector<float>(const QVector<float>& mono16k)>;
// Loading a model from the resolved file; an empty function = cannot be loaded (error: the reason).
using PcmEmbedderLoader =
    std::function<PcmEmbedder(const VoiceModelSpec& spec, const QString& path, QString* error)>;

// A model in use: the descriptor + the resolved file.
struct VoiceModelEntry {
    VoiceModelSpec spec;
    QString path;
};

class VoiceEmbedderSet {
public:
    // models: the models in use (any order; sorted by id inside). Empty loader = the real VoiceEmbedder (defaultLoader()).
    explicit VoiceEmbedderSet(QVector<VoiceModelEntry> models = {}, PcmEmbedderLoader loader = {});
    // The set per the settings: VoiceModelRegistry::active(enabledIds, modelsDir, appDir).
    static VoiceEmbedderSet fromSettings(const QStringList& enabledIds, const QString& modelsDir,
                                         const QString& appDir, PcmEmbedderLoader loader = {});
    static PcmEmbedderLoader defaultLoader();

    // The same configuration unloaded (for a background thread; the models load again there).
    VoiceEmbedderSet freshCopy() const;

    QStringList modelIds() const;                    // alphabetical
    bool isEmpty() const { return m_models.isEmpty(); }
    const QVector<VoiceModelEntry>& models() const { return m_models; }
    PcmEmbedderLoader loader() const { return m_loader; }

    // Loads the models not tried yet; true if at least one is usable.
    bool ensureLoaded() const;
    // Ids of the loadable models (triggers loading).
    QStringList loadedModelIds() const;

    // The vector of every loadable model (the key of a failed / unloadable model is missing).
    EmbeddingSet embedPcm(const QVector<float>& mono16k) const;
    // Only with the listed models; the key of a loadable, requested model is present even with an empty
    // vector (= tried, failed); that of an unloadable one is missing.
    EmbeddingSet embedPcmWith(const QVector<float>& mono16k, const QStringList& modelIds) const;

    QString lastError() const { return m_error; }

private:
    struct Slot {
        bool tried = false;
        PcmEmbedder fn;
    };
    Slot& slot(int i) const;

    QVector<VoiceModelEntry> m_models;
    PcmEmbedderLoader m_loader;
    mutable QVector<Slot> m_slots;
    mutable QString m_error;
};

} // namespace tanara::processing