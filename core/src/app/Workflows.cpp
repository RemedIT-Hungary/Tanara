#include "tanara/app/Workflows.h"

#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/editing/session/TrackSpeech.h"
#include "tanara/editing/undo/MeetingUndoRegistry.h"
#include "tanara/foundation/log/Logging.h"
#include "tanara/library/MeetingLayout.h"
#include "tanara/library/import/ImportService.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/ArchiveService.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/tags/TagSuggestionService.h"
#include "tanara/library/tracks/TrackCatalog.h"
#include "tanara/processing/analysis/TrackActivity.h"
#include "tanara/processing/diarization/DiarizationMerge.h"
#include "tanara/processing/diarization/DiarizationService.h"
#include "tanara/processing/stt/TranscriptionService.h"
#include "tanara/processing/summary/SummarizationService.h"
#include "tanara/recording/MixdownService.h"

#include <QDir>
#include <QFile>

namespace tanara::app {

using library::MeetingLayout;

Workflows::Workflows(Deps deps, QObject* parent)
    : QObject(parent), m_deps(std::move(deps))
{
    // A mix running as a transcription's stage moves that task's "mixdown" stage instead of its own task.
    if (m_deps.mixdown)
        m_deps.mixdown->setTranscriptionStageProbe([this](const QString& id) { return transcriptionInMixdown(id); });
    if (processing::TranscriptionService* stt = m_deps.transcription) {
        connect(stt, &processing::TranscriptionService::transcribed, this, &Workflows::transcribed);
        connect(stt, &processing::TranscriptionService::transcriptReady, this,
                [this](const QString& id) { m_speakersCache.remove(id); });
    }
}

Workflows::~Workflows()
{
    for (const MixWait& w : std::as_const(m_mixWaits))
        if (w.conn) QObject::disconnect(*w.conn);
    if (m_deps.mixdown) m_deps.mixdown->setTranscriptionStageProbe({});
}

bool Workflows::mixdownManual() const
{
    return m_deps.mixdownMode && m_deps.mixdownMode() == QStringLiteral("manual");
}

bool Workflows::analyzeParticipants(const QString& meetingId)
{
    return m_deps.voice.analyzeParticipants && m_deps.voice.analyzeParticipants(meetingId);
}

// ==== transcribe -> (mixdown) -> STT -> identify ===========================================================

void Workflows::transcribe(const QString& meetingId)
{
    if (m_deps.jobs->isRunning(meetingId, JobKind::Transcribe)) {
        clearChosenRoute(providers::Capability::Stt, meetingId);   // the choice was for a run that does not start
        return;   // already running on this meeting (a double click / two views)
    }
    m_diarizeProblem.remove(meetingId);

    Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) {
        m_clearSpeakers.remove(meetingId);
        emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId));
        return;
    }
    // The voice analysis runs in parallel with transcription (if it has not run on these tracks yet).
    analyzeParticipants(meetingId);

    // This run's routes (Ask is answered here, once): the readiness and the provider follow them.
    resolveRoute(providers::Capability::Stt, meetingId);
    resolveRoute(providers::Capability::Diarization, meetingId);

    if (m_deps.readiness) {
        const providers::ReadinessResult res = m_deps.readiness(providers::WorkflowStep::Transcribe, meetingId);
        if (!res.runnable) {
            m_clearSpeakers.remove(meetingId);
            emit errorOccurred(res.detail);
            return;
        }
    }

    // A standalone identification refers to the labels of the CURRENT transcript: we stop it so its names
    // do not land on the NEW transcript about to arrive (its result is dropped at completion by the
    // transcript-change check too).
    cancel(meetingId, JobKind::Identify);

    // The transcript is made from the MIXDOWN (a single audio stream: no per-track overlap
    // merging/duplication, 1x provider cost instead of ~Nx). If the mixdown is missing or stale, we make it
    // first and start the transcription when it is ready.
    const QString mixPath = QDir(m.folder).filePath(MeetingLayout::mixdownOf(m));
    const bool needMix = m.mixdownFile.isEmpty() || m.mixdownDirty || !QFile::exists(mixPath);

    // Structured task: the stage list. The mixdown is a stage only if needed; identification only if there
    // is a voice model.
    {
        int activeTracks = 0;
        for (const Track& t : m.tracks) if (t.active) ++activeTracks;
        QVector<JobStage> stages;
        if (needMix)
            stages.append({QStringLiteral("mixdown"), tr("Lekeverés"), StageState::Waiting, -1, QString()});
        stages.append({QStringLiteral("upload"), tr("Feltöltés"), StageState::Waiting, -1,
                       tr("%1 perc, %2 sáv").arg(qMax<qint64>(1, (m.durationMs + 30000) / 60000))
                                            .arg(activeTracks)});
        stages.append({QStringLiteral("transcribe"), tr("Átírás"), StageState::Waiting, -1, QString()});
        stages.append({QStringLiteral("diarize"), tr("Beszélők szétválasztása"), StageState::Waiting, -1, QString()});
        if (m_deps.voice.usable && m_deps.voice.usable())
            stages.append({QStringLiteral("identify"), tr("Résztvevők azonosítása"),
                           m_skipIdentify.contains(meetingId) ? StageState::Skipped : StageState::Waiting,
                           -1, QString()});
        m_deps.jobs->begin(meetingId, JobKind::Transcribe, tr("Átírás folyamatban"), stages);
    }

    if (needMix) {
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(m_deps.mixdown, &recording::MixdownService::finished, this,
            [this, meetingId, conn](const QString& id, bool ok) {
                if (id != meetingId) return;
                QObject::disconnect(*conn);
                if (!m_mixWaits.remove(meetingId)) return;   // cancelled meanwhile
                if (ok) {
                    m_deps.jobs->setStage(meetingId, JobKind::Transcribe, QStringLiteral("mixdown"), StageState::Done);
                    transcribeFromMixdown(meetingId);
                    return;
                }
                m_clearSpeakers.remove(meetingId);
                const QString msg = tr("A lekeverés sikertelen — az átírás nem indult.");
                JobError je;
                je.message = msg;
                je.detail = m_deps.mixdown->takeFailReason(meetingId);   // e.g. which track's file is missing
                m_deps.jobs->fail(meetingId, JobKind::Transcribe, je);
                emit errorOccurred(msg);
            });
        MixWait& wait = m_mixWaits[meetingId];
        wait.conn = conn;
        // If a standalone mix was already running, we wait for it; on cancel we do not stop that one.
        wait.ownsMixdown = !m_deps.mixdown->running(meetingId);
        m_deps.jobs->setStage(meetingId, JobKind::Transcribe, QStringLiteral("mixdown"), StageState::Running, 0);
        emit jobProgress(m.id, tr("Lekeverés az átíráshoz…"));
        m_deps.mixdown->regenerate(meetingId);
        return;
    }
    transcribeFromMixdown(meetingId);
}

