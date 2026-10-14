// tanara-cli migrate person-ids (P11): the one-time converter of the old, NAME-keyed person data
// to the PersonId formats. The real CLI (QProcess) runs on a MADE-UP old-format sandbox
// (isolated TANARA_HOME: people.json v2, voiceprints.json without a version, tags.json v1, one
// meeting folder with an old meeting.json, transcript.speakers.json v2, participants.analysis.json
// and a transcript-backup-* copy). Checks:
//  - --dry-run reports and writes NOTHING (every byte of the sandbox is unchanged, no backup);
//  - the real run first makes a backup (byte-equal copies of every changed file + manifest.json),
//    then writes the v3 files: people keyed by id (nickname match, new person for an unknown
//    name, own person from the settings), voiceprints / tag rejections / meeting references by id;
//  - the core reads the result (PeopleStore not legacy, the meeting's speakerMap resolves);
//  - a second run is a no-op ("Nothing to convert"), a non-empty backup folder is refused.
#include "tanara/app/AppSettings.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/VoiceprintStore.h"

#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

using namespace tanara;

namespace {

struct Run { int code = -1; QString out; QString err; };

const char* kFolder = "2026-09-01_1000_Nordvik";

bool writeJson(const QString& path, const QJsonObject& o)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(o).toJson()) > 0;
}

QJsonObject readJson(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

QByteArray readAll(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// Every data file under `root`: relative path -> content (the log folder is not data: every
// process may append to it).
QMap<QString, QByteArray> snapshot(const QString& root)
{
    QMap<QString, QByteArray> out;
    QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        const QString rel = QDir(root).relativeFilePath(p);
        if (!rel.startsWith(QLatin1String("logs/"))) out.insert(rel, readAll(p));
    }
    return out;
}

QJsonObject print(const QString& id, double x)
{
    return QJsonObject{{"id", id}, {"model", "campplus"}, {"embedding", QJsonArray{x, 1.0 - x, 0.0}},
                       {"sourceMeetingId", "m-nordvik"}};
}

} // namespace

