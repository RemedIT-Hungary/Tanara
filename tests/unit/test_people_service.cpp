//
// A Személyek ablak hátterének tesztjei: a people.json rekordjai (név, becenevek,
// megjegyzés), az egyszeri átállás a régi alakról (névlista + people-details.json),
// becenév-keresés a személyválasztóban, statisztika (megbeszélés-szám, beszédidő, utoljára látva),
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
#include "tanara/store/VoiceprintStore.h"

#include <memory>
#include <vector>

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

    void recordRoundTripKeepsUnknownFields();
    void corruptPeopleFileIsSetAside();
    void unlistKeepsDetailsAndAddReattaches();
    void migrateOldListOnly();
    void migrateOldListWithDetails();
    void migrateOrphanDetailsSurfaceWhenNameReturns();
    void migrateCorruptDetailsIsSetAside();
    void alreadyNewFileIsLeftAlone();
    void failedMigrationLeavesOldFilesUntouched();
    void twoProcessesMigrateAtOnce();
    void oldFilesMigrateUnderTheApp();
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

// ---- PeopleStore: rekordok ---------------------------------------------------

namespace {

const char* kOldList = R"({"people":["B. Gergő","Bárány Gergely","Kovács Lilla"]})";
const char* kOldDetails = R"({"version":1,"people":[
    {"name":"Bárány Gergely","aliases":["Gergely","Főnök"],"note":"PM","color":"blue"},
    {"name":"kovács lilla","aliases":[],"note":"én"}]})";

QJsonObject readJson(const QString& path)
{
    return QJsonDocument::fromJson(readFile(path)).object();
}

// A "people" tömb egy rekordja név szerint (üres objektum, ha nincs).
QJsonObject recordOf(const QJsonObject& root, const QString& name, const char* array = "people")
{
    for (const QJsonValue& v : root.value(QLatin1String(array)).toArray())
        if (v.toObject().value("name").toString() == name) return v.toObject();
    return {};
}

} // namespace

void PeopleServiceTest::recordRoundTripKeepsUnknownFields()
{
    const QString path = m_home->filePath("d/people.json");
    // Egy JÖVŐBELI build mezői (gyökérben és személynél) nem veszhetnek el.
    writeFile(path, R"({"version":2,"futureRoot":7,"people":[
        {"name":"Bárány Gergely","aliases":["Gergely"],"note":"PM","color":"blue"},
        {"name":"Tóth Eszter","aliases":[],"note":""}]})");
    {
        PeopleStore store(path);
        QVERIFY(!store.migrationPending());
        QCOMPARE(store.names(), QStringList({kGergely, kEszter}));
        QCOMPARE(store.aliases(kGergely), QStringList{"Gergely"});
        QCOMPARE(store.details("bárány gergely").note, QStringLiteral("PM"));   // kisbetű-független
        QVERIFY(store.addAlias(kGergely, "G. Bárány"));
        QVERIFY(!store.addAlias(kGergely, "gergely"));        // már szerepel
        QVERIFY(!store.addAlias(kGergely, kGergely));         // a saját neve nem becenév
        store.setNote(kEszter, "Pénzügy");
    }
    PeopleStore fresh(path);
    QCOMPARE(fresh.aliases(kGergely), QStringList({"Gergely", "G. Bárány"}));
    QCOMPARE(fresh.details(kEszter).note, QStringLiteral("Pénzügy"));
    QJsonObject root = readJson(path);
    QCOMPARE(root.value("version").toInt(), 2);
    QCOMPARE(root.value("futureRoot").toInt(), 7);
    QCOMPARE(recordOf(root, kGergely).value("color").toString(), QStringLiteral("blue"));
    QVERIFY(!root.contains("unlisted"));

    // Átnevezés létező névre = egyesítés: a rekord EGYBEN megy — a régi név becenév lesz, a
    // megjegyzések és az ismeretlen mezők megmaradnak, a régi néven nem marad semmi.
    fresh.rename(kGergely, kEszter, true);
    QCOMPARE(fresh.names(), QStringList{kEszter});
    QCOMPARE(fresh.aliases(kEszter), QStringList({"Gergely", "G. Bárány", kGergely}));
    QCOMPARE(fresh.details(kEszter).note, QStringLiteral("Pénzügy\nPM"));
    QVERIFY(fresh.details(kGergely).isEmpty());
    root = readJson(path);
    QCOMPARE(root.value("people").toArray().size(), 1);
    QCOMPARE(recordOf(root, kEszter).value("color").toString(), QStringLiteral("blue"));
    // Sima átnevezés: a rekord az új néven, a régi név csak kérésre lesz becenév.
    fresh.rename(kEszter, "Tóth Eszti");
    QCOMPARE(PeopleStore(path).names(), QStringList{"Tóth Eszti"});
    QCOMPARE(PeopleStore(path).aliases("Tóth Eszti"), QStringList({"Gergely", "G. Bárány", kGergely}));
    // Törlés: a teljes rekord megszűnik.
    fresh.remove("Tóth Eszti");
    QVERIFY(PeopleStore(path).names().isEmpty());
    QVERIFY(PeopleStore(path).details("Tóth Eszti").isEmpty());
}

