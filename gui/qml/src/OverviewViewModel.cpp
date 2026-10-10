#include "OverviewViewModel.h"

#include "AppContext.h"
#include "JobSupport.h"
#include "ShellMeetingModel.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/jobs/MeetingJobTracker.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/summary/SummaryStore.h"
#include "tanara/tags/TagService.h"

#include <QFileInfo>
#include <QLocale>

#include <algorithm>

namespace tanara_qml {

using namespace tanara;

namespace {

QVariantMap source(const QString& icon, const QString& text, bool negative = false, bool accent = false)
{
    return {{QStringLiteral("icon"), icon}, {QStringLiteral("text"), text},
            {QStringLiteral("negative"), negative}, {QStringLiteral("accent"), accent}};
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

// Egy résztvevő bizonyítékai → jelvények (a design: hang / sáv-oldal / címke / naptár / kézi;
// az ellentmondó bizonyíték szaggatott, halvány).
QVariantList sourcesOf(const Participant& p, bool self)
{
    QVariantList out;
    if (self) out << source(QStringLiteral("user"), OverviewViewModel::tr("te"));
    for (const Evidence& e : p.evidence) {
        const bool neg = e.polarity == Polarity::Contradict;
        switch (e.kind) {
        case EvidenceKind::Manual:
            if (!self) out << source(QStringLiteral("user"), OverviewViewModel::tr("kézi"));
            break;
        case EvidenceKind::Voice:
            out << source(QStringLiteral("fingerprint"), e.text, neg);
            break;
        case EvidenceKind::Side:
            out << source(e.text.contains(QStringLiteral("mikrofon")) ? QStringLiteral("mic")
                                                                       : QStringLiteral("phone"),
                          e.text, neg);
            break;
        case EvidenceKind::Tag: {
            const QString first = e.detail.section(QStringLiteral(", "), 0, 0);
            out << source(QStringLiteral("tag"),
                          first.isEmpty() ? e.text : QStringLiteral("#") + first, neg);
            break;
        }
        case EvidenceKind::Calendar:
            out << source(QStringLiteral("calendar"), e.text.isEmpty() ? OverviewViewModel::tr("naptár") : e.text, neg);
            break;
        default:
            break;
        }
    }
    // Bizonyíték nélkül (régi adat) legalább az oldal látsszon.
    if (p.evidence.isEmpty()) {
        for (const QString& side : p.sides)
            out << (side == QLatin1String("mic") ? source(QStringLiteral("mic"), OverviewViewModel::tr("mikrofon"))
                                                 : source(QStringLiteral("phone"), OverviewViewModel::tr("hívás")));
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
            {QStringLiteral("dimmed"), dimmed}, {QStringLiteral("removable"), removable}};
}

bool sameName(const QString& a, const QString& b)
{
    return !a.isEmpty() && a.compare(b, Qt::CaseInsensitive) == 0;
}

} // namespace

OverviewViewModel::OverviewViewModel(QObject* parent) : QObject(parent)
{
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(0);
    connect(&m_reloadTimer, &QTimer::timeout, this, &OverviewViewModel::reload);
    AppContext* ctx = AppContext::instance();
    connect(ctx, &AppContext::controllerChanged, this, [this] { connectController(); reload(); });
    connect(ctx, &AppContext::demoChanged, this, &OverviewViewModel::reload);
    connectController();
    reload();
}

AppController* OverviewViewModel::app() const { return jobsupport::resolveController(m_injected); }
QObject* OverviewViewModel::controllerObject() const { return app(); }

void OverviewViewModel::setController(QObject* controller)
{
    if (m_injected == controller)
        return;
    m_injected = controller;
    connectController();
    emit controllerChanged();
    reload();
}

void OverviewViewModel::setMeetingId(const QString& id)
{
    if (id == m_meetingId)
        return;
    m_meetingId = id;
    m_hiddenSuggestions.clear();
    m_removedIds.clear();
    emit meetingIdChanged();
    reload();
}

void OverviewViewModel::setDemoState(const QString& state)
{
    if (state == m_demoState)
        return;
    m_demoState = state;
    emit demoStateChanged();
    reload();
}

void OverviewViewModel::scheduleReload()
{
    m_reloadTimer.start();
}

void OverviewViewModel::connectController()
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
    auto mine = [this](const QString& id) {
        if (id == m_meetingId) scheduleReload();
    };
    m_connections << connect(c, &AppController::participantsChanged, this, mine)
                  << connect(c, &AppController::participantAnalysisStarted, this, mine)
                  << connect(c, &AppController::participantAnalysisFinished, this, mine)
                  << connect(c, &AppController::tracksChanged, this, mine)
                  << connect(c, &AppController::speakerMapChanged, this, mine)
                  << connect(c, &AppController::mixdownUpdated, this,
                             [mine](const QString& id, bool) { mine(id); })
                  << connect(c, &AppController::transcriptReady, this,
                             [mine](const QString& id, const QString&) { mine(id); })
                  << connect(c, &AppController::summaryReady, this,
                             [mine](const QString& id, const QString&) { mine(id); })
                  << connect(c, &AppController::summaryStaleChanged, this, mine);
    if (MeetingStore* store = c->store())
        m_connections << connect(store, &MeetingStore::meetingUpdated, this, mine);
    if (MeetingJobTracker* jobs = c->jobs()) {
        m_connections << connect(jobs, &MeetingJobTracker::stateChanged, this, mine)
                      << connect(jobs, &MeetingJobTracker::jobProgressChanged, this,
                                 [mine](const QString& id, const JobProgress&) { mine(id); });
    }
}

QString OverviewViewModel::monogramOf(const QString& name)
{
    const QStringList words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString out;
    for (const QString& w : words) {
        if (w.startsWith(QLatin1Char('('))) continue;   // „Kovács Lilla (te)”
        out += w.left(1).toUpper();
        if (out.size() == 2) break;
    }
    return out.isEmpty() ? QStringLiteral("?") : out;
}

void OverviewViewModel::refresh()
{
    reload();
}

QString OverviewViewModel::addParticipant(const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty())
        return {};
    AppController* c = app();
    if (jobsupport::demoMode(c)) {
        QVariantList rows = m_participants;
        const QString id = QStringLiteral("demo-") + QString::number(rows.size() + 1);
        rows << personRow(id, n, int(rows.size()), QString(),
                          {source(QStringLiteral("user"), tr("kézi"))}, -1.0, false, false, false, true);
        m_participants = rows;
        m_peopleState = QStringLiteral("list");
        emit changed();
        return id;
    }
    m_hiddenSuggestions.insert(n.toLower());
    const QString id = c->addParticipant(m_meetingId, n);
    scheduleReload();
    return id;
}

