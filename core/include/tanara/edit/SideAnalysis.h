#pragma once
//
// Sáv-oldal ellenőrzés: melyik OLDALON (helyi mikrofon / távoli loopback) szólt egy átirat-sor,
// és ellentmond-e ez a sor beszélőjének. Tiszta függvények az adaton (a sáv-aktivitás:
// TrackActivity.h; az átirat és az overlay: SpeakerOverlay.h) — I/O nincs, unit-tesztelhető.
//
// A felvétel sávonként külön fájl: a mikrofon-sáv(ok)on a helyi, a loopback-sáv(ok)on a távoli
// résztvevők hangja szól. Ha egy sor alatt csak a mikrofonon van jel, a sort helyi beszélő
// mondta; ha csak a loopbackon, távoli. A Soniox-diarizáció és a hangmodellek ezt nem látják,
// a sáv-energia viszont triviálisan szétválasztja.
//
// Az oldal (v3) BESZÉDSZINT-normalizált: sávonként a beszédszint az aktív (kActiveDb feletti)
// keretek 90. percentilise (= 0 dB), a sor szintje sávfajtánként a legerősebb sáv ehhez mért
// értéke. A felhasználó hangja a loopbackon visszhangként visszajön (~8 dB-lel a valódi távoli
// beszéd alatt) — a puszta teljesítmény-arány ezt távolinak látta. Az új döntés (classifyLevels):
//   * mic beszédszinten (≥ kSpeechDb)                       → Local vagy Mixed, SOHA Remote;
//   * mindkettő beszédszinten: a loopback ≥ kEchoDb-lel a saját szintje alatt → Local (visszhang),
//     különben Mixed (egymásra beszélés);
//   * loopback beszédszinten ÉS a mic alacsony (< kLowDb) → Remote;
//   * egyik sem (pl. a mic a kettő között: hangszóró-áthallás) → Unknown.
// Unknown akkor is, ha valamelyik sávfajta nem fedi le a sort. Egysávos meeting (csak mic vagy
// csak loopback, pl. import) → minden Unknown, nincs konfliktus (a funkció csendben inaktív).
//
// A régi (teljesítmény-arány) besorolás diagnosztikának megmarad (LineSide::legacySide):
// localShare = P_mic / (P_mic + P_loop): a zajpadló FELETTI lineáris teljesítmény
// (10^(dB/10) − 1, a windowEnergyDb-ből) sávfajtánként összegezve. Oldal a küszöbökkel:
// >= hiLocal → Local, <= loRemote → Remote, köztük Mixed.
//
// Személy oldala (elsőbbségi sorrendben): kézi sáv-beosztás (SpeakerOverlay::speakerTracks) >
// a megerősített sorokból következtetett > a people.json-ban tanult alapérték > saját név = Local
// (ill. sávhoz kötött beszélő). A következtetett az alapértéket csak egyértelmű többséggel írja
// felül (kOverrideMinLines / kOverrideMajority).
//
#include "tanara/Types.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/edit/TrackActivity.h"

#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

enum class Side { Local, Remote, Mixed, Unknown };

QString sideName(Side s);   // "local" | "remote" | "mixed" | "unknown"
Side sideFromName(const QString& name);   // ismeretlen → Unknown

