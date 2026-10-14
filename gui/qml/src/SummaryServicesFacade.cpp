#include "SummaryServicesFacade.h"

#include "tanara/app/AppSettings.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/editing/session/SpeakerEditor.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/processing/summary/SummarizationService.h"
#include "tanara/processing/summary/TopicQueue.h"
#include "tanara/providers/core/ProviderRegistry.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace tanara_qml {

using namespace tanara;
using namespace tanara::processing;

SummaryServicesFacade::SummaryServicesFacade(const Deps& deps, QObject* parent)
    : ISummaryFacade(parent), m_deps(deps), m_noteController(deps.noteController)
{
    // The meeting-wide changes: jobs, the summary, its statements, the stale marker, the settings.
    connect(m_deps.jobs, &library::MeetingJobTracker::stateChanged, this,
            [this](const QString& id) { emit changed(id); });
    connect(m_deps.jobs, &library::MeetingJobTracker::jobProgressChanged, this,
            [this](const QString& id, const JobProgress&) { emit jobsChanged(id); });
    connect(m_deps.summaries, &SummarizationService::summaryReady, this,
            [this](const QString& id, const QString&) { emit summaryArrived(id); });
    connect(m_deps.summaries, &SummarizationService::summaryStatementsChanged, this,
            [this](const QString& id) { emit changed(id); });
    connect(m_deps.settings, &app::SettingsManager::settingsChanged, this,
            [this] { emit changed(QString()); });
    // The note (and the detected call) live in meeting.json: after saving / editing from another view.
    connect(m_deps.store, &library::MeetingRepository::meetingUpdated, this,
            [this](const QString& id) { emit noteUpdated(id); });

    TopicQueue& tq = m_deps.summaries->topics();
    connect(&tq, &TopicQueue::topicsReady, this,
            [this](const QString& id, const QVector<SummaryTopic>&) { emit topicsArrived(id); });
    connect(&tq, &TopicQueue::topicsChanged, this, [this](const QString& id) { emit topicsChanged(id); });
    connect(&tq, &TopicQueue::topicStatusChanged, this,
            [this](const QString& id, const QString& topicId) { emit topicStatusChanged(id, topicId); });
    connect(&tq, &TopicQueue::topicAnalysisReady, this,
            [this](const QString& id, const TopicAnalysis& a) { emit topicAnalysisReady(id, a); });
    connect(&tq, &TopicQueue::topicAnalysisFailed, this,
            [this](const QString& id, const QString& topicId, const QString& error) {
        emit topicAnalysisFailed(id, topicId, error);
    });
}

SummaryData SummaryServicesFacade::load(const QString& meetingId) const
{
    SummaryData d;
    if (meetingId.isEmpty())
        return d;
    const Meeting m = m_deps.store->load(meetingId);
    d.valid = !m.id.isEmpty();
    d.durationMs = m.durationMs;

    // Gating + provider.
    d.readiness = m_deps.hooks.canRun(meetingId);
    d.cloudSoon = !d.readiness.runnable && d.readiness.blockerKind == providers::BlockerKind::ProviderConfig;
    d.usesCloud = m_deps.hooks.usesCloud();
    d.providerName = chatProviderName(m_deps.settings->settings().providers.llm.selectedId);

    // Leftover error (the last summary attempt).
    const MeetingProcessingState ps = m_deps.jobs->state(m);
    if (ps.summaryError.isValid()) {
        d.summaryError = ps.summaryError;
        // Notes of the finished parts in the cache: a retry runs only the missing ones.
        d.errorKeptParts = !m.folder.isEmpty()
            && QFileInfo::exists(QDir(m.folder).filePath(SummaryService::cacheFileName()));
    }

    // The summary, structured (new: summary.json; old: recovered from summary.md).
    d.document = m_deps.summaries->document(meetingId);
    if (d.document.exists) {
        if (d.document.summary.statements.isEmpty()) {
            d.stale = m_deps.hooks.staleOfMeeting(m);
        } else {
            // Quick summary with sources: the targeted staleness and the statements per the current
            // speaker state (staleBecause): two small file reads.
            d.stale = m_deps.hooks.staleOfId(meetingId);
            d.statements = m_deps.hooks.statements(meetingId);
        }
    }
    d.speakers = speakers(meetingId);
    d.jobs = jobs(meetingId);
    return d;
}

