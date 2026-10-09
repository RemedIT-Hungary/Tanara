#pragma once
//
// TrackCatalog — a „Sávok” fül (M09) háttere: sávonként barátságos megjelenített név az
// eszköz SZEREPE alapján, felhasználói átnevezés, hiányzó hangfájl észlelése és
// megkeresése, az eldobott sávok végleges törlése egy lépésben. UI-független.
//
// (A sáv visszaállítása / egyenkénti törlése és a lekeverés az AppController-ben él:
//  restoreTrack / deleteTrack / regenerateMixdown. A hullámforma: WaveformService.)
//
#include "tanara/Types.h"

#include <QMap>
#include <QObject>
#include <QVector>

namespace tanara {

class MeetingStore;

// Az eszköz szerepe a felvételben — ebből lesz a barátságos név.
enum class TrackRole {
    OwnMic,        // saját bemenet → „Saját mikrofon”
    CallAudio,     // a hívás eszközének monitorja / loopbackje → „Hívás hangja”
    SystemAudio,   // más monitor / loopback → „Rendszerhang”
    Other,         // vonalbemenet, AUX, ismeretlen → az eszköz (rövidített) neve
};

struct TrackView {
    Track     track;              // a nyers sáv (id, file, kind, active, peakLevel …)
    TrackRole role = TrackRole::Other;
    QString   displayName;        // amit a UI mutat: a felhasználó neve, különben a barátságos név
    QString   friendlyName;       // a szerepből levezetett alapértelmezett név (átnevezés visszavonásához)
    QString   rawDeviceName;      // a nyers eszköznév (a UI kis betűvel alatta mutatja)
    bool      renamed = false;    // a felhasználó átnevezte
    QString   absolutePath;       // a hangfájl teljes útja
    bool      fileMissing = false;// a hangfájl nincs a helyén („hiányzik a fájl” → „Megkeresés…”)
    qint64    fileSize = 0;
    // Egy eszköz több szakasza (felvétel közben ki-be kapcsolt eszköz → több fájl): a szakasz
    // sorszáma 1-től (az eltolás sorrendjében), ill. 0, ha az eszköznek egyetlen fájlja van.
    // A szakaszok egy logikai sávot alkotnak: közös név és szerep, egymás után állnak.
    int       segment = 0;
    int       segmentCount = 1;
    // A sáv hossza ms-ben: a hullámforma-gyorsítótárból (pontos, dekódolt hossz), ha az már
    // elkészült és érvényes; különben -1 (a WaveformService::peaksReady adja meg).
    qint64    durationMs = -1;
};

// ---- hangeszközök felhasználói nevei --------------------------------------------------
// A felhasználó a Beállításokban átnevezheti az eszközeit (AppSettings::deviceNames: nyers
// OS-név → barátságos név). EZ az egyetlen hely, ahol a név feloldódik: a felvevő, a „Sávok”
// fül (tracknames::friendlyNames), a figyelő értesítése és a Beállítások is innen kérdez.
// A táblát a SettingsManager tölti (betöltéskor és minden setSettings-nél), folyamat-szintű;
// szálbiztos.
namespace devicenames {

void setOverrides(const QMap<QString, QString>& rawToFriendly);
QMap<QString, QString> overrides();
// Van-e a felhasználónak saját neve erre az eszközre.
bool hasOverride(const QString& rawDeviceName);
// A megjelenítendő név: a felhasználó neve, különben tracknames::shortDeviceName(raw)
// (ha az üres, maga a nyers név). A monitor-előtagos alak („Monitor of X”) a saját kulcsán
// kap nevet: a kimenet és a bemenet külön eszköz.
QString displayName(const QString& rawDeviceName);
// Ugyanez egy adott táblával (a Beállítások még nem mentett piszkozatához).
QString displayName(const QString& rawDeviceName, const QMap<QString, QString>& overrides);

} // namespace devicenames

namespace tracknames {

// A hívás (kommunikációs) eszközére utal-e a név — pl. a headsetek „Communication” /
// „Chat” / „Hands-Free” profilja, amit a hívás-appok használnak.
bool looksLikeCallDevice(const QString& deviceName);

// A monitor/loopback előtag és a profil-rész nélküli rövid eszköznév
// („Monitor of Kanto YU4 - Optikai Digitális sztereó (IEC958)” → „Kanto YU4”).
QString shortDeviceName(const QString& deviceName);

// Egy eszköz szakaszai. Ugyanannak az eszköznek (azonos, nem üres deviceName) a fájljai
// akkor egy logikai sáv szakaszai, ha MIND különböző eltolással (startOffsetMs) indultak — a
// felvevő egy eszközt egyszerre csak egyszer tart nyitva, így a ki-be kapcsolás szakaszai
// mindig eltérő időben kezdődnek. Azonos eltolás (pl. két egyforma nevű mikrofon egyszerre)
// → külön logikai sávok.
struct SegmentInfo {
    int leader = -1;   // a logikai sáv első (legkorábbi) szakaszának indexe `all`-ban
    int segment = 0;   // 1..count az eltolás sorrendjében; 0, ha a sáv egyetlen fájl
    int count = 1;
};
QVector<SegmentInfo> segments(const QVector<Track>& all);

// Egy sáv szerepe a meeting ÖSSZES sávjának ismeretében:
//  - Mic → OwnMic; Other → Other;
//  - Loopback: ha a neve a hívás eszközére utal → CallAudio. Ha egyik loopback neve sem
//    utal rá: az egyetlen aktív loopback a CallAudio (a felvett hívás azon szól); több aktív
//    közül a leghangosabb (peakLevel). A többi SystemAudio. Egy eszköz szakaszai (segments)
//    együtt számítanak: közös a szerepük (aktív, ha bármelyik aktív; a csúcsuk a legnagyobb).
TrackRole classify(const Track& track, const QVector<Track>& all);

// Barátságos nevek a meeting összes sávjára (a `all` sorrendjében). Azonos szerepű sávoknál
// a rövid eszköznév különböztet („Rendszerhang (Kanto YU4)”), végső esetben sorszám. Egy
// eszköz szakaszai ugyanazt a nevet kapják (a szakaszt a Sávok fül a második sorban jelzi).
// Ha a felhasználó az eszközt átnevezte (devicenames), a sáv azt a nevet kapja a szerep-név
// helyett — amit ő adott, azt látja mindenhol.
QStringList friendlyNames(const QVector<Track>& all);

} // namespace tracknames

class TrackCatalog : public QObject {
    Q_OBJECT
public:
    explicit TrackCatalog(MeetingStore* store, QObject* parent = nullptr);

