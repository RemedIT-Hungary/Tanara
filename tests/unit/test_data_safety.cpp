// Data safety: the fault tolerance of the file-based stores (only in temporary folders):
//  - orphan recovery: does not touch a live recording's folder (recording.lock / fresh track file);
//  - index: a folder never appears twice; deleting a meeting does not delete another meeting's folder;
//  - settings.json: a corrupt file is not silently overwritten, it is set aside on save;
//  - people.json / voiceprints.json: two "processes" (two store instances) do not overwrite each other;
//  - replaceFile: the old file stays if the replace fails;
//  - relocateTrack: the mixdown, another track's file and a non-audio file cannot become a track.
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>

#include "tanara/app/SettingsManager.h"
#include "tanara/library/tracks/TrackCatalog.h"
#include "tanara/foundation/ProcessLock.h"
#include "tanara/foundation/secrets/SecretStore.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/foundation/SharedFile.h"
#include "tanara/library/store/VoiceprintStore.h"

using namespace tanara;

namespace {

void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    f.write(data);
}

QByteArray readFile(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// Resets a file's modification time to the past (as if nobody had written to it for a long time).
void backdate(const QString& path, int secs)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(-secs), QFileDevice::FileModificationTime));
}

Voiceprint print(float a, float b)
{
    Voiceprint vp;
    vp.embedding = {a, b, 0.5f};
    return vp;
}

} // namespace

class DataSafetyTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void orphanRecoverySkipsLiveLockFolder();
    void orphanRecoverySkipsFreshTracks();
    void indexNeverListsFolderTwice();
    void deleteKeepsFolderOfAnotherMeeting();
    void deleteKeepsLiveRecordingFolder();
    void meetingJsonWrittenAtomically();
    void corruptSettingsAreNotOverwritten();
    void healthySettingsRoundTrip();
    void secretsKeepPermissionsAndContent();
    void peopleFromTwoProcessesMerge();
    void voiceprintsFromTwoProcessesMerge();
    void corruptVoiceprintsAreSetAside();
    void replaceFileKeepsOldOnFailure();
    void relocateRejectsMixdownAndNonAudio();
    void deletingTracksNeverDeletesMixdown();
    void sandboxRolesStayInside();

private:

    // The tests' people: the name stands in as the PersonId.
    static PersonId pid(const QString& name) { return PersonId::fromString(name); }
    QString orphanFolder(const QString& name);

    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_audio, m_meta;
};

void DataSafetyTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    // SettingsManager's default folders (recordings, notes) go into the temp folder too: the
    // test NEVER touches the real ~/.tanara / ~/Tanara folder.
    qputenv("TANARA_HOME", m_dir->filePath("home").toUtf8());
    m_audio = m_dir->filePath("recordings");
    m_meta = m_dir->filePath("meta");
}

void DataSafetyTest::cleanup() { m_dir.reset(); }

// A recording folder without meeting.json, with an old (not fresh) track file.
QString DataSafetyTest::orphanFolder(const QString& name)
{
    const QString folder = QDir(m_audio).filePath(name);
    writeFile(QDir(folder).filePath("track_mic.ogg"), "nem-valodi-ogg");
    backdate(QDir(folder).filePath("track_mic.ogg"), 3600);
    return folder;
}

void DataSafetyTest::orphanRecoverySkipsLiveLockFolder()
{
    library::MeetingRepository store(m_audio, m_meta);
    const QString live = orphanFolder("2026-01-05_1000_Elo-felvetel");
    const QString crashed = orphanFolder("2026-01-04_0900_Osszeomlott");

    // A live recorder (the test's own PID is alive) on this folder: not an orphan.
    RecordingLock lock(recordingLockPath());
    QVERIFY(lock.acquire(live + "/"));   // with a trailing slash it is the same folder
    QCOMPARE(store.recoverOrphanRecordings(), 1);
    QVERIFY(!QFile::exists(QDir(live).filePath("meeting.json")));
    QVERIFY(QFile::exists(QDir(crashed).filePath("meeting.json")));
    const QVector<Meeting> all = store.loadAll();
    QCOMPARE(all.size(), 1);
    QCOMPARE(QDir(all.first().folder).dirName(), QStringLiteral("2026-01-04_0900_Osszeomlott"));

    // The recording ended (lock released), meeting.json still absent: now it is an orphan.
    lock.release();
    QCOMPARE(store.recoverOrphanRecordings(), 1);
    QVERIFY(QFile::exists(QDir(live).filePath("meeting.json")));

    // A stale lock (dead PID) does not protect: a crashed recorder's folder is recovered.
    const QString stale = orphanFolder("2026-01-06_1100_Halott-felvevo");
    writeFile(recordingLockPath(),
              QJsonDocument(QJsonObject{{"pid", 2147483000}, {"meetingFolder", stale},
                                        {"startedAt", "2026-01-06T11:00:00"}}).toJson());
    QVERIFY(!RecordingLock::read(recordingLockPath()).active);
    QCOMPARE(store.recoverOrphanRecordings(), 1);
    QVERIFY(QFile::exists(QDir(stale).filePath("meeting.json")));
}

