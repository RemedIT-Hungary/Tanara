#pragma once
//
// Voice-based analysis for the speaker editor: pure functions over the utterance embeddings
// (uncertain lines + the "similar lines" suggestion) and the thresholds. UI- and I/O-independent,
// so it is unit-testable and the validation harness calls it too.
//
// The thresholds were tuned on sample meetings (CAM++ 16 kHz, mixdown); see the measurement in
// tests/unit/test_speaker_sandbox.cpp (runs with the TANARA_SANDBOX environment variable).
#include <QVector>
#include <QtGlobal>

#include <algorithm>

namespace tanara {
namespace speakeredit {

// ---- utterance embedding -------------------------------------------------
// No embedding is computed from a line shorter than this ("Aha", "Yes"): it is unreliable, so
// such a line is NEVER uncertain and never enters a suggestion; it stays manual work.
inline constexpr qint64 kMinEmbedMs = 1500;
// For a long monologue, this much from the middle is enough (time grows with the line count, not the length).
inline constexpr qint64 kMaxEmbedMs = 10000;
// From this length on, a line's embedding is considered reliable.
inline constexpr qint64 kReliableMs = 3000;

// ---- uncertainty --------------------------------------------------------
// A speaker's voice centroid needs at least this many (embedded) lines; below that we do not judge.
inline constexpr int kMinSpeakerLines = 3;
// Uncertain if ANOTHER speaker's centroid fits better by this margin (cosine difference)...
inline constexpr double kUncertainMargin = 0.10;
// ...a short line (kMinEmbedMs..kReliableMs) needs a stricter margin (noisier embedding).
inline constexpr double kUncertainMarginShort = 0.20;
// ...or if a reliable long line fits its OWN speaker worse than this.
inline constexpr double kUncertainMinFit = 0.20;

// ---- "similar lines" suggestion ----------------------------------------------
// A line left at the source enters the suggestion if it is closer to the target by this much...
inline constexpr double kSuggestMargin = 0.10;
inline constexpr double kSuggestMarginShort = 0.18;
// ...and is also at least this similar to the target in absolute terms.
inline constexpr double kSuggestMinSimilarity = 0.35;
// If the centroids of the two split voices are more similar than this, it is not two people
// (measured: different speakers 0.15-0.35, sub-clusters of the same speaker ~0.85), so no suggestion.
inline constexpr double kSuggestMaxCentroidSimilarity = 0.60;

// ---- "deviating sample" flag ---------------------------------------------------
// After a positive (sample-giving) decision, the line's voice fits the target speaker's core
// (without the decided lines) worse than this: "This sample differs from X's other lines".
// The value is the limit of the candidate ranking's voice evidence (ranking::kVoiceGood = 0.50:
// above it "supports"), so the warning fires exactly when the "Why them?" voice chip would not
// support the decision either. 0.50 sits between the measured values: different speakers
// 0.15-0.35, sub-clusters of the same speaker ~0.85 (see kSuggestMaxCentroidSimilarity);
// kUncertainMinFit (0.20) would be too lenient here: it only catches "not them at all", not a
// sample that drifted.
inline constexpr double kSampleDeviationFit = 0.50;

// ---- manual voiceprint -----------------------------------------------------
inline constexpr qint64 kPrintMinLineMs = 3000;     // only lines longer than this
inline constexpr qint64 kPrintMinTotalMs = 15000;   // at least this much material in total
inline constexpr qint64 kPrintTargetMs = 20000;     // we collect up to this much (from the longest)

// An utterance as input to the analysis.
struct AnalysisLine {
    const QVector<float>* embedding = nullptr;  // L2-normalized; nullptr/empty = none
    int    speaker = -1;                        // index of the current speaker in [0, speakerCount)
    qint64 durationMs = 0;
    bool   locked = false;                      // confirmed / manually corrected (never uncertain)
    bool   noisy = false;                       // "talked over each other": not a voice sample (see below)
    // "Unidentifiable" (OverlayUtterance::ignored): the line's voice is left out of EVERY branch of
    // the analysis (centroid, core, including kModeLockedAll which uses noisy locked lines, 2-means,
    // pair review, uncertainty). So hasEmbedding() is false here: every branch filters through it,
    // and a new branch cannot forget it.
    bool   ignored = false;
    bool hasEmbedding() const { return !ignored && embedding && !embedding->isEmpty(); }
};

// A line's fit: to its own speaker (centroid computed without itself) and to the best other.
// If not computable (no embedding / too few lines), the value is NaN.
struct LineFit {
    double own = qQNaN();
    double other = qQNaN();
    int    otherSpeaker = -1;
};

// Centroids skip the NOISY (talked-over) lines if the speaker has at least kMinSpeakerLines clean
// (embedded) lines without them; then a noisy line's own fit is measured against the full centroid
// (computed without it).
QVector<LineFit> computeFits(const QVector<AnalysisLine>& lines, int speakerCount);

// Per line: whether the assignment is uncertain (see the thresholds above).
QVector<bool> computeUncertain(const QVector<AnalysisLine>& lines, int speakerCount);

// ---- stored voiceprints in the reference (only for the explicit rechecks) ----
// The stored voiceprints (VoiceprintStore) of the person assigned to the speaker also join the
// reference of the recheck (computeUncertainRechecked) and the pair review (computePairRecheck).
// Weight rule (weight = ms-equivalent, as for lines):
//   * a confirmed / corrected line weighs its length (at most kMaxEmbedMs), as before;
//   * a voiceprint made in THIS meeting (localVectors) carries this recording's acoustics
//     (typically made because recognition went wrong here), so it is local evidence: it weighs
//     kMaxEmbedMs, adds to the local weight, is not capped, and forms a local core by itself;
//   * a voiceprint from ANOTHER meeting (vectors) weighs kPriorPrintMs, but all earlier
//     voiceprints together weigh at most kPriorMaxShare times the local weight, so the
//     meeting's own confirmed voice always dominates;
//   * if the speaker has NO local core (few locked lines and no local voiceprint), the earlier
//     voiceprints alone form the reference; if there is no voiceprint either, the old fallback
//     remains (all the speaker's clean lines).
// Against double counting: if a local voiceprint's sample line is itself a locked reference line,
// the line is skipped (replacedLines) and the voiceprint counts (it may span several lines).
// The automatic uncertainty judgment (computeUncertain) and the "similar lines" suggestion
// (suggestSimilar) are not affected.
inline constexpr qint64 kPriorPrintMs = 4000;       // weight of one earlier voiceprint
inline constexpr double kPriorMaxShare = 1.0 / 3.0; // total weight of earlier voiceprints <= local weight x this
inline constexpr qint64 kLocalPrintMs = kMaxEmbedMs; // weight of one local voiceprint

struct SpeakerPrior {
    QVector<QVector<float>> vectors;        // earlier voiceprints (from other meetings), L2-normalized
    QVector<QVector<float>> localVectors;   // voiceprints made in this meeting
    QVector<int> replacedLines;             // line indexes whose voice a local voiceprint already carries
    bool isEmpty() const { return vectors.isEmpty() && localVectors.isEmpty(); }
};

// What a speaker's reference was built from.
enum class ReferenceKind {
    None,       // no reference was built
    Local,      // local core: locked lines and/or local voiceprints (+ capped earlier voiceprints)
    Prior,      // no local core: only earlier voiceprints
    Fallback,   // neither local core nor voiceprint: all of the speaker's (clean) lines
};

struct ReferenceInfo {
    ReferenceKind kind = ReferenceKind::None;
    int  lines = 0;             // number of lines in the reference
    int  localPrints = 0;       // local voiceprints
    int  priorPrints = 0;       // earlier voiceprints
    bool priorCapped = false;   // the weight of earlier voiceprints was cut by the kPriorMaxShare cap
    double priorShare = 0.0;    // weight share of earlier voiceprints in the reference [0..1]
    bool refLocal() const { return kind == ReferenceKind::Local; }
    bool refPrior() const { return kind == ReferenceKind::Prior; }
    bool fallback() const { return kind == ReferenceKind::Fallback; }
};

// ---- recheck based on confirmed lines ---------------------------
// In the normal uncertainty judgment a speaker's centroid is also pulled by lines WRONGLY
// assigned to it, so the similar-sounding ones among them stay hidden. The recheck builds a
// "reliable" centroid: per speaker ONLY from their locked (confirmed / corrected) lines, if
// they have at least kMinSpeakerLines such (embedded) lines; among the clean ones if there are
// enough of those, and only otherwise together with the noisy ones. A speaker without enough
// locked lines keeps the normal centroid (all lines, with the clean-line rule). Then every
// NOT locked, NOT noisy, embedded line is measured against these with the same thresholds
// (kUncertainMargin / kUncertainMarginShort / kUncertainMinFit). The reliable centroid does not
// contain the line under test, so no leave-one-out is needed there.
struct RecheckVerdict {
    bool   uncertain = false;
    // The OTHER speaker that fits the voice decisively better (the suggestion); -1 = none (e.g. the
    // line fits neither its own speaker nor another: a new, unseparated voice).
    int    otherSpeaker = -1;
    double own = qQNaN();
    double other = qQNaN();
};

struct RecheckAnalysis {
    QVector<RecheckVerdict> lines;  // per line (in input order)
    QVector<bool> trustedCore;      // per speaker: the centroid was built only from their locked lines
    QVector<int>  coreLines;        // per speaker: how many locked lines form the core (0 = no core)
    QVector<ReferenceInfo> refs;    // per speaker: what the reference was built from (see SpeakerPrior)
    int coreSpeakers() const { return int(std::count(trustedCore.cbegin(), trustedCore.cend(), true)); }
    int coreLineTotal() const { int n = 0; for (int c : coreLines) n += c; return n; }
    int flagged() const {
        return int(std::count_if(lines.cbegin(), lines.cend(),
                                 [](const RecheckVerdict& v) { return v.uncertain; }));
    }
};

// priors: per speaker index the stored voiceprints (see SpeakerPrior); empty = lines only.
// The local core (trustedCore) may consist of a local voiceprint; a speaker without a local core
// but with earlier voiceprints gets the voiceprints as reference (refs[s].kind == Prior).
RecheckAnalysis computeUncertainRechecked(const QVector<AnalysisLine>& lines, int speakerCount,
                                          const QVector<SpeakerPrior>& priors = {});

// Whether at least one speaker has a reliable centroid buildable from their locked lines
// (or local voiceprint).
bool hasTrustedCore(const QVector<AnalysisLine>& lines, int speakerCount,
                    const QVector<SpeakerPrior>& priors = {});

// ---- "talked over each other" ---------------------------------------------------
// A line is noisy (not a good voice sample) if, within its embedding window (the middle of the
// line, at most kMaxEmbedMs), another speaker's line is sounding at the same time for this long...
inline constexpr qint64 kNoisyOverlapMs = 1000;
// ...or for at least this fraction of the window.
inline constexpr double kNoisyOverlapRatio = 0.30;

struct TimedLine {
    qint64 startMs = 0;
    qint64 endMs = 0;
    int    speaker = -1;            // speaker id (only equality matters)
};

// Per line: whether it is noisy because of overlap. Lines are expected in time order (by start time).
QVector<bool> computeOverlapNoisy(const QVector<TimedLine>& lines);

// Indexes of the lines left at sourceSpeaker (not locked, embedded) that are closer by voice to
// targetSpeaker. The target's current lines are the fixed "core" (including manually moved
// ones); a core-seeded 2-means runs on the source's lines so the MIXED source centroid does not
// skew the result (works even if the highlighted voice is the majority of the source).
QVector<int> suggestSimilar(const QVector<AnalysisLine>& lines, int sourceSpeaker,
                            int targetSpeaker);

// The same, with the reason for "why no suggestion": blockedBySimilarity = the voice splitting off
// the source resembles the remainder above kSuggestMaxCentroidSimilarity (the "not two people"
// guard), which is exactly what stays silent for two people with SIMILAR voices.
// centroidSimilarity: cosine of the two clusters (NaN if the 2-means did not run).
struct SuggestOutcome {
    QVector<int> lines;
    bool   blockedBySimilarity = false;
    double centroidSimilarity = qQNaN();
};
SuggestOutcome suggestSimilarDetailed(const QVector<AnalysisLine>& lines, int sourceSpeaker,
                                      int targetSpeaker);

// ---- pairwise review ("Review between A and B") ---------------------------
// The user stated that A and B are two people, even if their voices are similar. So there is NO
// centroid-similarity guard here (the value is only reported), and the threshold is milder: a line
// is doubtful if the OTHER speaker's reference fits it better by this much (cosine difference)...
inline constexpr double kPairMargin = 0.05;
// ...for a short (kMinEmbedMs..kReliableMs) line by this much.
inline constexpr double kPairMarginShort = 0.10;
// Above this reference similarity the UI warns: the two voices are very similar and the
// result is uncertain (measured: different speakers 0.15-0.35).
inline constexpr double kPairSimilarWarn = kSuggestMaxCentroidSimilarity;

struct PairVerdict {
    bool   flagged = false;
    int    hintedSpeaker = -1;      // if flagged: the better-fitting other speaker (speakerA / speakerB)
    double toA = qQNaN();           // cosine to A's reference (without itself, if it is in it)
    double toB = qQNaN();
};

struct PairRecheckAnalysis {
    QVector<PairVerdict> lines;     // per line (in input order)
    int  refLinesA = 0;             // A's reference was built from this many lines
    int  refLinesB = 0;
    // The reference was NOT built from locked lines (nor voiceprints) but from all the speaker's
    // clean (not noisy, embedded) lines; this may be contaminated by lines wrongly assigned to it.
    bool fallbackA = false;
    bool fallbackB = false;
    ReferenceInfo refA;             // what A's / B's reference was built from (see SpeakerPrior)
    ReferenceInfo refB;
    double centroidSimilarity = qQNaN();    // cos(A-referencia, B-referencia)
    bool valid = false;             // both references had kMinSpeakerLines lines
    int flagged() const {
        return int(std::count_if(lines.cbegin(), lines.cend(),
                                 [](const PairVerdict& v) { return v.flagged; }));
    }
};

// Reference per speaker: their locked, non-noisy, embedded lines if there are at least
// kMinSpeakerLines of them; otherwise (fallback) all their non-noisy, embedded lines. Then every
// line on A or B that is NOT locked, NOT noisy, at least kMinEmbedMs long and embedded is measured
// against the two references (against its own reference without itself, if it is in one), and
// is doubtful if the other reference fits it better by at least kPairMargin (kPairMarginShort for
// short ones). Other speakers' lines are not examined. priors: as in computeUncertainRechecked:
// a local voiceprint counts as a local core; without a local core the earlier voiceprints form
// the reference, and only if there are none comes the fallback. A reference from voiceprints is
// valid even if there are few lines.
PairRecheckAnalysis computePairRecheck(const QVector<AnalysisLine>& lines, int speakerA,
                                       int speakerB, const QVector<SpeakerPrior>& priors = {});

} // namespace speakeredit
} // namespace tanara
