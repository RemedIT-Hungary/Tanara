#include "SummaryViewModel.h"

#include "DemoText.h"
#include "tanara/Logging.h"

#include "AppContext.h"
#include "JobSupport.h"
#include "SummaryProgress.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/cloud/CloudAccount.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/jobs/MeetingJobTracker.h"
#include "tanara/provider/ProviderRegistry.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/SummaryService.h"
#include "tanara/summary/SummaryStore.h"

#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace tanara_qml {

using namespace tanara;

namespace {

QVariantMap decisionMap(const QString& raw, qint64 durationMs)
{
    QString rest;
    qint64 ms = SummaryViewModel::splitTimestamp(raw, &rest);
    if (ms >= 0 && durationMs > 0 && ms > durationMs) {   // does not point into the recording -> not a link
        ms = -1;
        rest = raw;
    }
    return {{QStringLiteral("text"), rest}, {QStringLiteral("ms"), ms},
            {QStringLiteral("stamp"), ms >= 0 ? jobsupport::formatDuration(ms) : QString()}};
}

// A memo section for the view. A timestamp beyond the recording's length is not a link.
QVariantMap memoMap(const MemoSection& sec, qint64 durationMs)
{
    qint64 start = sec.startMs, end = sec.endMs;
    if (start >= 0 && durationMs > 0 && start > durationMs)
        start = end = -1;
    if (end >= 0 && (start < 0 || end < start))
        end = -1;
    const QString stamp = start >= 0 ? jobsupport::formatDuration(start) : QString();
    const QString range = start < 0 ? QString()
                        : end > start ? stamp + QStringLiteral("–") + jobsupport::formatDuration(end)
                                      : stamp;
    QStringList points;
    for (const QString& p : sec.points)
        if (!p.trimmed().isEmpty()) points << p.trimmed();
    return {{QStringLiteral("title"), sec.title.trimmed()}, {QStringLiteral("startMs"), start},
            {QStringLiteral("endMs"), end}, {QStringLiteral("stamp"), stamp},
            {QStringLiteral("range"), range}, {QStringLiteral("points"), points}};
}

QVariantMap participantMap(const QString& name, int colorIndex, int percent)
{
    return {{QStringLiteral("name"), name}, {QStringLiteral("colorIndex"), colorIndex},
            {QStringLiteral("percent"), percent}};
}

} // namespace

qint64 SummaryViewModel::splitTimestamp(const QString& text, QString* rest)
{
    // "[12:52] ...", "(1:02:10) ...", "12:52 - ..." at the start of the line.
    static const QRegularExpression re(QStringLiteral(
        R"(^\s*[\[(]?(\d{1,2}):(\d{2})(?::(\d{2}))?[\])]?\s*[-–—:]?\s+(\S.*)$)"),
        QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) {
        if (rest) *rest = text;
        return -1;
    }
    qint64 a = m.captured(1).toLongLong(), b = m.captured(2).toLongLong();
    qint64 secs;
    if (!m.captured(3).isEmpty())
        secs = a * 3600 + b * 60 + m.captured(3).toLongLong();
    else
        secs = a * 60 + b;
    if (rest) *rest = m.captured(4).trimmed();
    return secs * 1000;
}

SummaryViewModel::SummaryViewModel(QObject* parent)
    : QObject(parent), m_topics(new TopicListModel(this)), m_note(new MeetingNoteModel(this)),
      m_sentences(new StatementListModel(this)), m_decisionItems(new StatementListModel(this)),
      m_todoItems(new StatementListModel(this)), m_questionItems(new StatementListModel(this)),
      m_memoItems(new StatementListModel(this))
{
    connect(this, &SummaryViewModel::changed, this, &SummaryViewModel::generationLineChanged);
    connect(this, &SummaryViewModel::sectionChanged, this, &SummaryViewModel::generationLineChanged);
    connect(this, &SummaryViewModel::sectionChanged, this, &SummaryViewModel::marksChanged);
    connect(m_topics, &TopicListModel::countsChanged, this, &SummaryViewModel::changed);
    connect(m_note, &MeetingNoteModel::noteChanged, this, &SummaryViewModel::noteHintChanged);

    AppContext* ctx = AppContext::instance();
    connect(ctx, &AppContext::controllerChanged, this, [this]() { connectController(); reload(); });
    connect(ctx, &AppContext::demoChanged, this, &SummaryViewModel::reload);
    connectController();
    reload();
}

AppController* SummaryViewModel::app() const { return jobsupport::resolveController(m_injected); }
QObject* SummaryViewModel::controllerObject() const { return app(); }
bool SummaryViewModel::demo() const { return jobsupport::demoMode(app()); }

void SummaryViewModel::setController(QObject* controller)
{
    if (m_injected == controller)
        return;
    m_injected = controller;
    m_note->setController(controller);
    connectController();
    emit controllerChanged();
    reload();
}

void SummaryViewModel::setMeetingId(const QString& id)
{
    if (id == m_meetingId)
        return;
    const bool switching = !m_meetingId.isEmpty();
    // The not-yet-saved note belongs to the OLD meeting: it goes there before the switch.
    m_note->setMeetingId(id);
    m_meetingId = id;
    emit meetingIdChanged();
    // On switching to another meeting the workspace closes; if the new meeting has a topic list
    // but no summary yet, it opens (an interrupted per-topic analysis can resume from where it
    // stands on disk). On the first setting, an externally requested topicsOpen stays.
    AppController* c = app();
    if (switching) {
        m_topicsOpen = false;
        m_activeStatementId.clear();
        m_activeSection = -1;
        setNoteOpen(false);
        if (m_section != QLatin1String("exec")) {   // for a new meeting the short form always shows
            m_section = QStringLiteral("exec");
            emit sectionChanged();
        }
    }
    if (!jobsupport::demoMode(c) && !id.isEmpty()
        && !c->summaryDocument(id).exists && !c->meetingTopics(id).isEmpty())
        m_topicsOpen = true;
    reload();
}

void SummaryViewModel::setDemoState(const QString& state)
{
    if (state == m_demoState)
        return;
    m_demoState = state;
    emit demoStateChanged();
    // The memo demo states show the memo (for screenshots); the others the short form.
    const bool memo = state == QLatin1String("memo") || state == QLatin1String("memoShort")
                   || state == QLatin1String("oldMemo");
    const bool memoV3 = state == QLatin1String("memoSections");
    setSection(memo || memoV3 ? QStringLiteral("memo") : QStringLiteral("exec"));
    setNoteOpen(state == QLatin1String("noteOpen"));
    // The selected statement / section and the Sources switch, for screenshots.
    m_sourcesVisible = state != QLatin1String("sourcesOff");
    m_activeStatementId = state == QLatin1String("sourcesOn") || state == QLatin1String("sourcePopover")
        ? QStringLiteral("s1") : QString();
    m_activeSection = memoV3 ? 2 : -1;
    emit sourcesVisibleChanged();
    reload();
}

void SummaryViewModel::setNoteOpen(bool open)
{
    if (open == m_noteOpen)
        return;
    m_noteOpen = open;
    emit noteOpenChanged();
}

bool SummaryViewModel::noteChangedSinceSummary() const
{
    return m_hasSummary && m_summaryNoteKnown
        && m_summaryNote.simplified() != m_note->note().simplified();
}

void SummaryViewModel::setTopicsOpen(bool open)
{
    if (open == m_topicsOpen)
        return;
    m_topicsOpen = open;
    emit changed();
}

void SummaryViewModel::setSection(const QString& section)
{
    const QString s = section == QLatin1String("memo") ? section : QStringLiteral("exec");
    if (s == m_section)
        return;
    m_section = s;
    emit sectionChanged();
}

QString SummaryViewModel::memoState() const
{
    if (!m_hasSummary || m_mode == QLatin1String("topics"))
        return QStringLiteral("none");
    return m_memo.isEmpty() ? QStringLiteral("missing") : QStringLiteral("ready");
}

QString SummaryViewModel::view() const
{
    if (!m_valid)
        return QStringLiteral("none");
    if (m_topicsOpen)
        return QStringLiteral("topics");
    return m_hasSummary ? QStringLiteral("summary") : QStringLiteral("empty");
}

QString SummaryViewModel::modeLabel() const
{
    if (m_mode == QLatin1String("quick"))  return tr("Gyors összefoglaló");
    if (m_mode == QLatin1String("topics")) return tr("Témánkénti elemzés");
    return tr("Összefoglaló");
}

