//
// A Személyek ablak hátterének tesztjei: PersonDetailsStore (becenevek, megjegyzés a
// people.json MELLETT), a régi fájlformátum sértetlensége mindkét irányban, becenév-keresés
// a személyválasztóban, statisztika (megbeszélés-szám, beszédidő, utoljára látva),
// átnevezés / összevonás / törlés / minta-áthelyezés / új személy mintából a számaikkal, az
// összefoglalók elavult-jelölése, visszavonás, és két FOLYAMAT egyidejű írása.
//
// Minden egy ideiglenes TANARA_HOME-ban fut, kitalált nevekkel; valódi adathoz, modellhez,
// ffmpeg-hez nem nyúl (a „hang” hamis embedder).
//
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/edit/PeopleDirectory.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/people/PeopleService.h"
#include "tanara/people/PeopleStats.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/PeopleStore.h"
#include "tanara/store/PersonDetailsStore.h"
#include "tanara/store/VoiceprintStore.h"

#include <memory>

using namespace tanara;

namespace {

const QString kSelf = QStringLiteral("Kovács Lilla");
const QString kGergely = QStringLiteral("Bárány Gergely");
const QString kGergo = QStringLiteral("B. Gergő");
const QString kEszter = QStringLiteral("Tóth Eszter");

QByteArray readFile(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

struct Line { qint64 startMs; qint64 durMs; QString raw; };

// A „hang” a nyers címkéből tudható: a hamis embedder a szelet közepén beszélő címkéjére ad
// vektort (Beszélő 1 → x, Beszélő 2 → y, más → z).
class FakeEmbedder : public IUtteranceEmbedder {
public:
    bool open(const QString&) override { return true; }
    QVector<float> embed(qint64, qint64) override { return {1.0f, 0.0f, 0.0f}; }
};

Voiceprint print(const QString& id, const QVector<float>& e, const QString& meetingId = QString())
{
    Voiceprint p;
    p.id = id;
    p.embedding = e;
    p.sourceMeetingId = meetingId;
    p.sourceTrack = QStringLiteral("mixdown");
    p.sampleRef = QStringLiteral("mixdown.mp3#1000-9000");
    p.createdAt = QStringLiteral("2026-09-30T10:00:00");
    return p;
}

} // namespace

class PeopleServiceTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void detailsRoundTripKeepsUnknownFields();
    void detailsCorruptFileIsSetAside();
    void oldFormatFilesStayOldFormat();
    void oldBuildWritesDoNotTouchDetails();
    void aliasSearchInPersonPicker();
    void statsCountMeetingsTalkTimeAndLastSeen();
    void statsRefreshInBackgroundAndCache();
    void renameKeepsOldNameAsAliasAndFollowsMeetings();
    void renameToExistingNameIsRefused();
    void selfRenameChangesSettingAndSelfCannotBeDeleted();
    void aliasAddRemoveUndo();
    void noteIsStored();
    void sampleRemoveAndUndo();
    void sampleMoveMarksStaleAndUndoRestores();
    void newPersonFromSampleAndUndoRemovesPerson();
    void similarityFromStoredEmbeddings();
    void mergeNumbersAliasAndStale();
    void mergeWithOpenEditorShowsStale();
    void deleteNumbersAndStale();
    void deleteKeepingSamplesAsAnonymous();
    void voiceprintFromManuallyAssignedMeetings();
    void sampleSourceDescription();
    void twoProcessesWriteConcurrently();

private:
    QString metaFile(const QString& name) const { return QDir(m_app->store()->metadataDir()).filePath(name); }
    // Egy átírt megbeszélés: sorok nyers címkékkel, speakerMap, opcionálisan összefoglalóval.
    Meeting addMeeting(const QString& title, const QDateTime& when, const QVector<Line>& lines,
                       const QMap<QString, QString>& speakerMap, bool hasSummary);
    PeopleService* svc() const { return m_app->peopleService(); }

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<AppController> m_app;
};

void PeopleServiceTest::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    qputenv("TANARA_HOME", m_home->path().toUtf8());
    m_app = std::make_unique<AppController>();
    QVERIFY(m_app->settings()->settings().audioDir.startsWith(m_home->path()));
    m_app->setUserSpeakerName(kSelf);
}

void PeopleServiceTest::cleanup()
{
    m_app.reset();
    m_home.reset();
}

Meeting PeopleServiceTest::addMeeting(const QString& title, const QDateTime& when,
                                      const QVector<Line>& lines,
                                      const QMap<QString, QString>& speakerMap, bool hasSummary)
{
    Meeting m = m_app->store()->createMeeting(title);
    QJsonArray segs;
    for (const Line& l : lines) {
        QJsonObject o;
        o["startMs"] = double(l.startMs);
        o["endMs"] = double(l.startMs + l.durMs);
        o["speaker"] = l.raw;
        o["text"] = QStringLiteral("szöveg");
        segs.append(o);
    }
    writeFile(QDir(m.folder).filePath("transcript.segments.json"), QJsonDocument(segs).toJson());
    m.startedAt = when;
    m.hasTranscript = true;
    m.hasSummary = hasSummary;
    m.speakerMap = speakerMap;
    m.mixdownFile = QStringLiteral("mixdown.mp3");
    m_app->store()->saveMeeting(m);
    for (const QString& name : speakerMap) m_app->peopleService()->addPerson(name);
    return m_app->store()->load(m.id);
}

// ---- PersonDetailsStore ------------------------------------------------------

