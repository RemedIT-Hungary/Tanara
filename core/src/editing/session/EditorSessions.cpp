#include "tanara/editing/session/EditorSessions.h"

#include "tanara/editing/session/SpeakerEditor.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/editing/undo/MeetingUndoRegistry.h"
#include "tanara/library/store/MeetingRepository.h"

namespace tanara::editing {

EditorSessions::EditorSessions(Deps deps, QObject* parent)
    : QObject(parent), m_deps(std::move(deps))
{
}

EditorSessions::~EditorSessions() = default;

SpeakerEditor* EditorSessions::existing(const QString& meetingId) const { return m_editors.value(meetingId); }

SpeakerEditor* EditorSessions::editor(const QString& meetingId)
{
    if (SpeakerEditor* ed = m_editors.value(meetingId)) return ed;
    const Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) return nullptr;

    auto* ed = new SpeakerEditor(m_deps.store, m_deps.people, m_deps.voiceprints, meetingId, this);
    ed->setSelfPerson(m_deps.self ? m_deps.self() : SelfPerson());
    // Without voice ID (no model / TANARA_BUILD_VOICEID=OFF) the editor is fully functional even without
    // uncertainty and suggestions.
    if (m_deps.configure) m_deps.configure(ed);
    m_editors.insert(meetingId, ed);
    if (m_deps.undo) m_deps.undo->forMeeting(meetingId);   // the shared undo follows from the editor's first step

    // The editor's side effects go out on the usual signals (old UI, library).
    connect(ed, &SpeakerEditor::speakerMapChanged, this, [this](const QString& id) {
        m_emitting = true;
        emit speakerMapChanged(id);
        m_emitting = false;
    });
    connect(ed, &SpeakerEditor::peopleChanged, this, &EditorSessions::peopleChanged);
    connect(ed, &SpeakerEditor::voiceprintsChanged, this, &EditorSessions::voiceprintsChanged);
    connect(ed, &SpeakerEditor::summaryStaleChanged, this, [this, meetingId] { emit summaryStaleChanged(meetingId); });
    return ed;
}

void EditorSessions::close(const QString& meetingId)
{
    if (SpeakerEditor* ed = m_editors.take(meetingId)) ed->deleteLater();
}

void EditorSessions::forEach(const std::function<void(SpeakerEditor*)>& fn) const
{
    for (const QPointer<SpeakerEditor>& ed : m_editors)
        if (ed) fn(ed);
}

void EditorSessions::externalSpeakerMapChanged(const QString& meetingId)
{
    // A change from outside (old UI rename, auto-identification, person rename) -> the editor rereads the disk.
    if (m_emitting) return;
    if (SpeakerEditor* ed = m_editors.value(meetingId)) ed->refreshFromDisk();
}

void EditorSessions::notifyVoiceprintsChanged()
{
    for (const QPointer<SpeakerEditor>& ed : std::as_const(m_editors))
        if (ed) emit ed->speakersChanged();
}

void EditorSessions::summaryRegenerated(const QString& meetingId)
{
    if (SpeakerEditor* ed = m_editors.value(meetingId)) {
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
    if (SpeakerEditor* ed = m_editors.value(meetingId)) ed->reloadTranscript();
}

// ---- the summary's staleness ----

SummaryStaleInfo EditorSessions::summaryStale(const QString& meetingId) const
{
    const Meeting m = m_deps.store->load(meetingId);
    SpeakerEditor* ed = m_editors.value(meetingId);
    SummaryStaleInfo info = ed ? ed->summaryStale() : speakeredit::summaryStale(m);
    // The editor persists after every step, so the targeted part can be computed from disk.
    if (!m.id.isEmpty())
        speakeredit::fillTargetedStale(m, m_deps.personNames ? m_deps.personNames() : PersonNames(), info);
    return info;
}

SummaryStaleInfo EditorSessions::summaryStale(const Meeting& meeting) const
{
    if (SpeakerEditor* ed = m_editors.value(meeting.id)) return ed->summaryStale();
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
    if (SpeakerEditor* ed = m_editors.value(meetingId)) return ed->retranscribeImpact();
    return speakeredit::retranscribeImpact(m_deps.store->load(meetingId));
}

void EditorSessions::dismissSummaryStale(const QString& meetingId)
{
    if (SpeakerEditor* ed = m_editors.value(meetingId)) {
        ed->dismissSummaryStale();   // summaryStaleChanged goes out from the editor's signal
        return;
    }
    const Meeting m = m_deps.store->load(meetingId);
    if (!m.folder.isEmpty() && speakeredit::clearSummaryStale(m.folder))
        emit summaryStaleChanged(meetingId);
}

} // namespace tanara::editing
