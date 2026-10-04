//
// Adatbiztonság — a fájl-alapú tárak hibatűrése (csak ideiglenes mappákban):
//  - árva-helyreállítás: élő felvétel mappáját (recording.lock / friss sáv-fájl) nem nyúlja;
//  - index: egy mappa sosem szerepel kétszer; meeting törlése nem törli más meeting mappáját;
//  - settings.json: sérült fájlt nem ír felül csendben, mentéskor félreteszi;
//  - people.json / voiceprints.json: két „folyamat” (két tár-példány) nem írja felül egymást;
//  - replaceFile: a régi fájl megmarad, ha a csere nem sikerül;
//  - relocateTrack: a lekeverés, más sáv fájlja és nem-hangfájl nem lehet sáv.
//
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>

#include "tanara/SettingsManager.h"
#include "tanara/audio/TrackCatalog.h"
#include "tanara/detect/RecordingLock.h"
#include "tanara/store/KeyStore.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/PeopleStore.h"
#include "tanara/store/SharedFile.h"
#include "tanara/store/VoiceprintStore.h"

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

// A fájl módosítási idejének visszaállítása a múltba (mintha rég nem írtak volna bele).
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

private:
    QString orphanFolder(const QString& name);

    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_audio, m_meta;
};

void DataSafetyTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    // A SettingsManager alapértelmezett mappái (felvételek, jegyzetek) is a temp mappába
    // kerüljenek — a teszt SOSEM nyúl a valódi ~/.tanara / ~/Tanara mappához.
    qputenv("TANARA_HOME", m_dir->filePath("home").toUtf8());
    m_audio = m_dir->filePath("recordings");
    m_meta = m_dir->filePath("meta");
}

void DataSafetyTest::cleanup() { m_dir.reset(); }

// Felvétel-mappa meeting.json nélkül, egy régi (nem friss) sáv-fájllal.
QString DataSafetyTest::orphanFolder(const QString& name)
{
    const QString folder = QDir(m_audio).filePath(name);
    writeFile(QDir(folder).filePath("track_mic.ogg"), "nem-valodi-ogg");
    backdate(QDir(folder).filePath("track_mic.ogg"), 3600);
    return folder;
}

void DataSafetyTest::orphanRecoverySkipsLiveLockFolder()
{
    MeetingStore store(m_audio, m_meta);
    const QString live = orphanFolder("2026-01-05_1000_Elo-felvetel");
    const QString crashed = orphanFolder("2026-01-04_0900_Osszeomlott");

    // Élő felvevő (a teszt saját PID-je él) erre a mappára: nem árva.
    RecordingLock lock(QDir(m_meta).filePath("recording.lock"));
    QVERIFY(lock.acquire(live + "/"));   // záró perjellel is ugyanaz a mappa
    QCOMPARE(store.recoverOrphanRecordings(), 1);
    QVERIFY(!QFile::exists(QDir(live).filePath("meeting.json")));
    QVERIFY(QFile::exists(QDir(crashed).filePath("meeting.json")));
    const QVector<Meeting> all = store.loadAll();
    QCOMPARE(all.size(), 1);
    QCOMPARE(QDir(all.first().folder).dirName(), QStringLiteral("2026-01-04_0900_Osszeomlott"));

    // A felvétel véget ért (lock elengedve), meeting.json továbbra sincs → most már árva.
    lock.release();
    QCOMPARE(store.recoverOrphanRecordings(), 1);
    QVERIFY(QFile::exists(QDir(live).filePath("meeting.json")));

    // Elavult lock (halott PID) nem véd: az összeomlott felvevő mappája helyreáll.
    const QString stale = orphanFolder("2026-01-06_1100_Halott-felvevo");
    writeFile(QDir(m_meta).filePath("recording.lock"),
              QJsonDocument(QJsonObject{{"pid", 2147483000}, {"meetingFolder", stale},
                                        {"startedAt", "2026-01-06T11:00:00"}}).toJson());
    QVERIFY(!RecordingLock::read(QDir(m_meta).filePath("recording.lock")).active);
    QCOMPARE(store.recoverOrphanRecordings(), 1);
    QVERIFY(QFile::exists(QDir(stale).filePath("meeting.json")));
}

