#include "PreTranscriptViewModel.h"
#include "tanara/providers/core/ProviderConfig.h"

#include "DemoText.h"

#include "AppContext.h"
#include "JobSupport.h"
#include "TagMatching.h"

#include "tanara/app/AppController.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/MeetingStore.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>

namespace tanara_qml {

using namespace tanara;

namespace {

QVariantMap stageMap(const QString& id, const QString& label, const QString& state,
                     int percent = -1, const QString& detail = QString())
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("label"), label},
            {QStringLiteral("state"), state}, {QStringLiteral("percent"), percent},
            {QStringLiteral("detail"), detail}};
}

} // namespace

PreTranscriptViewModel::PreTranscriptViewModel(QObject* parent)
    : QObject(parent), m_note(new MeetingNoteModel(this)), m_tags(new MeetingTagsModel(this))
{
    // The tag suggestion row follows the field's tags, the regular suggestions and undo.
    connect(m_tags, &MeetingTagsModel::tagsChanged, this, &PreTranscriptViewModel::reloadTagSuggestions);
    connect(m_tags, &MeetingTagsModel::suggestionsChanged, this, &PreTranscriptViewModel::reloadTagSuggestions);
    connect(m_tags, &MeetingTagsModel::undoChanged, this, &PreTranscriptViewModel::reloadTagSuggestions);
    connect(m_tags, &MeetingTagsModel::toast, this, &PreTranscriptViewModel::toast);
    connect(m_note, &MeetingNoteModel::noteChanged, this, [this]() {
        emit contextNoteChanged();
        emit changed();   // the run view's footer quotes from this too
    });
    // The remaining time ticks down by the wall clock: recomputed periodically while running.
    m_etaTimer.setInterval(10000);
    connect(&m_etaTimer, &QTimer::timeout, this, &PreTranscriptViewModel::jobChanged);

    AppContext* ctx = AppContext::instance();
    connect(ctx, &AppContext::controllerChanged, this, [this]() { connectController(); reload(); });
    connect(ctx, &AppContext::demoChanged, this, &PreTranscriptViewModel::reload);
    connectController();
    reload();
}

app::AppController* PreTranscriptViewModel::app() const
{
    return jobsupport::resolveController(m_injected);
}

QObject* PreTranscriptViewModel::controllerObject() const { return app(); }

void PreTranscriptViewModel::setController(QObject* controller)
{
    if (m_injected == controller)
        return;
    m_injected = controller;
    m_note->setController(controller);
    m_tags->setController(controller);
    connectController();
    emit controllerChanged();
    reload();
}

bool PreTranscriptViewModel::demo() const { return jobsupport::demoMode(app()); }

void PreTranscriptViewModel::setMeetingId(const QString& id)
{
    if (id == m_meetingId)
        return;
    // The not-yet-saved note belongs to the OLD meeting: it is written there before the switch.
    m_note->setMeetingId(id);
    m_meetingId = id;
    // In the demo an empty id -> the fictional demo meeting's tags.
    m_tags->setMeetingId(jobsupport::demoMode(app()) ? QString() : id);
    reload();                 // new data first, so the signal already shows the new note
    emit meetingIdChanged();
}

void PreTranscriptViewModel::setDemoState(const QString& state)
{
    if (state == m_demoState)
        return;
    m_demoState = state;
    emit demoStateChanged();
    reload();
}

