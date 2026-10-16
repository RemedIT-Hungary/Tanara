// Tests for SharedFileLock (foundation/SharedFile.h), the inter-process lock next to people.json,
// voiceprints.json and tags.json:
//  - a lock held elsewhere is waited for at most the given time, then locked() is false;
//  - the folder of a not-yet-existing file is created, so the very first write can be locked;
//  - fairness: against a holder that relocks right after unlocking, a waiter still gets its turn quickly
//    (QLockFile::tryLock(timeout) backs off exponentially and starved such a waiter until the timeout);
//  - a waiting marker left over by a crashed process does not slow anyone down.
//
// Everything runs in a temporary folder (and a temporary TANARA_HOME); no real data is touched.
#include <QtTest>
#include <QElapsedTimer>
#include <QLockFile>
#include <QSaveFile>
#include <QTemporaryDir>

#include "tanara/foundation/SharedFile.h"

#include <algorithm>
#include <atomic>
#include <thread>

using tanara::SharedFileLock;

class SharedFileTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void heldLockTimesOut();
    void missingFolderIsCreated();
    void waiterIsNotStarved();
    void staleWaitingMarkerIsIgnored();

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
    const QString path = file("people.json");
    QLockFile other(path + ".lock");
    QVERIFY(other.tryLock(0));

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

    other.unlock();
    const SharedFileLock lock(path, 300);
    QVERIFY(lock.locked());
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
    // Two threads (QLockFile treats them like two processes: the lock file names a live PID) hammer the
    // same lock: lock -> write the file -> hold a little -> unlock -> immediately again. Each wait must
    // stay short; with the old QLockFile backoff one side waited until the other had finished entirely.
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
    // One run of the other side alone is >= 150 x 4 ms = 600 ms; a fair hand-over is a few poll intervals.
    QVERIFY2(maxWaitMs.load() < 400, qPrintable(QString::number(maxWaitMs.load())));
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

QTEST_GUILESS_MAIN(SharedFileTest)
#include "test_shared_file.moc"
