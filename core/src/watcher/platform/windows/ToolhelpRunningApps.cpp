// RunningApps on Windows: a Toolhelp32 process snapshot (kernel32, no extra library).
#include "tanara/watcher/RunningApps.h"
#include "tanara/watcher/detect/detail/WinCaptureRules.h"

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

namespace tanara::watcher {

bool RunningApps::supported()
{
    return true;
}

QVector<RunningProcess> RunningApps::processes()
{
    QVector<RunningProcess> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        // "Zoom.exe" -> "zoom" (the same normalization as the meeting detector's).
        const QString name = detail::win::imageBaseName(QString::fromWCharArray(pe.szExeFile));
        if (!name.isEmpty()) out.append(RunningProcess{qint64(pe.th32ProcessID), name});
    }
    CloseHandle(snap);
    return out;
}

} // namespace tanara::watcher