void DataSafetyTest::orphanRecoverySkipsFreshTracks()
{
    library::MeetingRepository store(m_audio, m_meta);
    const QString folder = QDir(m_audio).filePath("2026-01-05_1000_Most-irjak");
    writeFile(QDir(folder).filePath("track_mic.ogg"), "regi");
    backdate(QDir(folder).filePath("track_mic.ogg"), 3600);
    writeFile(QDir(folder).filePath("track_sys.ogg"), "epp-irodik");   // fresh modification, no lock

    QCOMPARE(store.recoverOrphanRecordings(), 0);
    QVERIFY(!QFile::exists(QDir(folder).filePath("meeting.json")));
    QVERIFY(store.loadAll().isEmpty());

    // After a few minutes of silence (nobody writes to it) it counts as an orphan.
    backdate(QDir(folder).filePath("track_sys.ogg"), 600);
    QCOMPARE(store.recoverOrphanRecordings(), 1);
    QCOMPARE(store.loadAll().size(), 1);
    QCOMPARE(store.load(store.loadAll().first().id).tracks.size(), 2);
}

void DataSafetyTest::indexNeverListsFolderTwice()
{
    library::MeetingRepository analyzer(m_audio, m_meta);
    Meeting a = analyzer.createMeeting("Helyreállított");

    // Another process (the recorder) writes meeting.json with a NEW id into the same folder.
    {
        library::MeetingRepository recorder(m_audio, m_meta);
        Meeting b = a;
        b.id = "felvevo-id";
        b.title = "A valódi felvétel";
        recorder.saveMeeting(b);
    }
    QVector<Meeting> all = analyzer.loadAll();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all.first().id, QStringLiteral("felvevo-id"));

    // A duplicate row left in an old index (from a build before the fix): the list still gives
    // one row, the one named in the folder's meeting.json.
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "teszt-kettos-sor");
        db.setDatabaseName(QDir(m_meta).filePath("index.db"));
        QVERIFY(db.open());
        QSqlQuery q(db);
        q.prepare("INSERT INTO meetings (id, title, root, dir, startedAt, durationMs, hasTranscript, hasSummary)"
                  " VALUES (:id, 'Árva sor', :root, :dir, '2099-01-01T00:00:00.000', 0, 0, 0)");
        q.bindValue(":id", a.id);
        q.bindValue(":root", QFileInfo(a.folder).absolutePath());   // index v2: (root, dir)
        q.bindValue(":dir", QFileInfo(a.folder).fileName());
        QVERIFY(q.exec());
        db.close();
    }
    QSqlDatabase::removeDatabase("teszt-kettos-sor");
    all = analyzer.loadAll();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all.first().id, QStringLiteral("felvevo-id"));

    // After an index rebuild there is one row too.
    analyzer.rebuildIndexFromDisk();
    QCOMPARE(analyzer.loadAll().size(), 1);
}

