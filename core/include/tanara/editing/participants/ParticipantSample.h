#pragma once
//
// Participant sample: which span of which track to play so the user can LISTEN to a participant
// before naming them (PlayerController::playRange(trackId, startMs, endMs), meeting time throughout).
//  - before the transcript: the participant's persisted sample (Participant::sampleRef, copied from
//    the largest voice cluster), failing that the largest cluster of the voice analysis with a sample;
//  - after the transcript: the speaker's longest clean line (not noisy / ignored, if there is one),
//    on the track of the participant's cluster that overlaps it most (failing that the persisted
//    sample's track), capped at kMaxSampleMs.
// The pure parts live here (data in, sample out); ParticipantService::participantSamples() gathers the
// data (the meeting, the analysis clusters through ParticipantAnalysisPort, the editor's lines).
#include "tanara/domain/Activity.h"
#include "tanara/domain/Participant.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::editing {

struct EditorUtterance;

// A listenable sample of a participant: one span of one track, in MEETING time (what
// PlayerController::playRange(trackId, startMs, endMs) takes). Before the transcript it is the
// voice cluster's representative window; after it, the speaker's longest line.
struct ParticipantSample {
    QString trackId;
    qint64 startMs = -1;
    qint64 endMs = -1;
    bool isValid() const { return !trackId.isEmpty() && startMs >= 0 && endMs > startMs; }
};

namespace participantsample {

inline constexpr qint64 kMaxSampleMs = 15000;

// "<file>#startMs-endMs" -> the span (trackId filled by the caller); invalid if malformed.
ParticipantSample parseSampleRef(const QString& trackId, const QString& sampleRef);

// The participant's persisted sample (Participant::sampleTrackId + sampleRef); invalid if none.
ParticipantSample storedSample(const Participant& p);

// The participant's largest cluster that has a sample; invalid if it has none.
ParticipantSample clusterSample(const QVector<VoiceCluster>& clusters, const QString& participantId);

// The line [startMs, endMs] as a sample on the participant's best-overlapping cluster track (capped at
// kMaxSampleMs); invalid if the participant has no cluster.
ParticipantSample lineSample(const QVector<VoiceCluster>& clusters, const QString& participantId,
                             qint64 startMs, qint64 endMs);

// The longest line of the given speakers: a clean one (not noisy, not ignored) wins over a longer noisy
// one. nullptr if the speakers have no line.
const EditorUtterance* longestLine(const QVector<EditorUtterance>& utterances, const QStringList& speakerKeys);

// The meeting-local "same voice" merges (Participant::mergedFrom) applied to the analysis clusters: a
// cluster of a merged-away participant belongs to the participant it was merged into.
QVector<VoiceCluster> withMerges(QVector<VoiceCluster> clusters, const QVector<Participant>& participants);

} // namespace participantsample
} // namespace tanara::editing