void PeopleServiceTest::corruptPeopleFileIsSetAside()
{
    const QString path = m_home->filePath("d/people.json");
    writeFile(path, R"({"version":2,"people":[{"name":"X","alia)");
    const QByteArray before = readFile(path);
    PeopleStore store(path);
    QCOMPARE(readFile(path), before);                       // betöltéstől nem változik
    QVERIFY(store.addAlias(kGergely, "Gergő"));
    QCOMPARE(QDir(m_home->filePath("d")).entryList({"people.json.corrupt-*"}, QDir::Files).size(), 1);
    QCOMPARE(PeopleStore(path).aliases(kGergely), QStringList{"Gergő"});
}

void PeopleServiceTest::unlistKeepsDetailsAndAddReattaches()
{
    const QString path = m_home->filePath("d/people.json");
    PeopleStore store(path);
    // Listán nem szereplő név (pl. csak hanglenyomata van) is kaphat becenevet: listán kívüli
    // rekord lesz, a névlistát nem bővíti.
    QVERIFY(store.addAlias(kGergely, "Gergő"));
    QVERIFY(store.names().isEmpty());
    QCOMPARE(store.unlistedNames(), QStringList{kGergely});
    QCOMPARE(recordOf(readJson(path), kGergely, "unlisted").value("aliases").toArray().size(), 1);
    // Ha a név felkerül a listára, a rekord vele megy.
    store.add("bárány gergely");
    QCOMPARE(store.names(), QStringList{kGergely});
    QVERIFY(store.unlistedNames().isEmpty());
    QVERIFY(!readJson(path).contains("unlisted"));
    // Levétel a listáról: az adatok megmaradnak; adat nélküli rekord nem marad a fájlban.
    store.add(kEszter);
    store.unlist(kGergely);
    store.unlist(kEszter);
    QVERIFY(store.names().isEmpty());
    PeopleStore fresh(path);
    QCOMPARE(fresh.unlistedNames(), QStringList{kGergely});
    QCOMPARE(fresh.aliases(kGergely), QStringList{"Gergő"});
    QCOMPARE(readJson(path).value("unlisted").toArray().size(), 1);
}

// ---- egyszeri átállás a régi alakról -----------------------------------------

