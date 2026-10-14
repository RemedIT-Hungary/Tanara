#include "tanara/domain/Types.h"
#include "AppContext.h"

#include "ControllerFacades.h"
#include "JobSupport.h"
#include "LibraryServicesFacade.h"
#include "OverviewViewModel.h"
#include "RecorderServicesFacade.h"
#include "RecorderViewModel.h"
#include "ShellMeetingModel.h"
#include "TrackActivitySource.h"

#include "demo/ImportDemoFacade.h"
#include "demo/LibraryDemoFacade.h"
#include "demo/OverviewDemoFacade.h"
#include "demo/RecorderDemoFacade.h"
#include "demo/ShellDemoFacade.h"
#include "ControllerEvents.h"
#include "ParticipantsServicesFacade.h"
#include "PeopleServicesFacade.h"
#include "TagControllerBackend.h"
#include "demo/ParticipantsDemoFacade.h"
#include "demo/PeopleDemoFacade.h"
#include "demo/TagDemoBackend.h"

#include "tanara/app/AppController.h"
#include "tanara/app/Services.h"
#include "tanara/editing/session/SpeakerEditor.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/summary/SummaryRepository.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/library/tags/TagService.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QJSEngine>
#include <QLocale>
#include <QStyleHints>

#include <algorithm>

namespace tanara_qml {

namespace {

// The real TrackActivitySource: the core's activity computation still lives in the
// AppController (it becomes a service of its own later; then this adapter goes away).
class TrackActivityControllerSource : public TrackActivitySource {
public:
    explicit TrackActivityControllerSource(tanara::app::AppController* controller)
        : TrackActivitySource(controller), m_controller(controller)
    {
        connect(controller, &tanara::app::AppController::trackActivityReady, this,
                &TrackActivitySource::activityReady);
        connect(controller, &tanara::app::AppController::tracksChanged, this,
                &TrackActivitySource::tracksChanged);
    }