void PeopleServiceTest::detailsRoundTripKeepsUnknownFields()
{
    const QString path = m_home->filePath("d/people-details.json");
    // Egy JÖVŐBELI build mezői (gyökérben és személynél) nem veszhetnek el.
    writeFile(path, R"({"version":1,"futureRoot":7,"people":[
        {"name":"Bárány Gergely","aliases":["Gergely"],"note":"PM","color":"blue"}]})");
    {
        PersonDetailsStore store(path);
        QCOMPARE(store.aliases(kGergely), QStringList{"Gergely"});
        QCOMPARE(store.details("bárány gergely").note, QStringLiteral("PM"));   // kisbetű-független
        QVERIFY(store.addAlias(kGergely, "G. Bárány"));
        QVERIFY(!store.addAlias(kGergely, "gergely"));        // már szerepel
        QVERIFY(!store.addAlias(kGergely, kGergely));         // a saját neve nem becenév
        store.setNote(kEszter, "Pénzügy");
    }
    PersonDetailsStore fresh(path);
    QCOMPARE(fresh.aliases(kGergely), QStringList({"Gergely", "G. Bárány"}));
    QCOMPARE(fresh.details(kEszter).note, QStringLiteral("Pénzügy"));
    const QJsonObject root = QJsonDocument::fromJson(readFile(path)).object();
    QCOMPARE(root.value("futureRoot").toInt(), 7);
    QCOMPARE(root.value("people").toArray().first().toObject().value("color").toString(), QStringLiteral("blue"));

    // Átnevezés létező névre = egyesítés; a régi név becenév lesz, a megjegyzések megmaradnak.
    fresh.rename(kEszter, kGergely, true);
    QCOMPARE(fresh.aliases(kGergely), QStringList({"Gergely", "G. Bárány", kEszter}));
    QCOMPARE(fresh.details(kGergely).note, QStringLiteral("PM\nPénzügy"));
    QVERIFY(fresh.details(kEszter).isEmpty());
    // Üres bejegyzés nem marad a fájlban.
    fresh.set(PersonDetails{kGergely, {}, QString()});
    QCOMPARE(PersonDetailsStore(path).all().size(), 0);
}

void PeopleServiceTest::detailsCorruptFileIsSetAside()
{
    const QString path = m_home->filePath("d/people-details.json");
    writeFile(path, R"({"people":[{"name":"X","alia)");
    PersonDetailsStore store(path);
    QVERIFY(store.addAlias(kGergely, "Gergő"));
    QCOMPARE(QDir(m_home->filePath("d")).entryList({"people-details.json.corrupt-*"}, QDir::Files).size(), 1);
    QCOMPARE(PersonDetailsStore(path).aliases(kGergely), QStringList{"Gergő"});
}

// ---- kompatibilitás a régi buildekkel ----------------------------------------

void PeopleServiceTest::oldFormatFilesStayOldFormat()
{
    // RÉGI formátumú fájlok (ahogy egy korábbi build írta) → az új build mindent lát belőlük.
    m_app.reset();
    const QString home = m_home->path();
    writeFile(QDir(home).filePath("people.json"), R"({"people":["B. Gergő","Bárány Gergely","Kovács Lilla"]})");
    writeFile(QDir(home).filePath("voiceprints.json"),
              R"({"people":[{"name":"Bárány Gergely","prints":[{"id":"p1","embedding":[1,0,0],"dim":3,
                 "sourceMeetingId":"","sourceTrack":"mic","device":"USB","sampleRef":"track_mic.ogg#0-4000",
                 "createdAt":"2026-09-01T10:00:00"}]}]})");
    m_app = std::make_unique<AppController>();
    m_app->setUserSpeakerName(kSelf);
    QCOMPARE(svc()->persons().size(), 3);
    QCOMPARE(svc()->person(kGergely).sampleCount, 1);
    QVERIFY(!QFile::exists(metaFile("people-details.json")));   // olvasástól nem jön létre

    // Az új műveletek után is a RÉGI alak marad a két fájlban — egy régi build be tudja tölteni.
    QVERIFY(svc()->addAlias(kGergely, "Gergely").ok);
    svc()->setNote(kGergely, "Projektvezető");
    QVERIFY(svc()->addPerson(kEszter).ok);
    QVERIFY(svc()->moveSample("p1", kEszter).ok);
    QVERIFY(svc()->merge(kGergo, kGergely).ok);
    QVERIFY(svc()->renamePerson(kEszter, "Tóth Eszti").ok);

    const QJsonObject people = QJsonDocument::fromJson(readFile(metaFile("people.json"))).object();
    QCOMPARE(people.keys(), QStringList{"people"});
    for (const QJsonValue& v : people.value("people").toArray()) QVERIFY(v.isString());
    const QJsonObject prints = QJsonDocument::fromJson(readFile(metaFile("voiceprints.json"))).object();
    QCOMPARE(prints.keys(), QStringList{"people"});
    const QStringList printKeys{"createdAt", "device", "dim", "embedding", "id", "sampleRef",
                                "sourceMeetingId", "sourceTrack"};
    for (const QJsonValue& pv : prints.value("people").toArray()) {
        QCOMPARE(pv.toObject().keys(), QStringList({"name", "prints"}));
        for (const QJsonValue& v : pv.toObject().value("prints").toArray())
            QCOMPARE(v.toObject().keys(), printKeys);
    }
    // A minta minden mezője megmaradt az áthelyezés + átnevezés után.
    const QVector<Voiceprint> moved = VoiceprintStore(metaFile("voiceprints.json")).printsFor("Tóth Eszti");
    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.first().id, QStringLiteral("p1"));
    QCOMPARE(moved.first().device, QStringLiteral("USB"));
    QCOMPARE(moved.first().sampleRef, QStringLiteral("track_mic.ogg#0-4000"));
    QCOMPARE(PeopleStore(metaFile("people.json")).names(), QStringList({kGergely, kSelf, "Tóth Eszti"}));
}

