#include "ShellMeetingModel.h"

#include "AppContext.h"
#include "LibraryDemoData.h"
#include "JobSupport.h"
#include "ShellFormat.h"
#include "SummaryProgress.h"

#include "tanara/AppController.h"
#include "tanara/jobs/MeetingJobTracker.h"
#include "tanara/store/MeetingStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace tanara_qml {

using tanara::JobKind;

namespace {

// A (nyers) beszélő-címkék száma az átiratban; 0, ha nincs / nem olvasható.
int speakerCount(const tanara::Meeting& m)
{
    // Egy elemű gyorsítótár: a feladat-haladás jelei sűrűn újratöltik a fejlécet, az átirat
    // viszont közben nem változik (útvonal + módosítási idő + méret azonosítja).
    static QString cachedKey;
    static int cachedCount = 0;
    const QFileInfo fi(QDir(m.folder).filePath(QStringLiteral("transcript.segments.json")));
    if (!fi.exists())
        return 0;
    const QString key = fi.absoluteFilePath() + QLatin1Char('|')
                        + QString::number(fi.lastModified().toMSecsSinceEpoch())
                        + QLatin1Char('|') + QString::number(fi.size());
    if (key == cachedKey)
        return cachedCount;
    QFile f(fi.absoluteFilePath());
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonArray segments = doc.isArray() ? doc.array()
                                              : doc.object().value(QStringLiteral("segments")).toArray();
    QSet<QString> speakers;
    for (const QJsonValue& v : segments) {
        const QString s = v.toObject().value(QStringLiteral("speaker")).toString();
        if (!s.isEmpty())
            speakers.insert(s);
    }
    cachedKey = key;
    cachedCount = int(speakers.size());
    return cachedCount;
}

QString metaLine(const QDateTime& startedAt, qint64 durationMs, const QString& tail)
{
    QStringList parts;
    const QString date = fmt::longDate(startedAt);
    if (!date.isEmpty()) parts << date;
    if (durationMs > 0) parts << fmt::clock(durationMs);
    if (!tail.isEmpty()) parts << tail;
    return parts.join(QStringLiteral(" · "));
}

QString fallbackTitle(JobKind kind)
{
    switch (kind) {
    case JobKind::Transcribe:    return ShellMeetingModel::tr("Átírás");
    case JobKind::Summarize:     return ShellMeetingModel::tr("Összefoglaló készítése");
    case JobKind::ExtractTopics: return ShellMeetingModel::tr("Témák javaslása");
    case JobKind::AnalyzeTopics: return ShellMeetingModel::tr("Témánkénti elemzés");
    case JobKind::Mixdown:       return ShellMeetingModel::tr("Lekeverés");
    case JobKind::Identify:      return ShellMeetingModel::tr("Résztvevők azonosítása");
    case JobKind::Import:        return ShellMeetingModel::tr("Importálás");
    case JobKind::Export:        return ShellMeetingModel::tr("Exportálás archívumba");
    }
    return {};
}

QString iconFor(JobKind kind)
{
    switch (kind) {
    case JobKind::Transcribe:    return QStringLiteral("file-text");
    case JobKind::Summarize:     return QStringLiteral("sparkles");
    case JobKind::ExtractTopics:
    case JobKind::AnalyzeTopics: return QStringLiteral("list-tree");
    case JobKind::Mixdown:       return QStringLiteral("audio-lines");
    case JobKind::Identify:      return QStringLiteral("fingerprint");
    case JobKind::Import:        return QStringLiteral("file-audio");
    case JobKind::Export:        return QStringLiteral("arrow-down-to-line");
    }
    return QStringLiteral("loader-circle");
}

} // namespace

ShellMeetingModel::ShellMeetingModel(QObject* parent)
    : QObject(parent), m_tags(new MeetingTagsModel(this))
{
    AppContext* ctx = AppContext::instance();
    m_controller = ctx->controller();
    connect(ctx, &AppContext::controllerChanged, this, [this, ctx] {
        if (m_controllerInjected) return;
        m_controller = ctx->controller();
        attach();
    });
    attach();
}

void ShellMeetingModel::setController(tanara::AppController* controller)
{
    m_controllerInjected = true;
    m_controller = controller;
    attach();
}

