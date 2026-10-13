#pragma once
//
// PRE-transcript voice analysis ("Who was there?"): voice windows from the speech spans of the
// track activity (TrackActivity), an embedding per window (VoiceEmbedderSet), agglomerative
// clustering per track, matching per cluster against the stored voiceprints -> participant
// candidates with evidence. Local, with no Soniox / paid service.
//
// Two steps so the heavy part runs on a background thread:
//  1. computeVoiceClusters: decoding + embedding + clustering (thread-safe: works only from the
//     given data, no store access; the PCM reader is replaceable, so tests use fake audio);
//  2. buildParticipants: matching + evidence + groups (on the main thread, through a ranking
//     function over the voiceprint DB).
// Once a transcript exists, bindRawSpeakers binds the clusters to the raw STT speakers by time
// overlap; a two-sided raw speaker (mic AND call) splits into two participants.
#include "tanara/domain/Audio.h"
#include "tanara/domain/Meeting.h"
#include "tanara/domain/Participant.h"
#include "tanara/domain/Voice.h"
#include "tanara/domain/Activity.h"
#include "tanara/domain/Overlay.h"
#include "tanara/edit/TrackActivity.h"
#include "tanara/tags/PersonTags.h"
#include "tanara/voiceid/EmbeddingSet.h"

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>