void Workflows::retranscribe(const QString& meetingId, bool keepBackup)
{
    const Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }
    if (m_deps.jobs->isRunning(meetingId, JobKind::Transcribe))
        return;   // a transcription is already running on this meeting
    // The current transcript (with the names and manual corrections) stays as a copy, if requested.
    if (keepBackup)
        editing::speakeredit::backupTranscript(m, m_deps.personNames ? m_deps.personNames() : PersonNames());
    // Both the speaker assignments (speakerMap) AND the manual line corrections (overlay) are cleared when the
    // NEW transcript arrives, not beforehand: the speakerMap by the transcription step, the overlay in the
    // transcriptReady hook. If the re-transcription is cancelled or fails, the old transcript stays untouched
    // with its names and per-line corrections.
    m_clearSpeakers.insert(meetingId);
    transcribe(meetingId);
}

void Workflows::transcribeFromMixdown(const QString& meetingId)
{
    // The step closes the job itself when it cannot start (unknown meeting / provider).
    m_deps.transcription->start(meetingId, m_clearSpeakers.contains(meetingId));
    m_clearSpeakers.remove(meetingId);
}

bool Workflows::wantsLocalDiarization(const QString& meetingId) const
{
    if (!m_deps.diarization || !m_deps.transcription) return false;
    if (m_deps.transcription->providerDiarized(meetingId)) return false;   // the provider separated the speakers
    const bool on = route(providers::Capability::Diarization, meetingId) == providers::Route::Local;
    return on && m_deps.localDiarizationUsable && m_deps.localDiarizationUsable();
}