void SummaryViewModel::connectController()
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
    m_connections << connect(c->jobs(), &MeetingJobTracker::stateChanged, this,
                             [this, mine](const QString& id) { if (mine(id)) reload(); });
    m_connections << connect(c->jobs(), &MeetingJobTracker::jobProgressChanged, this,
                             [this, mine](const QString& id, const JobProgress&) {
        if (mine(id)) reloadJobs();
    });
    m_connections << connect(c, &AppController::summaryReady, this,
                             [this, mine](const QString& id, const QString&) {
        if (!mine(id)) return;
        m_topicsOpen = false;          // finished -> we show the summary, the short form
        if (m_section != QLatin1String("exec")) {
            m_section = QStringLiteral("exec");
            emit sectionChanged();
        }
        reload();
        emit summaryArrived();
    });
    m_connections << connect(c, &AppController::topicsReady, this,
                             [this, mine](const QString& id, const QVector<SummaryTopic>&) {
        if (!mine(id)) return;
        m_topicsOpen = true;           // the suggested topics arrived -> editor
        reload();
    });
    m_connections << connect(c, &AppController::summaryStaleChanged, this,
                             [this, mine](const QString& id) { if (mine(id)) reload(); });
    m_connections << connect(c, &AppController::summaryStatementsChanged, this,
                             [this, mine](const QString& id) { if (mine(id)) reload(); });
    m_connections << connect(c, &AppController::transcriptReady, this,
                             [this, mine](const QString& id, const QString&) { if (mine(id)) reload(); });
    m_connections << connect(c, &AppController::speakerMapChanged, this,
                             [this, mine](const QString& id) { if (mine(id)) reloadParticipants(); });
    m_connections << connect(c->settings(), &SettingsManager::settingsChanged, this,
                             &SummaryViewModel::reload);
    // The note (and the detected call) live in meeting.json: after saving / editing from another view.
    m_connections << connect(c->store(), &MeetingStore::meetingUpdated, this,
                             [this, mine](const QString& id) { if (mine(id)) m_note->reload(); });
    if (CloudAccount* acc = c->cloud()) {
        m_connections << connect(acc, &CloudAccount::loggedIn, this, &SummaryViewModel::reload);
        m_connections << connect(acc, &CloudAccount::loggedOut, this, &SummaryViewModel::reload);
        m_connections << connect(acc, &CloudAccount::accountUpdated, this,
                                 [this](const AccountInfo&) { reload(); });
    }
}

void SummaryViewModel::refresh() { reload(); }

void SummaryViewModel::watchEditor(SpeakerEditor* editor)
{
    if (editor == m_editor)
        return;
    if (m_editorConn)
        disconnect(m_editorConn);
    m_editor = editor;
    if (editor)
        m_editorConn = connect(editor, &SpeakerEditor::speakersChanged, this,
                               &SummaryViewModel::reloadParticipants);
}

int SummaryViewModel::speakerIndexFor(const QString& name) const
{
    const QString n = name.trimmed();
    if (n.isEmpty())
        return -1;
    for (const SpeakerRef& s : m_speakers)
        if (s.name.compare(n, Qt::CaseInsensitive) == 0 || s.personName.compare(n, Qt::CaseInsensitive) == 0)
            return s.colorIndex;
    // The model often writes only the first / last name: an unambiguous word match is fine too.
    int found = -1, hits = 0;
    for (const SpeakerRef& s : m_speakers) {
        const QStringList words = s.name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString& w : words) {
            if (w.compare(n, Qt::CaseInsensitive) == 0) { found = s.colorIndex; ++hits; break; }
        }
    }
    return hits == 1 ? found : -1;
}

// "A, B and C" -> separate chips, each in its own speaker color (if the meeting's speaker).
QVariantList SummaryViewModel::ownerList(const QString& owner) const
{
    static const QRegularExpression sep(QStringLiteral("\\s*(?:,|;|/|&|\\bés\\b|\\band\\b)\\s*"),
                                        QRegularExpression::UseUnicodePropertiesOption);
    QVariantList out;
    // If the whole text is one speaker's name, we do not split it.
    const int whole = speakerIndexFor(owner);
    const QStringList parts = whole >= 0 ? QStringList{owner.trimmed()}
                                         : owner.split(sep, Qt::SkipEmptyParts);
    for (const QString& p : parts) {
        const QString name = p.trimmed();
        if (name.isEmpty()) continue;
        out << QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("index"), speakerIndexFor(name)}};
    }
    return out;
}

void SummaryViewModel::reloadParticipants()
{
    AppController* c = app();
    if (jobsupport::demoMode(c) || m_meetingId.isEmpty())
        return;

    m_speakers.clear();
    SpeakerEditor* editor = c->speakerEditor(m_meetingId);
    watchEditor(editor);
    int utterances = 0;
    if (editor) {
        for (const EditorSpeaker& s : editor->speakers())
            m_speakers.append({s.displayName, s.personName, s.colorIndex, s.talkShare});
        utterances = editor->utteranceCount();
    }
    m_sourceUtterances = utterances;
    m_transcriptLine = m_speakers.isEmpty()
        ? tr("Az átirat kész.")
        : tr("Az átirat kész, %1 beszélő, %2 megszólalás.").arg(m_speakers.size()).arg(utterances);

    // Participants: the meeting's speakers by speaking time; names mentioned in the summary but
    // not among the speakers go last, without a share.
    QVector<SpeakerRef> sorted = m_speakers;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const SpeakerRef& a, const SpeakerRef& b) { return a.share > b.share; });
    QVariantList list;
    for (const SpeakerRef& s : sorted)
        list << participantMap(s.name, s.colorIndex, int(s.share * 100.0 + 0.5));
    for (const QString& p : std::as_const(m_summaryParticipants))
        if (speakerIndexFor(p) < 0 && !p.trimmed().isEmpty())
            list << participantMap(p.trimmed(), -1, -1);
    m_participants = list;

    // The owner chips' color comes from the speakers -> the action items get recolored.
    QVariantList actions;
    for (const QVariant& v : std::as_const(m_actions)) {
        QVariantMap a = v.toMap();
        const QString owner = a.value(QStringLiteral("owner")).toString();
        a.insert(QStringLiteral("ownerIndex"), speakerIndexFor(owner));
        a.insert(QStringLiteral("owners"), ownerList(owner));
        actions << a;
    }
    m_actions = actions;

    // The memo sections' speakers and the statements' owners get the speaker colors too.
    buildStatements(m_rawStatements, m_affectedUtterances);   // + the memo sections (rebuildMemo)
    emit participantsChanged();
    emit changed();
    emit marksChanged();
}

void SummaryViewModel::reloadJobs()
{
    AppController* c = app();
    if (jobsupport::demoMode(c) || m_meetingId.isEmpty())
        return;
    int kind = -1;
    JobProgress shown;
    for (JobKind k : {JobKind::Summarize, JobKind::ExtractTopics}) {
        const JobProgress jp = c->jobs()->job(m_meetingId, k);
        if (jp.isValid()) { kind = int(k); shown = jp; break; }
    }
    const JobProgress analyze = c->jobs()->job(m_meetingId, JobKind::AnalyzeTopics);
    m_analyzing = analyze.isValid();
    m_jobKind = kind;
    m_jobTitle = shown.title;
    m_jobMessage = shown.message;
    m_jobCancelling = kind >= 0 ? shown.cancelling : (m_analyzing && analyze.cancelling);
    applyJob(shown);
    emit jobChanged();
}

// The summary's phases (the task bar uses the same mapping).
void SummaryViewModel::applyJob(const JobProgress& job)
{
    const SummaryProgress p = SummaryProgress::from(job);
    m_jobStage = p.stage;
    m_jobStageLabel = p.isValid() ? p.label : m_jobTitle;
    m_jobPercent = p.percent;
    m_jobStages = p.stages;
    m_jobReusedParts = p.reused;
    m_jobReusedNote = p.reusedNote;
}

