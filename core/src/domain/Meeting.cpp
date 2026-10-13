#include "tanara/domain/Meeting.h"

#include <algorithm>

namespace tanara::tracktiming {

FileRange meetingToFileRange(qint64 offsetMs, qint64 startMs, qint64 endMs, qint64 fileDurationMs)
{
    FileRange r;
    if (endMs <= startMs) return r;
    const qint64 off = std::max<qint64>(0, offsetMs);
    qint64 s = std::max<qint64>(0, startMs - off);
    qint64 e = endMs - off;
    if (fileDurationMs >= 0) e = std::min(e, fileDurationMs);
    if (e <= s) return r;
    r.startMs = s;
    r.endMs = e;
    return r;
}

} // namespace tanara::tracktiming