void PeopleServiceTest::migrateOldListOnly()
{
    const QString dir = m_home->filePath("d");
    const QString path = QDir(dir).filePath("people.json");
    writeFile(path, kOldList);
    PeopleStore store(path);
    QVERIFY(!store.migrationPending());
    QCOMPARE(store.names(), QStringList({kGergo, kGergely, kSelf}));
    // A betöltés maga átírta a fájlt az új alakra.
    const QJsonObject root = readJson(path);
    QCOMPARE(root.keys(), QStringList({"people", "version"}));
    QCOMPARE(root.value("version").toInt(), 2);
    const QJsonArray arr = root.value("people").toArray();
    QCOMPARE(arr.size(), 3);
    for (const QJsonValue& v : arr) {
        QCOMPARE(v.toObject().keys(), QStringList({"aliases", "name", "note"}));
        QVERIFY(v.toObject().value("aliases").toArray().isEmpty());
        QVERIFY(v.toObject().value("note").toString().isEmpty());
    }
    QCOMPARE(arr.at(1).toObject().value("name").toString(), kGergely);     // a sorrend megmaradt
    QCOMPARE(QDir(dir).entryList(QDir::Files | QDir::Hidden), QStringList{"people.json"});
}

void PeopleServiceTest::migrateOldListWithDetails()
{
    const QString dir = m_home->filePath("d");
    const QString path = QDir(dir).filePath("people.json");
    writeFile(path, kOldList);
    writeFile(QDir(dir).filePath("people-details.json"), kOldDetails);
    {
        PeopleStore store(path);
        QVERIFY(!store.migrationPending());
        QCOMPARE(store.names(), QStringList({kGergo, kGergely, kSelf}));
        QCOMPARE(store.aliases(kGergely), QStringList({"Gergely", "Főnök"}));
        QCOMPARE(store.details(kGergely).note, QStringLiteral("PM"));
        QCOMPARE(store.details(kSelf).note, QStringLiteral("én"));     // kisbetű-független egyezés
        QVERIFY(store.unlistedNames().isEmpty());
    }
    // A details-fájl eltűnt, minden adata az új people.json-ban van (az ismeretlen mező is).
    QCOMPARE(QDir(dir).entryList(QDir::Files | QDir::Hidden), QStringList{"people.json"});
    const QJsonObject root = readJson(path);
    QCOMPARE(root.value("version").toInt(), 2);
    QCOMPARE(root.value("people").toArray().size(), 3);
    const QJsonObject g = recordOf(root, kGergely);
    QCOMPARE(g.value("aliases").toArray(), QJsonArray({"Gergely", "Főnök"}));
    QCOMPARE(g.value("note").toString(), QStringLiteral("PM"));
    QCOMPARE(g.value("color").toString(), QStringLiteral("blue"));
    QCOMPARE(recordOf(root, kSelf).value("note").toString(), QStringLiteral("én"));   // a lista írásmódja marad
    // Újabb betöltés már nem ír.
    const QByteArray after = readFile(path);
    const FileStamp stamp = FileStamp::of(path);
    PeopleStore again(path);
    QCOMPARE(again.aliases(kGergely), QStringList({"Gergely", "Főnök"}));
    QCOMPARE(readFile(path), after);
    QVERIFY(FileStamp::of(path) == stamp);
}