namespace sides {

// Egy oldal akkor „szól”, ha az ablak átlagos energiája ennyivel a zajpadló felett van (dB).
inline constexpr float kActiveDb = 3.0f;
// Alap küszöbök (egycsúcsú eloszlásnál).
inline constexpr float kDefaultLoRemote = 0.3f;
inline constexpr float kDefaultHiLocal = 0.7f;
// Adaptív küszöb: a localShare-ek 1D 2-közepe akkor számít kétcsúcsúnak, ha legalább
// kAdaptiveMinLines sor van, mindkét csoportban legalább max(kAdaptiveMinCluster, 5%) sor,
// a két közép között legalább kAdaptiveMinGap a távolság, és a 0.5 közéjük esik.
inline constexpr int kAdaptiveMinLines = 6;
inline constexpr int kAdaptiveMinCluster = 3;
inline constexpr double kAdaptiveMinClusterShare = 0.05;
inline constexpr float kAdaptiveMinGap = 0.35f;
// Személy oldala a megerősített sorokból: legalább ennyi Local+Remote sor és ekkora többség.
inline constexpr int kPersonMinLines = 2;
inline constexpr float kPersonMajority = 0.75f;
// Az alapértelmezést (saját név / sávhoz kötött beszélő) csak egyértelmű ellenbizonyíték írja felül.
inline constexpr int kOverrideMinLines = 3;
inline constexpr float kOverrideMajority = 0.8f;
// Alapértelmezésből (adat nélkül) jött oldal bizonyossága.
inline constexpr float kDefaultConfidence = 0.5f;

// ---- beszédszint-normalizált besorolás (v3) ----
// A sáv beszédszintje: az aktív (>= kActiveDb) keretek ennyiedik percentilise.
inline constexpr double kSpeechPercentile = 0.90;
// „Beszédszinten": a sor szintje legfeljebb ennyivel a sáv beszédszintje alatt (dB).
inline constexpr float kSpeechDb = -10.0f;
// „Alacsony": ennél mélyebben a beszédszint alatt (vagy kActiveDb alatt).
inline constexpr float kLowDb = -20.0f;
// Mindkettő beszédszinten: ha a loopback legalább ennyivel a saját szintje alatt van → visszhang.
inline constexpr float kEchoDb = -5.0f;

struct Thresholds {
    float loRemote = kDefaultLoRemote;
    float hiLocal = kDefaultHiLocal;
    bool adaptive = false;          // a kétcsúcsú eloszlásból számolt
    float centerRemote = qQNaN();   // a 2-közép két középértéke (adaptívnál)
    float centerLocal = qQNaN();
};

// Az adaptív küszöb. Kétcsúcsú eloszlásnál (lásd fent) a Mixed-sáv a két közép közti szakasz
// KÖZÉPSŐ FELE: m = (cR+cL)/2, w = (cL−cR)/4 → loRemote = m−w, hiLocal = m+w. Így mic-áthallásnál
// (a távoli sorok is mutatnak némi mic-energiát, a távoli csúcs pl. 0.35-nél ül) a határ a két
// csúcs közé tolódik. A 2-közép 1D-ben egzakt: a rendezett sor legjobb kettévágása (min. SSE).
// Egyébként az alap 0.3 / 0.7.
Thresholds adaptiveThresholds(const QVector<float>& shares);

Side classifyShare(float localShare, const Thresholds& th);

// localShare a két oldal zajpadló feletti dB-jéből (több sáv → a hívó összegzi a teljesítményt).
float localShareFromDb(float micDb, float loopDb);

// A sáv beszédszintje (padló feletti dB): az aktív keretek kSpeechPercentile percentilise.
// NaN, ha nincs aktív keret.
float speechLevelDb(const TrackActivity& t);

// A v3 döntés a két oldal beszédszinthez mért értékéből (dB, <= 0 jellemzően). -inf = az oldal
// csendes (kActiveDb alatt); NaN = nincs adat (a sávfajta nem fedi le a sort) → Unknown.
Side classifyLevels(float micRelDb, float loopRelDb);

} // namespace sides

struct LineSide {
    QString utteranceId;
    qint64  startMs = 0;
    qint64  endMs = 0;
    QString speakerKey;     // a feloldott beszélő-kulcs
    QString rawLabel;       // a nyers diarizációs címke
    float   localShare = qQNaN();   // NaN = nincs adat
    Side    side = Side::Unknown;   // a v3 (beszédszint-normalizált) besorolás
    Side    legacySide = Side::Unknown;   // a régi, teljesítmény-arány szerinti (diagnosztika)
    float   micDb = qQNaN();        // a mic-sávok együttes energiája a zajpadló felett (NaN = nincs lefedés)
    float   loopDb = qQNaN();
    // A sávfajta legerősebb sávjának szintje a saját beszédszintjéhez mérve (dB); -inf = csend,
    // NaN = nincs lefedés.
    float   micRelDb = qQNaN();
    float   loopRelDb = qQNaN();
    bool    locked = false;         // megerősített / kézzel javított
    bool    noisy = false;          // „egymásra beszéltek” (overlay vagy átfedés-szabály)
};

