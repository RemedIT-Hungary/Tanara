#include "tanara/editing/session/EditorSessions.h"

#include "tanara/editing/recheck/RecheckService.h"
#include "tanara/editing/session/SpeakerEditSession.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/editing/undo/MeetingUndoRegistry.h"
#include "tanara/library/store/MeetingRepository.h"

namespace tanara::editing {

EditorSessions::EditorSessions(Deps deps, QObject* parent)
    : QObject(parent), m_deps(std::move(deps))
{
}

EditorSessions::~EditorSessions() = default;

SpeakerEditSession* EditorSessions::sessionOf(const QString& meetingId) const
{
    const auto it = m_editors.constFind(meetingId);
    return it == m_editors.cend() ? nullptr : it->session.data();
}

SpeakerEditSession* EditorSessions::existing(const QString& meetingId) const { return sessionOf(meetingId); }

EditorSessions::Entry* EditorSessions::open(const QString& meetingId)
{
    auto it = m_editors.find(meetingId);
    if (it != m_editors.end() && it->session) return &*it;
    const Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) return nullptr;

    auto* ed = new SpeakerEditSession(m_deps.store, m_deps.people, m_deps.voiceprints, meetingId, this);
    // The voice analysis works on the session and dies with it (it stops on the session's closing()).
    auto* rc = new RecheckService(ed, ed);
    ed->setSelfPerson(m_deps.self ? m_deps.self() : SelfPerson());
    // Without voice ID (no model / TANARA_BUILD_VOICEID=OFF) the editor is fully functional even without
    // uncertainty and suggestions.
    if (m_deps.configure) m_deps.configure(ed, rc);
    it = m_editors.insert(meetingId, Entry{ed, rc});
    if (m_deps.undo) m_deps.undo->forMeeting(meetingId);   // the shared undo follows from the editor's first step

    // The session's side effects go out on the usual signals (old UI, library).
    connect(ed, &SpeakerEditSession::speakerMapChanged, this, [this](const QString& id) {
        m_emitting = true;
        emit speakerMapChanged(id);
        m_emitting = false;
    });
    connect(ed, &SpeakerEditSession::peopleChanged, this, &EditorSessions::peopleChanged);
    connect(ed, &SpeakerEditSession::voiceprintsChanged, this, &EditorSessions::voiceprintsChanged);
    connect(ed, &SpeakerEditSession::summaryStaleChanged, this, [this, meetingId] { emit summaryStaleChanged(meetingId); });
    return &*it;
}

SpeakerEditSession* EditorSessions::editor(const QString& meetingId)
{
    Entry* e = open(meetingId);
    return e ? e->session.data() : nullptr;
}

RecheckService* EditorSessions::recheck(const QString& meetingId)
{
    Entry* e = open(meetingId);
    return e ? e->recheck.data() : nullptr;
}

void EditorSessions::close(const QString& meetingId)
{
    const Entry e = m_editors.take(meetingId);
    if (e.session) e.session->deleteLater();   // the recheck service is its child
}

void EditorSessions::forEach(const std::function<void(SpeakerEditSession*, RecheckService*)>& fn) const
{
    for (const Entry& e : m_editors)
        if (e.session) fn(e.session, e.recheck);
}

void EditorSessions::externalSpeakerMapChanged(const QString& meetingId)
{
    // A change from outside (old UI rename, auto-identification, person rename) -> the editor rereads the disk.
    if (m_emitting) return;
    if (SpeakerEditSession* ed = sessionOf(meetingId)) ed->refreshFromDisk();
}

void EditorSessions::notifyVoiceprintsChanged()
{
    for (const Entry& e : std::as_const(m_editors))
        if (e.session) emit e.session->speakersChanged();
}

void EditorSessions::summaryRegenerated(const QString& meetingId)
{
    if (SpeakerEditSession* ed = sessionOf(meetingId)) {
        ed->notifySummaryRegenerated();
    } else {
        const Meeting m = m_deps.store->load(meetingId);
        if (!m.folder.isEmpty()) speakeredit::clearSummaryStale(m.folder);
    }
    emit summaryStaleChanged(meetingId);
}

void EditorSessions::transcriptReplaced(const QString& meetingId)
{
    const Meeting m = m_deps.store->load(meetingId);
    if (m.folder.isEmpty()) return;
    speakeredit::discardForNewTranscript(m.folder);
    if (SpeakerEditSession* ed = sessionOf(meetingId)) ed->reloadTranscript();
}

// ---- the summary's staleness ----

SummaryStaleInfo EditorSessions::summaryStale(const QString& meetingId) const
{
    const Meeting m = m_deps.store->load(meetingId);
    SpeakerEditSession* ed = sessionOf(meetingId);
    SummaryStaleInfo info = ed ? ed->summaryStale() : speakeredit::summaryStale(m);
    // The editor persists after every step, so the targeted part can be computed from disk.
    if (!m.id.isEmpty())
        speakeredit::fillTargetedStale(m, m_deps.personNames ? m_deps.personNames() : PersonNames(), info);
    return info;
}

SummaryStaleInfo EditorSessions::summaryStale(const Meeting& meeting) const
{
    if (SpeakerEditSession* ed = sessionOf(meeting.id)) return ed->summaryStale();
    return speakeredit::summaryStale(meeting);
}

QVector<SummaryStatement> EditorSessions::summaryStatements(const QString& meetingId) const
{
    const Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) return {};
    SummaryStaleInfo info;
    QVector<SummaryStatement> out;
    speakeredit::fillTargetedStale(m, m_deps.personNames ? m_deps.personNames() : PersonNames(), info, &out);
    return out;
}

RetranscribeImpact EditorSessions::retranscribeImpact(const QString& meetingId) const
{
    if (SpeakerEditSession* ed = sessionOf(meetingId)) return ed->retranscribeImpact();
    return speakeredit::retranscribeImpact(m_deps.store->load(meetingId));
}

void EditorSessions::dismissSummaryStale(const QString& meetingId)
{
    if (SpeakerEditSession* ed = sessionOf(meetingId)) {
        ed->dismissSummaryStale();   // summaryStaleChanged goes out from the editor's signal
        return;
    }
    const Meeting m = m_deps.store->load(meetingId);
    if (!m.folder.isEmpty() && speakeredit::clearSummaryStale(m.folder))
        emit summaryStaleChanged(meetingId);
}

} // namespace tanara::editing