// ==== processing routes (S3) ===============================================================================

QString Workflows::routeKey(providers::Capability cap, const QString& meetingId)
{
    return providers::capabilityKey(cap) + QLatin1Char('/') + meetingId;
}

void Workflows::setRouteOverride(providers::Capability cap, std::optional<providers::Route> r)
{
    if (r) m_routeOverride.insert(int(cap), *r);
    else m_routeOverride.remove(int(cap));
}

void Workflows::chooseRoute(providers::Capability cap, const QString& meetingId, providers::Route r)
{
    m_chosenRoutes.insert(routeKey(cap, meetingId), r);
}

void Workflows::clearChosenRoute(providers::Capability cap, const QString& meetingId)
{
    m_chosenRoutes.remove(routeKey(cap, meetingId));
}

providers::Route Workflows::baseRoute(providers::Capability cap) const
{
    if (const auto it = m_routeOverride.constFind(int(cap)); it != m_routeOverride.constEnd()) return *it;
    return m_deps.storedRoute ? m_deps.storedRoute(cap) : providers::Route::Local;
}

providers::Route Workflows::usable(providers::Capability cap, providers::Route r) const
{
    const bool dead = r == providers::Route::Ask
        || (r == providers::Route::Cloud && !(m_deps.cloudLive && m_deps.cloudLive()));
    if (!dead) return r;
    return m_deps.fallbackRoute ? m_deps.fallbackRoute(cap) : providers::Route::Local;
}

providers::Route Workflows::resolveRoute(providers::Capability cap, const QString& meetingId)
{
    const QString key = routeKey(cap, meetingId);
    const providers::Route base = baseRoute(cap);
    providers::Route r = base;
    if (const auto chosen = m_chosenRoutes.constFind(key); chosen != m_chosenRoutes.constEnd()) {
        r = *chosen;
        m_chosenRoutes.erase(chosen);
    } else if (base == providers::Route::Ask) {
        const std::optional<providers::Route> answer = m_deps.askRoute ? m_deps.askRoute(cap, meetingId) : std::nullopt;
        if (answer && *answer != providers::Route::Ask) {
            r = *answer;
        } else {
            r = usable(cap, providers::Route::Ask);
            qCInfo(lcApp).noquote() << QStringLiteral("Route %1 is \"ask\" but nobody answered: running %2 (%3)")
                                           .arg(providers::capabilityKey(cap), providers::routeKey(r), meetingId);
        }
    }
    const providers::Route out = usable(cap, r);
    if (out != r)
        qCInfo(lcApp).noquote() << QStringLiteral("Route %1: %2 is not available, running %3 (%4)")
                                       .arg(providers::capabilityKey(cap), providers::routeKey(r),
                                            providers::routeKey(out), meetingId);
    m_resolvedRoutes.insert(key, Resolved{out, base});
    return out;
}

