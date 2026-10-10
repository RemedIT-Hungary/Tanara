#pragma once
//
// Átnézendő csoportok (v3 design, 15. döntés / E2): a kétes sorok csoportokba rendezve, csoportonként
// egy döntés (és egy visszavonási lépés). Tiszta függvények egy pillanatképen (ReviewInput) —
// a SpeakerEditor háttérszálon hívja, a CLI (`tanara-cli review`) közvetlenül.
//
// Csoportfajták (prioritás szerint; egy sor legfeljebb egy csoportban):
//   SideConflict       — a sor oldala ellentmond a beszélője oldalának; javaslat: a legjobb azonos
//                        oldali jelölt. Beszélő- és javaslat-páronként egy csoport.
//   SimilarToNewPerson — új személy létrehozása után a hozzá hasonló (≥ kNewPersonSimilarity)
//                        sorok ugyanazon az oldalon.
//   CoreMismatch       — a megerősített magokhoz mérve ≥ kCoreMargin-nal egy másik maghoz közelebb;
//                        (jelenlegi, javasolt) páronként.
//   ContaminatedCore   — egy beszélő megerősített sorai 2-közép szerint két hangra esnek (klaszter-
//                        hasonlóság < kContamMaxSimilarity, mindkét fél ≥ kContamMinShare) →
//                        javaslat: szétválasztás (a kisebb fél a legjobb jelölthöz, vagy új névtelen
//                        résztvevőhöz). Ezek zárolt sorok, így más csoporttal nem fednek át.
//   ShortLines         — < kMinEmbedMs, nincs beágyazás; csak tájékoztató (nincs javaslat).
//
#include "tanara/Types.h"
#include "tanara/edit/CandidateRanker.h"
#include "tanara/edit/Evidence.h"
#include "tanara/edit/SideAnalysis.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/edit/TrackActivity.h"

#include <QHash>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

enum class ReviewKind { SideConflict, CoreMismatch, ShortLines, ContaminatedCore, SimilarToNewPerson };

struct ReviewGroup {
    QString id;                 // stabil a (fajta, jelenlegi, javasolt) hármasra — applyReviewGroup kulcsa
    ReviewKind kind = ReviewKind::ShortLines;
    QString title;              // magyar, rövid
    QString subtitle;
    QStringList utteranceIds;   // időrendben
    QString currentSpeakerKey;
    QString proposedSpeakerKey; // üres: proposedPersonName, vagy új névtelen résztvevő (ShortLines: nincs javaslat)
    QString proposedPersonName; // a javasolt személy (meetingen kívüli jelöltnél a kulcs üres)
    QVector<Evidence> evidence;
    int confirmedBasis = 0;     // ennyi megerősített sor a döntés alapja
};

QString reviewKindName(ReviewKind k);   // "sideConflict" | "coreMismatch" | … (CLI / JSON)

// A csoportképzés bemenete: a meeting pillanatképe (érték-másolat, háttérszálra vihető).
struct ReviewInput {
    Meeting meeting;                                    // speakerMap, tracks, tagIds
    QVector<TranscriptLine> lines;
    SpeakerOverlay overlay;
    QHash<QString, QVector<float>> embeddings;          // megszólalás-id → fúziós, L2-normalizált vektor
    QString userName;
    QHash<QString, QVector<QVector<float>>> personPrints;   // személynév → fúziós lenyomatok (L2)
    PersonTagsFn personTags;                            // üres = nincs személy-címke (D szelet)
    SideHints sideHints;                                // tanult oldalak (a kézi beosztás az overlay-ből jön)
    QString newPersonKey;                               // az imént létrehozott személy (SimilarToNewPerson)
};

struct ReviewResult {
    SideReport sides;
    ranking::RankContext context;
    QVector<ReviewGroup> groups;
    // Sor-id → a beszélő-kulcs, akivel ellentmondásban áll (nem zárolt sorok; „bizonytalan · sáv").
    QHash<QString, QString> sideConflicts;
};

namespace review {

inline constexpr double kCoreMargin = 0.05;
inline constexpr int    kContamMinLines = 6;
inline constexpr double kContamMaxSimilarity = 0.70;
inline constexpr double kContamMinShare = 0.20;
inline constexpr double kNewPersonSimilarity = 0.60;
inline constexpr int    kKMeansIterations = 12;

// Egy beszélő megerősített magjának szétválási vizsgálata.
struct CoreSplit {
    bool contaminated = false;
    double similarity = qQNaN();    // a két klaszter-centroid cosine-ja
    QStringList minority;           // a kisebb fél sorai (időrendben)
    QStringList majority;
    QVector<float> minorityCentroid;
};
CoreSplit splitConfirmedCore(const ReviewInput& in, const SideReport& sides, const QString& speakerKey);

// Gömbi 2-közép L2 vektorokon (determinisztikus kezdés: a legkevésbé hasonló pár). Vissza:
// soronként 0 / 1; üres, ha kevesebb mint 2 vektor.
QVector<int> twoMeans(const QVector<QVector<float>>& vectors, int iterations = kKMeansIterations);

} // namespace review

// A rangsor kontextusa: a meeting beszélői (megerősített mag, sor-összeg, oldal, lenyomat, címke)
// és a meetingen kívüli, lenyomattal bíró személyek.
ranking::RankContext buildRankContext(const ReviewInput& in, const SideReport& sides);

// A csoportok a kész oldal-elemzésből és kontextusból.
QVector<ReviewGroup> buildReviewGroups(const ReviewInput& in, const SideReport& sides,
                                       const ranking::RankContext& ctx);

// Az egész: oldal-elemzés (az aktivitásból) + kontextus + csoportok.
ReviewResult analyzeReview(const ReviewInput& in, const MeetingActivity& activity);

} // namespace tanara

Q_DECLARE_METATYPE(tanara::ReviewGroup)
