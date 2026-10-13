#pragma once
//
// VoiceEval: comparing voice models on a meeting (the measurement of `tanara-cli voice-eval`).
// A pure function over the per-line, per-model vectors, per model AND with the fused vector:
//  (1) coverage: for how many lines there is a vector;
//  (2) per speaker a core from the confirmed / corrected (locked), non-noisy lines, the pairwise
//      cosine matrix of the cores;
//  (3) per speaker 2-means separation: the cosine of the two sub-centroids and the lines' split;
//  (4) how many lines are closer to ANOTHER speaker's core by at least kVoiceEvalMargin.
// UI- and I/O-independent (reading is the CLI's job), so it is unit-testable.
#include "tanara/voiceid/EmbeddingSet.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

inline constexpr double kVoiceEvalMargin = 0.05;   // (4) threshold of "closer to another core"
inline constexpr int kVoiceEvalMinCoreLines = 3;    // this many locked lines are needed for a core
inline constexpr int kVoiceEvalMinSplitLines = 4;   // this many lines are needed for the 2-means

// One line as input of the measurement.
struct VoiceEvalLine {
    QString id;
    qint64  durationMs = 0;
    int     speaker = -1;        // index in the speakers list
    bool    locked = false;      // confirmed / manually corrected
    bool    noisy = false;       // talked over each other: not used in a core
    EmbeddingSet vectors;        // per model (L2-normalized); empty = none
};

struct VoiceEvalSpeaker {
    QString key;                 // speaker key (raw label / participant:N)
    QString name;                // displayed name
};

struct VoiceEvalSplit {
    int    lines = 0;            // the speaker's lines with a vector, non-noisy
    int    sizeA = 0;            // size of the two clusters (sizeA >= sizeB)
    int    sizeB = 0;
    double centroidCosine = qQNaN();   // cosine of the two sub-centroids (NaN: too few lines)
};

// One "space": a model or the fusion ("fused").
struct VoiceEvalSpace {
    QString id;
    int linesTotal = 0;
    int linesEligible = 0;       // at least minMs long
    int linesCovered = 0;        // has a vector
    QVector<int> coreLines;      // per speaker the number of core lines (0 = no core)
    QVector<QVector<double>> coreCosine;   // speaker x speaker; NaN if either has no core
    QVector<VoiceEvalSplit> splits;        // per speaker
    int closerChecked = 0;       // this many lines were examined (with a vector, their speaker has a core)
    int closerToOther = 0;       // of these this many are closer to another core (>= kVoiceEvalMargin)
};

struct VoiceEvalReport {
    QStringList models;
    qint64 minMs = 0;
    QVector<VoiceEvalSpeaker> speakers;
    QVector<VoiceEvalSpace> spaces;   // per model (in the order of models), "fused" last
};

// The measurement. The fusion space is included only if there is more than one model.
VoiceEvalReport evaluateVoices(const QVector<VoiceEvalLine>& lines,
                               const QVector<VoiceEvalSpeaker>& speakers,
                               const QStringList& modelIds, qint64 minMs);

QJsonObject voiceEvalToJson(const VoiceEvalReport& report);

} // namespace tanara
