#pragma once
//
// PeopleService — a Személyek ablak háttere (UI-független): a személyek listája a
// kiegészítő adatokkal, a hangminták forrás-leírással, és a műveletek — új személy,
// átnevezés, becenevek, megjegyzés, minta törlése / áthelyezése / új személy egy mintából,
// hanglenyomat a kézzel hozzárendelt megbeszélésekből, összevonás, törlés —, valamint a
// visszavonás.
//
// Mit hova ír:
//  - people.json: személyenként egy rekord — név, becenevek, megjegyzés (PeopleStore);
//  - voiceprints.json: névhez kötött lenyomatok (VoiceprintStore);
//    mindkettő a tárolókon át (zár + visszaolvasás + atomikus írás);
//  - megbeszélések: az átnevezés / összevonás / törlés az AppController meglévő globális
//    műveletein megy át (renamePerson / setUserSpeakerName / removePerson), amelyek minden
//    megbeszélés speakerMap-jét, sáv-nevét és kézi résztvevőit átvezetik.
//
// Ami a beszélők kilétét érinti (összevonás, törlés, minta áthelyezése), az érintett
// megbeszélések összefoglalóját elavultnak jelöli a meglévő mechanizmussal
// (speakeredit::markSummaryStale), és a megszokott jeleket adja ki az AppControlleren
// (peopleChanged, voiceprintsChanged, speakerMapChanged, summaryStaleChanged), így a nyitott
// ablakok újraindítás nélkül frissülnek.
//
// Visszavonható: minta törlése, minta áthelyezése (és „új személy ebből a mintából”),
// becenév törlése, átnevezés. NEM vonható vissza: összevonás, személy törlése — ezek minden
// érintett megbeszélést átírnak; a verem ilyenkor kiürül.
//
#include "tanara/Types.h"
#include "tanara/edit/SpeakerEditTypes.h"
#include "tanara/edit/UtteranceEmbeddings.h"

#include <QDate>
#include <QObject>
#include <QStringList>
#include <QVector>

#include <memory>

namespace tanara {

class AppController;
class MeetingStore;
class PeopleStore;
class PeopleStats;
class VoiceprintStore;

struct PersonRecord {
    QString     name;
    QStringList aliases;
    QString     note;
    bool        isSelf = false;
    int         sampleCount = 0;
};

// Egy hangminta (lenyomat) megjelenítésre készen.
struct VoiceSample {
    QString id;
    QString person;
    QString sourceKind;     // "mic" | "call" | "system" | "mix" | "unknown"
    QString deviceLabel;    // barátságos eszköz- / sávnév; üres = ismeretlen
    QString meetingId;
    QString meetingTitle;   // üres, ha a megbeszélés már nincs meg
    QDate   recordedAt;     // a megbeszélés napja (annak híján a minta készítéséé)
    qint64  startMs = 0;    // a minta helye a hangfájlban (fájl-idő: a sáv eltolása levonva)
    qint64  endMs = 0;
    QString audioPath;      // abszolút út; üres, ha nem oldható fel
    bool    audioExists = false;
    qint64 durationMs() const { return endMs > startMs ? endMs - startMs : 0; }
};

struct PeopleOpResult {
    bool    ok = false;
    QString error;          // emberi hibaüzenet, ha !ok
    QString name;           // az érintett / létrejött személy neve
    int     meetings = 0;   // érintett megbeszélések
    int     samples = 0;    // érintett minták
    int     staleSummaries = 0;   // elavultnak jelölt összefoglalók
};

struct MergePreview {
    int meetingCount = 0;       // a két személy megbeszéléseinek uniója
    int sampleCount = 0;
    int staleSummaries = 0;     // ennyi összefoglaló lesz elavult (a megszűnő név megbeszélésein)
};

struct DeletePreview {
    int     meetingCount = 0;
    int     sampleCount = 0;
    int     staleSummaries = 0;
    QString exampleLabel;       // egy névtelen címke, amivé válik (pl. „Beszélő 2”); lehet üres
};

// Miből készülhet hanglenyomat annak, akinek még nincs.
struct VoiceprintPlan {
    bool        modelAvailable = false; // van hang-modell
    int         meetingsWithLines = 0;  // megbeszélések, ahol vannak sorai
    QStringList usableMeetingIds;       // ahol elég a biztos, hosszú sor egy mintához
    qint64      usableMs = 0;           // ezekből összesen ennyi hang kerül felhasználásra
};

enum class PeopleUndoKind { None, SampleRemoved, SampleMoved, AliasRemoved, Renamed };

struct PeopleUndoInfo {
    PeopleUndoKind kind = PeopleUndoKind::None;
    QString person;     // akinél a változás látszik (visszavonás után ezt érdemes kijelölni)
    QString detail;     // minta: a megbeszélés címe; becenév: a becenév; átnevezés: az új név
};

class PeopleService : public QObject {
    Q_OBJECT
public:
    PeopleService(AppController* controller, MeetingStore* store, PeopleStore* people,
                  VoiceprintStore* voiceprints, PeopleStats* stats, QObject* parent = nullptr);
    ~PeopleService() override;