void PeopleServiceTest::migrateOrphanDetailsSurfaceWhenNameReturns()
{
    // A details-fájlban olyan név is van, amely már nincs a névlistán (kívülről átnevezték).
    const QString dir = m_home->filePath("d");
    const QString path = QDir(dir).filePath("people.json");
    writeFile(path, R"({"people":["Bárány G.","Kovács Lilla"]})");
    writeFile(QDir(dir).filePath("people-details.json"), kOldDetails);
    PeopleStore store(path);
    QCOMPARE(store.names(), QStringList({"Bárány G.", kSelf}));     // az árva név nem kerül a listára
    QCOMPARE(store.unlistedNames(), QStringList{kGergely});
    QVERIFY(store.aliases("Bárány G.").isEmpty());
    // …de nem veszett el: a fájlban listán kívüli rekordként megvan,
    QVERIFY(!QFile::exists(QDir(dir).filePath("people-details.json")));
    const QJsonObject orphan = recordOf(readJson(path), kGergely, "unlisted");
    QCOMPARE(orphan.value("aliases").toArray(), QJsonArray({"Gergely", "Főnök"}));
    QCOMPARE(orphan.value("note").toString(), QStringLiteral("PM"));
    QCOMPARE(orphan.value("color").toString(), QStringLiteral("blue"));
    // név szerint lekérdezhető (pl. ha csak hanglenyomata van a névnek),
    QCOMPARE(store.aliases(kGergely), QStringList({"Gergely", "Főnök"}));
    // és ha a személy újra felkerül a listára, a rekordja vele együtt visszatér.
    PeopleStore other(path);
    other.add(kGergely);
    QCOMPARE(other.names(), QStringList({"Bárány G.", kGergely, kSelf}));
    QCOMPARE(other.details(kGergely).note, QStringLiteral("PM"));
    QVERIFY(other.unlistedNames().isEmpty());
    QVERIFY(!readJson(path).contains("unlisted"));
    QCOMPARE(recordOf(readJson(path), kGergely).value("aliases").toArray().size(), 2);
    // Átnevezéssel is visszaköthető: a kívülről átnevezett személy megkapja a régi adatait.
    store.refresh();
    store.unlist(kGergely);
    store.rename(kGergely, "Bárány G.", true);
    QCOMPARE(store.names(), QStringList({"Bárány G.", kSelf}));
    QCOMPARE(store.aliases("Bárány G."), QStringList({"Gergely", "Főnök", kGergely}));
    QVERIFY(store.unlistedNames().isEmpty());
}

void PeopleServiceTest::migrateCorruptDetailsIsSetAside()
{
    const QString dir = m_home->filePath("d");
    const QString path = QDir(dir).filePath("people.json");
    const QByteArray broken = R"({"version":1,"people":[{"name":"Bárány Gergely","alia)";
    writeFile(path, kOldList);
    writeFile(QDir(dir).filePath("people-details.json"), broken);
    PeopleStore store(path);
    // A névlista ettől még átáll; a sérült details-fájl nem törlődik, hanem félre kerül.
    QVERIFY(!store.migrationPending());
    QCOMPARE(store.names(), QStringList({kGergo, kGergely, kSelf}));
    QCOMPARE(readJson(path).value("version").toInt(), 2);
    QVERIFY(!QFile::exists(QDir(dir).filePath("people-details.json")));
    const QStringList aside = QDir(dir).entryList({"people-details.json.corrupt-*"}, QDir::Files);
    QCOMPARE(aside.size(), 1);
    QCOMPARE(readFile(QDir(dir).filePath(aside.first())), broken);
    // Újabb betöltés nem csinál semmit.
    const QByteArray after = readFile(path);
    PeopleStore again(path);
    QCOMPARE(readFile(path), after);
    QCOMPARE(QDir(dir).entryList(QDir::Files | QDir::Hidden).size(), 2);
}

void PeopleServiceTest::alreadyNewFileIsLeftAlone()
{
    const QString dir = m_home->filePath("d");
    const QString path = QDir(dir).filePath("people.json");
    const QByteArray v2 = R"({"version":2,"people":[{"name":"Bárány Gergely","aliases":["Gergő"],"note":""}]})";
    writeFile(path, v2);
    {
        PeopleStore store(path);
        QVERIFY(!store.migrationPending());
        QCOMPARE(store.aliases(kGergely), QStringList{"Gergő"});
    }
    QCOMPARE(readFile(path), v2);                                           // bájtra ugyanaz
    QCOMPARE(QDir(dir).entryList(QDir::Files | QDir::Hidden), QStringList{"people.json"});

    // Új alak MELLETT maradt details-fájl (egy korábbi átállás a törlés előtt megszakadt):
    // nem olvad be újra (nem támaszt fel azóta törölt becenevet), és nem is törlődik — félre kerül.
    writeFile(QDir(dir).filePath("people-details.json"), kOldDetails);
    PeopleStore store(path);
    QCOMPARE(store.aliases(kGergely), QStringList{"Gergő"});
    QCOMPARE(readFile(path), v2);
    QVERIFY(!QFile::exists(QDir(dir).filePath("people-details.json")));
    const QStringList aside = QDir(dir).entryList({"people-details.json.leftover-*"}, QDir::Files);
    QCOMPARE(aside.size(), 1);
    QCOMPARE(readFile(QDir(dir).filePath(aside.first())), QByteArray(kOldDetails));
}

