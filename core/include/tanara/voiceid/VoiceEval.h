#pragma once
//
// VoiceEval — hangmodellek összevetése egy megbeszélésen (a `tanara-cli voice-eval` mérése).
// Tiszta függvény a soronkénti, modellenkénti vektorokon: modellenként ÉS a fúziós vektorral
//  (1) lefedettség: hány sorhoz van vektor;
//  (2) beszélőnként mag a megerősített / javított (zárolt), nem zajos soraiból, a magok páronkénti
//      cosine-mátrixa;
//  (3) beszélőnként 2-közép szétválás: a két al-centroid cosine-ja és a sorok megoszlása;
//  (4) hány sor áll közelebb egy MÁSIK beszélő magjához legalább kVoiceEvalMargin-nal.
// UI- és I/O-független (a beolvasás a CLI dolga), így unit-tesztelhető.
//
#include "tanara/voiceid/EmbeddingSet.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

inline constexpr double kVoiceEvalMargin = 0.05;   // (4) a „közelebb egy másik maghoz” küszöbe
inline constexpr int kVoiceEvalMinCoreLines = 3;    // ennyi zárolt sor kell egy maghoz
inline constexpr int kVoiceEvalMinSplitLines = 4;   // ennyi sor kell a 2-középhez

// Egy sor a mérés bemenetén.
struct VoiceEvalLine {
    QString id;
    qint64  durationMs = 0;
    int     speaker = -1;        // index a speakers listában
    bool    locked = false;      // megerősített / kézzel javított
    bool    noisy = false;       // egymásra beszéltek: nem kerül magba
    EmbeddingSet vectors;        // modellenként (L2-normalizált); üres = nincs
};

struct VoiceEvalSpeaker {
    QString key;                 // beszélő-kulcs (nyers címke / participant:N)
    QString name;                // megjelenített név
};

struct VoiceEvalSplit {
    int    lines = 0;            // a beszélő vektoros, nem zajos sorai
    int    sizeA = 0;            // a két klaszter mérete (sizeA >= sizeB)
    int    sizeB = 0;
    double centroidCosine = qQNaN();   // a két al-centroid cosine-ja (NaN: kevés sor)
};

// Egy „tér”: egy modell vagy a fúzió ("fused").
struct VoiceEvalSpace {
    QString id;
    int linesTotal = 0;
    int linesEligible = 0;       // legalább minMs hosszú
    int linesCovered = 0;        // van vektor
    QVector<int> coreLines;      // beszélőnként a mag sorainak száma (0 = nincs mag)
    QVector<QVector<double>> coreCosine;   // beszélő × beszélő; NaN, ha valamelyiknek nincs magja
    QVector<VoiceEvalSplit> splits;        // beszélőnként
    int closerChecked = 0;       // ennyi sort vizsgáltunk (vektoros, a beszélőjének van magja)
    int closerToOther = 0;       // ebből ennyi áll közelebb egy másik maghoz (≥ kVoiceEvalMargin)
};

struct VoiceEvalReport {
    QStringList models;
    qint64 minMs = 0;
    QVector<VoiceEvalSpeaker> speakers;
    QVector<VoiceEvalSpace> spaces;   // modellenként (a models sorrendjében), utolsóként "fused"
};

// A mérés. A fúziós tér csak akkor szerepel, ha egynél több modell van.
VoiceEvalReport evaluateVoices(const QVector<VoiceEvalLine>& lines,
                               const QVector<VoiceEvalSpeaker>& speakers,
                               const QStringList& modelIds, qint64 minMs);

QJsonObject voiceEvalToJson(const VoiceEvalReport& report);

} // namespace tanara
