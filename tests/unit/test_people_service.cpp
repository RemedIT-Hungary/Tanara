// Tests for the People window's backend: people.json's records (name, nicknames, note), the
// one-time migration from the old form (name list + people-details.json), nickname search in
// the person picker, statistics (meeting count, talk time, last seen), rename / merge /
// delete / sample move / new person from a sample with their numbers, marking summaries
// stale, undo, and two PROCESSES writing at the same time.
//
// Everything runs in a temporary TANARA_HOME with made-up names; it touches no real data,
// model or ffmpeg (the "voice" is a fake embedder).
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "tanara/app/AppController.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/editing/session/PeopleDirectory.h"
#include "tanara/editing/session/SpeakerEditor.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/editing/review/SideAnalysis.h"
#include "tanara/library/people/PeopleService.h"
#include "tanara/library/people/PeopleStats.h"
#include "tanara/library/store/MeetingStore.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/VoiceprintStore.h"

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

// The "voice" is known from the raw label: the fake embedder gives a vector for the label of
// the speaker in the middle of the slice (Speaker 1 is x, Speaker 2 is y, other is z).
class FakeEmbedder : public processing::IUtteranceEmbedder {
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
    void learnedDefaultSide();
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
    // A transcribed meeting: lines with raw labels, speakerMap, optionally with a summary.
    Meeting addMeeting(const QString& title, const QDateTime& when, const QVector<Line>& lines,
                       const QMap<QString, QString>& speakerMap, bool hasSummary);
    library::PeopleService* svc() const { return m_app->peopleService(); }

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<app::AppController> m_app;
};

void PeopleServiceTest::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    qputenv("TANARA_HOME", m_home->path().toUtf8());
    m_app = std::make_unique<app::AppController>();
    QVERIFY(m_app->settings()->paths().primaryRecordingRoot().startsWith(m_home->path()));
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

// ---- PeopleStore: records ----

namespace {

const char* kOldList = R"({"people":["B. Gergő","Bárány Gergely","Kovács Lilla"]})";
const char* kOldDetails = R"({"version":1,"people":[
    {"name":"Bárány Gergely","aliases":["Gergely","Főnök"],"note":"PM","color":"blue"},
    {"name":"kovács lilla","aliases":[],"note":"én"}]})";

QJsonObject readJson(const QString& path)
{
    return QJsonDocument::fromJson(readFile(path)).object();
}

// A record of the "people" array by name (an empty object if none).
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
    // A FUTURE build's fields (in the root and on a person) must not be lost.
    writeFile(path, R"({"version":2,"futureRoot":7,"people":[
        {"name":"Bárány Gergely","aliases":["Gergely"],"note":"PM","color":"blue"},
        {"name":"Tóth Eszter","aliases":[],"note":""}]})");
    {
        library::PeopleStore store(path);
        QVERIFY(!store.migrationPending());
        QCOMPARE(store.names(), QStringList({kGergely, kEszter}));
        QCOMPARE(store.aliases(kGergely), QStringList{"Gergely"});
        QCOMPARE(store.details("bárány gergely").note, QStringLiteral("PM"));   // case-insensitive
        QVERIFY(store.addAlias(kGergely, "G. Bárány"));
        QVERIFY(!store.addAlias(kGergely, "gergely"));        // already listed
        QVERIFY(!store.addAlias(kGergely, kGergely));         // its own name is not a nickname
        store.setNote(kEszter, "Pénzügy");
    }
    library::PeopleStore fresh(path);
    QCOMPARE(fresh.aliases(kGergely), QStringList({"Gergely", "G. Bárány"}));
    QCOMPARE(fresh.details(kEszter).note, QStringLiteral("Pénzügy"));
    QJsonObject root = readJson(path);
    QCOMPARE(root.value("version").toInt(), 2);
    QCOMPARE(root.value("futureRoot").toInt(), 7);
    QCOMPARE(recordOf(root, kGergely).value("color").toString(), QStringLiteral("blue"));
    QVERIFY(!root.contains("unlisted"));

    // Renaming to an existing name = merge: the record goes AS ONE: the old name becomes a
    // nickname, the notes and unknown fields stay, nothing stays under the old name.
    fresh.rename(kGergely, kEszter, true);
    QCOMPARE(fresh.names(), QStringList{kEszter});
    QCOMPARE(fresh.aliases(kEszter), QStringList({"Gergely", "G. Bárány", kGergely}));
    QCOMPARE(fresh.details(kEszter).note, QStringLiteral("Pénzügy\nPM"));
    QVERIFY(fresh.details(kGergely).isEmpty());
    root = readJson(path);
    QCOMPARE(root.value("people").toArray().size(), 1);
    QCOMPARE(recordOf(root, kEszter).value("color").toString(), QStringLiteral("blue"));
    // Plain rename: the record under the new name, the old name becomes a nickname only on request.
    fresh.rename(kEszter, "Tóth Eszti");
    QCOMPARE(library::PeopleStore(path).names(), QStringList{"Tóth Eszti"});
    QCOMPARE(library::PeopleStore(path).aliases("Tóth Eszti"), QStringList({"Gergely", "G. Bárány", kGergely}));
    // Delete: the whole record goes away.
    fresh.remove("Tóth Eszti");
    QVERIFY(library::PeopleStore(path).names().isEmpty());
    QVERIFY(library::PeopleStore(path).details("Tóth Eszti").isEmpty());
}

void PeopleServiceTest::corruptPeopleFileIsSetAside()
{
    const QString path = m_home->filePath("d/people.json");
    writeFile(path, R"({"version":2,"people":[{"name":"X","alia)");
    const QByteArray before = readFile(path);
    library::PeopleStore store(path);
    QCOMPARE(readFile(path), before);                       // unchanged by loading
    QVERIFY(store.addAlias(kGergely, "Gergő"));
    QCOMPARE(QDir(m_home->filePath("d")).entryList({"people.json.corrupt-*"}, QDir::Files).size(), 1);
    QCOMPARE(library::PeopleStore(path).aliases(kGergely), QStringList{"Gergő"});
}