void PeopleServiceTest::oldBuildWritesDoNotTouchDetails()
{
    QVERIFY(svc()->addPerson(kGergely).ok);
    QVERIFY(svc()->addAlias(kGergely, "Gergely").ok);
    svc()->setNote(kGergely, "Projektvezető");
    const QByteArray detailsBefore = readFile(metaFile("people-details.json"));

    // Egy RÉGI build (csak a két régi tárolót ismeri) a teljes fájlokat újraírja.
    {
        PeopleStore oldPeople(metaFile("people.json"));
        oldPeople.add("Régi Build Rozi");
        VoiceprintStore oldPrints(metaFile("voiceprints.json"));
        oldPrints.addPrint("Régi Build Rozi", print("old1", {0, 1, 0}));
    }
    QCOMPARE(readFile(metaFile("people-details.json")), detailsBefore);
    svc()->reload();
    QVERIFY(svc()->exists("Régi Build Rozi"));
    QCOMPARE(svc()->person(kGergely).aliases, QStringList{"Gergely"});
    QCOMPARE(svc()->person(kGergely).note, QStringLiteral("Projektvezető"));

    // Ha a régi build nevez át valakit, a bejegyzés a régi néven megmarad (nem vész el).
    {
        PeopleStore oldPeople(metaFile("people.json"));
        oldPeople.rename(kGergely, "Bárány G.");
    }
    svc()->reload();
    QVERIFY(svc()->person("Bárány G.").aliases.isEmpty());
    QCOMPARE(PersonDetailsStore(metaFile("people-details.json")).aliases(kGergely), QStringList{"Gergely"});
}

// ---- becenév-keresés ----------------------------------------------------------

void PeopleServiceTest::aliasSearchInPersonPicker()
{
    QVERIFY(svc()->addPerson(kGergely).ok);
    QVERIFY(svc()->addPerson(kGergo).ok);
    QVERIFY(svc()->addPerson("Gál Bence").ok);
    QVERIFY(svc()->addAlias("Gál Bence", "Gergő bátyja").ok);
    QVERIFY(svc()->addAlias(kGergely, "Főnök").ok);

    const QVector<PersonInfo> all = m_app->peopleDirectory();
    // Ékezet- és kisbetű-független, a névre egyezők elöl, a csak becenévre egyezők utánuk.
    const QVector<PersonInfo> hits = filterPeople(all, "GERGO");
    QCOMPARE(hits.size(), 2);
    QCOMPARE(hits[0].name, kGergo);
    QVERIFY(hits[0].matchedAlias.isEmpty());
    QCOMPARE(hits[1].name, QStringLiteral("Gál Bence"));
    QCOMPARE(hits[1].matchedAlias, QStringLiteral("Gergő bátyja"));
    const QVector<PersonInfo> boss = filterPeople(all, "fonok");
    QCOMPARE(boss.size(), 1);
    QCOMPARE(boss[0].name, kGergely);
    QVERIFY(filterPeople(all, "nincs ilyen").isEmpty());
}

// ---- statisztika ----------------------------------------------------------------

void PeopleServiceTest::statsCountMeetingsTalkTimeAndLastSeen()
{
    const QDateTime d1(QDate(2026, 9, 24), QTime(10, 0)), d2(QDate(2026, 10, 1), QTime(9, 0));
    addMeeting("Partnerdemó", d1, {{0, 4000, "Beszélő 1"}, {5000, 6000, "Beszélő 2"}, {12000, 2000, "Beszélő 1"}},
               {{"Beszélő 1", kGergely}}, false);
    const Meeting m2 = addMeeting("Heti egyeztetés", d2, {{0, 10000, "Beszélő 1"}, {11000, 3000, "Beszélő 2"}},
                                  {{"Beszélő 2", kGergely}, {"Beszélő 1", kEszter}}, true);
    // Kézi javítás: a második megbeszélés első sora valójában Gergelyé (soronkénti felülírás).
    {
        SpeakerEditor ed(m_app->store(), nullptr, nullptr, m2.id);
        QVERIFY(ed.moveUtterances({"u0"}, "Beszélő 2"));
    }
    PeopleStats* stats = m_app->peopleStats();
    QVERIFY(!stats->ready());
    stats->refreshNow();
    QVERIFY(stats->ready());

    const PersonStats g = stats->stats("bárány gergely");
    QCOMPARE(g.meetingCount, 2);
    QCOMPARE(g.talkMs, qint64(4000 + 2000 + 10000 + 3000));
    QCOMPARE(g.lastSeen, d2);
    QCOMPARE(g.meetings.size(), 2);
    QCOMPARE(g.meetings[0].title, QStringLiteral("Heti egyeztetés"));   // legújabb elöl
    QCOMPARE(g.meetings[0].talkMs, qint64(13000));
    QCOMPARE(g.meetings[0].speakerKeys, QStringList{"Beszélő 2"});
    QVERIFY(g.meetings[0].hasSummary);
    const PersonStats e = stats->stats(kEszter);
    QCOMPARE(e.meetingCount, 1);
    QCOMPARE(e.talkMs, qint64(0));              // minden sora máshoz került
    QCOMPARE(stats->stats("Ismeretlen").meetingCount, 0);
    QVERIFY(!stats->stats("Ismeretlen").lastSeen.isValid());
}

void PeopleServiceTest::statsRefreshInBackgroundAndCache()
{
    for (int i = 0; i < 12; ++i)
        addMeeting(QStringLiteral("Megbeszélés %1").arg(i), QDateTime(QDate(2026, 9, 1 + i), QTime(10, 0)),
                   {{0, 5000, "Beszélő 1"}}, {{"Beszélő 1", kGergely}}, false);
    PeopleStats* stats = m_app->peopleStats();
    QSignalSpy changed(stats, &PeopleStats::changed);
    stats->refresh();
    QVERIFY(stats->busy());
    QCOMPARE(stats->stats(kGergely).meetingCount, 0);    // a lista addig is megjelenhet: a hívás nem vár
    QVERIFY(changed.wait(5000));
    QVERIFY(!stats->busy());
    QCOMPARE(stats->stats(kGergely).meetingCount, 12);
    QCOMPARE(stats->lastRescanned(), 12);

    // Gyorsítótár: változatlan megbeszéléseket nem olvas újra; egy megváltozottat igen.
    stats->refreshNow();
    QCOMPARE(stats->lastRescanned(), 0);
    Meeting m = m_app->store()->loadAll().first();
    m = m_app->store()->load(m.id);
    m.speakerMap.insert("Beszélő 1", kEszter);
    m_app->store()->saveMeeting(m);
    stats->refreshNow();
    QCOMPARE(stats->lastRescanned(), 1);
    QCOMPARE(stats->stats(kGergely).meetingCount, 11);
    QCOMPARE(stats->stats(kEszter).meetingCount, 1);
    // Törölt megbeszélés kikerül.
    m_app->store()->deleteMeeting(m.id);
    stats->refreshNow();
    QCOMPARE(stats->stats(kEszter).meetingCount, 0);
}