void DataSafetyTest::orphanRecoverySkipsFreshTracks()
{
    MeetingStore store(m_audio, m_meta);
    const QString folder = QDir(m_audio).filePath("2026-01-05_1000_Most-irjak");
    writeFile(QDir(folder).filePath("track_mic.ogg"), "regi");
    backdate(QDir(folder).filePath("track_mic.ogg"), 3600);
    writeFile(QDir(folder).filePath("track_sys.ogg"), "epp-irodik");   // friss módosítás, lock nélkül

    QCOMPARE(store.recoverOrphanRecordings(), 0);
    QVERIFY(!QFile::exists(QDir(folder).filePath("meeting.json")));
    QVERIFY(store.loadAll().isEmpty());

    // Pár perc csend után (senki nem ír bele) már árvának számít.
    backdate(QDir(folder).filePath("track_sys.ogg"), 600);
    QCOMPARE(store.recoverOrphanRecordings(), 1);
    QCOMPARE(store.loadAll().size(), 1);
    QCOMPARE(store.load(store.loadAll().first().id).tracks.size(), 2);
}

void DataSafetyTest::indexNeverListsFolderTwice()
{
    MeetingStore analyzer(m_audio, m_meta);
    Meeting a = analyzer.createMeeting("Helyreállított");

    // Egy másik folyamat (a felvevő) ugyanebbe a mappába ÚJ id-vel írja a meeting.json-t.
    {
        MeetingStore recorder(m_audio, m_meta);
        Meeting b = a;
        b.id = "felvevo-id";
        b.title = "A valódi felvétel";
        recorder.saveMeeting(b);
    }
    QVector<Meeting> all = analyzer.loadAll();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all.first().id, QStringLiteral("felvevo-id"));

    // Régi indexben maradt kettős sor (a javítás előtti buildből): a lista így is egy sort ad,
    // azt, amelyik a mappa meeting.json-jában áll.
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "teszt-kettos-sor");
        db.setDatabaseName(QDir(m_meta).filePath("index.db"));
        QVERIFY(db.open());
        QSqlQuery q(db);
        q.prepare("INSERT INTO meetings (id, title, folder, startedAt, durationMs, hasTranscript, hasSummary)"
                  " VALUES (:id, 'Árva sor', :folder, '2099-01-01T00:00:00.000', 0, 0, 0)");
        q.bindValue(":id", a.id);
        q.bindValue(":folder", a.folder);
        QVERIFY(q.exec());
        db.close();
    }
    QSqlDatabase::removeDatabase("teszt-kettos-sor");
    all = analyzer.loadAll();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all.first().id, QStringLiteral("felvevo-id"));

    // Index-újraépítés után is egy sor.
    analyzer.rebuildIndexFromDisk();
    QCOMPARE(analyzer.loadAll().size(), 1);
}

void DataSafetyTest::deleteKeepsFolderOfAnotherMeeting()
{
    MeetingStore store(m_audio, m_meta);
    Meeting a = store.createMeeting("Helyreállított");
    writeFile(QDir(a.folder).filePath("track_mic.ogg"), "felvetel");

    // A mappa meeting.json-ja már a felvevő id-jét tartalmazza; az indexben mindkét sor él.
    Meeting b = a;
    b.id = "felvevo-id";
    writeFile(QDir(a.folder).filePath("meeting.json"),
              QJsonDocument(QJsonObject{{"id", b.id}, {"title", "A valódi felvétel"}}).toJson());
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "teszt-masik-sor");
        db.setDatabaseName(QDir(m_meta).filePath("index.db"));
        QVERIFY(db.open());
        QSqlQuery q(db);
        q.prepare("INSERT INTO meetings (id, title, folder, startedAt, durationMs, hasTranscript, hasSummary)"
                  " VALUES (:id, 'A valódi felvétel', :folder, '2026-01-01T00:00:00.000', 0, 0, 0)");
        q.bindValue(":id", b.id);
        q.bindValue(":folder", a.folder);
        QVERIFY(q.exec());
        db.close();
    }
    QSqlDatabase::removeDatabase("teszt-masik-sor");

    // A „(helyreállított)” bejegyzés törlése NEM viheti el a másik meeting mappáját.
    QSignalSpy removed(&store, &MeetingStore::meetingRemoved);
    QVERIFY(store.deleteMeeting(a.id));
    QCOMPARE(removed.count(), 1);
    QVERIFY(QDir(a.folder).exists());
    QCOMPARE(readFile(QDir(a.folder).filePath("track_mic.ogg")), QByteArray("felvetel"));
    QVector<Meeting> all = store.loadAll();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all.first().id, b.id);

    // A mappa tényleges tulajdonosának törlése viszi a mappát (a régi viselkedés).
    QVERIFY(store.deleteMeeting(b.id));
    QVERIFY(!QDir(a.folder).exists());
    QVERIFY(store.loadAll().isEmpty());
}

