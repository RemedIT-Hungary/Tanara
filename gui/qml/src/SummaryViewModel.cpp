#include "SummaryViewModel.h"
#include "tanara/foundation/log/Logging.h"

#include "AppContext.h"
#include "JobSupport.h"
#include "SummaryProgress.h"

#include "tanara/domain/SpeakerText.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/library/summary/SummaryRepository.h"
#include "tanara/processing/summary/SummarySources.h"

#include <QClipboard>
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
    init();
    AppContext* ctx = AppContext::instance();
    connect(ctx, &AppContext::controllerChanged, this, [this]() { resolveFacade(); reload(); });
    connect(ctx, &AppContext::demoChanged, this, [this]() { resolveFacade(); reload(); });
    resolveFacade();
    reload();
}

SummaryViewModel::SummaryViewModel(processing::ISummaryFacade* facade, QObject* parent)
    : QObject(parent), m_topics(new TopicListModel(this)), m_note(new MeetingNoteModel(this)),
      m_sentences(new StatementListModel(this)), m_decisionItems(new StatementListModel(this)),
      m_todoItems(new StatementListModel(this)), m_questionItems(new StatementListModel(this)),
      m_memoItems(new StatementListModel(this)), m_facade(facade), m_explicitFacade(true)
{
    init();
    connectFacade();
    m_note->setController(facade ? facade->noteController() : nullptr);
    reload();
}

void SummaryViewModel::init()
{
    connect(this, &SummaryViewModel::changed, this, &SummaryViewModel::generationLineChanged);
    connect(this, &SummaryViewModel::sectionChanged, this, &SummaryViewModel::generationLineChanged);
    connect(this, &SummaryViewModel::sectionChanged, this, &SummaryViewModel::marksChanged);
    connect(m_topics, &TopicListModel::countsChanged, this, &SummaryViewModel::changed);
    connect(m_note, &MeetingNoteModel::noteChanged, this, &SummaryViewModel::noteHintChanged);
}

bool SummaryViewModel::demo() const { return m_facade && m_facade->fictional(); }

// The facade of the moment (QML-created view models): the real one, or the fixture of the demoState.
void SummaryViewModel::resolveFacade()
{
    if (m_explicitFacade)
        return;
    const auto& make = AppContext::instance()->services().summary;
    processing::ISummaryFacade* f = make ? make(m_demoState, this) : nullptr;
    if (f == m_facade)
        return;
    for (const QMetaObject::Connection& conn : std::as_const(m_connections))
        disconnect(conn);
    m_connections.clear();
    if (m_facade && m_facade->parent() == this)   // our own fixture
        m_facade->deleteLater();
    m_facade = f;
    connectFacade();
    m_note->setController(f ? f->noteController() : nullptr);
}

// The tab's initial UI state of a fixture (screenshots): the memo / note / Sources switch / selection.
void SummaryViewModel::applyPreset()
{
    const std::optional<processing::SummaryUiPreset> p = m_facade ? m_facade->uiPreset() : std::nullopt;
    if (!p)
        return;
    setSection(p->memo ? QStringLiteral("memo") : QStringLiteral("exec"));
    setNoteOpen(p->noteOpen);
    m_sourcesVisible = p->sourcesVisible;
    m_activeStatementId = p->activeStatementId;
    m_activeSection = p->activeSection;
    emit sourcesVisibleChanged();
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
    if (m_facade && !id.isEmpty() && m_facade->topicsResumable(id))
        m_topicsOpen = true;
    reload();
}