// ---- átnevezés -------------------------------------------------------------------

void PeopleServiceTest::renameKeepsOldNameAsAliasAndFollowsMeetings()
{
    const Meeting m = addMeeting("Partnerdemó", QDateTime(QDate(2026, 9, 24), QTime(10, 0)),
                                 {{0, 4000, "Beszélő 1"}}, {{"Beszélő 1", kGergely}}, false);
    m_app->voiceprints()->addPrint(kGergely, print("p1", {1, 0, 0}, m.id));
    QVERIFY(svc()->addAlias(kGergely, "Gergely").ok);

    const PeopleOpResult r = svc()->renamePerson(kGergely, "Bárány Gergő");
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.name, QStringLiteral("Bárány Gergő"));
    QVERIFY(!svc()->exists(kGergely));
    QCOMPARE(svc()->person("Bárány Gergő").aliases, QStringList({"Gergely", kGergely}));
    QCOMPARE(svc()->person("Bárány Gergő").sampleCount, 1);
    QCOMPARE(m_app->store()->load(m.id).speakerMap.value("Beszélő 1"), QStringLiteral("Bárány Gergő"));
    // A régi néven a személyválasztó továbbra is megtalálja.
    QCOMPARE(filterPeople(m_app->peopleDirectory(), "gergely").size(), 1);

    // Visszavonás: a régi név vissza mindenhol, az automatikus becenév nélkül.
    QVERIFY(svc()->canUndo());
    QCOMPARE(svc()->undoInfo().kind, PeopleUndoKind::Renamed);
    const PeopleUndoInfo info = svc()->undo();
    QCOMPARE(info.person, kGergely);
    QVERIFY(svc()->exists(kGergely));
    QVERIFY(!svc()->exists("Bárány Gergő"));
    QCOMPARE(svc()->person(kGergely).aliases, QStringList{"Gergely"});
    QCOMPARE(m_app->store()->load(m.id).speakerMap.value("Beszélő 1"), kGergely);
    QCOMPARE(m_app->voiceprints()->printCount(kGergely), 1);
    QVERIFY(!svc()->canUndo());
}

void PeopleServiceTest::renameToExistingNameIsRefused()
{
    QVERIFY(svc()->addPerson(kGergely).ok);
    QVERIFY(svc()->addPerson(kGergo).ok);
    const PeopleOpResult r = svc()->renamePerson(kGergo, "bárány gergely");
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
    QVERIFY(svc()->exists(kGergo));
    QVERIFY(!svc()->renamePerson(kGergo, "  ").ok);
    QVERIFY(!svc()->canUndo());
    // Új személy létező névvel: a meglévőt adja vissza.
    const PeopleOpResult dup = svc()->addPerson("b. gergő");
    QVERIFY(!dup.ok);
    QCOMPARE(dup.name, kGergo);
}

void PeopleServiceTest::selfRenameChangesSettingAndSelfCannotBeDeleted()
{
    QVERIFY(svc()->isSelf("kovács lilla"));
    QVERIFY(svc()->person(kSelf).isSelf);
    QVERIFY(svc()->renamePerson(kSelf, "Kovács-Nagy Lilla").ok);
    QCOMPARE(m_app->settings()->settings().userSpeakerName, QStringLiteral("Kovács-Nagy Lilla"));
    QVERIFY(svc()->isSelf("Kovács-Nagy Lilla"));
    QCOMPARE(svc()->person("Kovács-Nagy Lilla").aliases, QStringList{kSelf});

    const PeopleOpResult del = svc()->removePerson("Kovács-Nagy Lilla", false);
    QVERIFY(!del.ok);
    QVERIFY(svc()->exists("Kovács-Nagy Lilla"));

    svc()->undo();
    QCOMPARE(m_app->settings()->settings().userSpeakerName, kSelf);
    QVERIFY(svc()->person(kSelf).aliases.isEmpty());
}

// ---- becenév, megjegyzés ---------------------------------------------------------

void PeopleServiceTest::aliasAddRemoveUndo()
{
    QVERIFY(svc()->addPerson(kGergely).ok);
    QSignalSpy people(m_app.get(), &AppController::peopleChanged);
    QVERIFY(svc()->addAlias(kGergely, "Gergely").ok);
    QVERIFY(svc()->addAlias(kGergely, "G. Bárány").ok);
    QVERIFY(svc()->addAlias(kGergely, "Geri").ok);
    QCOMPARE(people.count(), 3);                       // a személyválasztók frissülnek
    QVERIFY(!svc()->addAlias(kGergely, "geri").ok);
    QVERIFY(!svc()->addAlias(kGergely, " ").ok);
    QVERIFY(!svc()->addAlias(kGergely, kGergely).ok);

    QVERIFY(svc()->removeAlias(kGergely, "G. Bárány"));
    QCOMPARE(svc()->person(kGergely).aliases, QStringList({"Gergely", "Geri"}));
    QCOMPARE(svc()->undoInfo().kind, PeopleUndoKind::AliasRemoved);
    QCOMPARE(svc()->undoInfo().detail, QStringLiteral("G. Bárány"));
    svc()->undo();
    QCOMPARE(svc()->person(kGergely).aliases, QStringList({"Gergely", "G. Bárány", "Geri"}));   // a helyére
    QVERIFY(!svc()->removeAlias(kGergely, "nincs"));
}

void PeopleServiceTest::noteIsStored()
{
    QVERIFY(svc()->addPerson(kGergely).ok);
    QSignalSpy changed(svc(), &PeopleService::changed);
    svc()->setNote(kGergely, "Northwind oldali projektvezető.\nGyakran telefonról csatlakozik.");
    QCOMPARE(changed.count(), 1);
    svc()->setNote(kGergely, "Northwind oldali projektvezető.\nGyakran telefonról csatlakozik.");
    QCOMPARE(changed.count(), 1);    // változatlan: nincs írás
    QCOMPARE(PersonDetailsStore(metaFile("people-details.json")).details(kGergely).note,
             QStringLiteral("Northwind oldali projektvezető.\nGyakran telefonról csatlakozik."));
}