providers::Route Workflows::route(providers::Capability cap, const QString& meetingId) const
{
    // A pending per-run choice (chooseRoute) is the route of the run about to start (the GUI's gate and
    // cost check read it before the start consumes it).
    if (const auto chosen = m_chosenRoutes.constFind(routeKey(cap, meetingId)); chosen != m_chosenRoutes.constEnd())
        return usable(cap, *chosen);
    const providers::Route base = baseRoute(cap);
    const auto it = m_resolvedRoutes.constFind(routeKey(cap, meetingId));
    if (it != m_resolvedRoutes.constEnd() && it->base == base) return usable(cap, it->route);
    return usable(cap, base);
}

void Workflows::transcribed(const QString& meetingId)
{
    // The provider gave no speakers (Whisper, a non-diarizing tier): the local diarization runs on the tracks
    // first (the "diarize" stage), then the chain goes on. Not started (no track, the transcript has speakers
    // after all, no diarizer): straight on.
    if (wantsLocalDiarization(meetingId)
        && m_deps.diarization->start(meetingId, [this, meetingId](const processing::DiarizationOutcome& o) {
               diarized(meetingId, o);
           }))
        return;
    continueAfterTranscript(meetingId);
}

void Workflows::diarized(const QString& meetingId, const processing::DiarizationOutcome& outcome)
{
    const bool manual = m_manualDiarize.remove(meetingId);
    if (outcome.cancelled) {
        // The transcript stays as the STT gave it; the job closes as cancelled (no identification).
        m_deps.jobs->cancelled(meetingId, JobKind::Transcribe);
        return;
    }
    if (!outcome.ok || !outcome.applied) {
        m_diarizeProblem.insert(meetingId, outcome.message.isEmpty() ? QStringLiteral("unknown") : outcome.message);
        if (!outcome.ok)
            qCWarning(lcVoice).noquote() << "Local diarization failed, the transcript keeps its speakers:" << outcome.message;
    } else {
        m_diarizeProblem.remove(meetingId);
    }
    // The manual action on a transcript that did not change has nothing to identify: the job just ends.
    if (manual && !(outcome.ok && outcome.applied)) {
        m_deps.jobs->finish(meetingId, JobKind::Transcribe);
        return;
    }
    if (outcome.ok && outcome.applied) {
        if (manual) m_deps.jobs->markIdentified(meetingId, false);   // the new speakers are not identified yet
        if (m_deps.voice.useDiarization) m_deps.voice.useDiarization(meetingId, outcome.result);
    }
    continueAfterTranscript(meetingId);
}

// ==== the manual "Beszélők felismerése" ====================================================================

Workflows::SpeakerStatus Workflows::speakerStatus(const QString& meetingId) const
{
    SpeakerStatus st;
    const Meeting m = m_deps.store ? m_deps.store->load(meetingId) : Meeting();
    if (m.id.isEmpty() || !m.hasTranscript || !m_deps.transcription) {
        st.cannotBecause = QStringLiteral("no_transcript");
        return st;
    }
    auto cached = m_speakersCache.constFind(meetingId);
    if (cached == m_speakersCache.constEnd()) {
        const MergedTranscript t = m_deps.transcription->currentTranscript(meetingId);
        QSet<QString> labels;
        for (const TranscriptToken& tok : t.tokens) labels.insert(tok.speaker);
        cached = m_speakersCache.insert(meetingId, {int(labels.size()), t.speakerSource});
    }
    st.speakers = cached->labels;
    st.hasNames = !m.speakerMap.isEmpty();
    st.speakerCount = m.diarizeSpeakerCount;
    st.problem = m_diarizeProblem.value(meetingId);

    const bool local = cached->source == processing::diarizationmerge::kSourceLocalDiarization;
    const bool separated = st.speakers > 1;
    const bool usable = m_deps.localDiarizationUsable && m_deps.localDiarizationUsable();
    const bool enabled = route(providers::Capability::Diarization, meetingId) == providers::Route::Local;
    const bool providerDiarizes = m_deps.transcription->providerDiarized(meetingId)
        || (m_deps.sttSeparatesSpeakers && m_deps.sttSeparatesSpeakers());
    if (local) st.source = QStringLiteral("local");
    else if (separated || providerDiarizes) st.source = QStringLiteral("provider");
    else st.source = QStringLiteral("none");
    if (st.source == QLatin1String("none"))
        st.blocker = !usable ? QStringLiteral("model") : !enabled ? QStringLiteral("disabled") : QString();

    if (m_deps.jobs && m_deps.jobs->isRunning(meetingId, JobKind::Transcribe)) st.cannotBecause = QStringLiteral("running");
    else if (!m_deps.diarization || !usable) st.cannotBecause = QStringLiteral("no_model");
    else if (processing::DiarizationService::requestFor(m).isEmpty()) st.cannotBecause = QStringLiteral("no_tracks");
    else if (separated && !local) st.cannotBecause = QStringLiteral("provider");
    st.canDiarize = st.cannotBecause.isEmpty();
    return st;
}

