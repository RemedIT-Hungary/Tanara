#include "tanara/recording/audio/SystemAudioSource.h"

namespace tanara::recording {

const ISystemAudioSource& systemAudioSource()
{
    static const std::unique_ptr<ISystemAudioSource> source = createSystemAudioSource();
    return *source;
}

} // namespace tanara::recording