void PeopleServiceTest::failedMigrationLeavesOldFilesUntouched()
{
#ifndef Q_OS_UNIX
    QSKIP("Az írásvédett mappa csak Unixon állítható elő megbízhatóan.");
#else
    const QString dir = m_home->filePath("d");
    const QString path = QDir(dir).filePath("people.json");
    const QString detailsPath = QDir(dir).filePath("people-details.json");
    writeFile(path, kOldList);
    writeFile(detailsPath, kOldDetails);
    const QFile::Permissions rw = QFile::permissions(dir);
    QVERIFY(QFile::setPermissions(dir, QFile::ReadOwner | QFile::ExeOwner));   // nem írható mappa
    struct Restore {
        QString dir; QFile::Permissions p;
        ~Restore() { QFile::setPermissions(dir, p); }
    } restore{dir, rw};
    if (QFile(QDir(dir).filePath("probe")).open(QIODevice::WriteOnly))
        QSKIP("A mappa írásvédelme nem érvényesül (root?).");

    PeopleStore store(path);
    // Az átállás nem sikerült: mindkét régi fájl bájtra ugyanaz, semmi más nem keletkezett…
    QVERIFY(store.migrationPending());
    QCOMPARE(readFile(path), QByteArray(kOldList));
    QCOMPARE(readFile(detailsPath), QByteArray(kOldDetails));
    QCOMPARE(QDir(dir).entryList(QDir::Files | QDir::Hidden), QStringList({"people-details.json", "people.json"}));
    // …a tár pedig a memóriában a teljes (egyesített) adattal dolgozik tovább.
    QCOMPARE(store.names(), QStringList({kGergo, kGergely, kSelf}));
    QCOMPARE(store.aliases(kGergely), QStringList({"Gergely", "Főnök"}));
    store.add("Új Ember");
    QVERIFY(store.addAlias(kGergely, "Geri"));
    QVERIFY(store.names().contains("Új Ember"));
    QVERIFY(store.migrationPending());
    QCOMPARE(readFile(path), QByteArray(kOldList));
    QCOMPARE(readFile(detailsPath), QByteArray(kOldDetails));

    // Amint a mappa újra írható, a következő mentés befejezi az átállást (a memóriában
    // közben felgyűlt változásokkal), és csak EKKOR törli a details-fájlt.
    QVERIFY(QFile::setPermissions(dir, rw));
    store.setNote(kGergo, "másik Gergő");
    QVERIFY(!store.migrationPending());
    QCOMPARE(QDir(dir).entryList(QDir::Files | QDir::Hidden), QStringList{"people.json"});
    PeopleStore fresh(path);
    QCOMPARE(fresh.names(), QStringList({kGergo, kGergely, kSelf, "Új Ember"}));
    QCOMPARE(fresh.aliases(kGergely), QStringList({"Gergely", "Főnök", "Geri"}));
    QCOMPARE(fresh.details(kGergo).note, QStringLiteral("másik Gergő"));
#endif
}