void DataSafetyTest::deleteKeepsFolderOfAnotherMeeting()
{
    library::MeetingRepository store(m_audio, m_meta);
    Meeting a = store.createMeeting("Helyreállított");
    writeFile(QDir(a.folder).filePath("track_mic.ogg"), "felvetel");

    // The folder's meeting.json already has the recorder's id; both rows are alive in the index.
    Meeting b = a;
    b.id = "felvevo-id";
    writeFile(QDir(a.folder).filePath("meeting.json"),
              QJsonDocument(QJsonObject{{"id", b.id}, {"title", "A valódi felvétel"}}).toJson());
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "teszt-masik-sor");
        db.setDatabaseName(QDir(m_meta).filePath("index.db"));
        QVERIFY(db.open());
        QSqlQuery q(db);
        q.prepare("INSERT INTO meetings (id, title, root, dir, startedAt, durationMs, hasTranscript, hasSummary)"
                  " VALUES (:id, 'A valódi felvétel', :root, :dir, '2026-01-01T00:00:00.000', 0, 0, 0)");
        q.bindValue(":id", b.id);
        q.bindValue(":root", QFileInfo(a.folder).absolutePath());   // index v2: (root, dir)
        q.bindValue(":dir", QFileInfo(a.folder).fileName());
        QVERIFY(q.exec());
        db.close();
    }
    QSqlDatabase::removeDatabase("teszt-masik-sor");

    // Deleting the "(recovered)" entry must NOT take the other meeting's folder.
    QSignalSpy removed(&store, &library::MeetingRepository::meetingRemoved);
    QVERIFY(store.deleteMeeting(a.id));
    QCOMPARE(removed.count(), 1);
    QVERIFY(QDir(a.folder).exists());
    QCOMPARE(readFile(QDir(a.folder).filePath("track_mic.ogg")), QByteArray("felvetel"));
    QVector<Meeting> all = store.loadAll();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all.first().id, b.id);

    // Deleting the folder's actual owner takes the folder (the old behavior).
    QVERIFY(store.deleteMeeting(b.id));
    QVERIFY(!QDir(a.folder).exists());
    QVERIFY(store.loadAll().isEmpty());
}

void DataSafetyTest::deleteKeepsLiveRecordingFolder()
{
    library::MeetingRepository store(m_audio, m_meta);
    const Meeting a = store.createMeeting("Felvétel alatt");
    writeFile(QDir(a.folder).filePath("track_mic.ogg"), "epp-irodik");
    RecordingLock lock(recordingLockPath());
    QVERIFY(lock.acquire(a.folder));

    QVERIFY(store.deleteMeeting(a.id));   // the entry disappears...
    QVERIFY(store.loadAll().isEmpty());
    QVERIFY(QFile::exists(QDir(a.folder).filePath("track_mic.ogg")));   // ...the recording does not
    lock.release();
}

void DataSafetyTest::meetingJsonWrittenAtomically()
{
    library::MeetingRepository store(m_audio, m_meta);
    Meeting m = store.createMeeting("Atomikus");
    m.speakerMap.insert("Beszélő 1", pid("Ödön"));
    m.hasTranscript = true;
    for (int i = 0; i < 20; ++i) {
        m.contextNote = QString("megjegyzés %1").arg(i);
        store.saveMeeting(m);
    }
    // The folder has only meeting.json (no temporary file remained), and it is valid JSON.
    QCOMPARE(QDir(m.folder).entryList(QDir::Files | QDir::Hidden), QStringList{"meeting.json"});
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(readFile(QDir(m.folder).filePath("meeting.json")), &err);
    QCOMPARE(err.error, QJsonParseError::NoError);
    QCOMPARE(doc.object().value("id").toString(), m.id);
    const Meeting back = store.load(m.id);
    QCOMPARE(back.contextNote, QStringLiteral("megjegyzés 19"));
    QCOMPARE(back.speakerMap.value("Beszélő 1"), pid("Ödön"));
}

void DataSafetyTest::corruptSettingsAreNotOverwritten()
{
    QDir().mkpath(m_meta);
    const QString path = QDir(m_meta).filePath("settings.json");
    // An interrupted write: truncated JSON, with the user's settings in it.
    const QByteArray broken = R"({"version": 2, "recording": {"userSpeakerName": "Ádá)";
    writeFile(path, broken);

    app::SettingsManager sm(m_meta);
    QVERIFY(sm.loadFailed());
    QCOMPARE(sm.settings().providers.stt.selectedId, QStringLiteral("soniox"));   // defaults in memory
    QCOMPARE(readFile(path), broken);                                  // the file is UNTOUCHED
    // Another process (recorder / watcher) loading does not overwrite it either.
    { app::SettingsManager other(m_meta); QVERIFY(other.loadFailed()); }
    QCOMPARE(readFile(path), broken);
    QCOMPARE(QDir(m_meta).entryList({"settings.json*"}, QDir::Files).size(), 1);

    // Explicit save: the corrupt file is set aside (kept), the new one is valid.
    tanara::app::AppSettings s = sm.settings();
    s.recording.userSpeakerName = "Lilla";
    sm.setSettings(s);
    QVERIFY(!sm.loadFailed());
    const QStringList aside = QDir(m_meta).entryList({"settings.json.corrupt-*"}, QDir::Files);
    QCOMPARE(aside.size(), 1);
    QCOMPARE(readFile(QDir(m_meta).filePath(aside.first())), broken);
    app::SettingsManager again(m_meta);
    QVERIFY(!again.loadFailed());
    QCOMPARE(again.settings().recording.userSpeakerName, QStringLiteral("Lilla"));
    // The next save sets nothing aside anymore.
    again.setSettings(again.settings());
    QCOMPARE(QDir(m_meta).entryList({"settings.json.corrupt-*"}, QDir::Files).size(), 1);
}

