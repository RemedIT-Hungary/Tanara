#include "ParticipantTrackBinder.h"

#include "AppContext.h"

#include "tanara/editing/participants/IParticipantsFacade.h"

#include <algorithm>

namespace tanara_qml {

using namespace tanara;

ParticipantTrackBinder::ParticipantTrackBinder(QObject* parent) : QObject(parent)
{
    AppContext* ctx = AppContext::instance();
    connect(ctx, &AppContext::controllerChanged, this, [this]() { resolveServices(); reload(); });
    connect(ctx, &AppContext::demoChanged, this, [this]() { resolveServices(); reload(); });
    resolveServices();
    reload();
}

ParticipantTrackBinder::ParticipantTrackBinder(const UiServices& services, QObject* parent)
    : QObject(parent), m_explicitServices(true)
{
    useServices(services);
    reload();
}

// The process's services changed (a controller arrived, App.demo flipped).
void ParticipantTrackBinder::resolveServices()
{
    if (m_explicitServices) return;
    useServices(AppContext::instance()->services());
}

void ParticipantTrackBinder::useServices(const UiServices& services)
{
    for (const QMetaObject::Connection& conn : std::as_const(m_connections))
        disconnect(conn);
    m_connections.clear();
    if (m_facade && m_facade->parent() == this) m_facade->deleteLater();   // a fictional facade of ours
    m_facade = services.participants(this);
    m_facade->applyDemoState(m_demoState);
    auto mine = [this](const QString& id) { return !m_meetingId.isEmpty() && id == m_meetingId; };
    m_connections << connect(m_facade, &editing::IParticipantsFacade::tracksChanged, this,
                             [this, mine](const QString& id) { if (mine(id)) reload(); });
    m_connections << connect(m_facade, &editing::IParticipantsFacade::participantsChanged, this,
                             [this, mine](const QString& id) { if (mine(id)) reload(); });
    m_connections << connect(m_facade, &editing::IParticipantsFacade::speakerMapChanged, this,
                             [this, mine](const QString& id) { if (mine(id)) reload(); });
}

void ParticipantTrackBinder::setMeetingId(const QString& id)
{
    if (id == m_meetingId)
        return;
    m_meetingId = id;
    emit meetingIdChanged();
    reload();
}

void ParticipantTrackBinder::setDemoState(const QString& state)
{
    if (state == m_demoState)
        return;
    m_demoState = state;
    m_facade->applyDemoState(state);
    emit demoStateChanged();
    reload();
}

bool ParticipantTrackBinder::available() const
{
    return m_tracks.size() >= 2;
}

void ParticipantTrackBinder::reload()
{
    m_tracks.clear();
    for (const editing::ParticipantTrackChoice& t : m_facade->selectableTracks(m_meetingId))
        m_tracks.append({t.id, t.name, t.icon});
    ++m_revision;
    emit changed();
}

QStringList ParticipantTrackBinder::binding(const QString& id, bool* manual) const
{
    const ParticipantTrackBinding b = m_facade->participantTracks(m_meetingId, id);
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
    if (m_meetingId.isEmpty() && !m_facade->fictional())
        return false;
    // The change signal (participantsChanged) comes from the core -> reload; the fictional facade has no meeting to signal for.
    const bool ok = m_facade->setParticipantTracks(m_meetingId, id, trackIds);
    if (ok && m_facade->fictional()) reload();
    return ok;
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