void DataSafetyTest::deleteKeepsLiveRecordingFolder()
{
    MeetingStore store(m_audio, m_meta);
    const Meeting a = store.createMeeting("Felvétel alatt");
    writeFile(QDir(a.folder).filePath("track_mic.ogg"), "epp-irodik");
    RecordingLock lock(QDir(m_meta).filePath("recording.lock"));
    QVERIFY(lock.acquire(a.folder));

    QVERIFY(store.deleteMeeting(a.id));   // a bejegyzés eltűnik…
    QVERIFY(store.loadAll().isEmpty());
    QVERIFY(QFile::exists(QDir(a.folder).filePath("track_mic.ogg")));   // …a felvétel nem
    lock.release();
}

void DataSafetyTest::meetingJsonWrittenAtomically()
{
    MeetingStore store(m_audio, m_meta);
    Meeting m = store.createMeeting("Atomikus");
    m.speakerMap.insert("Beszélő 1", "Ödön");
    m.hasTranscript = true;
    for (int i = 0; i < 20; ++i) {
        m.contextNote = QString("megjegyzés %1").arg(i);
        store.saveMeeting(m);
    }
    // A mappában csak a meeting.json van (nem maradt ideiglenes fájl), és érvényes JSON.
    QCOMPARE(QDir(m.folder).entryList(QDir::Files | QDir::Hidden), QStringList{"meeting.json"});
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(readFile(QDir(m.folder).filePath("meeting.json")), &err);
    QCOMPARE(err.error, QJsonParseError::NoError);
    QCOMPARE(doc.object().value("id").toString(), m.id);
    const Meeting back = store.load(m.id);
    QCOMPARE(back.contextNote, QStringLiteral("megjegyzés 19"));
    QCOMPARE(back.speakerMap.value("Beszélő 1"), QStringLiteral("Ödön"));
}

void DataSafetyTest::corruptSettingsAreNotOverwritten()
{
    QDir().mkpath(m_meta);
    const QString path = QDir(m_meta).filePath("settings.json");
    // Félbemaradt írás: csonka JSON, benne a felhasználó beállításai.
    const QByteArray broken = R"({"audioDir": "/mnt/felvetelek/tanara", "userSpeakerName": "Ádá)";
    writeFile(path, broken);

    SettingsManager sm(m_meta);
    QVERIFY(sm.loadFailed());
    QCOMPARE(sm.settings().sttProviderId, QStringLiteral("soniox"));   // defaultok a memóriában
    QCOMPARE(readFile(path), broken);                                  // a fájl ÉRINTETLEN
    // Egy másik folyamat (felvevő / figyelő) betöltése sem írja felül.
    { SettingsManager other(m_meta); QVERIFY(other.loadFailed()); }
    QCOMPARE(readFile(path), broken);
    QCOMPARE(QDir(m_meta).entryList({"settings.json*"}, QDir::Files).size(), 1);

    // Kifejezett mentés: a sérült fájl félrekerül (megmarad), az új érvényes.
    AppSettings s = sm.settings();
    s.userSpeakerName = "Lilla";
    sm.setSettings(s);
    QVERIFY(!sm.loadFailed());
    const QStringList aside = QDir(m_meta).entryList({"settings.json.corrupt-*"}, QDir::Files);
    QCOMPARE(aside.size(), 1);
    QCOMPARE(readFile(QDir(m_meta).filePath(aside.first())), broken);
    SettingsManager again(m_meta);
    QVERIFY(!again.loadFailed());
    QCOMPARE(again.settings().userSpeakerName, QStringLiteral("Lilla"));
    // A következő mentés már nem tesz félre semmit.
    again.setSettings(again.settings());
    QCOMPARE(QDir(m_meta).entryList({"settings.json.corrupt-*"}, QDir::Files).size(), 1);
}

