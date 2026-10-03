#include "tanara/detect/RecordingLock.h"

#include "tanara/Paths.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#if defined(Q_OS_WIN)
#  include <windows.h>
#else
#  include <csignal>
#  include <cerrno>
#endif

namespace tanara {

namespace {

// Él-e még a megadott PID-ű folyamat? (Elavult-lock felismerés.)
bool pidAlive(qint64 pid)
{
    if (pid <= 0)
        return false;
#if defined(Q_OS_WIN)
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!h)
        return false;
    const DWORD r = WaitForSingleObject(h, 0);
    CloseHandle(h);
    return r == WAIT_TIMEOUT;   // még fut (nem szignált)
#else
    // kill(pid,0): 0 → létezik és van jogunk; EPERM → létezik, de más usernél.
    if (::kill(static_cast<pid_t>(pid), 0) == 0)
        return true;
    return errno == EPERM;
#endif
}

} // namespace

QString instanceScopeSuffix()
{
    const QString home = paths::homeOverride();
    if (home.isEmpty())
        return {};
    const QString canonical = QDir::cleanPath(QDir(home).absolutePath());
    return QLatin1Char('-') + QString::fromLatin1(
        QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
}

QString recordingLockPath(const QString& configuredMetadataDir)
{
    return QDir(paths::resolveMetadataDir(configuredMetadataDir))
        .filePath(QStringLiteral("recording.lock"));
}

QString watcherLockPath()
{
    return QDir(paths::defaultMetadataDir()).filePath(QStringLiteral("watcher.lock"));
}

RecordingLock::RecordingLock(const QString& lockPath)
    : m_path(lockPath)
{
}

bool RecordingLock::acquire(const QString& meetingFolder)
{
    QJsonObject o;
    o[QStringLiteral("pid")]           = QCoreApplication::applicationPid();
    o[QStringLiteral("meetingFolder")] = meetingFolder;
    o[QStringLiteral("startedAt")]     = QDateTime::currentDateTime().toString(Qt::ISODate);

    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    if (!f.commit())
        return false;
    m_owned = true;
    return true;
}

void RecordingLock::release()
{
    if (!m_owned)
        return;
    // Csak a SAJÁT lockunkat töröljük (közben más felvevő nem vehette át — de az
    // óvatosság kedvéért a PID-et is ellenőrizzük).
    const Info info = read(m_path);
    if (info.pid == QCoreApplication::applicationPid() || info.pid == 0)
        QFile::remove(m_path);
    m_owned = false;
}

RecordingLock::Info RecordingLock::read(const QString& lockPath)
{
    Info info;
    QFile f(lockPath);
    if (!f.open(QIODevice::ReadOnly))
        return info;   // nincs lock → nincs felvétel
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    info.pid           = static_cast<qint64>(o.value(QStringLiteral("pid")).toDouble());
    info.meetingFolder = o.value(QStringLiteral("meetingFolder")).toString();
    info.startedAt     = o.value(QStringLiteral("startedAt")).toString();
    info.active        = pidAlive(info.pid);   // elavult (halott PID) → active=false
    return info;
}

} // namespace tanara