class MigratePersonIdsTest : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        m_home = std::make_unique<QTemporaryDir>();
        QVERIFY(m_home->isValid());
        qputenv("TANARA_HOME", m_home->path().toUtf8());
        seedOldFormat();
    }
    void cleanup() { m_home.reset(); }

    void usageErrors()
    {
        QCOMPARE(cli({"migrate"}).code, 2);
        QCOMPARE(cli({"migrate", "person-ids", "--bogus"}).code, 2);
    }

    void dryRunWritesNothing()
    {
        const QMap<QString, QByteArray> before = snapshot(m_home->path());
        const Run r = cli({"migrate", "person-ids", "--dry-run"});
        QVERIFY2(r.code == 0, qPrintable(r.err));
        QVERIFY(r.out.contains("DRY RUN - nothing is written."));
        QVERIFY(r.out.contains("people.json:     old format (version 2), 3 people"));
        QVERIFY2(r.out.contains("Ismeretlen Ernő"), qPrintable(r.out));      // unmatched name listed
        QVERIFY(r.out.contains("Files to write: 7"));
        QCOMPARE(snapshot(m_home->path()), before);                          // not a byte changed
        QVERIFY(QDir(m_home->path()).entryList({"backup-person-ids-*"}, QDir::Dirs).isEmpty());
        // The app itself refuses the old file (read-only, empty) until the conversion.
        library::PeopleStore legacy(QDir(m_home->path()).filePath("people.json"));
        QVERIFY(legacy.legacy());
        QVERIFY(legacy.people().isEmpty());
    }

    void convertsWithVerifiedBackupAndIsIdempotent()
    {
        const QMap<QString, QByteArray> before = snapshot(m_home->path());
        QTemporaryDir backupParent;
        const QString backup = QDir(backupParent.path()).filePath("mentes");
        const Run r = cli({"migrate", "person-ids", "--backup-dir", backup});
        QVERIFY2(r.code == 0, qPrintable(r.out + r.err));
        QVERIFY(r.out.contains("written: 7 files."));

        // ---- the backup: manifest + byte-equal copies of the originals ----
        const QJsonObject manifest = readJson(QDir(backup).filePath("manifest.json"));
        QCOMPARE(manifest.value("command").toString(), QStringLiteral("migrate person-ids"));
        const QJsonArray files = manifest.value("files").toArray();
        QCOMPARE(files.size(), 7);
        for (const QJsonValue& v : files) {
            const QString original = v.toObject().value("original").toString();
            const QString rel = QDir(m_home->path()).relativeFilePath(original);
            QVERIFY2(before.contains(rel), qPrintable(rel));
            QCOMPARE(readAll(QDir(backup).filePath(v.toObject().value("backup").toString())), before.value(rel));
        }

        // ---- people.json v3 ----
        const QJsonObject people = readJson(QDir(m_home->path()).filePath("people.json"));
        QCOMPARE(people.value("version").toInt(), 3);
        const QJsonObject ps = people.value("people").toObject();
        QCOMPARE(ps.size(), 5);   // 3 old + Tóth Bence (voiceprint owner) + Ismeretlen Ernő (meeting)
        QHash<QString, QString> idOf;
        for (auto it = ps.constBegin(); it != ps.constEnd(); ++it)
            idOf.insert(it.value().toObject().value("name").toString(), it.key());
        for (const QString& n : {"Kovács Lilla", "Fehér Gábor", "Varga Árpád", "Tóth Bence", "Ismeretlen Ernő"})
            QVERIFY2(PersonId::fromString(idOf.value(n)).toString().size() == 36, qPrintable(n));
        const QString lilla = idOf.value("Kovács Lilla"), gabor = idOf.value("Fehér Gábor"),
                      arpad = idOf.value("Varga Árpád"), bence = idOf.value("Tóth Bence"),
                      erno = idOf.value("Ismeretlen Ernő");
        QCOMPARE(people.value("self").toString(), lilla);                      // the settings' name
        const QJsonObject g = ps.value(gabor).toObject();
        QCOMPARE(g.value("aliases").toArray(), QJsonArray{"Gabi"});
        QCOMPARE(g.value("note").toString(), QStringLiteral("Partnercég"));
        QCOMPARE(g.value("tags").toArray(), QJsonArray{"t-nord"});
        QCOMPARE(g.value("defaultSide").toString(), QStringLiteral("remote"));
        QCOMPARE(people.value("custom").toString(), QStringLiteral("megmarad"));   // unknown root field kept

        // ---- voiceprints.json v3 ----
        const QJsonObject vp = readJson(QDir(m_home->path()).filePath("voiceprints.json"));
        QCOMPARE(vp.value("version").toInt(), 3);
        QCOMPARE(vp.value("people").toObject().value(gabor).toArray().size(), 2);
        QCOMPARE(vp.value("people").toObject().value(bence).toArray().size(), 1);

        // ---- tags.json v2 ----
        const QJsonObject tags = readJson(QDir(m_home->path()).filePath("tags.json"));
        QCOMPARE(tags.value("version").toInt(), 2);
        const QJsonArray rej = tags.value("rejected").toArray();
        QCOMPARE(rej.size(), 1);                                             // the unknown person's is dropped
        QCOMPARE(rej.at(0).toObject().value("personId").toString(), gabor);  // via the nickname "Gabi"
        QVERIFY(!rej.at(0).toObject().contains("person"));

        // ---- the meeting ----
        const QString folder = QDir(m_home->path()).filePath(QStringLiteral("recordings/") + kFolder);
        const QJsonObject mj = readJson(QDir(folder).filePath("meeting.json"));
        QCOMPARE(mj.value("version").toInt(), 3);
        const QJsonObject sm = mj.value("speakerMap").toObject();
        QCOMPARE(sm.value("Beszélő 1").toString(), lilla);
        QCOMPARE(sm.value("Beszélő 2").toString(), gabor);                   // "Gabi" -> Gábor
        QCOMPARE(sm.value("Beszélő 3").toString(), erno);
        QVERIFY(!sm.contains("Beszélő 4"));                                  // raw -> itself: anonymous
        QCOMPARE(mj.value("participants").toArray().at(0).toObject().value("personId").toString(), arpad);
        QVERIFY(!mj.value("participants").toArray().at(0).toObject().contains("personName"));
        QCOMPARE(mj.value("participantApproval").toObject().value("candidates").toArray().at(0).toObject()
                     .value("personId").toString(), arpad);
        const QJsonObject ov = readJson(QDir(folder).filePath("transcript.speakers.json"));
        QCOMPARE(ov.value("version").toInt(), 3);
        QCOMPARE(ov.value("participants").toArray().at(0).toObject().value("personId").toString(), bence);
        QCOMPARE(ov.value("identified").toObject().value("Beszélő 2").toObject().value("personId").toString(), gabor);
        const QJsonObject an = readJson(QDir(folder).filePath("participants.analysis.json"));
        QCOMPARE(an.value("clusters").toArray().at(0).toObject().value("matchPersonId").toString(), gabor);
        const QJsonObject bk = readJson(QDir(folder).filePath("transcript-backup-20260901-1100/speakerMap.json"));
        QCOMPARE(bk.value("speakerMap").toObject().value("Beszélő 2").toString(), arpad);
        // Audio and the transcript text are not touched.
        const QString segRel = QStringLiteral("recordings/") + kFolder + QStringLiteral("/transcript.segments.json");
        QCOMPARE(readAll(QDir(m_home->path()).filePath(segRel)), before.value(segRel));

        // ---- the core reads the result ----
        library::PeopleStore store(QDir(m_home->path()).filePath("people.json"));
        QVERIFY(!store.legacy());
        QCOMPARE(store.displayName(PersonId::fromString(gabor)), QStringLiteral("Fehér Gábor"));
        QCOMPARE(store.selfId(), PersonId::fromString(lilla));
        library::VoiceprintStore prints(QDir(m_home->path()).filePath("voiceprints.json"));
        QCOMPARE(prints.printCount(PersonId::fromString(gabor)), 2);
        const Meeting m = library::MeetingRepository::readMeetingFolder(folder);
        QCOMPARE(m.speakerMap.value("Beszélő 2"), PersonId::fromString(gabor));

        // ---- idempotent: a second run has nothing to do ----
        const QMap<QString, QByteArray> after = snapshot(m_home->path());
        const Run again = cli({"migrate", "person-ids"});
        QVERIFY2(again.code == 0, qPrintable(again.err));
        QVERIFY(again.out.contains("Nothing to convert"));
        QCOMPARE(snapshot(m_home->path()), after);
    }

    void refusesNonEmptyBackupFolder()
    {
        const QMap<QString, QByteArray> before = snapshot(m_home->path());
        QTemporaryDir backup;
        QVERIFY(writeJson(QDir(backup.path()).filePath("valami.json"), QJsonObject{{"x", 1}}));
        const Run r = cli({"migrate", "person-ids", "--backup-dir", backup.path()});
        QCOMPARE(r.code, 1);
        QVERIFY(r.err.contains("the backup folder is not empty"));
        QCOMPARE(snapshot(m_home->path()), before);
    }

    void defaultBackupInDataFolder()
    {
        const Run r = cli({"migrate", "person-ids"});
        QVERIFY2(r.code == 0, qPrintable(r.err));
        const QStringList dirs = QDir(m_home->path()).entryList({"backup-person-ids-*"}, QDir::Dirs);
        QCOMPARE(dirs.size(), 1);
        QVERIFY(QFileInfo::exists(QDir(m_home->path()).filePath(dirs.first() + "/manifest.json")));
        QVERIFY(QFileInfo::exists(QDir(m_home->path()).filePath(dirs.first() + "/data/people.json")));
    }

