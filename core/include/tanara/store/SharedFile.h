#pragma once
//
// SharedFile: shared helpers for JSON files written by several PROCESSES (analyzer, recorder,
// watcher) and rewritten as a whole (people.json, voiceprints.json):
//  - FileStamp: the file's state (mtime + size), for a cheap "did it change on disk since I loaded it?" check;
//  - SharedFileLock: an inter-process lock NEXT TO the file (<file>.lock, QLockFile) for the
//    "reread -> modify -> write out" step, so two processes do not overwrite each other's changes;
//  - setAsideCorrupt: setting an unparseable file aside before overwriting;
//  - replaceFile: puts a finished file in place of the old one so the old one survives a failure (mixdown).
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QString>

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
    // Waits for the lock (at most waitMs). If it cannot get it (e.g. another process is stuck), the caller
    // may still work; locked() tells. A crashed process's lock is released by QLockFile by itself (dead PID /
    // staleness time).
    explicit SharedFileLock(const QString& filePath, int waitMs = 5000)
        : m_lock(filePath + QStringLiteral(".lock"))
    {
        m_lock.setStaleLockTime(30000);
        m_locked = m_lock.tryLock(waitMs);
    }
    bool locked() const { return m_locked; }

private:
    QLockFile m_lock;
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
