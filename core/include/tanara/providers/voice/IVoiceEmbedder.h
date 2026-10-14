#pragma once
//
// Voice-embedding port (CapabilityKind::VoiceEmbedding): speaker-embedding vectors from audio.
//
// The request carries EITHER raw audio clips (a file + a time span, AudioClipRef) OR ready 16 kHz
// mono float PCM (D5: both inputs; the descriptor's traits.inputs says which a provider takes:
// InputKind::AudioClip / InputKind::Pcm). One clip / PCM slice -> one VoiceVector per requested model,
// in request order:
//   result()[i * models + m]  for clip i and model m (models = modelIds, or the provider's default).
//
// Implementations: processing::OnnxVoiceEmbedder (Origin::Local: the ONNX models; a clip is decoded
// with media::MediaTool), cloud::CloudVoiceEmbedder (Origin::Cloud: the clip is cut to Opus with the
// media tool and uploaded to /v1/voice/embeddings).
#include "tanara/providers/core/CapabilityJob.h"
#include "tanara/providers/core/ProviderDescriptor.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::providers {

// A span of an audio file. endMs <= 0 = to the end of the file. Times in FILE time.
struct AudioClipRef {
    QString path;
    qint64  startMs = 0;
    qint64  endMs = 0;
};

struct VoiceEmbeddingRequest {
    QVector<AudioClipRef>   clips;   // either clips ...
    QVector<QVector<float>> pcm;     // ... or 16 kHz mono float PCM slices ([-1,1])
    // The models to embed with; empty = the provider's default (every enabled local model / the
    // gateway's default voice model). One model id per string (VoiceModelRegistry / catalog ids).
    QStringList modelIds;

    bool isEmpty() const { return clips.isEmpty() && pcm.isEmpty(); }
    int  inputCount() const { return int(clips.isEmpty() ? pcm.size() : clips.size()); }
};

// One embedding. An empty vector = this input failed with this model (e.g. silence / too short);
// the job as a whole still succeeds.
struct VoiceVector {
    QVector<float> vector;   // L2-normalized
    int     dim = 0;
    QString modelId;
    int     input = 0;       // index of the clip / PCM slice in the request
};

using VoiceEmbeddingJob = ResultJob<QVector<VoiceVector>>;

class IVoiceEmbedder {
public:
    virtual ~IVoiceEmbedder() = default;
    virtual QString name() const = 0;
    // The models this provider embeds with when the request names none.
    virtual QStringList defaultModelIds() const = 0;
    // The job is the provider's child.
    virtual VoiceEmbeddingJob* embed(const VoiceEmbeddingRequest& req) = 0;
};

template <> struct ProviderTraits<IVoiceEmbedder> {
    static constexpr CapabilityKind kind = CapabilityKind::VoiceEmbedding;
};

} // namespace tanara::providers