SummaryJobs SummaryServicesFacade::jobs(const QString& meetingId) const
{
    SummaryJobs out;
    for (JobKind k : {JobKind::Summarize, JobKind::ExtractTopics}) {
        const JobProgress jp = m_deps.jobs->job(meetingId, k);
        if (jp.isValid()) { out.shown = jp; break; }
    }
    out.analyze = m_deps.jobs->job(meetingId, JobKind::AnalyzeTopics);
    return out;
}

void SummaryServicesFacade::watchEditor(const QString& meetingId, editing::SpeakerEditor* editor) const
{
    if (!editor || m_watched.value(meetingId) == editor)
        return;
    m_watched.insert(meetingId, editor);
    auto* self = const_cast<SummaryServicesFacade*>(this);   // a lazily made, mutable connection
    connect(editor, &editing::SpeakerEditor::speakersChanged, self,
            [self, meetingId] { emit self->speakersChanged(meetingId); });
}

SummarySpeakers SummaryServicesFacade::speakers(const QString& meetingId) const
{
    SummarySpeakers out;
    if (meetingId.isEmpty())
        return out;
    editing::SpeakerEditor* editor = m_deps.hooks.speakerEditor(meetingId);
    watchEditor(meetingId, editor);
    if (!editor)
        return out;
    for (const editing::EditorSpeaker& s : editor->speakers())
        out.list.append({s.displayName, s.anonymous ? QString() : s.displayName, s.colorIndex, s.talkShare});
    // The participants list shows the speakers by speaking time.
    std::stable_sort(out.list.begin(), out.list.end(),
                     [](const SummarySpeaker& a, const SummarySpeaker& b) { return a.share > b.share; });
    out.utteranceCount = editor->utteranceCount();
    return out;
}

SummaryQuote SummaryServicesFacade::quote(const QString& meetingId, const QString& utteranceId) const
{
    SummaryQuote q;
    if (meetingId.isEmpty())
        return q;
    editing::SpeakerEditor* editor = m_deps.hooks.speakerEditor(meetingId);
    if (!editor)
        return q;
    const int idx = editor->indexOf(utteranceId);
    if (idx < 0)
        return q;
    const editing::EditorUtterance u = editor->utteranceAt(idx);
    q.startMs = u.startMs;
    q.endMs = u.endMs;
    q.text = u.text.trimmed();
    for (const editing::EditorSpeaker& s : editor->speakers()) {
        if (s.key == u.speakerKey) {
            q.name = s.displayName;
            q.colorIndex = s.colorIndex;
            break;
        }
    }
    return q;
}

QString SummaryServicesFacade::chatProviderName(const QString& providerId) const
{
    return providers::ProviderRegistry::defaultInstance()
        .descriptor(providers::CapabilityKind::Chat, providerId).displayName;
}

bool SummaryServicesFacade::topicsResumable(const QString& meetingId) const
{
    return !meetingId.isEmpty() && !m_deps.summaries->document(meetingId).exists
        && !m_deps.summaries->topics().topics(meetingId).isEmpty();
}

bool SummaryServicesFacade::flagStatement(const QString& meetingId, const QString& statementId)
{
    return !meetingId.isEmpty() && m_deps.summaries->flagStatement(meetingId, statementId);
}

void SummaryServicesFacade::dismissStale(const QString& meetingId)
{
    if (!meetingId.isEmpty())
        m_deps.hooks.dismissStale(meetingId);   // -> the stale marker's signal -> reload
}

void SummaryServicesFacade::clearError(const QString& meetingId)
{
    if (!meetingId.isEmpty())
        m_deps.jobs->clearError(meetingId, JobKind::Summarize);
}

QVector<SummaryTopic> SummaryServicesFacade::topics(const QString& meetingId) const
{
    return m_deps.summaries->topics().topics(meetingId);
}

QVector<TopicStatus> SummaryServicesFacade::topicStatuses(const QString& meetingId) const
{
    return m_deps.summaries->topics().statuses(meetingId);
}

QVector<TopicAnalysis> SummaryServicesFacade::topicAnalyses(const QString& meetingId) const
{
    return m_deps.summaries->topics().analyses(meetingId);
}

void SummaryServicesFacade::setTopics(const QString& meetingId, const QVector<SummaryTopic>& topics)
{
    m_deps.summaries->topics().setTopics(meetingId, topics);   // topicsChanged
}

void SummaryServicesFacade::cancelTopic(const QString& meetingId, const QString& topicId)
{
    m_deps.summaries->topics().cancelTopic(meetingId, topicId);
}

} // namespace tanara_qml
