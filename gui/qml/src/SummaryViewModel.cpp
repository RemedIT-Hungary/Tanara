#include "SummaryViewModel.h"

#include "AppContext.h"
#include "JobSupport.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/cloud/CloudAccount.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/jobs/MeetingJobTracker.h"
#include "tanara/provider/ProviderRegistry.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/summary/SummaryStore.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>

namespace tanara_qml {

using namespace tanara;

namespace {

QVariantMap decisionMap(const QString& raw, qint64 durationMs)
{
    QString rest;
    qint64 ms = SummaryViewModel::splitTimestamp(raw, &rest);
    if (ms >= 0 && durationMs > 0 && ms > durationMs) {   // nem a felvételre mutat → nem hivatkozás
        ms = -1;
        rest = raw;
    }
    return {{QStringLiteral("text"), rest}, {QStringLiteral("ms"), ms},
            {QStringLiteral("stamp"), ms >= 0 ? jobsupport::formatDuration(ms) : QString()}};
}

QVariantMap participantMap(const QString& name, int colorIndex, int percent)
{
    return {{QStringLiteral("name"), name}, {QStringLiteral("colorIndex"), colorIndex},
            {QStringLiteral("percent"), percent}};
}

} // namespace

qint64 SummaryViewModel::splitTimestamp(const QString& text, QString* rest)
{
    // „[12:52] …”, „(1:02:10) …”, „12:52 – …” a sor elején.
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
    : QObject(parent), m_topics(new TopicListModel(this))
{
    connect(m_topics, &TopicListModel::countsChanged, this, &SummaryViewModel::changed);

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
    connectController();
    emit controllerChanged();
    reload();
}

void SummaryViewModel::setMeetingId(const QString& id)
{
    if (id == m_meetingId)
        return;
    const bool switching = !m_meetingId.isEmpty();
    m_meetingId = id;
    emit meetingIdChanged();
    // Másik meetingre váltva a munkaterület bezárul; ha az új meetingnek van téma-listája, de
    // még nincs összefoglalója, megnyílik (a félbemaradt témánkénti elemzés onnan folytatható,
    // ahol a lemezen tart). Az első beállításnál a kívülről kért topicsOpen megmarad.
    AppController* c = app();
    if (switching)
        m_topicsOpen = false;
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
    reload();
}

void SummaryViewModel::setTopicsOpen(bool open)
{
    if (open == m_topicsOpen)
        return;
    m_topicsOpen = open;
    emit changed();
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
        m_topicsOpen = false;          // elkészült → az összefoglalót mutatjuk
        reload();
        emit summaryArrived();
    });
    m_connections << connect(c, &AppController::topicsReady, this,
                             [this, mine](const QString& id, const QVector<SummaryTopic>&) {
        if (!mine(id)) return;
        m_topicsOpen = true;           // megjöttek a javasolt témák → szerkesztő
        reload();
    });
    m_connections << connect(c, &AppController::summaryStaleChanged, this,
                             [this, mine](const QString& id) { if (mine(id)) reload(); });
    m_connections << connect(c, &AppController::transcriptReady, this,
                             [this, mine](const QString& id, const QString&) { if (mine(id)) reload(); });
    m_connections << connect(c, &AppController::speakerMapChanged, this,
                             [this, mine](const QString& id) { if (mine(id)) reloadParticipants(); });
    m_connections << connect(c->settings(), &SettingsManager::settingsChanged, this,
                             &SummaryViewModel::reload);
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
    // A modell gyakran csak a keresztnevet / vezetéknevet írja: egyértelmű szó-egyezés is jó.
    int found = -1, hits = 0;
    for (const SpeakerRef& s : m_speakers) {
        const QStringList words = s.name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString& w : words) {
            if (w.compare(n, Qt::CaseInsensitive) == 0) { found = s.colorIndex; ++hits; break; }
        }
    }
    return hits == 1 ? found : -1;
}

