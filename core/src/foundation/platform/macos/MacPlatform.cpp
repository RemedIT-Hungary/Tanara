// foundation::platform, macOS-only facts and calls (the POSIX calls are in ../posix/).
#include "tanara/foundation/platform/Platform.h"

#include <mach-o/dyld.h>
#include <spawn.h>
#include <unistd.h>

#include <climits>
#include <cstdlib>

extern char** environ;

// Private libquarantine calls (in libSystem since macOS 10.14; Chromium and Qt Creator use them).
// Weak, so the app still loads if a future macOS drops them.
extern "C" int responsibility_spawnattrs_setdisclaim(posix_spawnattr_t* attrs, int disclaim)
    __attribute__((weak_import));
extern "C" pid_t responsibility_get_pid_responsible_for_pid(pid_t pid) __attribute__((weak_import));

namespace tanara::platform {

QString osId()
{
    return QStringLiteral("macos");
}

void becomeResponsibleProcess(char** argv)
{
    // Started from CLion, Xcode or a terminal, macOS asks the LAUNCHING app for the microphone and
    // "System audio recording" permissions, and refuses them without a prompt when that app has no
    // usage strings: the recording is silent. Re-executing with the responsibility disclaimed makes
    // Tanara the one asked (its usage strings: the bundle's Info.plist, or outside a bundle the
    // embedded __info_plist section, packaging/macos/embedded-Info.plist). POSIX_SPAWN_SETEXEC keeps
    // the pid, so an attached debugger stays attached.
    static const char kMarker[] = "TANARA_RESPONSIBLE";
    if (std::getenv(kMarker)) {
        unsetenv(kMarker);   // the re-executed image: our own child processes start clean
        return;
    }
    if (!responsibility_spawnattrs_setdisclaim || !responsibility_get_pid_responsible_for_pid)
        return;
    if (responsibility_get_pid_responsible_for_pid(getpid()) == getpid())
        return;   // started from Finder / open: already responsible
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0)
        return;
    setenv(kMarker, "1", 1);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETEXEC);
    responsibility_spawnattrs_setdisclaim(&attr, 1);
    posix_spawn(nullptr, path, nullptr, &attr, argv, environ);   // returns only on failure
    posix_spawnattr_destroy(&attr);
    unsetenv(kMarker);
}

} // namespace tanara::platform

namespace tanara::paths {

bool caseSensitiveFileSystem()
{
    return false;   // APFS / HFS+ default
}

} // namespace tanara::paths
