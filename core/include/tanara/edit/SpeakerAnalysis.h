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

#include <algorithm>

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
    bool   noisy = false;                       // „egymásra beszéltek": nem hangminta (lásd lent)
    bool hasEmbedding() const { return embedding && !embedding->isEmpty(); }
};

// Egy sor illeszkedése: a saját beszélőjéhez (önmaga nélkül számolt centroid) és a
// legjobb másikhoz. Ha nem számolható (nincs embedding / kevés sor), az érték NaN.
struct LineFit {
    double own = qQNaN();
    double other = qQNaN();
    int    otherSpeaker = -1;
};

// A centroidok a ZAJOS (egymásra beszélős) sorokat kihagyják, ha a beszélőnek enélkül is van
// legalább kMinSpeakerLines tiszta (embeddelt) sora; ilyenkor a zajos sor saját illeszkedése a
// (nélküle számolt) teljes centroidhoz mért érték.
QVector<LineFit> computeFits(const QVector<AnalysisLine>& lines, int speakerCount);

// Soronként: bizonytalan-e a hozzárendelés (lásd a fenti küszöböket).
QVector<bool> computeUncertain(const QVector<AnalysisLine>& lines, int speakerCount);

// ---- tárolt hanglenyomatok a referenciában (csak az explicit újraellenőrzéseknél) ----
// A beszélőhöz rendelt személy tárolt lenyomatai (VoiceprintStore) is beszállnak az
// újraellenőrzés (computeUncertainRechecked) és a páros átnézés (computePairRecheck)
// referenciájába — a súlyszabály (súly = ms-egyenérték, mint a soroknál):
//   * a megerősített / javított sor a hosszát nyomja (legfeljebb kMaxEmbedMs), mint eddig;
//   * az EBBEN a megbeszélésben készült lenyomat (localVectors) ennek a felvételnek az
//     akusztikáját hordozza (jellemzően épp azért készült, mert itt tévedett a felismerés),
//     ezért helyi bizonyíték: kMaxEmbedMs súllyal számít, a helyi súlyhoz adódik, nem vágjuk,
//     és egymagában is helyi magot alkot;
//   * a MÁS megbeszélésből való (korábbi) lenyomat (vectors) kPriorPrintMs súlyú, de az
//     összes korábbi lenyomat együtt legfeljebb a helyi súly kPriorMaxShare-szerese — így a
//     megbeszélés saját megerősített hangja mindig dominál;
//   * ha a beszélőnek NINCS helyi magja (kevés zárolt sor és nincs itteni lenyomat), a korábbi
//     lenyomatok egymagukban adják a referenciát; ha lenyomat sincs, marad a régi tartalék
//     (a beszélő összes tiszta sora).
// Kettős számolás ellen: ha egy itteni lenyomat mintasora maga is zárolt referencia-sor, a
// sor kimarad (replacedLines), a lenyomat számít (az több sort is összefoghat).
// A bizonytalanság automatikus ítéletét (computeUncertain) és a „hasonló sorok" javaslatot
// (suggestSimilar) ez nem érinti.
inline constexpr qint64 kPriorPrintMs = 4000;       // egy korábbi lenyomat súlya
inline constexpr double kPriorMaxShare = 1.0 / 3.0; // a korábbi lenyomatok összsúlya ≤ helyi súly × ennyi
inline constexpr qint64 kLocalPrintMs = kMaxEmbedMs; // egy itteni lenyomat súlya

struct SpeakerPrior {
    QVector<QVector<float>> vectors;        // korábbi (más megbeszélésből való) lenyomatok, L2-normalizált
    QVector<QVector<float>> localVectors;   // ebben a megbeszélésben készült lenyomatok
    QVector<int> replacedLines;             // sor-indexek, amelyek hangját egy itteni lenyomat már hordozza
    bool isEmpty() const { return vectors.isEmpty() && localVectors.isEmpty(); }
};

// Miből épült egy beszélő referenciája.
enum class ReferenceKind {
    None,       // nem épült referencia
    Local,      // helyi mag: zárolt sorok és/vagy itteni lenyomatok (+ vágott korábbi lenyomatok)
    Prior,      // nincs helyi mag: csak a korábbi lenyomatok
    Fallback,   // se helyi mag, se lenyomat: a beszélő összes (tiszta) sora
};

