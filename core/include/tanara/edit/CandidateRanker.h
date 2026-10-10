#pragma once
//
// CandidateRanker — jelölt-rangsor bizonyítékokkal (v3 design, E1 / E3): egy sorra vagy egy egész
// beszélőre kik jöhetnek szóba, és miért. Tiszta függvények egy előre felépített kontextuson
// (RankContext; a ReviewGroups.h buildRankContext() építi a meeting adataiból) — I/O nincs.
//
// Pontszám = hang (cosine a jelölt referenciájához, 0..1) · kVoiceWeight
//          + sáv (azonos oldal: +kSideSupport; ellenkező oldal: −kSidePenalty, otherSide)
//          + címke (közös címkénként +kTagSupport, legfeljebb kTagMaxShared; nincs közös: −kTagMismatch)
//          + sorok itt (kLineWeight · min(1, sorok / kLineSaturation)).
// A tiltott (másik) oldal LEJJEBB sorol, nem tüntet el. A hasonlóság-figyelmeztetés (két mag
// cosine-ja ≥ kSimilarWarn) csak tájékoztat, a pontszámot nem érinti.
//
#include "tanara/edit/Evidence.h"
#include "tanara/edit/SideAnalysis.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace tanara {

// A személy címkéi (a D szelet adja; addig üres seam). A visszaadott értékek a meeting
// címkéivel (Meeting::tagIds) összevethető azonosítók.
using PersonTagsFn = std::function<QStringList(const QString& personName)>;

namespace ranking {

inline constexpr double kVoiceWeight = 1.0;
inline constexpr double kSideSupport = 0.10;
inline constexpr double kSidePenalty = 0.40;
inline constexpr double kTagSupport = 0.05;
inline constexpr int    kTagMaxShared = 2;
inline constexpr double kTagMismatch = 0.03;
inline constexpr double kLineWeight = 0.05;
inline constexpr int    kLineSaturation = 20;
// Két beszélő megerősített magja ennél hasonlóbb → figyelmeztetés („Kettejük átnézése").
inline constexpr double kSimilarWarn = 0.90;
// A hang-bizonyíték polaritása: efölött támogat, ez alatt ellentmond (köztük semleges).
inline constexpr double kVoiceGood = 0.50;
inline constexpr double kVoiceBad = 0.30;
// Mag híján a beszélő sorainak centroidja legalább ennyi (a próbán kívüli) sorból számít referenciának.
inline constexpr int kMinReferenceLines = 2;

// Egy jelölt (a meeting beszélője, vagy a meetingen kívüli ismert személy) a rangsoroláshoz.
struct SpeakerProfile {
    QString key;                // üres = meetingen kívüli személy
    QString personName;
    QString displayName;
    Side    side = Side::Unknown;       // a beszélő oldala (SideAnalysis PersonSide)
    QString sideBasis;                  // PersonSide::basis
    Side    lineSide = Side::Unknown;   // a sorai többsége szerinti oldal (a beszélő mint „próba")
    int     lines = 0;                  // sorai itt
    int     confirmedLines = 0;         // a megerősített mag sorai (0 = nincs mag)
    QVector<float>  core;               // a megerősített, tiszta sorok centroidja (L2); üres = nincs mag
    QVector<double> sum;                // minden tiszta, beágyazott sorának összege (leave-one-out-hoz)
    int     sumCount = 0;
    QVector<QVector<float>> prints;     // a személy tárolt lenyomatai (fúziós, L2)
    QStringList tags;
};

struct RankContext {
    QVector<SpeakerProfile> speakers;   // a meeting beszélői, majd a meetingen kívüli személyek
    QStringList meetingTags;
    QHash<QString, int> index;          // beszélő-kulcs → speakers index
    bool sidesActive = false;
    const SpeakerProfile* profile(const QString& key) const;
};

// Egy „próba": egy sor vagy egy beszélő hangja + oldala.
struct Probe {
    QVector<float> embedding;   // L2; üres = nincs hang
    Side    side = Side::Unknown;
    QString currentKey;         // a próba mostani beszélője (Candidate-ként is szerepel)
    bool    inCurrentSum = false;   // a próba hangja benne van a mostani beszélő összegében
};

double cosine(const QVector<float>& a, const QVector<float>& b);   // L2 vektorokra: skalárszorzat
QVector<float> normalized(const QVector<double>& sum);

// A jelölt hang-pontszáma a próbához (NaN = nincs összemérhető referencia). *basis: "core" |
// "lines" | "prints" (a legjobban illő referencia fajtája).
double voiceScore(const SpeakerProfile& p, const Probe& probe, QString* basis = nullptr);

// Minden jelölt (a mostani beszélő is) csökkenő pontszám szerint.
QVector<Candidate> rank(const RankContext& ctx, const Probe& probe);
// Egy sor: a sor hangja + oldala.
QVector<Candidate> rankForLine(const RankContext& ctx, const QVector<float>& embedding, Side lineSide,
                               const QString& currentKey, bool inCurrentSum);
// Egy egész beszélő („Nem ő? Valójában…"): a beszélő centroidja + a sorai szerinti oldal. A
// beszélő maga nem szerepel.
QVector<Candidate> rankForSpeaker(const RankContext& ctx, const QString& speakerKey);
// „Miért ő?": a beszélő mellett és ellen szóló bizonyítékok (hang a lenyomataihoz, sáv, címke,
// sorok, hasonló hangú másik beszélő).
QVector<Evidence> selfEvidence(const RankContext& ctx, const QString& speakerKey);
// „Miért nem X?": a jelölt ellentmondó bizonyítékai (fix-linkkel).
QVector<Evidence> whyNot(const Candidate& c);
// A hasonló hangú beszélők (mag-cosine ≥ kSimilarWarn) figyelmeztetései (fixTarget "pair:<key>").
QVector<Evidence> similarityWarnings(const RankContext& ctx, const QString& speakerKey);

// Magyar felirat egy oldalhoz („mikrofon", „hívás hangja", „vegyes", „ismeretlen").
QString sideLabel(Side s);
QString evidenceKindName(EvidenceKind k);   // "voice" | "side" | … (CLI / JSON)
QString polarityName(Polarity p);           // "support" | "contradict" | "neutral"

} // namespace ranking
} // namespace tanara
