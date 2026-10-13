// Tanara: unit tests of the instance scope (sandbox isolation).
//
// With TANARA_HOME the recorder singleton's name and the lock files are tied to the sandbox's
// metadata folder, so a test instance never reaches the user's real recorder / watcher.
#include <QtTest>
#include <QTemporaryDir>

#include "tanara/foundation/paths/Paths.h"
#include "tanara/foundation/ProcessLock.h"

using namespace tanara;

class InstanceScopeTests : public QObject {
    Q_OBJECT
private slots:

    void cleanup() { qunsetenv("TANARA_HOME"); }

    // Without TANARA_HOME the real instance's name is unchanged (no suffix).
    void noOverrideNoSuffix()
    {
        qunsetenv("TANARA_HOME");
        QVERIFY(instanceScopeSuffix().isEmpty());
    }

    // Two different sandboxes get two different, stable suffixes, and neither is empty.
    void suffixDependsOnDirectory()
    {
        QTemporaryDir a, b;
        qputenv("TANARA_HOME", a.path().toUtf8());
        const QString sa = instanceScopeSuffix();
        QCOMPARE(instanceScopeSuffix(), sa);            // stable
        qputenv("TANARA_HOME", b.path().toUtf8());
        const QString sb = instanceScopeSuffix();
        QVERIFY(!sa.isEmpty() && !sb.isEmpty());
        QVERIFY(sa != sb);
        QVERIFY(sa.startsWith(QLatin1Char('-')));
        // The same folder spelled differently (trailing slash) gives the same.
        qputenv("TANARA_HOME", (a.path() + QStringLiteral("/")).toUtf8());
        QCOMPARE(instanceScopeSuffix(), sa);
    }

    // The lock files live in the sandbox, even if the setting names another folder.
    void lockPathsLiveInSandbox()
    {
        QTemporaryDir dir;
        qputenv("TANARA_HOME", dir.path().toUtf8());
        QVERIFY(recordingLockPath().startsWith(dir.path()));
        QVERIFY(recordingLockPath(QStringLiteral("~/.tanara")).startsWith(dir.path()));
        QVERIFY(watcherLockPath().startsWith(dir.path()));
        QVERIFY(recordingLockPath().endsWith(QStringLiteral("recording.lock")));
        QVERIFY(watcherLockPath().endsWith(QStringLiteral("watcher.lock")));
    }

    // A sandbox's lock is not visible from another sandbox (and vice versa).
    void sandboxLockInvisibleElsewhere()
    {
        QTemporaryDir a, b;
        qputenv("TANARA_HOME", a.path().toUtf8());
        RecordingLock lock(recordingLockPath());
        QVERIFY(lock.acquire(QStringLiteral("/tmp/x")));
        QVERIFY(RecordingLock::read(recordingLockPath()).active);
        qputenv("TANARA_HOME", b.path().toUtf8());
        QVERIFY(!RecordingLock::read(recordingLockPath()).active);
        lock.release();
    }
};

QTEST_GUILESS_MAIN(InstanceScopeTests)
#include "test_instance_scope.moc"