    tanara::Meeting meeting(const QString& meetingId) const override
    {
        if (meetingId.isEmpty() || !m_controller->store())
            return {};
        return m_controller->store()->load(meetingId);
    }
    tanara::MeetingActivity activity(const QString& meetingId) override
    {
        return m_controller->trackActivity(meetingId);
    }
    bool computing(const QString& meetingId) const override
    {
        return m_controller->trackActivityComputing(meetingId);
    }

private:
    tanara::app::AppController* m_controller;
};

} // namespace

// ---- ShellControllerFacade --------------------------------------------------------------

ShellControllerFacade::ShellControllerFacade(tanara::app::AppController* c)
    : tanara::editing::IShellFacade(c), m_c(c)
{
    using tanara::app::AppController;
    connect(c, &AppController::recordingStateChanged, this, &IShellFacade::recordingStateChanged);
    connect(c, &AppController::recordingFinished, this, &IShellFacade::recordingFinished);
    connect(c, &AppController::importFinished, this, &IShellFacade::importFinished);
    connect(c, &AppController::transcriptReady, this,
            [this](const QString& id, const QString&) { emit transcriptReady(id); });
    connect(c, &AppController::summaryReady, this,
            [this](const QString& id, const QString&) { emit summaryReady(id); });
    connect(c, &AppController::topicsReady, this,
            [this](const QString& id, const QVector<tanara::SummaryTopic>&) { emit topicsReady(id); });
    connect(c, &AppController::topicAnalysisQueueFinished, this, &IShellFacade::topicAnalysisQueueFinished);
    connect(c, &AppController::archiveFinished, this,
            [this](const QString& opId, bool ok, const QString& meetingId, const QString& path,
                   const QString& message) {
        emit archiveFinished(opId, ok, !ok && message == AppController::tr("Megszakítva."), meetingId, path,
                             message);
    });
    connect(c, &AppController::mixdownUpdated, this, &IShellFacade::mixdownUpdated);
    connect(c, &AppController::errorOccurred, this, &IShellFacade::errorOccurred);
    connect(c, &AppController::speakerMapChanged, this, &IShellFacade::speakerMapChanged);
    connect(c, &AppController::summaryStaleChanged, this, &IShellFacade::summaryStaleChanged);
    connect(c, &AppController::tracksChanged, this, &IShellFacade::tracksChanged);
    if (tanara::library::MeetingRepository* store = c->store()) {
        connect(store, &tanara::library::MeetingRepository::meetingUpdated, this, &IShellFacade::meetingUpdated);
        connect(store, &tanara::library::MeetingRepository::meetingRemoved, this, &IShellFacade::meetingRemoved);
    }
    if (tanara::library::MeetingJobTracker* jobs = c->jobs()) {
        using tanara::library::MeetingJobTracker;
        connect(jobs, &MeetingJobTracker::jobFinished, this, &IShellFacade::jobFinished);
        connect(jobs, &MeetingJobTracker::errorChanged, this, &IShellFacade::jobErrorChanged);
        connect(jobs, &MeetingJobTracker::stateChanged, this, &IShellFacade::jobStateChanged);
        connect(jobs, &MeetingJobTracker::jobProgressChanged, this,
                [this](const QString& id, const tanara::JobProgress&) { emit jobProgressChanged(id); });
    }
}

tanara::Meeting ShellControllerFacade::meeting(const QString& meetingId) const
{
    if (m_c && m_c->store() && !meetingId.isEmpty())
        return m_c->store()->load(meetingId);
    return {};
}

bool ShellControllerFacade::meetingExists(const QString& meetingId) const
{
    return !meetingId.isEmpty() && !meeting(meetingId).id.isEmpty();
}

QString ShellControllerFacade::meetingTitle(const QString& meetingId) const
{
    return meeting(meetingId).title;
}

int ShellControllerFacade::speakerCount(const tanara::Meeting& m) const
{
    return m_c ? m_c->transcripts()->speakerCount(m.folder) : 0;
}

tanara::SummaryStaleInfo ShellControllerFacade::summaryStale(const tanara::Meeting& m) const
{
    return m_c ? m_c->summaryStale(m) : tanara::SummaryStaleInfo{};
}

QString ShellControllerFacade::providerLabel(tanara::providers::WorkflowStep step) const
{
    return jobsupport::providerLabel(m_c, step);
}

tanara::MeetingProcessingState ShellControllerFacade::processingState(const QString& meetingId) const
{
    return m_c ? m_c->processingState(meetingId) : tanara::MeetingProcessingState{};
}

bool ShellControllerFacade::jobRunning(const QString& meetingId, tanara::JobKind kind) const
{
    return m_c && m_c->jobs() && m_c->jobs()->isRunning(meetingId, kind);
}

tanara::JobError ShellControllerFacade::lastJobError(const QString& meetingId, tanara::JobKind kind) const
{
    return m_c && m_c->jobs() ? m_c->jobs()->lastError(meetingId, kind) : tanara::JobError{};
}

tanara::RecordingState ShellControllerFacade::recordingState() const
{
    return m_c ? m_c->recordingState() : tanara::RecordingState::Idle;
}

tanara::editing::MeetingUndoService* ShellControllerFacade::meetingUndo(const QString& meetingId)
{
    return m_c ? m_c->meetingUndo(meetingId) : nullptr;
}

QObject* ShellControllerFacade::tagController() const
{
    return m_c.data();
}

tanara::providers::ReadinessResult ShellControllerFacade::canRun(tanara::providers::WorkflowStep step,
                                                                 const QString& meetingId) const
{
    return m_c ? m_c->canRun(step, meetingId) : tanara::providers::ReadinessResult{};
}

bool ShellControllerFacade::usesCloud(tanara::providers::WorkflowStep step) const
{
    return m_c && m_c->usesCloud(step);
}

void ShellControllerFacade::transcribe(const QString& meetingId) { if (m_c) m_c->transcribeMeeting(meetingId); }

void ShellControllerFacade::retranscribe(const QString& meetingId, bool keepBackup)
{
    if (m_c) m_c->retranscribeMeeting(meetingId, keepBackup);
}

tanara::editing::RetranscribeImpact ShellControllerFacade::retranscribeImpact(const QString& meetingId) const
{
    return m_c ? m_c->retranscribeImpact(meetingId) : tanara::editing::RetranscribeImpact{};
}

void ShellControllerFacade::summarize(const QString& meetingId) { if (m_c) m_c->summarizeMeeting(meetingId); }

QVector<tanara::SummaryTopic> ShellControllerFacade::topics(const QString& meetingId) const
{
    return m_c ? m_c->meetingTopics(meetingId) : QVector<tanara::SummaryTopic>{};
}

void ShellControllerFacade::extractTopics(const QString& meetingId)
{
    if (m_c) m_c->extractMeetingTopics(meetingId);
}

void ShellControllerFacade::generateComplexSummary(const QString& meetingId,
                                                   const QVector<tanara::SummaryTopic>& topics)
{
    if (m_c) m_c->generateComplexSummary(meetingId, topics);
}

void ShellControllerFacade::analyzeTopic(const QString& meetingId, const tanara::SummaryTopic& topic)
{
    if (m_c) m_c->analyzeTopic(meetingId, topic);
}

bool ShellControllerFacade::identifyAsync(const QString& meetingId)
{
    return m_c && m_c->identifyMeetingAsync(meetingId);
}

bool ShellControllerFacade::voiceIdentificationAvailable() const
{
    return m_c && m_c->voiceIdentificationAvailable();
}

bool ShellControllerFacade::participantApprovalPending(const QString& meetingId) const
{
    return m_c && m_c->participantApprovalPending(meetingId);
}

tanara::editing::SpeakerEditor* ShellControllerFacade::speakerEditor(const QString& meetingId)
{
    return m_c ? m_c->speakerEditor(meetingId) : nullptr;
}

QString ShellControllerFacade::personName(const tanara::PersonId& person)
{
    tanara::library::PeopleStore* people = m_c ? m_c->people() : nullptr;
    if (!people)
        return {};
    people->refresh();
    return people->displayName(person);
}

void ShellControllerFacade::requestLlmContext(int tokens) { if (m_c) m_c->requestLlmContext(tokens); }

void ShellControllerFacade::cancelJob(const QString& meetingId, tanara::JobKind kind)
{
    if (m_c) m_c->cancelJob(meetingId, kind);
}

void ShellControllerFacade::renameMeeting(const QString& meetingId, const QString& title)
{
    if (m_c) m_c->renameMeeting(meetingId, title);
}

void ShellControllerFacade::cancelAllJobs(const QString& meetingId) { if (m_c) m_c->cancelAllJobs(meetingId); }

void ShellControllerFacade::closeSpeakerEditor(const QString& meetingId)
{
    if (m_c) m_c->closeSpeakerEditor(meetingId);
}

void ShellControllerFacade::deleteMeeting(const QString& meetingId) { if (m_c) m_c->deleteMeeting(meetingId); }

void ShellControllerFacade::stopRecording()
{
    if (m_c && m_c->recordingState() == tanara::RecordingState::Recording)
        m_c->stopRecording();
}

bool ShellControllerFacade::importBusy() const
{
    return m_c && m_c->importer() && m_c->importer()->busy();
}

bool ShellControllerFacade::exportArchive(const QString& meetingId, const QString& zipPath)
{
    return m_c && !m_c->exportMeetingArchive(meetingId, zipPath).isEmpty();
}

bool ShellControllerFacade::importArchive(const QString& zipPath)
{
    return m_c && !m_c->importMeetingArchive(zipPath).isEmpty();
}

bool ShellControllerFacade::importFolder(const QString& folder)
{
    return m_c && !m_c->importMeetingFolder(folder).isEmpty();
}

// ---- ImportControllerFacade -------------------------------------------------------------

ImportControllerFacade::ImportControllerFacade(tanara::app::AppController* c)
    : tanara::library::IImportFacade(c), m_c(c)
{
    using tanara::app::AppController;
    if (tanara::library::AudioImporter* imp = c->importer()) {
        connect(imp, &tanara::library::AudioImporter::probed, this, &IImportFacade::probed);
        connect(imp, &tanara::library::AudioImporter::progress, this, &IImportFacade::progress);
    }
    connect(c, &AppController::importFinished, this, &IImportFacade::finished);
    connect(c, &AppController::importFailed, this, &IImportFacade::failed);
    connect(c, &AppController::importCancelled, this, &IImportFacade::cancelled);
}

void ImportControllerFacade::probeAsync(const QString& path)
{
    if (m_c && m_c->importer())
        m_c->importer()->probeAsync(path);
}

QString ImportControllerFacade::start(const tanara::library::ImportRequest& request)
{
    return m_c ? m_c->importAudio(request) : QString();
}

void ImportControllerFacade::cancel(const QString& importId)
{
    if (m_c) m_c->cancelJob(importId, tanara::JobKind::Import);
}

tanara::Tag ImportControllerFacade::createTag(const QString& name)
{
    tanara::library::TagService* svc = m_c ? m_c->tags() : nullptr;
    return svc ? svc->create(name) : tanara::Tag{};   // for an existing key, the existing tag
}

void ImportControllerFacade::applyTags(const QString& meetingId, const QStringList& ids)
{
    if (!m_c || ids.isEmpty() || !m_c->tags())
        return;
    // A tag deleted meanwhile is not attached.
    QStringList valid;
    for (const QString& id : ids)
        if (m_c->tags()->tag(id).isValid()) valid << id;
    if (!valid.isEmpty())
        m_c->tags()->setTags(meetingId, valid, tanara::TagSource::Manual);
}

QVector<tanara::TagSuggestion> ImportControllerFacade::suggestionsFor(const QString& title) const
{
    return m_c ? m_c->draftTagSuggestions(title) : QVector<tanara::TagSuggestion>{};
}

// ---- OverviewControllerFacade -----------------------------------------------------------

namespace ov {
using namespace tanara;

// detail: the badge's tooltip (the evidence's longer explanation; empty -> the label).
QVariantMap source(const QString& icon, const QString& text, bool negative = false, bool accent = false,
                   const QString& detail = QString())
{
    return {{QStringLiteral("icon"), icon}, {QStringLiteral("text"), text},
            {QStringLiteral("negative"), negative}, {QStringLiteral("accent"), accent},
            {QStringLiteral("detail"), detail}};
}

QVariantMap step(const QString& key, const QString& title, const QString& sub, const QString& state,
                 bool parallel = false, int progress = -1)
{
    return {{QStringLiteral("key"), key}, {QStringLiteral("title"), title},
            {QStringLiteral("sub"), sub}, {QStringLiteral("state"), state},
            {QStringLiteral("parallel"), parallel}, {QStringLiteral("progress"), progress}};
}

QVariantMap stat(const QString& label, const QString& value, const QString& sub)
{
    return {{QStringLiteral("label"), label}, {QStringLiteral("value"), value},
            {QStringLiteral("sub"), sub}};
}

// A participant's evidence -> badges (the design: voice / track side / tag / calendar / manual;
// contradicting evidence is dashed, dim).
QVariantList sourcesOf(const Participant& p, bool self)
{
    QVariantList out;
    if (self) out << source(QStringLiteral("user"), OverviewControllerFacade::tr("te"), false, false,
                            OverviewControllerFacade::tr("A Beállításokban megadott saját neved"));
    for (const Evidence& e : p.evidence) {
        const bool neg = e.polarity == Polarity::Contradict;
        switch (e.kind) {
        case EvidenceKind::Manual:
            if (!self) out << source(QStringLiteral("user"), OverviewControllerFacade::tr("kézi"), false, false,
                                     e.detail.isEmpty() ? OverviewControllerFacade::tr("Kézzel adtad hozzá") : e.detail);
            break;
        case EvidenceKind::Voice:
            out << source(QStringLiteral("fingerprint"), e.text, neg, false, e.detail);
            break;
        case EvidenceKind::Side:
            out << source(e.text.contains(QStringLiteral("mikrofon")) ? QStringLiteral("mic")
                                                                       : QStringLiteral("phone"),
                          e.text, neg, false, e.detail);
            break;
        case EvidenceKind::Tag:
            // The chip ("#Nordvik", "#Nordvik +1"); the explanation is in detail (ranking::tagEvidenceOf).
            out << source(QStringLiteral("tag"), e.text, neg);
            break;
        case EvidenceKind::Calendar:
            out << source(QStringLiteral("calendar"), e.text.isEmpty() ? OverviewControllerFacade::tr("naptár") : e.text,
                          neg, false, e.detail);
            break;
        default:
            break;
        }
    }
    // Without evidence (old data) at least the side shows.
    if (p.evidence.isEmpty()) {
        for (const QString& side : p.sides)
            out << (side == QLatin1String("mic")
                        ? source(QStringLiteral("mic"), OverviewControllerFacade::tr("mikrofon"), false, false,
                                 OverviewControllerFacade::tr("A saját mikrofon sávján beszél"))
                        : source(QStringLiteral("phone"), OverviewControllerFacade::tr("hívás"), false, false,
                                 OverviewControllerFacade::tr("A hívás hangján (távoli oldal) beszél")));
    }
    return out;
}

QVariantMap personRow(const QString& id, const QString& name, int colorIndex, const QString& sub,
                      const QVariantList& sources, double talkShare, bool self = false,
                      bool suggested = false, bool dimmed = false, bool removable = false)
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("name"), name},
            {QStringLiteral("monogram"), OverviewViewModel::monogramOf(name)},
            {QStringLiteral("colorIndex"), colorIndex}, {QStringLiteral("sub"), sub},
            {QStringLiteral("sources"), sources}, {QStringLiteral("talkShare"), talkShare},
            {QStringLiteral("self"), self}, {QStringLiteral("suggested"), suggested},
            {QStringLiteral("dimmed"), dimmed}, {QStringLiteral("removable"), removable},
            {QStringLiteral("speakerKey"), QString()}, {QStringLiteral("anonymous"), false},
            {QStringLiteral("unheard"), false}};
}

