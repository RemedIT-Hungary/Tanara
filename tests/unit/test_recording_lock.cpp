//
// Tanara — RecordingLock unit-tesztek.
//
// A lock (~/.tanara/recording.lock) jelzi, folyik-e felvétel: a felvevő felveszi,
// a figyelő olvassa. Elavult (crash utáni, halott PID) lock inaktívnak számít.
// Külön teszt-exe (tests/CMakeLists.txt GLOB), saját main → QTEST_GUILESS_MAIN.
//
#include <QtTest>
#include <QObject>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>

#include "tanara/detect/RecordingLock.h"

using namespace tanara;

class RecordingLockTests : public QObject {
    Q_OBJECT
private slots:

    // acquire() után a lock aktív (a saját PID él), a mappa és a PID visszaolvasható.
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

    // release() törli a lockot → nincs aktív felvétel.
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

    // Elavult lock (nem-létező PID) → inaktív (a crash utáni árva lock nem blokkol).
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

    // Nincs lock-fájl → nincs aktív felvétel (nem dob, nem crashel).
    void missingFileIsInactive()
    {
        QTemporaryDir dir;
        QVERIFY(!RecordingLock::read(dir.filePath(QStringLiteral("nope.lock"))).active);
    }
};

QTEST_GUILESS_MAIN(RecordingLockTests)
#include "test_recording_lock.moc"