void DataSafetyTest::healthySettingsRoundTrip()
{
    // No file: it is created with the defaults (first launch), as before.
    app::SettingsManager first(m_meta);
    QVERIFY(!first.loadFailed());
    const QString path = QDir(m_meta).filePath("settings.json");
    QVERIFY(QFile::exists(path));
    tanara::app::AppSettings s = first.settings();
    s.recording.userSpeakerName = "Ödön";
    s.processing.languageHints = QStringList{"hu", "en"};
    first.setSettings(s);
    // Only settings.json remains (no temporary / set-aside file), and it can be read back.
    QCOMPARE(QDir(m_meta).entryList({"settings.json*"}, QDir::Files | QDir::Hidden), QStringList{"settings.json"});
    app::SettingsManager second(m_meta);
    QCOMPARE(second.settings().recording.userSpeakerName, QStringLiteral("Ödön"));
    QCOMPARE(second.settings().processing.languageHints, QStringList({"hu", "en"}));
}

void DataSafetyTest::secretsKeepPermissionsAndContent()
{
    const QString path = QDir(m_meta).filePath("secrets.json");
    FileSecretStore ks(path);
    ks.set("soniox.apiKey", "titok-1");
    ks.set("llm.apiKey", "titok-2");
    FileSecretStore back(path);
    QCOMPARE(back.get("soniox.apiKey"), QStringLiteral("titok-1"));
    QCOMPARE(back.get("llm.apiKey"), QStringLiteral("titok-2"));
    QCOMPARE(QDir(m_meta).entryList(QDir::Files | QDir::Hidden), QStringList{"secrets.json"});
#ifdef Q_OS_UNIX
    QCOMPARE(QFile::permissions(path) & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther),
             QFile::Permissions());
    QVERIFY(QFile::permissions(path) & QFile::ReadOwner);
#endif
}

void DataSafetyTest::peopleFromTwoProcessesMerge()
{
    const QString path = QDir(m_meta).filePath("people.json");
    // Two "processes": both load at startup, then modify alternately.
    library::PeopleStore analyzer(path);
    library::PeopleStore recorder(path);
    auto names = [](const library::PeopleStore& st) {
        QStringList out;
        for (const Person& p : st.people()) out << p.displayName;
        return out;
    };
    const PersonId bence = analyzer.create("Bence");
    const PersonId lilla = recorder.create("Lilla");          // before the fix this dropped Bence
    analyzer.create("Zsófi");                                 // ...and this dropped Lilla
    QCOMPARE(names(library::PeopleStore(path)), QStringList({"Bence", "Lilla", "Zsófi"}));

    // Delete / rename builds on the other process's fresh state, and does not resurrect a deleted person.
    recorder.remove(bence);
    analyzer.rename(lilla, "Kovács Lilla");
    QCOMPARE(names(library::PeopleStore(path)), QStringList({"Kovács Lilla", "Zsófi"}));
    QCOMPARE(names(analyzer), QStringList({"Kovács Lilla", "Zsófi"}));
    // Two records in the file, and no lock file remains.
    QCOMPARE(QJsonDocument::fromJson(readFile(path)).object().value("people").toObject().size(), 2);
    QCOMPARE(QDir(m_meta).entryList(QDir::Files | QDir::Hidden), QStringList{"people.json"});
}