void PeopleServiceTest::unlistKeepsDetailsAndAddReattaches()
{
    const QString path = m_home->filePath("d/people.json");
    library::PeopleStore store(path);
    // A name not on the list (e.g. one that only has a voiceprint) can get a nickname too: it
    // becomes an off-list record, and the name list does not grow.
    QVERIFY(store.addAlias(kGergely, "Gergő"));
    QVERIFY(store.names().isEmpty());
    QCOMPARE(store.unlistedNames(), QStringList{kGergely});
    QCOMPARE(recordOf(readJson(path), kGergely, "unlisted").value("aliases").toArray().size(), 1);
    // If the name is added to the list, the record goes with it.
    store.add("bárány gergely");
    QCOMPARE(store.names(), QStringList{kGergely});
    QVERIFY(store.unlistedNames().isEmpty());
    QVERIFY(!readJson(path).contains("unlisted"));
    // Removal from the list: the data stays; a record without data does not remain in the file.
    store.add(kEszter);
    store.unlist(kGergely);
    store.unlist(kEszter);
    QVERIFY(store.names().isEmpty());
    library::PeopleStore fresh(path);
    QCOMPARE(fresh.unlistedNames(), QStringList{kGergely});
    QCOMPARE(fresh.aliases(kGergely), QStringList{"Gergő"});
    QCOMPARE(readJson(path).value("unlisted").toArray().size(), 1);
}

// ---- one-time migration from the old form ----

void PeopleServiceTest::migrateOldListOnly()
{
    const QString dir = m_home->filePath("d");
    const QString path = QDir(dir).filePath("people.json");
    writeFile(path, kOldList);
    library::PeopleStore store(path);
    QVERIFY(!store.migrationPending());
    QCOMPARE(store.names(), QStringList({kGergo, kGergely, kSelf}));
    // The load itself rewrote the file to the new form.
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
    QCOMPARE(arr.at(1).toObject().value("name").toString(), kGergely);     // the order stayed
    QCOMPARE(QDir(dir).entryList(QDir::Files | QDir::Hidden), QStringList{"people.json"});
}

void PeopleServiceTest::migrateOldListWithDetails()
{
    const QString dir = m_home->filePath("d");
    const QString path = QDir(dir).filePath("people.json");
    writeFile(path, kOldList);
    writeFile(QDir(dir).filePath("people-details.json"), kOldDetails);
    {
        library::PeopleStore store(path);
        QVERIFY(!store.migrationPending());
        QCOMPARE(store.names(), QStringList({kGergo, kGergely, kSelf}));
        QCOMPARE(store.aliases(kGergely), QStringList({"Gergely", "Főnök"}));
        QCOMPARE(store.details(kGergely).note, QStringLiteral("PM"));
        QCOMPARE(store.details(kSelf).note, QStringLiteral("én"));     // case-insensitive match
        QVERIFY(store.unlistedNames().isEmpty());
    }
    // The details file is gone, all its data is in the new people.json (the unknown field too).
    QCOMPARE(QDir(dir).entryList(QDir::Files | QDir::Hidden), QStringList{"people.json"});
    const QJsonObject root = readJson(path);
    QCOMPARE(root.value("version").toInt(), 2);
    QCOMPARE(root.value("people").toArray().size(), 3);
    const QJsonObject g = recordOf(root, kGergely);
    QCOMPARE(g.value("aliases").toArray(), QJsonArray({"Gergely", "Főnök"}));
    QCOMPARE(g.value("note").toString(), QStringLiteral("PM"));
    QCOMPARE(g.value("color").toString(), QStringLiteral("blue"));
    QCOMPARE(recordOf(root, kSelf).value("note").toString(), QStringLiteral("én"));   // the list's spelling stays
    // A further load no longer writes.
    const QByteArray after = readFile(path);
    const FileStamp stamp = FileStamp::of(path);
    library::PeopleStore again(path);
    QCOMPARE(again.aliases(kGergely), QStringList({"Gergely", "Főnök"}));
    QCOMPARE(readFile(path), after);
    QVERIFY(FileStamp::of(path) == stamp);
}

void PeopleServiceTest::migrateOrphanDetailsSurfaceWhenNameReturns()
{
    // The details file also has a name that is no longer on the name list (renamed from outside).
    const QString dir = m_home->filePath("d");
    const QString path = QDir(dir).filePath("people.json");
    writeFile(path, R"({"people":["Bárány G.","Kovács Lilla"]})");
    writeFile(QDir(dir).filePath("people-details.json"), kOldDetails);
    library::PeopleStore store(path);
    QCOMPARE(store.names(), QStringList({"Bárány G.", kSelf}));     // the orphan name does not go onto the list
    QCOMPARE(store.unlistedNames(), QStringList{kGergely});
    QVERIFY(store.aliases("Bárány G.").isEmpty());
    // ... but it is not lost: it is in the file as an off-list record,
    QVERIFY(!QFile::exists(QDir(dir).filePath("people-details.json")));
    const QJsonObject orphan = recordOf(readJson(path), kGergely, "unlisted");
    QCOMPARE(orphan.value("aliases").toArray(), QJsonArray({"Gergely", "Főnök"}));
    QCOMPARE(orphan.value("note").toString(), QStringLiteral("PM"));
    QCOMPARE(orphan.value("color").toString(), QStringLiteral("blue"));
    // can be queried by name (e.g. if the name only has a voiceprint),
    QCOMPARE(store.aliases(kGergely), QStringList({"Gergely", "Főnök"}));
    // and if the person is added back to the list, its record returns with it.
    library::PeopleStore other(path);
    other.add(kGergely);
    QCOMPARE(other.names(), QStringList({"Bárány G.", kGergely, kSelf}));
    QCOMPARE(other.details(kGergely).note, QStringLiteral("PM"));
    QVERIFY(other.unlistedNames().isEmpty());
    QVERIFY(!readJson(path).contains("unlisted"));
    QCOMPARE(recordOf(readJson(path), kGergely).value("aliases").toArray().size(), 2);
    // It can also be re-attached by renaming: a person renamed from outside gets the old data.
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
    library::PeopleStore store(path);
    // The name list still switches over; a corrupt details file is not deleted, it is set aside.
    QVERIFY(!store.migrationPending());
    QCOMPARE(store.names(), QStringList({kGergo, kGergely, kSelf}));
    QCOMPARE(readJson(path).value("version").toInt(), 2);
    QVERIFY(!QFile::exists(QDir(dir).filePath("people-details.json")));
    const QStringList aside = QDir(dir).entryList({"people-details.json.corrupt-*"}, QDir::Files);
    QCOMPARE(aside.size(), 1);
    QCOMPARE(readFile(QDir(dir).filePath(aside.first())), broken);
    // A further load does nothing.
    const QByteArray after = readFile(path);
    library::PeopleStore again(path);
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
        library::PeopleStore store(path);
        QVERIFY(!store.migrationPending());
        QCOMPARE(store.aliases(kGergely), QStringList{"Gergő"});
    }
    QCOMPARE(readFile(path), v2);                                           // byte-identical
    QCOMPARE(QDir(dir).entryList(QDir::Files | QDir::Hidden), QStringList{"people.json"});

    // A details file left NEXT TO the new form (an earlier migration was interrupted before the
    // delete): it is not merged in again (does not resurrect a nickname deleted since), and is
    // not deleted either; it is set aside.
    writeFile(QDir(dir).filePath("people-details.json"), kOldDetails);
    library::PeopleStore store(path);
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
    QVERIFY(QFile::setPermissions(dir, QFile::ReadOwner | QFile::ExeOwner));   // a non-writable folder
    struct Restore {
        QString dir; QFile::Permissions p;
        ~Restore() { QFile::setPermissions(dir, p); }
    } restore{dir, rw};
    if (QFile(QDir(dir).filePath("probe")).open(QIODevice::WriteOnly))
        QSKIP("A mappa írásvédelme nem érvényesül (root?).");

    library::PeopleStore store(path);
    // The migration did not succeed: both old files are byte-identical, nothing else was created...
    QVERIFY(store.migrationPending());
    QCOMPARE(readFile(path), QByteArray(kOldList));
    QCOMPARE(readFile(detailsPath), QByteArray(kOldDetails));
    QCOMPARE(QDir(dir).entryList(QDir::Files | QDir::Hidden), QStringList({"people-details.json", "people.json"}));
    // ... and the store keeps working in memory with the complete (merged) data.
    QCOMPARE(store.names(), QStringList({kGergo, kGergely, kSelf}));
    QCOMPARE(store.aliases(kGergely), QStringList({"Gergely", "Főnök"}));
    store.add("Új Ember");
    QVERIFY(store.addAlias(kGergely, "Geri"));
    QVERIFY(store.names().contains("Új Ember"));
    QVERIFY(store.migrationPending());
    QCOMPARE(readFile(path), QByteArray(kOldList));
    QCOMPARE(readFile(detailsPath), QByteArray(kOldDetails));

    // As soon as the folder is writable again, the next save completes the migration (with the
    // changes accumulated in memory meanwhile), and only THEN deletes the details file.
    QVERIFY(QFile::setPermissions(dir, rw));
    store.setNote(kGergo, "másik Gergő");
    QVERIFY(!store.migrationPending());
    QCOMPARE(QDir(dir).entryList(QDir::Files | QDir::Hidden), QStringList{"people.json"});
    library::PeopleStore fresh(path);
    QCOMPARE(fresh.names(), QStringList({kGergo, kGergely, kSelf, "Új Ember"}));
    QCOMPARE(fresh.aliases(kGergely), QStringList({"Gergely", "Főnök", "Geri"}));
    QCOMPARE(fresh.details(kGergo).note, QStringLiteral("másik Gergő"));