// ---- minták ----------------------------------------------------------------------

void PeopleServiceTest::sampleRemoveAndUndo()
{
    const Meeting m = addMeeting("Partnerdemó – adatimport", QDateTime(QDate(2026, 9, 24), QTime(10, 0)),
                                 {{0, 4000, "Beszélő 1"}}, {{"Beszélő 1", kGergely}}, true);
    m_app->voiceprints()->addPrint(kGergely, print("p1", {1, 0, 0}, m.id));
    QSignalSpy prints(m_app.get(), &AppController::voiceprintsChanged);

    const PeopleOpResult r = svc()->removeSample("p1");
    QVERIFY(r.ok);
    QCOMPARE(r.name, kGergely);
    QCOMPARE(prints.count(), 1);
    QCOMPARE(svc()->person(kGergely).sampleCount, 0);
    QVERIFY(svc()->exists(kGergely));                      // a személy megmarad
    QCOMPARE(svc()->undoInfo().kind, PeopleUndoKind::SampleRemoved);
    QCOMPARE(svc()->undoInfo().detail, QStringLiteral("Partnerdemó – adatimport"));
    QVERIFY(!m_app->summaryStale(m.id).stale);             // a minta törlése nem kilét-változás

    svc()->undo();
    const QVector<Voiceprint> back = m_app->voiceprints()->printsFor(kGergely);
    QCOMPARE(back.size(), 1);
    QCOMPARE(back.first().id, QStringLiteral("p1"));       // ugyanaz a minta, ugyanazzal az azonosítóval
    QCOMPARE(back.first().sampleRef, QStringLiteral("mixdown.mp3#1000-9000"));
    QVERIFY(!svc()->removeSample("nincs").ok);
}

void PeopleServiceTest::sampleMoveMarksStaleAndUndoRestores()
{
    const Meeting m = addMeeting("Partnerdemó", QDateTime(QDate(2026, 9, 24), QTime(10, 0)),
                                 {{0, 4000, "Beszélő 1"}}, {{"Beszélő 1", kGergely}}, true);
    const Meeting other = addMeeting("Másik", QDateTime(QDate(2026, 9, 25), QTime(10, 0)),
                                     {{0, 4000, "Beszélő 1"}}, {{"Beszélő 1", kGergely}}, true);
    QVERIFY(svc()->addPerson(kGergo).ok);
    m_app->voiceprints()->addPrint(kGergely, print("p1", {1, 0, 0}, m.id));
    m_app->voiceprints()->addPrint(kGergely, print("p2", {0, 1, 0}, other.id));
    QSignalSpy stale(m_app.get(), &AppController::summaryStaleChanged);

    const PeopleOpResult r = svc()->moveSample("p1", "b. gergő");
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.name, kGergo);                              // a tárolt írásmód
    QCOMPARE(r.staleSummaries, 1);
    QCOMPARE(m_app->voiceprints()->printCount(kGergely), 1);
    QCOMPARE(m_app->voiceprints()->printsFor(kGergo).first().id, QStringLiteral("p1"));
    QVERIFY(m_app->summaryStale(m.id).stale);              // a minta forrás-megbeszélése
    QVERIFY(!m_app->summaryStale(other.id).stale);         // más megbeszélés nem
    QCOMPARE(stale.count(), 1);
    QVERIFY(!svc()->moveSample("p2", kGergely).ok);        // már nála van

    svc()->undo();
    QCOMPARE(m_app->voiceprints()->printCount(kGergely), 2);
    QCOMPARE(m_app->voiceprints()->printCount(kGergo), 0);
    QVERIFY(svc()->exists(kGergo));                        // a cél korábban is létezett: marad
    QVERIFY(!m_app->summaryStale(m.id).stale);             // az elavult-jelölés is visszavonva
}

void PeopleServiceTest::newPersonFromSampleAndUndoRemovesPerson()
{
    const Meeting m = addMeeting("Partnerdemó", QDateTime(QDate(2026, 9, 24), QTime(10, 0)),
                                 {{0, 4000, "Beszélő 1"}}, {{"Beszélő 1", kGergely}}, false);
    m_app->voiceprints()->addPrint(kGergely, print("p1", {1, 0, 0}, m.id));
    m_app->voiceprints()->addPrint(kGergely, print("p2", {0, 1, 0}, m.id));

    const PeopleOpResult r = svc()->moveSample("p2", "Új Ember");
    QVERIFY(r.ok);
    QCOMPARE(r.name, QStringLiteral("Új Ember"));
    QCOMPARE(r.staleSummaries, 0);                          // nincs összefoglaló: nincs mit jelölni
    QVERIFY(svc()->exists("Új Ember"));
    QVERIFY(PeopleStore(metaFile("people.json")).names().contains("Új Ember"));
    QCOMPARE(svc()->person("Új Ember").sampleCount, 1);
    QCOMPARE(svc()->person(kGergely).sampleCount, 1);       // a minta elhagyta a korábbi lenyomatot

    svc()->undo();
    QVERIFY(!svc()->exists("Új Ember"));                    // a művelet hozta létre: eltűnik
    QCOMPARE(svc()->person(kGergely).sampleCount, 2);
}

void PeopleServiceTest::similarityFromStoredEmbeddings()
{
    m_app->voiceprints()->addPrint(kGergely, print("a1", {1, 0, 0}));
    m_app->voiceprints()->addPrint(kGergely, print("a2", {0, 1, 0}));
    m_app->voiceprints()->addPrint(kGergo, print("b1", {0.6f, 0.8f, 0}));
    m_app->voiceprints()->addPrint("Gál Bence", print("c1", {0, 0, 1}));
    QVERIFY(svc()->addPerson(kEszter).ok);
    // A legjobban egyező pár számít (ahogy a felismerésnél).
    QVERIFY(qAbs(svc()->similarity(kGergely, kGergo) - 0.8) < 1e-6);
    QVERIFY(qAbs(svc()->similarity(kGergo, kGergely) - 0.8) < 1e-6);
    QCOMPARE(svc()->similarity(kGergely, "Gál Bence"), 0.0);
    QCOMPARE(svc()->similarity(kGergely, kEszter), -1.0);   // nincs lenyomata: nincs százalék
}