void DataSafetyTest::voiceprintsFromTwoProcessesMerge()
{
    const QString path = QDir(m_meta).filePath("voiceprints.json");
    library::VoiceprintStore analyzer(path);
    library::VoiceprintStore recorder(path);
    analyzer.addPrint(pid("Bence"), print(1, 0));
    recorder.addPrint(pid("Lilla"), print(0, 1));        // before the fix this dropped Bence's voiceprint
    analyzer.addPrint(pid("Bence"), print(0.9f, 0.1f));  // ...and this dropped Lilla's
    {
        library::VoiceprintStore fresh(path);
        QCOMPARE(fresh.people(), PersonIds({pid("Bence"), pid("Lilla")}));
        QCOMPARE(fresh.printCount(pid("Bence")), 2);
        QCOMPARE(fresh.printCount(pid("Lilla")), 1);
    }
    // The other process's delete stays: the next write does not resurrect it.
    recorder.removePerson(pid("Bence"));
    analyzer.addPrint(pid("Zsófi"), print(0.5f, 0.5f));
    library::VoiceprintStore fresh(path);
    QCOMPARE(fresh.people(), PersonIds({pid("Lilla"), pid("Zsófi")}));
    QCOMPARE(fresh.totalPrintCount(), 2);
    QCOMPARE(analyzer.totalPrintCount(), 2);
    QCOMPARE(QDir(m_meta).entryList(QDir::Files | QDir::Hidden), QStringList{"voiceprints.json"});
}

void DataSafetyTest::corruptVoiceprintsAreSetAside()
{
    const QString path = QDir(m_meta).filePath("voiceprints.json");
    const QByteArray broken = R"({"version":3,"people":{"Ödön":[{"id":"a","embedding":[0.1,0.)";
    writeFile(path, broken);
    library::VoiceprintStore store(path);
    QCOMPARE(store.totalPrintCount(), 0);
    QCOMPARE(readFile(path), broken);            // not touched on load
    store.addPrint(pid("Lilla"), print(0, 1));
    const QStringList aside = QDir(m_meta).entryList({"voiceprints.json.corrupt-*"}, QDir::Files);
    QCOMPARE(aside.size(), 1);                   // the corrupt file stayed (can be saved by hand)
    QCOMPARE(readFile(QDir(m_meta).filePath(aside.first())), broken);
    QCOMPARE(library::VoiceprintStore(path).people(), PersonIds{pid("Lilla")});
}

void DataSafetyTest::replaceFileKeepsOldOnFailure()
{
    QDir().mkpath(m_meta);
    const QString out = QDir(m_meta).filePath("mixdown.mp3");
    const QString part = QDir(m_meta).filePath("mixdown.part.mp3");
    writeFile(out, "REGI-KEVEREK");

    // The replace cannot happen (no source), so the old file is UNTOUCHED. (The earlier "delete,
    // then rename" would have deleted the old one here.)
    QVERIFY(!replaceFile(part, out));
    QCOMPARE(readFile(out), QByteArray("REGI-KEVEREK"));

    // Successful replace: the new content is in place, no half-finished / set-aside file remains.
    writeFile(part, "UJ-KEVEREK");
    QVERIFY(replaceFile(part, out));
    QCOMPARE(readFile(out), QByteArray("UJ-KEVEREK"));
    QCOMPARE(QDir(m_meta).entryList(QDir::Files | QDir::Hidden), QStringList{"mixdown.mp3"});

    // Works without an old file too (first mix).
    QVERIFY(QFile::remove(out));
    writeFile(part, "ELSO");
    QVERIFY(replaceFile(part, out));
    QCOMPARE(readFile(out), QByteArray("ELSO"));
}

void DataSafetyTest::relocateRejectsMixdownAndNonAudio()
{
    library::MeetingRepository store(m_audio, m_meta);
    Meeting m = store.createMeeting("Sávok");
    Track mic; mic.id = "mic"; mic.kind = TrackKind::Mic; mic.file = "track_mic.ogg"; mic.active = true;
    Track sys; sys.id = "sys"; sys.kind = TrackKind::Loopback; sys.file = "track_sys.ogg"; sys.active = true;
    m.tracks = {mic, sys};
    m.mixdownFile = "mixdown.mp3";
    m.mixdownDirty = false;
    store.saveMeeting(m);
    const QDir folder(m.folder);
    writeFile(folder.filePath("track_sys.ogg"), "sys");
    writeFile(folder.filePath("mixdown.mp3"), "KEVEREK");
    writeFile(folder.filePath("transcript.md"), "# átirat");
    writeFile(folder.filePath("regi-mic.ogg"), "mic");
    writeFile(m_dir->filePath("kulso/jegyzet.txt"), "nem hang");

    library::TrackCatalog cat(&store);
    QSignalSpy changed(&cat, &library::TrackCatalog::tracksChanged);
    QString err;
    QVERIFY(!cat.relocateTrack(m.id, "mic", folder.filePath("mixdown.mp3"), &err));      // the mixdown
    QVERIFY(err.contains("lekeverés"));
    QVERIFY(!cat.relocateTrack(m.id, "mic", folder.filePath("track_sys.ogg"), &err));    // another track's file
    QVERIFY(!cat.relocateTrack(m.id, "mic", folder.filePath("transcript.md"), &err));    // not an audio file
    QVERIFY(err.contains("nem hangfájl"));
    QVERIFY(!cat.relocateTrack(m.id, "mic", folder.filePath("meeting.json"), &err));
    QVERIFY(!cat.relocateTrack(m.id, "mic", m_dir->filePath("kulso/jegyzet.txt"), &err));
    QCOMPARE(changed.count(), 0);
    const Meeting same = store.load(m.id);
    QCOMPARE(same.tracks.first().file, QStringLiteral("track_mic.ogg"));
    QVERIFY(!same.mixdownDirty);
    QVERIFY(!folder.exists("track_mic.txt"));   // copied nothing in

    // A real audio file in the folder is still selectable.
    QVERIFY2(cat.relocateTrack(m.id, "mic", folder.filePath("regi-mic.ogg"), &err), qPrintable(err));
    QCOMPARE(store.load(m.id).tracks.first().file, QStringLiteral("regi-mic.ogg"));
}