// After the transcript, a participant not tied to a speaker (never spoke / added manually):
// a dim row with a "not heard" badge, removable.
QVariantMap unheardRow(const Participant& p, const QString& name, int colorIndex)
{
    QVariantList src = sourcesOf(p, false);
    src.prepend(source(QStringLiteral("fingerprint"), OverviewControllerFacade::tr("nem hallottuk"), true, false,
                       OverviewControllerFacade::tr("Az átiratban nem találtuk a hangját: nincs beszélő a nevén")));
    QVariantMap row = personRow(p.id, name, colorIndex, QString(), src, -1.0, false, false, true, true);
    row.insert(QStringLiteral("unheard"), true);
    return row;
}

// The row is one of the transcript's speakers ("Why this one?" and "Name..." bind to this key).
QVariantMap withSpeaker(QVariantMap row, const QString& speakerKey, bool anonymous)
{
    row.insert(QStringLiteral("speakerKey"), speakerKey);
    row.insert(QStringLiteral("anonymous"), anonymous);
    return row;
}

// The number of tag-based person suggestions on the Overview.
constexpr int kMaxSuggestedPeople = 3;


} // namespace ov

OverviewControllerFacade::OverviewControllerFacade(tanara::app::AppController* c)
    : tanara::library::IOverviewFacade(c), m_c(c)
{
    using tanara::app::AppController;
    auto mine = [this](const QString& id) { emit changed(id); };
    connect(c, &AppController::participantsChanged, this, mine);
    connect(c, &AppController::participantAnalysisStarted, this, mine);
    connect(c, &AppController::participantAnalysisFinished, this, mine);
    connect(c, &AppController::participantAnalysisProgress, this,
            [this](const QString& id, int done, int total, int percent) {
        m_progress.insert(id, {done, total, percent});
        emit changed(id);
    });
    connect(c, &AppController::tracksChanged, this, mine);
    connect(c, &AppController::speakerMapChanged, this, mine);
    connect(c, &AppController::mixdownUpdated, this, [mine](const QString& id, bool) { mine(id); });
    connect(c, &AppController::transcriptReady, this, [mine](const QString& id, const QString&) { mine(id); });
    connect(c, &AppController::summaryReady, this, [mine](const QString& id, const QString&) { mine(id); });
    connect(c, &AppController::summaryStaleChanged, this, mine);
    if (tanara::library::MeetingRepository* store = c->store())
        connect(store, &tanara::library::MeetingRepository::meetingUpdated, this, mine);
    if (tanara::library::MeetingJobTracker* jobs = c->jobs()) {
        connect(jobs, &tanara::library::MeetingJobTracker::stateChanged, this, mine);
        connect(jobs, &tanara::library::MeetingJobTracker::jobProgressChanged, this,
                [mine](const QString& id, const tanara::JobProgress&) { mine(id); });
    }
}

