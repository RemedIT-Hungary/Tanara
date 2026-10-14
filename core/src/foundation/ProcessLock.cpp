#include "tanara/foundation/ProcessLock.h"

#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/foundation/platform/Platform.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>


namespace tanara {

namespace {

// Is the process with the given PID still alive? (Stale-lock detection.)
bool pidAlive(qint64 pid)
{
    return platform::processAlive(pid);
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

QString recordingLockPath()
{
    return paths::current().stateFile(paths::files::RecordingLock);
}

QString watcherLockPath()
{
    return paths::current().stateFile(paths::files::WatcherLock);
}

ProcessLock::ProcessLock(const QString& lockPath)
    : m_path(lockPath)
{
}

bool ProcessLock::acquire(const QString& meetingFolder)
{
    QJsonObject o;
    o[QStringLiteral("pid")]           = QCoreApplication::applicationPid();
    o[QStringLiteral("meetingFolder")] = meetingFolder;
    o[QStringLiteral("startedAt")]     = QDateTime::currentDateTime().toString(Qt::ISODate);

    QDir().mkpath(QFileInfo(m_path).absolutePath());   // the state folder may not exist yet
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    if (!f.commit())
        return false;
    m_owned = true;
    return true;
}

void ProcessLock::release()
{
    if (!m_owned)
        return;
    // We delete only OUR OWN lock (no other recorder could have taken over meanwhile, but out of
    // caution we check the PID too).
    const Info info = read(m_path);
    if (info.pid == QCoreApplication::applicationPid() || info.pid == 0)
        QFile::remove(m_path);
    m_owned = false;
}

ProcessLock::Info ProcessLock::read(const QString& lockPath)
{
    Info info;
    QFile f(lockPath);
    if (!f.open(QIODevice::ReadOnly))
        return info;   // no lock = no recording
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    info.pid           = static_cast<qint64>(o.value(QStringLiteral("pid")).toDouble());
    info.meetingFolder = o.value(QStringLiteral("meetingFolder")).toString();
    info.startedAt     = o.value(QStringLiteral("startedAt")).toString();
    info.active        = pidAlive(info.pid);   // stale (dead PID) -> active=false
    return info;
}

} // namespace tanara