void SummaryViewModel::reload()
{
    tanara::PerfScope perfScope("SummaryViewModel::reload", 20);
    m_blocker.clear();
    m_canRun = false;
    m_cloudSelected = false;
    m_cloudTierLabel.clear();
    m_cloudTeaser = false;
    m_errorMessage.clear();
    m_errorDetail.clear();
    m_fixActionLabel.clear();
    m_fixActionPage.clear();
    m_fixReloadContext = 0;
    m_errorKeptParts = false;
    m_hasSummary = false;
    m_stale = false;
    m_staleCount = 0;
    m_mode = QStringLiteral("unknown");
    m_metaLine.clear();
    m_modelLine.clear();
    m_execSummary.clear();
    m_markdown.clear();
    m_decisions.clear();
    m_openQuestions.clear();
    m_actions.clear();
    m_topicSections.clear();
    m_memo.clear();
    m_summary = Summary();
    m_summaryParticipants.clear();
    m_metaDate.clear();
    m_metaProvider.clear();
    m_rawStatements.clear();
    m_affectedUtterances.clear();
    m_hasStatements = false;
    m_staleTargeted = false;
    m_affectedStatements = m_affectedTodos = m_ownerChanges = 0;
    m_sourceUtterances = 0;
    m_demoLines.clear();
    m_summaryNote.clear();
    m_summaryNoteKnown = false;
    m_jobKind = -1;
    m_jobTitle.clear();
    m_jobMessage.clear();
    m_jobCancelling = false;
    m_analyzing = false;
    applyJob(JobProgress());
    m_transcriptLine.clear();

    AppController* c = app();
    if (jobsupport::demoMode(c)) {
        watchEditor(nullptr);
        m_valid = true;
        loadDemo();
        if (m_rawStatements.isEmpty())
            buildStatements({}, {});
        emit marksChanged();
        emit participantsChanged();
        emit jobChanged();
        emit changed();
        emit noteHintChanged();
        return;
    }
    if (m_meetingId.isEmpty()) {
        watchEditor(nullptr);
        m_valid = false;
        m_participants.clear();
        buildStatements({}, {});
        emit marksChanged();
        m_topics->clear();
        m_note->reload();
        emit noteHintChanged();
        emit participantsChanged();
        emit jobChanged();
        emit changed();
        return;
    }

    const Meeting m = c->store()->load(m_meetingId);
    m_valid = !m.id.isEmpty();
    m_durationMs = m.durationMs;
    m_topics->bind(c, m_meetingId);
    m_note->reload();

    // Gating + provider.
    const ReadinessResult r = c->canRun(WorkflowStep::Summarize, m_meetingId);
    m_canRun = r.runnable;
    m_blocker = jobsupport::blockerInfo(c, WorkflowStep::Summarize, r);
    m_providerLabel = jobsupport::providerLabel(c, WorkflowStep::Summarize);
    m_cloudTeaser = !r.runnable && r.blockerKind == BlockerKind::ProviderConfig && c->cloudTeaser();
    if (c->usesCloud(WorkflowStep::Summarize) && c->cloud() && c->cloud()->isLoggedIn()) {
        const AppSettings s = c->settings()->settings();
        m_cloudSelected = true;
        m_cloudTierLabel = !s.cloudLlmModel.isEmpty() ? tr("Expert-modell")
                         : s.cloudLlmTier == QLatin1String("fast") ? tr("Gyors szint") : tr("Pontos szint");
    }

    // Leftover error (the last summary attempt).
    const MeetingProcessingState ps = c->jobs()->state(m);
    if (ps.summaryError.isValid()) {
        m_errorMessage = ps.summaryError.message;
        m_errorDetail = ps.summaryError.detail;
        const jobsupport::FixAction fix = jobsupport::fixActionForError(ps.summaryError);
        m_fixActionLabel = fix.label;
        m_fixActionPage = fix.page;
        m_fixReloadContext = fix.reloadContext;
        // Notes of the finished parts in the cache: a retry runs only the missing ones.
        m_errorKeptParts = !m.folder.isEmpty()
            && QFileInfo::exists(QDir(m.folder).filePath(SummaryService::cacheFileName()));
    }

    // The summary, structured (new: summary.json; old: recovered from summary.md).
    const SummaryDocument doc = c->summaryDocument(m_meetingId);
    m_hasSummary = doc.exists;
    if (doc.exists) {
        m_markdown = doc.markdown;
        m_mode = summarystore::modeToString(doc.meta.mode);
        m_summary = doc.summary;
        m_execSummary = doc.summary.execSummary.trimmed();
        m_summaryParticipants = doc.summary.participants;
        for (const QString& d : doc.summary.decisions)
            m_decisions << decisionMap(d, m.durationMs);
        for (const QString& q : doc.summary.openQuestions)
            m_openQuestions << decisionMap(q, m.durationMs);
        for (const ActionItem& a : doc.summary.actionItems)
            m_actions << QVariantMap{{QStringLiteral("text"), a.text}, {QStringLiteral("owner"), a.owner},
                                     {QStringLiteral("ownerIndex"), -1}, {QStringLiteral("owners"), QVariantList{}},
                                     {QStringLiteral("due"), a.due}};
        for (const TopicAnalysis& t : doc.topics) {
            QVariantList acts;
            for (const ActionItem& a : t.actionItems)
                acts << QVariantMap{{QStringLiteral("text"), a.text}, {QStringLiteral("owner"), a.owner},
                                    {QStringLiteral("due"), a.due}};
            m_topicSections << QVariantMap{{QStringLiteral("title"), t.title},
                                           {QStringLiteral("detail"), t.detail.trimmed()},
                                           {QStringLiteral("decisions"), t.decisions},
                                           {QStringLiteral("openQuestions"), t.openQuestions},
                                           {QStringLiteral("actions"), acts}};
        }
        QStringList meta;
        if (doc.meta.createdAt.isValid())
            meta << (m_metaDate = QLocale().toString(doc.meta.createdAt, tr("MMM d. HH:mm")));
        if (doc.meta.providerId == cloud::ProviderId) {
            meta << (m_metaProvider = QStringLiteral("Tanara Cloud"));
        } else if (!doc.meta.providerId.isEmpty()) {
            const QString name = LlmProviderRegistry::instance().descriptor(doc.meta.providerId).displayName;
            meta << (m_metaProvider = name.isEmpty() ? tr("saját kulcs") : tr("%1 · saját kulcs").arg(name));
        }
        m_metaLine = meta.join(QStringLiteral(" · "));
        m_modelLine = doc.meta.model;
        m_summaryNote = doc.meta.contextNote;
        m_summaryNoteKnown = doc.meta.contextNoteKnown;

        if (doc.summary.statements.isEmpty()) {
            const SummaryStaleInfo st = c->summaryStale(m);
            m_stale = st.stale;
            m_staleCount = st.correctedSpeakers;
        } else {
            // Quick summary with sources: the targeted staleness and the statements per the current
            // speaker state (staleBecause): two small file reads.
            const SummaryStaleInfo st = c->summaryStale(m_meetingId);
            m_stale = st.stale;
            m_staleCount = st.correctedSpeakers;
            m_staleTargeted = st.targeted;
            m_affectedStatements = st.affectedStatements;
            m_affectedTodos = st.affectedTodos;
            m_ownerChanges = st.ownerChanges;
            m_affectedUtterances = st.affectedUtteranceIds;
            m_rawStatements = c->summaryStatements(m_meetingId);
        }
    }

    reloadParticipants();   // speakers -> participants, owner colors, transcript row (+ changed)
    reloadJobs();
    emit noteHintChanged();
}

QString SummaryViewModel::markdownFor(const QString& part) const
{
    if (!m_hasSummary)
        return QString();
    // Per-part copying makes sense only for a structured quick summary; for a per-topic or
    // memo-less summary the full text always goes.
    if (part == QLatin1String("exec") && memoState() == QLatin1String("ready")) {
        Summary s = m_summary;
        s.memo.clear();
        return s.renderMarkdown();
    }
    if (part == QLatin1String("memo")) {
        if (memoState() != QLatin1String("ready"))
            return QString();
        Summary s;
        s.memo = m_summary.memo;
        return s.renderMarkdown();
    }
    return m_markdown;
}