QString OverviewControllerFacade::addParticipant(const QString& meetingId, const QString& name)
{
    using namespace tanara;
    const QString n = name.trimmed();
    if (n.isEmpty() || !m_c)
        return {};
    // The typed name: a known person (PersonId or name), else a new one.
    const PersonChoice choice = m_c->people() ? m_c->people()->choiceFor(n) : PersonChoice::create(n);
    return m_c->addParticipant(meetingId, choice);
}

void OverviewControllerFacade::removeParticipant(const QString& meetingId, const QString& id)
{
    using namespace tanara;
    if (!m_c || meetingId.isEmpty() || id.isEmpty())
        return;
    if (id.startsWith(QLatin1String("suggest:"))) {
        // The suggestion's lasting rejection for this meeting (a shared-undo step; the stack must
        // be created first, since it does not take tag steps from before the attachment).
        m_c->meetingUndo(meetingId);
        if (library::TagService* tags = m_c->tags())
            tags->rejectPersonForMeeting(PersonId::fromString(id.mid(8)), meetingId);
    } else {
        m_c->removeParticipant(meetingId, id);
    }
}

bool OverviewControllerFacade::setSolo(const QString& meetingId)
{
    return m_c && m_c->setSoloMeeting(meetingId);
}

bool OverviewControllerFacade::reanalyze(const QString& meetingId)
{
    if (!m_c || meetingId.isEmpty())
        return false;
    return m_c->analyzeParticipants(meetingId);
}

void OverviewControllerFacade::setContextNote(const QString& meetingId, const QString& note)
{
    if (m_c) m_c->setMeetingContextNote(meetingId, note.trimmed());
}

