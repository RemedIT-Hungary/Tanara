#include "tanara/audio/TrackTiming.h"

#include <algorithm>

namespace tanara::tracktiming {

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
