#include "ParticipantTrackBinder.h"

#include "AppContext.h"
#include "JobSupport.h"

#include "tanara/app/AppController.h"
#include "tanara/library/tracks/TrackCatalog.h"
#include "tanara/library/store/MeetingStore.h"

#include <algorithm>

namespace tanara_qml {

using namespace tanara;

namespace {

QString iconOf(const library::TrackView& v)
{
    if (v.role == library::TrackRole::OwnMic || v.track.kind == TrackKind::Mic) return QStringLiteral("mic");
    if (v.role == library::TrackRole::CallAudio || v.track.kind == TrackKind::Loopback) return QStringLiteral("phone");
    return QStringLiteral("audio-lines");
}

} // namespace

ParticipantTrackBinder::ParticipantTrackBinder(QObject* parent) : QObject(parent)
{
    AppContext* ctx = AppContext::instance();
    connect(ctx, &AppContext::controllerChanged, this, [this]() { connectController(); reload(); });
    connect(ctx, &AppContext::demoChanged, this, &ParticipantTrackBinder::reload);
    connectController();
    reload();
}

app::AppController* ParticipantTrackBinder::app() const { return jobsupport::resolveController(m_injected); }
QObject* ParticipantTrackBinder::controllerObject() const { return app(); }

void ParticipantTrackBinder::setController(QObject* controller)
{
    if (m_injected == controller)
        return;
    m_injected = controller;
    connectController();
    emit controllerChanged();
    reload();
}

void ParticipantTrackBinder::setMeetingId(const QString& id)
{
    if (id == m_meetingId)
        return;
    m_meetingId = id;
    emit meetingIdChanged();
    reload();
}

bool ParticipantTrackBinder::available() const
{
    return m_tracks.size() >= 2;
}

void ParticipantTrackBinder::connectController()
{
    app::AppController* c = app();
    if (c == m_connected)
        return;
    for (const QMetaObject::Connection& conn : std::as_const(m_connections))
        disconnect(conn);
    m_connections.clear();
    m_connected = c;
    if (!c)
        return;
    auto mine = [this](const QString& id) { return !m_meetingId.isEmpty() && id == m_meetingId; };
    m_connections << connect(c, &app::AppController::tracksChanged, this,
                             [this, mine](const QString& id) { if (mine(id)) reload(); });
    m_connections << connect(c, &app::AppController::participantsChanged, this,
                             [this, mine](const QString& id) { if (mine(id)) reload(); });
    m_connections << connect(c, &app::AppController::speakerMapChanged, this,
                             [this, mine](const QString& id) { if (mine(id)) reload(); });
}

void ParticipantTrackBinder::reload()
{
    m_tracks.clear();
    app::AppController* c = app();
    if (jobsupport::demoMode(c)) {
        m_tracks = {{QStringLiteral("mic"), tr("Saját mikrofon"), QStringLiteral("mic")},
                    {QStringLiteral("call"), tr("Hívás hangja"), QStringLiteral("phone")}};
        if (m_demoManual.isEmpty())
            m_demoManual.insert(QStringLiteral("p3"), {QStringLiteral("mic"), QStringLiteral("call")});
    } else if (!m_meetingId.isEmpty() && c->store()) {
        const Meeting m = c->store()->load(m_meetingId);
        // The selectable tracks: the logical tracks included in the mixdown (once per segment).
        for (const library::TrackView& v : library::TrackCatalog::tracks(m)) {
            if (v.segment > 1 || !v.track.included() || v.fileMissing) continue;
            m_tracks.append({v.track.id, v.displayName, iconOf(v)});
        }
    }
    ++m_revision;
    emit changed();
}

QStringList ParticipantTrackBinder::binding(const QString& id, bool* manual) const
{
    app::AppController* c = app();
    if (jobsupport::demoMode(c)) {
        const QStringList m = m_demoManual.value(id);
        *manual = !m.isEmpty();
        if (*manual) return m;
        return {id == QLatin1String("p1") ? QStringLiteral("mic") : QStringLiteral("call")};
    }
    const ParticipantTrackBinding b = c->participantTracks(m_meetingId, id);
    *manual = b.manual;
    return b.trackIds;
}

QVariantList ParticipantTrackBinder::chips(const QString& id) const
{
    QVariantList out;
    if (id.isEmpty() || id.startsWith(QLatin1String("suggest:")) || m_tracks.size() < 2)
        return out;
    bool manual = false;
    const QStringList on = binding(id, &manual);
    // Short name for the row: "mic" / "call" if there is one track of that kind; otherwise the track's name.
    auto count = [this](const QString& icon) {
        return std::count_if(m_tracks.cbegin(), m_tracks.cend(), [&icon](const TrackChoice& t) { return t.icon == icon; });
    };
    for (const TrackChoice& t : m_tracks) {
        QString shortName = t.name;
        if (t.icon == QLatin1String("mic") && count(t.icon) == 1) shortName = tr("mikrofon");
        else if (t.icon == QLatin1String("phone") && count(t.icon) == 1) shortName = tr("hívás");
        out << QVariantMap{{QStringLiteral("trackId"), t.id}, {QStringLiteral("name"), t.name},
                           {QStringLiteral("icon"), t.icon}, {QStringLiteral("on"), on.contains(t.id)},
                           {QStringLiteral("manual"), manual}, {QStringLiteral("shortName"), shortName}};
    }
    return out;
}

bool ParticipantTrackBinder::setTracks(const QString& id, const QStringList& trackIds)
{
    app::AppController* c = app();
    if (jobsupport::demoMode(c)) {
        if (trackIds.isEmpty()) m_demoManual.remove(id);
        else m_demoManual.insert(id, trackIds);
        ++m_revision;
        emit changed();
        return true;
    }
    if (m_meetingId.isEmpty())
        return false;
    // The change signal (participantsChanged) comes from the core -> reload.
    return c->setParticipantTracks(m_meetingId, id, trackIds);
}

bool ParticipantTrackBinder::toggle(const QString& id, const QString& trackId)
{
    if (id.isEmpty() || trackId.isEmpty())
        return false;
    bool manual = false;
    QStringList set = binding(id, &manual);
    if (set.contains(trackId)) set.removeAll(trackId);
    else set.append(trackId);
    // Clicking the single inferred track = confirmation as a manual binding (not "nothing").
    if (set.isEmpty() && !manual) set << trackId;
    // In track order (stable display and comparison).
    QStringList ordered;
    for (const TrackChoice& t : m_tracks)
        if (set.contains(t.id)) ordered << t.id;
    return setTracks(id, ordered);
}

bool ParticipantTrackBinder::reset(const QString& id)
{
    return !id.isEmpty() && setTracks(id, {});
}

} // namespace tanara_qml