void PeopleServiceTest::twoProcessesMigrateAtOnce()
{
    // Több FOLYAMAT (ez a teszt-exe gyerek-módban) egyszerre indul ugyanarra a régi alakú
    // fájl-párra: mind megpróbál átállni, és utána rögtön ír is. A végén egyetlen, ép, új
    // alakú fájl van, a details adatai pontosan egyszer, és minden folyamat írása megvan.
    const QString dir = m_home->filePath("d");
    const QString path = QDir(dir).filePath("people.json");
    for (int round = 0; round < 3; ++round) {
        QDir(dir).removeRecursively();
        writeFile(path, kOldList);
        writeFile(QDir(dir).filePath("people-details.json"), kOldDetails);
        const int n = 6;
        std::vector<std::unique_ptr<QProcess>> children;
        for (int i = 0; i < n; ++i) {
            auto child = std::make_unique<QProcess>();
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert("TANARA_PEOPLE_MIGRATE_CHILD", path);
            env.insert("TANARA_PEOPLE_CHILD_NO", QString::number(i));
            child->setProcessEnvironment(env);
            child->start(QCoreApplication::applicationFilePath(), QStringList());
            children.push_back(std::move(child));
        }
        for (auto& child : children) {
            QVERIFY(child->waitForFinished(60000));
            QCOMPARE(child->exitStatus(), QProcess::NormalExit);
            QCOMPARE(child->exitCode(), 0);
        }
        // Csak a people.json maradt: se details, se félretett, se zár-fájl.
        QCOMPARE(QDir(dir).entryList(QDir::Files | QDir::Hidden), QStringList{"people.json"});
        const QJsonObject root = readJson(path);
        QCOMPARE(root.value("version").toInt(), 2);
        QVERIFY(!root.contains("unlisted"));
        PeopleStore store(path);
        QCOMPARE(store.names().size(), 3 + n);
        for (int i = 0; i < n; ++i) {
            QVERIFY2(store.names().contains(QStringLiteral("Gyerek %1").arg(i)), qPrintable(QString::number(i)));
            QVERIFY2(store.aliases(kGergely).contains(QStringLiteral("gyerek-becenév %1").arg(i)),
                     qPrintable(QString::number(i)));
        }
        QCOMPARE(store.aliases(kGergely).size(), 2 + n);                 // a régi kettő egyszer
        QCOMPARE(store.aliases(kGergely).mid(0, 2), QStringList({"Gergely", "Főnök"}));
        QCOMPARE(store.details(kGergely).note, QStringLiteral("PM"));    // nem duplázódott
        QCOMPARE(recordOf(root, kGergely).value("color").toString(), QStringLiteral("blue"));
    }
}

