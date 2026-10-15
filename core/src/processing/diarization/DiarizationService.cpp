#include "tanara/processing/diarization/DiarizationService.h"

#include "tanara/foundation/log/Logging.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/processing/diarization/DiarizationMerge.h"

#include <QDir>
#include <QFile>
#include <QSet>
#include <QTimer>

namespace tanara::processing {

namespace {
const QString kStage = QStringLiteral("diarize");
}

DiarizationService::DiarizationService(Deps deps, QObject* parent) : QObject(parent), m_deps(std::move(deps)) {}

DiarizationService::~DiarizationService()
{
    for (const Run& r : std::as_const(m_runs)) {
        if (r.job) r.job->cancel();
        delete r.owner;
    }
}

providers::DiarizeRequest DiarizationService::requestFor(const Meeting& m)
{
    providers::DiarizeRequest req;
    if (m.diarizeSpeakerCount > 0) req.numClusters = m.diarizeSpeakerCount;
    for (const Track& t : m.tracks) {
        if (!t.included() || t.file.isEmpty()) continue;
        providers::DiarizeTrack dt;
        dt.trackId = t.id;
        dt.path = QDir(m.folder).filePath(t.file);
        dt.startOffsetMs = t.startOffsetMs;
        // Single-speaker only by a manual binding: exactly one named participant bound to this track.
        QSet<QString> persons;
        PersonId person;
        for (const Participant& p : m.participants)
            if (!p.personId.isNull() && p.tracks.contains(t.id)) {
                persons.insert(p.personId.toString());
                person = p.personId;
            }
        if (persons.size() == 1) dt.knownPerson = person;
        req.tracks.append(dt);
    }
    return req;
}

bool DiarizationService::start(const QString& meetingId, Done done, bool redo)
{
    if (m_runs.contains(meetingId) || !m_deps.store || !m_deps.diarizer) return false;
    const Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) return false;
    const providers::DiarizeRequest req = requestFor(m);
    if (req.isEmpty()) return false;
    const MergedTranscript current = m_deps.transcript ? m_deps.transcript(meetingId) : MergedTranscript();
    const bool separatedLocally = current.speakerSource == diarizationmerge::kSourceLocalDiarization;
    if (!diarizationmerge::needsSpeakers(current) && !(redo && separatedLocally)) return false;

    providers::IDiarizer* diarizer = m_deps.diarizer(this);
    if (!diarizer) return false;
    providers::DiarizeJob* job = diarizer->diarize(req);
    Run& run = m_runs[meetingId];
    run.job = job;
    run.owner = dynamic_cast<QObject*>(diarizer);
    run.done = std::move(done);

    library::MeetingJobTracker* jobs = m_deps.jobs;
    if (jobs) {
        jobs->setStage(meetingId, JobKind::Transcribe, kStage, StageState::Running, 0);
        jobs->setStageDetail(meetingId, JobKind::Transcribe, kStage, tr("helyben, %n sáv", nullptr, int(req.tracks.size())));
    }
    emit jobProgress(meetingId, tr("Beszélők szétválasztása…"));
    qCInfo(lcVoice).noquote() << "Local diarization started:" << meetingId << req.tracks.size() << "track(s)";

    connect(job, &providers::CapabilityJob::progress, this, [jobs, meetingId](qint64 done, qint64 total) {
        if (jobs && total > 0)
            jobs->setStagePercent(meetingId, JobKind::Transcribe, kStage, int(qBound<qint64>(qint64(0), done * 100 / total, qint64(100))));
    });
    connect(job, &providers::CapabilityJob::finished, this, [this, meetingId, job, redo]() {
        DiarizationOutcome out;
        out.ok = true;
        out.result = job->result();
        MergedTranscript t = m_deps.transcript ? m_deps.transcript(meetingId) : MergedTranscript();
        if (redo && t.speakerSource == diarizationmerge::kSourceLocalDiarization) {
            // Fold the earlier local speakers back into one, so the merge applies to a fresh start.
            for (TranscriptToken& tok : t.tokens) tok.speaker = diarizationmerge::rawLabel(1);
            t.speakerSource.clear();
            t.speakerModels.clear();
        }
        const diarizationmerge::MergeResult mr = diarizationmerge::apply(t, out.result);
        out.applied = mr.applied;
        out.rawSpeakers = int(mr.labels.size());
        out.message = mr.reason;
        if (mr.applied && m_deps.saveTranscript) m_deps.saveTranscript(meetingId, t);
        qCInfo(lcVoice).noquote() << "Local diarization finished:" << meetingId << out.result.turns.size() << "turns,"
                                  << out.result.speakers.size() << "speakers," << out.rawSpeakers << "raw speakers"
                                  << (mr.applied ? QString() : QStringLiteral("(not applied: %1)").arg(mr.reason));
        finish(meetingId, out);
    });
    connect(job, &providers::CapabilityJob::failed, this, [this, meetingId, job]() {
        DiarizationOutcome out;
        out.message = job->error().message.isEmpty() ? job->error().code : job->error().message;
        qCWarning(lcVoice).noquote() << "Local diarization failed:" << meetingId << job->error().code << out.message;
        finish(meetingId, out);
    });
    return true;
}

bool DiarizationService::cancel(const QString& meetingId)
{
    if (!m_runs.contains(meetingId)) return false;
    if (QPointer<providers::DiarizeJob> job = m_runs.value(meetingId).job) job->cancel();   // no signal follows
    DiarizationOutcome out;
    out.cancelled = true;
    out.message = QStringLiteral("cancelled");
    // `done` must not run inside the caller's cancel(): the next event-loop turn.
    QTimer::singleShot(0, this, [this, meetingId, out]() { finish(meetingId, out); });
    return true;
}

void DiarizationService::finish(const QString& meetingId, const DiarizationOutcome& outcome)
{
    auto it = m_runs.find(meetingId);
    if (it == m_runs.end()) return;
    Run run = it.value();
    m_runs.erase(it);
    if (run.owner) run.owner->deleteLater();   // the job is the diarizer's child
    if (library::MeetingJobTracker* jobs = m_deps.jobs; jobs && !outcome.cancelled) {
        if (outcome.ok) {
            jobs->setStage(meetingId, JobKind::Transcribe, kStage, StageState::Done);
            jobs->setStageDetail(meetingId, JobKind::Transcribe, kStage,
                                 outcome.applied ? tr("%n beszélő", nullptr, outcome.rawSpeakers) : QString());
        } else {
            jobs->setStage(meetingId, JobKind::Transcribe, kStage, StageState::Failed);
            jobs->setStageDetail(meetingId, JobKind::Transcribe, kStage,
                                 tr("nem sikerült — az átirat beszélők nélkül marad"));
        }
    }
    if (run.done) run.done(outcome);
}

} // namespace tanara::processing
