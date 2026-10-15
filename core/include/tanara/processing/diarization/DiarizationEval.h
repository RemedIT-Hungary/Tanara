#pragma once
//
// Evaluation of the local diarization (D6), pure functions without I/O or ONNX Runtime:
//
//   1. DER (diarization error rate, the NIST md-eval definition) of hypothesis turns against reference
//      turns, frame-based (10 ms by default): per frame Nref / Nhyp active speakers, Ncorrect = the
//      reference speakers whose MAPPED hypothesis speaker is active too. The mapping hyp -> ref is the
//      one-to-one assignment with the largest total overlap (the Hungarian algorithm of SegmentationMath).
//        missed      = sum max(0, Nref - Nhyp)
//        falseAlarm  = sum max(0, Nhyp - Nref)
//        confusion   = sum min(Nref, Nhyp) - Ncorrect
//        DER         = (missed + falseAlarm + confusion) / sum Nref
//      A collar (default 0.25 s) around every reference turn boundary is not scored (the usual "forgiveness
//      collar" for imprecise reference boundaries), and neither are the `excluded` regions. Overlapped
//      speech is scored (every speaker of a frame counts).
//   2. The speaker-count error: hypothesis speakers - reference speakers (signed).
//   3. The reference format: a JSON file (format "tanara-diarization-ref", see parseReferenceJson) or
//      RTTM (SPEAKER <file> <chan> <start> <dur> <NA> <NA> <speaker> ...).
//   4. A reference from a transcript's provider tokens (buildTrackReference): tokens -> per-speaker
//      utterances (gap <= maxGapSec) -> padded (emission-time tokens are points, not ranges) -> turns.
//
// Everything is deterministic and unit-tested with synthetic turns (test_diarization_eval).
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::processing::diareval {

// One turn of one speaker, in seconds (any time base; the reference and the hypothesis must share it).
struct EvalTurn {
    double start = 0;
    double end = 0;
    QString speaker;
};

struct Interval {
    double start = 0;
    double end = 0;
};

struct DerOptions {
    double collar = 0.25;            // seconds on BOTH sides of every reference boundary (not scored)
    double step = 0.01;              // the frame length of the scoring
};

struct DerResult {
    double scored = 0;               // reference speech in the scored region (speaker-seconds)
    double missed = 0;
    double falseAlarm = 0;
    double confusion = 0;
    int refSpeakers = 0;
    int hypSpeakers = 0;
    QHash<QString, QString> mapping; // hyp speaker -> ref speaker (the mapped pairs only)

    double errorSeconds() const { return missed + falseAlarm + confusion; }
    // The error rate; 0 if nothing was scored (and no false alarm), 1 if only false alarm.
    double der() const;
    double missedRate() const { return scored > 0 ? missed / scored : 0.0; }
    double falseAlarmRate() const { return scored > 0 ? falseAlarm / scored : 0.0; }
    double confusionRate() const { return scored > 0 ? confusion / scored : 0.0; }
    int speakerCountError() const { return hypSpeakers - refSpeakers; }
    // Sums the seconds and the speaker counts (aggregating tracks / meetings; the mapping is dropped).
    DerResult& operator+=(const DerResult& o);
};

// The distinct speakers of the turns (with a positive duration).
QStringList speakersOf(const QVector<EvalTurn>& turns);

// DER of `hyp` against `ref` over [0, end of the latest turn], minus `excluded` and the collars.
DerResult computeDer(const QVector<EvalTurn>& ref, const QVector<EvalTurn>& hyp, const DerOptions& opts = {},
                     const QVector<Interval>& excluded = {});

// ---- reference files ----

// One track of a reference: its turns and the regions not to score (meeting time, seconds).
struct TrackReference {
    QString trackId;
    QString file;                    // the track file name (informational)
    qint64 startOffsetMs = 0;
    QVector<EvalTurn> turns;
    QVector<Interval> excluded;
};

struct Reference {
    QString source;                  // "soniox", "rttm", ...
    QStringList notes;               // how it was built, its limits
    QVector<TrackReference> tracks;

    const TrackReference* track(const QString& trackId) const;
};

// The JSON format: {"format": "tanara-diarization-ref", "version": 1, "source": "...", "notes": [...],
// "tracks": [{"trackId", "file", "startOffsetMs", "turns": [{"start", "end", "speaker"}],
// "excluded": [[start, end], ...]}]}. Unknown keys are ignored. nullopt-like: an empty Reference + *error.
Reference parseReferenceJson(const QByteArray& json, QString* error = nullptr);
QJsonObject referenceToJson(const Reference& ref);
// RTTM: every SPEAKER line becomes a turn of the track named by its file column (one track per file id).
Reference parseRttm(const QString& text, QString* error = nullptr);
QString toRttm(const QString& fileId, const QVector<EvalTurn>& turns);

// ---- a reference from provider tokens ----

struct RefToken {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString speaker;                 // after the speaker map (a person key or the raw label)
};

struct RefBuildOptions {
    double maxGapSec = 1.0;          // tokens of one speaker closer than this form one utterance
    double leadSec = 0.3;            // emission-time padding (sonioxtiming::kLeadMs)
    double tailSec = 0.1;            // (sonioxtiming::kTailMs)
};

// The tokens of one speaker -> utterances (sorted, gap-merged, padded, overlapping ones merged).
QVector<Interval> utterancesOf(QVector<RefToken> tokens, const RefBuildOptions& opts = {});
// All tokens -> turns per speaker (utterancesOf per speaker), sorted by start.
QVector<EvalTurn> turnsFromTokens(const QVector<RefToken>& tokens, const RefBuildOptions& opts = {});

// Which track a speaker belongs to, from per-utterance votes: votes[track] = the seconds of the speaker's
// utterances where that track was the loudest relative to its own speech level. The winner needs at
// least `majority` of all voted seconds; otherwise "" (ambiguous).
QString assignTrack(const QHash<QString, double>& votes, double majority = 0.75);

} // namespace tanara::processing::diareval
