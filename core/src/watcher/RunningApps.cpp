#include "tanara/watcher/RunningApps.h"

namespace tanara::watcher {

QStringList RunningApps::names()
{
    QStringList out;
    for (const RunningProcess& p : processes())
        if (!p.name.isEmpty() && !out.contains(p.name))
            out << p.name;
    out.sort();
    return out;
}

} // namespace tanara::watcher