#endif
}

void PeopleServiceTest::twoProcessesMigrateAtOnce()
{
    // Several PROCESSES (this test exe in child mode) start at once on the same old-form file
    // pair: all try to migrate, then write right away. At the end there is a single, intact,
    // new-form file, the details' data exactly once, and every process's write is there.
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
        // Only people.json remained: no details, no set-aside file, no lock file.
        QCOMPARE(QDir(dir).entryList(QDir::Files | QDir::Hidden), QStringList{"people.json"});
        const QJsonObject root = readJson(path);
        QCOMPARE(root.value("version").toInt(), 2);
        QVERIFY(!root.contains("unlisted"));
        library::PeopleStore store(path);
        QCOMPARE(store.names().size(), 3 + n);
        for (int i = 0; i < n; ++i) {
            QVERIFY2(store.names().contains(QStringLiteral("Gyerek %1").arg(i)), qPrintable(QString::number(i)));
            QVERIFY2(store.aliases(kGergely).contains(QStringLiteral("gyerek-becenév %1").arg(i)),
                     qPrintable(QString::number(i)));
        }
        QCOMPARE(store.aliases(kGergely).size(), 2 + n);                 // the old two once
        QCOMPARE(store.aliases(kGergely).mid(0, 2), QStringList({"Gergely", "Főnök"}));
        QCOMPARE(store.details(kGergely).note, QStringLiteral("PM"));    // did not duplicate
        QCOMPARE(recordOf(root, kGergely).value("color").toString(), QStringLiteral("blue"));
    }
}

void PeopleServiceTest::oldFilesMigrateUnderTheApp()
{
    // OLD-format files (as an earlier build wrote them): app startup migrates people.json;
    // voiceprints.json's shape is UNCHANGED (voiceprints bound to the name).
    m_app.reset();
    const QString home = m_home->path();
    writeFile(QDir(home).filePath("people.json"), kOldList);
    writeFile(QDir(home).filePath("people-details.json"), kOldDetails);
    writeFile(QDir(home).filePath("voiceprints.json"),
              R"({"people":[{"name":"Bárány Gergely","prints":[{"id":"p1","embedding":[1,0,0],"dim":3,
                 "sourceMeetingId":"","sourceTrack":"mic","device":"USB","sampleRef":"track_mic.ogg#0-4000",
                 "createdAt":"2026-09-01T10:00:00"}]}]})");
    m_app = std::make_unique<app::AppController>();
    QVERIFY(!QFile::exists(metaFile("people-details.json")));
    QCOMPARE(readJson(metaFile("people.json")).value("version").toInt(), 2);
    m_app->setUserSpeakerName(kSelf);
    QCOMPARE(svc()->persons().size(), 3);
    QCOMPARE(svc()->person(kGergely).sampleCount, 1);
    QCOMPARE(svc()->person(kGergely).aliases, QStringList({"Gergely", "Főnök"}));
    QCOMPARE(svc()->person(kGergely).note, QStringLiteral("PM"));
    // The person picker sees the carried-over nicknames too.
    const QVector<editing::PersonInfo> hits = filterPeople(m_app->peopleDirectory(), "fonok");
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first().name, kGergely);

    svc()->setNote(kGergely, "Projektvezető");
    QVERIFY(svc()->addPerson(kEszter).ok);
    QVERIFY(svc()->addAlias(kEszter, "Eszti néni").ok);
    QVERIFY(svc()->moveSample("p1", kEszter).ok);
    QVERIFY(svc()->merge(kGergo, kGergely).ok);
    QVERIFY(svc()->renamePerson(kEszter, "Tóth Eszti").ok);

    // The rename carried the record as one: everything is under the new name, nothing under the old.
    const QJsonObject people = readJson(metaFile("people.json"));
    QCOMPARE(people.keys(), QStringList({"people", "version"}));
    QCOMPARE(recordOf(people, "Tóth Eszti").value("aliases").toArray(), QJsonArray({"Eszti néni", kEszter}));
    QVERIFY(recordOf(people, kEszter).isEmpty());
    QCOMPARE(recordOf(people, kGergely).value("aliases").toArray(), QJsonArray({"Gergely", "Főnök", kGergo}));
    QCOMPARE(recordOf(people, kGergely).value("note").toString(), QStringLiteral("Projektvezető"));
    QCOMPARE(recordOf(people, kGergely).value("color").toString(), QStringLiteral("blue"));
    QCOMPARE(library::PeopleStore(metaFile("people.json")).names(), QStringList({kGergely, kSelf, "Tóth Eszti"}));
    QVERIFY(!QFile::exists(metaFile("people-details.json")));

    const QJsonObject prints = readJson(metaFile("voiceprints.json"));
    QCOMPARE(prints.keys(), QStringList{"people"});
    const QStringList printKeys{"createdAt", "device", "dim", "embedding", "id", "model", "sampleRef",
                                "sourceMeetingId", "sourceTrack"};
    for (const QJsonValue& pv : prints.value("people").toArray()) {
        QCOMPARE(pv.toObject().keys(), QStringList({"name", "prints"}));
        for (const QJsonValue& v : pv.toObject().value("prints").toArray())
            QCOMPARE(v.toObject().keys(), printKeys);
    }
    // Every field of the sample survived the move + rename.
    const QVector<Voiceprint> moved = library::VoiceprintStore(metaFile("voiceprints.json")).printsFor("Tóth Eszti");
    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.first().id, QStringLiteral("p1"));
    QCOMPARE(moved.first().device, QStringLiteral("USB"));
    QCOMPARE(moved.first().sampleRef, QStringLiteral("track_mic.ogg#0-4000"));
}

