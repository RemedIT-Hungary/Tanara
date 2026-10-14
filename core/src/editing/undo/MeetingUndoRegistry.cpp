#include "tanara/editing/undo/MeetingUndoRegistry.h"

#include "tanara/editing/session/SpeakerEditSession.h"
#include "tanara/editing/undo/MeetingUndo.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/ParticipantJson.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace tanara::editing {

namespace {

QJsonObject participantUndoState(const Meeting& m)
{
    QJsonArray ps;
    for (const Participant& p : m.participants) ps.append(library::toJson(p));
    QJsonArray ts;
    for (const Track& t : m.tracks)
        ts.append(QJsonObject{{QStringLiteral("id"), t.id}, {QStringLiteral("active"), t.active},
                              {QStringLiteral("excludedReason"), t.excludedReason}});
    return QJsonObject{{QStringLiteral("participants"), ps},
                       {QStringLiteral("approval"), m.approval ? QJsonValue(library::toJson(*m.approval)) : QJsonValue()},
                       {QStringLiteral("contextNote"), m.contextNote},
                       {QStringLiteral("tracks"), ts}};
}

quint64 editorTop(SpeakerEditSession* ed)
{
    if (!ed) return 0;
    const QVector<quint64> s = ed->undoSerials();
    return s.isEmpty() ? 0 : s.last();
}

} // namespace

MeetingUndoRegistry::MeetingUndoRegistry(library::MeetingRepository* store, library::TagService* tags,
                                         std::function<SpeakerEditSession*(const QString&)> existingEditor,
                                         QObject* parent)
    : QObject(parent), m_store(store), m_tags(tags), m_existingEditor(std::move(existingEditor))
{
}

MeetingUndoRegistry::~MeetingUndoRegistry() = default;

MeetingUndoService* MeetingUndoRegistry::forMeeting(const QString& meetingId)
{
    if (meetingId.isEmpty()) return nullptr;
    SpeakerEditSession* ed = m_existingEditor ? m_existingEditor(meetingId) : nullptr;
    if (MeetingUndoService* u = m_services.value(meetingId)) {
        if (ed) u->attachEditSession(ed);
        return u;
    }
    if (!m_store || m_store->load(meetingId).id.isEmpty()) return nullptr;
    // The stack's parent stays the facade's object tree owner (this registry).
    auto* u = new MeetingUndoService(meetingId, this);
    u->attachTagService(m_tags);
    if (ed) u->attachEditSession(ed);
    m_services.insert(meetingId, u);
    connect(m_store, &library::MeetingRepository::meetingRemoved, u, [this, u](const QString& id) {
        if (id != u->meetingId()) return;
        m_services.remove(id);
        u->deleteLater();
    });
    return u;
}

// Writing the snapshot back (the rest of the meeting: title, tags, transcript, is untouched).
void MeetingUndoRegistry::restore(const Meeting& snap)
{
    Meeting m = m_store->load(snap.id);
    if (m.id.isEmpty()) return;
    bool tracksChanged = false;
    for (Track& t : m.tracks)
        for (const Track& o : snap.tracks)
            if (o.id == t.id && (o.active != t.active || o.excludedReason != t.excludedReason)) {
                t.active = o.active;
                t.excludedReason = o.excludedReason;
                tracksChanged = true;
            }
    m.participants = snap.participants;
    m.approval = snap.approval;
    m.contextNote = snap.contextNote;
    if (tracksChanged) m.mixdownDirty = true;
    m_store->saveMeeting(m);
    emit participantsChanged(m.id);
    if (tracksChanged) {
        emit this->tracksChanged(m.id);
        bool anyIncluded = false;
        for (const Track& t : m.tracks) anyIncluded = anyIncluded || t.included();
        if (anyIncluded) emit remixRequested(m.id);
    }
}

ParticipantUndoScope::ParticipantUndoScope(MeetingUndoRegistry* registry, const QString& meetingId,
                                           const QString& label)
    : text(label), m_registry(registry), m_meetingId(meetingId)
{
    if (!m_registry || meetingId.isEmpty() || m_registry->m_scopeDepth[meetingId]++ > 0) return;
    m_before = m_registry->m_store->load(meetingId);
    if (m_before.id.isEmpty()) return;
    m_editorTopBefore = editorTop(m_registry->m_existingEditor ? m_registry->m_existingEditor(meetingId) : nullptr);
    m_active = true;
}

ParticipantUndoScope::~ParticipantUndoScope()
{
    if (!m_registry) return;
    if (!m_meetingId.isEmpty() && --m_registry->m_scopeDepth[m_meetingId] <= 0)
        m_registry->m_scopeDepth.remove(m_meetingId);
    if (!m_active) return;
    const Meeting after = m_registry->m_store->load(m_meetingId);
    if (after.id.isEmpty() || participantUndoState(after) == participantUndoState(m_before)) return;
    const quint64 top = editorTop(m_registry->m_existingEditor ? m_registry->m_existingEditor(m_meetingId) : nullptr);
    MeetingUndoService* undo = m_registry->forMeeting(m_meetingId);
    if (!undo) return;
    QPointer<MeetingUndoRegistry> self(m_registry);
    const Meeting snap = m_before;
    undo->pushStep(text, [self, snap]() { if (self) self->restore(snap); },
                   top != m_editorTopBefore ? top : 0);
}

} // namespace tanara::editing
