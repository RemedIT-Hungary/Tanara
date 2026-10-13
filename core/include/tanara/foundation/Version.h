#pragma once
//
// The program version (foundation). TANARA_VERSION is given by the build (root CMakeLists.txt:
// project(VERSION) + TANARA_VERSION_SUFFIX); only Version.cpp sees it.
#include <QString>

namespace tanara {

QString libraryVersion();   // e.g. "0.5.0a"

} // namespace tanara
