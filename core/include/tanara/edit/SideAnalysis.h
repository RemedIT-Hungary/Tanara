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
// localShare = P_mic / (P_mic + P_loop): a zajpadló FELETTI lineáris teljesítmény
// (10^(dB/10) − 1, a windowEnergyDb-ből) sávfajtánként összegezve. Oldal a küszöbökkel:
// >= hiLocal → Local, <= loRemote → Remote, köztük Mixed. Unknown: valamelyik sávfajta nem fedi
// le a sort, vagy egyik oldalon sincs kActiveDb feletti jel. Egysávos meeting (csak mic vagy csak
// loopback, pl. import) → minden Unknown, nincs konfliktus (a funkció csendben inaktív).
//
#include "tanara/Types.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/edit/TrackActivity.h"

#include <QMap>
#include <QString>
#include <QVector>

namespace tanara {

enum class Side { Local, Remote, Mixed, Unknown };

QString sideName(Side s);   // "local" | "remote" | "mixed" | "unknown"

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

} // namespace sides

struct LineSide {
    QString utteranceId;
    qint64  startMs = 0;
    qint64  endMs = 0;
    QString speakerKey;     // a feloldott beszélő-kulcs
    QString rawLabel;       // a nyers diarizációs címke
    float   localShare = qQNaN();   // NaN = nincs adat
    Side    side = Side::Unknown;
    float   micDb = qQNaN();        // a mic-sávok együttes energiája a zajpadló felett (NaN = nincs lefedés)
    float   loopDb = qQNaN();
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
    Side    defaultSide = Side::Unknown;   // saját név / sávhoz kötött beszélő alapján
    QString basis;              // "lines" | "user-name" | "fixed-track" | "override" | "none"
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
    sides::Thresholds thresholds;
    QVector<LineSide> lines;             // a bemenet sorrendjében
    QVector<PersonSide> persons;         // az első megjelenés sorrendjében
    QVector<SideConflict> conflicts;     // időrendben
    QMap<QString, SideCounts> rawLabels; // NYERS diarizációs címkénként
    SideCounts totals;
    QVector<int> histogram;              // localShare 10 vödörben (a NaN-ok nélkül)
};

// A teljes elemzés. userName: a felhasználó saját neve (SettingsManager); az ehhez kötött beszélő
// és a Track.fixedSpeaker sáv speakerLabel-jével egyező beszélő alapból a sáv oldalán áll (mic →
// Local, loopback → Remote). Az alapértelmezést a megerősített sorok egyértelmű ellentmondása
// felülírja (naplózva).
SideReport analyzeSides(const Meeting& m, const QVector<TranscriptLine>& lines,
                        const SpeakerOverlay& ov, const MeetingActivity& activity,
                        const QString& userName);

} // namespace tanara