bool SummaryViewModel::copyToClipboard(const QString& part)
{
    const QString md = markdownFor(part);
    if (md.isEmpty() || !qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
        return false;
    QGuiApplication::clipboard()->setText(md);
    return true;
}

void SummaryViewModel::dismissStale()
{
    AppController* c = app();
    if (jobsupport::demoMode(c)) {
        m_stale = false;
        emit changed();
        return;
    }
    if (!m_meetingId.isEmpty())
        c->dismissSummaryStale(m_meetingId);   // → summaryStaleChanged → reload()
}

void SummaryViewModel::clearError()
{
    AppController* c = app();
    if (jobsupport::demoMode(c)) {
        m_errorMessage.clear();
        m_errorDetail.clear();
        m_errorKeptParts = false;
        emit changed();
        return;
    }
    if (!m_meetingId.isEmpty())
        c->jobs()->clearError(m_meetingId, JobKind::Summarize);
}

// Fictional sample data (matching the design's three summary screens).
void SummaryViewModel::loadDemo()
{
    const QString st = m_demoState.isEmpty() ? QStringLiteral("stale") : m_demoState;
    m_providerLabel = tr("%1 · saját kulcs").arg(QStringLiteral("LM Studio"));
    m_canRun = true;
    m_transcriptLine = tr("Az átirat kész, %1 beszélő, %2 megszólalás.").arg(4).arg(412);
    m_durationMs = 76 * 60 * 1000 + 4000;
    m_participants.clear();

    const bool empty = st.startsWith(QLatin1String("empty"));
    static const QStringList v3{QStringLiteral("sourcesOn"), QStringLiteral("sourcesOff"),
                                QStringLiteral("sourcePopover"), QStringLiteral("memoSections"),
                                QStringLiteral("staleTargeted"), QStringLiteral("noSources")};
    if (v3.contains(st)) {
        if (m_topics->count() > 0 || m_topicsOpen) {
            m_topics->clear();
            m_topicsOpen = false;
        }
        m_note->setDemoContent(QString(), QString(), {});
        loadDemoSources(st);
        return;
    }

    // The note: fictional, with spelling fixes; the "emptyNote" / "noteOpen" state with
    // suggestions from earlier, similarly titled meetings.
    const QString demoNote = tr("Negyedéves partner-egyeztetés: támogatási igény, súgóoldalak, számlázás. "
                                "A „Szúgó Pont” helyesen: SúgóPont.");
    if (st == QLatin1String("emptyNote"))
        m_note->setDemoContent(QString(), QStringLiteral("Microsoft Teams"),
                               MeetingNoteModel::demoSuggestions());
    else if (st == QLatin1String("noteOpen"))
        m_note->setDemoContent(demoNote, QString(), MeetingNoteModel::demoSuggestions());
    else
        m_note->setDemoContent(demoNote, QString(), {});
    m_summaryNoteKnown = true;
    m_summaryNote = st == QLatin1String("noteChanged")
        ? tr("Negyedéves partner-egyeztetés: támogatási igény, súgóoldalak, számlázás.")
        : demoNote;
    if (st == QLatin1String("topics")) {
        m_topics->loadDemo();
        m_topicsOpen = true;
    } else if (m_topics->count() > 0 || m_topicsOpen) {
        m_topics->clear();
        m_topicsOpen = false;
    }

    // A running summary with the core's phases (the mapping is the same as live).
    auto demoJob = [this](const QString& kind) {
        JobProgress job;
        job.meetingId = QStringLiteral("demo");
        job.kind = JobKind::Summarize;
        job.title = tr("Összefoglaló készítése");
        if (kind == QLatin1String("single")) {
            job.stages = {{QStringLiteral("single"), tr("Összefoglalás"), StageState::Running, -1, QString()}};
        } else {
            const bool merging = kind == QLatin1String("merge");
            job.done = merging ? 6 : 3;
            job.total = 6;
            job.stages = {
                {QStringLiteral("notes"), tr("Jegyzetelés részenként"),
                 merging ? StageState::Done : StageState::Running, -1,
                 merging ? tr("%n rész kész", nullptr, 6)
                         : tr("%1/%2. rész").arg(4).arg(6) + tr(" · %n korábbi futásból", nullptr, 2)},
                {QStringLiteral("merge"), tr("Összegzés"),
                 merging ? StageState::Running : StageState::Waiting, -1, QString()},
            };
        }
        m_jobKind = int(JobKind::Summarize);
        m_jobTitle = job.title;
        applyJob(job);
    };

    if (empty) {
        if (st == QLatin1String("emptyBlocked")) {
            m_canRun = false;
            m_blocker = {
                {QStringLiteral("title"), tr("Nincs beállítva összefoglaló szolgáltató")},
                {QStringLiteral("text"), tr("Hiányzik: Cím (URL) (OpenAI-kompatibilis). Add meg a saját "
                                            "szolgáltatód adatait a Beállításokban.")},
                {QStringLiteral("actionLabel"), tr("Szolgáltató beállítása")},
                {QStringLiteral("actionPage"), QStringLiteral("providers")},
                {QStringLiteral("reason"), tr("A szolgáltató beállítása után indítható.")},
            };
        } else if (st == QLatin1String("emptyRunning")) {
            demoJob(QStringLiteral("single"));
        } else if (st == QLatin1String("emptyRunningParts")) {
            demoJob(QStringLiteral("notes"));
        } else if (st == QLatin1String("emptyRunningMerge")) {
            demoJob(QStringLiteral("merge"));
        } else if (st == QLatin1String("emptyError")) {
            m_errorMessage = tr("A szolgáltató nem válaszolt időben. Próbáld újra.");
            m_errorDetail = QStringLiteral("HTTP 504 · gateway_timeout");
        } else if (st == QLatin1String("emptyErrorKept")) {
            m_errorMessage = tr("A szolgáltatónál hiba történt. Próbáld újra később.");
            m_errorDetail = QStringLiteral("HTTP 500 · server_error · model crashed");
            m_errorKeptParts = true;
        } else if (st == QLatin1String("emptyErrorContext")) {
            // The classified context error (LM Studio): fix buttons per the hint.
            JobError e;
            e.message = tr("A modell 4096 tokenes kontextussal van betöltve, a kérés 6042 token volt — nem fér bele. "
                           "A Tanara újra tudja tölteni az LM Studióban legalább 16 384 tokenes kontextussal, és újraindítja a feladatot.");
            e.detail = QStringLiteral("HTTP 400 · exceed_context_size_error · ") + tr("kérés 6042 token · kontextus 4096 token");
            e.fixActionHint = QStringLiteral("llm:reload-context:16384");
            const jobsupport::FixAction fix = jobsupport::fixActionForError(e);
            m_errorMessage = e.message;
            m_errorDetail = e.detail;
            m_fixActionLabel = fix.label;
            m_fixActionPage = fix.page;
            m_fixReloadContext = fix.reloadContext;
        }
        return;
    }

    // --- finished summary ---
    m_hasSummary = true;
    m_speakers = {
        {demoName("Kovács Lilla"), demoName("Kovács Lilla"), 0, 0.34}, {demoName("Tóth Bence"), demoName("Tóth Bence"), 1, 0.22},
        {demoName("Varga Nóra"), demoName("Varga Nóra"), 2, 0.17},     {demoName("Molnár Eszter"), demoName("Molnár Eszter"), 3, 0.14},
        {demoName("Szabó Áron"), demoName("Szabó Áron"), 4, 0.08},     {demoRaw(QStringLiteral("Távoli 2")), QString(), 5, 0.05},
    };
    for (const SpeakerRef& s : std::as_const(m_speakers))
        m_participants << participantMap(s.name, s.colorIndex, int(s.share * 100.0 + 0.5));

    // Old (pre-memo) summary: no memo, no open questions, no model data either.
    const bool old = st == QLatin1String("oldSummary") || st == QLatin1String("oldMemo");
    m_summary.execSummary = tr("A partnerek megerősítették, hogy a harmadik negyedévben a támogatási igény "
                               "harmadával csökkent, főként az új súgóoldalak miatt. A súgót a többi termékre "
                               "is kiterjesztik, de a számlázásnál előbb a folyamatot egyszerűsítik. A "
                               "dokumentációs létszámbővítésről a költségvetés dönt.");
    m_summary.decisions = QStringList{
        tr("[12:52] A súgóoldalakat a többi termékre is kiterjesztik."),
        tr("[31:10] A számlázásnál előbb a folyamatot egyszerűsítik, utána a leírást."),
        tr("[58:40] A dokumentációs létszámról a költségvetési tervezés dönt."),
    };
    if (!old)
        m_summary.openQuestions = QStringList{
            tr("[43:30] Legyen-e nyomtatható változata a súgóoldalaknak?"),
            tr("[58:40] Bevonjanak-e átmenetileg külső szövegírót a dokumentációhoz?"),
            tr("Mikorra frissül a partnerportál?"),
        };
    m_summary.actionItems = {
        {tr("A súgóoldalak kiterjesztése a további termékekre: ütemterv"), demoName("Kovács Lilla"), tr("okt. 15.")},
        {tr("Számlázási elakadási pontok összegyűjtése"), demoName("Varga Nóra"), tr("okt. 10.")},
        {tr("Dokumentációs létszámigény a költségvetési tervbe"), demoName("Szabó Áron"), tr("okt. 20.")},
        {tr("Új-ügyfél bontás elküldése a partnereknek"), demoName("Kovács Lilla"), tr("okt. 4.")},
    };
    for (const SpeakerRef& s : std::as_const(m_speakers))
        m_summary.participants << s.name;

    for (const QString& d : std::as_const(m_summary.decisions))
        m_decisions << decisionMap(d, m_durationMs);
    for (const QString& q : std::as_const(m_summary.openQuestions))
        m_openQuestions << decisionMap(q, m_durationMs);
    for (const ActionItem& a : std::as_const(m_summary.actionItems))
        m_actions << QVariantMap{{QStringLiteral("text"), a.text}, {QStringLiteral("owner"), a.owner},
                                 {QStringLiteral("ownerIndex"), speakerIndexFor(a.owner)},
                                 {QStringLiteral("owners"), ownerList(a.owner)},
                                 {QStringLiteral("due"), a.due}};
    m_execSummary = m_summary.execSummary;
    m_mode = old ? QStringLiteral("unknown") : QStringLiteral("quick");
    m_metaLine = demoText("okt. 1. 17:05", "Oct 1, 5:05 PM") + QStringLiteral(" · ") + m_providerLabel;
    m_modelLine = old ? QString() : QStringLiteral("gemma-4-12b");
    m_stale = st == QLatin1String("stale");
    m_staleCount = m_stale ? 3 : 0;

    if (!old && st != QLatin1String("topicsDoc"))
        loadDemoMemo(st != QLatin1String("memoShort"));
    rebuildMemo();
    m_markdown = m_summary.renderMarkdown();

    if (st == QLatin1String("running"))
        demoJob(QStringLiteral("notes"));

    if (st == QLatin1String("topicsDoc")) {
        m_mode = QStringLiteral("topics");
        m_topicSections = {
            QVariantMap{{QStringLiteral("title"), tr("Támogatási igény alakulása")},
                        {QStringLiteral("detail"), tr("A jegyek száma harmadával csökkent, miközben az aktív "
                                                      "felhasználók 12%-kal nőttek. A csökkenés fő oka a "
                                                      "termékbe épített súgó.")},
                        {QStringLiteral("decisions"), QStringList{tr("A súgóoldalakat a többi termékre is kiterjesztik.")}},
                        {QStringLiteral("openQuestions"), QStringList{tr("Legyen-e nyomtatható változata a súgóoldalaknak?")}},
                        {QStringLiteral("actions"), QVariantList{QVariantMap{
                             {QStringLiteral("text"), tr("Ütemterv a kiterjesztéshez")},
                             {QStringLiteral("owner"), demoName("Kovács Lilla")}, {QStringLiteral("due"), tr("okt. 15.")}}}}},
            QVariantMap{{QStringLiteral("title"), tr("Számlázási folyamat")},
                        {QStringLiteral("detail"), tr("A partnerek szerint a gond nem a leírás, hanem maga a "
                                                      "folyamat: túl sok lépés, kevés visszajelzés.")},
                        {QStringLiteral("decisions"), QStringList{}},
                        {QStringLiteral("openQuestions"), QStringList{}},
                        {QStringLiteral("actions"), QVariantList{}}},
        };
    }
}

// The demo's memo: 24 sections of a 76-minute meeting, or (short variant) the first four.
void SummaryViewModel::loadDemoMemo(bool longForm)
{
    struct Row { qint64 from; qint64 to; QString title; QStringList points; };
    auto t = [](int m, int s) { return qint64(m * 60 + s) * 1000; };
    const QVector<Row> rows{
        {t(0, 0), t(2, 40), tr("Nyitás, napirend"),
         {demoText("Kovács Lilla összefoglalta a negyedév fő számait és a mai napirendet.", "Emma Carter summed up the main figures of the quarter and today’s agenda."),
          tr("A partnerek kérték, hogy a számlázás külön napirendi pont legyen.")}},
        {t(2, 40), t(7, 15), tr("Támogatási jegyek alakulása"),
         {tr("A jegyek száma harmadával csökkent az előző negyedévhez képest."),
          tr("Az aktív felhasználók száma közben 12%-kal nőtt."),
          tr("A csökkenés nagyobb része a beállítási kérdéseknél jelentkezett.")}},
        {t(7, 15), t(12, 52), tr("A súgóoldalak hatása"),
         {demoText("Tóth Bence szerint a termékbe épített súgó a beállítási kérdések felét kiváltotta.", "According to Ryan Mitchell, the in-product help replaced half of the setup questions."),
          tr("A partnerek ugyanezt tapasztalják a saját ügyfélszolgálatukon."),
          tr("A súgót a többi termékre is kiterjesztik.")}},
        {t(12, 52), t(17, 30), tr("A kiterjesztés sorrendje"),
         {tr("Elsőként a riportmodul kap súgóoldalakat, utána az integrációk."),
          demoText("Kovács Lilla október 15-ig ütemtervet készít.", "Emma Carter prepares a schedule by October 15.")}},
        {t(17, 30), t(22, 5), tr("Számlázási panaszok"),
         {tr("A számlázási jegyek száma nem csökkent; a panaszok fele a számla módosításáról szól."),
          demoText("Varga Nóra szerint a leírás rendben van, a folyamat hosszú.", "According to Olivia Grant, the documentation is fine; the process is long.")}},
        {t(22, 5), t(26, 40), tr("Folyamat vagy dokumentáció"),
         {tr("Két partner a lépések számát tartja a fő gondnak."),
          tr("Abban maradtak, hogy előbb a folyamatot egyszerűsítik, utána a leírást.")}},
        {t(26, 40), t(31, 10), tr("Elakadási pontok gyűjtése"),
         {demoText("Varga Nóra október 10-ig összegyűjti, hol akadnak el a felhasználók.", "Olivia Grant collects where users get stuck by October 10."),
          tr("A gyűjtéshez a jegyek címkéit és a munkamenet-felvételeket használják.")}},
        {t(31, 10), t(35, 20), tr("Új ügyfelek bevezetése"),
         {tr("Az új ügyfelek az első két hétben háromszor annyi jegyet nyitnak."),
          demoText("Molnár Eszter bontást kért ügyféltípus szerint.", "Sophie Bennett asked for a breakdown by customer type.")}},
        {t(35, 20), t(39, 45), tr("Bevezető levelek"),
         {tr("A bevezető levelek megnyitási aránya 40% alatt van."),
          tr("Javaslat: a levelek a súgóoldalakra mutassanak, ne a dokumentációra.")}},
        {t(39, 45), t(43, 30), tr("Partneri visszajelzések"),
         {tr("A partnerek a keresőt dicsérték, a nyomtatható változatot hiányolják.")}},
        {t(43, 30), t(46, 50), tr("Nyomtatható súgó"),
         {tr("Nem döntöttek; a nyomtatható változat igényét előbb felmérik.")}},
        {t(46, 50), t(51, 40), tr("Dokumentációs kapacitás"),
         {tr("A dokumentációs csapat két fővel dolgozik, ez a kiterjesztéshez kevés."),
          demoText("Szabó Áron szerint legalább egy fő kellene a következő félévre.", "According to Lucas Reed, at least one more person is needed for the next half-year.")}},
        {t(51, 40), t(55, 15), tr("Költségvetési keret"),
         {tr("A létszámbővítés legkorábban a jövő évi tervben szerepelhet.")}},
        {t(55, 15), t(58, 40), tr("Döntés a létszámról"),
         {tr("A dokumentációs létszámról a költségvetési tervezés dönt."),
          demoText("Szabó Áron október 20-ig beadja az igényt.", "Lucas Reed submits the request by October 20.")}},
        {t(58, 40), t(61, 30), tr("Külső szövegíró"),
         {tr("Átmeneti megoldásként felmerült egy külső szövegíró; nem döntöttek.")}},
        {t(61, 30), t(64, 10), tr("Mérőszámok"),
         {tr("A jegyszám mellett a megoldási időt is mérik; ez nem javult.")}},
        {t(64, 10), t(67, 0), tr("A következő negyedév céljai"),
         {tr("Cél a számlázási jegyek 20%-os csökkentése.")}},
        {t(67, 0), t(69, 20), tr("Riportok a partnereknek"),
         {tr("A partnerek havi bontású riportot kérnek a jegyekről.")}},
        {t(69, 20), t(71, 0), tr("A riport formája"),
         {tr("A riport táblázatként megy ki, grafikon nélkül.")}},
        {t(71, 0), t(72, 30), tr("Partnerportál"),
         {tr("A partnerportál frissítése a következő negyedévre csúszik.")}},
        {t(72, 30), t(73, 40), tr("Kapcsolattartók"),
         {tr("Minden partnernél egy állandó kapcsolattartó lesz.")}},
        {t(73, 40), t(74, 40), tr("Új-ügyfél bontás"),
         {demoText("Kovács Lilla október 4-ig elküldi a bontást a partnereknek.", "Emma Carter sends the breakdown to the partners by October 4.")}},
        {t(74, 40), t(75, 30), tr("Nyitva maradt ügyek"),
         {tr("Nyitva maradt a nyomtatható súgó és a külső szövegíró kérdése.")}},
        {t(75, 30), t(76, 4), tr("Zárás"),
         {tr("A következő partnertalálkozó januárban lesz.")}},
    };
    const int n = longForm ? int(rows.size()) : 4;
    for (int i = 0; i < n; ++i)
        m_summary.memo.append({rows[i].title, rows[i].from, rows[i].to, rows[i].points});
}

// ---- source links -------------------------------------------------------

QVariantList SummaryViewModel::speakerChips(const QStringList& names) const
{
    QVariantList out;
    for (const QString& n : names) {
        const QString name = n.trimmed();
        if (!name.isEmpty())
            out << QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("colorIndex"), speakerIndexFor(name)}};
    }
    return out;
}

