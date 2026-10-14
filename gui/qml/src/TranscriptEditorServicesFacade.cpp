#include "TranscriptEditorServicesFacade.h"

#include "tanara/app/SettingsManager.h"
#include "tanara/editing/session/EditorSessions.h"
#include "tanara/editing/session/PeopleDirectory.h"
#include "tanara/editing/session/SpeakerEditSession.h"
#include "tanara/editing/session/SpeakerEditor.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/library/store/UiStateRepository.h"
#include "tanara/library/tags/TagService.h"

namespace tanara_qml {

using namespace tanara;

TranscriptEditorServicesFacade::TranscriptEditorServicesFacade(const Deps& deps, QObject* parent)
    : ITranscriptEditorFacade(parent)
    , m_sessions(deps.sessions)
    , m_store(deps.store)
    , m_uiState(deps.uiState)
    , m_people(deps.people)
    , m_voiceprints(deps.voiceprints)
    , m_tags(deps.tags)
    , m_settings(deps.settings)
{
}

editing::ITranscriptEditorFacade::Session TranscriptEditorServicesFacade::open(const QString& meetingId)
{
    Session s;
    if (meetingId.isEmpty() || !m_sessions)
        return s;
    // TODO(P15-6): EditorSessions hands out the SpeakerEditor facade until P15-6 removes it; only its
    // two parts are taken from it here.
    editing::SpeakerEditor* ed = m_sessions->editor(meetingId);
    if (!ed)
        return s;
    s.session = ed->session();
    s.recheck = ed->recheck();
    const Meeting meeting = m_store ? m_store->load(meetingId) : Meeting();
    s.legacyTranscript = !s.session->hasTranscript() && meeting.hasTranscript;
    if (s.legacyTranscript) {
        // An old transcript stays readable: the text of transcript.md (with an upper limit), as plain
        // text (without the Markdown markers `[00:06]`, **Name**).
        s.legacyText = library::TranscriptRepository::readMarkdown(meeting.folder, 4 * 1024 * 1024);
        s.legacyText.remove(QLatin1Char('`')).remove(QStringLiteral("**"));
    }
    return s;
}

QVector<Track> TranscriptEditorServicesFacade::tracks(const QString& meetingId) const
{
    if (meetingId.isEmpty() || !m_store)
        return {};
    return m_store->load(meetingId).tracks;
}

bool TranscriptEditorServicesFacade::railVisible(const QString& meetingId) const
{
    return m_uiState && !m_uiState->filePath().isEmpty() && m_uiState->railVisible(meetingId);
}

void TranscriptEditorServicesFacade::setRailVisible(const QString& meetingId, bool visible)
{
    if (m_uiState && !m_uiState->filePath().isEmpty())
        m_uiState->setRailVisible(meetingId, visible);
}

QVector<editing::PersonInfo> TranscriptEditorServicesFacade::people() const
{
    if (m_people)
        m_people->refresh();
    return editing::listPeople(m_people, m_voiceprints, m_store);
}

QString TranscriptEditorServicesFacade::tagName(const QString& tagId) const
{
    return m_tags ? m_tags->tag(tagId).name : QString();
}

PersonId TranscriptEditorServicesFacade::selfId() const
{
    return m_tags ? m_tags->selfId() : PersonId();
}

bool TranscriptEditorServicesFacade::addPersonTag(const PersonId& person, const QString& tagId)
{
    return m_tags && m_tags->addPersonTag(person, tagId).isValid();
}

QVector<library::PersonTagQuestion> TranscriptEditorServicesFacade::newPersonTagQuestions(
    const QString& meetingId, const PersonId& person, const QString&)
{
    if (!m_tags || person.isNull())
        return {};
    const QVector<library::PersonTagQuestion> questions = m_tags->learningQuestions(meetingId, {person});
    m_tags->markLearningAsked(meetingId, questions);
    return questions;
}

void TranscriptEditorServicesFacade::answerTagQuestion(const library::PersonTagQuestion& question, bool yes)
{
    if (m_tags)
        m_tags->answerLearning(question, yes);
}

void TranscriptEditorServicesFacade::undoTagAnswer()
{
    if (m_tags && m_tags->canUndo())
        m_tags->undo();
}

bool TranscriptEditorServicesFacade::reviewAutoPlay() const
{
    return m_settings ? m_settings->settings().ui.reviewAutoPlay : m_reviewAutoPlay;
}

void TranscriptEditorServicesFacade::setReviewAutoPlay(bool on)
{
    m_reviewAutoPlay = on;
    if (!m_settings)
        return;
    app::AppSettings s = m_settings->settings();
    s.ui.reviewAutoPlay = on;
    m_settings->setSettings(s);
}

} // namespace tanara_qml