struct ReferenceInfo {
    ReferenceKind kind = ReferenceKind::None;
    int  lines = 0;             // a referenciában lévő sorok száma
    int  localPrints = 0;       // itteni lenyomatok
    int  priorPrints = 0;       // korábbi lenyomatok
    bool priorCapped = false;   // a korábbi lenyomatok súlyát a kPriorMaxShare-plafon vágta
    double priorShare = 0.0;    // a korábbi lenyomatok súlyaránya a referenciában [0..1]
    bool refLocal() const { return kind == ReferenceKind::Local; }
    bool refPrior() const { return kind == ReferenceKind::Prior; }
    bool fallback() const { return kind == ReferenceKind::Fallback; }
};

// ---- újraellenőrzés a megerősített sorok alapján ---------------------------
// A rendes bizonytalanság-ítéletben egy beszélő centroidját a hozzá TÉVESEN sorolt sorok is
// húzzák, így azok közül a hasonló hangúak rejtve maradnak. Az újraellenőrzés „megbízható"
// centroidot épít: beszélőnként CSAK a zárolt (megerősített / javított) soraiból, ha legalább
// kMinSpeakerLines ilyen (embeddelt) sora van — a tiszták közül, ha azokból is van elég, és
// csak különben a zajosakkal együtt. Akinek nincs elég zárolt sora, annál marad a rendes
// centroid (minden sora, a tiszta-sor szabállyal). Ezután minden NEM zárolt, NEM zajos,
// embeddelt sort ezekhez mérünk ugyanazokkal a küszöbökkel (kUncertainMargin /
// kUncertainMarginShort / kUncertainMinFit). A megbízható centroidban nincs benne a vizsgált
// sor, ezért ott nem kell leave-one-out.
struct RecheckVerdict {
    bool   uncertain = false;
    // A hangra határozottan jobban illő MÁSIK beszélő (javaslat); -1 = nincs ilyen (pl. a sor a
    // sajátjához sem illik, de máshoz sem — új, el nem különített hang).
    int    otherSpeaker = -1;
    double own = qQNaN();
    double other = qQNaN();
};

struct RecheckAnalysis {
    QVector<RecheckVerdict> lines;  // soronként (a bemenet sorrendjében)
    QVector<bool> trustedCore;      // beszélőnként: a centroid csak a zárolt soraiból épült
    QVector<int>  coreLines;        // beszélőnként: hány zárolt sor alkotja a magot (0 = nincs mag)
    QVector<ReferenceInfo> refs;    // beszélőnként: miből épült a referencia (lásd SpeakerPrior)
    int coreSpeakers() const { return int(std::count(trustedCore.cbegin(), trustedCore.cend(), true)); }
    int coreLineTotal() const { int n = 0; for (int c : coreLines) n += c; return n; }
    int flagged() const {
        return int(std::count_if(lines.cbegin(), lines.cend(),
                                 [](const RecheckVerdict& v) { return v.uncertain; }));
    }
};

// priors: beszélő-indexenként a tárolt lenyomatok (lásd SpeakerPrior); üres = csak a sorok.
// A helyi mag (trustedCore) itteni lenyomatból is állhat; akinek nincs helyi magja, de van
// korábbi lenyomata, annál a lenyomatok adják a referenciát (refs[s].kind == Prior).
RecheckAnalysis computeUncertainRechecked(const QVector<AnalysisLine>& lines, int speakerCount,
                                          const QVector<SpeakerPrior>& priors = {});

// Van-e legalább egy beszélő, akinek a zárolt soraiból (vagy itteni lenyomatából) megbízható
// centroid építhető.
bool hasTrustedCore(const QVector<AnalysisLine>& lines, int speakerCount,
                    const QVector<SpeakerPrior>& priors = {});

// ---- „egymásra beszéltek" ---------------------------------------------------
// Egy sor zajos (nem jó hangminta), ha az embedding-ablakában (a sor közepe, legfeljebb
// kMaxEmbedMs) más beszélő sora ennyi ideig szól egyszerre…
inline constexpr qint64 kNoisyOverlapMs = 1000;
// …vagy az ablak legalább ekkora hányadában.
inline constexpr double kNoisyOverlapRatio = 0.30;

struct TimedLine {
    qint64 startMs = 0;
    qint64 endMs = 0;
    int    speaker = -1;            // beszélő-azonosító (csak az egyenlőség számít)
};

// Soronként: zajos-e az átfedés miatt. A sorok időrendben várhatók (kezdőidő szerint).
QVector<bool> computeOverlapNoisy(const QVector<TimedLine>& lines);