void PreTranscriptViewModel::connectController()
{
    app::AppController* c = app();
    if (c == m_connected)
        return;
    for (const QMetaObject::Connection& conn : std::as_const(m_connections))
        disconnect(conn);
    m_connections.clear();
    m_connected = c;
    if (!c)
        return;

    auto mine = [this](const QString& id) { return !m_meetingId.isEmpty() && id == m_meetingId; };
    m_connections << connect(c->jobs(), &library::MeetingJobTracker::stateChanged, this,
                             [this, mine](const QString& id) { if (mine(id)) reload(); });
    // Progress: only the phase list refreshes (no disk read).
    m_connections << connect(c->jobs(), &library::MeetingJobTracker::jobProgressChanged, this,
                             [this, mine](const QString& id, const JobProgress& p) {
        if (!mine(id) || p.kind != JobKind::Transcribe || m_state != QLatin1String("running"))
            return;
        applyJob(p);
        emit jobChanged();
    });
    m_connections << connect(c, &app::AppController::mixdownProgress, this,
                             [this, mine](const QString& id, int pct) {
        if (!mine(id) || pct == m_mixdownPercent) return;
        m_mixdownPercent = pct;
        emit mixdownPercentChanged();
    });
    m_connections << connect(c, &app::AppController::mixdownUpdated, this,
                             [this, mine](const QString& id, bool) { if (mine(id)) reload(); });
    m_connections << connect(c, &app::AppController::tracksChanged, this,
                             [this, mine](const QString& id) { if (mine(id)) reload(); });
    m_connections << connect(c->store(), &library::MeetingStore::meetingUpdated, this,
                             [this, mine](const QString& id) { if (mine(id)) reload(); });
    m_connections << connect(c->settings(), &app::SettingsManager::settingsChanged, this,
                             &PreTranscriptViewModel::reload);
}

void PreTranscriptViewModel::refresh() { reload(); }

void PreTranscriptViewModel::reload()
{
    const int oldPct = m_mixdownPercent;

    m_blocker.clear();
    m_canStart = false;
    m_cloudSoon = false;
    m_errorMessage.clear();
    m_errorDetail.clear();
    m_fixActionLabel.clear();
    m_fixActionPage.clear();
    m_stages.clear();
    m_jobTitle.clear();
    m_demoEta.clear();
    m_job = JobProgress{};
    m_cancellable = true;
    m_cancelling = false;
    m_mixdownCancellable = false;

    app::AppController* c = app();
    m_title.clear();
    if (jobsupport::demoMode(c)) {
        loadDemo();
    } else if (m_meetingId.isEmpty()) {
        m_state = QStringLiteral("none");
        m_note->reload();
    } else {
        const Meeting m = c->store()->load(m_meetingId);
        m_title = m.title;
        const MeetingProcessingState ps = c->jobs()->state(m);
        const JobProgress job = c->jobs()->job(m_meetingId, JobKind::Transcribe);

        m_note->reload();
        m_providerLabel = jobsupport::providerLabel(c, providers::WorkflowStep::Transcribe);
        m_identifyAvailable = c->voiceIdentificationAvailable();
        m_identifyEnabled = m_identifyAvailable && c->identifyAfterTranscription(m_meetingId);

        // Mixdown: running / done / stale / missing.
        const QString mixPath = QDir(m.folder).filePath(
            m.mixdownFile.isEmpty() ? QStringLiteral("mixdown.mp3") : m.mixdownFile);
        if (ps.mixdownRunning) {
            m_mixdownState = QStringLiteral("running");
            m_mixdownPercent = ps.mixdownPercent;
            m_mixdownCancellable = c->jobs()->isRunning(m_meetingId, JobKind::Mixdown);
        } else {
            m_mixdownPercent = -1;
            if (m.mixdownFile.isEmpty() || !QFileInfo::exists(mixPath))
                m_mixdownState = QStringLiteral("missing");
            else
                m_mixdownState = m.mixdownDirty ? QStringLiteral("stale") : QStringLiteral("ready");
        }

        // Gating: from the same source of truth as the AppController's guards.
        const providers::ReadinessResult r = c->canRun(providers::WorkflowStep::Transcribe, m_meetingId);
        m_canStart = r.runnable;
        m_blocker = jobsupport::blockerInfo(c, providers::WorkflowStep::Transcribe, r);
        m_cloudSoon = !r.runnable && r.blockerKind == providers::BlockerKind::ProviderConfig;

        if (job.isValid()) {
            m_state = QStringLiteral("running");
            applyJob(job);
        } else if (ps.transcriptState == StepState::Failed && ps.transcriptError.isValid()) {
            m_state = QStringLiteral("failed");
            m_errorMessage = ps.transcriptError.message;
            m_errorDetail = ps.transcriptError.detail;
            if (ps.transcriptError.when.isValid()) {
                const QString when = QLocale().toString(ps.transcriptError.when, tr("MMM d. HH:mm"));
                m_errorDetail = m_errorDetail.isEmpty() ? when
                                                        : m_errorDetail + QStringLiteral(" · ") + when;
            }
            const jobsupport::FixAction fix = jobsupport::fixActionForError(ps.transcriptError);
            m_fixActionLabel = fix.label;
            m_fixActionPage = fix.page;
        } else {
            m_state = m.id.isEmpty() ? QStringLiteral("none") : QStringLiteral("steps");
        }
    }

    if (m_state == QLatin1String("running") && m_job.isValid())
        m_etaTimer.start();
    else
        m_etaTimer.stop();

    if (m_mixdownPercent != oldPct)
        emit mixdownPercentChanged();
    emit jobChanged();
    emit changed();
    reloadTagSuggestions();
}

