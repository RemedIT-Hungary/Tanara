#pragma once
//
// SharedFile: shared helpers for JSON files written by several PROCESSES (analyzer, recorder,
// watcher) and rewritten as a whole (people.json, voiceprints.json):
//  - FileStamp: the file's state (mtime + size), for a cheap "did it change on disk since I loaded it?" check;
//  - SharedFileLock: an inter-process lock NEXT TO the file (<file>.lock, QLockFile) for the
//    "reread -> modify -> write out" step, so two processes do not overwrite each other's changes;
//    threads of one process are first serialised by a process-local FIFO gate per file;
//  - setAsideCorrupt: setting an unparseable file aside before overwriting;
//  - replaceFile: puts a finished file in place of the old one so the old one survives a failure (mixdown).
#include "tanara/foundation/log/Logging.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLockFile>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QSysInfo>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <system_error>

namespace tanara {

struct FileStamp {
    qint64 mtimeMs = -1;   // -1: the file does not exist
    qint64 size    = -1;

    static FileStamp of(const QString& path)
    {
        FileStamp s;
        const QFileInfo fi(path);
        if (fi.exists()) {
            s.mtimeMs = fi.lastModified().toMSecsSinceEpoch();
            s.size    = fi.size();
        }
        return s;
    }
    bool operator==(const FileStamp& o) const { return mtimeMs == o.mtimeMs && size == o.size; }
    bool operator!=(const FileStamp& o) const { return !(*this == o); }
};

namespace detail {

// Process-local, first-come-first-served lock per file path, taken by SharedFileLock BEFORE the lock
// file. QLockFile is an inter-process lock: between threads of one process it can only be polled
// (create/delete/stat of files every few ms, ~16 ms timer ticks on Windows), and the waiting-marker
// hand-over costs a yield per relock. Threads of one process hand over here in microseconds instead and
// never poll the file system against each other; the lock file then only arbitrates between processes.
//
// A plain std::timed_mutex is not fair: a thread that unlocks and immediately relocks usually wins again,
// so a waiter could starve. Hence a ticket queue: the lock goes to the oldest waiter. Not recursive
// (nested locking of the same file never worked with QLockFile either; it times out).
class FileGate {
public:
    // The gate of a path; entries live while somebody uses them (see release()).
    static std::shared_ptr<FileGate> acquire(const QString& key)
    {
        Registry& r = registry();
        const QMutexLocker guard(&r.mutex);
        std::shared_ptr<FileGate>& slot = r.gates[key];
        if (!slot)
            slot = std::make_shared<FileGate>();
        ++slot->m_users;
        return slot;
    }
    static void release(const QString& key, const std::shared_ptr<FileGate>& gate)
    {
        Registry& r = registry();
        const QMutexLocker guard(&r.mutex);
        if (--gate->m_users == 0)
            r.gates.remove(key);
    }

    // Waits in line until the deadline; true = held.
    bool lock(const QDeadlineTimer& deadline)
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        const quint64 me = m_nextTicket++;
        m_queue.push_back(me);
        const auto myTurn = [&] { return !m_held && m_queue.front() == me; };
        bool ok = true;
        if (deadline.isForever())
            m_cv.wait(lk, myTurn);
        else
            ok = m_cv.wait_until(lk, deadline.deadline<std::chrono::steady_clock>(), myTurn);
        if (ok) {
            m_held = true;
            m_queue.pop_front();
        } else {
            m_queue.erase(std::find(m_queue.begin(), m_queue.end(), me));
            // The one behind may be first now.
            m_cv.notify_all();
        }
        return ok;
    }
    void unlock()
    {
        {
            const std::lock_guard<std::mutex> lk(m_mutex);
            m_held = false;
        }
        m_cv.notify_all();
    }

    // The registry key: the absolute, cleaned path, case-folded (Windows and macOS file systems are
    // case-insensitive; on Linux two files differing only in case merely share a gate, which serialises
    // a little more but is never wrong). The file need not exist yet, so no canonicalFilePath().
    static QString keyOf(const QString& filePath)
    {
        return QDir::cleanPath(QFileInfo(filePath).absoluteFilePath()).toCaseFolded();
    }

private:
    struct Registry {
        QMutex mutex;
        QHash<QString, std::shared_ptr<FileGate>> gates;
    };
    static Registry& registry()
    {
        static Registry r;
        return r;
    }