bool Workflows::diarizeSpeakers(const QString& meetingId, int speakerCount, QString* whyNot)
{
    const SpeakerStatus st = speakerStatus(meetingId);
    if (!st.canDiarize) {
        if (whyNot) *whyNot = st.cannotBecause;
        return false;
    }
    Meeting m = m_deps.store->load(meetingId);
    const int want = qMax(0, speakerCount);
    if (m.diarizeSpeakerCount != want) {
        m.diarizeSpeakerCount = want;
        m_deps.store->saveMeeting(m);
    }
    m_diarizeProblem.remove(meetingId);
    QVector<JobStage> stages;
    stages.append({QStringLiteral("diarize"), tr("Beszélők szétválasztása"), StageState::Waiting, -1, QString()});
    if (m_deps.voice.usable && m_deps.voice.usable())
        stages.append({QStringLiteral("identify"), tr("Résztvevők azonosítása"),
                       m_skipIdentify.contains(meetingId) ? StageState::Skipped : StageState::Waiting, -1, QString()});
    m_deps.jobs->begin(meetingId, JobKind::Transcribe, tr("Beszélők felismerése"), stages);
    m_manualDiarize.insert(meetingId);
    if (!m_deps.diarization->start(meetingId, [this, meetingId](const processing::DiarizationOutcome& o) {
            diarized(meetingId, o);
        }, /*redo*/ true)) {
        m_manualDiarize.remove(meetingId);
        m_deps.jobs->cancelled(meetingId, JobKind::Transcribe);
        if (whyNot) *whyNot = QStringLiteral("not_started");
        return false;
    }
    return true;
}

void Workflows::continueAfterTranscript(const QString& meetingId)
{
    // Voice ID: auto-matching the diarized speakers against the voiceprint DB, on a background thread, as
    // the last stage of the transcription task (the UI stays usable, cancellable). (If the user skipped it
    // with the toggle, the transcript finishes with anonymous speakers.) The "Who was there?" decision
    // (approval / "only me") was made before the transcript: we apply that, and automatic identification is
    // skipped. Otherwise the candidates' binding + identification.
    const VoiceSteps& v = m_deps.voice;
    const bool decided = v.applyParticipants && v.applyParticipants(meetingId);
    if (decided && v.usable && v.usable())
        m_deps.jobs->setStage(meetingId, JobKind::Transcribe, QStringLiteral("identify"), StageState::Skipped);
    if (decided || m_skipIdentify.contains(meetingId)
        || !(v.startIdentify && v.startIdentify(meetingId, /*asStage*/ true)))
        m_deps.jobs->finish(meetingId, JobKind::Transcribe);
}

bool Workflows::transcriptionInMixdown(const QString& meetingId) const { return m_mixWaits.contains(meetingId); }

bool Workflows::identify(const QString& meetingId)
{
    if (m_deps.jobs->isRunning(meetingId, JobKind::Transcribe))
        return false;   // the transcription identifies by itself at the end
    // The matches come back on VoiceAnalysisService::speakersIdentified (committed by the participant service).
    return m_deps.voice.startIdentify && m_deps.voice.startIdentify(meetingId, /*asStage*/ false);
}

