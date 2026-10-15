#include "tanara/processing/stt/TranscriptionService.h"

#include "tanara/foundation/SharedFile.h"
#include "tanara/library/MeetingLayout.h"
#include "tanara/library/MeetingNotes.h"
#include "tanara/library/jobs/JobErrors.h"
#include "tanara/library/jobs/JobStats.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/processing/ProviderFailureSink.h"
#include "tanara/processing/stt/TranscriptionPort.h"
#include "tanara/processing/summary/TranscriptMerger.h"
#include "tanara/providers/core/ProviderRegistry.h"
#include "tanara/providers/stt/ISttProvider.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QTimer>

#include <memory>

namespace tanara::processing {

using library::MeetingLayout;
using library::TranscriptRepository;

namespace {

QString sttPhase(SttJobState s)
{
    switch (s) {
    case SttJobState::Uploading:  return TranscriptionService::tr("Hang feltöltése…");
    case SttJobState::Queued:     return TranscriptionService::tr("Várakozás a Soniox sorban…");
    case SttJobState::Processing: return TranscriptionService::tr("Átírás folyamatban…");
    case SttJobState::Completed:  return TranscriptionService::tr("Sáv kész");
    case SttJobState::Failed:     return TranscriptionService::tr("Hiba");
    default:                      return QStringLiteral("…");
    }
}

} // namespace

TranscriptionService::TranscriptionService(Deps deps, QObject* parent)
    : QObject(parent), m_deps(std::move(deps))
{
}

TranscriptionService::~TranscriptionService() = default;

bool TranscriptionService::running(const QString& meetingId) const { return m_runs.contains(meetingId); }

MergedTranscript TranscriptionService::cachedTranscript(const QString& meetingId) const
{
    return m_cache.value(meetingId);
}

void TranscriptionService::forget(const QString& meetingId)
{
    m_cache.remove(meetingId);
    m_providerDiarized.remove(meetingId);
}

MergedTranscript TranscriptionService::currentTranscript(const QString& meetingId) const
{
    const auto it = m_cache.constFind(meetingId);
    if (it != m_cache.constEnd()) return it.value();
    const QString folder = m_deps.store ? m_deps.store->folderOf(meetingId) : QString();
    return folder.isEmpty() ? MergedTranscript() : TranscriptRepository::readTokens(folder);
}

bool TranscriptionService::replaceTranscript(const QString& meetingId, const MergedTranscript& merged)
{
    Meeting mm = m_deps.store ? m_deps.store->load(meetingId) : Meeting();
    if (mm.id.isEmpty()) return false;
    TranscriptRepository::writeMarkdown(mm.folder, merged.renderMarkdown());
    TranscriptRepository::writeTokens(mm.folder, merged);
    TranscriptRepository::writeSegments(mm.folder, merged.segments());
    m_cache.insert(mm.id, merged);
    ++m_generation[meetingId];   // a new transcript: the running identifications' results are stale
    mm.speakerMap.clear();       // the raw labels mean other people now
    mm.hasTranscript = true;
    m_deps.store->saveMeeting(mm);
    emit transcriptReady(mm.id, TranscriptRepository::markdownPath(mm.folder));
    return true;
}

QString TranscriptionService::transcriptStamp(const QString& meetingId, const QString& folder) const
{
    const FileStamp fs = FileStamp::of(TranscriptRepository::tokensPath(folder));
    return QStringLiteral("%1/%2/%3").arg(m_generation.value(meetingId)).arg(fs.mtimeMs).arg(fs.size);
}

bool TranscriptionService::cancel(const QString& meetingId)
{
    auto it = m_runs.find(meetingId);
    if (it == m_runs.end()) return false;
    if (it->job) {
        it->cancelRequested = true;
        it->job->cancel();   // a synchronous failed("cancelled"): the failed branch closes it (without an error)
        return true;
    }
    // (In principle it does not occur: the provider call has not started yet.)
    m_runs.erase(it);
    m_deps.jobs->cancelled(meetingId, JobKind::Transcribe);
    return true;
}

bool TranscriptionService::start(const QString& meetingId, bool clearSpeakers)
{
    library::MeetingJobTracker* jobs = m_deps.jobs;
    // Closing the task with an error before the provider call.
    auto abortJob = [this, jobs, meetingId](const QString& msg) {
        m_runs.remove(meetingId);
        JobError je;
        je.message = msg;
        jobs->fail(meetingId, JobKind::Transcribe, je);
        emit errorOccurred(msg);
    };

    const Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) {
        m_runs.remove(meetingId);
        jobs->cancelled(meetingId, JobKind::Transcribe);
        return false;
    }
    Run& entry = m_runs[meetingId];
    entry = Run{};
    entry.clearSpeakers = clearSpeakers;

    // Own key or a Tanara Cloud run: the same provider with the gateway config; the run (job id, charge,
    // write-back, error) is filled from the response hook.
    const TranscriptionPort::Setup setup = m_deps.port->setup(meetingId);
    const QString sttId = setup.providerId;
    const providers::ProviderConfig& cfg = setup.config;
    const TranscriptionPort::Run run = setup.run;
    // Remembering the last failed HTTP exchange (in own-key mode too): this becomes the remembered error's
    // technical line ("HTTP 401 | invalid_api_key").
    const auto sink = std::make_shared<FailureSink>();
    providers::ProviderContext pctx = m_deps.port->context(run);
    captureFailures(pctx, sink);