    // A megszólalás-embedder gyára a „minta a megbeszélésekből” művelethez. Alapból a valódi
    // (ha van hang-modell); a tesztek hamisat adnak be.
    void setEmbedderFactory(UtteranceEmbedderFactory factory);

    // ---- olvasás (gyors: nem olvas megbeszéléseket) ----
    // A lemez friss állapota (másik folyamat is írhat a két fájlba).
    void reload();
    QString selfName() const;
    bool isSelf(const QString& name) const;
    // people.json ∪ lenyomat-DB ∪ a saját név; sorrend nélkül.
    QVector<PersonRecord> persons() const;
    PersonRecord person(const QString& name) const;
    bool exists(const QString& name) const;
    // A személy mintái, legújabb elöl.
    QVector<VoiceSample> samples(const QString& name) const;
    // Két személy hangjának hasonlósága a tárolt lenyomatokból (a legjobban egyező pár
    // koszinusza, 0…1 közé vágva); -1, ha valamelyiknek nincs lenyomata.
    double similarity(const QString& a, const QString& b) const;

    // ---- műveletek ----
    PeopleOpResult addPerson(const QString& name);
    PeopleOpResult renamePerson(const QString& oldName, const QString& newName);
    PeopleOpResult addAlias(const QString& name, const QString& alias);
    bool removeAlias(const QString& name, const QString& alias);
    void setNote(const QString& name, const QString& note);

    PeopleOpResult removeSample(const QString& printId);
    // A minta átkerül `toName`-hez. Ha ilyen személy nincs, létrejön („új személy ebből a
    // mintából”). A minta forrás-megbeszélésének összefoglalója elavult lesz.
    PeopleOpResult moveSample(const QString& printId, const QString& toName);

    VoiceprintPlan voiceprintPlan(const QString& name) const;
    // Egy megbeszélésből egy minta (a meglévő SpeakerEditor::createVoiceprint logikával).
    VoiceprintResult createVoiceprintFromMeeting(const QString& name, const QString& meetingId);

    MergePreview mergePreview(const QString& loser, const QString& survivor) const;
    // `loser` minden megbeszélése, mintája, beceneve `survivor`-hoz kerül; a `loser` név
    // becenév lesz. Nem vonható vissza.
    PeopleOpResult merge(const QString& loser, const QString& survivor);

    DeletePreview deletePreview(const QString& name) const;
    // A személy törlése minden megbeszélésről (névtelen beszélő marad). keepSamples: a
    // mintái egy új, „Névtelen N” nevű személyhez kerülnek (result.name), aki a listában
    // megjelenik, később átnevezhető vagy összevonható. A saját személy nem törölhető.
    // Nem vonható vissza.
    PeopleOpResult removePerson(const QString& name, bool keepSamples);

    // ---- visszavonás ----
    bool canUndo() const;
    PeopleUndoInfo undoInfo() const;    // a legfelső lépés
    PeopleUndoInfo undo();              // visszavonja; kind == None, ha nem volt mit

signals:
    // A lista vagy egy személy adatai megváltoztak (a nézetmodell újraépít).
    void changed();
    void undoChanged();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::VoiceSample)
