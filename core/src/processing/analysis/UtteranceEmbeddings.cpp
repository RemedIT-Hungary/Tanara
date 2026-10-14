#include "tanara/processing/analysis/UtteranceEmbeddings.h"

#include "tanara/processing/voiceid/VoiceEmbedder.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace tanara::processing {

namespace {

constexpr int kSampleRate = 16000;

// Several models over a meeting's audio: the whole audio as 16 kHz mono s16 in memory (~115 MB / hour,
// ONE ffmpeg run), the utterances are sliced from it, and every loaded model computes from the same slice.
class VoiceUtteranceEmbedder final : public IUtteranceEmbedder {
public:
    VoiceUtteranceEmbedder(VoiceEmbedderSet set, media::MediaToolPtr media)
        : m_set(std::move(set)), m_media(std::move(media)) {}

    bool open(const QString& audioPath) override
    {
        m_pcm.clear();
        if (!QFileInfo::exists(audioPath)) {
            m_error = QStringLiteral("Hiányzik a hangfájl: %1").arg(audioPath);
            return false;
        }
        if (!loadModels()) return false;

        if (!m_media) {
            m_error = QStringLiteral("Az ffmpeg nem indítható.");
            return false;
        }
        // The whole audio is kept (an hour is ~115 MB); the tool streams it in.
        media::DecodeSpec spec;
        spec.path = audioPath;
        spec.format = media::PcmFormat{kSampleRate, 1, media::SampleFormat::S16};
        const media::MediaResult r = m_media->decodePcm(spec, [this](const char* data, qsizetype bytes) {
            m_pcm.append(data, bytes);
        });
        if (!r.started) {
            m_error = QStringLiteral("Az ffmpeg nem indítható.");
            m_pcm.clear();
            return false;
        }
        if (!r.normalExit || m_pcm.size() < 2) {
            m_error = QStringLiteral("ffmpeg dekódolás sikertelen: %1").arg(audioPath);
            m_pcm.clear();
            return false;
        }
        return true;
    }

    QVector<float> embed(qint64 startMs, qint64 endMs) override
    {
        const QString id = VoiceModelRegistry::defaultModelId();
        return embedAll(startMs, endMs, {id}).value(id);
    }

    EmbeddingSet embedAll(qint64 startMs, qint64 endMs, const QStringList& modelIds) override
    {
        if (m_pcm.isEmpty()) return {};
        return m_set.embedPcmWith(slice(startMs, endMs), modelIds);
    }

    QString lastError() const override { return m_error; }

private:
    // Loading the models (once). At least one must succeed.
    bool loadModels()
    {
        if (m_set.ensureLoaded()) return true;
        m_error = m_set.isEmpty() ? QStringLiteral("Nincs hangmodell.") : m_set.lastError();
        return false;
    }

    // The [startMs,endMs] slice as float PCM; under half a second it is empty (no meaningful embedding).
    QVector<float> slice(qint64 startMs, qint64 endMs) const
    {
        const qint64 total = m_pcm.size() / qint64(sizeof(qint16));
        const qint64 from = std::clamp<qint64>(startMs * kSampleRate / 1000, 0, total);
        const qint64 to   = std::clamp<qint64>(endMs * kSampleRate / 1000, from, total);
        if (to - from < kSampleRate / 2) return {};
        const qint16* src = reinterpret_cast<const qint16*>(m_pcm.constData());
        QVector<float> pcm(int(to - from));
        for (qint64 i = from; i < to; ++i)
            pcm[int(i - from)] = float(src[i]) / 32768.0f;
        return pcm;
    }

    VoiceEmbedderSet m_set;   // loaded lazily per model (the background thread's own instance)
    media::MediaToolPtr m_media;
    QString m_error;
    QByteArray m_pcm;   // s16le mono 16 kHz
};

} // namespace

UtteranceEmbedderFactory voiceUtteranceEmbedderFactory(const QVector<UtteranceModel>& models,
                                                       media::MediaToolPtr media)
{
    QVector<VoiceModelEntry> entries;
    for (const UtteranceModel& m : models) {
        VoiceModelSpec spec;
        if (const auto known = VoiceModelRegistry::spec(m.id)) spec = *known;
        spec.id = m.id;
        spec.features = m.features;
        spec.dim = m.dim;
        entries.append({spec, m.path});
    }
    return voiceUtteranceEmbedderFactory(VoiceEmbedderSet(entries), std::move(media));
}

UtteranceEmbedderFactory voiceUtteranceEmbedderFactory(const VoiceEmbedderSet& set,
                                                       media::MediaToolPtr media)
{
    if (set.isEmpty()) return {};
    const VoiceEmbedderSet proto = set.freshCopy();
    return [proto, media]() -> std::unique_ptr<IUtteranceEmbedder> {
        return std::make_unique<VoiceUtteranceEmbedder>(proto.freshCopy(), media);
    };
}

UtteranceEmbedderFactory voiceUtteranceEmbedderFactory(const QString& modelPath,
                                                       media::MediaToolPtr media)
{
    const auto spec = VoiceModelRegistry::spec(VoiceModelRegistry::defaultModelId());
    return voiceUtteranceEmbedderFactory(
        QVector<UtteranceModel>{{spec->id, modelPath, spec->features, spec->dim}}, std::move(media));
}

QVector<UtteranceModel> utteranceModelsFor(const QVector<VoiceModelSpec>& specs,
                                           const QString& modelsDir, const QString& appDir)
{
    QVector<UtteranceModel> out;
    for (const VoiceModelSpec& s : specs)
        out.append({s.id, VoiceModelRegistry::resolvePath(s, modelsDir, appDir), s.features, s.dim});
    return out;
}

} // namespace tanara::processing