QList<StatementListModel*> SummaryViewModel::statementModels() const
{
    return {m_sentences, m_decisionItems, m_questionItems, m_todoItems, m_memoItems};
}

void SummaryViewModel::rebuildMemo()
{
    m_memo.clear();
    for (int i = 0; i < m_summary.memo.size(); ++i)
        m_memo << memoMapFor(m_summary.memo.at(i), i);
}

QVariantMap SummaryViewModel::memoMapFor(const MemoSection& sec, int index) const
{
    QVariantMap map = memoMap(sec, m_durationMs);
    // Statement rows: the memo statement if there is one ("i<n>.<k>" / "m<n>.<k>"), else the bare text.
    const QString n = QString::number(index + 1) + QLatin1Char('.');
    auto plainRow = [](const QString& text) {
        StatementListModel::Row r;
        r.kind = QStringLiteral("memoPoint");
        r.text = text.trimmed();
        return StatementListModel::rowMap(r);
    };
    QVariantList introRows;
    for (const StatementListModel::Row& r : m_memoItems->items())
        if (r.id.startsWith(QLatin1Char('i') + n))
            introRows << StatementListModel::rowMap(r);
    if (introRows.isEmpty() && !sec.intro.trimmed().isEmpty())
        introRows << plainRow(sec.intro);
    QVariantList blocks;
    for (const auto& g : tanara::summarysrc::memoGroups(sec)) {
        QVariantList rows;
        for (int i : g.second) {
            if (sec.points.at(i).trimmed().isEmpty())
                continue;
            const int row = m_memoItems->indexOfId(QLatin1Char('m') + n + QString::number(i + 1));
            rows << (row >= 0 ? m_memoItems->get(row) : plainRow(sec.points.at(i)));
        }
        if (rows.isEmpty())
            continue;
        const QString title = g.first == QLatin1String("arguments") ? tr("Elhangzott érvek")
                            : g.first == QLatin1String("open")      ? tr("Nyitott kérdés") : g.first;
        blocks << QVariantMap{{QStringLiteral("key"), g.first}, {QStringLiteral("title"), title},
                              {QStringLiteral("rows"), rows}};
    }
    map.insert(QStringLiteral("introRows"), introRows);
    map.insert(QStringLiteral("blocks"), blocks);
    const qint64 start = map.value(QStringLiteral("startMs")).toLongLong();
    const qint64 end = map.value(QStringLiteral("endMs")).toLongLong();
    map.insert(QStringLiteral("sourceRange"),
               start < 0 ? QString()
                         : end > start ? StatementListModel::stamp(start) + QStringLiteral("–") + StatementListModel::stamp(end)
                                       : StatementListModel::stamp(start));
    QStringList names;
    for (const QString& n : sec.speakers)
        if (!n.trimmed().isEmpty()) names << n.trimmed();
    map.insert(QStringLiteral("speakers"), speakerChips(names));
    map.insert(QStringLiteral("speakersText"), names.join(QStringLiteral(" · ")));
    return map;
}

