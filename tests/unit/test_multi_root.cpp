// Several recording roots + moving a root (library::MeetingRepository), in temporary folders with
// made-up meetings (no audio is decoded):
//  - the index lists every root; a new meeting goes into the primary; the id is root-independent;
//  - index.db stores (root, dir), no absolute folder; an old-schema index is rebuilt from disk;
//  - move-root by rename (same file system) and by copy+verify+delete; the index follows;
//  - cancel between folders, then running it again continues; a complete copy left by an
//    interrupted run is finished, a partial ".move-*" copy is discarded;
//  - the folder of a live recording (recording.lock) stays in place;
//  - free-space / nested-root refusals; AppPaths keeps sandbox-internal roots under TANARA_HOME.
#include <QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>

#include "tanara/foundation/ProcessLock.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/library/MeetingLayout.h"
#include "tanara/library/store/MeetingRepository.h"

using namespace tanara;
using library::MeetingLayout;
using library::MeetingRepository;

namespace {

// A finished meeting folder directly under root (meeting.json + a fake track + a transcript file).
Meeting makeMeeting(const QString& root, const QString& name, const QString& id)
{
    Meeting m;
    m.id = id;
    m.title = name;
    m.folder = QDir(root).filePath(name);
    m.startedAt = QDateTime::fromString(QStringLiteral("2026-10-01T09:00:00"), Qt::ISODate).addSecs(id.size() * 60);
    Track t;
    t.id = QStringLiteral("mic");
    t.file = QStringLiteral("track_mic.ogg");
    m.tracks << t;
    MeetingRepository::writeMeetingFile(m);
    QFile track(QDir(m.folder).filePath(t.file));
    if (track.open(QIODevice::WriteOnly)) track.write(QByteArray(4096, 'a'));
    QFile md(MeetingLayout::path(m.folder, MeetingLayout::transcriptMd()));
    if (md.open(QIODevice::WriteOnly)) md.write("# x\n");
    return m;
}

QStringList folderNames(const QVector<Meeting>& ms)
{
    QStringList out;
    for (const Meeting& m : ms) out << QFileInfo(m.folder).fileName();
    out.sort();
    return out;
}

} // namespace

