// Tests for SharedFileLock (foundation/SharedFile.h), the inter-process lock next to people.json,
// voiceprints.json and tags.json:
//  - a lock held by another process is waited for at most the given time, then locked() is false;
//  - a lock file naming THIS process (a leftover; in-process lockers go through the gate) is reclaimed;
//  - the folder of a not-yet-existing file is created, so the very first write can be locked;
//  - fairness: against a holder that relocks right after unlocking, a waiter still gets its turn quickly
//    (QLockFile::tryLock(timeout) backs off exponentially and starved such a waiter until the timeout);
//  - a waiting marker left over by a crashed process does not slow anyone down;
//  - threads of one process queue at the in-process gate: a waiter that times out leaves the line, so the
//    next one is not blocked by its stale place.
//
// Everything runs in a temporary folder (and a temporary TANARA_HOME); no real data is touched.
#include <QtTest>
#include <QElapsedTimer>
#include <QLockFile>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>

#include "tanara/foundation/SharedFile.h"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <string>
#include <thread>

using tanara::SharedFileLock;

class SharedFileTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void heldLockTimesOut();
    void ownPidLeftoverIsRemoved();
    void missingFolderIsCreated();
    void waiterIsNotStarved();
    void staleWaitingMarkerIsIgnored();
    void timedOutThreadLeavesTheLine();

private:
    QString file(const QString& name) const { return QDir(m_dir->path()).filePath(name); }
    std::unique_ptr<QTemporaryDir> m_dir;
};

void SharedFileTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    qputenv("TANARA_HOME", m_dir->path().toUtf8());
}

void SharedFileTest::cleanup()
{
    m_dir.reset();
}

void SharedFileTest::heldLockTimesOut()
{
    // The holder must be ANOTHER process: a lock file naming this process is reclaimed as a leftover
    // (see ownPidLeftoverIsRemoved). The child is this test executable in holder mode (see main()); it
    // keeps the lock until its stdin is closed. Works the same on Windows: QLockFile checks the PID there
    // too, and the child is alive.
    const QString path = file("people.json");
    QProcess child;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("TANARA_LOCK_HOLDER"), path + ".lock");
    child.setProcessEnvironment(env);
    child.start(QCoreApplication::applicationFilePath(), QStringList());
    QVERIFY(child.waitForStarted(10000));
    QByteArray out;
    while (!out.contains("locked") && child.waitForReadyRead(10000))
        out += child.readAllStandardOutput();
    QVERIFY2(out.contains("locked"), out.constData());

    QElapsedTimer t;
    t.start();
    {
        const SharedFileLock lock(path, 300);
        QVERIFY(!lock.locked());
    }
    QVERIFY2(t.elapsed() >= 250, qPrintable(QString::number(t.elapsed())));
    QVERIFY2(t.elapsed() < 5000, qPrintable(QString::number(t.elapsed())));
    // The waiter cleans up its marker.
    QVERIFY(!QFileInfo::exists(path + ".lock.waiting"));

    child.closeWriteChannel();
    QVERIFY(child.waitForFinished(10000));
    QCOMPARE(child.exitCode(), 0);
    const SharedFileLock lock(path, 300);
    QVERIFY(lock.locked());
}

void SharedFileTest::ownPidLeftoverIsRemoved()
{
    // A lock file that names this process, with no thread holding the gate: a leftover (e.g. a delete that
    // failed on Windows). Made by copying a real lock file's content back after its unlock, so no handle
    // is open on it on any platform. Without the rule it would block until the 30 s stale time.
    const QString path = file("tags.json");
    QByteArray content;
    {
        QLockFile raw(path + ".lock");
        QVERIFY(raw.tryLock(0));
        QFile f(path + ".lock");
        QVERIFY(f.open(QIODevice::ReadOnly));
        content = f.readAll();
    }
    {
        QFile f(path + ".lock");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(content);
    }
    QElapsedTimer t;
    t.start();
    {
        const SharedFileLock lock(path, 3000);
        QVERIFY(lock.locked());
    }
    QVERIFY2(t.elapsed() < 1000, qPrintable(QString::number(t.elapsed())));
    QVERIFY(!QFileInfo::exists(path + ".lock"));
}

void SharedFileTest::missingFolderIsCreated()
{
    const QString path = file("sub/dir/voiceprints.json");
    const SharedFileLock lock(path, 300);
    QVERIFY(lock.locked());
    QVERIFY(QFileInfo::exists(path + ".lock"));
}