void DataSafetyTest::deletingTracksNeverDeletesMixdown()
{
    // An old meeting.json (made before the fix): a dropped track points to the mixdown file.
    library::MeetingRepository store(m_audio, m_meta);
    Meeting m = store.createMeeting("Régi hiba");
    Track mic; mic.id = "mic"; mic.kind = TrackKind::Mic; mic.file = "track_mic.ogg"; mic.active = true;
    Track bad; bad.id = "sys"; bad.kind = TrackKind::Loopback; bad.file = "mixdown.mp3"; bad.active = false;
    Track dropped; dropped.id = "d"; dropped.kind = TrackKind::Loopback; dropped.file = "track_d.ogg"; dropped.active = false;
    m.tracks = {mic, bad, dropped};
    m.mixdownFile = "mixdown.mp3";
    store.saveMeeting(m);
    const QDir folder(m.folder);
    writeFile(folder.filePath("track_mic.ogg"), "mic");
    writeFile(folder.filePath("track_d.ogg"), "d");
    writeFile(folder.filePath("mixdown.mp3"), "KEVEREK");

    library::TrackCatalog cat(&store);
    QCOMPARE(cat.deleteDroppedTracks(m.id), 2);
    QCOMPARE(store.load(m.id).tracks.size(), 1);
    QVERIFY(!folder.exists("track_d.ogg"));                                   // the real dropped track was deleted
    QCOMPARE(readFile(folder.filePath("mixdown.mp3")), QByteArray("KEVEREK")); // the mix was not
    QVERIFY(folder.exists("track_mic.ogg"));
}

// Every AppPaths role and the secret backend stay inside TANARA_HOME, whatever the "paths"
// settings section or the secrets backend says (a copied real settings.json must not leak out).
void DataSafetyTest::sandboxRolesStayInside()
{
    const QString home = m_dir->filePath("home");
    paths::PathSettings out;
    out.dataDir = QStringLiteral("~/.tanara");
    out.modelsDir = QStringLiteral("~/.tanara/models");
    out.notesDir = QStringLiteral("~/Tanara/notes");
    out.setPrimaryRecordingRoot(QStringLiteral("~/Tanara/recordings"));
    const paths::AppPaths p = paths::AppPaths::system(out);
    for (paths::Role r : {paths::Role::Config, paths::Role::Data, paths::Role::State, paths::Role::Logs,
                          paths::Role::Models, paths::Role::Recordings, paths::Role::Notes})
        QVERIFY2(p.dir(r) == home || p.dir(r).startsWith(home + QLatin1Char('/')), qPrintable(paths::roleName(r)));
    QVERIFY(recordingLockPath().startsWith(home + QLatin1Char('/')));
    QVERIFY(watcherLockPath().startsWith(home + QLatin1Char('/')));
    // The keychain is never reached in a sandbox, even if asked for explicitly.
    const secrets::OpenResult r = secrets::open(SecretSettings{QStringLiteral("keychain")},
                                                p.dataFile(paths::files::Secrets), {QStringLiteral("soniox.apiKey")});
    QCOMPARE(r.store->backendName(), QStringLiteral("file"));
    QVERIFY(!secrets::keychainAllowed());
}

QTEST_GUILESS_MAIN(DataSafetyTest)
#include "test_data_safety.moc"