struct SideCounts {
    int local = 0, remote = 0, mixed = 0, unknown = 0;
    void add(Side s);
    int total() const { return local + remote + mixed + unknown; }
};

struct PersonSide {
    QString speakerKey;
    QString personName;         // a kötött személy (üres = névtelen)
    QString displayName;
    Side    side = Side::Unknown;
    // A megerősített/javított, nem zajos sorok oldalai (ebből az ítélet).
    int localLines = 0, remoteLines = 0, mixedLines = 0;
    SideCounts allLines;        // a beszélő összes sora (tájékoztató)
    float   confidence = 0.0f;  // 0..1: a többség aránya, ill. kDefaultConfidence alapértelmezésnél
    Side    defaultSide = Side::Unknown;   // tanult alapérték / saját név / sávhoz kötött beszélő alapján
    Side    learnedSide = Side::Unknown;   // a people.json-ban tanult alapérték (SideHints)
    QStringList manualTracks;   // a kézi sáv-beosztás (üres = nincs megkötés)
    Side    manualSide = Side::Unknown;    // a kézi beosztás sávjainak oldala
    // "manual" | "lines" | "learned" | "user-name" | "fixed-track" | "override" | "none"
    QString basis;
};

struct SideConflict {
    QString utteranceId;
    qint64  startMs = 0;
    QString speakerKey;
    Side    lineSide = Side::Unknown;
    Side    personSide = Side::Unknown;
    float   localShare = qQNaN();
};

struct SideReport {
    bool active = false;                 // van mic ÉS loopback aktivitás (különben minden Unknown)
    sides::Thresholds thresholds;        // a régi besorolás küszöbei (legacySide)
    QMap<QString, float> speechLevels;   // sáv-id → beszédszint (padló feletti dB; NaN = nincs)
    SideCounts legacyTotals;             // a régi besorolás összesítője
    QVector<LineSide> lines;             // a bemenet sorrendjében
    QVector<PersonSide> persons;         // az első megjelenés sorrendjében
    QVector<SideConflict> conflicts;     // időrendben
    QMap<QString, SideCounts> rawLabels; // NYERS diarizációs címkénként
    SideCounts totals;
    QVector<int> histogram;              // localShare 10 vödörben (a NaN-ok nélkül)
};

// Külső tudás a személyek oldaláról.
struct SideHints {
    // Kézi sáv-beosztás: beszélő-kulcs → sáv-id-k (SpeakerOverlay::speakerTracks). Ha üres,
    // az overlay-é számít.
    QHash<QString, QStringList> speakerTracks;
    // A people.json-ban tanult alapérték: személynév (toCaseFolded) → oldal.
    QHash<QString, Side> learnedSides;
};

// A teljes elemzés. userName: a felhasználó saját neve (SettingsManager); az ehhez kötött beszélő
// és a Track.fixedSpeaker sáv speakerLabel-jével egyező beszélő alapból a sáv oldalán áll (mic →
// Local, loopback → Remote). A tanult alapérték ezt megelőzi; az alapértéket a megerősített sorok
// egyértelmű ellentmondása felülírja (naplózva); a kézi sáv-beosztás mindent felülír.
SideReport analyzeSides(const Meeting& m, const QVector<TranscriptLine>& lines,
                        const SpeakerOverlay& ov, const MeetingActivity& activity,
                        const QString& userName, const SideHints& hints = {});

namespace sides {
// A sáv-ok oldala a meeting sávjai szerint (mic → Local, loopback → Remote; vegyes → Mixed;
// ismeretlen id-k → Unknown).
Side sideOfTracks(const Meeting& m, const QStringList& trackIds);
// Mit tanulhat a people.json egy elemzésből: az elnevezett személyek, akiknek az oldala
// adatból (megerősített sorok) vagy kézi beosztásból jött, és Local / Remote.
// Kulcs: a személy neve (ahogy a speakerMap-ben áll).
QHash<QString, Side> learnedDefaults(const SideReport& r);
} // namespace sides

} // namespace tanara
