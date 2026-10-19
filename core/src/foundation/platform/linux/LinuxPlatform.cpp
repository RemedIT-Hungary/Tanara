// foundation::platform, Linux-only facts (the POSIX calls are in ../posix/).
#include "tanara/foundation/platform/Platform.h"

namespace tanara::platform {

QString osId()
{
    return QStringLiteral("linux");
}

void becomeResponsibleProcess(char**) {}

} // namespace tanara::platform

namespace tanara::paths {

bool caseSensitiveFileSystem()
{
    return true;
}

} // namespace tanara::paths