void Workflows::setIdentifyAfterTranscription(const QString& meetingId, bool enabled)
{
    if (enabled) m_skipIdentify.remove(meetingId);
    else         m_skipIdentify.insert(meetingId);
}

bool Workflows::identifyAfterTranscription(const QString& meetingId) const
{
    return !m_skipIdentify.contains(meetingId);
}

// ==== cancel ================================================================================================

bool Workflows::cancel(const QString& meetingId, JobKind kind)
{
    auto cancelIdentify = [this, &meetingId](JobKind k) {
        return m_deps.voice.cancelIdentify && m_deps.voice.cancelIdentify(meetingId, k);
    };
    switch (kind) {
    case JobKind::Transcribe: {
        if (m_deps.diarization && m_deps.diarization->running(meetingId)) {
            // Diarization step: the worker stops; its outcome (cancelled) closes the job.
            m_deps.jobs->setCancelling(meetingId, kind);
            return m_deps.diarization->cancel(meetingId);
        }
        const auto wait = m_mixWaits.find(meetingId);
        const bool stt = m_deps.transcription && m_deps.transcription->running(meetingId);
        if (wait == m_mixWaits.end() && !stt) {
            // The transcript is already done, the last (identification) stage is running, so only that one is skipped.
            return cancelIdentify(kind);
        }
        m_deps.jobs->setCancelling(meetingId, kind);
        if (wait != m_mixWaits.end()) {
            // Mixdown step: we break the chain; the ffmpeg we started ourselves is stopped (the half-made file is
            // deleted, the earlier mix and the tracks are untouched).
            if (wait->conn) QObject::disconnect(*wait->conn);
            const bool owns = wait->ownsMixdown;
            m_mixWaits.erase(wait);
            m_clearSpeakers.remove(meetingId);
            if (owns) m_deps.mixdown->cancelProcess(meetingId);
            m_deps.jobs->cancelled(meetingId, kind);
            return true;
        }
        // STT step: the provider job cleans up; its failed("cancelled") closes the job (without an error).
        return m_deps.transcription->cancel(meetingId);
    }
    case JobKind::Summarize:
    case JobKind::ExtractTopics:
    case JobKind::AnalyzeTopics:
        return m_deps.summarization && m_deps.summarization->cancel(meetingId, kind);   // the LLM call / topic batch
    case JobKind::Mixdown:
        return m_deps.mixdown && m_deps.mixdown->cancel(meetingId);
    case JobKind::Import:
        return m_deps.import && m_deps.import->cancel(meetingId);
    case JobKind::Export:
        return m_deps.archive && m_deps.archive->cancel(meetingId);
    case JobKind::Identify:
        return cancelIdentify(kind);   // the thread stops before the next speaker
    }
    return false;
}

void Workflows::cancelAll(const QString& meetingId)
{
    for (JobKind k : {JobKind::Transcribe, JobKind::Summarize, JobKind::ExtractTopics,
                      JobKind::AnalyzeTopics, JobKind::Mixdown, JobKind::Identify})
        cancel(meetingId, k);
}

// ==== summarize -> tags =====================================================================================

void Workflows::summarize(const QString& meetingId)
{
    resolveRoute(providers::Capability::Llm, meetingId);   // Ask is answered here, once per run
    if (m_deps.summarization) m_deps.summarization->summarize(meetingId);
}

void Workflows::summaryReady(const QString& meetingId)
{
    if (m_deps.tagSuggestions) m_deps.tagSuggestions->summaryReady(meetingId);
}

// ==== after a recording / an import =========================================================================