void PreTranscriptViewModel::reloadTagSuggestions()
{
    TagBackend* b = m_tags->backend();
    QVector<TagSuggestionItem> list = m_tags->visibleSuggestions();
    m_tagSuggestionsFromModel = !list.isEmpty();
    if (list.isEmpty() && b && (m_state == QLatin1String("steps") || jobsupport::demoMode(app()))) {
        const QString meeting = m_tags->effectiveMeetingId();
        const QStringList applied = m_tags->tagIds();
        QStringList appliedKeys;
        for (const QString& id : applied) appliedKeys << tagmatch::key(m_tags->tagName(id));
        for (const TagSuggestionItem& s : b->draftSuggestions(m_title)) {
            if ((!s.tagId.isEmpty() && applied.contains(s.tagId)) || appliedKeys.contains(tagmatch::key(s.name)))
                continue;
            if (b->isRejected(meeting, s.tagId.isEmpty() ? s.name : s.tagId))
                continue;
            list << s;
        }
    }
    if (list.size() > 3) list.resize(3);
    m_tagSuggestions = list;
    emit tagSuggestionsChanged();
}

QVariantList PreTranscriptViewModel::tagSuggestions() const
{
    QVariantList out;
    for (int i = 0; i < m_tagSuggestions.size(); ++i) {
        const TagSuggestionItem& s = m_tagSuggestions[i];
        out << QVariantMap{{QStringLiteral("index"), i}, {QStringLiteral("id"), s.tagId},
                           {QStringLiteral("name"), s.name}, {QStringLiteral("isNew"), s.isNew},
                           {QStringLiteral("source"), s.source}};
    }
    return out;
}

QString PreTranscriptViewModel::tagSuggestionReason() const
{
    if (m_tagSuggestions.isEmpty()) return {};
    const TagSuggestionItem& s = m_tagSuggestions.first();
    auto find = [&s](const char* kind) -> const TagReasonItem* {
        for (const TagReasonItem& r : s.reasons)
            if (r.kind == QLatin1String(kind) && !r.values.isEmpty()) return &r;
        return nullptr;
    };
    if (const TagReasonItem* r = find("title")) {
        const QString title = s.similarMeetings.isEmpty() ? r->values.first() : s.similarMeetings.first().title;
        return tr("hasonló cím: „%1”").arg(title);
    }
    if (const TagReasonItem* r = find("participant"))
        return tr("közös résztvevő: %1").arg(r->values.mid(0, 2).join(QStringLiteral(", ")));
    if (const TagReasonItem* r = find("terms"))
        return tr("közös kifejezések: %1").arg(r->values.mid(0, 3).join(QStringLiteral(", ")));
    return {};
}