private:
    // The made-up OLD data (a name-keyed sandbox, as before P11).
    void seedOldFormat()
    {
        {
            app::SettingsManager sm;
            app::AppSettings s = sm.settings();
            s.recording.userSpeakerName = QStringLiteral("Kovács Lilla");
            sm.setSettings(s);
        }
        const QDir home(m_home->path());
        QVERIFY(writeJson(home.filePath("people.json"), QJsonObject{
            {"version", 2}, {"custom", "megmarad"},
            {"people", QJsonArray{
                QJsonObject{{"name", "Kovács Lilla"}},
                QJsonObject{{"name", "Fehér Gábor"}, {"aliases", QJsonArray{"Gabi"}}, {"note", "Partnercég"},
                            {"tags", QJsonArray{"t-nord"}}, {"defaultSide", "remote"}}}},
            {"unlisted", QJsonArray{QJsonObject{{"name", "Varga Árpád"}}}}}));
        QVERIFY(writeJson(home.filePath("voiceprints.json"), QJsonObject{
            {"people", QJsonArray{
                QJsonObject{{"name", "Fehér Gábor"}, {"prints", QJsonArray{print("vp1", 1.0), print("vp2", 0.9)}}},
                QJsonObject{{"name", "Tóth Bence"}, {"prints", QJsonArray{print("vp3", 0.2)}}}}}}));
        QVERIFY(writeJson(home.filePath("tags.json"), QJsonObject{
            {"version", 1},
            {"tags", QJsonArray{QJsonObject{{"id", "t-nord"}, {"name", "Nordvik"}}}},
            {"rejected", QJsonArray{QJsonObject{{"tagId", "t-nord"}, {"meetingId", "m-nordvik"}, {"person", "Gabi"}},
                                    QJsonObject{{"tagId", "t-nord"}, {"meetingId", "m-nordvik"}, {"person", "Senki"}}}}}));

        const QString folder = home.filePath(QStringLiteral("recordings/") + kFolder);
        QVERIFY(writeJson(QDir(folder).filePath("meeting.json"), QJsonObject{
            {"id", "m-nordvik"}, {"title", "Nordvik"},
            {"speakerMap", QJsonObject{{"Beszélő 1", "Kovács Lilla"}, {"Beszélő 2", "Gabi"},
                                       {"Beszélő 3", "Ismeretlen Ernő"}, {"Beszélő 4", "Beszélő 4"}}},
            {"participants", QJsonArray{QJsonObject{{"id", "c1"}, {"personName", "Varga Árpád"},
                                                    {"source", "calendar"}}}},
            {"participantApproval", QJsonObject{{"at", "2026-09-01T11:00:00"},
                {"candidates", QJsonArray{QJsonObject{{"participantId", "c1"}, {"personName", "Varga Árpád"},
                                                      {"checked", true}}}}}}}));
        QFile seg(QDir(folder).filePath("transcript.segments.json"));
        QVERIFY(seg.open(QIODevice::WriteOnly));
        seg.write(R"([{"startMs":0,"endMs":4000,"speaker":"Beszélő 1","text":"Szia"}])");
        seg.close();
        QVERIFY(writeJson(QDir(folder).filePath("transcript.speakers.json"), QJsonObject{
            {"version", 2},
            {"participants", QJsonArray{QJsonObject{{"key", "p:1"}, {"person", "Tóth Bence"}}}},
            {"identified", QJsonObject{{"Beszélő 2", QJsonObject{{"person", "Fehér Gábor"}, {"score", 0.8}}},
                                       {"Beszélő 3", QJsonObject{{"person", "Nincs Ilyen"}, {"score", 0.7}}}}}}));
        QVERIFY(writeJson(QDir(folder).filePath("participants.analysis.json"), QJsonObject{
            {"clusters", QJsonArray{QJsonObject{{"id", "k1"}, {"matchName", "Fehér Gábor"}}}}}));
        QVERIFY(writeJson(QDir(folder).filePath("transcript-backup-20260901-1100/speakerMap.json"), QJsonObject{
            {"speakerMap", QJsonObject{{"Beszélő 2", "Varga Árpád"}}}}));
    }

    Run cli(const QStringList& args)
    {
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("TANARA_HOME", m_home->path());
        env.insert("TANARA_CLOUD", "off");
        p.setProcessEnvironment(env);
        p.start(QStringLiteral(TANARA_CLI_EXE), args);
        Run r;
        if (!p.waitForFinished(120000)) { p.kill(); return r; }
        r.code = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
        r.out = QString::fromUtf8(p.readAllStandardOutput());
        r.err = QString::fromUtf8(p.readAllStandardError());
        return r;
    }

    std::unique_ptr<QTemporaryDir> m_home;
};

QTEST_GUILESS_MAIN(MigratePersonIdsTest)
#include "test_migrate_person_ids.moc"
