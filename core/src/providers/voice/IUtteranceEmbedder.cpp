#include "tanara/providers/voice/IUtteranceEmbedder.h"

namespace tanara::providers {

EmbeddingSet IUtteranceEmbedder::embedAll(qint64 startMs, qint64 endMs, const QStringList& modelIds)
{
    EmbeddingSet out;
    const QString id = voicemodels::defaultModelId();
    if (modelIds.contains(id)) out.insert(id, embed(startMs, endMs));
    return out;
}

} // namespace tanara::providers