void DataSafetyTest::healthySettingsRoundTrip()
{
    // Nincs fájl → létrejön a defaultokkal (első indítás), mint eddig.
    SettingsManager first(m_meta);
    QVERIFY(!first.loadFailed());
    const QString path = QDir(m_meta).filePath("settings.json");
    QVERIFY(QFile::exists(path));
    AppSettings s = first.settings();
    s.userSpeakerName = "Ödön";
    s.languageHints = QStringList{"hu", "en"};
    first.setSettings(s);
    // Csak a settings.json marad (nincs ideiglenes / félretett fájl), és visszaolvasható.
    QCOMPARE(QDir(m_meta).entryList({"settings.json*"}, QDir::Files | QDir::Hidden), QStringList{"settings.json"});
    SettingsManager second(m_meta);
    QCOMPARE(second.settings().userSpeakerName, QStringLiteral("Ödön"));
    QCOMPARE(second.settings().languageHints, QStringList({"hu", "en"}));
}

void DataSafetyTest::secretsKeepPermissionsAndContent()
{
    const QString path = QDir(m_meta).filePath("secrets.json");
    KeyStore ks(path);
    ks.set("soniox.apiKey", "titok-1");
    ks.set("llm.apiKey", "titok-2");
    KeyStore back(path);
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
    // Két „folyamat”: mindkettő induláskor betölt, aztán felváltva módosít.
    PeopleStore analyzer(path);
    PeopleStore recorder(path);
    analyzer.add("Bence");
    recorder.add("Lilla");          // a javítás előtt ez eldobta Bencét
    analyzer.add("Zsófi");          // …ez pedig Lillát
    QCOMPARE(PeopleStore(path).names(), QStringList({"Bence", "Lilla", "Zsófi"}));

    // Törlés / átnevezés a másik folyamat friss állapotára épül, és nem támaszt fel törölt nevet.
    recorder.remove("Bence");
    analyzer.rename("Lilla", "Kovács Lilla");
    QCOMPARE(PeopleStore(path).names(), QStringList({"Kovács Lilla", "Zsófi"}));
    QCOMPARE(analyzer.names(), QStringList({"Kovács Lilla", "Zsófi"}));
    // Két rekord a fájlban, és nem marad zár-fájl.
    QCOMPARE(QJsonDocument::fromJson(readFile(path)).object().value("people").toArray().size(), 2);
    QCOMPARE(QDir(m_meta).entryList(QDir::Files | QDir::Hidden), QStringList{"people.json"});
}

void DataSafetyTest::voiceprintsFromTwoProcessesMerge()
{
    const QString path = QDir(m_meta).filePath("voiceprints.json");
    VoiceprintStore analyzer(path);
    VoiceprintStore recorder(path);
    analyzer.addPrint("Bence", print(1, 0));
    recorder.addPrint("Lilla", print(0, 1));        // a javítás előtt ez eldobta Bence lenyomatát
    analyzer.addPrint("Bence", print(0.9f, 0.1f));  // …ez pedig Lilláét
    {
        VoiceprintStore fresh(path);
        QCOMPARE(fresh.people(), QStringList({"Bence", "Lilla"}));
        QCOMPARE(fresh.printCount("Bence"), 2);
        QCOMPARE(fresh.printCount("Lilla"), 1);
    }
    // A másik folyamat törlése megmarad: a következő írás nem támasztja fel.
    recorder.removePerson("Bence");
    analyzer.addPrint("Zsófi", print(0.5f, 0.5f));
    VoiceprintStore fresh(path);
    QCOMPARE(fresh.people(), QStringList({"Lilla", "Zsófi"}));
    QCOMPARE(fresh.totalPrintCount(), 2);
    QCOMPARE(analyzer.totalPrintCount(), 2);
    QCOMPARE(QDir(m_meta).entryList(QDir::Files | QDir::Hidden), QStringList{"voiceprints.json"});
}

void DataSafetyTest::corruptVoiceprintsAreSetAside()
{
    const QString path = QDir(m_meta).filePath("voiceprints.json");
    const QByteArray broken = R"({"people":[{"name":"Ödön","prints":[{"id":"a","embedding":[0.1,0.)";
    writeFile(path, broken);
    VoiceprintStore store(path);
    QCOMPARE(store.totalPrintCount(), 0);
    QCOMPARE(readFile(path), broken);            // betöltéskor nem nyúl hozzá
    store.addPrint("Lilla", print(0, 1));
    const QStringList aside = QDir(m_meta).entryList({"voiceprints.json.corrupt-*"}, QDir::Files);
    QCOMPARE(aside.size(), 1);                   // a sérült fájl megmaradt (kézzel menthető)
    QCOMPARE(readFile(QDir(m_meta).filePath(aside.first())), broken);
    QCOMPARE(VoiceprintStore(path).people(), QStringList{"Lilla"});
}