// ---- nickname search ----

void PeopleServiceTest::aliasSearchInPersonPicker()
{
    QVERIFY(svc()->addPerson(kGergely).ok);
    QVERIFY(svc()->addPerson(kGergo).ok);
    QVERIFY(svc()->addPerson("Gál Bence").ok);
    QVERIFY(svc()->addAlias("Gál Bence", "Gergő bátyja").ok);
    QVERIFY(svc()->addAlias(kGergely, "Főnök").ok);

    const QVector<editing::PersonInfo> all = m_app->peopleDirectory();
    // Accent- and case-insensitive, name matches first, nickname-only matches after them.
    const QVector<editing::PersonInfo> hits = filterPeople(all, "GERGO");
    QCOMPARE(hits.size(), 2);
    QCOMPARE(hits[0].name, kGergo);
    QVERIFY(hits[0].matchedAlias.isEmpty());
    QCOMPARE(hits[1].name, QStringLiteral("Gál Bence"));
    QCOMPARE(hits[1].matchedAlias, QStringLiteral("Gergő bátyja"));
    const QVector<editing::PersonInfo> boss = filterPeople(all, "fonok");
    QCOMPARE(boss.size(), 1);
    QCOMPARE(boss[0].name, kGergely);
    QVERIFY(filterPeople(all, "nincs ilyen").isEmpty());
}

// ---- statistics ----

void PeopleServiceTest::statsCountMeetingsTalkTimeAndLastSeen()
{
    const QDateTime d1(QDate(2026, 9, 24), QTime(10, 0)), d2(QDate(2026, 10, 1), QTime(9, 0));
    addMeeting("Partnerdemó", d1, {{0, 4000, "Beszélő 1"}, {5000, 6000, "Beszélő 2"}, {12000, 2000, "Beszélő 1"}},
               {{"Beszélő 1", kGergely}}, false);
    const Meeting m2 = addMeeting("Heti egyeztetés", d2, {{0, 10000, "Beszélő 1"}, {11000, 3000, "Beszélő 2"}},
                                  {{"Beszélő 2", kGergely}, {"Beszélő 1", kEszter}}, true);
    // Manual fix: the second meeting's first line is really Gergely's (per-line override).
    {
        editing::SpeakerEditor ed(m_app->store(), nullptr, nullptr, m2.id);
        QVERIFY(ed.moveUtterances({"u0"}, "Beszélő 2"));
    }
    library::PeopleStats* stats = m_app->peopleStats();
    QVERIFY(!stats->ready());
    stats->refreshNow();
    QVERIFY(stats->ready());

    const library::PersonStats g = stats->stats("bárány gergely");
    QCOMPARE(g.meetingCount, 2);
    QCOMPARE(g.talkMs, qint64(4000 + 2000 + 10000 + 3000));
    QCOMPARE(g.lastSeen, d2);
    QCOMPARE(g.meetings.size(), 2);
    QCOMPARE(g.meetings[0].title, QStringLiteral("Heti egyeztetés"));   // newest first
    QCOMPARE(g.meetings[0].talkMs, qint64(13000));
    QCOMPARE(g.meetings[0].speakerKeys, QStringList{"Beszélő 2"});
    QVERIFY(g.meetings[0].hasSummary);
    const library::PersonStats e = stats->stats(kEszter);
    QCOMPARE(e.meetingCount, 1);
    QCOMPARE(e.talkMs, qint64(0));              // all its lines went to another
    QCOMPARE(stats->stats("Ismeretlen").meetingCount, 0);
    QVERIFY(!stats->stats("Ismeretlen").lastSeen.isValid());
}

void PeopleServiceTest::statsRefreshInBackgroundAndCache()
{
    for (int i = 0; i < 12; ++i)
        addMeeting(QStringLiteral("Megbeszélés %1").arg(i), QDateTime(QDate(2026, 9, 1 + i), QTime(10, 0)),
                   {{0, 5000, "Beszélő 1"}}, {{"Beszélő 1", kGergely}}, false);
    library::PeopleStats* stats = m_app->peopleStats();
    QSignalSpy changed(stats, &library::PeopleStats::changed);
    stats->refresh();
    QVERIFY(stats->busy());
    QCOMPARE(stats->stats(kGergely).meetingCount, 0);    // the list can show meanwhile: the call does not wait
    QVERIFY(changed.wait(5000));
    QCOMPARE(stats->stats(kGergely).meetingCount, 12);
    QCOMPARE(stats->lastRescanned(), 12);
    // On a slower filesystem (Windows) the scheduled (250 ms) refresh of addMeeting fires during
    // the background computation, so another round runs afterwards. We wait for it before looking at the cache.
    QTRY_VERIFY_WITH_TIMEOUT(!stats->busy(), 5000);

    // Cache: it does not re-read unchanged meetings; it does a changed one.
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
    // A deleted meeting drops out.
    m_app->store()->deleteMeeting(m.id);
    stats->refreshNow();
    QCOMPARE(stats->stats(kEszter).meetingCount, 0);
}

// ---- rename ----