void Workflows::afterRecording(const Meeting& m)
{
    // The mixdown is decoupled from stop(): we start it here ASYNCHRONOUSLY, only if the setting is "auto".
    // It does not block, so a new recording can start at once. In manual mode the user starts it with the
    // review panel's "Mix" button. (The mixdown is needed only for listening; for transcription the per-track
    // files suffice.)
    // In the standalone recorder process (setAutoMixdown(false)) it does not start: that process exits after
    // recording, and the analyzer makes the mix when needed.
    // Before it, the tracks' speech check (on a background thread): a speechless track is left out of the mix
    // (Track::excludedReason "noSpeech"); the mix starts at the end of the measurement.
    if (!m_autoMixdown) return;
    const bool mix = !mixdownManual() && m.mixdownFile.isEmpty();
    if (editing::trackspeech::needsSpeechCheck(m)) startSpeechCheck(m.id, mix);
    else if (mix) m_deps.mixdown->regenerate(m.id);
    // The pre-transcript voice analysis from the raw tracks, right after the recording (if the speech
    // measurement is still pending, its end starts it).
    analyzeParticipants(m.id);
}

void Workflows::afterImport(const Meeting& m)
{
    // Mixdown: exactly as at the end of a recording: first the tracks' speech check (a speechless track is
    // left out of the mix).
    if (!m_autoMixdown) return;
    const bool mix = !mixdownManual();
    if (editing::trackspeech::needsSpeechCheck(m)) startSpeechCheck(m.id, mix);
    else if (mix) m_deps.mixdown->regenerate(m.id);
}

void Workflows::mixdownProgress(const QString& meetingId, int percent)
{
    // (100 is only the UI's "done" tick; the stage is closed by the transcription chain.)
    if (percent < 100 && transcriptionInMixdown(meetingId))
        m_deps.jobs->setStage(meetingId, JobKind::Transcribe, QStringLiteral("mixdown"), StageState::Running, percent);
}

void Workflows::mixdownRunEnded(const QString& meetingId, bool ok)
{
    // The pre-transcript voice analysis works from the raw tracks, independent of the mixdown; we try here
    // only because a mix can also run after a track change (an excluded track).
    if (ok) analyzeParticipants(meetingId);
}

void Workflows::startSpeechCheck(const QString& meetingId, bool thenMixdown)
{
    if (m_speechCheckRunning.contains(meetingId)) return;
    const Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) return;
    if (!m_deps.voice.measureActivity) return;
    m_speechCheckRunning.insert(meetingId);
    // The measurement runs on the voice analysis's pool (cache, else computed and saved).
    m_deps.voice.measureActivity(m, [this, meetingId, thenMixdown](const MeetingActivity& act) {
        finishSpeechCheck(meetingId, act, thenMixdown);
    });
}

void Workflows::finishSpeechCheck(const QString& meetingId, const MeetingActivity& activity, bool thenMixdown)
{
    m_speechCheckRunning.remove(meetingId);
    m_speechChecked.insert(meetingId);   // on error (an undecodable track) we do not repeat either
    Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) return;
    const editing::trackspeech::SpeechCheckResult r = editing::trackspeech::applySpeechRatios(m, activity);
    if (r.changed()) {
        if (!r.excluded.isEmpty()) {
            m.mixdownDirty = true;
            qCInfo(lcAudio).noquote() << "Beszéd nélküli sáv kimarad a lekeverésből:"
                                      << r.excluded.join(QStringLiteral(", "));
            // The full measurement becomes the speech strip's cache (so the excluded track shows too and need not be
            // decoded again); the analysis's cache is that of the included tracks.
            activity.save(m.folder, MeetingActivity::viewFilePath(m.folder));
            // The track-activity cache belongs to the included tracks: we save it again without the excluded ones.
            MeetingActivity kept;
            kept.fingerprint = processing::trackactivity::activityFingerprint(m, m.folder);
            for (const TrackActivity& t : activity.tracks)
                if (!r.excluded.contains(t.trackId)) kept.tracks.append(t);
            if (!kept.isEmpty()) kept.save(m.folder);
        }
        m_deps.store->saveMeeting(m);
    }
    // We signal even if nothing changed: the view leaves the "measuring" state only on this.
    emit tracksChanged(meetingId);
    if (thenMixdown) m_deps.mixdown->regenerate(meetingId);
    // The speechless tracks are filtered out: the voice analysis can go on over the remaining raw tracks.
    if (m_autoMixdown) analyzeParticipants(meetingId);
}

