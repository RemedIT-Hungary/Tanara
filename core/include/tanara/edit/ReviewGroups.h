#pragma once
//
// "To review" groups: the doubtful lines arranged into groups, one decision (and one undo step)
// per group. Pure functions on a snapshot (ReviewInput); SpeakerEditor calls them on a background
// thread, the CLI (`tanara-cli review`) directly.
//
// Group kinds (by priority; a line is in at most one group):
//   SideConflict       - the line's side contradicts its speaker's side; suggestion: the best
//                        same-side, non-excluded candidate; if the manual track assignment excludes
//                        the current speaker and there is no such candidate: a new speaker on that
//                        side (proposeNew). One group per speaker and suggestion pair.
//   SimilarToNewPerson - after creating a new person, the lines similar to them (>= kNewPersonSimilarity)
//                        on the same side.
//   CoreMismatch       - measured against the confirmed cores, closer to another core by >= kCoreMargin;
//                        per (current, proposed) pair.
//   ContaminatedCore   - a speaker's confirmed lines fall into two voices by 2-means (cluster
//                        similarity < kContamMaxSimilarity, both halves >= kContamMinShare) ->
//                        suggestion: split (the smaller half goes to the best candidate, or to a new
//                        anonymous participant). These are locked lines, so they do not overlap other groups.
//   ShortLines         - < kMinEmbedMs, no embedding; informational only (no suggestion).
//
// "Unidentifiable" lines (OverlayUtterance::ignored) go into none of the kinds and do not count in
// the ranking's profiles (line count, line sum, confirmed core) either.
#include "tanara/domain/Meeting.h"
#include "tanara/edit/CandidateRanker.h"
#include "tanara/domain/Participant.h"
#include "tanara/edit/SideAnalysis.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/domain/Activity.h"

#include <QHash>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

enum class ReviewKind { SideConflict, CoreMismatch, ShortLines, ContaminatedCore, SimilarToNewPerson };

struct ReviewGroup {
    QString id;                 // stable for the (kind, current, proposed) triple; the key of applyReviewGroup
    ReviewKind kind = ReviewKind::ShortLines;
    QString title;              // Hungarian, short
    QString subtitle;
    QStringList utteranceIds;   // in time order
    QString currentSpeakerKey;
    QString proposedSpeakerKey; // empty: proposedPersonName, or a new anonymous participant (ShortLines: no suggestion)
    QString proposedPersonName; // the proposed person (the key is empty for a candidate outside the meeting)
    // SideConflict: the manual track assignment excludes the current speaker and there is no known
    // candidate on the line's side -> suggestion: a NEW anonymous speaker on this side (proposedSide:
    // Local / Remote). The key and name are empty then; applying (applyReviewGroup) creates a new participant.
    bool proposeNew = false;
    Side proposedSide = Side::Unknown;
    QVector<Evidence> evidence;
    int confirmedBasis = 0;     // this many confirmed lines are the basis of the decision
};

QString reviewKindName(ReviewKind k);   // "sideConflict" | "coreMismatch" | … (CLI / JSON)

// Input of the grouping: a snapshot of the meeting (a value copy, can go to a background thread).
struct ReviewInput {
    Meeting meeting;                                    // speakerMap, tracks, tagIds
    QVector<TranscriptLine> lines;
    SpeakerOverlay overlay;
    QHash<QString, QVector<float>> embeddings;          // utterance id -> fused, L2-normalized vector
    QString userName;
    QHash<QString, QVector<QVector<float>>> personPrints;   // person name -> fused voiceprints (L2)
    TagEvidenceFn tagEvidence;                          // empty = no tag evidence (PersonTags.h)
    SideHints sideHints;                                // learned sides (the manual assignment comes from the overlay)
    QString newPersonKey;                               // the person just created (SimilarToNewPerson)
};

struct ReviewResult {
    SideReport sides;
    ranking::RankContext context;
    QVector<ReviewGroup> groups;
    // Line id -> the speaker key it contradicts (non-locked lines; "uncertain - track").
    QHash<QString, QString> sideConflicts;
};

namespace review {

inline constexpr double kCoreMargin = 0.05;
inline constexpr int    kContamMinLines = 6;
inline constexpr double kContamMaxSimilarity = 0.70;
inline constexpr double kContamMinShare = 0.20;
inline constexpr double kNewPersonSimilarity = 0.60;
inline constexpr int    kKMeansIterations = 12;

// Split check of a speaker's confirmed core.
struct CoreSplit {
    bool contaminated = false;
    double similarity = qQNaN();    // cosine of the two cluster centroids
    QStringList minority;           // lines of the smaller half (in time order)
    QStringList majority;
    QVector<float> minorityCentroid;
};
CoreSplit splitConfirmedCore(const ReviewInput& in, const SideReport& sides, const QString& speakerKey);

// Spherical 2-means on L2 vectors (deterministic start: the least similar pair). Returns 0 / 1
// per line; empty if fewer than 2 vectors.
QVector<int> twoMeans(const QVector<QVector<float>>& vectors, int iterations = kKMeansIterations);

} // namespace review

// The ranking context: the meeting's speakers (confirmed core, line sum, side, voiceprint, tags)
// and the people outside the meeting who have a voiceprint.
ranking::RankContext buildRankContext(const ReviewInput& in, const SideReport& sides);

// The groups from the finished side analysis and context.
QVector<ReviewGroup> buildReviewGroups(const ReviewInput& in, const SideReport& sides,
                                       const ranking::RankContext& ctx);

// The whole thing: side analysis (from the activity) + context + groups.
ReviewResult analyzeReview(const ReviewInput& in, const MeetingActivity& activity);

} // namespace tanara

Q_DECLARE_METATYPE(tanara::ReviewGroup)