void PeopleServiceTest::renameKeepsOldNameAsAliasAndFollowsMeetings()
{
    const Meeting m = addMeeting("Partnerdemó", QDateTime(QDate(2026, 9, 24), QTime(10, 0)),
                                 {{0, 4000, "Beszélő 1"}}, {{"Beszélő 1", kGergely}}, false);
    m_app->voiceprints()->addPrint(kGergely, print("p1", {1, 0, 0}, m.id));
    QVERIFY(svc()->addAlias(kGergely, "Gergely").ok);

    const library::PeopleOpResult r = svc()->renamePerson(kGergely, "Bárány Gergő");
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.name, QStringLiteral("Bárány Gergő"));
    QVERIFY(!svc()->exists(kGergely));
    QCOMPARE(svc()->person("Bárány Gergő").aliases, QStringList({"Gergely", kGergely}));
    QCOMPARE(svc()->person("Bárány Gergő").sampleCount, 1);
    QCOMPARE(m_app->store()->load(m.id).speakerMap.value("Beszélő 1"), QStringLiteral("Bárány Gergő"));
    // Under the old name the person picker still finds it.
    QCOMPARE(filterPeople(m_app->peopleDirectory(), "gergely").size(), 1);

    // Undo: the old name is back everywhere, without the automatic nickname.
    QVERIFY(svc()->canUndo());
    QCOMPARE(svc()->undoInfo().kind, library::PeopleUndoKind::Renamed);
    const library::PeopleUndoInfo info = svc()->undo();
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
    const library::PeopleOpResult r = svc()->renamePerson(kGergo, "bárány gergely");
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
    QVERIFY(svc()->exists(kGergo));
    QVERIFY(!svc()->renamePerson(kGergo, "  ").ok);
    QVERIFY(!svc()->canUndo());
    // New person with an existing name: returns the existing one.
    const library::PeopleOpResult dup = svc()->addPerson("b. gergő");
    QVERIFY(!dup.ok);
    QCOMPARE(dup.name, kGergo);
}

void PeopleServiceTest::selfRenameChangesSettingAndSelfCannotBeDeleted()
{
    QVERIFY(svc()->isSelf("kovács lilla"));
    QVERIFY(svc()->person(kSelf).isSelf);
    QVERIFY(svc()->renamePerson(kSelf, "Kovács-Nagy Lilla").ok);
    QCOMPARE(m_app->settings()->settings().recording.userSpeakerName, QStringLiteral("Kovács-Nagy Lilla"));
    QVERIFY(svc()->isSelf("Kovács-Nagy Lilla"));
    QCOMPARE(svc()->person("Kovács-Nagy Lilla").aliases, QStringList{kSelf});

    const library::PeopleOpResult del = svc()->removePerson("Kovács-Nagy Lilla", false);
    QVERIFY(!del.ok);
    QVERIFY(svc()->exists("Kovács-Nagy Lilla"));

    svc()->undo();
    QCOMPARE(m_app->settings()->settings().recording.userSpeakerName, kSelf);
    QVERIFY(svc()->person(kSelf).aliases.isEmpty());
}

// ---- nickname, note ----

void PeopleServiceTest::aliasAddRemoveUndo()
{
    QVERIFY(svc()->addPerson(kGergely).ok);
    QSignalSpy people(m_app.get(), &app::AppController::peopleChanged);
    QVERIFY(svc()->addAlias(kGergely, "Gergely").ok);
    QVERIFY(svc()->addAlias(kGergely, "G. Bárány").ok);
    QVERIFY(svc()->addAlias(kGergely, "Geri").ok);
    QCOMPARE(people.count(), 3);                       // the person pickers refresh
    QVERIFY(!svc()->addAlias(kGergely, "geri").ok);
    QVERIFY(!svc()->addAlias(kGergely, " ").ok);
    QVERIFY(!svc()->addAlias(kGergely, kGergely).ok);

    QVERIFY(svc()->removeAlias(kGergely, "G. Bárány"));
    QCOMPARE(svc()->person(kGergely).aliases, QStringList({"Gergely", "Geri"}));
    QCOMPARE(svc()->undoInfo().kind, library::PeopleUndoKind::AliasRemoved);
    QCOMPARE(svc()->undoInfo().detail, QStringLiteral("G. Bárány"));
    svc()->undo();
    QCOMPARE(svc()->person(kGergely).aliases, QStringList({"Gergely", "G. Bárány", "Geri"}));   // into its place
    QVERIFY(!svc()->removeAlias(kGergely, "nincs"));
}

void PeopleServiceTest::noteIsStored()
{
    QVERIFY(svc()->addPerson(kGergely).ok);
    QSignalSpy changed(svc(), &library::PeopleService::changed);
    svc()->setNote(kGergely, "Northwind oldali projektvezető.\nGyakran telefonról csatlakozik.");
    QCOMPARE(changed.count(), 1);
    svc()->setNote(kGergely, "Northwind oldali projektvezető.\nGyakran telefonról csatlakozik.");
    QCOMPARE(changed.count(), 1);    // unchanged: no write
    QCOMPARE(library::PeopleStore(metaFile("people.json")).details(kGergely).note,
             QStringLiteral("Northwind oldali projektvezető.\nGyakran telefonról csatlakozik."));
}

void PeopleServiceTest::learnedDefaultSide()
{
    QVERIFY(svc()->addPerson(kGergely).ok);
    QVERIFY(svc()->person(kGergely).defaultSide.isEmpty());
    // One analysis: Gergely gets Remote from his confirmed lines, Eszter Local from the default
    // only (that does not learn), an anonymous speaker is left out.
    editing::SideReport r;
    editing::PersonSide g; g.speakerKey = "Beszélő 1"; g.personName = kGergely; g.side = editing::Side::Remote; g.basis = "lines";
    editing::PersonSide e; e.speakerKey = "Beszélő 2"; e.personName = kEszter; e.side = editing::Side::Local; e.basis = "user-name";
    editing::PersonSide a; a.speakerKey = "Beszélő 3"; a.side = editing::Side::Local; a.basis = "lines";
    r.persons = {g, e, a};
    QSignalSpy changed(svc(), &library::PeopleService::changed);
    QCOMPARE(svc()->learnDefaultSides(r), 1);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(svc()->person(kGergely).defaultSide, QStringLiteral("remote"));
    QVERIFY(svc()->person(kEszter).defaultSide.isEmpty());
    QCOMPARE(svc()->learnDefaultSides(r), 0);    // unchanged: no write
    // The record's field in people.json; the other data stays, and it can be deleted.
    library::PeopleStore store(metaFile("people.json"));
    QCOMPARE(store.defaultSide(kGergely), QStringLiteral("remote"));
    QCOMPARE(store.defaultSides().value(kGergely), QStringLiteral("remote"));
    QVERIFY(store.setDefaultSide(kGergely, QString()));
    QVERIFY(library::PeopleStore(metaFile("people.json")).defaultSide(kGergely).isEmpty());
    QVERIFY(library::PeopleStore(metaFile("people.json")).names().contains(kGergely));
}