bool Workflows::speechCheckRunning(const QString& meetingId) const { return m_speechCheckRunning.contains(meetingId); }

bool Workflows::speechCheckPending(const Meeting& m) const
{
    // The measurement is running (its end calls again), or the speechless tracks are not filtered out yet.
    return m_speechCheckRunning.contains(m.id)
        || (!m.hasTranscript && editing::trackspeech::needsSpeechCheck(m) && !m_speechChecked.contains(m.id));
}

bool Workflows::checkTrackSpeech(const QString& meetingId)
{
    if (m_speechCheckRunning.contains(meetingId) || m_speechChecked.contains(meetingId)) return false;
    const Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty() || !editing::trackspeech::needsSpeechCheck(m)) return false;
    startSpeechCheck(meetingId, /*thenMixdown*/ false);
    return true;
}

// ==== track inclusion =======================================================================================

namespace {

// The index of the track's segment leader (a split track's parts move together); -1 if not found.
int segmentLeader(const QVector<Track>& tracks, const QVector<library::tracknames::SegmentInfo>& seg,
                  const QString& trackId)
{
    int lead = -1;
    for (int i = 0; i < tracks.size(); ++i)
        if (tracks.at(i).id == trackId) lead = seg.value(i).leader >= 0 ? seg.value(i).leader : i;
    return lead;
}

} // namespace

void Workflows::includeTrack(const QString& meetingId, const QString& trackId)
{
    editing::ParticipantUndoScope undoScope(m_deps.undo, meetingId, tr("Sáv beemelve"));
    Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) return;
    const QVector<library::tracknames::SegmentInfo> seg = library::tracknames::segments(m.tracks);
    const int lead = segmentLeader(m.tracks, seg, trackId);
    if (lead < 0) return;
    bool changed = false;
    for (int i = 0; i < m.tracks.size(); ++i) {
        const int l = seg.value(i).leader >= 0 ? seg.value(i).leader : i;
        Track& t = m.tracks[i];
        if (l != lead || t.included()) continue;
        t.active = true;
        t.excludedReason.clear();
        changed = true;
    }
    if (!changed) return;
    m.mixdownDirty = true;
    m_deps.store->saveMeeting(m);
    emit tracksChanged(meetingId);
    m_deps.mixdown->regenerate(meetingId);
}

void Workflows::excludeTrack(const QString& meetingId, const QString& trackId)
{
    editing::ParticipantUndoScope undoScope(m_deps.undo, meetingId, tr("Sáv kivéve"));
    Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) return;
    const QVector<library::tracknames::SegmentInfo> seg = library::tracknames::segments(m.tracks);
    const int lead = segmentLeader(m.tracks, seg, trackId);
    if (lead < 0) return;
    bool changed = false;
    for (int i = 0; i < m.tracks.size(); ++i) {
        const int l = seg.value(i).leader >= 0 ? seg.value(i).leader : i;
        Track& t = m.tracks[i];
        if (l != lead || t.excludedReason == editing::trackspeech::kManual) continue;
        t.active = false;
        t.excludedReason = editing::trackspeech::kManual;
        changed = true;
    }
    if (!changed) return;
    m.mixdownDirty = true;
    m_deps.store->saveMeeting(m);
    emit tracksChanged(meetingId);
    bool anyIncluded = false;
    for (const Track& t : m.tracks) anyIncluded = anyIncluded || t.included();
    if (anyIncluded) m_deps.mixdown->regenerate(meetingId);
}

} // namespace tanara::app
