#include "tanara/processing/analysis/LocalParticipantIdentifier.h"
#include "tanara/library/store/VoiceprintStore.h"

namespace tanara::processing {

double LocalParticipantIdentifier::score(const providers::IdentifyProbe& probe,
                                         const providers::IdentifyCandidate& candidate)
{
    double sum = 0.0;
    int shared = 0;
    for (const providers::VoiceVector& v : probe.vectors) {
        if (v.vector.isEmpty()) continue;
        double best = -2.0;
        for (const Voiceprint& p : candidate.prints) {
            if (p.model != v.modelId || p.embedding.size() != v.vector.size()) continue;
            best = std::max(best, library::VoiceprintStore::cosineSimilarity(v.vector, p.embedding));
        }
        if (best > -2.0) { sum += best; ++shared; }
    }
    return shared > 0 ? sum / shared : -1.0;
}

QVector<ParticipantGuess> LocalParticipantIdentifier::identifyNow(const providers::IdentifyRequest& req)
{
    const double threshold = req.threshold >= 0.0 ? req.threshold : kDefaultThreshold;
    QVector<ParticipantGuess> out;
    out.reserve(req.probes.size());
    for (const providers::IdentifyProbe& probe : req.probes) {
        ParticipantGuess g;
        g.trackId = probe.trackId;
        g.deviceName = probe.deviceName;
        g.windows = probe.windows;
        g.sampleRef = probe.sampleRef;
        QString bestName;
        double best = -1.0;
        for (const providers::IdentifyCandidate& c : req.candidates) {
            const double s = score(probe, c);
            if (s > best) { best = s; bestName = c.name; }
        }
        g.score = best;
        g.name = best >= threshold ? bestName : QString();
        out.append(g);
    }
    return out;
}

providers::IdentifyJob* LocalParticipantIdentifier::identify(const providers::IdentifyRequest& req)
{
    return new providers::FunctionJob<QVector<ParticipantGuess>>(
        [req](QVector<ParticipantGuess>* out, Error*) { *out = identifyNow(req); return true; }, this);
}

} // namespace tanara::processing
