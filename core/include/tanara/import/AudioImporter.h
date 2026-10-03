#pragma once
//
// AudioImporter — meglévő hang- (vagy videó-) fájlokból új meeting, „mintha külső
// interfészen vettük volna fel”: forrásonként egy sáv, vagy — ha a felhasználó kéri —
// a többcsatornás fájl csatornánként külön sávra bontva. UI-független (QtCore).
//
//  - A forrásfájlokhoz NEM nyúl (csak olvassa őket); a sávok ffmpeg-gel ugyanabba a
//    lemez-formába kerülnek, mint a felvett sávok: track_<slug>.ogg, Opus, 48 kHz, a
//    beállított bitrátával (opusBitrateKbps). Így a TrackCatalog, a lekeverés, a
//    hullámforma, a hang-azonosítás és az átirat-szerkesztő változatlanul működik.
//  - Több fájl: mind a 0. időpontban indul (nincs eltolás / szinkronizálás).
//  - Fájlonként az ELSŐ hangfolyam kerül be (videónál a hangja).
//  - Az importált sáv fajtája TrackKind::Other, fixedSpeaker = false (minden hangja a
//    beszélő-szétválasztáson megy át), a neve a fájl neve kiterjesztés nélkül, ill. bontott
//    csatornánál „Bal csatorna” / „Jobb csatorna” / „N. csatorna”. Egy sáv megjelölhető a
//    felhasználó saját mikrofonjaként: az TrackKind::Mic, fixedSpeaker = true, a beszélője
//    a felhasználó neve (ha nincs beállítva: „Mikrofon 1”, mint a felvett sávoknál).
//  - Félkész meeting SOSEM látszik: a kódolás egy rejtett <audioDir>/.import-<id> mappába
//    megy (az árva-helyreállítás a ponttal kezdődő mappákat nem nézi); csak a teljes siker
//    után kap végleges nevet és meeting.json-t. Hiba / megszakítás után a mappa törlődik.
//  - Lekeverést az importáló NEM készít: azt a hívó (AppController) indítja a mixdownMode
//    szerint, pontosan úgy, mint egy felvétel végén.
//
#include "tanara/Types.h"

#include <QObject>
#include <QVector>
#include <memory>

namespace tanara {

class MeetingStore;

// Egy forrásfájl ffprobe-eredménye.
struct ImportFileInfo {
    QString   path;               // abszolút út
    bool      ok = false;         // van benne dekódolható hangfolyam
    QString   error;              // !ok: emberi magyarázat
    qint64    durationMs = 0;     // 0 = a konténer nem mondja meg (a haladás ilyenkor határozatlan)
    int       channels = 0;
    int       sampleRate = 0;
    QString   codec;              // pl. "pcm_s16le", "aac", "opus"
    bool      hasVideo = false;   // videó-konténer (csak a hangja kerül be)
    qint64    sizeBytes = 0;
    QDateTime createdAt;          // a fájlba ágyazott készítési idő; érvénytelen, ha nincs
    QDateTime modifiedAt;         // a fájl módosítási ideje

    // A meeting kezdetének alapértelmezése: a beágyazott idő, különben a módosítási idő.
    QDateTime bestDate() const { return createdAt.isValid() ? createdAt : modifiedAt; }
};

struct ImportSource {
    QString path;
    bool    splitChannels = false;   // a csatornák külön sávra kerülnek (csak channels >= 2 mellett hat)
};

// Egy leendő sáv (a kérésből és a fájl-adatokból levezetve; a UI ebből mondja meg előre,
// hány sáv lesz, és ezek közül választható a „saját mikrofon”).
struct ImportPlannedTrack {
    int     sourceIndex = 0;
    int     channel = -1;         // -1 = a teljes fájl; különben a forrás csatornája (0-tól)
    int     channels = 1;         // a kész sáv csatornaszáma (1 vagy 2)
    QString name;                 // megjelenített név
};

struct ImportRequest {
    QVector<ImportSource> sources;
    QString   title;              // üres → audioimport::defaultTitle
    QDateTime startedAt;          // érvénytelen → audioimport::defaultStart
    int       ownTrack = -1;      // a tervezett sávok közül a saját mikrofon indexe; -1 = nincs
};

namespace audioimport {

// ffprobe, BLOKKOLVA (CLI, tesztek). A UI az AudioImporter::probeAsync-et használja.
ImportFileInfo probe(const QString& path, int timeoutMs = 20000);

// Érdemes-e alapból csatornákra bontani: kettőnél több csatornás HANGfájlnál igen (több-
// csatornás interfész felvétele); sima sztereónál és videónál nem (a felhasználó dönt).
bool splitByDefault(const ImportFileInfo& info);

// A leendő sávok a források sorrendjében. A `files` a `sources`-szal párhuzamos.
QVector<ImportPlannedTrack> planTracks(const QVector<ImportSource>& sources,
                                       const QVector<ImportFileInfo>& files);

// Cím: a fájl neve kiterjesztés nélkül; több fájlnál a nevek közös eleje (ha értelmes),
// különben az első fájl neve.
QString defaultTitle(const QStringList& paths);

// Kezdés: a források bestDate()-jei közül a legkorábbi érvényes; ha nincs, a mostani idő.
QDateTime defaultStart(const QVector<ImportFileInfo>& files);

// A csatorna neve: 2 csatornánál „Bal csatorna” / „Jobb csatorna”, különben „N. csatorna”.
QString channelName(int channel, int channelCount);

} // namespace audioimport

class AudioImporter : public QObject {
    Q_OBJECT
public:
    explicit AudioImporter(MeetingStore* store, QObject* parent = nullptr);
    ~AudioImporter() override;

    // A sávok Opus-bitrátája (kbps) és a „saját mikrofon” sáv beszélőneve.
    void setOpusBitrateKbps(int kbps);
    void setUserSpeakerName(const QString& name);

    bool    busy() const;          // fut egy importálás (egyszerre egy futhat)
    QString currentId() const;     // a futó importálás (= a leendő meeting) azonosítója

    // Egy fájl adatainak lekérése a háttérben → probed(). Több is futhat egyszerre.
    void probeAsync(const QString& path);

    // Importálás indítása. Visszaadja a leendő meeting azonosítóját; üres, ha már fut egy.
    // Minden további eredmény jelben jön (started → progress… → finished | failed | cancelled).
    QString start(const ImportRequest& request);

public slots:
    // A futó importálás megszakítása: az ffmpeg leáll, a félkész mappa törlődik → cancelled().
    void cancel();

signals:
    void probed(tanara::ImportFileInfo info);
    void started(QString importId, QString title);
    // percent: 0..100 a teljes munkára, -1 = nem mérhető; fileIndex: a futó forrás (0-tól).
    void progress(QString importId, int percent, int fileIndex, int fileCount);
    void finished(QString importId, tanara::Meeting meeting);
    void failed(QString importId, QString message, QString detail);
    void cancelled(QString importId);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::ImportFileInfo)
Q_DECLARE_METATYPE(tanara::ImportRequest)