void SummaryViewModel::setDemoState(const QString& state)
{
    if (state == m_demoState)
        return;
    m_demoState = state;
    emit demoStateChanged();
    resolveFacade();
    applyPreset();
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

void SummaryViewModel::connectFacade()
{
    processing::ISummaryFacade* f = m_facade;
    if (!f)
        return;
    auto mine = [this](const QString& id) { return id == m_meetingId; };
    m_connections << connect(f, &processing::ISummaryFacade::changed, this,
                             [this](const QString& id) { if (id.isEmpty() || id == m_meetingId) reload(); });
    m_connections << connect(f, &processing::ISummaryFacade::jobsChanged, this,
                             [this, mine](const QString& id) { if (mine(id)) reloadJobs(); });
    m_connections << connect(f, &processing::ISummaryFacade::summaryArrived, this,
                             [this, mine](const QString& id) {
        if (!mine(id)) return;
        m_topicsOpen = false;          // finished -> we show the summary, the short form
        if (m_section != QLatin1String("exec")) {
            m_section = QStringLiteral("exec");
            emit sectionChanged();
        }
        reload();
        emit summaryArrived();
    });
    m_connections << connect(f, &processing::ISummaryFacade::topicsArrived, this,
                             [this, mine](const QString& id) {
        if (!mine(id)) return;
        m_topicsOpen = true;           // the suggested topics arrived -> editor
        reload();
    });
    m_connections << connect(f, &processing::ISummaryFacade::speakersChanged, this,
                             [this, mine](const QString& id) { if (mine(id)) reloadParticipants(); });
    // The note (and the detected call) live in meeting.json: after saving / editing from another view.
    m_connections << connect(f, &processing::ISummaryFacade::noteUpdated, this,
                             [this, mine](const QString& id) { if (mine(id)) m_note->reload(); });
}

void SummaryViewModel::refresh() { reload(); }

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
    if (!m_facade || !m_valid)
        return;
    applySpeakers(m_facade->speakers(m_meetingId));
}

// The meeting's speakers: the transcript line, the participants, the owner chips' colors.
void SummaryViewModel::applySpeakers(const processing::SummarySpeakers& speakers)
{
    m_speakers.clear();
    for (const processing::SummarySpeaker& s : speakers.list)
        m_speakers.append({s.name, s.personName, s.colorIndex, s.share});
    const int speakerCount = speakers.speakerCount >= 0 ? speakers.speakerCount : int(speakers.list.size());
    m_sourceUtterances = speakers.utteranceCount;
    m_transcriptLine = speakerCount == 0
        ? tr("Az átirat kész.")
        : tr("Az átirat kész, %1 beszélő, %2 megszólalás.").arg(speakerCount).arg(speakers.utteranceCount);

    // Participants: the meeting's speakers in the facade's order (by speaking time); names mentioned
    // in the summary but not among the speakers go last, without a share.
    QVariantList list;
    for (const SpeakerRef& s : std::as_const(m_speakers))
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
    if (!m_facade || !m_valid)
        return;
    applyJobs(m_facade->jobs(m_meetingId));
    emit jobChanged();
}