void PeopleServiceTest::oldFilesMigrateUnderTheApp()
{
    // RÉGI formátumú fájlok (ahogy egy korábbi build írta) → az alkalmazás indulása átállítja
    // a people.json-t; a voiceprints.json alakja VÁLTOZATLAN (névhez kötött lenyomatok).
    m_app.reset();
    const QString home = m_home->path();
    writeFile(QDir(home).filePath("people.json"), kOldList);
    writeFile(QDir(home).filePath("people-details.json"), kOldDetails);
    writeFile(QDir(home).filePath("voiceprints.json"),
              R"({"people":[{"name":"Bárány Gergely","prints":[{"id":"p1","embedding":[1,0,0],"dim":3,
                 "sourceMeetingId":"","sourceTrack":"mic","device":"USB","sampleRef":"track_mic.ogg#0-4000",
                 "createdAt":"2026-09-01T10:00:00"}]}]})");
    m_app = std::make_unique<AppController>();
    QVERIFY(!QFile::exists(metaFile("people-details.json")));
    QCOMPARE(readJson(metaFile("people.json")).value("version").toInt(), 2);
    m_app->setUserSpeakerName(kSelf);
    QCOMPARE(svc()->persons().size(), 3);
    QCOMPARE(svc()->person(kGergely).sampleCount, 1);
    QCOMPARE(svc()->person(kGergely).aliases, QStringList({"Gergely", "Főnök"}));
    QCOMPARE(svc()->person(kGergely).note, QStringLiteral("PM"));
    // A személyválasztó is látja az átvett beceneveket.
    const QVector<PersonInfo> hits = filterPeople(m_app->peopleDirectory(), "fonok");
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first().name, kGergely);

    svc()->setNote(kGergely, "Projektvezető");
    QVERIFY(svc()->addPerson(kEszter).ok);
    QVERIFY(svc()->addAlias(kEszter, "Eszti néni").ok);
    QVERIFY(svc()->moveSample("p1", kEszter).ok);
    QVERIFY(svc()->merge(kGergo, kGergely).ok);
    QVERIFY(svc()->renamePerson(kEszter, "Tóth Eszti").ok);

    // Az átnevezés a rekordot egyben vitte: az új néven minden megvan, a régin semmi.
    const QJsonObject people = readJson(metaFile("people.json"));
    QCOMPARE(people.keys(), QStringList({"people", "version"}));
    QCOMPARE(recordOf(people, "Tóth Eszti").value("aliases").toArray(), QJsonArray({"Eszti néni", kEszter}));
    QVERIFY(recordOf(people, kEszter).isEmpty());
    QCOMPARE(recordOf(people, kGergely).value("aliases").toArray(), QJsonArray({"Gergely", "Főnök", kGergo}));
    QCOMPARE(recordOf(people, kGergely).value("note").toString(), QStringLiteral("Projektvezető"));
    QCOMPARE(recordOf(people, kGergely).value("color").toString(), QStringLiteral("blue"));
    QCOMPARE(PeopleStore(metaFile("people.json")).names(), QStringList({kGergely, kSelf, "Tóth Eszti"}));
    QVERIFY(!QFile::exists(metaFile("people-details.json")));

    const QJsonObject prints = readJson(metaFile("voiceprints.json"));
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
    QCOMPARE(PeopleStore(metaFile("people.json")).details(kGergely).note,
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
    QVERIFY(PeopleStore(metaFile("people.json")).details(kGergely).isEmpty());
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
    // Egy MÁSIK folyamat (ez a teszt-exe gyerek-módban) ugyanabba a két fájlba ír, miközben
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
    const QStringList aliases = PeopleStore(QDir(meta).filePath("people.json")).aliases(kGergely);
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

// A gyerek-folyamat: a megadott metaadat-mappa két fájljába ír (tárolókon át, ahogy egy
// másik Tanara-folyamat tenné).
static int runChild(const QString& meta)
{
    PeopleStore people(QDir(meta).filePath("people.json"));
    VoiceprintStore prints(QDir(meta).filePath("voiceprints.json"));
    for (int i = 0; i < 40; ++i) {
        people.add(QStringLiteral("Gyerek %1").arg(i));
        if (!people.addAlias(kGergely, QStringLiteral("gyerek-becenév %1").arg(i))) return 2;
        prints.addPrint("Gyerek", print(QStringLiteral("child-%1").arg(i), {0, 1, 0}));
    }
    return 0;
}

// A gyerek-folyamat az átállás-versenyhez: megnyitja a (régi alakú) tárat — ez maga az
// átállás —, majd rögtön ír is bele.
static int runMigrateChild(const QString& path, const QString& no)
{
    PeopleStore people(path);
    people.add(QStringLiteral("Gyerek %1").arg(no));
    if (!people.addAlias(kGergely, QStringLiteral("gyerek-becenév %1").arg(no))) return 2;
    return people.migrationPending() ? 3 : 0;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString migratePath = qEnvironmentVariable("TANARA_PEOPLE_MIGRATE_CHILD");
    if (!migratePath.isEmpty())
        return runMigrateChild(migratePath, qEnvironmentVariable("TANARA_PEOPLE_CHILD_NO"));
    const QString childMeta = qEnvironmentVariable("TANARA_PEOPLE_CHILD");
    if (!childMeta.isEmpty()) return runChild(childMeta);
    PeopleServiceTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_people_service.moc"