tanara::library::OverviewSnapshot OverviewControllerFacade::snapshot(const QString& meetingId)
{
    using namespace tanara;
    using namespace ov;
    app::AppController* c = m_c;
    library::OverviewSnapshot s;
    if (!c)
        return s;
    const Meeting m = (c->store() && !meetingId.isEmpty()) ? c->store()->load(meetingId) : Meeting();
    const MeetingProcessingState proc = c->processingState(meetingId);
    const PersonId selfId = c->selfPerson().id;
    PersonNames personNames;
    if (c->people()) { c->people()->refresh(); personNames = c->people()->names(); }
    auto nameOf = [&personNames](const PersonId& p) { return personName(personNames, p); };

    auto job = [&proc](JobKind kind) -> const JobProgress* {
        for (const JobProgress& j : proc.jobs)
            if (j.kind == kind) return &j;
        return nullptr;
    };
    const JobProgress* transcribeJob = job(JobKind::Transcribe);
    const JobProgress* mixdownJob = job(JobKind::Mixdown);
    const JobProgress* summaryJob = job(JobKind::Summarize);

    s.hasTranscript = m.hasTranscript;
    s.title = m.title;
    s.transcribing = transcribeJob != nullptr;
    s.contextNote = m.contextNote;
    s.approvalPending = !m.id.isEmpty() && c->participantApprovalPending(m.id);
    s.analysisRunning = !m.id.isEmpty() && c->participantAnalysisRunning(m.id);
    if (!s.analysisRunning) m_progress.remove(meetingId);
    const AnalysisProgress progress = m_progress.value(meetingId);
    // "Re-analyze": there is audio and a model, it is not running, and not "only I spoke".
    s.canReanalyze = !m.id.isEmpty() && !s.analysisRunning && !m.tracks.isEmpty()
                     && c->voiceIdentificationAvailable() && !(m.approval && m.approval->solo)
                     && (m.hasTranscript || !transcribeJob);
    s.approved = m.approval.has_value() && !m.approval->skipped;

    // ---- participants ----
    const QVector<Participant> parts = m.id.isEmpty() ? QVector<Participant>() : c->participants(m.id);
    s.candidateCount = int(std::count_if(parts.cbegin(), parts.cend(), [](const Participant& p) {
        return p.source != ParticipantSource::Manual;
    }));
    editing::SpeakerEditor* ed = m.hasTranscript ? c->speakerEditor(m.id) : nullptr;
    const QVector<editing::EditorSpeaker> speakers = ed ? ed->speakers() : QVector<editing::EditorSpeaker>();

    QVariantList rows;
    PersonIds names;            // the people already listed
    QVariantList anonymous;
    if (!speakers.isEmpty()) {
        // After the transcript: the speakers (with color, share), with the participant's evidence.
        QVector<editing::EditorSpeaker> sorted = speakers;
        std::sort(sorted.begin(), sorted.end(),
                  [](const editing::EditorSpeaker& a, const editing::EditorSpeaker& b) { return a.colorIndex < b.colorIndex; });
        bool anyNamed = false;
        for (const editing::EditorSpeaker& s : sorted) {
            if (s.utteranceCount == 0 && !s.added) continue;
            const Participant* match = nullptr;
            for (const Participant& p : parts) {
                if ((!s.personId.isNull() && p.personId == s.personId)
                    || (!s.rawLabel.isEmpty() && p.rawSpeakerIds.contains(s.rawLabel))) {
                    match = &p;
                    break;
                }
            }
            const bool self = s.isSelf || (!selfId.isNull() && s.personId == selfId);
            if (!s.anonymous) anyNamed = true;
            QString name = s.displayName;
            if (self) name = tr("%1 (te)").arg(name);
            const QVariantList src = match ? sourcesOf(*match, false) : QVariantList();
            // Removable: with a participant record, or without one too if it has a name (its rows
            // become anonymous: AppController::removeParticipant with the speaker key).
            rows << withSpeaker(personRow(match ? match->id : s.key, name, s.colorIndex,
                                          s.anonymous ? tr("névtelen · elnevezhető") : QString(), src,
                                          s.talkShare, self, false, false, match != nullptr || !s.anonymous),
                                s.key, s.anonymous);
            if (!s.personId.isNull()) names << s.personId;
            anonymous << QVariantMap{{QStringLiteral("name"), s.displayName},
                                     {QStringLiteral("share"), s.talkShare},
                                     {QStringLiteral("colorIndex"), s.colorIndex}};
        }
        // Participants not tied to a speaker but with a name (manual / never spoke) after the
        // speakers, dim; removable.
        const QVector<Participant> unheard = c->unheardParticipants(m.id);
        int colorIndex = int(sorted.size());
        for (const Participant& p : unheard) {
            if (names.contains(p.personId))
                continue;
            rows << unheardRow(p, nameOf(p.personId), colorIndex++);
            names << p.personId;
        }
        s.peopleState = rows.isEmpty() ? QStringLiteral("empty")
                      : (anyNamed || !unheard.isEmpty()) ? QStringLiteral("list") : QStringLiteral("nobody");
    } else {
        int i = 0;
        for (const Participant& p : parts) {
            const bool self = !selfId.isNull() && p.personId == selfId;
            QString name = p.personId.isNull() ? tr("Ismeretlen hang %1").arg(i + 1) : nameOf(p.personId);
            if (self) name = tr("%1 (te)").arg(name);
            rows << withSpeaker(personRow(p.id, name, i,
                                          p.personId.isNull() ? tr("névtelen · elnevezhető") : QString(),
                                          sourcesOf(p, self), p.talkShare > 0 ? p.talkShare : -1.0, self,
                                          false, false, true),
                                QString(), p.personId.isNull());
            if (!p.personId.isNull()) names << p.personId;
            ++i;
        }
        s.peopleState = rows.isEmpty() ? QStringLiteral("empty") : QStringLiteral("list");
    }
    s.anonymousSpeakers = s.peopleState == QLatin1String("nobody") ? anonymous : QVariantList();

    // Tag-based suggestion: people learned from the meeting's tags who are not here yet. The
    // core filters out those already here, the own person and those rejected for this meeting
    // (rejectPersonForMeeting); at most 3.
    if (library::TagService* tags = c->tags(); tags && !m.id.isEmpty() && s.peopleState != QLatin1String("nobody")) {
        int added = 0;
        const QVector<library::PersonTagStat> stats = tags->suggestPeopleForMeeting(m.id, names, kMaxSuggestedPeople);
        for (const library::PersonTagStat& st : stats) {
            if (names.contains(st.personId))
                continue;
            const QString tagName = tags->tag(st.tagId).name;
            rows << personRow(QStringLiteral("suggest:") + st.personId.toString(), st.name, -1, tr("javasolt"),
                              {source(QStringLiteral("tag"),
                                      tr("a #%1 alapján · %2 / %3").arg(tagName).arg(st.shared).arg(st.tagTotal),
                                      false, true)},
                              -1.0, false, true, false, true);
            names << st.personId;
            ++added;
        }
        if (added > 0 && s.peopleState == QLatin1String("empty"))
            s.peopleState = QStringLiteral("list");
    }
    s.participants = rows;
    if (m.approval && m.approval->solo) {
        s.peopleHint = tr("csak én beszéltem");
    } else if (s.approved && m.approval->at.size() > 0) {
        const QDateTime at = QDateTime::fromString(m.approval->at, Qt::ISODate).toLocalTime();
        s.peopleHint = at.isValid() ? tr("jóváhagyva %1-kor").arg(QLocale().toString(at, QStringLiteral("MMM d. HH:mm")))
                                    : tr("jóváhagyva");
    } else {
        s.peopleHint = s.peopleState == QLatin1String("list") ? tr("naptár · hang · címke · kézi") : QString();
    }

    // Who speaks on each side (the Tracks rows' subtext).
    QStringList micNames, loopNames;
    for (const Participant& p : parts) {
        if (p.sides.contains(QStringLiteral("mic"))) micNames << (p.personId.isNull() ? tr("névtelen") : nameOf(p.personId));
        if (p.sides.contains(QStringLiteral("loopback"))) loopNames << nameOf(p.personId);
    }
    s.sideWho.clear();
    if (micNames.size() == 1) s.sideWho.insert(QStringLiteral("mic"), micNames.first());
    else if (!micNames.isEmpty()) s.sideWho.insert(QStringLiteral("mic"), tr("%n résztvevő", nullptr, int(micNames.size())));
    if (loopNames.size() == 1 && !loopNames.first().isEmpty()) s.sideWho.insert(QStringLiteral("loopback"), loopNames.first());
    else if (!loopNames.isEmpty()) s.sideWho.insert(QStringLiteral("loopback"), tr("%n résztvevő", nullptr, int(loopNames.size())));

    // ---- processing ----
    int included = 0, excluded = 0, measured = 0;
    for (const Track& t : m.tracks) {
        if (t.included()) ++included;
        if (!t.excludedReason.isEmpty()) ++excluded;
        if (t.speechRatio >= 0) ++measured;
    }
    QVariantList steps;
    const QString durationText = jobsupport::formatDuration(m.durationMs);
    steps << step(QStringLiteral("recording"), tr("Felvétel"),
                  tr("%1 · %n sáv", nullptr, int(m.tracks.size())).arg(durationText),
                  m.tracks.isEmpty() ? QStringLiteral("waiting") : QStringLiteral("done"));
    QString tracksSub = tr("%n aktív", nullptr, included);
    if (excluded > 0) tracksSub += QStringLiteral(" · ") + tr("%n kimaradt, nincs rajta beszéd", nullptr, excluded);
    // The speech check for not-yet-measured tracks is started by the view (in a separate recorder
    // process or on an older meeting nobody else would); the step "runs" only while the core
    // really measures, otherwise the bar would spin forever.
    if (measured < m.tracks.size() && !m.hasTranscript) c->checkTrackSpeech(meetingId);
    // The pre-transcript audio analysis from the raw tracks, on opening too (if not yet run on these).
    if (!m.hasTranscript) c->maybeAnalyzeParticipants(meetingId);
    const bool speechRunning = c->speechCheckRunning(meetingId);
    steps << step(QStringLiteral("tracks"), tr("Sávok"), tracksSub,
                  speechRunning ? QStringLiteral("running") : QStringLiteral("done"),
                  false, speechRunning ? -2 : -1);
    const bool mixRunning = proc.mixdownRunning || mixdownJob;
    const bool mixReady = !m.mixdownFile.isEmpty() && !m.mixdownDirty;
    steps << step(QStringLiteral("mixdown"), tr("Lekeverés"),
                  mixRunning ? tr("fut · %1%").arg(qMax(0, proc.mixdownPercent))
                  : m.mixdownDirty ? tr("a sávok változtak azóta")
                                   : tr("%n sávból", nullptr, included),
                  mixRunning ? QStringLiteral("running")
                  : m.mixdownDirty ? QStringLiteral("attention")
                  : mixReady ? QStringLiteral("done") : QStringLiteral("waiting"),
                  false, mixRunning ? qMax(0, proc.mixdownPercent) : -1);
    QString voiceSub, voiceState;
    int voiceProgress = -1;
    if (s.analysisRunning) {
        // "track k / N" (per the core's participantAnalysisProgress signal); "running" until the first signal.
        if (progress.total > 0) {
            voiceSub = tr("%1 / %2 sáv").arg(progress.done).arg(progress.total);
            voiceProgress = progress.percent;
        } else {
            voiceSub = tr("fut");
            voiceProgress = -2;
        }
        voiceState = QStringLiteral("running");
    } else if (s.approvalPending) {
        voiceSub = tr("%n jelölt · jóváhagyásra vár", nullptr, s.candidateCount);
        voiceState = QStringLiteral("attention");
    } else if (m.approval.has_value() || !parts.isEmpty()) {
        const int known = int(std::count_if(parts.cbegin(), parts.cend(),
                                            [](const Participant& p) { return !p.personId.isNull(); }));
        voiceSub = m.approval && m.approval->solo ? tr("csak én beszéltem")
                 : known == 0 ? tr("nincs ismert hang")
                              : tr("%n résztvevő", nullptr, known);
        voiceState = QStringLiteral("done");
    } else {
        voiceSub = c->voiceIdentificationAvailable() ? tr("nem futott") : tr("nincs hangmodell");
        voiceState = m.hasTranscript ? QStringLiteral("done") : QStringLiteral("waiting");
    }
    steps << step(QStringLiteral("voice"), tr("Hanglenyomat-elemzés"), voiceSub, voiceState, true,
                  voiceProgress);
    if (transcribeJob) {
        const QVariantMap d = ShellMeetingModel::describeJob(*transcribeJob);
        const int pct = d.value(QStringLiteral("percent")).toInt();
        QStringList sub;
        const QString provider = jobsupport::providerLabel(c, providers::WorkflowStep::Transcribe).section(QStringLiteral(" · "), 0, 0);
        if (!provider.isEmpty()) sub << provider;
        if (pct >= 0) sub << QStringLiteral("%1%").arg(pct);
        const int eta = transcribeJob->etaSeconds();
        if (eta >= 0) sub << tr("kb. %n perc", nullptr, qMax(1, (eta + 30) / 60));
        steps << step(QStringLiteral("transcript"), tr("Átirat"), sub.join(QStringLiteral(" · ")),
                      QStringLiteral("running"), true, pct >= 0 ? pct : -2);
    } else if (m.hasTranscript) {
        const QString sub = ed ? tr("%n megszólalás", nullptr, ed->utteranceCount()) + QStringLiteral(" · ")
                                     + tr("%n beszélő", nullptr, int(speakers.size()))
                               : tr("kész");
        steps << step(QStringLiteral("transcript"), tr("Átirat"), sub, QStringLiteral("done"), true);
    } else {
        steps << step(QStringLiteral("transcript"), tr("Átirat"),
                      proc.transcriptState == StepState::Failed ? tr("nem sikerült") : tr("még nem indult"),
                      proc.transcriptState == StepState::Failed ? QStringLiteral("attention")
                                                                : QStringLiteral("waiting"), true);
    }
    QDateTime summaryAt;
    if (m.hasSummary)
        summaryAt = QFileInfo(library::SummaryRepository::markdownPath(m.folder)).lastModified();
    if (summaryJob) {
        const int pct = ShellMeetingModel::describeJob(*summaryJob).value(QStringLiteral("percent")).toInt();
        steps << step(QStringLiteral("summary"), tr("Összefoglaló"), pct >= 0 ? QStringLiteral("%1%").arg(pct) : tr("fut"),
                      QStringLiteral("running"), false, pct >= 0 ? pct : -2);
    } else if (m.hasSummary) {
        steps << step(QStringLiteral("summary"), tr("Összefoglaló"),
                      proc.summaryStale ? tr("elavult") : tr("kész"),
                      proc.summaryStale ? QStringLiteral("attention") : QStringLiteral("done"));
    } else {
        steps << step(QStringLiteral("summary"), tr("Összefoglaló"),
                      m.hasTranscript ? tr("még nem készült") : tr("az átirat után"), QStringLiteral("waiting"));
    }
    s.steps = steps;
    s.allDone = std::all_of(steps.cbegin(), steps.cend(), [](const QVariant& v) {
        return v.toMap().value(QStringLiteral("state")).toString() == QLatin1String("done");
    });
    s.stepsSummary = tr("felvétel · lekeverés · átirat · összefoglaló");

    // ---- DATA ----
    QVariantList stats;
    if (m.hasTranscript && ed) {
        qint64 speech = 0;
        int corrected = 0;
        for (const editing::EditorUtterance& u : ed->utterances()) {
            speech += qMax<qint64>(0, u.endMs - u.startMs);
            if (u.manuallyCorrected) ++corrected;
        }
        const int anon = int(std::count_if(speakers.cbegin(), speakers.cend(),
                                           [](const editing::EditorSpeaker& s) { return s.anonymous && s.utteranceCount > 0; }));
        const int active = int(std::count_if(speakers.cbegin(), speakers.cend(),
                                             [](const editing::EditorSpeaker& s) { return s.utteranceCount > 0; }));
        stats << stat(tr("HOSSZ"), durationText, tr("felvétel"))
              << stat(tr("BESZÉD"), QStringLiteral("%1:%2").arg(speech / 3600000)
                                        .arg(int((speech / 60000) % 60), 2, 10, QLatin1Char('0')),
                      tr("%n sávon", nullptr, included))
              << stat(tr("BESZÉLŐK"), QString::number(active), anon > 0 ? tr("%n névtelen", nullptr, anon) : tr("mind elnevezve"))
              << stat(tr("MEGSZÓLALÁS"), QString::number(ed->utteranceCount()), tr("%n javítva", nullptr, corrected))
              << stat(tr("ÁTNÉZENDŐ"), QString::number(ed->uncertainCount()), tr("az Átirat fülön"))
              << stat(tr("ÖSSZEFOGLALÓ"), m.hasSummary ? tr("kész") : tr("nincs"),
                      summaryAt.isValid() ? QLocale().toString(summaryAt, QStringLiteral("MMM d. HH:mm")) : QString());
    }
    s.stats = stats;
    return s;
}


