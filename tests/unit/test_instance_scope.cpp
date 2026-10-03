//
// Tanara — példány-hatókör (sandbox-elszigetelés) unit-tesztek.
//
// TANARA_HOME mellett a felvevő-singleton neve és a lock-fájlok a sandbox metaadat-mappájához
// kötődnek, így egy teszt-példány soha nem éri el a felhasználó valódi felvevőjét/figyelőjét.
//
#include <QtTest>
#include <QTemporaryDir>

#include "tanara/Paths.h"
#include "tanara/detect/RecordingLock.h"

using namespace tanara;

class InstanceScopeTests : public QObject {
    Q_OBJECT
private slots:

    void cleanup() { qunsetenv("TANARA_HOME"); }

    // TANARA_HOME nélkül a valódi példány neve változatlan (nincs utótag).
    void noOverrideNoSuffix()
    {
        qunsetenv("TANARA_HOME");
        QVERIFY(instanceScopeSuffix().isEmpty());
    }

    // Két különböző sandbox két különböző, stabil utótagot kap — és egyik sem üres.
    void suffixDependsOnDirectory()
    {
        QTemporaryDir a, b;
        qputenv("TANARA_HOME", a.path().toUtf8());
        const QString sa = instanceScopeSuffix();
        QCOMPARE(instanceScopeSuffix(), sa);            // stabil
        qputenv("TANARA_HOME", b.path().toUtf8());
        const QString sb = instanceScopeSuffix();
        QVERIFY(!sa.isEmpty() && !sb.isEmpty());
        QVERIFY(sa != sb);
        QVERIFY(sa.startsWith(QLatin1Char('-')));
        // Ugyanaz a mappa más írásmóddal (záró perjel) ugyanazt adja.
        qputenv("TANARA_HOME", (a.path() + QStringLiteral("/")).toUtf8());
        QCOMPARE(instanceScopeSuffix(), sa);
    }

    // A lock-fájlok a sandboxban élnek — akkor is, ha a beállítás más mappát mondana.
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

    // A sandbox lockja nem látszik egy másik sandboxból (és viszont).
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