// ---- samples ----

void PeopleServiceTest::sampleRemoveAndUndo()
{
    const Meeting m = addMeeting("Partnerdemó – adatimport", QDateTime(QDate(2026, 9, 24), QTime(10, 0)),
                                 {{0, 4000, "Beszélő 1"}}, {{"Beszélő 1", kGergely}}, true);
    m_app->voiceprints()->addPrint(kGergely, print("p1", {1, 0, 0}, m.id));
    QSignalSpy prints(m_app.get(), &app::AppController::voiceprintsChanged);

    const library::PeopleOpResult r = svc()->removeSample("p1");
    QVERIFY(r.ok);
    QCOMPARE(r.name, kGergely);
    QCOMPARE(prints.count(), 1);
    QCOMPARE(svc()->person(kGergely).sampleCount, 0);
    QVERIFY(svc()->exists(kGergely));                      // the person stays
    QCOMPARE(svc()->undoInfo().kind, library::PeopleUndoKind::SampleRemoved);
    QCOMPARE(svc()->undoInfo().detail, QStringLiteral("Partnerdemó – adatimport"));
    QVERIFY(!m_app->summaryStale(m.id).stale);             // deleting a sample is not an identity change

    svc()->undo();
    const QVector<Voiceprint> back = m_app->voiceprints()->printsFor(kGergely);
    QCOMPARE(back.size(), 1);
    QCOMPARE(back.first().id, QStringLiteral("p1"));       // the same sample, with the same id
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
    QSignalSpy stale(m_app.get(), &app::AppController::summaryStaleChanged);

    const library::PeopleOpResult r = svc()->moveSample("p1", "b. gergő");
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.name, kGergo);                              // the stored spelling
    QCOMPARE(r.staleSummaries, 1);
    QCOMPARE(m_app->voiceprints()->printCount(kGergely), 1);
    QCOMPARE(m_app->voiceprints()->printsFor(kGergo).first().id, QStringLiteral("p1"));
    QVERIFY(m_app->summaryStale(m.id).stale);              // the sample's source meeting
    QVERIFY(!m_app->summaryStale(other.id).stale);         // another meeting not
    QCOMPARE(stale.count(), 1);
    QVERIFY(!svc()->moveSample("p2", kGergely).ok);        // already belongs to that person

    svc()->undo();
    QCOMPARE(m_app->voiceprints()->printCount(kGergely), 2);
    QCOMPARE(m_app->voiceprints()->printCount(kGergo), 0);
    QVERIFY(svc()->exists(kGergo));                        // the target existed before: it stays
    QVERIFY(!m_app->summaryStale(m.id).stale);             // the stale marking is undone too
}

void PeopleServiceTest::newPersonFromSampleAndUndoRemovesPerson()
{
    const Meeting m = addMeeting("Partnerdemó", QDateTime(QDate(2026, 9, 24), QTime(10, 0)),
                                 {{0, 4000, "Beszélő 1"}}, {{"Beszélő 1", kGergely}}, false);
    m_app->voiceprints()->addPrint(kGergely, print("p1", {1, 0, 0}, m.id));
    m_app->voiceprints()->addPrint(kGergely, print("p2", {0, 1, 0}, m.id));

    const library::PeopleOpResult r = svc()->moveSample("p2", "Új Ember");
    QVERIFY(r.ok);
    QCOMPARE(r.name, QStringLiteral("Új Ember"));
    QCOMPARE(r.staleSummaries, 0);                          // no summary: nothing to mark
    QVERIFY(svc()->exists("Új Ember"));
    QVERIFY(library::PeopleStore(metaFile("people.json")).names().contains("Új Ember"));
    QCOMPARE(svc()->person("Új Ember").sampleCount, 1);
    QCOMPARE(svc()->person(kGergely).sampleCount, 1);       // the sample left the earlier voiceprint

    svc()->undo();
    QVERIFY(!svc()->exists("Új Ember"));                    // the operation created it: it disappears
    QCOMPARE(svc()->person(kGergely).sampleCount, 2);
}

void PeopleServiceTest::similarityFromStoredEmbeddings()
{
    m_app->voiceprints()->addPrint(kGergely, print("a1", {1, 0, 0}));
    m_app->voiceprints()->addPrint(kGergely, print("a2", {0, 1, 0}));
    m_app->voiceprints()->addPrint(kGergo, print("b1", {0.6f, 0.8f, 0}));
    m_app->voiceprints()->addPrint("Gál Bence", print("c1", {0, 0, 1}));
    QVERIFY(svc()->addPerson(kEszter).ok);
    // The best matching pair counts (as in recognition).
    QVERIFY(qAbs(svc()->similarity(kGergely, kGergo) - 0.8) < 1e-6);
    QVERIFY(qAbs(svc()->similarity(kGergo, kGergely) - 0.8) < 1e-6);
    QCOMPARE(svc()->similarity(kGergely, "Gál Bence"), 0.0);
    QCOMPARE(svc()->similarity(kGergely, kEszter), -1.0);   // no voiceprint: no percentage
}

// ---- merge ----

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

    const library::MergePreview preview = svc()->mergePreview(kGergo, kGergely);
    QCOMPARE(preview.meetingCount, 4);          // union: the shared meeting counts once
    QCOMPARE(preview.sampleCount, 3);
    QCOMPARE(preview.staleSummaries, 2);        // the disappearing name's meetings with a summary

    QSignalSpy stale(m_app.get(), &app::AppController::summaryStaleChanged);
    const library::PeopleOpResult r = svc()->merge(kGergo, kGergely);
    QVERIFY2(r.ok, qPrintable(r.error));
    // The dialog's numbers = the actual result.
    QCOMPARE(r.meetings, preview.meetingCount);
    QCOMPARE(r.samples, preview.sampleCount);
    QCOMPARE(r.staleSummaries, preview.staleSummaries);
    QCOMPARE(stale.count(), 2);

    QVERIFY(!svc()->exists(kGergo));
    const library::PersonRecord g = svc()->person(kGergely);
    QCOMPARE(g.sampleCount, 3);
    QCOMPARE(g.aliases, QStringList({"Geri", kGergo}));     // the disappearing name became a nickname
    QCOMPARE(g.note, QStringLiteral("Telefonról csatlakozik."));
    QCOMPARE(m_app->peopleStats()->stats(kGergely).meetingCount, 4);
    QCOMPARE(m_app->store()->load(both.id).speakerMap.value("Beszélő 2"), kGergely);
    QCOMPARE(m_app->store()->load(noSummary.id).speakerMap.value("Beszélő 1"), kGergely);

    QVERIFY(m_app->summaryStale(both.id).stale);
    QVERIFY(m_app->summaryStale(onlyLoser.id).stale);
    QVERIFY(!m_app->summaryStale(noSummary.id).stale);       // it has no summary
    QVERIFY(!m_app->summaryStale(onlySurvivor.id).stale);    // nothing changed there
    QVERIFY(!svc()->canUndo());                              // the merge cannot be undone
    QVERIFY(!svc()->merge(kGergely, kGergely).ok);
    QVERIFY(!svc()->merge("Nincs Ilyen", kGergely).ok);
}

