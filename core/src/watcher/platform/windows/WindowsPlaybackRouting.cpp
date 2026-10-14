// IPlaybackRouting on Windows: not implemented yet (WASAPI audio sessions would be per endpoint).
#include "tanara/watcher/PlaybackRouting.h"

namespace tanara::watcher {

std::unique_ptr<IPlaybackRouting> createPlatformPlaybackRouting()
{
    return createUnsupportedPlaybackRouting();
}

} // namespace tanara::watcher