void OverviewViewModel::removeParticipant(const QString& id)
{
    if (id.startsWith(QLatin1String("suggest:"))) {
        m_hiddenSuggestions.insert(id.mid(8).toLower());
        QVariantList rows;
        for (const QVariant& v : std::as_const(m_participants))
            if (v.toMap().value(QStringLiteral("id")).toString() != id) rows << v;
        m_participants = rows;
        emit changed();
        return;
    }
    AppController* c = app();
    if (jobsupport::demoMode(c)) {
        QVariantList rows;
        for (const QVariant& v : std::as_const(m_participants))
            if (v.toMap().value(QStringLiteral("id")).toString() != id) rows << v;
        m_participants = rows;
        emit changed();
        return;
    }
    // A core nem töröl résztvevőt (csak a kötését és a jelölését veszi el): a sort ebben a
    // munkamenetben rejtjük el. Hiányzó core-API: AppController::removeParticipant.
    m_removedIds.insert(id);
    c->unbindParticipant(m_meetingId, id);
    scheduleReload();
}

bool OverviewViewModel::setSolo()
{
    AppController* c = app();
    if (jobsupport::demoMode(c))
        return true;
    const bool ok = c->setSoloMeeting(m_meetingId);
    scheduleReload();
    return ok;
}

void OverviewViewModel::setContextNote(const QString& note)
{
    AppController* c = app();
    if (jobsupport::demoMode(c)) {
        if (note != m_contextNote) { m_contextNote = note; emit changed(); }
        return;
    }
    c->setMeetingContextNote(m_meetingId, note.trimmed());
    scheduleReload();
}

