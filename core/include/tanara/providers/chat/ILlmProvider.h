#pragma once
// The old name of the chat port. TODO(P15): remove (the callers include IChatProvider.h).
#include "tanara/providers/chat/IChatProvider.h"

namespace tanara::providers {
using ILlmProvider = IChatProvider;   // TODO(P15): remove
} // namespace tanara::providers