    std::mutex              m_mutex;
    std::condition_variable m_cv;
    std::deque<quint64>     m_queue;
    quint64                 m_nextTicket = 0;
    bool                    m_held = false;
    int                     m_users = 0;   // guarded by the registry mutex
};

} // namespace detail

class SharedFileLock {
public:
    // The lock is held for milliseconds (reread -> modify -> QSaveFile), so a long wait is cheap and only
    // matters when another process is stuck.
    static constexpr int kDefaultWaitMs = 30000;
    // The wait when the caller gives none: kDefaultWaitMs, or TANARA_LOCK_WAIT_MS if set (tests shorten it;
    // an environment variable also reaches child processes).
    static int defaultWaitMs()
    {
        bool ok = false;
        const int env = qEnvironmentVariableIntValue("TANARA_LOCK_WAIT_MS", &ok);
        return ok && env >= 0 ? env : kDefaultWaitMs;
    }
    // Poll interval while another process holds the lock.
    static constexpr int kPollMs = 5;
    // How long a newcomer stands back when somebody is already waiting. Longer than one poll interval
    // even with Windows' default ~15.6 ms timer tick, so the waiter's next poll lands in the gap.
    static constexpr int kYieldMs = 20;

    // Waits for the lock (at most waitMs; negative = defaultWaitMs()). locked() tells whether it was obtained; when it is false the
    // caller MUST NOT write the file (another process may be in the middle of its own reread -> write
    // cycle, and an unlocked write would be a lost update). A crashed process's lock is released by
    // QLockFile by itself (dead PID / staleness time).
    //
    // QLockFile::tryLock(timeout) is not used: between attempts it sleeps with an exponential backoff
    // (100, 200, 400 ... ms), so against a process that locks and unlocks in a tight loop the waiter
    // almost never catches the free moment and starves until the timeout. Here the waiter retries with
    // tryLock(0) every kPollMs (tryLock(0) still does the stale-lock handling). Polling alone is not
    // enough against a holder that relocks right after unlocking, so a waiter also leaves a marker file
    // (<file>.lock.waiting); whoever comes to lock while the marker exists first stands back for
    // kYieldMs, which hands the lock to the waiter (roughly first come, first served).
    //
    // Threads of the same process never meet at the lock file: they first queue at a process-local
    // gate (detail::FileGate) and only the gate's holder goes on to the file lock. waitMs covers both.
    // Release order: the file lock, then the gate.
    //
    // Leftover lock file of THIS process: if the lock file names our own PID on this host while we hold
    // the gate, no thread of this process can legitimately hold it (every in-process locker goes through
    // the gate first), so it is a leftover, e.g. an unlock() whose delete failed because a virus scanner
    // or indexer had the fresh file open on Windows. QLockFile would treat it as held by a live process
    // until the stale time (30 s), so it is removed (at most once per acquisition) and locking is retried
    // at once. Caveat: the same file reached through two different path spellings (symlink, 8.3 name)
    // gets two gates and so bypasses this guarantee; case differences are covered by the case-folded key.
    explicit SharedFileLock(const QString& filePath, int waitMs = -1)
        : m_lock(filePath + QStringLiteral(".lock"))
        , m_marker(filePath + QStringLiteral(".lock.waiting"))
        , m_gateKey(detail::FileGate::keyOf(filePath))
        , m_gate(detail::FileGate::acquire(m_gateKey))
    {
        if (waitMs < 0)
            waitMs = defaultWaitMs();
        const QDeadlineTimer deadline(waitMs);
        m_gateHeld = m_gate->lock(deadline);
        if (!m_gateHeld) {
            qCWarning(lcStore).noquote()
                << QStringLiteral("SharedFileLock: could not lock %1 within %2 ms (held by another thread of this process)")
                       .arg(filePath).arg(waitMs);
            return;
        }
        m_lock.setStaleLockTime(30000);
        // The lock file lives next to the data file: without the folder (first write) it cannot be
        // created at all. Checked first: mkpath walks the whole path, which is slow on Windows.
        const QString folder = QFileInfo(filePath).absolutePath();
        if (!QFileInfo::exists(folder))
            QDir().mkpath(folder);
        yieldToWaiter();
        bool announced = false;
        bool leftoverRemoved = false;
        for (;;) {
            if (m_lock.tryLock(0)) {
                m_locked = true;
                break;
            }
            // Permission / unknown error: waiting does not help (QLockFile gives up on these too).
            if (m_lock.error() != QLockFile::LockFailedError)
                break;
            if (!leftoverRemoved && heldByThisProcess()) {
                leftoverRemoved = true;
                if (m_lock.removeStaleLockFile()) {
                    qCInfo(lcStore).noquote()
                        << QStringLiteral("SharedFileLock: leftover lock file of this process removed: %1")
                               .arg(m_lock.fileName());
                    continue;
                }
            }
            if (deadline.hasExpired())
                break;
            // Re-created on every poll: another waiter that got the lock removed it.
            if (!QFileInfo::exists(m_marker)) {
                QFile marker(m_marker);
                announced = marker.open(QIODevice::WriteOnly) || announced;
            }
            QThread::msleep(static_cast<unsigned long>(std::min<qint64>(kPollMs, deadline.remainingTime())));
        }
        if (announced)
            QFile::remove(m_marker);
        if (m_locked)
            return;
        qint64 pid = 0;
        QString host, app;
        m_lock.getLockInfo(&pid, &host, &app);
        qCWarning(lcStore).noquote()
            << QStringLiteral("SharedFileLock: could not lock %1 within %2 ms (error %3, holder pid %4 %5)")
                   .arg(filePath).arg(waitMs).arg(int(m_lock.error())).arg(pid).arg(app);
    }
    ~SharedFileLock()
    {
        if (m_locked)
            m_lock.unlock();
        if (m_gateHeld)
            m_gate->unlock();
        detail::FileGate::release(m_gateKey, m_gate);
    }
    SharedFileLock(const SharedFileLock&) = delete;
    SharedFileLock& operator=(const SharedFileLock&) = delete;

