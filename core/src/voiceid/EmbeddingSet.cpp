#include "tanara/voiceid/EmbeddingSet.h"

#include "tanara/store/VoiceprintStore.h"
#include "tanara/voiceid/VoiceModelRegistry.h"

#include <cmath>

namespace tanara {
namespace fusion {

QVector<float> fuse(const EmbeddingSet& set, const QStringList& modelIds)
{
    const QStringList ids = VoiceModelRegistry::normalizeIds(modelIds);
    if (ids.isEmpty()) return {};
    int total = 0;
    for (const QString& id : ids) {
        const auto it = set.constFind(id);
        if (it == set.constEnd() || it->isEmpty()) return {};
        total += it->size();
    }
    const double w = 1.0 / std::sqrt(double(ids.size()));
    QVector<float> out;
    out.reserve(total);
    for (const QString& id : ids) {
        const QVector<float> n = VoiceprintStore::l2normalize(set.value(id));
        for (float x : n) out.append(float(double(x) * w));
    }
    return out;
}

double averageCosine(const EmbeddingSet& a, const EmbeddingSet& b, const QStringList& modelIds,
                     int* used)
{
    double sum = 0.0;
    int n = 0;
    for (const QString& id : VoiceModelRegistry::normalizeIds(modelIds)) {
        const QVector<float> va = a.value(id), vb = b.value(id);
        if (va.isEmpty() || vb.isEmpty()) continue;
        sum += VoiceprintStore::cosineSimilarity(va, vb);
        ++n;
    }
    if (used) *used = n;
    return n > 0 ? sum / n : -1.0;
}

} // namespace fusion
} // namespace tanara
