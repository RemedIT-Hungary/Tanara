#pragma once
//
// Remote lines from the voices of THIS meeting: the owner of a remote (loopback-side) transcript line comes
// from clustering the line embeddings of the remote side, not from the STT provider's raw speaker id (which
// on a mixdown often holds several people, or one person under several ids). Pure functions over data (no
// I/O, no audio, no models), so they are unit-testable with synthetic vectors; the editor session builds the
// input (SpeakerEditSession::planRemoteAssignment) and applies the result as one undo step.
//
// The algorithm:
//  1. Seeds. A Remote line (sides::classifyLineSides) that is not locked, not ignored, has a vector and is at
//     least seedMinMs long is a seed. A locked (confirmed / corrected) line with a vector whose owner is not a
//     local target is a must-keep seed: the locked lines of one owner start as ONE cluster, and two clusters
//     locked to different owners never merge (cannot-link). So every correction improves the next run.
//  2. Agglomerative clustering, average linkage: the duration-weighted mean of the pairwise cosines (weight =
//     min(duration, maxWeightMs); the fused multi-model vector, so the cosine is the mean over the models); the
//     clusters' centroids are duration-weighted. (Centroid linkage is kept as an option: a centroid of many noisy
//     lines resembles every other centroid, so it fuses large clusters.)
//     Merging stops when the best pair is below mergeThreshold: the threshold is set to over-split rather than
//     fuse two people (one person in two clusters is one merge for the user; two people in one cluster is
//     line-by-line work).
//  3. Tiny clusters (below minClusterMs of seed audio or minClusterLines lines, without a locked line) are
//     dissolved: their lines are assigned afterwards like short lines.
//  4. Binding a cluster to a target: (a) the locked lines' owner, if they are at least lockedMinShare of the
//     cluster's audio (a smaller share yields to (b) / (c), but still wins over anonymous); (b) the pre-transcript voice windows of the
//     targets (VoiceCluster::windows of the participant analysis): the target whose windows overlap the
//     cluster's lines the most, if that is at least bindMinOverlapMs and bindMinShare of all the overlap with
//     remote windows (also those of unnamed analysis voices); (c) the voiceprint match of the centroid
//     (>= printMatch, by printMargin ahead of the next target). Several clusters may bind to the same target.
//     A cluster with no target becomes an anonymous voice ("anon:<n>"): the caller reuses an anonymous speaker
//     of the meeting or creates a new anonymous participant.
//  5. Lines without a cluster (short lines, lines of dissolved clusters, Mixed / Unknown lines not owned by a
//     local target, lines without a vector): the nearest cluster when clearly closest (fit >= nearestMinFit and
//     nearestMargin / nearestMarginShort ahead of the next target); otherwise the temporal neighbours of the
//     same turn (the previous / next decided line within turnGapMs), the provider's raw speaker as the
//     tie-breaker; otherwise the target the same raw speaker's lines went to around this time. Such a weak
//     decision is marked (LineDecision::weak + the runner-up as hint): the caller shows it as uncertain; for a
//     line without a vector a weak guess only becomes the suggestion, the line keeps its current non-local owner.
//  6. Never: a locked line is not changed; a Local line is not touched (the local side is the line-slicing /
//     approval's job); a Remote line never goes to a local target.
//
// Single-track meetings (no side knowledge: every line Unknown): Options::sideKnown == false clusters every
// line the same way; the caller only does that if it measured it is not worse than the raw mapping (see
// kClusterSingleTrack).
#include "tanara/editing/review/SideAnalysis.h"

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtNumeric>

