#include "ParticipantSample.h"

#include <QtGlobal>

namespace tanara_qml::participantsample {

using tanara::editing::ParticipantSample;
using tanara::VoiceCluster;

ParticipantSample parseSampleRef(const QString& trackId, const QString& sampleRef)
{
    ParticipantSample out;
    const int hash = sampleRef.lastIndexOf(QLatin1Char('#'));
    if (hash < 0 || trackId.isEmpty())
        return out;
    const QStringList span = sampleRef.mid(hash + 1).split(QLatin1Char('-'));
    if (span.size() != 2)
        return out;
    bool okA = false, okB = false;
    const qint64 a = span.at(0).toLongLong(&okA);
    const qint64 b = span.at(1).toLongLong(&okB);
    if (!okA || !okB || a < 0 || b <= a)
        return out;
    out.trackId = trackId;
    out.startMs = a;
    out.endMs = b;
    return out;
}

namespace {

const VoiceCluster* largestCluster(const QVector<VoiceCluster>& clusters, const QString& participantId,
                                   bool needSample)
{
    const VoiceCluster* best = nullptr;
    for (const VoiceCluster& c : clusters) {
        if (c.participantId != participantId || participantId.isEmpty()) continue;
        if (needSample && !parseSampleRef(c.trackId, c.sampleRef).isValid()) continue;
        if (!best || c.speechMs() > best->speechMs()) best = &c;
    }
    return best;
}

qint64 overlapMs(const VoiceCluster& c, qint64 startMs, qint64 endMs)
{
    qint64 sum = 0;
    for (const QPair<qint64, qint64>& w : c.windows)
        sum += qMax<qint64>(0, qMin(w.second, endMs) - qMax(w.first, startMs));
    return sum;
}

} // namespace

ParticipantSample clusterSample(const QVector<VoiceCluster>& clusters, const QString& participantId)
{
    const VoiceCluster* c = largestCluster(clusters, participantId, true);
    return c ? parseSampleRef(c->trackId, c->sampleRef) : ParticipantSample();
}

ParticipantSample lineSample(const QVector<VoiceCluster>& clusters, const QString& participantId,
                             qint64 startMs, qint64 endMs)
{
    if (participantId.isEmpty() || endMs <= startMs)
        return {};
    const VoiceCluster* best = nullptr;
    qint64 bestOverlap = -1;
    for (const VoiceCluster& c : clusters) {
        if (c.participantId != participantId) continue;
        const qint64 o = overlapMs(c, startMs, endMs);
        if (o > bestOverlap) { best = &c; bestOverlap = o; }
    }
    if (!best)
        return {};
    ParticipantSample out;
    out.trackId = best->trackId;
    out.startMs = startMs;
    out.endMs = qMin(endMs, startMs + kMaxSampleMs);
    return out.isValid() ? out : ParticipantSample();
}

} // namespace tanara_qml::participantsample