void SummaryViewModel::buildStatements(const QVector<SummaryStatement>& statements,
                                       const QStringList& affectedUtterances)
{
    QVector<StatementListModel::Row> sentences, decisions, todos, questions, memo;
    const QSet<QString> affectedIds(affectedUtterances.cbegin(), affectedUtterances.cend());
    int todoIndex = 0;
    for (const SummaryStatement& st : statements) {
        StatementListModel::Row r;
        r.id = st.id;
        r.text = st.text.trimmed();
        r.flagged = st.flagged;
        r.staleBecause = st.staleBecause.join(QStringLiteral(", "));
        r.ownerStale = st.ownerStaleBecause;
        r.affected = !st.staleBecause.isEmpty() || !st.ownerStaleBecause.isEmpty();
        for (const SourceSpan& sp : st.sourceSpans) {
            if (sp.startMs < 0)
                continue;
            StatementListModel::Span span;
            span.startMs = sp.startMs;
            span.endMs = sp.endMs;
            span.utteranceIds = sp.utteranceIds;
            for (const QString& id : sp.utteranceIds)
                span.affected |= affectedIds.contains(id);
            r.spans << span;
        }
        if (!r.ownerStale.isEmpty()) {
            // "X -> Y?": the new owner's (Y) color on the dot.
            QString to = r.ownerStale.section(QStringLiteral("→"), 1).trimmed();
            if (to.endsWith(QLatin1Char('?'))) to.chop(1);
            r.ownerStaleIndex = speakerIndexFor(to.trimmed());
        }
        switch (st.kind) {
        case StatementKind::Statement:
            r.kind = QStringLiteral("statement");
            sentences << r;
            break;
        case StatementKind::Decision:
            r.kind = QStringLiteral("decision");
            decisions << r;
            break;
        case StatementKind::Todo: {
            r.kind = QStringLiteral("todo");
            r.owner = st.owner.trimmed();
            r.owners = ownerList(r.owner);
            // The action items' order matches actionItems': the due date comes from there.
            if (todoIndex < m_summary.actionItems.size()) {
                const ActionItem& a = m_summary.actionItems.at(todoIndex);
                r.due = a.due;
                if (r.owner.isEmpty() && !a.owner.trimmed().isEmpty()) {
                    r.owner = a.owner.trimmed();
                    r.owners = ownerList(r.owner);
                }
            }
            ++todoIndex;
            todos << r;
            break;
        }
        case StatementKind::OpenQuestion:
            r.kind = QStringLiteral("openQuestion");
            questions << r;
            break;
        case StatementKind::MemoIntro:
            r.kind = QStringLiteral("intro");
            memo << r;
            break;
        case StatementKind::MemoPoint:
            r.kind = QStringLiteral("memoPoint");
            memo << r;
            break;
        }
    }
    m_hasStatements = !statements.isEmpty();
    m_sentences->setRows(sentences);
    m_decisionItems->setRows(decisions);
    m_todoItems->setRows(todos);
    m_questionItems->setRows(questions);
    m_memoItems->setRows(memo);
    rebuildMemo();   // the sections' rows from the memo statements
}

void SummaryViewModel::setSourcesVisible(bool on)
{
    if (on == m_sourcesVisible)
        return;
    m_sourcesVisible = on;
    emit sourcesVisibleChanged();
    emit marksChanged();
}

void SummaryViewModel::setActiveStatementId(const QString& id)
{
    if (id == m_activeStatementId)
        return;
    m_activeStatementId = id;
    emit marksChanged();
}

void SummaryViewModel::setActiveSection(int index)
{
    if (index == m_activeSection)
        return;
    m_activeSection = index;
    emit marksChanged();
}

QVariantList SummaryViewModel::sourceMarks() const
{
    if (!m_hasStatements || !m_sourcesVisible)
        return {};
    // Highlight: the selected statement's sources; with no selection and a stale summary, the
    // affected sources; in the memo (no selection) the highlighted section's statements. The memo
    // view shows the memo statements' sources, the executive view the others'.
    const bool memoView = m_section == QLatin1String("memo") && memoState() == QLatin1String("ready");
    const bool staleMode = m_activeStatementId.isEmpty() && m_stale && m_staleTargeted;
    const QString activeSec = m_activeSection >= 0 ? QString::number(m_activeSection + 1) + QLatin1Char('.')
                                                   : QString();
    auto isActive = [&](const StatementListModel::Row& r, const StatementListModel::Span& sp) {
        if (!m_activeStatementId.isEmpty()) return r.id == m_activeStatementId;
        if (staleMode) return sp.affected;
        return memoView && !activeSec.isEmpty() && r.id.mid(1).startsWith(activeSec);
    };
    QVector<QVariantMap> marks;
    auto add = [&](const StatementListModel* model) {
        for (const StatementListModel::Row& r : model->items()) {
            for (const StatementListModel::Span& sp : r.spans) {
                const bool active = isActive(r, sp);
                bool merged = false;
                for (QVariantMap& m : marks) {
                    if (m.value(QStringLiteral("startMs")).toLongLong() == sp.startMs
                        && m.value(QStringLiteral("endMs")).toLongLong() == sp.endMs) {
                        if (active) m.insert(QStringLiteral("active"), true);
                        merged = true;
                        break;
                    }
                }
                if (!merged)
                    marks << QVariantMap{{QStringLiteral("startMs"), sp.startMs},
                                         {QStringLiteral("endMs"), qMax(sp.endMs, sp.startMs)},
                                         {QStringLiteral("active"), active}};
            }
        }
    };
    if (memoView) {
        add(m_memoItems);
    } else {
        add(m_sentences);
        add(m_decisionItems);
        add(m_questionItems);
        add(m_todoItems);
    }
    std::sort(marks.begin(), marks.end(), [](const QVariantMap& a, const QVariantMap& b) {
        return a.value(QStringLiteral("startMs")).toLongLong() < b.value(QStringLiteral("startMs")).toLongLong();
    });
    QVariantList out;
    for (const QVariantMap& m : std::as_const(marks))
        out << m;
    return out;
}

QVariantList SummaryViewModel::sectionBands() const
{
    QVariantList out;
    for (int i = 0; i < m_memo.size(); ++i) {
        const QVariantMap sec = m_memo.at(i).toMap();
        const qint64 start = sec.value(QStringLiteral("startMs")).toLongLong();
        if (start < 0)
            continue;
        qint64 end = sec.value(QStringLiteral("endMs")).toLongLong();
        if (end < start) {   // unknown end: the next section's start (or the recording's end)
            end = i + 1 < m_memo.size() ? m_memo.at(i + 1).toMap().value(QStringLiteral("startMs")).toLongLong()
                                        : m_durationMs;
            end = qMax(end, start);
        }
        out << QVariantMap{{QStringLiteral("startMs"), start}, {QStringLiteral("endMs"), end},
                           {QStringLiteral("active"), i == m_activeSection},
                           {QStringLiteral("label"), QString::number(i + 1)},
                           {QStringLiteral("index"), i}};
    }
    return out;
}

QString SummaryViewModel::mapHint() const
{
    if (m_section == QLatin1String("memo") && memoState() == QLatin1String("ready")) {
        if (m_activeSection >= 0 && m_activeSection < m_memo.size()) {
            const QString range = m_memo.at(m_activeSection).toMap().value(QStringLiteral("sourceRange")).toString();
            if (!range.isEmpty())
                return tr("a kiemelt szakasz: %1").arg(range);
        }
        return QString();
    }
    if (!m_hasStatements || !m_sourcesVisible)
        return QString();
    if (!m_activeStatementId.isEmpty())
        return tr("kék: a kijelölt állítás forrása · halvány: az összes állításé");
    if (m_stale && m_staleTargeted)
        return tr("kék: a javítás által érintett források");
    return tr("halvány: az összes állítás forrása");
}

QString SummaryViewModel::generationLine() const
{
    if (!m_hasSummary)
        return QString();
    QStringList parts;
    const bool memo = m_section == QLatin1String("memo") && memoState() == QLatin1String("ready");
    if (memo)
        parts << tr("Memó") << tr("%n szakasz", nullptr, int(m_memo.size()));
    else
        parts << modeLabel();
    if (!m_metaDate.isEmpty())
        parts << m_metaDate;
    if (m_stale && m_staleCount > 0 && !memo) {
        parts << tr("azóta %n beszélő-javítás", nullptr, m_staleCount);
    } else {
        if (!m_metaProvider.isEmpty())
            parts << m_metaProvider;
        if (!memo && m_sourceUtterances > 0)
            parts << tr("%n megszólalásból", nullptr, m_sourceUtterances);
    }
    return parts.join(QStringLiteral(" · "));
}

SummaryViewModel::QuoteLine SummaryViewModel::quoteLine(const QString& utteranceId) const
{
    AppController* c = app();
    if (jobsupport::demoMode(c))
        return m_demoLines.value(utteranceId);
    QuoteLine q;
    if (m_meetingId.isEmpty())
        return q;
    SpeakerEditor* editor = c->speakerEditor(m_meetingId);
    if (!editor)
        return q;
    const int idx = editor->indexOf(utteranceId);
    if (idx < 0)
        return q;
    const EditorUtterance u = editor->utteranceAt(idx);
    q.startMs = u.startMs;
    q.endMs = u.endMs;
    q.text = u.text.trimmed();
    for (const EditorSpeaker& s : editor->speakers()) {
        if (s.key == u.speakerKey) {
            q.name = s.displayName;
            q.colorIndex = s.colorIndex;
            break;
        }
    }
    return q;
}

QVariantMap SummaryViewModel::sourceDetails(const QString& statementId) const
{
    const StatementListModel::Row* row = nullptr;
    for (const StatementListModel* model : statementModels()) {
        const int i = model->indexOfId(statementId);
        if (i >= 0) { row = &model->items().at(i); break; }
    }
    if (!row)
        return {};
    QVariantList quotes;
    QSet<QString> seen;
    for (const StatementListModel::Span& sp : row->spans) {
        bool any = false;
        for (const QString& id : sp.utteranceIds) {
            if (seen.contains(id))
                continue;
            seen.insert(id);
            const QuoteLine q = quoteLine(id);
            if (q.text.isEmpty() && q.name.isEmpty())
                continue;
            any = true;
            quotes << QVariantMap{{QStringLiteral("utteranceId"), id}, {QStringLiteral("name"), q.name},
                                  {QStringLiteral("colorIndex"), q.colorIndex},
                                  {QStringLiteral("startMs"), q.startMs}, {QStringLiteral("endMs"), q.endMs},
                                  {QStringLiteral("stamp"), StatementListModel::stamp(q.startMs)},
                                  {QStringLiteral("text"), q.text},
                                  {QStringLiteral("affected"), sp.affected}};
        }
        if (!any)   // time only (the utterance is not found): the range itself
            quotes << QVariantMap{{QStringLiteral("utteranceId"), QString()}, {QStringLiteral("name"), QString()},
                                  {QStringLiteral("colorIndex"), -1},
                                  {QStringLiteral("startMs"), sp.startMs}, {QStringLiteral("endMs"), sp.endMs},
                                  {QStringLiteral("stamp"), StatementListModel::stamp(sp.startMs)},
                                  {QStringLiteral("text"), QString()},
                                  {QStringLiteral("affected"), sp.affected}};
    }
    std::stable_sort(quotes.begin(), quotes.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(QStringLiteral("startMs")).toLongLong() < b.toMap().value(QStringLiteral("startMs")).toLongLong();
    });
    return {{QStringLiteral("statementId"), row->id}, {QStringLiteral("text"), row->text},
            {QStringLiteral("kind"), row->kind}, {QStringLiteral("flagged"), row->flagged},
            {QStringLiteral("staleBecause"), row->staleBecause},
            {QStringLiteral("count"), quotes.size()}, {QStringLiteral("quotes"), quotes}};
}