    bool locked() const { return m_locked; }

private:
    // The lock file names this process on this host (see the constructor comment).
    bool heldByThisProcess() const
    {
        qint64 pid = 0;
        QString host, app;
        if (!m_lock.getLockInfo(&pid, &host, &app))
            return false;
        return pid == QCoreApplication::applicationPid()
            && (host.isEmpty() || host == QSysInfo::machineHostName());
    }

    void yieldToWaiter() const
    {
        const QFileInfo fi(m_marker);
        if (!fi.exists())
            return;
        // A marker older than any wait is left over from a crashed waiter.
        if (fi.lastModified().msecsTo(QDateTime::currentDateTime()) > kDefaultWaitMs + 5000) {
            QFile::remove(m_marker);
            return;
        }
        QThread::msleep(kYieldMs);
    }

    QLockFile m_lock;
    QString   m_marker;
    QString   m_gateKey;
    std::shared_ptr<detail::FileGate> m_gate;
    bool      m_gateHeld = false;
    bool      m_locked = false;
};

// Renames an unparseable (non-empty) file to "<file>.corrupt-<timestamp>" so the next save does not
// destroy it. true if there is nothing to set aside, or it succeeded.
inline bool setAsideCorrupt(const QString& path)
{
    const QFileInfo fi(path);
    if (!fi.exists() || fi.size() == 0)
        return true;
    const QString aside = path + QStringLiteral(".corrupt-")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    return QFile::rename(path, aside);
}

// Replaces file `to` with the finished file `from` so the old `to` is NEVER lost without success
// ("delete, then rename" loses the old one on failure). First an atomic rename onto it; if that
// fails (e.g. the file system does not allow overwriting), the old one is set aside and deleted only
// after the new one is in place; on failure it is put back. On false, `to` is in its pre-call state and
// `from` is untouched.
inline bool replaceFile(const QString& from, const QString& to)
{
    if (!QFileInfo::exists(from))
        return false;
    std::error_code ec;
    std::filesystem::rename(QFileInfo(from).filesystemAbsoluteFilePath(),
                            QFileInfo(to).filesystemAbsoluteFilePath(), ec);
    if (!ec)
        return true;
    const bool hadOld = QFileInfo::exists(to);
    const QString aside = to + QStringLiteral(".old");
    if (hadOld) {
        QFile::remove(aside);
        if (!QFile::rename(to, aside))
            return false;
    }
    if (QFile::rename(from, to)) {
        if (hadOld) QFile::remove(aside);
        return true;
    }
    if (hadOld) QFile::rename(aside, to);
    return false;
}

} // namespace tanara
