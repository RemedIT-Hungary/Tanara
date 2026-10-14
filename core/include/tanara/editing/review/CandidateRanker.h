#pragma once
//
// CandidateRanker: candidate ranking with evidence: who comes into question for a line or a whole
// speaker, and why. Pure functions on a prebuilt context (RankContext; buildRankContext() in
// ReviewGroups.h builds it from the meeting's data); no I/O.
//
// Score = voice (cosine to the candidate's reference, 0..1) * kVoiceWeight
//       + side (same side: +kSideSupport; opposite side: -kSidePenalty, otherSide)
//       + tag (PersonTagSnapshot::evidence: manual OR learned: +kTagSupport, once; after the strict
//         contradiction guards: -kTagMismatch; UX plan rule 4)
//       + lines here (kLineWeight * min(1, lines / kLineSaturation)).
// The forbidden (other) side ranks LOWER, it does not remove: except the manual track assignment's
// exclusion (sides::vetoes): then the candidate is vetoed, without a score, at the end of the list.
// The similarity warning (two cores' cosine >= kSimilarWarn) only informs, it does not affect the score.
#include "tanara/domain/Participant.h"
#include "tanara/editing/review/SideAnalysis.h"
#include "tanara/library/tags/PersonTags.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace tanara::editing {

// Seam of the tag evidence: TagEvidenceFn (library/tags/PersonTags.h): the manual tag AND the learned
// association (c(p,t)) together, the same rule in the candidate ranking and in "Who was there?".

namespace ranking {

inline constexpr double kVoiceWeight = 1.0;
inline constexpr double kSideSupport = 0.10;
inline constexpr double kSidePenalty = 0.40;
// Tag: a ranking advantage, not a decision (UX plan rule 4): supports +0.04 (manual and learned
// equally, not cumulative), contradicts -0.02.
inline constexpr double kTagSupport = 0.04;
inline constexpr double kTagMismatch = 0.02;
inline constexpr double kLineWeight = 0.05;
inline constexpr int    kLineSaturation = 20;
// Two speakers' confirmed cores more similar than this -> a warning ("Review the two").
inline constexpr double kSimilarWarn = 0.90;
// Polarity of the voice evidence: above this it supports, below it contradicts (neutral in between).
inline constexpr double kVoiceGood = 0.50;
inline constexpr double kVoiceBad = 0.30;
// Without a core, the speaker's line centroid counts as the reference from at least this many lines (excluding the probe).
inline constexpr int kMinReferenceLines = 2;

// A candidate (a speaker of the meeting, or a known person outside the meeting) for ranking.
struct SpeakerProfile {
    QString key;                // empty = a person outside the meeting
    PersonId personId;          // null = an anonymous speaker
    QString displayName;
    Side    side = Side::Unknown;       // the speaker's side (SideAnalysis PersonSide)
    QString sideBasis;                  // PersonSide::basis
    // The side of the PERSON's manual track assignment (tracks of same-named speakers together; for an
    // anonymous one the speaker's own): Local / Remote / Mixed; Unknown = no manual assignment (never excluded).
    Side    manualSide = Side::Unknown;
    Side    lineSide = Side::Unknown;   // side by the majority of its lines (the speaker as a "probe")
    int     lines = 0;                  // its lines here
    int     confirmedLines = 0;         // lines of the confirmed core (0 = no core)
    QVector<float>  core;               // centroid of the confirmed, clean lines (L2); empty = no core
    QVector<double> sum;                // sum of all its clean, embedded lines (for leave-one-out)
    int     sumCount = 0;
    QVector<QVector<float>> prints;     // the person's stored voiceprints (fused, L2)
    library::TagEvidence tagEvidence;            // the person's tag evidence on this meeting
};

struct RankContext {
    QVector<SpeakerProfile> speakers;   // the meeting's speakers, then the people outside the meeting
    QStringList meetingTags;
    QHash<QString, int> index;          // speaker key -> speakers index
    bool sidesActive = false;
    // Condition 1 of the track veto (sides::allNamedSpeakersBound): every named speaker has a manual track assignment.
    bool vetoActive = false;
    const SpeakerProfile* profile(const QString& key) const;
};

// A "probe": the voice + side of one line or one speaker.
struct Probe {
    QVector<float> embedding;   // L2; empty = no voice
    Side    side = Side::Unknown;
    QString currentKey;         // the probe's current speaker (also appears as a Candidate)
    bool    inCurrentSum = false;   // the probe's voice is part of the current speaker's sum
};

double cosine(const QVector<float>& a, const QVector<float>& b);   // for L2 vectors: dot product
QVector<float> normalized(const QVector<double>& sum);

// The candidate's voice score for the probe (NaN = no comparable reference). *basis: "core" |
// "lines" | "prints" (the kind of the best-fitting reference).
double voiceScore(const SpeakerProfile& p, const Probe& probe, QString* basis = nullptr);

// All candidates (the current speaker too) by descending score; the vetoed ones at the end.
QVector<Candidate> rank(const RankContext& ctx, const Probe& probe);
// One line: the line's voice + its side.
QVector<Candidate> rankForLine(const RankContext& ctx, const QVector<float>& embedding, Side lineSide,
                               const QString& currentKey, bool inCurrentSum);
// A whole speaker ("Not them? Actually..."): the speaker's centroid + the side by its lines. The
// speaker itself is not included.
QVector<Candidate> rankForSpeaker(const RankContext& ctx, const QString& speakerKey);
// "Why them?": evidence for and against the speaker (voice to their voiceprints, side, tag, lines,
// a similar-sounding other speaker).
QVector<Evidence> selfEvidence(const RankContext& ctx, const QString& speakerKey);
// "Why not X?": the candidate's contradicting evidence (with a fix link).
QVector<Evidence> whyNot(const Candidate& c);
// Warnings about similar-sounding speakers (core cosine >= kSimilarWarn) (fixTarget "pair:<key>").
QVector<Evidence> similarityWarnings(const RankContext& ctx, const QString& speakerKey);

// Hungarian caption for a side ("mic", "call audio", "mixed", "unknown").
QString sideLabel(Side s);
QString evidenceKindName(EvidenceKind k);   // "voice" | "side" | … (CLI / JSON)
QString polarityName(Polarity p);           // "support" | "contradict" | "neutral"
// The tag evidence as Evidence: text is the chip ("#Nordvik", "#Nordvik +1", "other tags" on a
// contradiction), detail the explanatory sentence with numbers ("... (11 / 14)"). false: no evidence.
// personName: the person's display name (empty = no name in the sentence).
bool tagEvidenceOf(const library::TagEvidence& tag, const QString& personName, Evidence* out);

} // namespace ranking
} // namespace tanara::editing