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
    // A sáv hossza ms-ben: a hullámforma-gyorsítótárból (pontos, dekódolt hossz), ha az már
    // elkészült és érvényes; különben -1 (a WaveformService::peaksReady adja meg).
    qint64    durationMs = -1;
};

namespace tracknames {

// A hívás (kommunikációs) eszközére utal-e a név — pl. a headsetek „Communication” /
// „Chat” / „Hands-Free” profilja, amit a hívás-appok használnak.
bool looksLikeCallDevice(const QString& deviceName);

// A monitor/loopback előtag és a profil-rész nélküli rövid eszköznév
// („Monitor of Kanto YU4 - Optikai Digitális sztereó (IEC958)” → „Kanto YU4”).
QString shortDeviceName(const QString& deviceName);

// Egy sáv szerepe a meeting ÖSSZES sávjának ismeretében:
//  - Mic → OwnMic; Other → Other;
//  - Loopback: ha a neve a hívás eszközére utal → CallAudio. Ha egyik loopback neve sem
//    utal rá: az egyetlen aktív loopback a CallAudio (a felvett hívás azon szól); több aktív
//    közül a leghangosabb (peakLevel). A többi SystemAudio.
TrackRole classify(const Track& track, const QVector<Track>& all);

// Barátságos nevek a meeting összes sávjára (a `all` sorrendjében). Azonos szerepű sávoknál
// a rövid eszköznév különböztet („Rendszerhang (Kanto YU4)”), végső esetben sorszám.
QStringList friendlyNames(const QVector<Track>& all);

} // namespace tracknames

class TrackCatalog : public QObject {
    Q_OBJECT
public:
    explicit TrackCatalog(MeetingStore* store, QObject* parent = nullptr);

    // A meeting sávjai megjelenítésre készen (a meeting.json sorrendjében).
    QVector<TrackView> tracks(const QString& meetingId) const;
    static QVector<TrackView> tracks(const Meeting& meeting);

    // Hány eldobott (nem aktív) sáv van — a „végleges törlés” gomb és a megerősítés számához.
    int droppedTrackCount(const QString& meetingId) const;

public slots:
    // Sáv átnevezése (perzisztens: meeting.json → track.customName). Üres név → vissza a
    // barátságos névre. true, ha változott. tracksChanged jel.
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