void SummaryViewModel::applyJobs(const processing::SummaryJobs& jobs)
{
    const JobProgress& shown = jobs.shown;
    const bool running = shown.isValid();
    m_analyzing = jobs.analyze.isValid();
    m_jobKind = running ? int(shown.kind) : -1;
    m_jobTitle = shown.title;
    m_jobMessage = shown.message;
    m_jobCancelling = running ? shown.cancelling : (m_analyzing && jobs.analyze.cancelling);
    applyJob(shown);
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
    m_cloudSoon = false;
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
    m_summaryNote.clear();
    m_summaryNoteKnown = false;
    m_jobKind = -1;
    m_jobTitle.clear();
    m_jobMessage.clear();
    m_jobCancelling = false;
    m_analyzing = false;
    applyJob(JobProgress());
    m_transcriptLine.clear();

    const processing::SummaryData d = m_facade ? m_facade->load(m_meetingId) : processing::SummaryData();
    m_valid = d.valid;
    if (!m_valid) {
        m_participants.clear();
        buildStatements({}, {});
        emit marksChanged();
        m_topics->bind(nullptr, QString());
        m_note->reload();
        emit noteHintChanged();
        emit participantsChanged();
        emit jobChanged();
        emit changed();
        return;
    }

    m_durationMs = d.durationMs;
    m_topics->bind(m_facade, m_meetingId);
    if (d.topicsOpen)
        m_topicsOpen = *d.topicsOpen;
    if (d.note)   // a fixture's note block
        m_note->setDemoContent(d.note->note, d.note->detectedCall,
                               d.note->suggestions ? MeetingNoteModel::demoSuggestions() : QVariantList());
    else
        m_note->reload();

    // Gating + provider.
    m_canRun = d.readiness.runnable;
    m_blocker = jobsupport::blockerInfo(providers::WorkflowStep::Summarize, d.readiness);
    m_providerLabel = providerLabelFor(d);
    m_cloudSoon = d.cloudSoon;

    // Leftover error (the last summary attempt).
    if (d.summaryError.isValid()) {
        m_errorMessage = d.summaryError.message;
        m_errorDetail = d.summaryError.detail;
        const jobsupport::FixAction fix = jobsupport::fixActionForError(d.summaryError);
        m_fixActionLabel = fix.label;
        m_fixActionPage = fix.page;
        m_fixReloadContext = fix.reloadContext;
        m_errorKeptParts = d.errorKeptParts;
    }

    // The summary, structured (new: summary.json; old: recovered from summary.md).
    const library::SummaryDocument& doc = d.document;
    m_hasSummary = doc.exists;
    if (doc.exists) {
        m_markdown = doc.markdown;
        m_mode = library::SummaryRepository::modeToString(doc.meta.mode);
        m_summary = doc.summary;
        m_execSummary = doc.summary.execSummary.trimmed();
        m_summaryParticipants = doc.summary.participants;
        for (const QString& s : doc.summary.decisions)
            m_decisions << decisionMap(s, m_durationMs);
        for (const QString& q : doc.summary.openQuestions)
            m_openQuestions << decisionMap(q, m_durationMs);
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
        if (!d.generatedText.isEmpty())
            m_metaDate = d.generatedText;
        else if (doc.meta.createdAt.isValid())
            m_metaDate = QLocale().toString(doc.meta.createdAt, tr("MMM d. HH:mm"));
        if (!m_metaDate.isEmpty())
            meta << m_metaDate;
        if (!d.providerText.isEmpty()) {
            m_metaProvider = d.providerText;
        } else if (doc.meta.providerId == cloud::ProviderId) {
            m_metaProvider = QStringLiteral("Tanara Cloud");
        } else if (!doc.meta.providerId.isEmpty()) {
            const QString name = m_facade->chatProviderName(doc.meta.providerId);
            m_metaProvider = name.isEmpty() ? tr("saját kulcs") : tr("%1 · saját kulcs").arg(name);
        }
        if (!m_metaProvider.isEmpty())
            meta << m_metaProvider;
        m_metaLine = meta.join(QStringLiteral(" · "));
        m_modelLine = doc.meta.model;
        m_summaryNote = doc.meta.contextNote;
        m_summaryNoteKnown = doc.meta.contextNoteKnown;

        m_stale = d.stale.stale;
        m_staleCount = d.stale.correctedSpeakers;
        if (!doc.summary.statements.isEmpty()) {
            // Quick summary with sources: the targeted staleness and the statements per the current
            // speaker state (staleBecause).
            m_staleTargeted = d.stale.targeted;
            m_affectedStatements = d.stale.affectedStatements;
            m_affectedTodos = d.stale.affectedTodos;
            m_ownerChanges = d.stale.ownerChanges;
            m_affectedUtterances = d.stale.affectedUtteranceIds;
            m_rawStatements = d.statements;
        }
    }

    applySpeakers(d.speakers);   // speakers -> participants, owner colors, transcript row (+ changed)
    applyJobs(d.jobs);
    emit jobChanged();
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
    if (m_facade)
        m_facade->dismissStale(m_meetingId);   // -> changed -> reload()
}

void SummaryViewModel::clearError()
{
    if (m_facade)
        m_facade->clearError(m_meetingId);   // -> changed -> reload()
}

QString SummaryViewModel::providerLabelFor(const processing::SummaryData& data) const
{
    if (data.usesCloud)
        return QStringLiteral("Tanara Cloud");
    return data.providerName.isEmpty() ? tr("saját kulcs") : tr("%1 · saját kulcs").arg(data.providerName);
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
    for (const auto& g : tanara::processing::summarysrc::memoGroups(sec)) {
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
        if (!n.trimmed().isEmpty()) names << tanara::speakerLabelText(n.trimmed());   // a raw key in the UI language
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
            const processing::SummaryQuote q = m_facade ? m_facade->quote(m_meetingId, id) : processing::SummaryQuote();
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
    if (!m_facade || !m_facade->flagStatement(m_meetingId, statementId))
        return false;
    // Show immediately (live, the changed statements reload anyway).
    bool found = false;
    for (StatementListModel* model : statementModels())
        found |= model->setFlagged(statementId, true);
    if (found && m_memoItems->indexOfId(statementId) >= 0) {
        rebuildMemo();   // the sections' rows are copies: the flag should show there too
        emit changed();
    }
    return found;
}

} // namespace tanara_qml
