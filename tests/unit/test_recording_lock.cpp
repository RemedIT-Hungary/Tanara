// Tanara: RecordingLock unit tests.
//
// The lock (~/.tanara/recording.lock) shows whether a recording is in progress: the recorder
// takes it, the watcher reads it. A stale lock (after a crash, a dead PID) counts as inactive.
// A separate test exe (tests/CMakeLists.txt GLOB), its own main, so QTEST_GUILESS_MAIN.
#include <QtTest>
#include <QObject>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>

#include "tanara/foundation/ProcessLock.h"

using namespace tanara;

class RecordingLockTests : public QObject {
    Q_OBJECT
private slots:

    // After acquire() the lock is active (our own PID is alive), the folder and PID can be read back.
    void acquireThenReadActive()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("recording.lock"));
        RecordingLock lock(path);
        QVERIFY(lock.acquire(QStringLiteral("/tmp/meetingX")));

        const RecordingLock::Info info = RecordingLock::read(path);
        QVERIFY(info.active);
        QCOMPARE(info.pid, QCoreApplication::applicationPid());
        QCOMPARE(info.meetingFolder, QStringLiteral("/tmp/meetingX"));
    }

    // release() deletes the lock: no active recording.
    void releaseRemovesLock()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("recording.lock"));
        RecordingLock lock(path);
        lock.acquire(QStringLiteral("/tmp/m"));
        lock.release();
        QVERIFY(!QFile::exists(path));
        QVERIFY(!RecordingLock::read(path).active);
    }

    // A stale lock (a non-existent PID) is inactive (an orphan lock after a crash does not block).
    void staleLockIsInactive()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("recording.lock"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArrayLiteral(
            "{\"pid\":2000000000,\"meetingFolder\":\"/tmp/dead\",\"startedAt\":\"2026-01-01T00:00:00\"}"));
        f.close();

        const RecordingLock::Info info = RecordingLock::read(path);
        QVERIFY(!info.active);
        QCOMPARE(info.pid, qint64(2000000000));
    }

    // No lock file means no active recording (does not throw or crash).
    void missingFileIsInactive()
    {
        QTemporaryDir dir;
        QVERIFY(!RecordingLock::read(dir.filePath(QStringLiteral("nope.lock"))).active);
    }

    // watcherRunning(): the watcher's lock (in the sandbox's state folder, TANARA_HOME) decides.
    // Without the state folder, or with a free lock, no watcher runs; a held lock means one does.
    void watcherRunningFollowsTheLock()
    {
        const QString path = watcherLockPath();
        const QString dir = QFileInfo(path).absolutePath();
        QVERIFY(!QFileInfo::exists(dir) || QDir(dir).entryList(QDir::Files).isEmpty());
        QVERIFY(!watcherRunning());             // no state folder yet
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(!watcherRunning());             // folder, no lock
        {
            QLockFile held(path);
            QVERIFY(held.tryLock(0));
            QVERIFY(watcherRunning());          // a live PID holds it (ours)
        }
        QVERIFY(!watcherRunning());             // released
    }
};

QTEST_GUILESS_MAIN(RecordingLockTests)
#include "test_recording_lock.moc"
