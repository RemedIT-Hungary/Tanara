// foundation::platform for the platforms without their own folder yet (macOS until the plan in
// docs/macos.md is implemented, and any other POSIX system). The POSIX calls are in ../posix/.
#include "tanara/foundation/platform/Platform.h"

#include <QSysInfo>

namespace tanara::platform {

QString osId()
{
    // Run-time: this folder serves several systems.
    return QSysInfo::kernelType() == QLatin1String("darwin") ? QStringLiteral("macos")
                                                             : QStringLiteral("unknown");
}

} // namespace tanara::platform

namespace tanara::paths {

bool caseSensitiveFileSystem()
{
    // macOS (APFS / HFS+ default): case-insensitive. Treating names as equal regardless of case is
    // the safe side elsewhere too (it only rejects more).
    return false;
}

} // namespace tanara::paths
