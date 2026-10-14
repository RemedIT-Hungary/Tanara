#pragma once
// The old name of the text-embedding port. TODO(P15): remove (the callers include ITextEmbedder.h).
#include "tanara/providers/embedding/ITextEmbedder.h"

namespace tanara::providers {
using IEmbeddingProvider = ITextEmbedder;   // TODO(P15): remove
} // namespace tanara::providers
