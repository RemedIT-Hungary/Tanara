#pragma once
//
// VoiceAnalysisService: the voice-analysis steps of a meeting, moved unchanged from AppController
// (cleanup phase 2, P12d). It owns the speaker models in use (VoiceEmbedderSet), and the background work:
//  - the lazy voiceprint backfill (1-thread pool): every sample of every person that has no voiceprint
//    yet for a model in use gets one computed from its audio;
//  - the analysis pool (2 threads): the per-track activity (TrackActivity: the speech strip, the speech
//    check's measurement) and the pre-transcript participant analysis (ParticipantAnalysis: speech
//    windows -> embeddings -> clusters -> candidates with evidence);
//  - the voice identification of a transcript's anonymous speakers (a background thread per meeting;
//    the Identify job, or the last stage of a transcription), and its synchronous counterparts;
//  - voiceprint enrollment from a transcript speaker or from an arbitrary audio span.
// Matching voices to people goes through the IParticipantIdentifier port's local rule
// (LocalParticipantIdentifier::identifyNow, the same rule as VoiceprintStore::bestMatch) with the
// voiceprint store's people as the candidates.
//
// What it does not do: writing the identification into the meeting / overlay (that is
// editing::ParticipantService::commitIdentification: the speakersIdentified signal hands the matches over),
// the speech check's track verdicts and mixdown (the AppController facade, P12f), the transcript editors.
// The AppController facade delegates here and re-emits the signals.
#include "tanara/domain/Activity.h"
#include "tanara/domain/Job.h"
#include "tanara/domain/Meeting.h"
#include "tanara/domain/Overlay.h"
#include "tanara/domain/Participant.h"
#include "tanara/domain/Transcript.h"
#include "tanara/domain/Voice.h"
#include "tanara/media/MediaTool.h"
#include "tanara/processing/analysis/ParticipantAnalysis.h"
#include "tanara/processing/analysis/UtteranceEmbeddings.h"
#include "tanara/processing/voiceid/BackfillReport.h"
#include "tanara/processing/voiceid/VoiceEmbedderSet.h"
#include "tanara/providers/voice/IDiarizer.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace tanara::library {
class MeetingJobTracker;
class MeetingRepository;
class PeopleStore;
class TagService;
class VoiceprintStore;
}

namespace tanara::processing {

class VoiceAnalysisService : public QObject {
    Q_OBJECT
public:
    // Cosine threshold of a voice match (validation: same 0.77, cross <= 0.35).
    static constexpr double kVoiceMatchThreshold = 0.5;

    struct Deps {
        library::MeetingRepository* store = nullptr;
        library::VoiceprintStore*   voiceprints = nullptr;
        library::PeopleStore*       people = nullptr;
        library::TagService*        tags = nullptr;        // tag evidence of the candidates
        library::MeetingJobTracker* jobs = nullptr;        // the Identify job / the transcription's identify stage
        media::MediaToolPtr         media;
        QString modelsDir;          // AppPaths::modelsDir()
        QString appDir;             // location of the packaged models (<appDir>/models)
        // The voice models enabled in the settings.
        std::function<QStringList()> enabledModels;
        // The user's own person (people.json "self" + the settings' name).
        std::function<SelfPerson()> self;
        // The transcript's lines (transcript.segments.json; the editing module's reader).
        std::function<QVector<TranscriptLine>(const QString& meetingFolder)> transcriptLines;
        // The transcript's identity at a moment (the identification drops its result if it changed meanwhile).
        std::function<QString(const QString& meetingId, const QString& meetingFolder)> transcriptStamp;
        // The tracks' speech check is still pending for the meeting (the analysis waits for it).
        std::function<bool(const Meeting& m)> speechCheckPending;
    };

    // What applyModels changed.
    struct ModelChange {
        bool changed = false;   // the set of models in use changed (or this was the first application)
        bool first = false;     // the first application
        bool grew = false;      // a new model joined
    };

    explicit VoiceAnalysisService(const Deps& deps, QObject* parent = nullptr);
    ~VoiceAnalysisService() override;

    // Stops the background work: the queued tasks are skipped, the running ones exit at the next item; waits.
    // (Called by the owner before the stores go away; the destructor calls it too.)
    void shutdown();

    // ---- the speaker models in use ----
    // Recomputes the set from the enabled models (intersected with the existing model files).
    ModelChange applyModels();
    // Test seam: the loader of the models (a fake "model" without ONNX). The set is rebuilt at the next
    // applyModels (as a first application: no backfill).
    void setLoader(PcmEmbedderLoader loader);
    QStringList modelIds() const;                   // alphabetical
    bool usable() const;                            // a usable model exists
    VoiceEmbedderSet embedderSet() const;           // an unloaded copy
    UtteranceEmbedderFactory utteranceEmbedderFactory() const;   // empty without a usable model
    // The editor's model list: the models in use; if none is usable, the enabled ones.
    QStringList editorModelIds() const;
    // Expected location of the base model (diagnostics).
    QString defaultModelPath() const;

