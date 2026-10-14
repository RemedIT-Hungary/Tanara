#pragma once
//
// OnnxVoiceEmbedder: the local IVoiceEmbedder (Origin::Local): a wrapper around the existing
// VoiceEmbedderSet (ONNX speaker models, fbank) and the media tool (clip decoding). No new
// algorithm: a clip is decoded with VoiceEmbedder::decodePcm16kMono, every slice is embedded with
// VoiceEmbedderSet::embedPcmWith.
//
// The work runs on a worker thread with the set's own unloaded copy (VoiceEmbedderSet::freshCopy:
// the set is not thread-safe); the job's result comes back on the caller's thread. Cancelling (or
// destroying) the job stops the worker after the slice in progress.
//
// Test seam: the set's PcmEmbedderLoader (a fake "model" without ONNX).
#include "tanara/media/MediaTool.h"
#include "tanara/processing/voiceid/VoiceEmbedderSet.h"
#include "tanara/providers/voice/IVoiceEmbedder.h"

#include <QObject>

namespace tanara::processing {

class OnnxVoiceEmbedder : public QObject, public providers::IVoiceEmbedder {
    Q_OBJECT
public:
    static inline const QString Id = QStringLiteral("onnx");

    OnnxVoiceEmbedder(VoiceEmbedderSet set, media::MediaToolPtr media, QObject* parent = nullptr);

    QString name() const override { return Id; }
    // The models of the set (enabled intersect available), alphabetical.
    QStringList defaultModelIds() const override;
    providers::VoiceEmbeddingJob* embed(const providers::VoiceEmbeddingRequest& req) override;

private:
    VoiceEmbedderSet m_set;
    media::MediaToolPtr m_media;
};

} // namespace tanara::processing