void PeopleServiceTest::mergeWithOpenEditorShowsStale()
{
    const Meeting m = addMeeting("Nyitott", QDateTime(QDate(2026, 9, 20), QTime(10, 0)),
                                 {{0, 4000, "Beszélő 1"}, {5000, 4000, "Beszélő 2"}},
                                 {{"Beszélő 1", kGergo}}, true);
    QVERIFY(svc()->addPerson(kGergely).ok);
    // In the main window the transcript editor is open: it must follow without a restart.
    editing::SpeakerEditor* ed = m_app->speakerEditor(m.id);
    QVERIFY(ed);
    QVERIFY(!ed->summaryStale().stale);
    QVERIFY(svc()->merge(kGergo, kGergely).ok);
    QVERIFY(ed->summaryStale().stale);
    QCOMPARE(ed->speaker("Beszélő 1").personName, kGergely);
    QVERIFY(m_app->summaryStale(m.id).stale);
}

// ---- delete ----

void PeopleServiceTest::deleteNumbersAndStale()
{
    const QDateTime d(QDate(2026, 9, 20), QTime(10, 0));
    const Meeting a = addMeeting("Egyik", d, {{0, 4000, "Távoli 2"}}, {{"Távoli 2", kGergely}}, true);
    const Meeting b = addMeeting("Másik", d.addDays(1), {{0, 4000, "Beszélő 1"}}, {{"Beszélő 1", kGergely}}, false);
    m_app->voiceprints()->addPrint(kGergely, print("a1", {1, 0, 0}, a.id));
    QVERIFY(svc()->addAlias(kGergely, "Gergely").ok);
    QVERIFY(svc()->removeAlias(kGergely, "Gergely"));       // let there be one undoable step
    m_app->peopleStats()->refreshNow();

    const library::DeletePreview preview = svc()->deletePreview(kGergely);
    QCOMPARE(preview.meetingCount, 2);
    QCOMPARE(preview.sampleCount, 1);
    QCOMPARE(preview.staleSummaries, 1);
    QCOMPARE(preview.exampleLabel, QStringLiteral("Beszélő 1"));   // the newest meeting's label

    const library::PeopleOpResult r = svc()->removePerson(kGergely, false);
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.meetings, 2);
    QCOMPARE(r.samples, 1);
    QCOMPARE(r.staleSummaries, 1);
    QVERIFY(r.name.isEmpty());
    QVERIFY(!svc()->exists(kGergely));
    QCOMPARE(m_app->voiceprints()->totalPrintCount(), 0);
    QVERIFY(m_app->store()->load(a.id).speakerMap.isEmpty());       // an anonymous speaker stays
    QVERIFY(m_app->summaryStale(a.id).stale);
    QVERIFY(!m_app->summaryStale(b.id).stale);
    QVERIFY(library::PeopleStore(metaFile("people.json")).details(kGergely).isEmpty());
    QVERIFY(!svc()->canUndo());                                      // the delete cannot be undone
    QVERIFY(!svc()->removePerson(kGergely, false).ok);
}

void PeopleServiceTest::deleteKeepingSamplesAsAnonymous()
{
    const Meeting a = addMeeting("Egyik", QDateTime(QDate(2026, 9, 20), QTime(10, 0)),
                                 {{0, 4000, "Beszélő 1"}}, {{"Beszélő 1", kGergely}}, false);
    m_app->voiceprints()->addPrint(kGergely, print("a1", {1, 0, 0}, a.id));
    m_app->voiceprints()->addPrint(kGergely, print("a2", {0, 1, 0}, a.id));
    QVERIFY(svc()->addPerson("Névtelen 1").ok);       // taken name: the next free one is needed

    const library::PeopleOpResult r = svc()->removePerson(kGergely, true);
    QVERIFY(r.ok);
    QCOMPARE(r.name, QStringLiteral("Névtelen 2"));
    QVERIFY(!svc()->exists(kGergely));
    QVERIFY(svc()->exists("Névtelen 2"));
    QCOMPARE(svc()->person("Névtelen 2").sampleCount, 2);
    QVERIFY(library::PeopleStore(metaFile("people.json")).names().contains("Névtelen 2"));
    // An anonymous speaker stays on the meeting; the anonymous PERSON does not go onto it.
    QVERIFY(m_app->store()->load(a.id).speakerMap.isEmpty());

    // Without samples there is nothing to keep: no anonymous person is created.
    QVERIFY(svc()->addPerson(kEszter).ok);
    const library::PeopleOpResult none = svc()->removePerson(kEszter, true);
    QVERIFY(none.ok);
    QVERIFY(none.name.isEmpty());
    QVERIFY(!svc()->exists("Névtelen 3"));
}

// ---- voiceprint from the manually assigned meetings ----