void SharedFileTest::waiterIsNotStarved()
{
    // Two threads hammer the same lock: lock -> write the file -> hold a little -> unlock -> immediately
    // again. They meet at the in-process FIFO gate; with the old QLockFile backoff (and with an unfair
    // mutex) one side waited until the other had finished entirely.
    const QString path = file("tags.json");
    constexpr int kCycles = 150;
    std::atomic<int> failures{0};
    std::atomic<qint64> maxWaitMs{0};
    auto worker = [&](char tag) {
        for (int i = 0; i < kCycles; ++i) {
            QElapsedTimer t;
            t.start();
            const SharedFileLock lock(path, 10000);
            const qint64 waited = t.elapsed();
            qint64 prev = maxWaitMs.load();
            while (waited > prev && !maxWaitMs.compare_exchange_weak(prev, waited)) {}
            if (!lock.locked()) {
                ++failures;
                continue;
            }
            QSaveFile f(path);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(QByteArray(4096, tag));
                f.commit();
            }
            QThread::msleep(4);
        }
    };
    QElapsedTimer total;
    total.start();
    std::thread a(worker, 'a'), b(worker, 'b');
    a.join();
    b.join();
    qInfo("%d cycles x 2 threads in %lld ms, longest wait %lld ms", kCycles, total.elapsed(), maxWaitMs.load());
    QCOMPARE(failures.load(), 0);
    // The bound is a ratio, not an absolute time: on a loaded CI runner (or with Windows' ~15.6 ms timer
    // ticks) a single fair hand-over can take hundreds of ms, while starvation means one side waits for
    // the other's whole run, i.e. close to 100 % of the total. Half of the total separates the two on any
    // machine. The absolute cap is the wait given to the lock (10 s): beyond it the lock had failed anyway.
    const qint64 totalMs = total.elapsed();
    QVERIFY2(maxWaitMs.load() < totalMs / 2,
             qPrintable(QStringLiteral("longest wait %1 ms of %2 ms").arg(maxWaitMs.load()).arg(totalMs)));
    QVERIFY2(maxWaitMs.load() < 10000, qPrintable(QString::number(maxWaitMs.load())));
    QVERIFY(!QFileInfo::exists(path + ".lock"));
    QVERIFY(!QFileInfo::exists(path + ".lock.waiting"));
}

void SharedFileTest::staleWaitingMarkerIsIgnored()
{
    const QString path = file("people.json");
    const QString marker = path + ".lock.waiting";
    {
        QFile f(marker);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(-3600), QFileDevice::FileModificationTime));
    }
    { const SharedFileLock lock(path, 300); QVERIFY(lock.locked()); }
    QVERIFY(!QFileInfo::exists(marker));   // removed, so later lockers do not stand back either

    // A fresh marker (somebody is waiting) makes a newcomer stand back once.
    {
        QFile f(marker);
        QVERIFY(f.open(QIODevice::WriteOnly));
    }
    QElapsedTimer t;
    t.start();
    { const SharedFileLock lock(path, 300); QVERIFY(lock.locked()); }
    QVERIFY2(t.elapsed() >= SharedFileLock::kYieldMs - 2, qPrintable(QString::number(t.elapsed())));
}

void SharedFileTest::timedOutThreadLeavesTheLine()
{
    const QString path = file("tags.json");
    std::atomic<bool> held{false}, release{false};
    std::thread holder([&] {
        const SharedFileLock lock(path, 1000);
        held = lock.locked();
        while (!release)
            QThread::msleep(1);
    });
    while (!held)
        QThread::msleep(1);
    // Same file, same process: the gate is busy, so this waits its time out and gives up.
    {
        QElapsedTimer t;
        t.start();
        const SharedFileLock lock(path, 100);
        QVERIFY(!lock.locked());
        QVERIFY2(t.elapsed() >= 90, qPrintable(QString::number(t.elapsed())));
    }
    release = true;
    holder.join();
    // The timed-out ticket is gone: the next locker gets in at once.
    QElapsedTimer t;
    t.start();
    const SharedFileLock lock(path, 1000);
    QVERIFY(lock.locked());
    QVERIFY2(t.elapsed() < 500, qPrintable(QString::number(t.elapsed())));
}

// Holder mode for heldLockTimesOut: lock the given lock file, report it, and keep it until stdin closes.
static int runHolder(const QString& lockPath)
{
    QLockFile lock(lockPath);
    if (!lock.tryLock(5000))
        return 2;
    std::cout << "locked" << std::endl;
    std::string line;
    while (std::getline(std::cin, line)) {}
    return 0;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString holder = qEnvironmentVariable("TANARA_LOCK_HOLDER");
    if (!holder.isEmpty())
        return runHolder(holder);
    SharedFileTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_shared_file.moc"