// A sourceSpeaker-nél maradt (nem zárolt, embeddelt) sorok indexei, amelyek hangra a
// targetSpeaker-hez állnak közelebb. A cél jelenlegi sorai a rögzített „mag" (köztük a
// kézzel átrakottak); a forrás sorain magozott 2-közép fut, hogy a KEVERT forrás-centroid
// ne húzza el az eredményt (akkor is működik, ha a kiemelt hang a forrás többsége).
QVector<int> suggestSimilar(const QVector<AnalysisLine>& lines, int sourceSpeaker,
                            int targetSpeaker);

// Ugyanez, a „miért nincs javaslat" okával: blockedBySimilarity = a forrásból leváló hang
// centroidja kSuggestMaxCentroidSimilarity fölött hasonlít a maradékhoz („nem két ember" őr) — két
// HASONLÓ hangú embernél éppen ez hallgat el. centroidSimilarity: a két klaszter cosine-ja
// (NaN, ha a 2-közép nem futott le).
struct SuggestOutcome {
    QVector<int> lines;
    bool   blockedBySimilarity = false;
    double centroidSimilarity = qQNaN();
};
SuggestOutcome suggestSimilarDetailed(const QVector<AnalysisLine>& lines, int sourceSpeaker,
                                      int targetSpeaker);

// ---- páronkénti átnézés („Átnézés A és B között") ---------------------------
// A felhasználó kimondta, hogy A és B két ember — akkor is, ha a hangjuk hasonló. Ezért itt
// NINCS centroid-hasonlósági őr (csak jelentjük az értéket), és a küszöb enyhébb: a sor akkor
// kétes, ha a MÁSIK beszélő referenciája ennyivel jobban illik rá (cosine-különbség)…
inline constexpr double kPairMargin = 0.05;
// …rövid (kMinEmbedMs..kReliableMs) sornál ennyivel.
inline constexpr double kPairMarginShort = 0.10;
// Efölötti referencia-hasonlóságnál a UI figyelmeztet: a két hang nagyon hasonló, az
// eredmény bizonytalan (a mért értékek: különböző beszélők 0.15–0.35).
inline constexpr double kPairSimilarWarn = kSuggestMaxCentroidSimilarity;

struct PairVerdict {
    bool   flagged = false;
    int    hintedSpeaker = -1;      // flagged esetén a jobban illő másik (speakerA / speakerB)
    double toA = qQNaN();           // cosine az A-referenciához (önmaga nélkül, ha benne van)
    double toB = qQNaN();
};

struct PairRecheckAnalysis {
    QVector<PairVerdict> lines;     // soronként (a bemenet sorrendjében)
    int  refLinesA = 0;             // ennyi sorból épült A referenciája
    int  refLinesB = 0;
    // A referencia NEM a zárolt sorokból (és nem lenyomatokból) épült, hanem a beszélő összes
    // tiszta (nem zajos, embeddelt) sorából — ez szennyezett lehet a tévesen hozzá sorolt sorokkal.
    bool fallbackA = false;
    bool fallbackB = false;
    ReferenceInfo refA;             // miből épült A / B referenciája (lásd SpeakerPrior)
    ReferenceInfo refB;
    double centroidSimilarity = qQNaN();    // cos(A-referencia, B-referencia)
    bool valid = false;             // mindkét referenciához megvolt a kMinSpeakerLines sor
    int flagged() const {
        return int(std::count_if(lines.cbegin(), lines.cend(),
                                 [](const PairVerdict& v) { return v.flagged; }));
    }
};

// Referencia beszélőnként: a zárolt, nem zajos, embeddelt sorai, ha legalább kMinSpeakerLines
// ilyen van; különben (fallback) az összes nem zajos, embeddelt sora. Ezután minden A-n vagy
// B-n lévő, NEM zárolt, NEM zajos, legalább kMinEmbedMs hosszú, embeddelt sort a két
// referenciához mérünk (ha a sor maga is benne van egy referenciában, ahhoz önmaga nélkül), és
// kétes, ha a másik referencia legalább kPairMargin (rövidnél kPairMarginShort) értékkel jobban
// illik rá. Más beszélők sorait nem nézi. priors: mint computeUncertainRechecked-nél — itteni
// lenyomat helyi magnak számít; helyi mag híján a korábbi lenyomatok adják a referenciát, és
// csak ha az sincs, jön a fallback. Lenyomatos referencia akkor is érvényes, ha kevés a sor.
PairRecheckAnalysis computePairRecheck(const QVector<AnalysisLine>& lines, int speakerA,
                                       int speakerB, const QVector<SpeakerPrior>& priors = {});

} // namespace speakeredit
} // namespace tanara