void PeopleServiceTest::voiceprintFromManuallyAssignedMeetings()
{
    const QDateTime d(QDate(2026, 9, 3), QTime(10, 0));
    // Enough material: 4 x 5 s of certain lines.
    const Meeting rich = addMeeting("Kickoff", d,
        {{0, 5000, "Beszélő 1"}, {6000, 5000, "Beszélő 1"}, {12000, 5000, "Beszélő 1"}, {18000, 5000, "Beszélő 1"},
         {24000, 5000, "Beszélő 2"}}, {{"Beszélő 1", kEszter}}, false);
    // Little material: a single 4 s line.
    const Meeting poor = addMeeting("Rövid", d.addDays(3), {{0, 4000, "Beszélő 1"}}, {{"Beszélő 1", kEszter}}, false);
    m_app->peopleStats()->refreshNow();

    // Without a voice model: the plan says it cannot work.
    library::VoiceprintPlan plan = svc()->voiceprintPlan(kEszter);
    QVERIFY(!plan.modelAvailable);
    QVERIFY(!svc()->createVoiceprintFromMeeting(kEszter, rich.id).ok);

    svc()->setEmbedderFactory([] { return std::make_unique<FakeEmbedder>(); });
    plan = svc()->voiceprintPlan(kEszter);
    QVERIFY(plan.modelAvailable);
    QCOMPARE(plan.meetingsWithLines, 2);
    QCOMPARE(plan.usableMeetingIds, QStringList{rich.id});
    QCOMPARE(plan.usableMs, qint64(20000));

    QSignalSpy prints(m_app.get(), &app::AppController::voiceprintsChanged);
    const editing::VoiceprintResult bad = svc()->createVoiceprintFromMeeting(kEszter, poor.id);
    QVERIFY(!bad.ok);
    QVERIFY(!bad.error.isEmpty());
    const editing::VoiceprintResult ok = svc()->createVoiceprintFromMeeting(kEszter, rich.id);
    QVERIFY2(ok.ok, qPrintable(ok.error));
    QCOMPARE(ok.usedMs, qint64(20000));
    QCOMPARE(prints.count(), 1);
    const QVector<Voiceprint> made = m_app->voiceprints()->printsFor(kEszter);
    QCOMPARE(made.size(), 1);
    QCOMPARE(made.first().sourceMeetingId, rich.id);
    // Someone with no line in any meeting: an empty plan.
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
    loop.startOffsetMs = 10000;   // a track switched on later: the sample's meeting time becomes file time
    m.tracks = {mic, loop};
    m_app->store()->saveMeeting(m);
    writeFile(QDir(m.folder).filePath("track_mic.ogg"), "x");

    Voiceprint a = print("s-mic", {1, 0, 0}, m.id);
    a.sourceTrack = "mic"; a.sampleRef = "track_mic.ogg#2000-44000";
    Voiceprint b = print("s-call", {1, 0, 0}, m.id);
    b.sourceTrack = "loop"; b.sampleRef = "track_loop.ogg#12000-38000";
    Voiceprint c = print("s-mix", {1, 0, 0}, m.id);
    Voiceprint e = print("s-gone", {1, 0, 0}, "torolt-megbeszeles");
    e.sourceTrack.clear(); e.createdAt = "2026-08-28T09:00:00";
    for (const Voiceprint& p : {a, b, c, e}) m_app->voiceprints()->addPrint(kGergely, p);

    const QVector<library::VoiceSample> samples = svc()->samples(kGergely);
    QCOMPARE(samples.size(), 4);
    auto byId = [&](const QString& id) {
        for (const library::VoiceSample& s : samples) if (s.id == id) return s;
        return library::VoiceSample();
    };
    const library::VoiceSample sm = byId("s-mic");
    QCOMPARE(sm.sourceKind, QStringLiteral("mic"));
    QCOMPARE(sm.deviceLabel, QStringLiteral("Trust USB mikrofon"));
    QCOMPARE(sm.meetingTitle, QStringLiteral("Partnerdemó – adatimport"));
    QCOMPARE(sm.recordedAt, QDate(2026, 9, 24));
    QCOMPARE(sm.durationMs(), qint64(42000));
    QVERIFY(sm.audioExists);
    QCOMPARE(sm.audioPath, QDir(m.folder).filePath("track_mic.ogg"));
    QCOMPARE(byId("s-call").sourceKind, QStringLiteral("call"));
    QCOMPARE(byId("s-call").startMs, qint64(2000));
    QCOMPARE(byId("s-call").endMs, qint64(28000));
    QVERIFY(!byId("s-call").audioExists);
    QCOMPARE(byId("s-mix").sourceKind, QStringLiteral("mix"));
    const library::VoiceSample gone = byId("s-gone");
    QCOMPARE(gone.sourceKind, QStringLiteral("unknown"));
    QVERIFY(gone.meetingTitle.isEmpty());
    QCOMPARE(gone.recordedAt, QDate(2026, 8, 28));     // without the meeting, the day the sample was made
    QCOMPARE(samples.last().id, QStringLiteral("s-gone"));   // newest first
}

// ---- two processes ----

void PeopleServiceTest::twoProcessesWriteConcurrently()
{
    // ANOTHER process (this test exe in child mode) writes into the same two files while we
    // write too: at the end both sides' every change must be there.
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
    const QStringList names = library::PeopleStore(QDir(meta).filePath("people.json")).names();
    const QStringList aliases = library::PeopleStore(QDir(meta).filePath("people.json")).aliases(kGergely);
    const library::VoiceprintStore prints(QDir(meta).filePath("voiceprints.json"));
    for (int i = 0; i < n; ++i) {
        QVERIFY2(names.contains(QStringLiteral("Szülő %1").arg(i)), qPrintable(QString::number(i)));
        QVERIFY2(names.contains(QStringLiteral("Gyerek %1").arg(i)), qPrintable(QString::number(i)));
        QVERIFY2(aliases.contains(QStringLiteral("szülő-becenév %1").arg(i)), qPrintable(QString::number(i)));
        QVERIFY2(aliases.contains(QStringLiteral("gyerek-becenév %1").arg(i)), qPrintable(QString::number(i)));
    }
    QCOMPARE(prints.printCount("Szülő"), n);
    QCOMPARE(prints.printCount("Gyerek"), n);
    // The running instance sees the other process's data too.
    QVERIFY(svc()->exists("Gyerek 0"));
    QCOMPARE(svc()->person(kGergely).aliases.size(), 2 * n);
    QCOMPARE(svc()->person("Gyerek").sampleCount, n);
    // No lock file remains.
    QVERIFY(QDir(meta).entryList({"*.lock"}, QDir::Files | QDir::Hidden).isEmpty());
}

// The child process: writes into the two files of the given metadata folder (through the
// stores, as another Tanara process would).
static int runChild(const QString& meta)
{
    library::PeopleStore people(QDir(meta).filePath("people.json"));
    library::VoiceprintStore prints(QDir(meta).filePath("voiceprints.json"));
    for (int i = 0; i < 40; ++i) {
        people.add(QStringLiteral("Gyerek %1").arg(i));
        if (!people.addAlias(kGergely, QStringLiteral("gyerek-becenév %1").arg(i))) return 2;
        prints.addPrint("Gyerek", print(QStringLiteral("child-%1").arg(i), {0, 1, 0}));
    }
    return 0;
}

// The child process for the migration race: opens the (old-form) store, which is itself the
// migration, then writes into it right away.
static int runMigrateChild(const QString& path, const QString& no)
{
    library::PeopleStore people(path);
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