void PreTranscriptViewModel::acceptTagSuggestion(int index)
{
    TagBackend* b = m_tags->backend();
    if (!b || index < 0 || index >= m_tagSuggestions.size()) return;
    if (m_tagSuggestionsFromModel) {   // the regular suggestions: the model asks (with a toast)
        m_tags->accept(index);
        return;
    }
    const TagSuggestionItem s = m_tagSuggestions[index];
    b->beginGroup(tr("Javaslat elfogadva: #%1").arg(s.name));
    b->addTag(m_tags->effectiveMeetingId(), s.isNew || s.tagId.isEmpty() ? s.name : s.tagId,
              TagAddSource::Suggestion);
    b->endGroup();
    reloadTagSuggestions();
    emit toast(b->undoLabel(), b->canUndo());
}

void PreTranscriptViewModel::rejectTagSuggestion(int index)
{
    TagBackend* b = m_tags->backend();
    if (!b || index < 0 || index >= m_tagSuggestions.size()) return;
    if (m_tagSuggestionsFromModel) {
        m_tags->reject(index);
        return;
    }
    const TagSuggestionItem s = m_tagSuggestions[index];
    b->beginGroup(tr("Javaslat elutasítva: #%1").arg(s.name));
    b->reject(m_tags->effectiveMeetingId(), s);
    b->endGroup();
    reloadTagSuggestions();
    emit toast(b->undoLabel(), b->canUndo());
}

void PreTranscriptViewModel::applyJob(const JobProgress& job)
{
    m_job = job;
    m_jobTitle = job.title.isEmpty() ? tr("Átírás folyamatban") : job.title;
    m_cancellable = job.cancellable;
    m_cancelling = job.cancelling;
    QVariantList list;
    for (const JobStage& s : job.stages)
        list << stageMap(s.id, s.label, jobsupport::stageStateName(s.state), s.percent, s.detail);
    m_stages = list;
}