AppContext::AppContext(QObject* parent) : QObject(parent)
{
    // Following the system's color scheme (only matters in "system" mode). Without a
    // QGuiApplication (e.g. QTEST_GUILESS_MAIN) there is no styleHints -> stays light.
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                this, &AppContext::updateDark);
    }
    updateDark();
}

AppContext* AppContext::instance()
{
    // Deliberately has no parent and is never freed: it stays valid after the QML engine shuts
    // down (the view models' destructors may still refer to it).
    static AppContext* s_instance = new AppContext;
    return s_instance;
}

AppContext* AppContext::create(QQmlEngine*, QJSEngine*)
{
    AppContext* ctx = instance();
    QJSEngine::setObjectOwnership(ctx, QJSEngine::CppOwnership);
    return ctx;
}

QString AppContext::normalizedThemeMode(const QString& mode)
{
    const QString m = mode.trimmed().toLower();
    if (m == QLatin1String("light") || m == QLatin1String("dark"))
        return m;
    return QStringLiteral("system");
}

void AppContext::setThemeMode(const QString& mode)
{
    const QString m = normalizedThemeMode(mode);
    if (m == m_themeMode)
        return;
    m_themeMode = m;
    emit themeModeChanged();
    updateDark();
}

void AppContext::updateDark()
{
    bool dark = false;
    if (m_themeMode == QLatin1String("dark")) {
        dark = true;
    } else if (m_themeMode == QLatin1String("system")
               && qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    }
    if (dark == m_dark)
        return;
    m_dark = dark;
    emit darkChanged();
}