void ShellMeetingModel::attach()
{
    if (m_controller != m_attached) {
        if (m_attached) {
            m_attached->disconnect(this);
            if (m_attached->store()) m_attached->store()->disconnect(this);
            if (m_attached->jobs()) m_attached->jobs()->disconnect(this);
        }
        m_attached = m_controller;
        if (tanara::AppController* c = m_controller) {
            if (tanara::MeetingStore* store = c->store()) {
                connect(store, &tanara::MeetingStore::meetingUpdated, this,
                        &ShellMeetingModel::onMeetingTouched);
                connect(store, &tanara::MeetingStore::meetingRemoved, this,
                        &ShellMeetingModel::onMeetingTouched);
            }
            connect(c, &tanara::AppController::speakerMapChanged, this,
                    &ShellMeetingModel::onMeetingTouched);
            connect(c, &tanara::AppController::summaryStaleChanged, this,
                    &ShellMeetingModel::onMeetingTouched);
            connect(c, &tanara::AppController::tracksChanged, this,
                    &ShellMeetingModel::onMeetingTouched);
            connect(c, &tanara::AppController::transcriptReady, this,
                    [this](const QString& id, const QString&) {
                // Új átirat → új javaslatok (a controller előbb eldobja a régi profilt).
                if (id == m_meetingId) m_tagsRequestedFor.clear();
                onMeetingTouched(id);
            });
            connect(c, &tanara::AppController::summaryReady, this,
                    [this](const QString& id, const QString&) { onMeetingTouched(id); });
            if (tanara::MeetingJobTracker* jobs = c->jobs()) {
                connect(jobs, &tanara::MeetingJobTracker::stateChanged, this,
                        [this](const QString& id) {
                    if (id != m_meetingId) return;
                    // Az állapot (pl. elavult-jelző) és a futó feladatok is változhattak.
                    reload();
                });
                connect(jobs, &tanara::MeetingJobTracker::jobProgressChanged, this,
                        [this](const QString& id, const tanara::JobProgress&) {
                    if (id == m_meetingId) reloadTasks();
                });
            }
        }
        // A címkesor a valódi készlettel dolgozik (controller nélkül a kitalált marad).
        if (m_controller) m_tags->setController(m_controller.data());
        m_tagsRequestedFor.clear();
    }
    reload();
}

void ShellMeetingModel::syncTags()
{
    // Demóban (controller nélkül) üres azonosító → a kitalált demó-megbeszélés címkéi.
    m_tags->setMeetingId(m_controller ? m_meetingId : QString());
    if (!m_controller || !m_exists || !m_hasTranscript || m_tagsRequestedFor == m_meetingId)
        return;
    m_tagsRequestedFor = m_meetingId;
    m_tags->requestSuggestions();
}

void ShellMeetingModel::setMeetingId(const QString& id)
{
    if (id == m_meetingId)
        return;
    m_meetingId = id;
    emit meetingIdChanged();
    reload();
}

void ShellMeetingModel::setDemoTask(bool on)
{
    if (on == m_demoTask)
        return;
    m_demoTask = on;
    reloadTasks();
}

void ShellMeetingModel::onMeetingTouched(const QString& id)
{
    if (id == m_meetingId)
        reload();
}

void ShellMeetingModel::reload()
{
    bool exists = false, hasTranscript = false, stale = false, canIdentify = false, hasSummary = false;
    QString title, meta, provider;

    if (!m_meetingId.isEmpty() && m_controller && m_controller->store()) {
        const tanara::Meeting m = m_controller->store()->load(m_meetingId);
        if (!m.id.isEmpty()) {
            exists = true;
            title = m.title;
            hasTranscript = m.hasTranscript;
            int activeTracks = 0;
            for (const tanara::Track& t : m.tracks)
                if (t.active) ++activeTracks;
            canIdentify = activeTracks > 0;
            QString tail;
            if (hasTranscript) {
                const int n = speakerCount(m);
                if (n > 0) tail = tr("%n beszélő", nullptr, n);
            } else if (!m.tracks.isEmpty()) {
                tail = tr("%n sáv", nullptr, int(m.tracks.size()));
            }
            meta = metaLine(m.startedAt, m.durationMs, tail);
            stale = m.hasSummary && m_controller->summaryStale(m).stale;
            hasSummary = m.hasSummary;
            provider = jobsupport::providerLabel(m_controller, tanara::WorkflowStep::Summarize);
        }
    } else if (!m_meetingId.isEmpty() && !m_controller && AppContext::instance()->demo()) {
        if (const demo::DemoMeeting* d = demo::find(m_meetingId)) {
            exists = true;
            title = d->entry.title;
            hasTranscript = d->entry.hasTranscript;
            canIdentify = true;
            stale = d->entry.state.summaryStale;
            hasSummary = d->entry.hasSummary;
            provider = tr("LM Studio · saját kulcs");
            meta = metaLine(d->entry.startedAt, d->entry.durationMs,
                            hasTranscript ? tr("%n beszélő", nullptr, d->speakers)
                                          : tr("%n sáv", nullptr, d->tracks));
        }
    }

    if (exists != m_exists || title != m_title || meta != m_meta
        || hasTranscript != m_hasTranscript || stale != m_summaryStale
        || canIdentify != m_canIdentify || hasSummary != m_hasSummary || provider != m_summaryProvider) {
        m_exists = exists;
        m_title = title;
        m_meta = meta;
        m_hasTranscript = hasTranscript;
        m_summaryStale = stale;
        m_canIdentify = canIdentify;
        m_hasSummary = hasSummary;
        m_summaryProvider = provider;
        emit changed();
    }
    syncTags();
    reloadTasks();
}

