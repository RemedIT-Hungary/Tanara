// foundation::platform, POSIX part (Linux and macOS): user info, process liveness, names.
#include "tanara/foundation/platform/Platform.h"

#include <pwd.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>

namespace tanara::platform {

QString executableName(const QString& baseName)
{
    return baseName;
}

QString userDisplayName()
{
    QString name;
    if (const passwd* pw = getpwuid(getuid())) {
        const QString gecos = QString::fromLocal8Bit(pw->pw_gecos).section(QLatin1Char(','), 0, 0);
        name = gecos.trimmed().isEmpty() ? QString::fromLocal8Bit(pw->pw_name) : gecos;
    }
    if (name.trimmed().isEmpty())
        name = qEnvironmentVariable("USER");
    return name;
}

QString perUserSuffix()
{
    return QStringLiteral("-%1").arg(getuid());
}

bool processAlive(qint64 pid)
{
    if (pid <= 0)
        return false;
    // kill(pid,0): 0 = exists and we have permission; EPERM = exists, but under another user.
    if (::kill(static_cast<pid_t>(pid), 0) == 0)
        return true;
    return errno == EPERM;
}

void debugOutput(const QString& line)
{
    Q_UNUSED(line);   // stderr / the log file already carry the line
}

} // namespace tanara::platform