    providers::ISttProvider* provider = m_deps.registry->create<providers::ISttProvider>(sttId, pctx, cfg, this);
    if (!provider) {
        abortJob(tr("Ismeretlen STT-provider: %1.").arg(sttId));
        return false;
    }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    emit jobProgress(m.id, tr("Átírás indítása…"));

    const QString mixPath = QDir(m.folder).filePath(MeetingLayout::mixdownOf(m));

    // A SINGLE request with the mixdown. Speaker separation comes from the provider's diarization ("Speaker N"
    // labels); the NAME is resolved afterwards by voice ID / manual rename.
    providers::SttRequest req;
    req.audioFilePath = mixPath;
    req.trackId = QStringLiteral("mixdown");
    req.languageHints = setup.languageHints;
    // Context envelope -> Soniox's structured "context" object (general/text/terms): the title + the user's
    // note (names, jargon, mishearing corrections) + the participant names. The detected call app is NOT sent
    // (it tells the transcriber nothing).
    library::meetingnotes::fillSttContext(req, m);
    req.diarization = true;
    if (!setup.diarization) {
        // The chosen cloud tier does not diarize (e.g. Fast): everyone becomes "Speaker 1" (the recording's tracks
        // stay; the pre-run warning covered this).
        req.diarization = false;
        req.speakerLabel = QStringLiteral("1");
    }
    const bool diarizing = req.diarization;
    // Whether the speakers will come from the provider (a Whisper server ignores req.diarization).
    m_providerDiarized[meetingId] = diarizing
        && m_deps.registry->descriptor(providers::CapabilityKind::Transcription, sttId).supportsDiarization;

    providers::SttJob* job = provider->transcribe(req);

    // Structured state: the job for cancelling, the earlier runs' speed for the estimate (with the same
    // provider/model); without a sample there is no estimate.
    const QString statsKey = QStringLiteral("transcribe/%1/%2").arg(sttId, cfg.model);
    {
        m_runs[meetingId].job = job;
        const int est = m_deps.jobStats ? m_deps.jobStats->estimateSec(statsKey, double(m.durationMs) / 1000.0) : -1;
        if (est >= 0) {
            const JobProgress jp = jobs->job(meetingId, JobKind::Transcribe);
            const int elapsed = jp.startedAt.isValid()
                ? int(jp.startedAt.secsTo(QDateTime::currentDateTime())) : 0;
            jobs->setEstimate(meetingId, JobKind::Transcribe, elapsed + est);
        }
        if (!diarizing)
            jobs->setStage(meetingId, JobKind::Transcribe, QStringLiteral("diarize"), StageState::Skipped);
        jobs->setStage(meetingId, JobKind::Transcribe, QStringLiteral("upload"), StageState::Running, 0);
    }
    auto sttClock = std::make_shared<QElapsedTimer>();
    sttClock->start();

