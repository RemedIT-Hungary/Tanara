#pragma once
//
// Participant sample lookup: which span of which track to play so the user can LISTEN to a
// participant before naming them. Shared by the Overview rows (AppContext) and the participants
// facade. Meeting time throughout (PlayerController::playRange(trackId, startMs, endMs)).
//  - clusterSample: before the transcript, the participant's voice cluster (the largest one by
//    speech time) and its representative window (VoiceCluster::sampleRef).
//  - lineSample: after the transcript, a given line (the speaker's longest) on the track of the
//    participant's cluster that overlaps it most; the span is capped at kMaxSampleMs.
//
#include "tanara/domain/Activity.h"
#include "tanara/editing/participants/IParticipantsFacade.h"

#include <QString>
#include <QVector>

namespace tanara_qml::participantsample {

inline constexpr qint64 kMaxSampleMs = 15000;

// "<file>#startMs-endMs" -> the span (trackId filled by the caller); invalid if malformed.
tanara::editing::ParticipantSample parseSampleRef(const QString& trackId, const QString& sampleRef);

// The participant's cluster sample; invalid if it has no cluster with a sample.
tanara::editing::ParticipantSample clusterSample(const QVector<tanara::VoiceCluster>& clusters,
                                                 const QString& participantId);

// The line [startMs, endMs] as a sample on the participant's best-overlapping cluster track;
// falls back to the participant's largest cluster's track; invalid if it has no cluster.
tanara::editing::ParticipantSample lineSample(const QVector<tanara::VoiceCluster>& clusters,
                                              const QString& participantId, qint64 startMs, qint64 endMs);

} // namespace tanara_qml::participantsample