// „A, B és C” → külön chipek, mindegyik a saját beszélő-színében (ha a meeting beszélője).
QVariantList SummaryViewModel::ownerList(const QString& owner) const
{
    static const QRegularExpression sep(QStringLiteral("\\s*(?:,|;|/|&|\\bés\\b|\\band\\b)\\s*"),
                                        QRegularExpression::UseUnicodePropertiesOption);
    QVariantList out;
    // Ha az egész szöveg egy beszélő neve, nem daraboljuk.
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
    m_transcriptLine = m_speakers.isEmpty()
        ? tr("Az átirat kész.")
        : tr("Az átirat kész, %1 beszélő, %2 megszólalás.").arg(m_speakers.size()).arg(utterances);

    // Résztvevők: a meeting beszélői beszédidő szerint; az összefoglalóban említett, de a
    // beszélők közt nem szereplő nevek a végén, arány nélkül.
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

    // A felelős-chipek színe a beszélőkből jön → a teendők újraszíneződnek.
    QVariantList actions;
    for (const QVariant& v : std::as_const(m_actions)) {
        QVariantMap a = v.toMap();
        const QString owner = a.value(QStringLiteral("owner")).toString();
        a.insert(QStringLiteral("ownerIndex"), speakerIndexFor(owner));
        a.insert(QStringLiteral("owners"), ownerList(owner));
        actions << a;
    }
    m_actions = actions;
    emit participantsChanged();
    emit changed();
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
    emit jobChanged();
}

void SummaryViewModel::reload()
{
    m_blocker.clear();
    m_canRun = false;
    m_cloudSelected = false;
    m_cloudTierLabel.clear();
    m_cloudTeaser = false;
    m_errorMessage.clear();
    m_errorDetail.clear();
    m_fixActionLabel.clear();
    m_fixActionPage.clear();
    m_hasSummary = false;
    m_stale = false;
    m_staleCount = 0;
    m_mode = QStringLiteral("unknown");
    m_metaLine.clear();
    m_modelLine.clear();
    m_execSummary.clear();
    m_markdown.clear();
    m_decisions.clear();
    m_actions.clear();
    m_topicSections.clear();
    m_summaryParticipants.clear();
    m_jobKind = -1;
    m_jobTitle.clear();
    m_jobMessage.clear();
    m_jobCancelling = false;
    m_analyzing = false;
    m_transcriptLine.clear();

    AppController* c = app();
    if (jobsupport::demoMode(c)) {
        watchEditor(nullptr);
        m_valid = true;
        loadDemo();
        emit participantsChanged();
        emit jobChanged();
        emit changed();
        return;
    }
    if (m_meetingId.isEmpty()) {
        watchEditor(nullptr);
        m_valid = false;
        m_participants.clear();
        m_topics->clear();
        emit participantsChanged();
        emit jobChanged();
        emit changed();
        return;
    }

    const Meeting m = c->store()->load(m_meetingId);
    m_valid = !m.id.isEmpty();
    m_durationMs = m.durationMs;
    m_topics->bind(c, m_meetingId);

    // Kapuzás + szolgáltató.
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

    // Megmaradt hiba (az utolsó összefoglaló-kísérlet).
    const MeetingProcessingState ps = c->jobs()->state(m);
    if (ps.summaryError.isValid()) {
        m_errorMessage = ps.summaryError.message;
        m_errorDetail = ps.summaryError.detail;
        const jobsupport::FixAction fix = jobsupport::fixActionForError(ps.summaryError);
        m_fixActionLabel = fix.label;
        m_fixActionPage = fix.page;
    }

    // Az összefoglaló strukturáltan (új: summary.json; régi: a summary.md-ből visszanyerve).
    const SummaryDocument doc = c->summaryDocument(m_meetingId);
    m_hasSummary = doc.exists;
    if (doc.exists) {
        m_markdown = doc.markdown;
        m_mode = summarystore::modeToString(doc.meta.mode);
        m_execSummary = doc.summary.execSummary.trimmed();
        m_summaryParticipants = doc.summary.participants;
        for (const QString& d : doc.summary.decisions)
            m_decisions << decisionMap(d, m.durationMs);
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
                                           {QStringLiteral("actions"), acts}};
        }
        QStringList meta;
        if (doc.meta.createdAt.isValid())
            meta << QLocale().toString(doc.meta.createdAt, tr("MMM d. HH:mm"));
        if (doc.meta.providerId == cloud::ProviderId) {
            meta << QStringLiteral("Tanara Cloud");
        } else if (!doc.meta.providerId.isEmpty()) {
            const QString name = LlmProviderRegistry::instance().descriptor(doc.meta.providerId).displayName;
            meta << (name.isEmpty() ? tr("saját kulcs") : tr("%1 · saját kulcs").arg(name));
        }
        m_metaLine = meta.join(QStringLiteral(" · "));
        m_modelLine = doc.meta.model;

        const SummaryStaleInfo st = c->summaryStale(m);
        m_stale = st.stale;
        m_staleCount = st.correctedSpeakers;
    }

    reloadParticipants();   // beszélők → résztvevők, felelős-színek, átirat-sor (+ changed)
    reloadJobs();
}

