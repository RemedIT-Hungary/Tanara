#include "tanara/editing/participants/ParticipantSample.h"

#include "tanara/editing/session/SpeakerEditTypes.h"

#include <QHash>
#include <QtGlobal>

namespace tanara::editing::participantsample {

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

ParticipantSample storedSample(const Participant& p)
{
    return parseSampleRef(p.sampleTrackId, p.sampleRef);
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

bool clean(const EditorUtterance& u) { return !u.noisy && !u.ignored; }

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

const EditorUtterance* longestLine(const QVector<EditorUtterance>& utterances, const QStringList& speakerKeys)
{
    const EditorUtterance* longest = nullptr;
    for (const EditorUtterance& u : utterances) {
        if (!speakerKeys.contains(u.speakerKey)) continue;
        const bool longer = !longest || (clean(u) && !clean(*longest))
                            || (clean(u) == clean(*longest) && u.endMs - u.startMs > longest->endMs - longest->startMs);
        if (longer) longest = &u;
    }
    return longest;
}

QVector<VoiceCluster> withMerges(QVector<VoiceCluster> clusters, const QVector<Participant>& participants)
{
    QHash<QString, QString> into;   // merged-away participant id -> the surviving one
    for (const Participant& p : participants)
        for (const QString& id : p.mergedFrom)
            if (!id.isEmpty() && id != p.id) into.insert(id, p.id);
    if (into.isEmpty()) return clusters;
    for (VoiceCluster& c : clusters)
        if (const auto it = into.constFind(c.participantId); it != into.constEnd()) c.participantId = it.value();
    return clusters;
}

} // namespace tanara::editing::participantsample
