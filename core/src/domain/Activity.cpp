#include "tanara/domain/Activity.h"

#include <algorithm>
#include <cmath>

namespace tanara {

int TrackActivity::coveredFrames() const
{
    return int(std::count_if(dbAboveFloor.cbegin(), dbAboveFloor.cend(),
                             [](float v) { return !std::isnan(v); }));
}

qint64 VoiceCluster::speechMs() const
{
    qint64 sum = 0;
    for (const auto& w : windows) sum += w.second - w.first;
    return sum;
}

} // namespace tanara
