#include "tanara/audio/TrackTiming.h"

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

qint64 framesOwed(qint64 expectedFrames, qint64 deliveredFrames, qint64 toleranceFrames)
{
    const qint64 gap = expectedFrames - deliveredFrames;
    return gap > std::max<qint64>(0, toleranceFrames) ? gap : 0;
}

qint64 silenceToInsert(qint64 elapsedSinceOpenMs, qint64 idleMs, qint64 deliveredFrames,
                       int sampleRate)
{
    if (idleMs < kGapIdleMs) return 0;
    return framesOwed(expectedFrames(elapsedSinceOpenMs, sampleRate), deliveredFrames,
                      expectedFrames(kGapToleranceMs, sampleRate));
}

} // namespace tanara::tracktiming