// ---- összevonás ------------------------------------------------------------------

void PeopleServiceTest::mergeNumbersAliasAndStale()
{
    const QDateTime d(QDate(2026, 9, 20), QTime(10, 0));
    const Meeting both = addMeeting("Közös", d, {{0, 4000, "Beszélő 1"}, {5000, 5000, "Beszélő 2"}},
                                    {{"Beszélő 1", kGergely}, {"Beszélő 2", kGergo}}, true);
    const Meeting onlyLoser = addMeeting("Csak Gergő", d.addDays(1), {{0, 4000, "Beszélő 1"}},
                                         {{"Beszélő 1", kGergo}}, true);
    const Meeting noSummary = addMeeting("Összefoglaló nélkül", d.addDays(2), {{0, 4000, "Beszélő 1"}},
                                         {{"Beszélő 1", kGergo}}, false);
    const Meeting onlySurvivor = addMeeting("Csak Gergely", d.addDays(3), {{0, 4000, "Beszélő 1"}},
                                            {{"Beszélő 1", kGergely}}, true);
    m_app->voiceprints()->addPrint(kGergely, print("a1", {1, 0, 0}, both.id));
    m_app->voiceprints()->addPrint(kGergo, print("b1", {0, 1, 0}, onlyLoser.id));
    m_app->voiceprints()->addPrint(kGergo, print("b2", {0, 1, 0}, onlyLoser.id));
    QVERIFY(svc()->addAlias(kGergo, "Geri").ok);
    svc()->setNote(kGergo, "Telefonról csatlakozik.");
    m_app->peopleStats()->refreshNow();

    const MergePreview preview = svc()->mergePreview(kGergo, kGergely);
    QCOMPARE(preview.meetingCount, 4);          // unió: a közös megbeszélés egyszer számít
    QCOMPARE(preview.sampleCount, 3);
    QCOMPARE(preview.staleSummaries, 2);        // a megszűnő név összefoglalós megbeszélései

    QSignalSpy stale(m_app.get(), &AppController::summaryStaleChanged);
    const PeopleOpResult r = svc()->merge(kGergo, kGergely);
    QVERIFY2(r.ok, qPrintable(r.error));
    // A párbeszédablak számai = a tényleges eredmény.
    QCOMPARE(r.meetings, preview.meetingCount);
    QCOMPARE(r.samples, preview.sampleCount);
    QCOMPARE(r.staleSummaries, preview.staleSummaries);
    QCOMPARE(stale.count(), 2);

    QVERIFY(!svc()->exists(kGergo));
    const PersonRecord g = svc()->person(kGergely);
    QCOMPARE(g.sampleCount, 3);
    QCOMPARE(g.aliases, QStringList({"Geri", kGergo}));     // a megszűnő név becenév lett
    QCOMPARE(g.note, QStringLiteral("Telefonról csatlakozik."));
    QCOMPARE(m_app->peopleStats()->stats(kGergely).meetingCount, 4);
    QCOMPARE(m_app->store()->load(both.id).speakerMap.value("Beszélő 2"), kGergely);
    QCOMPARE(m_app->store()->load(noSummary.id).speakerMap.value("Beszélő 1"), kGergely);

    QVERIFY(m_app->summaryStale(both.id).stale);
    QVERIFY(m_app->summaryStale(onlyLoser.id).stale);
    QVERIFY(!m_app->summaryStale(noSummary.id).stale);       // nincs összefoglalója
    QVERIFY(!m_app->summaryStale(onlySurvivor.id).stale);    // ott semmi nem változott
    QVERIFY(!svc()->canUndo());                              // az összevonás nem vonható vissza
    QVERIFY(!svc()->merge(kGergely, kGergely).ok);
    QVERIFY(!svc()->merge("Nincs Ilyen", kGergely).ok);
}

void PeopleServiceTest::mergeWithOpenEditorShowsStale()
{
    const Meeting m = addMeeting("Nyitott", QDateTime(QDate(2026, 9, 20), QTime(10, 0)),
                                 {{0, 4000, "Beszélő 1"}, {5000, 4000, "Beszélő 2"}},
                                 {{"Beszélő 1", kGergo}}, true);
    QVERIFY(svc()->addPerson(kGergely).ok);
    // A főablakban nyitva van az átirat-szerkesztő: újraindítás nélkül kell követnie.
    SpeakerEditor* ed = m_app->speakerEditor(m.id);
    QVERIFY(ed);
    QVERIFY(!ed->summaryStale().stale);
    QVERIFY(svc()->merge(kGergo, kGergely).ok);
    QVERIFY(ed->summaryStale().stale);
    QCOMPARE(ed->speaker("Beszélő 1").personName, kGergely);
    QVERIFY(m_app->summaryStale(m.id).stale);
}

// ---- törlés ----------------------------------------------------------------------

