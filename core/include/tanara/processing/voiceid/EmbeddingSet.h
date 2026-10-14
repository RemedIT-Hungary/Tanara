#pragma once
//
// The fusion of an EmbeddingSet lives in the library (library/store/VoiceFusion.h, next to the voiceprint
// store), so the editing module can use it too; processing::fusion stays as an alias for the existing callers.
#include "tanara/domain/Voice.h"
#include "tanara/library/store/VoiceFusion.h"

namespace tanara::processing {

namespace fusion = library::fusion;

} // namespace tanara::processing
