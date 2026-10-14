#pragma once
//
// Linux /proc reader behind RunningApps (platform/linux). The root is a parameter so the tests can
// point it at a fake /proc tree in a temporary folder.
#include "tanara/watcher/RunningApps.h"

namespace tanara::watcher::procfs {

// Every numeric <procRoot>/<pid>/ whose cmdline is not empty (kernel threads have none): the pid and
// the lowercase file name of argv[0].
QVector<RunningProcess> readProcesses(const QString& procRoot);

} // namespace tanara::watcher::procfs