void PeopleServiceTest::deleteNumbersAndStale()
{
    const QDateTime d(QDate(2026, 9, 20), QTime(10, 0));
    const Meeting a = addMeeting("Egyik", d, {{0, 4000, "Távoli 2"}}, {{"Távoli 2", kGergely}}, true);
    const Meeting b = addMeeting("Másik", d.addDays(1), {{0, 4000, "Beszélő 1"}}, {{"Beszélő 1", kGergely}}, false);
    m_app->voiceprints()->addPrint(kGergely, print("a1", {1, 0, 0}, a.id));
    QVERIFY(svc()->addAlias(kGergely, "Gergely").ok);
    QVERIFY(svc()->removeAlias(kGergely, "Gergely"));       // legyen egy visszavonható lépés
    m_app->peopleStats()->refreshNow();

    const DeletePreview preview = svc()->deletePreview(kGergely);
    QCOMPARE(preview.meetingCount, 2);
    QCOMPARE(preview.sampleCount, 1);
    QCOMPARE(preview.staleSummaries, 1);
    QCOMPARE(preview.exampleLabel, QStringLiteral("Beszélő 1"));   // a legújabb megbeszélés címkéje

    const PeopleOpResult r = svc()->removePerson(kGergely, false);
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.meetings, 2);
    QCOMPARE(r.samples, 1);
    QCOMPARE(r.staleSummaries, 1);
    QVERIFY(r.name.isEmpty());
    QVERIFY(!svc()->exists(kGergely));
    QCOMPARE(m_app->voiceprints()->totalPrintCount(), 0);
    QVERIFY(m_app->store()->load(a.id).speakerMap.isEmpty());       // névtelen beszélő marad
    QVERIFY(m_app->summaryStale(a.id).stale);
    QVERIFY(!m_app->summaryStale(b.id).stale);
    QVERIFY(PersonDetailsStore(metaFile("people-details.json")).details(kGergely).isEmpty());
    QVERIFY(!svc()->canUndo());                                      // a törlés nem vonható vissza
    QVERIFY(!svc()->removePerson(kGergely, false).ok);
}

void PeopleServiceTest::deleteKeepingSamplesAsAnonymous()
{
    const Meeting a = addMeeting("Egyik", QDateTime(QDate(2026, 9, 20), QTime(10, 0)),
                                 {{0, 4000, "Beszélő 1"}}, {{"Beszélő 1", kGergely}}, false);
    m_app->voiceprints()->addPrint(kGergely, print("a1", {1, 0, 0}, a.id));
    m_app->voiceprints()->addPrint(kGergely, print("a2", {0, 1, 0}, a.id));
    QVERIFY(svc()->addPerson("Névtelen 1").ok);       // foglalt név: a következő szabad kell

    const PeopleOpResult r = svc()->removePerson(kGergely, true);
    QVERIFY(r.ok);
    QCOMPARE(r.name, QStringLiteral("Névtelen 2"));
    QVERIFY(!svc()->exists(kGergely));
    QVERIFY(svc()->exists("Névtelen 2"));
    QCOMPARE(svc()->person("Névtelen 2").sampleCount, 2);
    QVERIFY(PeopleStore(metaFile("people.json")).names().contains("Névtelen 2"));
    // A megbeszélésen névtelen beszélő marad — a névtelen SZEMÉLY nem kerül rá.
    QVERIFY(m_app->store()->load(a.id).speakerMap.isEmpty());

    // Minta nélkül nincs mit megtartani: nem jön létre névtelen személy.
    QVERIFY(svc()->addPerson(kEszter).ok);
    const PeopleOpResult none = svc()->removePerson(kEszter, true);
    QVERIFY(none.ok);
    QVERIFY(none.name.isEmpty());
    QVERIFY(!svc()->exists("Névtelen 3"));
}

// ---- hanglenyomat a kézzel hozzárendelt megbeszélésekből -------------------------

void PeopleServiceTest::voiceprintFromManuallyAssignedMeetings()
{
    const QDateTime d(QDate(2026, 9, 3), QTime(10, 0));
    // Elég anyag: 4 × 5 mp biztos sor.
    const Meeting rich = addMeeting("Kickoff", d,
        {{0, 5000, "Beszélő 1"}, {6000, 5000, "Beszélő 1"}, {12000, 5000, "Beszélő 1"}, {18000, 5000, "Beszélő 1"},
         {24000, 5000, "Beszélő 2"}}, {{"Beszélő 1", kEszter}}, false);
    // Kevés anyag: egyetlen 4 mp-es sor.
    const Meeting poor = addMeeting("Rövid", d.addDays(3), {{0, 4000, "Beszélő 1"}}, {{"Beszélő 1", kEszter}}, false);
    m_app->peopleStats()->refreshNow();

    // Hang-modell nélkül: a terv megmondja, hogy nem megy.
    VoiceprintPlan plan = svc()->voiceprintPlan(kEszter);
    QVERIFY(!plan.modelAvailable);
    QVERIFY(!svc()->createVoiceprintFromMeeting(kEszter, rich.id).ok);

    svc()->setEmbedderFactory([] { return std::make_unique<FakeEmbedder>(); });
    plan = svc()->voiceprintPlan(kEszter);
    QVERIFY(plan.modelAvailable);
    QCOMPARE(plan.meetingsWithLines, 2);
    QCOMPARE(plan.usableMeetingIds, QStringList{rich.id});
    QCOMPARE(plan.usableMs, qint64(20000));

    QSignalSpy prints(m_app.get(), &AppController::voiceprintsChanged);
    const VoiceprintResult bad = svc()->createVoiceprintFromMeeting(kEszter, poor.id);
    QVERIFY(!bad.ok);
    QVERIFY(!bad.error.isEmpty());
    const VoiceprintResult ok = svc()->createVoiceprintFromMeeting(kEszter, rich.id);
    QVERIFY2(ok.ok, qPrintable(ok.error));
    QCOMPARE(ok.usedMs, qint64(20000));
    QCOMPARE(prints.count(), 1);
    const QVector<Voiceprint> made = m_app->voiceprints()->printsFor(kEszter);
    QCOMPARE(made.size(), 1);
    QCOMPARE(made.first().sourceMeetingId, rich.id);
    // Akinek egy megbeszélésen sincs sora: üres terv.
    QVERIFY(svc()->addPerson(kGergo).ok);
    QCOMPARE(svc()->voiceprintPlan(kGergo).meetingsWithLines, 0);
}

