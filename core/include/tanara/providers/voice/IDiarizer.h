#pragma once
//
// Diarization port (CapabilityKind::Diarization): "who speaks when" in one or more audio tracks.
//
// The request is per TRACK. A track is either an audio file (decoded by the provider) or ready
// 16 kHz mono float PCM. Every track is diarized (clustered into speakers) UNLESS the user bound a
// person to it by hand (DiarizeTrack::knownPerson); nothing is assumed from the track's kind: a
// microphone track is not "the owner" (imported recordings, dictaphone files and stereo downmixes
// carry no such information). Only a deliberate manual binding makes a track single-speaker: it
// is not clustered, and all of its speech becomes one speaker whose DiarizedSpeaker::person is set.
//
// The result is per track too: SpeakerTurn::speakerIndex is unique inside a track (0, 1, 2 ...), the
// pair (trackId, speakerIndex) names a speaker. Clusters are never matched across tracks here; the
// link between tracks is the voiceprint identification (IParticipantIdentifier).
//
// Every DiarizedSpeaker carries its cluster centroid as an IdentifyProbe, so the speakers can go
// straight into IdentifyRequest::probes (the vectors are L2-normalized, one per embedding model).
//
// All times are MEETING time: the provider adds DiarizeTrack::startOffsetMs to the track-file time.
//
// Implementations: processing::LocalDiarizer (Origin::Local; pyannote segmentation + speaker
// embedding + agglomerative clustering), later a Cloud variant. The job is the provider's child;
// progress(done, total) counts processed windows; cancel() stops the work (no signal follows).
#include "tanara/domain/Person.h"
#include "tanara/providers/core/CapabilityJob.h"
#include "tanara/providers/core/ProviderDescriptor.h"
#include "tanara/providers/voice/IParticipantIdentifier.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::providers {

// One input track.
struct DiarizeTrack {
    QString trackId;                 // echoed in the turns / speakers
    QString path;                    // the audio file (any format the media tool decodes) ...
    QVector<float> pcm;              // ... or 16 kHz mono float PCM ([-1,1]); used when `path` is empty
    qint64  startOffsetMs = 0;       // Track::startOffsetMs: file time -> meeting time
    // The person the USER bound to this track by hand (Participant.tracks / a fixed speaker). Set it
    // ONLY from such a manual binding, never from the track kind. Non-null: the track is not
    // clustered; its speech is one speaker (index 0) with DiarizedSpeaker::person = this id.
    PersonId knownPerson;

    bool hasInput() const { return !path.isEmpty() || !pcm.isEmpty(); }
};

struct DiarizeRequest {
    QVector<DiarizeTrack> tracks;
    // Optional known speakers (voiceprints of candidate people). A provider MAY use them to name
    // clusters; the first implementations ignore them (identification is IParticipantIdentifier's
    // job, afterwards).
    QVector<IdentifyCandidate> knownSpeakers;

    // ---- options (negative / empty = the provider's default) ----
    int     numClusters = -1;        // > 0: exactly this many speakers per (unbound) track
    double  threshold = -1.0;        // cosine-distance cut of the clustering (default 0.5); used when numClusters <= 0
    double  minDurationOn = -1.0;    // seconds: shorter speech segments are dropped (default 0.3)
    double  minDurationOff = -1.0;   // seconds: shorter gaps between segments of one speaker are bridged (default 0.5)
    QString segmentationModelId;     // VoiceModelRegistry id of the segmentation model; "" = default
    QStringList embeddingModelIds;   // speaker-embedding models; empty = the provider's enabled models

    bool isEmpty() const
    {
        for (const DiarizeTrack& t : tracks)
            if (t.hasInput()) return false;
        return true;
    }
};

// One stretch of one speaker's speech.
struct SpeakerTurn {
    QString trackId;
    qint64  startMs = 0;             // meeting time
    qint64  endMs = 0;
    int     speakerIndex = 0;        // inside the track (see DiarizedSpeaker::index)
    bool    overlap = false;         // another speaker talks at the same time during (part of) the turn

    qint64 durationMs() const { return endMs - startMs; }
};

// One speaker of one track (one cluster).
struct DiarizedSpeaker {
    QString trackId;
    int     index = 0;               // unique inside the track
    qint64  totalMs = 0;             // total speech time of this speaker
    // The cluster centroid, ready for IdentifyRequest::probes. probe.trackId == trackId; probe.windows
    // = the number of embeddings averaged; probe.sampleRef = the longest clean span ("<file>#startMs-endMs").
    IdentifyProbe centroid;
    // Set only for a track bound by hand (DiarizeTrack::knownPerson); null for a clustered speaker.
    PersonId person;
};

struct DiarizeResult {
    QVector<SpeakerTurn>     turns;      // sorted by startMs (then by trackId, speakerIndex)
    QVector<DiarizedSpeaker> speakers;   // sorted by trackId, then index

    // The turns of one track, in order.
    QVector<SpeakerTurn> turnsOf(const QString& trackId) const
    {
        QVector<SpeakerTurn> out;
        for (const SpeakerTurn& t : turns)
            if (t.trackId == trackId) out.append(t);
        return out;
    }
    // The speaker (trackId, index); nullptr if there is none.
    const DiarizedSpeaker* speaker(const QString& trackId, int index) const
    {
        for (const DiarizedSpeaker& s : speakers)
            if (s.trackId == trackId && s.index == index) return &s;
        return nullptr;
    }
};

using DiarizeJob = ResultJob<DiarizeResult>;

class IDiarizer {
public:
    virtual ~IDiarizer() = default;
    virtual QString name() const = 0;
    // The job is the provider's child. A request without any input fails with Error::Kind::Input.
    virtual DiarizeJob* diarize(const DiarizeRequest& req) = 0;
};

template <> struct ProviderTraits<IDiarizer> {
    static constexpr CapabilityKind kind = CapabilityKind::Diarization;
};

} // namespace tanara::providers