QString AppContext::version() const { return tanara::libraryVersion(); }

void AppContext::setDemo(bool demo)
{
    if (demo == m_demo)
        return;
    m_demo = demo;
    emit demoChanged();
}

tanara::app::AppController* AppContext::controller() const
{
    return qobject_cast<tanara::app::AppController*>(m_controller.data());
}

QObject* AppContext::controllerObject() const
{
    return m_controller.data();
}

void AppContext::setController(tanara::app::AppController* controller)
{
    if (controller == m_controller.data())
        return;
    m_controller = controller;
    emit controllerChanged();
}

const UiServices& AppContext::services()
{
    // The same predicate as the view models' old "demo mode": no controller, or App.demo.
    tanara::app::AppController* c = controller();
    return (c == nullptr || m_demo) ? demoServices() : servicesFor(c);
}

const UiServices& AppContext::demoServices()
{
    if (!m_demoServices) {
        m_demoServices = std::make_unique<UiServices>();
        m_demoServices->recorder = new tanara_demo::RecorderDemoFacade(QString(),
                                       tanara_demo::RecorderDemoFacade::Fixture::Recorder, this);
        m_demoServices->trackActivity = [](const QString& scenario, QObject* parent) -> TrackActivitySource* {
            return new TrackActivityDemoSource(scenario, parent);
        };
        m_demoServices->library = new tanara_demo::LibraryDemoFacade(this);
        m_demoServices->shell = new tanara_demo::ShellDemoFacade(this);
        m_demoServices->import = new tanara_demo::ImportDemoFacade(this);
        m_demoServices->overview = [](const QString& scenario, QObject* parent) -> tanara::library::IOverviewFacade* {
            return new tanara_demo::OverviewDemoFacade(scenario, parent);
        };
        // The demo facades keep per-view-model state (the design state): one for each asking model.
        m_demoServices->tagBackend = [](QObject* parent) -> tanara::library::TagBackend* {
            return new tanara_demo::TagDemoBackend(tanara_demo::TagDemoBackend::Content::Sample, parent);
        };
        m_demoServices->people = [](QObject* parent) -> tanara::library::IPeopleFacade* {
            return new tanara_demo::PeopleDemoFacade(parent);
        };
        m_demoServices->participants = [](QObject* parent) -> tanara::editing::IParticipantsFacade* {
            return new tanara_demo::ParticipantsDemoFacade(parent);
        };
    }
    return *m_demoServices;
}

