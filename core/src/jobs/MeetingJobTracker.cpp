#include "tanara/jobs/MeetingJobTracker.h"
#include "tanara/store/MeetingStore.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace tanara {

namespace {

// A megmaradó hibák „helyei” a processing.json-ban.
QString errorSlot(JobKind kind)
{
    switch (kind) {
    case JobKind::Transcribe:    return QStringLiteral("transcribe");
    case JobKind::Summarize:
    case JobKind::ExtractTopics:
    case JobKind::AnalyzeTopics: return QStringLiteral("summarize");
    case JobKind::Mixdown:       return QStringLiteral("mixdown");
    case JobKind::Identify:      return QString();
    }
    return QString();
}

QString kindToString(JobKind kind)
{
    switch (kind) {
    case JobKind::Transcribe:    return QStringLiteral("transcribe");
    case JobKind::Summarize:     return QStringLiteral("summarize");
    case JobKind::ExtractTopics: return QStringLiteral("extractTopics");
    case JobKind::AnalyzeTopics: return QStringLiteral("analyzeTopics");
    case JobKind::Mixdown:       return QStringLiteral("mixdown");
    case JobKind::Identify:      return QStringLiteral("identify");
    }
    return QStringLiteral("transcribe");
}

JobKind kindFromString(const QString& s, JobKind fallback)
{
    if (s == QLatin1String("transcribe"))    return JobKind::Transcribe;
    if (s == QLatin1String("summarize"))     return JobKind::Summarize;
    if (s == QLatin1String("extractTopics")) return JobKind::ExtractTopics;
    if (s == QLatin1String("analyzeTopics")) return JobKind::AnalyzeTopics;
    if (s == QLatin1String("mixdown"))       return JobKind::Mixdown;
    if (s == QLatin1String("identify"))      return JobKind::Identify;
    return fallback;
}

QJsonObject errorToJson(const JobError& e)
{
    QJsonObject o;
    o[QStringLiteral("kind")]    = kindToString(e.kind);
    o[QStringLiteral("message")] = e.message;
    if (!e.detail.isEmpty())        o[QStringLiteral("detail")] = e.detail;
    if (!e.fixActionHint.isEmpty()) o[QStringLiteral("fixActionHint")] = e.fixActionHint;
    if (e.when.isValid())           o[QStringLiteral("when")] = e.when.toString(Qt::ISODate);
    return o;
}

JobError errorFromJson(const QJsonObject& o, JobKind fallbackKind)
{
    JobError e;
    e.kind          = kindFromString(o.value(QStringLiteral("kind")).toString(), fallbackKind);
    e.message       = o.value(QStringLiteral("message")).toString();
    e.detail        = o.value(QStringLiteral("detail")).toString();
    e.fixActionHint = o.value(QStringLiteral("fixActionHint")).toString();
    e.when          = QDateTime::fromString(o.value(QStringLiteral("when")).toString(), Qt::ISODate);
    return e;
}

bool isSummaryKind(JobKind k)
{
    return k == JobKind::Summarize || k == JobKind::ExtractTopics || k == JobKind::AnalyzeTopics;
}

} // namespace

// A meeting lemezen megmaradó feldolgozási állapota (processing.json).
struct MeetingJobTracker::Persisted {
    QString folder;                       // üres → csak memóriában él (ismeretlen meeting)
    QHash<QString, JobError> errors;      // hibahely → hiba
    QHash<QString, JobError> topics;      // topicId → hiba
    QDateTime identifiedAt;

    bool isEmpty() const { return errors.isEmpty() && topics.isEmpty() && !identifiedAt.isValid(); }
};