void OverviewViewModel::reload()
{
    m_reloadTimer.stop();
    AppController* c = app();
    if (jobsupport::demoMode(c)) {
        loadDemo();
        return;
    }
    const Meeting m = (c->store() && !m_meetingId.isEmpty()) ? c->store()->load(m_meetingId) : Meeting();
    const MeetingProcessingState proc = c->processingState(m_meetingId);
    const QString selfName = c->settings() ? c->settings()->settings().userSpeakerName : QString();

    auto job = [&proc](JobKind kind) -> const JobProgress* {
        for (const JobProgress& j : proc.jobs)
            if (j.kind == kind) return &j;
        return nullptr;
    };
    const JobProgress* transcribeJob = job(JobKind::Transcribe);
    const JobProgress* mixdownJob = job(JobKind::Mixdown);
    const JobProgress* summaryJob = job(JobKind::Summarize);

    m_hasTranscript = m.hasTranscript;
    m_transcribing = transcribeJob != nullptr;
    m_contextNote = m.contextNote;
    m_approvalPending = !m.id.isEmpty() && c->participantApprovalPending(m.id);
    m_analysisRunning = !m.id.isEmpty() && c->participantAnalysisRunning(m.id);
    m_approved = m.approval.has_value() && !m.approval->skipped;

    // ---- résztvevők ----
    const QVector<Participant> parts = m.id.isEmpty() ? QVector<Participant>() : c->participants(m.id);
    m_candidateCount = int(std::count_if(parts.cbegin(), parts.cend(), [](const Participant& p) {
        return p.source != ParticipantSource::Manual;
    }));
    SpeakerEditor* ed = m.hasTranscript ? c->speakerEditor(m.id) : nullptr;
    const QVector<EditorSpeaker> speakers = ed ? ed->speakers() : QVector<EditorSpeaker>();

    QVariantList rows;
    QStringList names;
    QVariantList anonymous;
    if (!speakers.isEmpty()) {
        // Átirat után: a beszélők (színnel, aránnyal), a résztvevő bizonyítékaival.
        QVector<EditorSpeaker> sorted = speakers;
        std::sort(sorted.begin(), sorted.end(),
                  [](const EditorSpeaker& a, const EditorSpeaker& b) { return a.colorIndex < b.colorIndex; });
        bool anyNamed = false;
        for (const EditorSpeaker& s : sorted) {
            if (s.utteranceCount == 0 && !s.added) continue;
            const Participant* match = nullptr;
            for (const Participant& p : parts) {
                if ((!s.personName.isEmpty() && sameName(p.personName, s.personName))
                    || (!s.rawLabel.isEmpty() && p.rawSpeakerIds.contains(s.rawLabel))) {
                    match = &p;
                    break;
                }
            }
            const bool self = s.isSelf || sameName(s.personName, selfName);
            if (!s.anonymous) anyNamed = true;
            QString name = s.displayName;
            if (self) name = tr("%1 (te)").arg(name);
            const QVariantList src = match ? sourcesOf(*match, false) : QVariantList();
            rows << personRow(match ? match->id : s.key, name, s.colorIndex,
                              s.anonymous ? tr("névtelen · elnevezhető") : QString(), src,
                              s.talkShare, self, false, false, match != nullptr);
            names << s.personName;
            anonymous << QVariantMap{{QStringLiteral("name"), s.displayName},
                                     {QStringLiteral("share"), s.talkShare},
                                     {QStringLiteral("colorIndex"), s.colorIndex}};
        }
        m_peopleState = rows.isEmpty() ? QStringLiteral("empty")
                      : anyNamed ? QStringLiteral("list") : QStringLiteral("nobody");
    } else {
        int i = 0;
        for (const Participant& p : parts) {
            if (m_removedIds.contains(p.id)) continue;
            const bool self = sameName(p.personName, selfName);
            QString name = p.personName.isEmpty() ? tr("Ismeretlen hang %1").arg(i + 1) : p.personName;
            if (self) name = tr("%1 (te)").arg(name);
            rows << personRow(p.id, name, i, p.personName.isEmpty() ? tr("névtelen · elnevezhető") : QString(),
                              sourcesOf(p, self), p.talkShare > 0 ? p.talkShare : -1.0, self, false,
                              false, true);
            names << p.personName;
            ++i;
        }
        m_peopleState = rows.isEmpty() ? QStringLiteral("empty") : QStringLiteral("list");
    }
    m_anonymous = m_peopleState == QLatin1String("nobody") ? anonymous : QVariantList();

    // Címke alapú javaslat (D): a megbeszélés címkéin tanult személyek, akik még nincsenek itt.
    if (TagService* tags = c->tags(); tags && m_peopleState != QLatin1String("nobody")) {
        int added = 0;
        for (const QString& tagId : m.tagIds) {
            const QVector<PersonTagStat> stats = tags->suggestPeopleForTag(tagId, names, 2);
            for (const PersonTagStat& st : stats) {
                if (added >= 2 || m_hiddenSuggestions.contains(st.name.toLower())) continue;
                if (std::any_of(names.cbegin(), names.cend(), [&](const QString& n) { return sameName(n, st.name); }))
                    continue;
                const QString tagName = tags->tag(tagId).name;
                rows << personRow(QStringLiteral("suggest:") + st.name, st.name, -1, tr("javasolt"),
                                  {source(QStringLiteral("tag"),
                                          tr("a #%1 alapján · %2 / %3").arg(tagName).arg(st.shared).arg(st.tagTotal),
                                          false, true)},
                                  -1.0, false, true, false, true);
                names << st.name;
                ++added;
            }
        }
        if (added > 0 && m_peopleState == QLatin1String("empty"))
            m_peopleState = QStringLiteral("list");
    }
    m_participants = rows;
    if (m.approval && m.approval->solo) {
        m_peopleHint = tr("csak én beszéltem");
    } else if (m_approved && m.approval->at.size() > 0) {
        const QDateTime at = QDateTime::fromString(m.approval->at, Qt::ISODate).toLocalTime();
        m_peopleHint = at.isValid() ? tr("jóváhagyva %1-kor").arg(QLocale().toString(at, QStringLiteral("MMM d. HH:mm")))
                                    : tr("jóváhagyva");
    } else {
        m_peopleHint = m_peopleState == QLatin1String("list") ? tr("naptár · hang · címke · kézi") : QString();
    }

    // Ki beszél az egyes oldalakon (a Sávok sorainak alszövege).
    QStringList micNames, loopNames;
    for (const Participant& p : parts) {
        if (p.sides.contains(QStringLiteral("mic"))) micNames << (p.personName.isEmpty() ? tr("névtelen") : p.personName);
        if (p.sides.contains(QStringLiteral("loopback"))) loopNames << p.personName;
    }
    m_sideWho.clear();
    if (micNames.size() == 1) m_sideWho.insert(QStringLiteral("mic"), micNames.first());
    else if (!micNames.isEmpty()) m_sideWho.insert(QStringLiteral("mic"), tr("%n résztvevő", nullptr, int(micNames.size())));
    if (loopNames.size() == 1 && !loopNames.first().isEmpty()) m_sideWho.insert(QStringLiteral("loopback"), loopNames.first());
    else if (!loopNames.isEmpty()) m_sideWho.insert(QStringLiteral("loopback"), tr("%n résztvevő", nullptr, int(loopNames.size())));

    // ---- feldolgozás ----
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
    steps << step(QStringLiteral("tracks"), tr("Sávok"), tracksSub,
                  measured < m.tracks.size() && !m.hasTranscript ? QStringLiteral("running") : QStringLiteral("done"),
                  false, measured < m.tracks.size() && !m.hasTranscript ? -2 : -1);
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
    if (m_analysisRunning) {
        voiceSub = tr("fut");
        voiceState = QStringLiteral("running");
    } else if (m_approvalPending) {
        voiceSub = tr("%n jelölt · jóváhagyásra vár", nullptr, m_candidateCount);
        voiceState = QStringLiteral("attention");
    } else if (m.approval.has_value() || !parts.isEmpty()) {
        const int known = int(std::count_if(parts.cbegin(), parts.cend(),
                                            [](const Participant& p) { return !p.personName.isEmpty(); }));
        voiceSub = m.approval && m.approval->solo ? tr("csak én beszéltem")
                 : known == 0 ? tr("nincs ismert hang")
                              : tr("%n résztvevő", nullptr, known);
        voiceState = QStringLiteral("done");
    } else {
        voiceSub = m.hasTranscript ? tr("nem futott") : tr("a lekeverés után");
        voiceState = m.hasTranscript ? QStringLiteral("done") : QStringLiteral("waiting");
    }
    steps << step(QStringLiteral("voice"), tr("Hanglenyomat-elemzés"), voiceSub, voiceState, true,
                  m_analysisRunning ? -2 : -1);
    if (transcribeJob) {
        const QVariantMap d = ShellMeetingModel::describeJob(*transcribeJob);
        const int pct = d.value(QStringLiteral("percent")).toInt();
        QStringList sub;
        const QString provider = jobsupport::providerLabel(c, WorkflowStep::Transcribe).section(QStringLiteral(" · "), 0, 0);
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
        summaryAt = QFileInfo(summarystore::markdownPath(m.folder)).lastModified();
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
    m_steps = steps;
    m_allDone = std::all_of(steps.cbegin(), steps.cend(), [](const QVariant& v) {
        return v.toMap().value(QStringLiteral("state")).toString() == QLatin1String("done");
    });
    m_stepsSummary = tr("felvétel · lekeverés · átirat · összefoglaló");

    // ---- ADATOK ----
    QVariantList stats;
    if (m.hasTranscript && ed) {
        qint64 speech = 0;
        int corrected = 0;
        for (const EditorUtterance& u : ed->utterances()) {
            speech += qMax<qint64>(0, u.endMs - u.startMs);
            if (u.manuallyCorrected) ++corrected;
        }
        const int anon = int(std::count_if(speakers.cbegin(), speakers.cend(),
                                           [](const EditorSpeaker& s) { return s.anonymous && s.utteranceCount > 0; }));
        const int active = int(std::count_if(speakers.cbegin(), speakers.cend(),
                                             [](const EditorSpeaker& s) { return s.utteranceCount > 0; }));
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
    m_stats = stats;
    emit changed();
}

void OverviewViewModel::loadDemo()
{
    // Kitalált adat a handoff neveivel (V1 / V3 / V5 / V6).
    const QString st = m_demoState.isEmpty() ? QStringLiteral("overviewProcessing") : m_demoState;
    const bool done = st == QLatin1String("overviewDone");
    const bool empty = st == QLatin1String("overviewEmpty");
    const bool nobody = st == QLatin1String("overviewNobody");

    m_hasTranscript = done || nobody;
    m_transcribing = !m_hasTranscript;
    m_allDone = done;
    m_analysisRunning = empty;
    m_approvalPending = st == QLatin1String("overviewProcessing");
    m_approved = done;
    m_candidateCount = 4;
    // A leírás a demó-állapot első betöltésekor kap értéket (utána a setContextNote-é).
    if (m_demoLoadedFor != st) {
        m_demoLoadedFor = st;
        m_contextNote = (empty || nobody) ? QString()
                      : tr("Q4 partner review — napirend: Q3 számok, súgóoldalak, számlázási folyamat, Nordvik-megújítás.");
    }

    QVariantList rows;
    if (done) {
        rows << personRow(QStringLiteral("p1"), tr("Kovács Lilla (te)"), 0, {}, {source(QStringLiteral("mic"), tr("mikrofon"))}, 0.22, true)
             << personRow(QStringLiteral("p2"), QStringLiteral("Fehér Gábor"), 1, {}, {source(QStringLiteral("phone"), tr("hívás")), source(QStringLiteral("tag"), QStringLiteral("#Nordvik"))}, 0.26)
             << personRow(QStringLiteral("p3"), QStringLiteral("Varga Árpád"), 2, {}, {source(QStringLiteral("phone"), tr("hívás")), source(QStringLiteral("tag"), QStringLiteral("#Nordvik"))}, 0.19)
             << personRow(QStringLiteral("p4"), QStringLiteral("Molnár Eszter"), 3, {}, {source(QStringLiteral("phone"), tr("hívás"))}, 0.15)
             << personRow(QStringLiteral("p5"), QStringLiteral("Távoli 1"), 4, tr("névtelen · elnevezhető"), {source(QStringLiteral("phone"), tr("hívás"))}, 0.07);
        m_peopleState = QStringLiteral("list");
        m_peopleHint = tr("jóváhagyva %1-kor").arg(QStringLiteral("okt. 1. 16:40"));
    } else if (empty) {
        m_peopleState = QStringLiteral("empty");
        m_peopleHint.clear();
    } else if (nobody) {
        m_peopleState = QStringLiteral("nobody");
        m_peopleHint.clear();
    } else {
        rows << personRow(QStringLiteral("p1"), tr("Kovács Lilla (te)"), 0, {}, {source(QStringLiteral("user"), tr("te")), source(QStringLiteral("mic"), tr("mikrofon"))}, -1.0, true)
             << personRow(QStringLiteral("p2"), QStringLiteral("Fehér Gábor"), 1, {}, {source(QStringLiteral("fingerprint"), tr("hang 84%")), source(QStringLiteral("tag"), QStringLiteral("#Nordvik"))}, -1.0, false, false, false, true)
             << personRow(QStringLiteral("p3"), QStringLiteral("Varga Árpád"), 2, {}, {source(QStringLiteral("fingerprint"), tr("hang 79%")), source(QStringLiteral("tag"), QStringLiteral("#Nordvik"))}, -1.0, false, false, false, true)
             << personRow(QStringLiteral("p4"), QStringLiteral("Molnár Eszter"), 3, {}, {source(QStringLiteral("fingerprint"), tr("hang 71%"))}, -1.0, false, false, false, true)
             << personRow(QStringLiteral("p5"), QStringLiteral("Lantos Réka"), 4, {}, {source(QStringLiteral("fingerprint"), tr("nem hallottuk"), true)}, -1.0, false, false, true, true)
             << personRow(QStringLiteral("suggest:Szabó Bence"), QStringLiteral("Szabó Bence"), -1, tr("javasolt"),
                          {source(QStringLiteral("tag"), tr("a #%1 alapján · %2 / %3").arg(QStringLiteral("Nordvik")).arg(11).arg(14), false, true)},
                          -1.0, false, true, false, true);
        m_peopleState = QStringLiteral("list");
        m_peopleHint = tr("naptár · hang · címke · kézi");
    }
    QVariantList filtered;
    for (const QVariant& v : std::as_const(rows))
        if (!m_hiddenSuggestions.contains(v.toMap().value(QStringLiteral("name")).toString().toLower())) filtered << v;
    m_participants = filtered;
    m_anonymous.clear();
    if (nobody) {
        const QList<QPair<QString, double>> anon = {{tr("Beszélő 1"), 0.46}, {tr("Beszélő 2"), 0.38}, {tr("Beszélő 3"), 0.16}};
        int i = 0;
        for (const auto& [name, share] : anon)
            m_anonymous << QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("share"), share},
                                       {QStringLiteral("colorIndex"), i++}};
    }
    m_sideWho = (empty || nobody) ? QVariantMap()
              : QVariantMap{{QStringLiteral("mic"), QStringLiteral("Kovács Lilla")},
                            {QStringLiteral("loopback"), tr("%n résztvevő", nullptr, 4)}};

    QVariantList steps;
    if (empty || nobody) {
        steps << step(QStringLiteral("recording"), tr("Felvétel"), QStringLiteral("0:42:10 · ") + tr("%n sáv", nullptr, 2), QStringLiteral("done"))
              << step(QStringLiteral("tracks"), tr("Sávok"), tr("%n aktív", nullptr, 2), QStringLiteral("done"))
              << step(QStringLiteral("mixdown"), tr("Lekeverés"), tr("%n sávból", nullptr, 2), QStringLiteral("done"));
        if (empty)
            steps << step(QStringLiteral("voice"), tr("Hanglenyomat-elemzés"), QStringLiteral("1 / 2 ") + tr("sáv"), QStringLiteral("running"), true, 40)
                  << step(QStringLiteral("transcript"), tr("Átirat"), QStringLiteral("Soniox · 18% · ") + tr("kb. %n perc", nullptr, 6), QStringLiteral("running"), true, 18)
                  << step(QStringLiteral("summary"), tr("Összefoglaló"), tr("az átirat után"), QStringLiteral("waiting"));
        else
            steps << step(QStringLiteral("voice"), tr("Hanglenyomat-elemzés"), tr("nincs ismert hang"), QStringLiteral("done"), true)
                  << step(QStringLiteral("transcript"), tr("Átirat"), tr("%n megszólalás", nullptr, 412) + QStringLiteral(" · ") + tr("%n beszélő", nullptr, 3), QStringLiteral("done"), true)
                  << step(QStringLiteral("summary"), tr("Összefoglaló"), tr("még nem készült"), QStringLiteral("waiting"));
    } else {
        steps << step(QStringLiteral("recording"), tr("Felvétel"), QStringLiteral("2:11:04 · ") + tr("%n sáv", nullptr, 3), QStringLiteral("done"))
              << step(QStringLiteral("tracks"), tr("Sávok"), tr("%n aktív", nullptr, 2) + QStringLiteral(" · ") + tr("%n kimaradt, nincs rajta beszéd", nullptr, 1), QStringLiteral("done"))
              << step(QStringLiteral("mixdown"), tr("Lekeverés"), tr("%n sávból", nullptr, 2), QStringLiteral("done"));
        if (done)
            steps << step(QStringLiteral("voice"), tr("Hanglenyomat-elemzés"), tr("%n résztvevő", nullptr, 4), QStringLiteral("done"), true)
                  << step(QStringLiteral("transcript"), tr("Átirat"), tr("%n megszólalás", nullptr, 1298) + QStringLiteral(" · ") + tr("%n beszélő", nullptr, 5), QStringLiteral("done"), true)
                  << step(QStringLiteral("summary"), tr("Összefoglaló"), tr("kész"), QStringLiteral("done"));
        else
            steps << step(QStringLiteral("voice"), tr("Hanglenyomat-elemzés"), tr("%n jelölt · jóváhagyásra vár", nullptr, 4), QStringLiteral("attention"), true)
                  << step(QStringLiteral("transcript"), tr("Átirat"), QStringLiteral("Soniox · 62% · ") + tr("kb. %n perc", nullptr, 4), QStringLiteral("running"), true, 62)
                  << step(QStringLiteral("summary"), tr("Összefoglaló"), tr("az átirat után"), QStringLiteral("waiting"));
    }
    m_steps = steps;
    m_stepsSummary = tr("felvétel · lekeverés · átirat · összefoglaló");
    m_stats.clear();
    if (done)
        m_stats << stat(tr("HOSSZ"), QStringLiteral("2:11:04"), tr("felvétel"))
                << stat(tr("BESZÉD"), QStringLiteral("1:52"), tr("%n sávon", nullptr, 2))
                << stat(tr("BESZÉLŐK"), QStringLiteral("5"), tr("%n névtelen", nullptr, 1))
                << stat(tr("MEGSZÓLALÁS"), QStringLiteral("1298"), tr("%n javítva", nullptr, 23))
                << stat(tr("ÁTNÉZENDŐ"), QStringLiteral("84"), tr("az Átirat fülön"))
                << stat(tr("ÖSSZEFOGLALÓ"), tr("kész"), QStringLiteral("okt. 1. 17:05"));
    emit changed();
}

} // namespace tanara_qml
