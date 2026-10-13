#include "tanara/Types.h"

namespace tanara {
// TANARA_VERSION is given by the build (CMakeLists.txt: project(VERSION) + TANARA_VERSION_SUFFIX).
#ifndef TANARA_VERSION
#define TANARA_VERSION "0.0.0-dev"
#endif
QString libraryVersion() { return QStringLiteral(TANARA_VERSION); }
}