MeetingJobTracker::MeetingJobTracker(MeetingStore* store, QObject* parent)
    : QObject(parent), m_store(store)
{
    qRegisterMetaType<tanara::JobKind>();
    qRegisterMetaType<tanara::JobProgress>();
    qRegisterMetaType<tanara::JobOutcome>();
    qRegisterMetaType<tanara::JobError>();
    qRegisterMetaType<tanara::MeetingProcessingState>();
    if (m_store) {
        // Törölt meeting: a gyorsítótár és az esetleg még nyilvántartott feladatok eldobása.
        connect(m_store, &MeetingStore::meetingRemoved, this, [this](const QString& id) {
            delete m_persisted.take(id);
            for (int i = m_jobs.size() - 1; i >= 0; --i)
                if (m_jobs.at(i).meetingId == id) m_jobs.removeAt(i);
        });
    }
}

MeetingJobTracker::~MeetingJobTracker()
{
    qDeleteAll(m_persisted);
}

QString MeetingJobTracker::folderFor(const QString& meetingId) const
{
    return m_store ? m_store->load(meetingId).folder : QString();
}

MeetingJobTracker::Persisted& MeetingJobTracker::persisted(const QString& meetingId,
                                                           const QString& folderHint) const
{
    auto it = m_persisted.find(meetingId);
    if (it != m_persisted.end()) {
        // Korábban mappa nélkül (ismeretlenként) került be, de most már tudjuk a mappát.
        if ((*it)->folder.isEmpty() && !folderHint.isEmpty())
            (*it)->folder = folderHint;
        return **it;
    }
    auto* p = new Persisted;
    p->folder = folderHint.isEmpty() ? folderFor(meetingId) : folderHint;
    if (!p->folder.isEmpty()) {
        QFile f(QDir(p->folder).filePath(stateFileName()));
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
            const QJsonObject errs = root.value(QStringLiteral("errors")).toObject();
            for (auto e = errs.constBegin(); e != errs.constEnd(); ++e) {
                const JobKind fb = e.key() == QLatin1String("transcribe") ? JobKind::Transcribe
                                 : e.key() == QLatin1String("mixdown")    ? JobKind::Mixdown
                                                                          : JobKind::Summarize;
                const JobError je = errorFromJson(e.value().toObject(), fb);
                if (je.isValid()) p->errors.insert(e.key(), je);
            }
            const QJsonObject tops = root.value(QStringLiteral("topicErrors")).toObject();
            for (auto t = tops.constBegin(); t != tops.constEnd(); ++t) {
                const JobError je = errorFromJson(t.value().toObject(), JobKind::AnalyzeTopics);
                if (je.isValid()) p->topics.insert(t.key(), je);
            }
            p->identifiedAt = QDateTime::fromString(
                root.value(QStringLiteral("identifiedAt")).toString(), Qt::ISODate);
        }
    }
    m_persisted.insert(meetingId, p);
    return *p;
}

