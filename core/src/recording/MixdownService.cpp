#include "tanara/recording/MixdownService.h"

#include "tanara/domain/Job.h"
#include "tanara/foundation/SharedFile.h"
#include "tanara/library/MeetingLayout.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/recording/audio/MixdownPlan.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPointer>
#include <QSet>

namespace tanara::recording {

using library::MeetingLayout;

struct MixdownService::Impl {
    library::MeetingRepository* store = nullptr;
    library::MeetingJobTracker* jobs = nullptr;
    media::MediaToolPtr media;
    std::function<bool(const QString&)> inTranscriptionStage;

    QHash<QString, QPointer<media::MediaJob>> procs;   // meetingId -> running mixdown (ffmpeg)
    QSet<QString> cancelled;
    QHash<QString, QString> failReason;   // the reason of the last mix that did not even start (missing track)

    bool partOfTranscription(const QString& meetingId) const
    {
        return inTranscriptionStage && inTranscriptionStage(meetingId);
    }
};

MixdownService::MixdownService(library::MeetingRepository* store, library::MeetingJobTracker* jobs,
                               media::MediaToolPtr media, QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
    d->store = store;
    d->jobs = jobs;
    d->media = std::move(media);
}

MixdownService::~MixdownService() = default;

void MixdownService::setTranscriptionStageProbe(std::function<bool(const QString&)> probe)
{
    d->inTranscriptionStage = std::move(probe);
}

bool MixdownService::running(const QString& meetingId) const { return d->procs.contains(meetingId); }

QString MixdownService::takeFailReason(const QString& meetingId) { return d->failReason.take(meetingId); }

bool MixdownService::cancel(const QString& meetingId)
{
    media::MediaJob* proc = d->procs.value(meetingId);
    if (!proc || !d->jobs->isRunning(meetingId, JobKind::Mixdown)) return false;
    d->jobs->setCancelling(meetingId, JobKind::Mixdown);
    d->cancelled.insert(meetingId);
    proc->cancel();   // the finished branch closes: the half-made file is deleted, the old mix stays
    return true;
}

bool MixdownService::cancelProcess(const QString& meetingId)
{
    media::MediaJob* proc = d->procs.value(meetingId);
    if (!proc) return false;
    d->cancelled.insert(meetingId);
    proc->cancel();
    return true;
}

void MixdownService::regenerate(const QString& meetingId)
{
    // A mix is already running on this meeting: a no-op (the running one's result will do for everyone).
    if (d->procs.contains(meetingId)) return;

    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;

    d->failReason.remove(meetingId);
    const QString outRel = MeetingLayout::mixdownFile();
    const QString outPath = QDir(m.folder).filePath(outRel);

    // Only the active tracks that exist on disk go into the mix, those starting later shifted into place
    // (startOffsetMs -> adelay). The filter graph: MixdownPlan.
    const MixdownPlan plan = MixdownPlan::fromMeeting(m);
    const QStringList& missing = plan.missing;
    // With an active track missing we do not replace the EXISTING (complete) mix with a partial one, neither
    // for a manual remix nor for the automatic one before transcription.
    const bool haveMixdown = QFileInfo(outPath).isFile()
        || (!m.mixdownFile.isEmpty() && QFileInfo(QDir(m.folder).filePath(m.mixdownFile)).isFile());
    if (!missing.isEmpty() && haveMixdown) {
        const QString msg = tr("Hiányzik a(z) „%1” sáv hangfájlja — a meglévő lekeverés megmaradt. "
                               "Keresd meg a fájlt a Sávok fülön, vagy dobd el a sávot, és keverd újra.")
                                .arg(missing.join(QStringLiteral("”, „")));
        d->failReason.insert(meetingId, msg);
        emit error(msg);
        emit finished(meetingId, false);
        return;
    }
    if (plan.inputs.isEmpty()) {
        emit error(tr("Nincs aktív hangsáv a lekeveréshez."));
        emit finished(meetingId, false);
        return;
    }

    // We mix into a half-made file and replace the old one only after SUCCESS: on cancel or error the
    // earlier (still playable) mix stays untouched.
    const QString partPath = QDir(m.folder).filePath(MeetingLayout::mixdownPartFile());

    const QStringList args = plan.ffmpegArgs(partPath);
    const qint64 totalMs = m.durationMs;   // the denominator of the percentage

    // An asynchronous media job (ffmpeg): it does NOT block the UI (even a 90-minute mix may run), nor does
    // it block starting a new recording (a separate child process + a separate capture engine).
    media::MediaJob* proc = d->media->runFilterGraph(media::FilterGraphSpec{args}, this);
    d->procs.insert(meetingId, proc);
    // Structured state: a standalone mix is its own (cancellable) task; a mix running as part of a
    // transcription moves the transcription task's "mixdown" stage (the caller, from progress()).
    if (!d->partOfTranscription(meetingId))
        d->jobs->begin(meetingId, JobKind::Mixdown, tr("Lekeverés"));

    auto reportPercent = [this, meetingId](int pct) {
        emit progress(meetingId, pct);
        if (d->jobs->isRunning(meetingId, JobKind::Mixdown))
            d->jobs->setPercent(meetingId, JobKind::Mixdown, pct);
    };

    // The written length (ffmpeg -progress, parsed by the media port) -> a percentage of the recording's length.
    connect(proc, &media::MediaJob::progress, this, [reportPercent, totalMs](qint64 outMs) {
        if (totalMs <= 0 || outMs < 0) return;
        const qint64 p = outMs * 100 / totalMs;
        reportPercent(static_cast<int>(p < 0 ? 0 : (p > 99 ? 99 : p)));
    });
    // The closing in ONE place (normal exit, error, cancel, an ffmpeg that did not even start).
    connect(proc, &media::MediaJob::finished, this,
            [this, proc, meetingId, outRel, outPath, partPath](const media::MediaResult& result) {
        if (d->procs.value(meetingId) != proc) return;   // we already closed it
        d->procs.remove(meetingId);
        const bool wasCancelled = d->cancelled.remove(meetingId);
        bool ok = result.ok() && !wasCancelled;
        // Replacing so the old mix survives a failure (not "delete, then rename").
        if (ok)
            ok = replaceFile(partPath, outPath);
        if (!ok) QFile::remove(partPath);   // no half-made file stays in the folder
        if (ok) {
            // A fresh meeting (it may have changed meanwhile) -> set mixdownFile + clear dirty.
            Meeting mm = d->store->load(meetingId);
            if (!mm.id.isEmpty()) {
                mm.mixdownFile  = outRel;
                mm.mixdownDirty = false;
                d->store->saveMeeting(mm);
            }
            emit progress(meetingId, 100);
        } else if (!wasCancelled) {
            emit error(tr("A lekeverés (ffmpeg) sikertelen."));
        }
        if (d->jobs->isRunning(meetingId, JobKind::Mixdown)) {
            if (ok) d->jobs->finish(meetingId, JobKind::Mixdown);
            else if (wasCancelled) d->jobs->cancelled(meetingId, JobKind::Mixdown);
            else {
                JobError je;
                je.message = tr("A lekeverés nem sikerült.");
                je.detail = QString::fromUtf8(result.stdErr).simplified().left(200);
                d->jobs->fail(meetingId, JobKind::Mixdown, je);
            }
        }
        emit finished(meetingId, ok);
        proc->deleteLater();
        emit runEnded(meetingId, ok);
    });

    // Progress goes out as progress() (0..100) to the non-modal UI, not as a busy-flag job message.
    // Starting 0%:
    reportPercent(0);
    proc->start();
}

} // namespace tanara::recording