void DataSafetyTest::replaceFileKeepsOldOnFailure()
{
    QDir().mkpath(m_meta);
    const QString out = QDir(m_meta).filePath("mixdown.mp3");
    const QString part = QDir(m_meta).filePath("mixdown.part.mp3");
    writeFile(out, "REGI-KEVEREK");

    // A csere nem tud megtörténni (nincs forrás) → a régi fájl ÉRINTETLEN. (A korábbi
    // „töröld, aztán nevezd át” itt a régit már kitörölte volna.)
    QVERIFY(!replaceFile(part, out));
    QCOMPARE(readFile(out), QByteArray("REGI-KEVEREK"));

    // Sikeres csere: az új tartalom a helyén, félkész / félretett fájl nem marad.
    writeFile(part, "UJ-KEVEREK");
    QVERIFY(replaceFile(part, out));
    QCOMPARE(readFile(out), QByteArray("UJ-KEVEREK"));
    QCOMPARE(QDir(m_meta).entryList(QDir::Files | QDir::Hidden), QStringList{"mixdown.mp3"});

    // Régi fájl nélkül is működik (első keverés).
    QVERIFY(QFile::remove(out));
    writeFile(part, "ELSO");
    QVERIFY(replaceFile(part, out));
    QCOMPARE(readFile(out), QByteArray("ELSO"));
}

void DataSafetyTest::relocateRejectsMixdownAndNonAudio()
{
    MeetingStore store(m_audio, m_meta);
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

    TrackCatalog cat(&store);
    QSignalSpy changed(&cat, &TrackCatalog::tracksChanged);
    QString err;
    QVERIFY(!cat.relocateTrack(m.id, "mic", folder.filePath("mixdown.mp3"), &err));      // a lekeverés
    QVERIFY(err.contains("lekeverés"));
    QVERIFY(!cat.relocateTrack(m.id, "mic", folder.filePath("track_sys.ogg"), &err));    // másik sáv fájlja
    QVERIFY(!cat.relocateTrack(m.id, "mic", folder.filePath("transcript.md"), &err));    // nem hangfájl
    QVERIFY(err.contains("nem hangfájl"));
    QVERIFY(!cat.relocateTrack(m.id, "mic", folder.filePath("meeting.json"), &err));
    QVERIFY(!cat.relocateTrack(m.id, "mic", m_dir->filePath("kulso/jegyzet.txt"), &err));
    QCOMPARE(changed.count(), 0);
    const Meeting same = store.load(m.id);
    QCOMPARE(same.tracks.first().file, QStringLiteral("track_mic.ogg"));
    QVERIFY(!same.mixdownDirty);
    QVERIFY(!folder.exists("track_mic.txt"));   // semmit nem másolt be

    // Valódi hangfájl a mappában továbbra is választható.
    QVERIFY2(cat.relocateTrack(m.id, "mic", folder.filePath("regi-mic.ogg"), &err), qPrintable(err));
    QCOMPARE(store.load(m.id).tracks.first().file, QStringLiteral("regi-mic.ogg"));
}

void DataSafetyTest::deletingTracksNeverDeletesMixdown()
{
    // Régi (a javítás előtt készült) meeting.json: egy eldobott sáv a lekeverés fájljára mutat.
    MeetingStore store(m_audio, m_meta);
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

    TrackCatalog cat(&store);
    QCOMPARE(cat.deleteDroppedTracks(m.id), 2);
    QCOMPARE(store.load(m.id).tracks.size(), 1);
    QVERIFY(!folder.exists("track_d.ogg"));                                   // a valódi eldobott sáv törlődött
    QCOMPARE(readFile(folder.filePath("mixdown.mp3")), QByteArray("KEVEREK")); // a keverék nem
    QVERIFY(folder.exists("track_mic.ogg"));
}

QTEST_GUILESS_MAIN(DataSafetyTest)
#include "test_data_safety.moc"