void MeetingJobTracker::savePersisted(const QString& meetingId) const
{
    const Persisted& p = persisted(meetingId);
    if (p.folder.isEmpty() || !QDir(p.folder).exists())
        return;
    const QString path = QDir(p.folder).filePath(stateFileName());
    if (p.isEmpty()) {            // nincs mit őrizni → ne maradjon üres fájl a mappában
        QFile::remove(path);
        return;
    }
    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    QJsonObject errs;
    for (auto it = p.errors.constBegin(); it != p.errors.constEnd(); ++it)
        errs[it.key()] = errorToJson(it.value());
    if (!errs.isEmpty()) root[QStringLiteral("errors")] = errs;
    QJsonObject tops;
    for (auto it = p.topics.constBegin(); it != p.topics.constEnd(); ++it)
        tops[it.key()] = errorToJson(it.value());
    if (!tops.isEmpty()) root[QStringLiteral("topicErrors")] = tops;
    if (p.identifiedAt.isValid())
        root[QStringLiteral("identifiedAt")] = p.identifiedAt.toString(Qt::ISODate);
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

// ---- lekérdezés ------------------------------------------------------------------------

JobProgress* MeetingJobTracker::find(const QString& meetingId, JobKind kind)
{
    for (JobProgress& j : m_jobs)
        if (j.kind == kind && j.meetingId == meetingId) return &j;
    return nullptr;
}

const JobProgress* MeetingJobTracker::find(const QString& meetingId, JobKind kind) const
{
    for (const JobProgress& j : m_jobs)
        if (j.kind == kind && j.meetingId == meetingId) return &j;
    return nullptr;
}

JobProgress MeetingJobTracker::job(const QString& meetingId, JobKind kind) const
{
    const JobProgress* j = find(meetingId, kind);
    return j ? *j : JobProgress{};
}

bool MeetingJobTracker::isRunning(const QString& meetingId, JobKind kind) const
{
    return find(meetingId, kind) != nullptr;
}

bool MeetingJobTracker::isBusy(const QString& meetingId) const
{
    for (const JobProgress& j : m_jobs)
        if (j.meetingId == meetingId) return true;
    return false;
}

QVector<JobProgress> MeetingJobTracker::activeJobs() const { return m_jobs; }

JobError MeetingJobTracker::lastError(const QString& meetingId, JobKind kind) const
{
    const QString slot = errorSlot(kind);
    if (slot.isEmpty()) return {};
    return persisted(meetingId).errors.value(slot);
}

QHash<QString, JobError> MeetingJobTracker::topicErrors(const QString& meetingId) const
{
    return persisted(meetingId).topics;
}

QDateTime MeetingJobTracker::identifiedAt(const QString& meetingId) const
{
    return persisted(meetingId).identifiedAt;
}

MeetingProcessingState MeetingJobTracker::state(const QString& meetingId) const
{
    if (!m_store) {
        Meeting m; m.id = meetingId;
        return state(m);
    }
    Meeting m = m_store->load(meetingId);
    if (m.id.isEmpty()) m.id = meetingId;   // ismeretlen → üres állapot (csak a futó feladatok)
    return state(m);
}

MeetingProcessingState MeetingJobTracker::state(const Meeting& m) const
{
    MeetingProcessingState s;
    s.meetingId = m.id;
    const Persisted& p = persisted(m.id, m.folder);
    s.transcriptError = p.errors.value(QStringLiteral("transcribe"));
    s.summaryError    = p.errors.value(QStringLiteral("summarize"));

    bool transcribing = false, summarizing = false, identifying = false;
    for (const JobProgress& j : m_jobs) {
        if (j.meetingId != m.id) continue;
        s.jobs.append(j);
        if (j.kind == JobKind::Transcribe) {
            transcribing = true;
            if (const JobStage* st = j.stage(QStringLiteral("mixdown")); st && st->state == StageState::Running) {
                s.mixdownRunning = true;
                s.mixdownPercent = st->percent;
            }
            if (const JobStage* st = j.stage(QStringLiteral("identify")); st && st->state == StageState::Running)
                identifying = true;
        } else if (isSummaryKind(j.kind)) {
            summarizing = true;
        } else if (j.kind == JobKind::Mixdown) {
            s.mixdownRunning = true;
            s.mixdownPercent = j.percent;
        } else if (j.kind == JobKind::Identify) {
            identifying = true;
        }
    }

    s.transcriptState = transcribing ? StepState::Running
                      : m.hasTranscript ? StepState::Done
                      : s.transcriptError.isValid() ? StepState::Failed : StepState::None;
    s.summaryState = summarizing ? StepState::Running
                   : m.hasSummary ? StepState::Done
                   : s.summaryError.isValid() ? StepState::Failed : StepState::None;
    if (s.summaryState == StepState::Done && m_staleProbe) {
        const int corrected = m_staleProbe(m);
        if (corrected >= 0) {
            s.summaryStale = true;
            s.staleCorrectedSpeakers = corrected;
        }
    }
    // Azonosítás: fut → Running; lefutott (jelölés a lemezen) → Done; régi meetingnél a jelölés
    // hiányzik, ott a nevesített beszélő a jel, hogy megtörtént.
    s.identifyState = identifying ? StepState::Running
                    : (m.hasTranscript && (p.identifiedAt.isValid() || !m.speakerMap.isEmpty()))
                          ? StepState::Done : StepState::None;
    return s;
}

// ---- hibák -----------------------------------------------------------------------------

void MeetingJobTracker::clearError(const QString& meetingId, JobKind kind)
{
    const QString slot = errorSlot(kind);
    if (slot.isEmpty()) return;
    Persisted& p = persisted(meetingId);
    if (p.errors.remove(slot) == 0) return;
    savePersisted(meetingId);
    emit errorChanged(meetingId, kind);
    emit stateChanged(meetingId);
}

void MeetingJobTracker::recordError(const QString& meetingId, const JobError& error)
{
    const QString slot = errorSlot(error.kind);
    if (slot.isEmpty() || !error.isValid()) return;
    JobError e = error;
    if (!e.when.isValid()) e.when = QDateTime::currentDateTime();
    persisted(meetingId).errors.insert(slot, e);
    savePersisted(meetingId);
    emit errorChanged(meetingId, error.kind);
    emit stateChanged(meetingId);
}

void MeetingJobTracker::setTopicError(const QString& meetingId, const QString& topicId,
                                      const JobError& error)
{
    if (topicId.isEmpty() || !error.isValid()) return;
    JobError e = error;
    e.kind = JobKind::AnalyzeTopics;
    if (!e.when.isValid()) e.when = QDateTime::currentDateTime();
    persisted(meetingId).topics.insert(topicId, e);
    savePersisted(meetingId);
}

void MeetingJobTracker::clearTopicError(const QString& meetingId, const QString& topicId)
{
    if (persisted(meetingId).topics.remove(topicId) > 0)
        savePersisted(meetingId);
}

void MeetingJobTracker::markIdentified(const QString& meetingId, bool identified)
{
    Persisted& p = persisted(meetingId);
    if (!identified && !p.identifiedAt.isValid()) return;
    p.identifiedAt = identified ? QDateTime::currentDateTime() : QDateTime();
    savePersisted(meetingId);
    emit stateChanged(meetingId);
}

// ---- vezénylő API ----------------------------------------------------------------------

void MeetingJobTracker::begin(const QString& meetingId, JobKind kind, const QString& title,
                              const QVector<JobStage>& stages, bool cancellable)
{
    JobProgress fresh;
    fresh.meetingId = meetingId;
    fresh.kind = kind;
    fresh.title = title;
    fresh.stages = stages;
    fresh.cancellable = cancellable;
    fresh.startedAt = QDateTime::currentDateTime();
    if (JobProgress* existing = find(meetingId, kind))
        *existing = fresh;
    else
        m_jobs.append(fresh);
    emit jobStarted(meetingId, kind);
    emit jobProgressChanged(meetingId, fresh);
    emit stateChanged(meetingId);
}

void MeetingJobTracker::setStage(const QString& meetingId, JobKind kind, const QString& stageId,
                                 StageState state, int percent, const QString& detail)
{
    JobProgress* j = find(meetingId, kind);
    if (!j) return;
    for (JobStage& s : j->stages) {
        if (s.id != stageId) continue;
        const bool stateFlip = s.state != state;
        if (!stateFlip && s.percent == percent && (detail.isEmpty() || s.detail == detail))
            return;
        s.state = state;
        s.percent = (state == StageState::Running) ? percent : -1;
        if (!detail.isEmpty()) s.detail = detail;
        emit jobProgressChanged(meetingId, *j);
        // A szakasz-váltás a levezetett állapotot is érintheti (lekeverés / azonosítás fut-e).
        if (stateFlip) emit stateChanged(meetingId);
        return;
    }
}

void MeetingJobTracker::setStagePercent(const QString& meetingId, JobKind kind,
                                        const QString& stageId, int percent)
{
    JobProgress* j = find(meetingId, kind);
    if (!j) return;
    for (JobStage& s : j->stages) {
        if (s.id != stageId || s.percent == percent) continue;
        s.percent = percent;
        emit jobProgressChanged(meetingId, *j);
        return;
    }
}

void MeetingJobTracker::setStageDetail(const QString& meetingId, JobKind kind,
                                       const QString& stageId, const QString& detail)
{
    JobProgress* j = find(meetingId, kind);
    if (!j) return;
    for (JobStage& s : j->stages) {
        if (s.id != stageId || s.detail == detail) continue;
        s.detail = detail;
        emit jobProgressChanged(meetingId, *j);
        return;
    }
}

void MeetingJobTracker::setMessage(const QString& meetingId, JobKind kind, const QString& message)
{
    JobProgress* j = find(meetingId, kind);
    if (!j || j->message == message) return;
    j->message = message;
    emit jobProgressChanged(meetingId, *j);
}

void MeetingJobTracker::setPercent(const QString& meetingId, JobKind kind, int percent)
{
    JobProgress* j = find(meetingId, kind);
    if (!j || j->percent == percent) return;
    j->percent = percent;
    emit jobProgressChanged(meetingId, *j);
}

void MeetingJobTracker::setCounts(const QString& meetingId, JobKind kind, int done, int total)
{
    JobProgress* j = find(meetingId, kind);
    if (!j || (j->done == done && j->total == total)) return;
    j->done = done;
    j->total = total;
    emit jobProgressChanged(meetingId, *j);
}

void MeetingJobTracker::setEstimate(const QString& meetingId, JobKind kind, int estimatedTotalSec)
{
    JobProgress* j = find(meetingId, kind);
    if (!j || j->estimatedTotalSec == estimatedTotalSec) return;
    j->estimatedTotalSec = estimatedTotalSec;
    emit jobProgressChanged(meetingId, *j);
}

void MeetingJobTracker::setCancelling(const QString& meetingId, JobKind kind)
{
    JobProgress* j = find(meetingId, kind);
    if (!j || j->cancelling) return;
    j->cancelling = true;
    emit jobProgressChanged(meetingId, *j);
}

void MeetingJobTracker::closeJob(const QString& meetingId, JobKind kind, JobOutcome outcome)
{
    bool removed = false;
    for (int i = 0; i < m_jobs.size(); ++i)
        if (m_jobs.at(i).kind == kind && m_jobs.at(i).meetingId == meetingId) {
            m_jobs.removeAt(i);
            removed = true;
            break;
        }
    if (removed)
        emit jobFinished(meetingId, kind, outcome);
    emit stateChanged(meetingId);
}

void MeetingJobTracker::finish(const QString& meetingId, JobKind kind)
{
    // Siker → az ehhez a lépéshez tartozó korábbi hiba érvényét veszti.
    const QString slot = errorSlot(kind);
    if (!slot.isEmpty() && persisted(meetingId).errors.remove(slot) > 0) {
        savePersisted(meetingId);
        emit errorChanged(meetingId, kind);
    }
    closeJob(meetingId, kind, JobOutcome::Done);
}

void MeetingJobTracker::fail(const QString& meetingId, JobKind kind, const JobError& error)
{
    const QString slot = errorSlot(kind);
    if (!slot.isEmpty() && error.isValid()) {
        JobError e = error;
        e.kind = kind;
        if (!e.when.isValid()) e.when = QDateTime::currentDateTime();
        persisted(meetingId).errors.insert(slot, e);
        savePersisted(meetingId);
        emit errorChanged(meetingId, kind);
    }
    closeJob(meetingId, kind, JobOutcome::Failed);
}

void MeetingJobTracker::cancelled(const QString& meetingId, JobKind kind)
{
    closeJob(meetingId, kind, JobOutcome::Cancelled);
}

} // namespace tanara