class MultiRootTest : public QObject {
    Q_OBJECT
private slots:
    void listsEveryRoot()
    {
        QTemporaryDir dir;
        const QString r1 = dir.filePath(QStringLiteral("r1")), r2 = dir.filePath(QStringLiteral("r2"));
        QDir().mkpath(r2);
        makeMeeting(r1, QStringLiteral("a"), QStringLiteral("id-a"));
        makeMeeting(r2, QStringLiteral("b"), QStringLiteral("id-b"));
        MeetingRepository repo({r1, r2}, dir.filePath(QStringLiteral("meta")));
        QCOMPARE(repo.primaryRoot(), QDir::cleanPath(r1));
        repo.reindex();
        QCOMPARE(folderNames(repo.loadAll()), (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
        QCOMPARE(repo.load(QStringLiteral("id-b")).folder, QDir(r2).filePath(QStringLiteral("b")));
        QCOMPARE(MeetingRepository::meetingFolderCount(r2), 1);

        // A new meeting goes into the primary root.
        const Meeting n = repo.createMeeting(QStringLiteral("Új"));
        QCOMPARE(MeetingRepository::rootOf(n.folder), QDir::cleanPath(r1));
        QCOMPARE(repo.loadAll().size(), 3);

        // Adopting a folder that is already in a further root keeps it in place.
        makeMeeting(r2, QStringLiteral("c"), QStringLiteral("id-c"));
        QString error;
        const Meeting c = repo.adoptMeetingFolder(QDir(r2).filePath(QStringLiteral("c")), &error);
        QVERIFY2(!c.id.isEmpty(), qPrintable(error));
        QCOMPARE(c.folder, QDir(r2).filePath(QStringLiteral("c")));
    }

    void indexStoresRootAndDir()
    {
        QTemporaryDir dir;
        const QString r1 = dir.filePath(QStringLiteral("r1")), meta = dir.filePath(QStringLiteral("meta"));
        makeMeeting(r1, QStringLiteral("a"), QStringLiteral("id-a"));
        {
            MeetingRepository repo(r1, meta);
            repo.reindex();
        }
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("probe"));
        db.setDatabaseName(QDir(meta).filePath(paths::files::IndexDb));
        QVERIFY(db.open());
        {
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("PRAGMA table_info(meetings)")));
            QStringList cols;
            while (q.next()) cols << q.value(1).toString();
            QVERIFY(cols.contains(QStringLiteral("root")));
            QVERIFY(cols.contains(QStringLiteral("dir")));
            QVERIFY(!cols.contains(QStringLiteral("folder")));
            QSqlQuery r(db);
            QVERIFY(r.exec(QStringLiteral("SELECT root, dir FROM meetings")) && r.next());
            QCOMPARE(r.value(0).toString(), QDir::cleanPath(r1));
            QCOMPARE(r.value(1).toString(), QStringLiteral("a"));
        }
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(QStringLiteral("probe"));
    }

    void oldSchemaIsRebuilt()
    {
        QTemporaryDir dir;
        const QString r1 = dir.filePath(QStringLiteral("r1")), meta = dir.filePath(QStringLiteral("meta"));
        makeMeeting(r1, QStringLiteral("a"), QStringLiteral("id-a"));
        QDir().mkpath(meta);
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("old"));
            db.setDatabaseName(QDir(meta).filePath(paths::files::IndexDb));
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("CREATE TABLE meetings (id TEXT PRIMARY KEY, title TEXT, folder TEXT,"
                                          " startedAt TEXT, durationMs INTEGER, hasTranscript INTEGER, hasSummary INTEGER)")));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO meetings VALUES ('stale', 'x', '/nowhere/x', '', 0, 0, 0)")));
            QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 1")));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("old"));
        MeetingRepository repo(r1, meta);
        const QVector<Meeting> all = repo.loadAll();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all.first().id, QStringLiteral("id-a"));
    }

    void moveByRename()
    {
        QTemporaryDir dir;
        const QString r1 = dir.filePath(QStringLiteral("r1")), r2 = dir.filePath(QStringLiteral("r2")),
                      r3 = dir.filePath(QStringLiteral("r3"));
        QDir().mkpath(r2);
        makeMeeting(r2, QStringLiteral("a"), QStringLiteral("id-a"));
        makeMeeting(r2, QStringLiteral("b"), QStringLiteral("id-b"));
        QDir().mkpath(QDir(r2).filePath(QStringLiteral("not-a-meeting")));   // left alone
        MeetingRepository repo({r1, r2}, dir.filePath(QStringLiteral("meta")));
        repo.reindex();

        const MeetingRepository::MovePlan plan = repo.planMove(r2, r3);
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
        QVERIFY(plan.sameFileSystem);
        QCOMPARE(plan.folders, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));

        QSignalSpy updated(&repo, &MeetingRepository::meetingUpdated);
        const MeetingRepository::MoveResult res = repo.moveMeetings(r2, r3);
        QVERIFY2(res.ok(), qPrintable(res.failed.join(QLatin1Char('\n')) + res.error));
        QCOMPARE(res.moved.size(), 2);
        QCOMPARE(updated.count(), 2);
        QCOMPARE(repo.load(QStringLiteral("id-a")).folder, QDir(r3).filePath(QStringLiteral("a")));
        QVERIFY(QFile::exists(QDir(r3).filePath(QStringLiteral("b/track_mic.ogg"))));
        QCOMPARE(MeetingRepository::meetingFolderCount(r2), 0);
        QVERIFY(QDir(QDir(r2).filePath(QStringLiteral("not-a-meeting"))).exists());
        // The ids and the list do not change.
        QCOMPARE(repo.loadAll().size(), 2);
    }

    void moveByCopyWithCancelAndResume()
    {
        QTemporaryDir dir;
        const QString r1 = dir.filePath(QStringLiteral("r1")), r2 = dir.filePath(QStringLiteral("r2"));
        for (const char* n : {"a", "b", "c"})
            makeMeeting(r1, QString::fromLatin1(n), QStringLiteral("id-") + QString::fromLatin1(n));
        MeetingRepository repo(r1, dir.filePath(QStringLiteral("meta")));
        repo.reindex();

        // Cancelled after the first folder.
        std::atomic<bool> cancel{false};
        MeetingRepository::MoveResult res = repo.moveMeetings(r1, r2, &cancel,
            [&cancel](int done, int, const QString&) { if (done >= 1) cancel.store(true); },
            MeetingRepository::MoveMode::Copy);
        QVERIFY(res.cancelled);
        QCOMPARE(res.moved, QStringList{QStringLiteral("a")});
        QVERIFY(!QDir(QDir(r1).filePath(QStringLiteral("a"))).exists());
        QVERIFY(QDir(QDir(r1).filePath(QStringLiteral("b"))).exists());
        QCOMPARE(repo.load(QStringLiteral("id-a")).folder, QDir(r2).filePath(QStringLiteral("a")));
        QCOMPARE(repo.load(QStringLiteral("id-b")).folder, QDir(r1).filePath(QStringLiteral("b")));

        // Leftovers of an interrupted run: a complete copy of "b" (the source not yet deleted) and a
        // partial copy of "c".
        QDir().mkpath(QDir(r2).filePath(QStringLiteral("b")));
        for (const QString& f : QDir(QDir(r1).filePath(QStringLiteral("b"))).entryList(QDir::Files))
            QVERIFY(QFile::copy(QDir(r1).filePath(QStringLiteral("b/") + f), QDir(r2).filePath(QStringLiteral("b/") + f)));
        QDir().mkpath(QDir(r2).filePath(QStringLiteral(".move-c")));
        QFile part(QDir(r2).filePath(QStringLiteral(".move-c/meeting.json")));
        QVERIFY(part.open(QIODevice::WriteOnly));
        part.write("{");
        part.close();

        res = repo.moveMeetings(r1, r2, nullptr, {}, MeetingRepository::MoveMode::Copy);
        QVERIFY2(res.ok(), qPrintable(res.failed.join(QLatin1Char('\n')) + res.error));
        QCOMPARE(res.moved, (QStringList{QStringLiteral("b"), QStringLiteral("c")}));
        QCOMPARE(MeetingRepository::meetingFolderCount(r1), 0);
        QCOMPARE(MeetingRepository::meetingFolderCount(r2), 3);
        QVERIFY(!QDir(QDir(r2).filePath(QStringLiteral(".move-c"))).exists());
        QCOMPARE(repo.load(QStringLiteral("id-c")).folder, QDir(r2).filePath(QStringLiteral("c")));
        QCOMPARE(QFileInfo(QDir(r2).filePath(QStringLiteral("c/track_mic.ogg"))).size(), qint64(4096));
    }

    void clashIsNotOverwritten()
    {
        QTemporaryDir dir;
        const QString r1 = dir.filePath(QStringLiteral("r1")), r2 = dir.filePath(QStringLiteral("r2"));
        makeMeeting(r1, QStringLiteral("a"), QStringLiteral("id-a"));
        makeMeeting(r2, QStringLiteral("a"), QStringLiteral("other"));
        QFile extra(QDir(r2).filePath(QStringLiteral("a/extra.txt")));
        QVERIFY(extra.open(QIODevice::WriteOnly));
        extra.write("x");
        extra.close();
        MeetingRepository repo(r1, dir.filePath(QStringLiteral("meta")));
        const MeetingRepository::MoveResult res = repo.moveMeetings(r1, r2);
        QCOMPARE(res.failed.size(), 1);
        QVERIFY(QDir(QDir(r1).filePath(QStringLiteral("a"))).exists());
        QVERIFY(QFile::exists(QDir(r2).filePath(QStringLiteral("a/extra.txt"))));
    }

    void liveRecordingStays()
    {
        QTemporaryDir dir;
        const QString r1 = dir.filePath(QStringLiteral("r1")), r2 = dir.filePath(QStringLiteral("r2"));
        makeMeeting(r1, QStringLiteral("a"), QStringLiteral("id-a"));
        const Meeting live = makeMeeting(r1, QStringLiteral("live"), QStringLiteral("id-live"));
        ProcessLock lock(recordingLockPath());
        QVERIFY(lock.acquire(live.folder));
        MeetingRepository repo(r1, dir.filePath(QStringLiteral("meta")));
        const MeetingRepository::MovePlan plan = repo.planMove(r1, r2);
        QCOMPARE(plan.skipped, QStringList{QStringLiteral("live")});
        const MeetingRepository::MoveResult res = repo.moveMeetings(r1, r2);
        lock.release();
        QCOMPARE(res.moved, QStringList{QStringLiteral("a")});
        QCOMPARE(res.skipped, QStringList{QStringLiteral("live")});
        QVERIFY(QDir(live.folder).exists());
    }

    void refusals()
    {
        QTemporaryDir dir;
        const QString r1 = dir.filePath(QStringLiteral("r1"));
        makeMeeting(r1, QStringLiteral("a"), QStringLiteral("id-a"));
        MeetingRepository repo(r1, dir.filePath(QStringLiteral("meta")));
        QVERIFY(!repo.planMove(r1, QDir(r1).filePath(QStringLiteral("inner"))).error.isEmpty());   // nested
        QVERIFY(!repo.planMove(r1, r1).error.isEmpty());
        QVERIFY(!repo.planMove(dir.filePath(QStringLiteral("missing")), dir.filePath(QStringLiteral("x"))).error.isEmpty());
        QVERIFY(!repo.moveMeetings(r1, r1).error.isEmpty());
        QVERIFY(QDir(QDir(r1).filePath(QStringLiteral("a"))).exists());
    }

    void sandboxKeepsInnerRoots()
    {
        paths::PlatformBase base;
        base.platform = paths::Platform::Linux;
        base.home = QStringLiteral("/home/u");
        base.documents = QStringLiteral("/home/u/Documents");
        base.configBase = QStringLiteral("/home/u/.config");
        base.dataBase = QStringLiteral("/home/u/.local/share");
        base.stateBase = QStringLiteral("/home/u/.local/state");
        paths::PathSettings s;
        s.recordingRoots = {paths::RecordingRoot{QStringLiteral("/sb/rec-a"), true},
                            paths::RecordingRoot{QStringLiteral("/home/u/Tanara/recordings"), false},   // outside: ignored
                            paths::RecordingRoot{QStringLiteral("rec-b"), false}};                       // relative to H
        const paths::AppPaths p = paths::AppPaths::resolve(base, s, QStringLiteral("/sb"));
        QCOMPARE(p.recordingRoots(), (QStringList{QStringLiteral("/sb/rec-a"), QStringLiteral("/sb/rec-b")}));
        // Without inner roots: the sandbox default.
        const paths::AppPaths d = paths::AppPaths::resolve(base, paths::PathSettings(), QStringLiteral("/sb"));
        QCOMPARE(d.recordingRoots(), QStringList{QStringLiteral("/sb/recordings")});
        // Without TANARA_HOME every configured root counts, the primary first.
        const paths::AppPaths n = paths::AppPaths::resolve(base, s, QString());
        QCOMPARE(n.recordingRoots().size(), 3);
        QCOMPARE(n.primaryRecordingRoot(), QStringLiteral("/sb/rec-a"));
    }
};

QTEST_GUILESS_MAIN(MultiRootTest)
#include "test_multi_root.moc"
