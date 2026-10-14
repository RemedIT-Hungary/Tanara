#include "ShellMeetingModel.h"

#include "AppContext.h"
#include "ShellFormat.h"
#include "SummaryProgress.h"

#include "tanara/editing/IShellFacade.h"

#include <QSet>

namespace tanara_qml {

using tanara::JobKind;

namespace {

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
    // Without an injected facade the model follows the App singleton's services.
    AppContext* ctx = AppContext::instance();
    const auto resolve = [this] {
        if (m_injected) return;
        m_facade = AppContext::instance()->services().shell;
        attach();
    };
    connect(ctx, &AppContext::controllerChanged, this, resolve);
    connect(ctx, &AppContext::demoChanged, this, resolve);
    m_facade = ctx->services().shell;
    attach();
}

ShellMeetingModel::ShellMeetingModel(tanara::editing::IShellFacade* facade, QObject* parent)
    : QObject(parent), m_tags(new MeetingTagsModel(this)), m_facade(facade), m_injected(true)
{
    attach();
}

void ShellMeetingModel::attach()
{
    using tanara::editing::IShellFacade;
    if (m_facade != m_attached) {
        if (m_attached)
            m_attached->disconnect(this);
        m_attached = m_facade;
        if (IShellFacade* f = m_facade) {
            connect(f, &IShellFacade::meetingUpdated, this, &ShellMeetingModel::onMeetingTouched);
            connect(f, &IShellFacade::meetingRemoved, this, &ShellMeetingModel::onMeetingTouched);
            connect(f, &IShellFacade::speakerMapChanged, this, &ShellMeetingModel::onMeetingTouched);
            connect(f, &IShellFacade::summaryStaleChanged, this, &ShellMeetingModel::onMeetingTouched);
            connect(f, &IShellFacade::tracksChanged, this, &ShellMeetingModel::onMeetingTouched);
            connect(f, &IShellFacade::transcriptReady, this, [this](const QString& id) {
                // New transcript -> new suggestions (the controller drops the old profile first).
                if (id == m_meetingId) m_tagsRequestedFor.clear();
                onMeetingTouched(id);
            });
            connect(f, &IShellFacade::summaryReady, this, &ShellMeetingModel::onMeetingTouched);
            connect(f, &IShellFacade::jobStateChanged, this, [this](const QString& id) {
                if (id != m_meetingId) return;
                // The state (e.g. the stale marker) and the running tasks may have changed too.
                reload();
            });
            connect(f, &IShellFacade::jobProgressChanged, this, [this](const QString& id) {
                if (id == m_meetingId) reloadTasks();
            });
            // The tag row works with the real pool (without one the fictional pool stays).
            if (QObject* tags = f->tagController())
                m_tags->setController(tags);
        }
        m_tagsRequestedFor.clear();
    }
    reload();
}

void ShellMeetingModel::syncTags()
{
    // In the demo an empty id -> the fictional demo meeting's tags.
    const bool live = m_facade && m_facade->live();
    m_tags->setMeetingId(live ? m_meetingId : QString());
    if (!live || !m_exists || !m_hasTranscript || m_tagsRequestedFor == m_meetingId)
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

    if (!m_meetingId.isEmpty() && m_facade) {
        const tanara::Meeting m = m_facade->meeting(m_meetingId);
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
                const int n = m_facade->speakerCount(m);
                if (n > 0) tail = tr("%n beszélő", nullptr, n);
            } else if (!m.tracks.isEmpty()) {
                tail = tr("%n sáv", nullptr, int(m.tracks.size()));
            }
            meta = metaLine(m.startedAt, m.durationMs, tail);
            stale = m.hasSummary && m_facade->summaryStale(m).stale;
            hasSummary = m.hasSummary;
            provider = m_facade->providerLabel(tanara::providers::WorkflowStep::Summarize);
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

    // Summary: by phases ("Taking notes: part k / n" with a real bar, then "Merging"
    // indeterminate): the same mapping as on the Summary tab (parts taken over from an earlier
    // run are stated only by the tab; the bar is short).
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
    if (m_exists && m_facade && m_facade->live()) {
        const QVector<tanara::JobProgress> jobs = m_facade->processingState(m_meetingId).jobs;
        for (const tanara::JobProgress& job : jobs) {
            if (job.kind == JobKind::Identify) identify = true;
            // The tabs' pill: the running step's progress (0 if indeterminate).
            if (job.kind == JobKind::Transcribe)
                transcribe = qMax(0, describeJob(job).value(QStringLiteral("percent")).toInt());
            if (job.kind == JobKind::Summarize || job.kind == JobKind::AnalyzeTopics)
                summarize = qMax(0, describeJob(job).value(QStringLiteral("percent")).toInt());
            // The FIRST transcription is shown by the Overview's "Transcript" step (phases + "Cancel",
            // TranscribeStepPanel); only a re-transcription goes into the bar (there is no other indicator).
            if (job.kind == JobKind::Transcribe && !m_hasTranscript) continue;
            tasks.append(describeJob(job));
        }
    } else if (m_demoTask && m_facade) {
        // A design state's sample row of the bar.
        for (const tanara::JobProgress& job : m_facade->sampleTasks(m_meetingId)) {
            if (job.kind == JobKind::Identify) identify = true;
            tasks.append(describeJob(job));
        }
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
