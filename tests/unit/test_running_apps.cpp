// Tanara: watcher::RunningApps. On Linux the /proc reader runs on a fake /proc tree in a temporary
// folder; on every platform the live list must be callable and consistent.
#include <QtTest>
#include <QObject>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>

#include "tanara/watcher/RunningApps.h"
#if defined(Q_OS_LINUX)
#  include "tanara/watcher/platform/linux/Procfs.h"
#endif

using namespace tanara;

namespace {

bool writeFile(const QString& path, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    return f.write(content) == content.size();
}

} // namespace

class RunningAppsTests : public QObject {
    Q_OBJECT
private slots:

    void procfsReaderOnFakeTree()
    {
#if !defined(Q_OS_LINUX)
        QSKIP("/proc: Linux only.");
#else
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString r = root.path();
        // A process with a path and arguments (NUL-separated cmdline).
        QVERIFY(writeFile(r + "/101/cmdline", QByteArray("/usr/bin/Signal-Desktop\0--no-sandbox\0", 38)));
        // A bare name.
        QVERIFY(writeFile(r + "/202/cmdline", QByteArray("zoom\0", 5)));
        // A kernel thread: empty cmdline, skipped.
        QVERIFY(writeFile(r + "/3/cmdline", QByteArray()));
        // A second instance of the same app.
        QVERIFY(writeFile(r + "/404/cmdline", QByteArray("/opt/zoom/zoom\0", 15)));
        // Not a process folder, and a process folder without cmdline.
        QVERIFY(writeFile(r + "/self-not-a-pid/cmdline", QByteArray("bogus\0", 6)));
        QVERIFY(QDir(r).mkpath(QStringLiteral("505")));

        QVector<watcher::RunningProcess> procs = watcher::procfs::readProcesses(r);
        std::sort(procs.begin(), procs.end(),
                  [](const watcher::RunningProcess& a, const watcher::RunningProcess& b) { return a.pid < b.pid; });
        QCOMPARE(procs.size(), 3);
        QCOMPARE(procs.at(0).pid, qint64(101));
        QCOMPARE(procs.at(0).name, QStringLiteral("signal-desktop"));
        QCOMPARE(procs.at(1).pid, qint64(202));
        QCOMPARE(procs.at(1).name, QStringLiteral("zoom"));
        QCOMPARE(procs.at(2).pid, qint64(404));
        QCOMPARE(procs.at(2).name, QStringLiteral("zoom"));

        QVERIFY(watcher::procfs::readProcesses(r + "/missing").isEmpty());
#endif
    }

    void liveListIsConsistent()
    {
        const QVector<watcher::RunningProcess> procs = watcher::RunningApps::processes();
        const QStringList names = watcher::RunningApps::names();
        if (!watcher::RunningApps::supported()) {
            QVERIFY(procs.isEmpty());
            QVERIFY(names.isEmpty());
            return;
        }
        // At least this test process runs.
        QVERIFY(!procs.isEmpty());
        QVERIFY(names.contains(QStringLiteral("test_running_apps")));
        // names(): distinct, sorted, lowercase.
        QStringList sorted = names;
        sorted.sort();
        QCOMPARE(names, sorted);
        QCOMPARE(QSet<QString>(names.begin(), names.end()).size(), names.size());
        for (const QString& n : names) QCOMPARE(n, n.toLower());
    }
};

QTEST_GUILESS_MAIN(RunningAppsTests)
#include "test_running_apps.moc"
