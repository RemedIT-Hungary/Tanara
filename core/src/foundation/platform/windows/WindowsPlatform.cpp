// foundation::platform, Windows.
#include "tanara/foundation/platform/Platform.h"

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#define SECURITY_WIN32
#include <windows.h>
#include <security.h>

#include <iterator>

namespace tanara::platform {

QString osId()
{
    return QStringLiteral("windows");
}

QString executableName(const QString& baseName)
{
    return baseName + QStringLiteral(".exe");
}

QString userDisplayName()
{
    QString name;
    wchar_t buf[256];
    ULONG len = ULONG(std::size(buf));
    if (GetUserNameExW(NameDisplay, buf, &len) && len > 0)
        name = QString::fromWCharArray(buf, int(len));
    if (name.trimmed().isEmpty())
        name = qEnvironmentVariable("USERNAME");
    return name;
}

QString perUserSuffix()
{
    return QString();
}

bool processAlive(qint64 pid)
{
    if (pid <= 0)
        return false;
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!h)
        return false;
    const DWORD r = WaitForSingleObject(h, 0);
    CloseHandle(h);
    return r == WAIT_TIMEOUT;   // still running (not signaled)
}

void debugOutput(const QString& line)
{
    OutputDebugStringW(reinterpret_cast<const wchar_t*>((line + QLatin1Char('\n')).utf16()));
}

} // namespace tanara::platform

namespace tanara::paths {

bool caseSensitiveFileSystem()
{
    return false;
}

} // namespace tanara::paths