    // ---- the candidates' context (evidence, ranking) ----
    participants::CandidateContext candidateContext() const;
    // Test seam: a person's manual tags instead of the TagService's.
    void setPersonTagsProvider(std::function<QStringList(const PersonId&)> provider);
    std::function<QStringList(const PersonId&)> personTagsProvider() const;

    // ---- voiceprint backfill ----
    void backfill();
    bool backfillRunning() const;
    QVector<VoiceprintBackfillSkip> backfillSkipped() const;   // sorted by person

    // ---- voiceprints from audio ----
    // A transcript speaker's voiceprint (from the meeting's mixdown, else the label's track). true: recorded
    // (voiceprintsChanged emitted).
    bool enrollSpeaker(const QString& meetingId, const QString& rawLabel, const PersonId& person);
    // An arbitrary span ([startMs,endMs] in meeting time) of a track, for a chosen person (created for
    // PersonChoice::newName). The person (null: nothing recorded); voiceprintsChanged + peopleChanged.
    PersonId enrollFromSample(const PersonChoice& person, const QString& meetingId, const QString& trackId,
                              qint64 startMs, qint64 endMs);

    // ---- matching ----
    // The best match of ONE transcript speaker's voice (modifies nothing; {null, -1} without model / match).
    VoiceMatch testSpeakerMatch(const QString& meetingId, const QString& rawLabel);
    // The still anonymous speakers of the transcript matched synchronously (onProgress(done, total):
    // false cancels; the matches found so far are returned).
    QVector<SpeakerVoiceMatch> identifyTranscriptSpeakers(const QString& meetingId,
                                                          const std::function<bool(int, int)>& onProgress = {});
    // PRE-TRANSCRIPT participant guesses: windowed sampling per track, clustering, matching (synchronous).
    QVector<ParticipantGuess> identifyParticipants(const QString& meetingId,
                                                   const std::function<bool(int, int)>& onProgress = {});

    // ---- asynchronous identification of the transcript's anonymous speakers ----
    // asStage: the last ("identify") stage of the meeting's Transcribe job; otherwise its own Identify job.
    // The matches go out on speakersIdentified (on the main thread), then the job closes.
    // false: running already / no model / no transcript / nothing to identify.
    bool startIdentify(const QString& meetingId, bool asStage);
    bool identifyRunning(const QString& meetingId) const;
    // Cancels the run of the given kind: Identify (an own job) or Transcribe (the stage). true if there was one.
    // Matches already found are saved, the other speakers stay anonymous.
    bool cancel(const QString& meetingId, JobKind kind);

    // ---- track activity (the speech strip) ----
    MeetingActivity trackActivity(const QString& meetingId);
    bool trackActivityComputing(const QString& meetingId) const;
    // The activity of the meeting's active tracks (cache, else computed and saved) on the analysis pool;
    // `done` runs on the main thread (not if the service is gone).
    void measureActivity(const Meeting& m, std::function<void(const MeetingActivity&)> done);

    // ---- pre-transcript participant analysis ----
    bool analyzeParticipants(const QString& meetingId);
    bool maybeAnalyzeParticipants(const QString& meetingId);
    bool analysisRunning(const QString& meetingId) const;
    // The running analysis's result is dropped (it still finishes).
    void cancelAnalysis(const QString& meetingId);
    // The local diarization's speakers (D4) as the analysis's clusters: per diarized speaker one cluster (its
    // turns as the windows, its centroid, its sample re-pointed to the track's file); a running window
    // analysis is dropped. Then exactly what a finished analysis does: candidates (participants.analysis.json,
    // meeting.participants), bound to the transcript's raw speakers, participantsChanged. Speakers without a
    // centroid (a hand-bound track's) are skipped. false: unknown meeting, a decision was already made
    // (approval), or no usable speaker.
    bool useDiarization(const QString& meetingId, const providers::DiarizeResult& result);

signals:
    void voiceprintsChanged();
    void peopleChanged();
    void backfillStarted(int total);
    void backfillProgress(int done, int total);
    void backfillFinished(int added, int skipped);
    void participantAnalysisStarted(QString meetingId);
    void participantAnalysisFinished(QString meetingId);
    void participantAnalysisProgress(QString meetingId, int tracksDone, int tracksTotal, int percent);
    void participantsChanged(QString meetingId);
    void trackActivityReady(QString meetingId);
    // The asynchronous identification's matches (before the job closes); the facade commits them.
    void speakersIdentified(QString meetingId, QVector<tanara::SpeakerVoiceMatch> matches);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace tanara::processing
