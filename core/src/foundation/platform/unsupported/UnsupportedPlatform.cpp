// foundation::platform for the POSIX systems without their own folder (not Linux, Windows or macOS).
// The POSIX calls are in ../posix/.
#include "tanara/foundation/platform/Platform.h"

namespace tanara::platform {

QString osId()
{
    return QStringLiteral("unknown");
}

void becomeResponsibleProcess(char**) {}

} // namespace tanara::platform

namespace tanara::paths {

bool caseSensitiveFileSystem()
{
    // Unknown file system: treating names as equal regardless of case is the safe side (it only
    // rejects more).
    return false;
}

} // namespace tanara::paths