bool SummaryViewModel::copyToClipboard()
{
    if (m_markdown.isEmpty() || !qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
        return false;
    QGuiApplication::clipboard()->setText(m_markdown);
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
        emit changed();
        return;
    }
    if (!m_meetingId.isEmpty())
        c->jobs()->clearError(m_meetingId, JobKind::Summarize);
}

// Kitalált mintaadat (a tervrajz M06–M08 képernyőinek megfelelően).
void SummaryViewModel::loadDemo()
{
    const QString st = m_demoState.isEmpty() ? QStringLiteral("stale") : m_demoState;
    m_providerLabel = tr("%1 · saját kulcs").arg(QStringLiteral("LM Studio"));
    m_canRun = true;
    m_transcriptLine = tr("Az átirat kész, %1 beszélő, %2 megszólalás.").arg(4).arg(412);
    m_durationMs = 76 * 60 * 1000 + 4000;
    m_participants.clear();

    const bool empty = st.startsWith(QLatin1String("empty"));
    if (st == QLatin1String("topics")) {
        m_topics->loadDemo();
        m_topicsOpen = true;
    } else if (m_topics->count() > 0 || m_topicsOpen) {
        m_topics->clear();
        m_topicsOpen = false;
    }

    if (empty) {
        if (st == QLatin1String("emptyBlocked")) {
            m_canRun = false;
            m_blocker = {
                {QStringLiteral("title"), tr("Nincs beállítva összefoglaló szolgáltató")},
                {QStringLiteral("text"), tr("Hiányzik: Alap URL (OpenAI-kompatibilis). Add meg a saját "
                                            "szolgáltatód adatait a Beállításokban.")},
                {QStringLiteral("actionLabel"), tr("Szolgáltató beállítása")},
                {QStringLiteral("actionPage"), QStringLiteral("providers")},
                {QStringLiteral("reason"), tr("A szolgáltató beállítása után indítható.")},
            };
        } else if (st == QLatin1String("emptyRunning")) {
            m_jobKind = int(JobKind::Summarize);
            m_jobTitle = tr("Összefoglaló készítése");
        } else if (st == QLatin1String("emptyError")) {
            m_errorMessage = tr("A szolgáltató nem válaszolt időben. Próbáld újra.");
            m_errorDetail = QStringLiteral("HTTP 504 · gateway_timeout");
        }
        return;
    }

    // --- kész összefoglaló ---
    m_hasSummary = true;
    m_markdown = QStringLiteral("## Vezetői összefoglaló\n\n…");
    m_speakers = {
        {tr("Kovács Lilla"), tr("Kovács Lilla"), 0, 0.34}, {tr("Tóth Bence"), tr("Tóth Bence"), 1, 0.22},
        {tr("Varga Nóra"), tr("Varga Nóra"), 2, 0.17},     {tr("Molnár Eszter"), tr("Molnár Eszter"), 3, 0.14},
        {tr("Szabó Áron"), tr("Szabó Áron"), 4, 0.08},     {tr("Távoli 2"), QString(), 5, 0.05},
    };
    for (const SpeakerRef& s : std::as_const(m_speakers))
        m_participants << participantMap(s.name, s.colorIndex, int(s.share * 100.0 + 0.5));

    m_execSummary = tr("A partnerek megerősítették, hogy a harmadik negyedévben a támogatási igény "
                       "harmadával csökkent, főként az új súgóoldalak miatt. A súgót a többi termékre "
                       "is kiterjesztik, de a számlázásnál előbb a folyamatot egyszerűsítik. A "
                       "dokumentációs létszámbővítésről a költségvetés dönt.");
    const QStringList decisions{
        tr("[12:52] A súgóoldalakat a többi termékre is kiterjesztik."),
        tr("[31:10] A számlázásnál előbb a folyamatot egyszerűsítik, utána a leírást."),
        tr("[58:40] A dokumentációs létszámról a költségvetési tervezés dönt."),
    };
    for (const QString& d : decisions)
        m_decisions << decisionMap(d, m_durationMs);
    auto action = [this](const QString& text, const QString& owner, const QString& due) {
        return QVariantMap{{QStringLiteral("text"), text}, {QStringLiteral("owner"), owner},
                           {QStringLiteral("ownerIndex"), speakerIndexFor(owner)},
                           {QStringLiteral("owners"), ownerList(owner)},
                           {QStringLiteral("due"), due}};
    };
    m_actions = {
        action(tr("A súgóoldalak kiterjesztése a további termékekre: ütemterv"), tr("Kovács Lilla"), tr("okt. 15.")),
        action(tr("Számlázási elakadási pontok összegyűjtése"), tr("Varga Nóra"), tr("okt. 10.")),
        action(tr("Dokumentációs létszámigény a költségvetési tervbe"), tr("Szabó Áron"), tr("okt. 20.")),
        action(tr("Új-ügyfél bontás elküldése a partnereknek"), tr("Kovács Lilla"), tr("okt. 4.")),
    };
    m_mode = QStringLiteral("quick");
    m_metaLine = tr("okt. 1. 17:05") + QStringLiteral(" · ") + m_providerLabel;
    m_modelLine = QStringLiteral("gemma-4-12b");
    m_stale = st == QLatin1String("stale");
    m_staleCount = m_stale ? 3 : 0;

    if (st == QLatin1String("topicsDoc")) {
        m_mode = QStringLiteral("topics");
        m_topicSections = {
            QVariantMap{{QStringLiteral("title"), tr("Támogatási igény alakulása")},
                        {QStringLiteral("detail"), tr("A jegyek száma harmadával csökkent, miközben az aktív "
                                                      "felhasználók 12%-kal nőttek. A csökkenés fő oka a "
                                                      "termékbe épített súgó.")},
                        {QStringLiteral("decisions"), QStringList{tr("A súgóoldalakat a többi termékre is kiterjesztik.")}},
                        {QStringLiteral("actions"), QVariantList{QVariantMap{
                             {QStringLiteral("text"), tr("Ütemterv a kiterjesztéshez")},
                             {QStringLiteral("owner"), tr("Kovács Lilla")}, {QStringLiteral("due"), tr("okt. 15.")}}}}},
            QVariantMap{{QStringLiteral("title"), tr("Számlázási folyamat")},
                        {QStringLiteral("detail"), tr("A partnerek szerint a gond nem a leírás, hanem maga a "
                                                      "folyamat: túl sok lépés, kevés visszajelzés.")},
                        {QStringLiteral("decisions"), QStringList{}},
                        {QStringLiteral("actions"), QVariantList{}}},
        };
    }
}

} // namespace tanara_qml
