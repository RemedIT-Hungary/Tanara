#include "TrackActivityModel.h"

#include "AppContext.h"
#include "TrackActivitySource.h"

#include "tanara/library/tracks/TrackCatalog.h"
#include "tanara/editing/review/SideAnalysis.h"

#include <algorithm>
#include <cmath>

namespace tanara_qml {

using namespace tanara;

TrackActivityModel::TrackActivityModel(QObject* parent) : QObject(parent)
{
    AppContext* ctx = AppContext::instance();
    connect(ctx, &AppContext::controllerChanged, this, [this]() { resolveSource(); reload(); });
    connect(ctx, &AppContext::demoChanged, this, [this]() { resolveSource(); reload(); });
    resolveSource();
    reload();
}

TrackActivityModel::TrackActivityModel(TrackActivitySource* source, QObject* parent) : QObject(parent)
{
    m_explicitSource = true;
    attach(source);
    reload();
}

void TrackActivityModel::resolveSource()
{
    if (m_explicitSource)
        return;
    // The services of the moment: the real source (one per process), or a demo source of this
    // model's own (its scenario is the demoState).
    attach(AppContext::instance()->services().trackActivity(m_demoState, this));
}

void TrackActivityModel::attach(TrackActivitySource* source)
{
    if (source == m_source)
        return;
    for (const QMetaObject::Connection& conn : std::as_const(m_connections))
        disconnect(conn);
    m_connections.clear();
    if (m_ownDemo && m_ownDemo != source)
        m_ownDemo->deleteLater();
    m_ownDemo = source && source->parent() == this ? source : nullptr;
    m_source = source;
    if (!source)
        return;
    auto mine = [this](const QString& id) { return !m_meetingId.isEmpty() && id == m_meetingId; };
    m_connections << connect(source, &TrackActivitySource::activityReady, this,
                             [this, mine](const QString& id) { if (mine(id)) reload(); });
    // New / located / deleted track: the fingerprint changes, the core recomputes.
    m_connections << connect(source, &TrackActivitySource::tracksChanged, this,
                             [this, mine](const QString& id) { if (mine(id)) reload(); });
}

void TrackActivityModel::setMeetingId(const QString& id)
{
    if (id == m_meetingId)
        return;
    m_meetingId = id;
    emit meetingIdChanged();
    reload();
}

void TrackActivityModel::setDemoState(const QString& state)
{
    if (state == m_demoState)
        return;
    m_demoState = state;
    emit demoStateChanged();
    resolveSource();
    reload();
}

TrackActivityModel::Lane TrackActivityModel::laneFrom(const TrackActivity& t)
{
    Lane lane;
    lane.trackId = t.trackId;
    lane.originMs = t.originMs;
    lane.frameMs = std::max(1, t.frameMs);
    lane.frames.resize(t.dbAboveFloor.size());
    // Speech: relative to the track's speech level at most 10 dB quieter, and above the noise.
    const float level = editing::sides::speechLevelDb(t);
    const float threshold = std::isnan(level) ? INFINITY
                                              : std::max(editing::sides::kActiveDb, level + editing::sides::kSpeechDb);
    for (qsizetype i = 0; i < t.dbAboveFloor.size(); ++i) {
        const float d = t.dbAboveFloor.at(i);
        qint8 v = NoData;
        if (!std::isnan(d)) v = d >= threshold ? Speech : Quiet;
        lane.frames[i] = v;
        if (v == Speech) ++lane.speechFrames;
    }
    return lane;
}

const TrackActivityModel::Lane* TrackActivityModel::lane(const QString& trackId) const
{
    const auto it = m_laneOf.constFind(trackId);
    return it == m_laneOf.constEnd() ? nullptr : &m_lanes.at(it.value());
}

int TrackActivityModel::nextSpeechMs(const QString& trackId, int fromMs) const
{
    const Lane* l = lane(trackId);
    if (!l || l->speechFrames == 0)
        return -1;
    const qint64 start = std::max<qint64>(0, (qint64(fromMs) - l->originMs) / l->frameMs);
    for (qsizetype i = start; i < l->frames.size(); ++i)
        if (l->frames.at(i) == Speech)
            return int(l->originMs + i * l->frameMs);
    return -1;
}

qreal TrackActivityModel::speechShare(const QString& trackId) const
{
    const Lane* l = lane(trackId);
    if (!l)
        return -1.0;
    const auto covered = std::count_if(l->frames.cbegin(), l->frames.cend(), [](qint8 v) { return v != NoData; });
    return covered > 0 ? qreal(l->speechFrames) / qreal(covered) : -1.0;
}

void TrackActivityModel::publish(const QString& state)
{
    m_state = state;
    ++m_revision;
    emit changed();
}

void TrackActivityModel::reload()
{
    m_lanes.clear();
    m_laneOf.clear();
    m_durationMs = 0;
    TrackActivitySource* src = m_source;
    if (!src) {
        publish(QStringLiteral("none"));
        return;
    }
    const Meeting m = src->meeting(m_meetingId);
    if (m.id.isEmpty() || m.tracks.isEmpty()) {
        publish(QStringLiteral("none"));
        return;
    }
    m_durationMs = m.durationMs;
    const MeetingActivity act = src->activity(m_meetingId);
    for (const TrackActivity& t : act.tracks) {
        m_laneOf.insert(t.trackId, int(m_lanes.size()));
        m_lanes.append(laneFrom(t));
        m_durationMs = std::max(m_durationMs, t.endMs());
    }
    // Every segment id to the logical track's leader.
    const QVector<library::tracknames::SegmentInfo> seg = library::tracknames::segments(m.tracks);
    for (int i = 0; i < m.tracks.size(); ++i) {
        const int lead = seg.value(i).leader >= 0 ? seg.value(i).leader : i;
        const auto it = m_laneOf.constFind(m.tracks.at(lead).id);
        if (it != m_laneOf.constEnd()) m_laneOf.insert(m.tracks.at(i).id, it.value());
    }
    publish(src->computing(m_meetingId) ? QStringLiteral("loading") : QStringLiteral("ready"));
}

} // namespace tanara_qml
