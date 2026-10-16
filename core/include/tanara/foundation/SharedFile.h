#pragma once
//
// SharedFile: shared helpers for JSON files written by several PROCESSES (analyzer, recorder,
// watcher) and rewritten as a whole (people.json, voiceprints.json):
//  - FileStamp: the file's state (mtime + size), for a cheap "did it change on disk since I loaded it?" check;
//  - SharedFileLock: an inter-process lock NEXT TO the file (<file>.lock, QLockFile) for the
//    "reread -> modify -> write out" step, so two processes do not overwrite each other's changes;
//  - setAsideCorrupt: setting an unparseable file aside before overwriting;
//  - replaceFile: puts a finished file in place of the old one so the old one survives a failure (mixdown).
#include "tanara/foundation/log/Logging.h"

#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QString>
#include <QThread>

#include <algorithm>
#include <filesystem>
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
    explicit SharedFileLock(const QString& filePath, int waitMs = -1)
        : m_lock(filePath + QStringLiteral(".lock"))
        , m_marker(filePath + QStringLiteral(".lock.waiting"))
    {
        m_lock.setStaleLockTime(30000);
        // The lock file lives next to the data file: without the folder (first write) it cannot be
        // created at all.
        QDir().mkpath(QFileInfo(filePath).absolutePath());
        if (waitMs < 0)
            waitMs = defaultWaitMs();
        yieldToWaiter();
        const QDeadlineTimer deadline(waitMs);
        bool announced = false;
        for (;;) {
            if (m_lock.tryLock(0)) {
                m_locked = true;
                break;
            }
            // Permission / unknown error: waiting does not help (QLockFile gives up on these too).
            if (m_lock.error() != QLockFile::LockFailedError || deadline.hasExpired())
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
    SharedFileLock(const SharedFileLock&) = delete;
    SharedFileLock& operator=(const SharedFileLock&) = delete;

    bool locked() const { return m_locked; }

private:
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