namespace tanara::editing::remotelines {

// ---- thresholds (tuned on a 114-minute 1 local + 4 remote meeting; see the slice report) ----
// A line seeds the clusters from this length on (shorter vectors are noisy).
inline constexpr qint64 kSeedMinMs = 2000;
// Merge while the two closest clusters are at least this similar: the duration-weighted mean pairwise cosine of
// their lines (average linkage; fused vectors). Measured: 0.25..0.40 give the same 4 remote voices on the evaluation
// meeting, 0.20 fuses them into 2; same-person line pairs average 0.32..0.57, different people 0.07..0.41.
inline constexpr double kMergeThreshold = 0.30;
// A cluster with less seed audio / fewer lines than this (and no locked line) is dissolved.
inline constexpr qint64 kMinClusterMs = 15000;
inline constexpr int    kMinClusterLines = 3;
// A line weighs its duration in its cluster's centroid, at most this much (= the embedding window).
inline constexpr qint64 kMaxWeightMs = 10000;
// Nearest cluster for a line outside the clusters: at least this fit, and this far ahead of the next target.
inline constexpr double kNearestMinFit = 0.30;
inline constexpr double kNearestMargin = 0.10;
inline constexpr double kNearestMarginShort = 0.15;   // a line shorter than kSeedMinMs
// A seed line is weak (uncertain) if another target's cluster fits it nearly as well as its own.
inline constexpr double kWeakMargin = 0.05;
// Same turn: the gap to the previous / next decided line at most this long.
inline constexpr qint64 kTurnGapMs = 2000;
// The raw speaker's recent owner: decided lines of the same raw speaker within this distance.
inline constexpr qint64 kRawWindowMs = 120000;
// Binding by the pre-transcript voice windows.
inline constexpr qint64 kBindMinOverlapMs = 6000;
inline constexpr double kBindMinShare = 0.50;
// A cluster's locked lines decide its owner when they are at least this share of its seed audio; a smaller
// locked share yields to the windows / voiceprint evidence (and still wins over "anonymous").
inline constexpr double kLockedMinShare = 0.20;
// Binding by voiceprint.
inline constexpr double kPrintMatch = 0.50;
inline constexpr double kPrintMargin = 0.05;
// Single-track meetings are left unchanged: measured on the evaluation meeting with the side knowledge withheld,
// clustering every line moved 13 of the local person's 72 lines away (97% -> 79% right).
inline constexpr bool kClusterSingleTrack = false;

// Linkage of the clustering: the duration-weighted mean of the pairwise cosines (average) or the cosine of
// the duration-weighted centroids (centroid).
enum class Linkage { Average, Centroid };

struct Options {
    Linkage linkage = Linkage::Average;
    qint64 seedMinMs = kSeedMinMs;
    double mergeThreshold = kMergeThreshold;
    qint64 minClusterMs = kMinClusterMs;
    int    minClusterLines = kMinClusterLines;
    qint64 maxWeightMs = kMaxWeightMs;
    double nearestMinFit = kNearestMinFit;
    double nearestMargin = kNearestMargin;
    double nearestMarginShort = kNearestMarginShort;
    double weakMargin = kWeakMargin;
    qint64 turnGapMs = kTurnGapMs;
    qint64 rawWindowMs = kRawWindowMs;
    qint64 bindMinOverlapMs = kBindMinOverlapMs;
    double bindMinShare = kBindMinShare;
    double lockedMinShare = kLockedMinShare;
    double printMatch = kPrintMatch;
    double printMargin = kPrintMargin;
    // false: a single-track meeting (no side knowledge): every non-locked line is clustered and any target
    // (also a local one) may own a cluster.
    bool sideKnown = true;
    // A weak cluster line inside another target's turn (both neighbours within turnGapMs agree) follows the turn.
    bool smoothTurns = true;
    // A weak turn / raw-speaker decision for a line without a vector keeps the line's current (non-local) owner and
    // only suggests the guess (the line is marked uncertain).
    bool keepWeakShort = true;
};

struct Line {
    QString id;
    qint64  startMs = 0;
    qint64  endMs = 0;
    QString rawLabel;               // the provider's raw speaker (a weak hint: tie-breaker only)
    Side    side = Side::Unknown;   // sides::classifyLineSides
    QVector<float> embedding;       // the fused vector (L2-normalized); empty = none
    QString lockedTarget;           // confirmed / corrected: the owner's target id; empty = not locked
    QString currentTarget;          // the owner now (target id; may be empty)
    bool    ignored = false;        // "unidentifiable": its voice is not used, the line is not changed
    qint64 durationMs() const { return endMs - startMs; }
};

struct Target {
    QString id;                     // the caller's opaque id (a speaker key, or "person:<uuid>")
    bool    local = false;          // the local participant: never gets a Remote line or a cluster
    bool    named = false;          // has a person (only a named target is bound by windows / voiceprint)
    QVector<QPair<qint64, qint64>> windows;   // its pre-transcript voice windows on the remote side
    QVector<QVector<float>> prints;           // its voiceprints, fused like the lines (L2-normalized)
};

// What a line's decision is based on.
enum class Basis { Unchanged, Cluster, Nearest, Turn, Raw };
QString basisName(Basis b);   // "unchanged" | "cluster" | "nearest" | "turn" | "raw"

struct LineDecision {
    QString target;                 // target id or "anon:<n>"; empty = unchanged
    Basis   basis = Basis::Unchanged;
    bool    weak = false;           // a weak decision: show it as uncertain
    QString hint;                   // weak: the runner-up target (may be empty)
    int     cluster = -1;           // the line's cluster (seed lines), else -1
};

struct Cluster {
    QVector<int> lines;             // seed / locked line indexes (time order)
    qint64  seedMs = 0;             // their total duration
    int     lockedLines = 0;
    QVector<float> centroid;        // L2-normalized, duration-weighted
    QString target;                 // the bound target id, or "anon:<n>"
    QString bindBasis;              // "locked" | "windows" | "voiceprint" | "anonymous"
    qint64  windowMs = 0;           // overlap with the bound target's windows
    double  windowShare = qQNaN();  // ... as a share of all remote-window overlap
    double  printScore = qQNaN();   // the bound target's voiceprint score
};

struct Result {
    bool ran = false;
    QString skipped;                // why it did not run ("no-seeds", "single-track"); empty when it ran
    QVector<LineDecision> lines;    // per input line
    QVector<Cluster> clusters;      // surviving clusters (the dissolved ones are not listed)
    int seeds = 0;                  // seed lines (without the locked ones)
    int dissolved = 0;              // tiny clusters dissolved
    int anonymous = 0;              // anonymous voices ("anon:1".."anon:<n>")
};

// otherWindows: remote-side pre-transcript windows of voices with no named target (they count in the share).
Result assign(const QVector<Line>& lines, const QVector<Target>& targets,
              const QVector<QPair<qint64, qint64>>& otherWindows = {}, const Options& opt = {});

inline bool isAnonymous(const QString& target) { return target.startsWith(QLatin1String("anon:")); }

} // namespace tanara::editing::remotelines