const UiServices& AppContext::servicesFor(tanara::app::AppController* c)
{
    if (!c)
        return demoServices();
    const auto it = m_realServices.find(c);
    if (it != m_realServices.end())
        return it->second;

    // The facades are children of the controller: they go away with it.
    tanara::app::Services* sv = c->services();
    // The controller still merges several services' signals: the people / participants facades get
    // them through one object (see ControllerEvents).
    auto* events = new ControllerEvents(c);
    connect(c, &tanara::app::AppController::peopleChanged, events, &ControllerEvents::peopleChanged);
    connect(c, &tanara::app::AppController::voiceprintsChanged, events, &ControllerEvents::voiceprintsChanged);
    connect(c, &tanara::app::AppController::voiceModelsChanged, events, &ControllerEvents::voiceModelsChanged);
    connect(c, &tanara::app::AppController::voiceprintBackfillFinished, events,
            [events](int, int) { emit events->voiceprintBackfillFinished(); });
    connect(c, &tanara::app::AppController::participantsChanged, events, &ControllerEvents::participantsChanged);
    connect(c, &tanara::app::AppController::participantAnalysisStarted, events,
            &ControllerEvents::participantAnalysisStarted);
    connect(c, &tanara::app::AppController::participantAnalysisFinished, events,
            &ControllerEvents::participantAnalysisFinished);
    connect(c, &tanara::app::AppController::tracksChanged, events, &ControllerEvents::tracksChanged);
    connect(c, &tanara::app::AppController::speakerMapChanged, events, &ControllerEvents::speakerMapChanged);

    TagControllerBackend::Deps tagDeps;
    tagDeps.tags = c->tags();
    tagDeps.suggestions = &sv->tagSuggestions();
    tagDeps.people = c->people();

    RecorderServicesFacade::Deps rd;
    rd.recording = &sv->recording();
    rd.store = c->store();
    rd.tags = c->tags();
    rd.settings = c->settings();
    rd.tagBackend = new TagControllerBackend(tagDeps, c);   // the recorder's tag input, shared
    UiServices set;
    set.recorder = new RecorderServicesFacade(rd, c);
    auto* activity = new TrackActivityControllerSource(c);
    set.trackActivity = [activity](const QString&, QObject*) -> TrackActivitySource* { return activity; };
    set.library = new LibraryServicesFacade(c->library(), c->tags(), c);
    set.shell = new ShellControllerFacade(c);
    set.import = new ImportControllerFacade(c);
    auto* overview = new OverviewControllerFacade(c);
    set.overview = [overview](const QString&, QObject*) -> tanara::library::IOverviewFacade* { return overview; };
    set.tagBackend = [tagDeps](QObject* parent) -> tanara::library::TagBackend* {
        return new TagControllerBackend(tagDeps, parent);
    };

    PeopleServicesFacade::Deps pd;
    pd.service = &sv->people();
    pd.stats = c->peopleStats();
    pd.voiceprints = c->voiceprints();
    pd.voice = &sv->voiceAnalysis();
    pd.events = events;
    auto* people = new PeopleServicesFacade(pd, c);
    set.people = [people](QObject*) -> tanara::library::IPeopleFacade* { return people; };

    ParticipantsServicesFacade::Deps qd;
    qd.store = c->store();
    qd.people = c->people();
    qd.peopleService = &sv->people();
    qd.voiceprints = c->voiceprints();
    qd.tags = c->tags();
    qd.settings = c->settings();
    qd.participants = &sv->participants();
    qd.voice = &sv->voiceAnalysis();
    qd.events = events;
    auto* participants = new ParticipantsServicesFacade(qd, c);
    set.participants = [participants](QObject*) -> tanara::editing::IParticipantsFacade* { return participants; };

    connect(c, &QObject::destroyed, this, [this, c] { m_realServices.erase(c); });
    return m_realServices.emplace(c, std::move(set)).first->second;
}

const UiServices& AppContext::servicesForObject(QObject* controller)
{
    return servicesFor(qobject_cast<tanara::app::AppController*>(controller));
}

void AppContext::useControllerIfNone(tanara::app::AppController* controller)
{
    if (controller && !m_controller)
        setController(controller);
}

QObject* AppContext::createRecorderDemo(const QString& designState)
{
    auto* facade = new tanara_demo::RecorderDemoFacade(designState);
    auto* vm = new RecorderViewModel(facade);
    facade->setParent(vm);
    return vm;
}

void AppContext::setBridge(QObject* bridge)
{
    if (bridge == m_bridge.data())
        return;
    m_bridge = bridge;
    emit bridgeChanged();
}

} // namespace tanara_qml