void PeopleServiceTest::sampleSourceDescription()
{
    Meeting m = addMeeting("Partnerdemó – adatimport", QDateTime(QDate(2026, 9, 24), QTime(10, 0)),
                           {{0, 4000, "Beszélő 1"}}, {{"Beszélő 1", kGergely}}, false);
    Track mic;
    mic.id = "mic"; mic.kind = TrackKind::Mic; mic.file = "track_mic.ogg"; mic.deviceName = "Trust USB mikrofon";
    Track loop;
    loop.id = "loop"; loop.kind = TrackKind::Loopback; loop.file = "track_loop.ogg";
    loop.deviceName = "Monitor of Headset Communication";
    m.tracks = {mic, loop};
    m_app->store()->saveMeeting(m);
    writeFile(QDir(m.folder).filePath("track_mic.ogg"), "x");

    Voiceprint a = print("s-mic", {1, 0, 0}, m.id);
    a.sourceTrack = "mic"; a.sampleRef = "track_mic.ogg#2000-44000";
    Voiceprint b = print("s-call", {1, 0, 0}, m.id);
    b.sourceTrack = "loop"; b.sampleRef = "track_loop.ogg#0-38000";
    Voiceprint c = print("s-mix", {1, 0, 0}, m.id);
    Voiceprint e = print("s-gone", {1, 0, 0}, "torolt-megbeszeles");
    e.sourceTrack.clear(); e.createdAt = "2026-08-28T09:00:00";
    for (const Voiceprint& p : {a, b, c, e}) m_app->voiceprints()->addPrint(kGergely, p);

    const QVector<VoiceSample> samples = svc()->samples(kGergely);
    QCOMPARE(samples.size(), 4);
    auto byId = [&](const QString& id) {
        for (const VoiceSample& s : samples) if (s.id == id) return s;
        return VoiceSample();
    };
    const VoiceSample sm = byId("s-mic");
    QCOMPARE(sm.sourceKind, QStringLiteral("mic"));
    QCOMPARE(sm.deviceLabel, QStringLiteral("Trust USB mikrofon"));
    QCOMPARE(sm.meetingTitle, QStringLiteral("Partnerdemó – adatimport"));
    QCOMPARE(sm.recordedAt, QDate(2026, 9, 24));
    QCOMPARE(sm.durationMs(), qint64(42000));
    QVERIFY(sm.audioExists);
    QCOMPARE(sm.audioPath, QDir(m.folder).filePath("track_mic.ogg"));
    QCOMPARE(byId("s-call").sourceKind, QStringLiteral("call"));
    QVERIFY(!byId("s-call").audioExists);
    QCOMPARE(byId("s-mix").sourceKind, QStringLiteral("mix"));
    const VoiceSample gone = byId("s-gone");
    QCOMPARE(gone.sourceKind, QStringLiteral("unknown"));
    QVERIFY(gone.meetingTitle.isEmpty());
    QCOMPARE(gone.recordedAt, QDate(2026, 8, 28));     // a megbeszélés híján a minta készítésének napja
    QCOMPARE(samples.last().id, QStringLiteral("s-gone"));   // legújabb elöl
}

// ---- két folyamat ------------------------------------------------------------------

void PeopleServiceTest::twoProcessesWriteConcurrently()
{
    // Egy MÁSIK folyamat (ez a teszt-exe gyerek-módban) ugyanabba a három fájlba ír, miközben
    // mi is írunk: a végén mindkét fél minden változásának meg kell lennie.
    const QString meta = m_app->store()->metadataDir();
    QProcess child;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("TANARA_PEOPLE_CHILD", meta);
    child.setProcessEnvironment(env);
    child.start(QCoreApplication::applicationFilePath(), QStringList());
    QVERIFY(child.waitForStarted(10000));

    const int n = 40;
    for (int i = 0; i < n; ++i) {
        QVERIFY(svc()->addPerson(QStringLiteral("Szülő %1").arg(i)).ok);
        QVERIFY(svc()->addAlias(kGergely, QStringLiteral("szülő-becenév %1").arg(i)).ok);
        m_app->voiceprints()->addPrint("Szülő", print(QStringLiteral("parent-%1").arg(i), {1, 0, 0}));
    }
    QVERIFY(child.waitForFinished(60000));
    QCOMPARE(child.exitCode(), 0);

    svc()->reload();
    const QStringList names = PeopleStore(QDir(meta).filePath("people.json")).names();
    const QStringList aliases = PersonDetailsStore(QDir(meta).filePath("people-details.json")).aliases(kGergely);
    const VoiceprintStore prints(QDir(meta).filePath("voiceprints.json"));
    for (int i = 0; i < n; ++i) {
        QVERIFY2(names.contains(QStringLiteral("Szülő %1").arg(i)), qPrintable(QString::number(i)));
        QVERIFY2(names.contains(QStringLiteral("Gyerek %1").arg(i)), qPrintable(QString::number(i)));
        QVERIFY2(aliases.contains(QStringLiteral("szülő-becenév %1").arg(i)), qPrintable(QString::number(i)));
        QVERIFY2(aliases.contains(QStringLiteral("gyerek-becenév %1").arg(i)), qPrintable(QString::number(i)));
    }
    QCOMPARE(prints.printCount("Szülő"), n);
    QCOMPARE(prints.printCount("Gyerek"), n);
    // A futó példány is látja a másik folyamat adatait.
    QVERIFY(svc()->exists("Gyerek 0"));
    QCOMPARE(svc()->person(kGergely).aliases.size(), 2 * n);
    QCOMPARE(svc()->person("Gyerek").sampleCount, n);
    // Zár-fájl nem marad.
    QVERIFY(QDir(meta).entryList({"*.lock"}, QDir::Files | QDir::Hidden).isEmpty());
}

// A gyerek-folyamat: a megadott metaadat-mappa három fájljába ír (tárolókon át, ahogy egy
// másik Tanara-folyamat tenné).
static int runChild(const QString& meta)
{
    PeopleStore people(QDir(meta).filePath("people.json"));
    PersonDetailsStore details(QDir(meta).filePath("people-details.json"));
    VoiceprintStore prints(QDir(meta).filePath("voiceprints.json"));
    for (int i = 0; i < 40; ++i) {
        people.add(QStringLiteral("Gyerek %1").arg(i));
        if (!details.addAlias(kGergely, QStringLiteral("gyerek-becenév %1").arg(i))) return 2;
        prints.addPrint("Gyerek", print(QStringLiteral("child-%1").arg(i), {0, 1, 0}));
    }
    return 0;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString childMeta = qEnvironmentVariable("TANARA_PEOPLE_CHILD");
    if (!childMeta.isEmpty()) return runChild(childMeta);
    PeopleServiceTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_people_service.moc"
