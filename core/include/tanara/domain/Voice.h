#pragma once
//
// Domain: voiceprints, voice matches and the per-model embedding set.
// The fusion of an EmbeddingSet (fusion::fuse / averageCosine) is an algorithm and lives in
// processing/voiceid/EmbeddingSet.h.
#include <QMap>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

// The embeddings of an audio slice (or voiceprint) per model. The model order is deterministic
// everywhere: the QMap sorts by key.
using EmbeddingSet = QMap<QString /*modelId*/, QVector<float>>;

namespace voicemodels {

// Model id of the "old" single-model paths: voiceprints and caches without a model field belong to
// this model (VoiceModelRegistry::defaultModelId() returns the same).
inline QString defaultModelId() { return QStringLiteral("campplus"); }

// Deterministic form of a model id list: trimmed, without empties, unique, alphabetical.
QStringList normalizeIds(const QStringList& ids);

} // namespace voicemodels

// ---- speaker identification (voiceprint) ------------------------------------
// A voiceprint: an L2-normalized embedding vector computed from one representative
// segment of a speaker + the source metadata. A name may have several (a different
// microphone gives a different acoustic signature).
struct Voiceprint {
    QString id;                 // unique id (QUuid)
    QVector<float> embedding;   // L2-normalized speaker embedding
    int dim = 0;                // embedding.size() (redundant, but useful in the JSON)
    QString sourceMeetingId;    // which meeting it came from
    QString sourceTrack;        // pl. "loopback" / "mic" / track-id
    QString device;             // name of the recording device (multi-microphone context)
    QString sampleRef;          // "<folder>/track_x.ogg#startMs-endMs"; times in meeting time
    QString createdAt;          // ISO-8601
    // The embedding model (VoiceModelRegistry id). JSON "model"; missing = "campplus". A sample
    // (sampleRef) gets a separate voiceprint per enabled model (same sampleRef/createdAt).
    QString model{QStringLiteral("campplus")};
    // Every contributing audio span as "<file>#startMs-endMs" (meeting time), when the voiceprint
    // was built from several spans (JSON "sourceRefs"). sampleRef is the span to play (the longest);
    // lazy backfill (with another model) recomputes from these, or from sampleRef if absent.
    QStringList sourceRefs;
};

// Result of a match: which person, with what (cosine) score.
struct VoiceMatch {
    QString name;
    double score = -1.0;        // [-1..1] cosine; <0 = no match / empty DB
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::Voiceprint)
Q_DECLARE_METATYPE(tanara::VoiceMatch)