    // A meeting sávjai megjelenítésre készen: logikai sávonként (az első előfordulás
    // sorrendjében), azon belül a szakaszok eltolás szerint. Szakasz nélkül = a meeting.json
    // sorrendje.
    QVector<TrackView> tracks(const QString& meetingId) const;
    static QVector<TrackView> tracks(const Meeting& meeting);

    // Hány eldobott (nem aktív) sáv van — a „végleges törlés” gomb és a megerősítés számához.
    int droppedTrackCount(const QString& meetingId) const;

public slots:
    // Sáv átnevezése (perzisztens: meeting.json → track.customName). Üres név → vissza a
    // barátságos névre. Egy eszköz minden szakasza együtt kapja az új nevet. true, ha
    // változott. tracksChanged jel.
    bool renameTrack(const QString& meetingId, const QString& trackId, const QString& name);

    // Hiányzó hangfájl megkeresése („Megkeresés…”): a kiválasztott fájlt a meeting mappájába
    // MÁSOLJA (az eredeti érintetlen marad; ha már a mappában van, csak ráhivatkozik), és a
    // sávot erre állítja. Aktív sávnál a lekeverés elavultnak jelölődik. Hiba esetén false és
    // *error kitöltve (nincs változás). tracksChanged jel.
    bool relocateTrack(const QString& meetingId, const QString& trackId,
                       const QString& newFilePath, QString* error = nullptr);

    // MINDEN eldobott (nem aktív) sáv végleges törlése egy lépésben: a hangfájlok és a
    // hullámforma-gyorsítótáruk törlődnek, a sávok kikerülnek a meetingből. Az aktív sávokhoz
    // és a lekeveréshez nem nyúl. Visszatér: a törölt sávok száma. Egyetlen tracksChanged jel.
    int deleteDroppedTracks(const QString& meetingId);

signals:
    void tracksChanged(QString meetingId);

private:
    MeetingStore* m_store = nullptr;
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::TrackRole)
Q_DECLARE_METATYPE(tanara::TrackView)