bool SummaryViewModel::flagStatement(const QString& statementId)
{
    AppController* c = app();
    if (!jobsupport::demoMode(c)) {
        if (m_meetingId.isEmpty() || !c->flagStatement(m_meetingId, statementId))
            return false;
    }
    // Show immediately (live, summaryStatementsChanged reloads anyway).
    bool found = false;
    for (StatementListModel* model : statementModels())
        found |= model->setFlagged(statementId, true);
    if (found && m_memoItems->indexOfId(statementId) >= 0) {
        rebuildMemo();   // the sections' rows are copies: the flag should show there too
        emit changed();
    }
    return found;
}

// Sample data: "Quarterly partner meeting", 2:11:04, five speakers.
void SummaryViewModel::loadDemoSources(const QString& state)
{
    auto t = [](int m, int s) { return qint64(m * 60 + s) * 1000; };
    m_hasSummary = true;
    m_mode = QStringLiteral("quick");
    m_durationMs = t(131, 4);
    m_providerLabel = QStringLiteral("LM Studio");
    m_metaDate = demoText("okt. 1. 17:05", "Oct 1, 5:05 PM");
    m_metaProvider = QStringLiteral("LM Studio");
    m_metaLine = m_metaDate + QStringLiteral(" · ") + m_metaProvider;
    m_modelLine = QStringLiteral("gemma-4-12b");
    m_sourceUtterances = 1298;
    m_transcriptLine = tr("Az átirat kész, %1 beszélő, %2 megszólalás.").arg(5).arg(1298);
    m_speakers = {
        {demoName("Kovács Lilla"), demoName("Kovács Lilla"), 0, 0.22}, {demoName("Fehér Gábor"), demoName("Fehér Gábor"), 1, 0.26},
        {demoName("Varga Árpád"), demoName("Varga Árpád"), 2, 0.19},   {demoName("Molnár Eszter"), demoName("Molnár Eszter"), 3, 0.15},
        {demoRaw(QStringLiteral("Távoli 1")), QString(), 5, 0.07},
    };
    m_participants.clear();
    for (const SpeakerRef& s : std::as_const(m_speakers))   // in the design's order
        m_participants << participantMap(s.name, s.colorIndex, int(s.share * 100.0 + 0.5));

    // Fictional utterances for the quotes (id: "u<startMs>").
    struct Line { qint64 at; qint64 len; int speaker; QString text; };
    const QVector<Line> lines{
        {t(0, 4), 9000, 0, tr("Az első és legfontosabb: a támogatási jegyek száma harmadával csökkent, miközben az aktív felhasználók száma nagyjából tizenkét százalékkal nőtt.")},
        {t(1, 16), 5000, 0, tr("Az összesre. Az újaknál még jobb az arány, de ott kicsi a minta.")},
        {t(4, 18), 8000, 1, tr("A súgóoldalak júliusban mentek élesbe, és az első hat hétben a keresések kétharmada ott ért véget.")},
        {t(12, 41), 7000, 2, tr("Nálunk a beállítási kérdések szinte eltűntek, a számlázásiak viszont maradtak, főleg a Nordvik-ügyfeleknél.")},
        {t(13, 1), 6000, 1, tr("Az első változatot megcsinálom a dokumentációs csapattal, október közepére.")},
        {t(13, 12), 6000, 0, tr("A negyedik negyedév elejére kell, különben a megújításnál újra előjön.")},
        {t(22, 52), 5000, 0, tr("Akkor a súgóoldalakat a többi termékre is kiterjesztjük.")},
        {t(31, 10), 7000, 0, tr("Én a számlázást javasolnám következő súgó-témának, ott a jegyek fele számlázási.")},
        {t(33, 40), 6000, 1, tr("Vállalom, de két hét dokumentációs kapacitás kell hozzá.")},
        {t(36, 2), 7000, 3, tr("A számlázási kérdések felét a díjbekérők adják; ha azokat külön kezeljük, a maradék is eltűnhet.")},
        {t(39, 20), 6000, 2, tr("Ha a súgó nem lesz kész a negyedév elejére, a megújításnál újra előjön ugyanez.")},
        {t(41, 5), 5000, 0, tr("A Nordvik-tárgyalás pont akkor indul, addig kell valami kézzelfogható.")},
        {t(42, 48), 6000, 1, tr("A partnerportálon a leggyakoribb keresés a „számla letöltése”.")},
        {t(43, 30), 7000, 3, tr("És ki adja a dokumentációs kapacitást, ha közben a kiterjesztésen dolgoztok?")},
        {t(47, 2), 6000, 3, tr("A díjbekérős kérdéseket érdemes lenne külön kezelni a súgóban.")},
        {t(50, 15), 6000, 4, tr("A nyomtatható változatot többen kérdezték, de nem tudom, megéri-e.")},
        {t(58, 40), 5000, 2, tr("A létszámról a költségvetési tervezésnél döntünk.")},
    };
    for (const Line& l : lines) {
        const SpeakerRef& sp = m_speakers.at(l.speaker);
        m_demoLines.insert(QStringLiteral("u%1").arg(l.at), {l.at, l.at + l.len, l.text, sp.name, sp.colorIndex});
    }
    auto span = [&](std::initializer_list<qint64> starts) {
        SourceSpan s;
        for (qint64 at : starts) {
            const QString id = QStringLiteral("u%1").arg(at);
            s.utteranceIds << id;
            if (s.startMs < 0) s.startMs = at;
            s.endMs = m_demoLines.value(id).endMs;
        }
        return s;
    };
    auto statement = [](const QString& id, StatementKind kind, const QString& text,
                        const QVector<SourceSpan>& spans, const QString& owner = QString()) {
        SummaryStatement st;
        st.id = id; st.kind = kind; st.text = text; st.sourceSpans = spans; st.owner = owner;
        return st;
    };

    const QString s1 = tr("A harmadik negyedévben a támogatási jegyek száma harmadával csökkent, miközben az aktív felhasználók száma 12%-kal nőtt.");
    const QString s2 = tr("A javulás fő oka a júliusban élesített súgóoldalak: az első hat hétben a keresések kétharmada ott ért véget.");
    const QString s3 = tr("A partnerek oldalán a beállítási kérdések szinte eltűntek, a számlázási kérdések viszont maradtak, főleg a Nordvik-ügyfeleknél.");
    const QString s4 = tr("A számlázási súgónak a negyedik negyedév elejére el kell készülnie, különben a megújításnál újra előjön a probléma.");
    m_summary.execSummary = QStringList{s1, s2, s3, s4}.join(QLatin1Char(' '));
    m_summary.decisions = QStringList{
        tr("A súgóoldalakat a többi termékre is kiterjesztik."),
        tr("A számlázásnál előbb a folyamatot egyszerűsítik, utána a leírást."),
        tr("A dokumentációs létszámról a költségvetési tervezés dönt."),
    };
    m_summary.actionItems = {
        {tr("Számlázási súgó első változata a dokumentációs csapattal"), demoName("Fehér Gábor"), tr("okt. 15.")},
        {tr("Díjbekérő-kérdések külön kezelése a súgóban"), demoName("Molnár Eszter"), tr("okt. 22.")},
    };
    for (const SpeakerRef& s : std::as_const(m_speakers))
        m_summary.participants << s.name;

    QVector<SummaryStatement> sts{
        statement(QStringLiteral("s1"), StatementKind::Statement, s1, {span({t(0, 4), t(1, 16)})}),
        statement(QStringLiteral("s2"), StatementKind::Statement, s2, {span({t(4, 18)})}),
        statement(QStringLiteral("s3"), StatementKind::Statement, s3, {span({t(12, 41)}), span({t(47, 2)})}),
        statement(QStringLiteral("s4"), StatementKind::Statement, s4, {span({t(13, 12)})}),
        statement(QStringLiteral("d1"), StatementKind::Decision, m_summary.decisions.at(0), {span({t(22, 52)})}),
        statement(QStringLiteral("d2"), StatementKind::Decision, m_summary.decisions.at(1), {span({t(31, 10)}), span({t(33, 40)})}),
        statement(QStringLiteral("d3"), StatementKind::Decision, m_summary.decisions.at(2), {span({t(58, 40)})}),
        statement(QStringLiteral("t1"), StatementKind::Todo, m_summary.actionItems.at(0).text, {span({t(13, 1)})}, demoName("Fehér Gábor")),
        statement(QStringLiteral("t2"), StatementKind::Todo, m_summary.actionItems.at(1).text, {span({t(47, 2)})}, demoName("Molnár Eszter")),
    };

    if (state == QLatin1String("staleTargeted")) {
        // A line of Varga Árpád was corrected from Fehér Gábor: the affected statement and action item.
        m_stale = true;
        m_staleCount = 3;
        m_staleTargeted = true;
        sts[2].staleBecause = QStringList{demoText("Fehér Gábor → Varga Árpád?", "Daniel Hughes → James Walker?")};
        sts[7].staleBecause = QStringList{demoText("Fehér Gábor → Varga Árpád?", "Daniel Hughes → James Walker?")};
        sts[7].ownerStaleBecause = demoText("Fehér Gábor → Varga Árpád?", "Daniel Hughes → James Walker?");
        m_affectedStatements = 1;
        m_affectedTodos = 1;
        m_ownerChanges = 1;
        m_affectedUtterances = QStringList{QStringLiteral("u%1").arg(t(12, 41)), QStringLiteral("u%1").arg(t(13, 1))};
    }

    // Open questions (as a statement, with a source chip).
    m_summary.openQuestions = QStringList{
        tr("Ki adja a dokumentációs kapacitást, ha a csapat a negyedik negyedévben a súgó kiterjesztésén dolgozik?"),
        tr("Megéri-e elkészíteni a súgó nyomtatható változatát?"),
    };
    sts << statement(QStringLiteral("q1"), StatementKind::OpenQuestion, m_summary.openQuestions.at(0), {span({t(43, 30)})})
        << statement(QStringLiteral("q2"), StatementKind::OpenQuestion, m_summary.openQuestions.at(1), {span({t(50, 15)})});

    // Memo: seven sections (the 3rd highlighted), with an intro and subsections.
    struct Sec { qint64 from; qint64 to; QString title; QStringList speakers; QStringList points; };
    const QString lilla = demoName("Kovács Lilla"), gabor = demoName("Fehér Gábor"), arpad = demoName("Varga Árpád"),
                  eszter = demoName("Molnár Eszter"), tavoli = demoRaw(QStringLiteral("Távoli 1"));
    const QVector<Sec> secs{
        {t(0, 0), t(12, 30), tr("Nyitás, Q3 számok"), {lilla, gabor},
         {demoText("Kovács Lilla összefoglalta a negyedév számait: a jegyek harmadával kevesebbek, az aktív felhasználók 12%-kal többen vannak.", "Emma Carter summarized the quarter's numbers: tickets are down by a third, active users are up 12%."),
          tr("A partnerek kérték, hogy a számlázás külön napirendi pont legyen.")}},
        {t(12, 30), t(31, 10), tr("Súgóoldalak"), {gabor, arpad, lilla},
         {tr("A júliusban élesített súgóoldalak az első hat hétben a keresések kétharmadát lezárták."),
          tr("A súgót a többi termékre is kiterjesztik.")}},
        {t(31, 10), t(44, 5), tr("Számlázási súgó és a Nordvik-megújítás"), {lilla, gabor, eszter},
         {tr("A díjbekérőkkel kapcsolatos kérdések a számlázási kérdések felét adják; külön kezelve a maradék is eltűnhet."),
          tr("Ha a súgó nem készül el a negyedév elejére, a megújítási tárgyalásokon újra előjön a probléma."),
          tr("A partnerportál keresési adatai alapján a „számla letöltése” a leggyakoribb keresés."),
          tr("Ki adja a dokumentációs kapacitást, ha a csapat a negyedik negyedévben a súgó kiterjesztésén dolgozik?")}},
        {t(44, 5), t(64, 40), tr("Partneri visszajelzések"), {eszter, arpad, tavoli},
         {tr("A partnerek a keresőt dicsérték, a nyomtatható változatot hiányolják."),
          tr("A díjbekérős kérdéseket külön kezelik a súgóban.")}},
        {t(64, 40), t(92, 10), tr("Nordvik-megújítás"), {arpad, lilla},
         {tr("A Nordvik-szerződés megújítása a számlázási súgón múlik."),
          tr("A tárgyalás a negyedik negyedév elején indul.")}},
        {t(92, 10), t(115, 50), tr("Költségvetés"), {lilla, arpad, gabor},
         {tr("A dokumentációs létszámról a költségvetési tervezés dönt.")}},
        {t(115, 50), t(131, 4), tr("Lezárás, teendők"), {lilla, gabor, eszter},
         {demoText("Fehér Gábor október 15-ig elkészíti a számlázási súgó első változatát.", "Daniel Hughes will finish the first version of the billing help by October 15."),
          demoText("Molnár Eszter október 22-ig kidolgozza a díjbekérő-kérdések külön kezelését.", "Sophie Bennett will work out the separate handling of proforma invoice questions by October 22.")}},
    };
    for (const Sec& s : secs)
        m_summary.memo.append({s.title, s.from, s.to, s.points, s.speakers});
    // The 3rd section: a two-sentence intro, "Arguments raised" and "Open question" subsections.
    MemoSection& third = m_summary.memo[2];
    third.intro = demoText("Kovács Lilla szerint a számlázási kérdések a jegyek felét teszik ki a Nordvik-ügyfeleknél, ezért a következő súgó-témának ezt javasolja.", "According to Emma Carter, billing questions make up half the tickets for Nordvik customers, so she suggests it as the next help topic.")
                + QLatin1Char(' ')
                + demoText("Fehér Gábor vállalta az első változatot, de két hét dokumentációs kapacitást kér hozzá.", "Daniel Hughes took on the first version, but asks for two weeks of documentation capacity.");
    third.pointGroups = QStringList{QStringLiteral("arguments"), QStringLiteral("arguments"),
                                    QStringLiteral("arguments"), QStringLiteral("open")};
    m_summary.memo[0].intro = tr("A negyedév számai jók, a számlázás külön napirendi pontot kapott.");
    // The memo's statements (points and intro sentences with a source; a few points without one).
    auto memoSt = [&](const QString& id, StatementKind kind, const QString& text,
                      const QVector<SourceSpan>& spans) { sts << statement(id, kind, text, spans); };
    memoSt(QStringLiteral("i1.1"), StatementKind::MemoIntro, m_summary.memo[0].intro, {span({t(0, 4)})});
    memoSt(QStringLiteral("m1.1"), StatementKind::MemoPoint, m_summary.memo[0].points.at(0), {span({t(0, 4)}), span({t(1, 16)})});
    memoSt(QStringLiteral("m1.2"), StatementKind::MemoPoint, m_summary.memo[0].points.at(1), {});
    memoSt(QStringLiteral("m2.1"), StatementKind::MemoPoint, m_summary.memo[1].points.at(0), {span({t(4, 18)})});
    memoSt(QStringLiteral("m2.2"), StatementKind::MemoPoint, m_summary.memo[1].points.at(1), {span({t(22, 52)})});
    memoSt(QStringLiteral("i3.1"), StatementKind::MemoIntro,
           demoText("Kovács Lilla szerint a számlázási kérdések a jegyek felét teszik ki a Nordvik-ügyfeleknél, ezért a következő súgó-témának ezt javasolja.", "According to Emma Carter, billing questions make up half the tickets for Nordvik customers, so she suggests it as the next help topic."),
           {span({t(31, 10)})});
    memoSt(QStringLiteral("i3.2"), StatementKind::MemoIntro,
           demoText("Fehér Gábor vállalta az első változatot, de két hét dokumentációs kapacitást kér hozzá.", "Daniel Hughes took on the first version, but asks for two weeks of documentation capacity."),
           {span({t(33, 40)})});
    memoSt(QStringLiteral("m3.1"), StatementKind::MemoPoint, third.points.at(0), {span({t(36, 2)})});
    memoSt(QStringLiteral("m3.2"), StatementKind::MemoPoint, third.points.at(1), {span({t(39, 20)}), span({t(41, 5)})});
    memoSt(QStringLiteral("m3.3"), StatementKind::MemoPoint, third.points.at(2), {span({t(42, 48)})});
    memoSt(QStringLiteral("m3.4"), StatementKind::MemoPoint, third.points.at(3), {span({t(43, 30)})});
    memoSt(QStringLiteral("m4.1"), StatementKind::MemoPoint, m_summary.memo[3].points.at(0), {span({t(50, 15)})});
    memoSt(QStringLiteral("m4.2"), StatementKind::MemoPoint, m_summary.memo[3].points.at(1), {span({t(47, 2)})});
    memoSt(QStringLiteral("m6.1"), StatementKind::MemoPoint, m_summary.memo[5].points.at(0), {span({t(58, 40)})});
    memoSt(QStringLiteral("m7.1"), StatementKind::MemoPoint, m_summary.memo[6].points.at(0), {span({t(13, 1)})});
    memoSt(QStringLiteral("m7.2"), StatementKind::MemoPoint, m_summary.memo[6].points.at(1), {span({t(47, 2)})});
    rebuildMemo();

    for (const QString& d : std::as_const(m_summary.decisions))
        m_decisions << decisionMap(d, m_durationMs);
    for (const QString& q : std::as_const(m_summary.openQuestions))
        m_openQuestions << decisionMap(q, m_durationMs);
    for (const ActionItem& a : std::as_const(m_summary.actionItems))
        m_actions << QVariantMap{{QStringLiteral("text"), a.text}, {QStringLiteral("owner"), a.owner},
                                 {QStringLiteral("ownerIndex"), speakerIndexFor(a.owner)},
                                 {QStringLiteral("owners"), ownerList(a.owner)},
                                 {QStringLiteral("due"), a.due}};
    m_execSummary = m_summary.execSummary;
    m_markdown = m_summary.renderMarkdown();

    // "noSources": an old (source-less) summary, today's view.
    if (state != QLatin1String("noSources")) {
        m_summary.statements = sts;
        m_rawStatements = sts;
    }
    buildStatements(m_rawStatements, m_affectedUtterances);
}

} // namespace tanara_qml

