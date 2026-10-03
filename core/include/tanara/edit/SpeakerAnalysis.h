#pragma once
//
// A beszélő-szerkesztő hang-alapú elemzése: tiszta függvények a megszólalás-embeddingeken
// (bizonytalan sorok + „hasonló sorok" javaslat) és a küszöbértékek. UI- és I/O-független,
// így unit-tesztelhető és a validációs harness is ezt hívja.
//
// A küszöbök a minta-meetingeken (CAM++ 16 kHz, mixdown) lettek belőve — lásd a
// tests/unit/test_speaker_sandbox.cpp mérését (TANARA_SANDBOX környezeti változóval fut).
//
#include <QVector>
#include <QtGlobal>

namespace tanara {
namespace speakeredit {

// ---- megszólalás-embedding -------------------------------------------------
// Ennél rövidebb sorból nem számolunk embeddinget („Aha", „Igen"): megbízhatatlan, ezért
// az ilyen sor SOSEM bizonytalan és javaslatba sem kerül — kézi munka marad.
inline constexpr qint64 kMinEmbedMs = 1500;
// Hosszú monológból elég a közepéről ennyi (az idő így a sorok számával, nem a hosszal nő).
inline constexpr qint64 kMaxEmbedMs = 10000;
// Ettől a hossztól tekintjük megbízhatónak egy sor embeddingjét.
inline constexpr qint64 kReliableMs = 3000;

// ---- bizonytalanság --------------------------------------------------------
// Egy beszélő hang-centroidjához legalább ennyi (embeddelt) sor kell; alatta nem ítélünk.
inline constexpr int kMinSpeakerLines = 3;
// Bizonytalan, ha egy MÁSIK beszélő centroidja ennyivel jobban illik (cosine-különbség)…
inline constexpr double kUncertainMargin = 0.10;
// …rövid (kMinEmbedMs..kReliableMs) sornál szigorúbb különbség kell (zajosabb embedding).
inline constexpr double kUncertainMarginShort = 0.20;
// …vagy ha a megbízható hosszú sor a SAJÁT beszélőjéhez is ennél gyengébben illik.
inline constexpr double kUncertainMinFit = 0.20;

// ---- „hasonló sorok" javaslat ----------------------------------------------
// A forrásnál maradt sor akkor kerül a javaslatba, ha a célhoz ennyivel közelebb áll…
inline constexpr double kSuggestMargin = 0.10;
inline constexpr double kSuggestMarginShort = 0.18;
// …és a célhoz abszolút értékben is legalább ennyire hasonlít.
inline constexpr double kSuggestMinSimilarity = 0.35;
// Ha a szétvált két hang centroidja ennél hasonlóbb, az nem két ember (a mért értékek:
// különböző beszélők 0.15–0.35, ugyanazon beszélő al-klaszterei ~0.85) → nincs javaslat.
inline constexpr double kSuggestMaxCentroidSimilarity = 0.60;

// ---- kézi hanglenyomat -----------------------------------------------------
inline constexpr qint64 kPrintMinLineMs = 3000;     // csak az ennél hosszabb sorok
inline constexpr qint64 kPrintMinTotalMs = 15000;   // legalább ennyi anyag kell összesen
inline constexpr qint64 kPrintTargetMs = 20000;     // eddig gyűjtünk (a leghosszabbaktól)

// Egy megszólalás az elemzés bemenetén.
struct AnalysisLine {
    const QVector<float>* embedding = nullptr;  // L2-normalizált; nullptr/üres = nincs
    int    speaker = -1;                        // a jelenlegi beszélő indexe [0, speakerCount)
    qint64 durationMs = 0;
    bool   locked = false;                      // megerősített / kézzel javított (sosem bizonytalan)
    bool hasEmbedding() const { return embedding && !embedding->isEmpty(); }
};

// Egy sor illeszkedése: a saját beszélőjéhez (önmaga nélkül számolt centroid) és a
// legjobb másikhoz. Ha nem számolható (nincs embedding / kevés sor), az érték NaN.
struct LineFit {
    double own = qQNaN();
    double other = qQNaN();
    int    otherSpeaker = -1;
};

QVector<LineFit> computeFits(const QVector<AnalysisLine>& lines, int speakerCount);

// Soronként: bizonytalan-e a hozzárendelés (lásd a fenti küszöböket).
QVector<bool> computeUncertain(const QVector<AnalysisLine>& lines, int speakerCount);

// A sourceSpeaker-nél maradt (nem zárolt, embeddelt) sorok indexei, amelyek hangra a
// targetSpeaker-hez állnak közelebb. A cél jelenlegi sorai a rögzített „mag" (köztük a
// kézzel átrakottak); a forrás sorain magozott 2-közép fut, hogy a KEVERT forrás-centroid
// ne húzza el az eredményt (akkor is működik, ha a kiemelt hang a forrás többsége).
QVector<int> suggestSimilar(const QVector<AnalysisLine>& lines, int sourceSpeaker,
                            int targetSpeaker);

} // namespace speakeredit
} // namespace tanara