// Fictional sample data (matching the design's M03-M05 screens).
void PreTranscriptViewModel::loadDemo()
{
    const QString st = m_demoState.isEmpty() ? QStringLiteral("steps") : m_demoState;
    // One attached tag, the suggestion comes from the title (fictional pool).
    m_tags->setDemoState(QStringLiteral("draft"));
    const QString note = demoText("Ügyféltámogatás átadása az új csapatnak. Érintett rendszerek: jegykezelő, "
                            "súgóoldalak, számlázás. Résztvevők: Molnár Eszter, Tóth Bence.", "Support handover to the new team. Systems involved: ticket system, help pages, billing. Participants: Sophie Bennett, Ryan Mitchell.");
    if (st == QLatin1String("note") || st == QLatin1String("readyNote")) {
        // Template suggestions from earlier meetings with a similar title + the detected call.
        m_note->setDemoContent(QString(), QStringLiteral("Microsoft Teams"), MeetingNoteModel::demoSuggestions());
    } else {
        m_note->setDemoContent(note, QString(), {});
    }
    m_identifyAvailable = true;
    m_mixdownState = QStringLiteral("ready");
    m_mixdownPercent = -1;
    m_providerLabel = tr("%1 · saját kulcs").arg(QStringLiteral("Soniox"));
    m_state = QStringLiteral("steps");

    if (st == QLatin1String("running") || st == QLatin1String("uploading")) {
        m_state = QStringLiteral("running");
        m_jobTitle = tr("Átírás folyamatban");
        const bool up = st == QLatin1String("uploading");
        m_demoEta = up ? QString() : jobsupport::formatEta(240);
        m_stages = {
            stageMap(QStringLiteral("mixdown"), tr("Lekeverés"), QStringLiteral("done")),
            stageMap(QStringLiteral("upload"), tr("Feltöltés"),
                     up ? QStringLiteral("running") : QStringLiteral("done"), up ? 62 : -1,
                     tr("%1 perc, %2 sáv").arg(52).arg(3)),
            stageMap(QStringLiteral("transcribe"), tr("Átírás"),
                     up ? QStringLiteral("waiting") : QStringLiteral("running")),
            stageMap(QStringLiteral("diarize"), tr("Beszélők szétválasztása"),
                     up ? QStringLiteral("waiting") : QStringLiteral("running")),
            stageMap(QStringLiteral("identify"), tr("Résztvevők azonosítása"), QStringLiteral("waiting")),
        };
        if (!up)
            m_stages.removeFirst();   // the sketch has no mixdown phase (the mix already existed)
    } else if (st == QLatin1String("failed")) {
        m_state = QStringLiteral("failed");
        m_errorMessage = tr("A szolgáltató nem fogadta el az API-kulcsot. Ellenőrizd vagy cseréld "
                            "a kulcsot a Beállításokban.");
        m_errorDetail = QStringLiteral("HTTP 401 · invalid_api_key · ") + tr("szept. 30. 16:42");
        m_fixActionLabel = tr("Kulcs módosítása");
        m_fixActionPage = QStringLiteral("providers");
    } else if (st == QLatin1String("ready") || st == QLatin1String("readyNote")) {
        m_canStart = true;
    } else if (st == QLatin1String("mixing")) {
        m_canStart = true;
        m_mixdownState = QStringLiteral("running");
        m_mixdownPercent = 64;
        m_mixdownCancellable = true;
    } else {
        m_blocker = {
            {QStringLiteral("title"), tr("Nincs beállítva átíró szolgáltató")},
            {QStringLiteral("text"), tr("Hiányzik: API-kulcs (Soniox). Add meg a saját szolgáltatód "
                                        "adatait a Beállításokban.")},
            {QStringLiteral("actionLabel"), tr("Szolgáltató beállítása")},
            {QStringLiteral("actionPage"), QStringLiteral("providers")},
            {QStringLiteral("reason"), tr("A szolgáltató beállítása után indítható.")},
            {QStringLiteral("kind"), int(providers::BlockerKind::ProviderConfig)},
        };
        m_cloudSoon = true;
    }
}

void PreTranscriptViewModel::setContextNote(const QString& note)
{
    m_note->setNote(note);   // → noteChanged → contextNoteChanged + changed
}

void PreTranscriptViewModel::setIdentifyEnabled(bool enabled)
{
    if (enabled == m_identifyEnabled || !m_identifyAvailable)
        return;
    m_identifyEnabled = enabled;
    app::AppController* c = app();
    if (!jobsupport::demoMode(c) && !m_meetingId.isEmpty())
        c->setIdentifyAfterTranscription(m_meetingId, enabled);
    emit changed();
}

QString PreTranscriptViewModel::etaText() const
{
    if (!m_demoEta.isEmpty())
        return m_demoEta;
    return jobsupport::formatEta(m_job.etaSeconds());
}

QString PreTranscriptViewModel::footerLine() const
{
    QStringList parts;
    QString note = m_note->note().simplified();
    if (!note.isEmpty()) {
        if (note.size() > 42)
            note = note.left(40).trimmed() + QStringLiteral("…");
        parts << tr("Kontextus: „%1”").arg(note);
    }
    if (!m_providerLabel.isEmpty())
        parts << tr("Szolgáltató: %1").arg(m_providerLabel);
    return parts.join(QStringLiteral(" · "));
}

void PreTranscriptViewModel::startMixdown()
{
    app::AppController* c = app();
    if (jobsupport::demoMode(c) || m_meetingId.isEmpty())
        return;
    c->regenerateMixdown(m_meetingId);
}

void PreTranscriptViewModel::clearError()
{
    app::AppController* c = app();
    if (jobsupport::demoMode(c) || m_meetingId.isEmpty())
        return;
    c->jobs()->clearError(m_meetingId, JobKind::Transcribe);   // → stateChanged → reload()
}

} // namespace tanara_qml
