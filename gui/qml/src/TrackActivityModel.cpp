#include "TrackActivityModel.h"

#include "AppContext.h"
#include "JobSupport.h"

#include "tanara/AppController.h"
#include "tanara/audio/TrackCatalog.h"
#include "tanara/edit/SideAnalysis.h"
#include "tanara/store/MeetingStore.h"

#include <algorithm>
#include <cmath>

namespace tanara_qml {

using namespace tanara;

TrackActivityModel::TrackActivityModel(QObject* parent) : QObject(parent)
{
    AppContext* ctx = AppContext::instance();
    connect(ctx, &AppContext::controllerChanged, this, [this]() { connectController(); reload(); });
    connect(ctx, &AppContext::demoChanged, this, &TrackActivityModel::reload);
    connectController();
    reload();
}

AppController* TrackActivityModel::app() const { return jobsupport::resolveController(m_injected); }
QObject* TrackActivityModel::controllerObject() const { return app(); }

void TrackActivityModel::setController(QObject* controller)
{
    if (m_injected == controller)
        return;
    m_injected = controller;
    connectController();
    emit controllerChanged();
    reload();
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
    reload();
}

void TrackActivityModel::connectController()
{
    AppController* c = app();
    if (c == m_connected)
        return;
    for (const QMetaObject::Connection& conn : std::as_const(m_connections))
        disconnect(conn);
    m_connections.clear();
    m_connected = c;
    if (!c)
        return;
    auto mine = [this](const QString& id) { return !m_meetingId.isEmpty() && id == m_meetingId; };
    m_connections << connect(c, &AppController::trackActivityReady, this,
                             [this, mine](const QString& id) { if (mine(id)) reload(); });
    // New / located / deleted track: the fingerprint changes, the core recomputes.
    m_connections << connect(c, &AppController::tracksChanged, this,
                             [this, mine](const QString& id) { if (mine(id)) reload(); });
}

TrackActivityModel::Lane TrackActivityModel::laneFrom(const TrackActivity& t)
{
    Lane lane;
    lane.trackId = t.trackId;
    lane.originMs = t.originMs;
    lane.frameMs = std::max(1, t.frameMs);
    lane.frames.resize(t.dbAboveFloor.size());
    // Speech: relative to the track's speech level at most 10 dB quieter, and above the noise.
    const float level = sides::speechLevelDb(t);
    const float threshold = std::isnan(level) ? INFINITY
                                              : std::max(sides::kActiveDb, level + sides::kSpeechDb);
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
    AppController* c = app();
    if (jobsupport::demoMode(c)) {
        loadDemo();
        return;
    }
    m_lanes.clear();
    m_laneOf.clear();
    m_durationMs = 0;
    if (m_meetingId.isEmpty() || !c->store()) {
        publish(QStringLiteral("none"));
        return;
    }
    const Meeting m = c->store()->load(m_meetingId);
    if (m.id.isEmpty() || m.tracks.isEmpty()) {
        publish(QStringLiteral("none"));
        return;
    }
    m_durationMs = m.durationMs;
    const MeetingActivity act = c->trackActivity(m_meetingId);
    for (const TrackActivity& t : act.tracks) {
        m_laneOf.insert(t.trackId, int(m_lanes.size()));
        m_lanes.append(laneFrom(t));
        m_durationMs = std::max(m_durationMs, t.endMs());
    }
    // Every segment id to the logical track's leader.
    const QVector<tracknames::SegmentInfo> seg = tracknames::segments(m.tracks);
    for (int i = 0; i < m.tracks.size(); ++i) {
        const int lead = seg.value(i).leader >= 0 ? seg.value(i).leader : i;
        const auto it = m_laneOf.constFind(m.tracks.at(lead).id);
        if (it != m_laneOf.constEnd()) m_laneOf.insert(m.tracks.at(i).id, it.value());
    }
    publish(c->trackActivityComputing(m_meetingId) ? QStringLiteral("loading") : QStringLiteral("ready"));
}

// Fictional, deterministic speech pattern for the demo (for TrackListModel's demo tracks).
void TrackActivityModel::loadDemo()
{
    m_lanes.clear();
    m_laneOf.clear();
    m_durationMs = (30 * 60 + 34) * 1000;
    if (m_demoState == QLatin1String("loading")) {
        publish(QStringLiteral("loading"));
        return;
    }
    const int frameMs = 500;
    const int n = int(m_durationMs / frameMs);
    auto make = [&](const char* id, int seed, qreal talk, qint64 startMs) {
        Lane l;
        l.trackId = QString::fromLatin1(id);
        l.frameMs = frameMs;
        l.originMs = 0;
        l.frames.resize(n);
        quint32 x = 2463534242u + quint32(seed) * 7919u;
        bool speaking = false;
        int left = 0;
        for (int i = 0; i < n; ++i) {
            if (qint64(i) * frameMs < startMs) { l.frames[i] = NoData; continue; }
            if (left-- <= 0) {
                x ^= x << 13; x ^= x >> 17; x ^= x << 5;
                speaking = talk > 0 && (x % 1000) / 1000.0 < talk;
                left = 4 + int((x >> 10) % (speaking ? 40 : 30));
            }
            l.frames[i] = speaking ? Speech : Quiet;
            if (speaking) ++l.speechFrames;
        }
        m_laneOf.insert(l.trackId, int(m_lanes.size()));
        m_lanes.append(l);
    };
    make("mic", 1, 0.38, 0);
    make("call", 2, 0.61, 0);
    make("system", 3, 0.0, 0);
    make("mic2", 4, 0.2, 0);
    publish(QStringLiteral("ready"));
}

} // namespace tanara_qml
