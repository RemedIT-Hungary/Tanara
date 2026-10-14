#include "tanara/providers/summary/ISummarizer.h"

namespace tanara::providers {

QString summarizationModeName(SummarizationMode mode)
{
    return mode == SummarizationMode::Complex ? QStringLiteral("complex") : QStringLiteral("quick");
}

} // namespace tanara::providers
