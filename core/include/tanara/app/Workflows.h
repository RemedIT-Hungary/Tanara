#pragma once
//
// app::Workflows: the processing chains (cleanup phase 2, P12f), moved unchanged from AppController.
//
//  - transcribe: a small state machine over the meeting's Transcribe job (MeetingJobTracker holds the
//    state the UI sees):
//        [mixdown]  only if the mix is missing / stale (recording::MixdownService, the job's "mixdown" stage)
//      -> stt       processing::TranscriptionService (upload / transcribe / diarize stages)
//      -> identify  the "Who was there?" decision is applied, otherwise the automatic voice identification
//                   as the job's last stage (skippable per meeting: setIdentifyAfterTranscription)
//      -> the job finishes (the identification closes it when it runs).
//    Each step is cancellable (cancel(meetingId, JobKind::Transcribe) stops the step that runs: the
//    transcription's own ffmpeg, the provider job, or the identification). A failed step closes the job
//    with the error; the earlier transcript stays untouched. A new transcribe() restarts from the top.
//  - summarize -> tags: the summary's arrival starts the LLM tag suggestions.
//  - after a recording / an import: the tracks' speech check (a speechless track is left out of the
//    mix) -> mixdown (automatic mode) -> the pre-transcript participant analysis.
//  - the per-kind cancel of every job (cancelJob's remaining switch).
// The identification / participant side is injected (VoiceSteps), so the chain is testable on its own.
#include "tanara/domain/Activity.h"
#include "tanara/domain/Job.h"
#include "tanara/domain/Meeting.h"
#include "tanara/domain/Person.h"
#include "tanara/providers/core/ReadinessModel.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>
#include <memory>

namespace tanara::library {
class ArchiveService;
class ImportService;
class MeetingJobTracker;
class MeetingRepository;
class TagSuggestionService;
}
namespace tanara::recording { class MixdownService; }
namespace tanara::processing {
class SummarizationService;
class TranscriptionService;
}
namespace tanara::editing { class MeetingUndoRegistry; }

namespace tanara::app {

class Workflows : public QObject {
    Q_OBJECT
public:
    // The voice identification and the participants' side of the chains (processing::VoiceAnalysisService +
    // editing::ParticipantService in the app; fakes in the tests). Every hook may be empty.
    struct VoiceSteps {
        std::function<bool()> usable;                                         // a usable voice model exists
        std::function<bool(const QString& meetingId, bool asStage)> startIdentify;
        std::function<bool(const QString& meetingId, JobKind kind)> cancelIdentify;
        // The new transcript: binding + the "Who was there?" decision. true: the decision covered the speakers.
        std::function<bool(const QString& meetingId)> applyParticipants;
        std::function<bool(const QString& meetingId)> analyzeParticipants;   // if not run on these tracks yet
        // The tracks' activity (cache, else computed) on a background pool; `done` on the main thread.
        std::function<void(const Meeting& m, std::function<void(const MeetingActivity&)> done)> measureActivity;
    };

    struct Deps {
        library::MeetingRepository*      store = nullptr;
        library::MeetingJobTracker*      jobs = nullptr;
        recording::MixdownService*       mixdown = nullptr;
        processing::TranscriptionService* transcription = nullptr;
        VoiceSteps voice;
        // Whether a step can run on the meeting (provider, key, Tanara Cloud blockers).
        std::function<providers::ReadinessResult(providers::WorkflowStep step, const QString& meetingId)> readiness;
        std::function<PersonNames()> personNames;      // the re-transcription's backup copy
        std::function<QString()> mixdownMode;          // the recording.mixdownMode setting ("manual" = no auto mix)
        editing::MeetingUndoRegistry* undo = nullptr;  // the track inclusion's shared undo step
        // The other jobs (cancel per kind; summarize -> tags). May be null.
        processing::SummarizationService* summarization = nullptr;
        library::TagSuggestionService*    tagSuggestions = nullptr;
        library::ImportService*           import = nullptr;
        library::ArchiveService*          archive = nullptr;
    };

    explicit Workflows(Deps deps, QObject* parent = nullptr);
    ~Workflows() override;

    // ---- transcribe -> (mixdown) -> STT -> identify ----
    void transcribe(const QString& meetingId);
    // The old speaker names / corrections go with the NEW transcript (not before); keepBackup: a copy first.
    void retranscribe(const QString& meetingId, bool keepBackup);
    // The transcription waits for its mixdown (the mix runs as the job's stage, not as its own task).
    bool transcriptionInMixdown(const QString& meetingId) const;
    // A standalone identification (its own Identify job); false while the transcription runs (it identifies
    // at its end), or when nothing can be identified.
    bool identify(const QString& meetingId);
    void setIdentifyAfterTranscription(const QString& meetingId, bool enabled);
    bool identifyAfterTranscription(const QString& meetingId) const;

    // ---- cancel ----
    bool cancel(const QString& meetingId, JobKind kind);
    void cancelAll(const QString& meetingId);

    // ---- summarize -> tags ----
    void summarize(const QString& meetingId);
    // A new summary arrived (the facade's summaryReady): the LLM tag suggestions follow.
    void summaryReady(const QString& meetingId);

    // ---- after a recording / an import: speech check -> mixdown -> participant analysis ----
    // false: the standalone process makes no mix and no analysis (the analyzer makes them when needed).
    void setAutoMixdown(bool on) { m_autoMixdown = on; }
    bool autoMixdown() const { return m_autoMixdown; }
    void afterRecording(const Meeting& m);
    void afterImport(const Meeting& m);
    // The facade's hooks of the mixdown signals (after it re-emitted them).
    void mixdownProgress(const QString& meetingId, int percent);
    void mixdownRunEnded(const QString& meetingId, bool ok);
    // The per-track speech share (once per process): false if not needed / running / done already.
    bool checkTrackSpeech(const QString& meetingId);
    bool speechCheckRunning(const QString& meetingId) const;
    // The measurement runs, or the speechless tracks are not filtered out yet (the analysis waits for it).
    bool speechCheckPending(const Meeting& m) const;

    // ---- track inclusion (one shared undo step, then a remix) ----
    void includeTrack(const QString& meetingId, const QString& trackId);
    void excludeTrack(const QString& meetingId, const QString& trackId);

signals:
    void jobProgress(QString meetingId, QString message);
    void tracksChanged(QString meetingId);
    void errorOccurred(QString message);

private:
    void transcribeFromMixdown(const QString& meetingId);
    void transcribed(const QString& meetingId);
    void startSpeechCheck(const QString& meetingId, bool thenMixdown);
    void finishSpeechCheck(const QString& meetingId, const MeetingActivity& activity, bool thenMixdown);
    bool analyzeParticipants(const QString& meetingId);
    bool mixdownManual() const;

    // The transcription's mixdown step: the chain waits for the mix's result.
    struct MixWait {
        std::shared_ptr<QMetaObject::Connection> conn;
        bool ownsMixdown = false;   // this transcription started the mix (a cancel stops its ffmpeg)
    };

    Deps m_deps;
    QHash<QString, MixWait> m_mixWaits;
    QSet<QString> m_clearSpeakers;          // re-transcription: the old names go with the new transcript
    QSet<QString> m_skipIdentify;           // the user TURNED OFF identification after transcription (per process)
    QSet<QString> m_speechCheckRunning;     // meetingIds being measured now
    QSet<QString> m_speechChecked;          // measured in this process (not repeated even on error)
    bool m_autoMixdown = true;
};

} // namespace tanara::app