QVariantMap ShellMeetingModel::describeJob(const tanara::JobProgress& job)
{
    QString detail;
    int percent = job.percent;
    const tanara::JobStage* running = nullptr;
    for (const tanara::JobStage& s : job.stages)
        if (s.state == tanara::StageState::Running) { running = &s; break; }

    // Összefoglaló: a szakaszok szerint („Jegyzetek készítése: k / n rész” valós csíkkal, majd
    // „Összefésülés” határozatlanul) — ugyanaz a leképezés, mint az Összefoglaló fülön (a
    // korábbi futásból átvett részeket csak a fül mondja ki; a sáv rövid).
    const SummaryProgress sp = SummaryProgress::from(job);
    if (sp.isValid()) {
        detail = sp.label;
        percent = sp.percent;
    } else if (job.done >= 0 && job.total > 0) {
        detail = job.kind == JobKind::Identify ? tr("%1 / %2 beszélő").arg(job.done).arg(job.total)
               : job.kind == JobKind::AnalyzeTopics ? tr("%1 / %2 téma").arg(job.done).arg(job.total)
               : QStringLiteral("%1 / %2").arg(job.done).arg(job.total);
        if (percent < 0)
            percent = qBound(0, int(100.0 * job.done / job.total), 100);
    } else if (running) {
        detail = running->label;
        if (!running->detail.isEmpty())
            detail += QStringLiteral(" · ") + running->detail;
        if (percent < 0)
            percent = running->percent;
    }
    if (percent >= 0 && detail.isEmpty())
        detail = QStringLiteral("%1%").arg(percent);

    QString eta;
    const int left = job.etaSeconds();
    if (job.estimatedTotalSec > 0 && left >= 0)
        eta = left < 60 ? tr("kevesebb mint 1 perc van hátra")
                        : tr("kb. %n perc van hátra", nullptr, (left + 30) / 60);

    QString title = job.title.isEmpty() ? fallbackTitle(job.kind) : job.title;
    if (!title.endsWith(QStringLiteral("…")))
        title += QStringLiteral("…");

    return {{QStringLiteral("kind"), int(job.kind)},
            {QStringLiteral("title"), title},
            {QStringLiteral("detail"), detail},
            {QStringLiteral("eta"), eta},
            {QStringLiteral("iconName"), iconFor(job.kind)},
            {QStringLiteral("percent"), percent},
            {QStringLiteral("cancellable"), job.cancellable},
            {QStringLiteral("cancelling"), job.cancelling}};
}

void ShellMeetingModel::reloadTasks()
{
    QVariantList tasks;
    bool identify = false;
    int transcribe = -1, summarize = -1;
    if (m_exists && m_controller && m_controller->jobs()) {
        const QVector<tanara::JobProgress> jobs = m_controller->processingState(m_meetingId).jobs;
        for (const tanara::JobProgress& job : jobs) {
            if (job.kind == JobKind::Identify) identify = true;
            // A fülek pirulája: a futó lépés haladása (határozatlannál 0).
            if (job.kind == JobKind::Transcribe)
                transcribe = qMax(0, describeJob(job).value(QStringLiteral("percent")).toInt());
            if (job.kind == JobKind::Summarize || job.kind == JobKind::AnalyzeTopics)
                summarize = qMax(0, describeJob(job).value(QStringLiteral("percent")).toInt());
            // Az ELSŐ átírást az átirat előtti nézet szakasz-listája mutatja (M04); a sávba
            // csak az újra-átírás kerül (ott a fülek látszanak, nincs más jelzés).
            if (job.kind == JobKind::Transcribe && !m_hasTranscript) continue;
            tasks.append(describeJob(job));
        }
    } else if (m_demoTask && !m_controller) {
        tanara::JobProgress job;
        job.meetingId = m_meetingId.isEmpty() ? QStringLiteral("demo") : m_meetingId;
        job.kind = JobKind::Identify;
        job.title = tr("Résztvevők azonosítása");
        job.done = 3;
        job.total = 5;
        tasks.append(describeJob(job));
        identify = true;
    }
    if (tasks == m_tasks && identify == m_identifyRunning && transcribe == m_transcribePercent
        && summarize == m_summarizePercent)
        return;
    m_tasks = tasks;
    m_identifyRunning = identify;
    m_transcribePercent = transcribe;
    m_summarizePercent = summarize;
    emit tasksChanged();
}

} // namespace tanara_qml