namespace tanara {

class VoiceEmbedderSet;

// One voice window on a track (in meeting time).
struct SpeechWindow {
    QString trackId;            // id of the logical track (leading span)
    QString side;               // "mic" | "loopback" | "" (other)
    qint64  startMs = 0;
    qint64  endMs = 0;
    qint64 durationMs() const { return endMs - startMs; }
};

// The persisted part of the analysis (<folder>/participants.analysis.json): the clusters'
// windows and matches (needed to bind to a transcript that arrives later).
struct ParticipantAnalysisFile {
    QString at;
    QStringList modelIds;
    QString tracksKey;              // fingerprint of the included tracks (activityFingerprint) at analysis time
    QVector<VoiceCluster> clusters;
    bool isEmpty() const { return at.isEmpty(); }
    static QString filePath(const QString& meetingFolder);
    static ParticipantAnalysisFile load(const QString& meetingFolder);
    bool save(const QString& meetingFolder) const;
};

namespace participants {

// Speech span: runs of speech frames (trackspeech::isSpeechFrame), joined across pauses of at
// most kMaxGapMs. Window: at least kMinWindowMs, at most kMaxWindowMs.
inline constexpr qint64 kMinWindowMs = 3000;
inline constexpr qint64 kMaxWindowMs = 10000;
inline constexpr qint64 kMaxGapMs = 400;
inline constexpr int    kMaxWindowsPerTrack = 60;
// Thresholds for cluster merging (fusion cosine) and voiceprint matching.
inline constexpr double kClusterThreshold = 0.55;
inline constexpr double kMatchThreshold = 0.5;
// Advantage of a manually added participant in matching (added to the score).
inline constexpr double kManualBoost = 0.1;
// Two-sided raw speaker: both sides' overlap is at least this fraction.
inline constexpr double kSplitMinShare = 0.25;

inline const QString kSideMic = QStringLiteral("mic");
inline const QString kSideLoopback = QStringLiteral("loopback");

QString sideOf(TrackKind kind);                 // Mic -> "mic", Loopback -> "loopback", other -> ""
QString sideLabel(const QString& side);         // Hungarian caption for the side ("mic" | "call" | "")

// A track's voice windows (in time order; if too many, evenly thinned to kMaxWindowsPerTrack).
QVector<SpeechWindow> speechWindows(const TrackActivity& t, const QString& side);

// Agglomerative clustering with a cosine centroid: per window, the cluster's label (the index of one member).
QVector<int> clusterEmbeddings(const QVector<QVector<float>>& embs, double mergeThreshold);

// Audio of a logical track's [startMs, endMs) span (16 kHz mono, [-1,1]); empty = none.
using PcmReader = std::function<QVector<float>(const QString& trackId, qint64 startMs, qint64 endMs)>;
// The real reader: from the track's (span) file, with the offset (tracktiming), via ffmpeg.
PcmReader filePcmReader(const Meeting& m);

// Step 1 (background thread). From the included tracks' activity: windows -> embedding -> per-track
// clusters (a one-window cluster counts as noise if there is another). progress(done,total) returning false cancels.
ClusterSet computeVoiceClusters(const Meeting& m, const MeetingActivity& activity,
                                const VoiceEmbedderSet& embedders, const PcmReader& pcm,
                                const std::function<bool(int, int)>& progress = {});

// Context of the candidates (filled on the main thread).
struct CandidateContext {
    QString selfName;                                                  // the microphone's owner
    std::function<QVector<VoiceMatch>(const EmbeddingSet&)> rank;      // voiceprint ranking, descending
    std::function<bool(const QString& person)> hasVoiceprint;
    // The tag evidence (manual tag and learned association; the same seam as the candidate ranking's:
    // PersonTagSnapshot::evidence). Empty = no tag evidence.
    TagEvidenceFn tagEvidence;
};

// Step 2: participants from the clusters. Existing manual participants stay (extended with
// evidence), earlier voice candidates are replaced by new ones; an approved name stays approved.
// Fills the participantId / matchName / matchScore fields of the clusters.
QVector<Participant> buildParticipants(ClusterSet& clusters, const Meeting& m,
                                       const CandidateContext& ctx);

// The participant's evidence recomputed (heard: whether we heard them; score: the voice score).
QVector<Evidence> participantEvidence(const Participant& p, bool heard, double score,
                                      const Meeting& m, const CandidateContext& ctx);

ParticipantGroup participantGroup(const Participant& p);
// Default of the approval dialog: has a name and no contradicting evidence.
bool defaultChecked(const Participant& p);

// Raw-speaker reference: "Speaker 1" (whole) or "Speaker 1@mic" / "@loopback" (one side of a
// two-sided raw speaker).
QString rawLabelOf(const QString& rawId);
QString rawSideOf(const QString& rawId);
QString rawId(const QString& rawLabel, const QString& side = QString());
// Display: "Speaker 1" or "Speaker 1 - mic" / "Speaker 1 - call" (Hungarian captions in the UI).
QString rawDisplay(const QString& rawId);

// Binds the clusters to the raw STT speakers by time overlap (rawSpeakerIds, talkShare).
// A two-sided raw speaker (both sides >= kSplitMinShare, with different best participants) is
// split into two participants ("@mic" / "@loopback"). true if anything changed.
bool bindRawSpeakers(QVector<Participant>& ps, const QVector<VoiceCluster>& clusters,
                     const QVector<TranscriptLine>& lines);

// A transcript line's side: the overlap of the cluster windows, failing that the track energy
// (activity may be null). "" = undecidable.
QString lineSide(const TranscriptLine& line, const QVector<VoiceCluster>& clusters,
                 const MeetingActivity* activity);

// The approval's bindings: the raw speakers of the checked (accepted) participants go to their
// names (for a two-sided raw speaker only that side's lines), raw speakers bound only to
// unchecked participants go anonymous.
// Track veto (needs meeting + activity): if every checked, named participant has manual tracks
// (Participant::tracks), the raw speaker's lines split BY SIDE (the line's side:
// sides::classifyLineSides): the person gets only the non-excluded lines; excluded ones go to
// the best (by its clusters) non-excluded participant of the other side, failing that they stay
// anonymous (the whole raw speaker is anonymous, the remaining lines go to the person via utteranceIds).
QVector<SpeakerBinding> approvalBindings(const QVector<Participant>& all, const QStringList& acceptedIds,
                                         const QVector<TranscriptLine>& lines,
                                         const QVector<VoiceCluster>& clusters,
                                         const MeetingActivity* activity, const Meeting* meeting = nullptr);

} // namespace participants
} // namespace tanara