    connect(job, &providers::SttJob::uploadProgress, this, [jobs, meetingId](qint64 sent, qint64 total) {
        if (total <= 0) return;
        // Real byte ratio. After 100% the provider's state change closes the stage.
        const JobProgress jp = jobs->job(meetingId, JobKind::Transcribe);
        const JobStage* st = jp.stage(QStringLiteral("upload"));
        if (st && st->state == StageState::Running)
            jobs->setStagePercent(meetingId, JobKind::Transcribe, QStringLiteral("upload"), int(sent * 100 / total));
    });
    connect(job, &providers::SttJob::stateChanged, this, [this, jobs, id = m.id, diarizing](SttJobState st) {
        emit jobProgress(id, sttPhase(st));
        // During transcription the providers give NO percentage, only a state (upload / queued / processing).
        // The provider does speaker separation in the same pass as transcription, so the two stages run and
        // close together.
        const JobKind k = JobKind::Transcribe;
        if (st == SttJobState::Uploading) {
            jobs->setStage(id, k, QStringLiteral("upload"), StageState::Running, 0);
        } else if (st == SttJobState::Queued || st == SttJobState::Processing) {
            jobs->setStage(id, k, QStringLiteral("upload"), StageState::Done);
            jobs->setStage(id, k, QStringLiteral("transcribe"), StageState::Running);
            jobs->setStageDetail(id, k, QStringLiteral("transcribe"),
                                 st == SttJobState::Queued ? tr("sorban áll") : QString());
            if (diarizing)
                jobs->setStage(id, k, QStringLiteral("diarize"), StageState::Running);
        }
    });
    // The detailed poll message (elapsed time + heartbeat) should also reach the UI, so that during a long
    // async processing it shows it is running and the connection is alive.
    connect(job, &providers::SttJob::progress, this, [this, jobs, id = m.id](int, const QString& msg) {
        emit jobProgress(id, msg);
        jobs->setMessage(id, JobKind::Transcribe, msg);
    });
    connect(job, &providers::SttJob::finished, this,
            [this, jobs, meetingId, providerObj, run, job, statsKey, sttClock, diarizing](const TrackTranscript& result) {
        const Run info = m_runs.take(meetingId);
        if (providerObj) providerObj->deleteLater();
        // The job still sends its best-effort cleanup DELETEs; after that it can be deleted.
        QTimer::singleShot(30000, job, &QObject::deleteLater);

        // A FRESH meeting: transcription may run for minutes, during which the user may have renamed or edited
        // (or deleted) the meeting; saving the old instance would overwrite that.
        Meeting mm = m_deps.store->load(meetingId);
        if (mm.id.isEmpty()) {
            jobs->cancelled(meetingId, JobKind::Transcribe);
            m_deps.port->finishRun(run);
            return;
        }

        // An EMPTY result (the provider returned not a single word) = an ERROR: we do not write it over the
        // existing transcript. The earlier transcript, speaker names and manual corrections are untouched.
        if (result.tokens.isEmpty()) {
            const QString msg = mm.hasTranscript
                ? tr("Az átírás üres eredményt adott (a szolgáltató nem talált beszédet a felvételen) — "
                     "a korábbi átirat megmaradt.")
                : tr("Az átírás üres eredményt adott: a szolgáltató nem talált beszédet a felvételen.");
            JobError je;
            je.message = msg;
            jobs->fail(meetingId, JobKind::Transcribe, je);
            m_deps.port->finishRun(run);   // the run closes (any charge shows up this way too)
            emit errorOccurred(msg);
            return;
        }
        // Whether there was a transcript BEFORE the run (independent of the flag; see below).
        const bool hadTranscript = mm.hasTranscript || TranscriptRepository::hasTokens(mm.folder);

        TrackTranscript res = result;
        // We translate the provider's diarization ids (1,2,...) into a neutral "Speaker N" label.
        for (TranscriptToken& tok : res.tokens)
            tok.speaker = tok.speaker.isEmpty()
                ? QStringLiteral("Beszélő")
                : QStringLiteral("Beszélő %1").arg(tok.speaker);

        QVector<TrackTranscript> single{res};
        MergedTranscript merged = mergeTranscripts(single);
        TranscriptRepository::writeMarkdown(mm.folder, merged.renderMarkdown());
        TranscriptRepository::writeTokens(mm.folder, merged);
        TranscriptRepository::writeSegments(mm.folder, merged.segments());
        m_cache.insert(mm.id, merged);
        ++m_generation[meetingId];   // a new transcript: the running identifications' results are stale
        // Re-transcription: the new diarization's "Speaker N" labels may mean OTHER people, so the old
        // assignments are cleared together with the NEW transcript. NOT only on the re-transcription's flag: that
        // is lost on failure, and a plain (retried) transcription after it would leave the old names on the new
        // labels. So whenever a transcript already existed, we always clear.
        if (info.clearSpeakers || hadTranscript)
            mm.speakerMap.clear();
        mm.hasTranscript = true;
        m_deps.store->saveMeeting(mm);

        if (m_deps.jobStats)
            m_deps.jobStats->addSample(statsKey, double(sttClock->elapsed()) / 1000.0, double(mm.durationMs) / 1000.0);
        const JobKind k = JobKind::Transcribe;
        jobs->setStage(meetingId, k, QStringLiteral("upload"), StageState::Done);
        jobs->setStage(meetingId, k, QStringLiteral("transcribe"), StageState::Done);
        if (diarizing)
            jobs->setStage(meetingId, k, QStringLiteral("diarize"), StageState::Done);
        jobs->markIdentified(meetingId, false);   // identification has not run on the new transcript yet

        emit transcriptReady(mm.id, TranscriptRepository::markdownPath(mm.folder));
        m_deps.port->finishRun(run);   // "This transcription cost $0.41." (cloud runs only)
        emit transcribed(meetingId);
    });
    connect(job, &providers::SttJob::failed, this, [this, jobs, meetingId, providerObj, run, job, sink](QString e) {
        const Run info = m_runs.take(meetingId);
        if (providerObj) providerObj->deleteLater();
        QTimer::singleShot(30000, job, &QObject::deleteLater);   // after the cleanup DELETEs

        if (info.cancelRequested) {
            // Cancel: no error, no errorOccurred. At the provider the job cleaned up (the uploaded audio +
            // transcript deleted); the earlier transcript (if any) is untouched.
            m_deps.port->cancelledRun(run);
            jobs->cancelled(meetingId, JobKind::Transcribe);
            return;
        }
        Error cloudError;
        const JobError je = m_deps.port->runError(run, &cloudError)
            ? library::describeCloudFailure(JobKind::Transcribe, cloudError)
            : library::describeJobFailure(JobKind::Transcribe, e, sink->has ? &sink->ex : nullptr);
        jobs->fail(meetingId, JobKind::Transcribe, je);
        if (!m_deps.port->failRun(run))   // a structured gateway error is reported by the port
            emit errorOccurred(tr("Átírás-hiba: %1").arg(e));
    });
    return true;
}

} // namespace tanara::processing
