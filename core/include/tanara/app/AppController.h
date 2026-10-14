#pragma once
//
// AppController: the ONE object the UI binds to (the QML view models, the CLI).
// Wires the modules together: settings, devices, recording, store, STT, LLM, summary.
// RULE: no Widgets dependency here either; a plain QObject API (signals/slots + value DTOs).
#include "tanara/domain/Types.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/providers/core/ReadinessModel.h"
#include "tanara/editing/session/SpeakerEditTypes.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/domain/Job.h"
#include "tanara/library/summary/SummaryRepository.h"
#include "tanara/library/import/AudioImporter.h"
#include "tanara/domain/Tag.h"
#include "tanara/processing/analysis/UtteranceEmbeddings.h"
#include "tanara/processing/voiceid/VoiceEmbedderSet.h"
#include "tanara/processing/analysis/ParticipantAnalysis.h"
#include "tanara/processing/analysis/TrackActivity.h"
#include "tanara/processing/voiceid/BackfillReport.h"
#include "tanara/media/MediaTool.h"
#include <QObject>
#include <QVector>
#include <memory>
#include <functional>

namespace tanara::recording { class DeviceManager; }
namespace tanara::library { class MeetingRepository; }
namespace tanara::library { class TranscriptRepository; }
namespace tanara::library { class UiStateRepository; }
namespace tanara::library { class VoiceprintStore; }
namespace tanara::editing { class SpeakerEditor; }
namespace tanara::cloud { class CloudAccount; struct CloudRun; }
namespace tanara::providers { struct ProviderContext; class ProviderRegistry; }
namespace tanara::library { class MeetingJobTracker; }
namespace tanara::library { class MeetingLibrary; }
namespace tanara::library { class TrackCatalog; }
namespace tanara::library { class WaveformService; }
namespace tanara::library { class PeopleService; }
namespace tanara::library { class PeopleStats; }
namespace tanara::library { class TagService; }
namespace tanara::library { class MeetingProfiles; }
namespace tanara::library { class EmbeddingIndex; }
namespace tanara::library { class EmbeddingPreparer; }
namespace tanara::recording { class AudioEngine; }
namespace tanara::editing { class MeetingUndoService; }

namespace tanara::app {

class SettingsManager;

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(tanara::RecordingState recordingState READ recordingState NOTIFY recordingStateChanged)
public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    // Sub-components (the UI can bind models/views to these).
    SettingsManager* settings() const;
    recording::DeviceManager*   devices() const;
    library::MeetingRepository*    store() const;
    // The transcript files (+ cached facts, invalidated on transcriptReady / speakerMapChanged) and
    // the editor's per-meeting view state. (P15 moves these getters to Services.)
    library::TranscriptRepository* transcripts() const;
    library::UiStateRepository*    uiState() const;
    library::VoiceprintStore* voiceprints() const;   // voiceprint DB (voice ID)

    // ---- backend of the redesigned main window (structured layer) -------------------------
    // Per-meeting processing state: running jobs (phases, real progress), persistent errors,
    // derived icon states. See library/jobs/MeetingJobTracker.h.
    library::MeetingJobTracker* jobs() const;
    // Library sidebar queries: date sections, search, filters, "Waiting for you".
    library::MeetingLibrary*    library() const;
    // "Tracks" tab: friendly names, rename, locate a missing file, delete dropped tracks.
    library::TrackCatalog*      tracks() const;
    // Waveform peaks (asynchronous, cached in the meeting folder).
    library::WaveformService*   waveforms() const;
    // Audio file import engine (file info: probeAsync; start: importAudio).
    library::AudioImporter*     importer() const;
    // The media port (ffmpeg / ffprobe, resolved once at startup from settings.media).
    media::MediaToolPtr         mediaTool() const;
    // Convenience: jobs()->state(meetingId).
    MeetingProcessingState processingState(const QString& meetingId) const;

    // The meeting's summary in STRUCTURED form + its provenance (when, which provider/model,
    // quick or per-topic). For an old (summary.md only) summary the structure is parsed from the
    // markdown (fromMarkdown=true). exists=false if there is no summary.
    library::SummaryDocument summaryDocument(const QString& meetingId) const;

    // The meeting's (edited) topic list from disk (summary.topics.json), without an LLM call.
    QVector<tanara::SummaryTopic> meetingTopics(const QString& meetingId) const;
    // Per-topic state, queryable AT ANY TIME (not only from transient signals): in topic-list
    // order waiting / queued / running / done / failed (with message; a failure survives a
    // restart). Assembled from the analyses on disk, the running queue and the remembered failures.
    QVector<tanara::TopicStatus> topicStatuses(const QString& meetingId) const;

    RecordingState recordingState() const;
    QString currentMeetingFolder() const;   // the folder currently or last recorded

    // ---- Tanara Cloud ----------------------------------------------------------------
    // The account (always exists: the teaser-mode waitlist goes through it too). See CloudAccount.h.
    cloud::CloudAccount* cloud() const;
    // The sign-in cloud mode is live: compiled in (TANARA_BUILD_CLOUD) AND enabled
    // (settings.cloud.enabled, or TANARA_CLOUD=live). TANARA_CLOUD=off disables everything.
    bool cloudLive() const;
    // The "Coming soon" (waitlist) panel is visible: compiled in (TANARA_CLOUD_TEASER) and the cloud is not live yet.
    bool cloudTeaser() const;
    // This controller's provider registry (every capability; providers/core/ProviderRegistry.h).
    providers::ProviderRegistry& providerRegistry() const;
    // The selected provider of the given step is Tanara Cloud.
    bool usesCloud(providers::WorkflowStep step) const;
    // The model id actually sent (the Expert model, otherwise the tier's virtual name).
    QString cloudModelFor(providers::WorkflowStep step) const;
    // Builds an estimate request for a meeting (task: transcribe | summarize; summaryMode:
    // quick | complex). The LLM part is described process-independently, in call groups (llm_calls).
    cloud::EstimateRequest makeEstimateRequest(const QString& meetingId, const QString& task,
                                        const QString& summaryMode) const;

    // Gates a workflow step from a SINGLE source of truth (ReadinessModel): whether it can run
    // on the given meeting and, if not, EXACTLY what is missing. The GUI button enabling and the
    // guards here use the same thing. Headless (no UI).
    providers::ReadinessResult canRun(providers::WorkflowStep step, const QString& meetingId) const;

    // NAMES of the devices used last (at the latest recording); the UI should pre-check
    // these at startup. Persistent (<state>/state.json, AppPaths).
    QStringList lastUsedDeviceNames() const;

    // ---- recorder (floating recorder window) -------------------------------------------------
    // Device names of the running recording's LIVE (open) tracks (in track order), and of the
    // tracks detached during recording (safely closed). A track switched off by the user is in
    // neither. Empty outside a recording.
    QStringList recordingDeviceNames() const;
    QStringList disconnectedRecordingDeviceNames() const;

    // ---- backend of the People window ---------------------------------------------------------
    // People, nicknames / notes, voice samples, merge, delete, undo; see library/people/PeopleService.h.
    // Per-person statistics (meeting count, speaking time, last seen) are computed on a
    // background thread: library/people/PeopleStats.h.
    library::PeopleService* peopleService() const;
    library::PeopleStats*   peopleStats() const;
    // Expected location of the BASE model (CAM++ ONNX), for diagnostics; usability is
    // voiceIdentificationAvailable().
    QString voiceModelPath() const;

    // ---- speaker models (voice embedding) --------------------------------------------
    // Models in use: the "voiceModels" list of the settings, intersected with the existing model
    // files (VoiceModelRegistry::active), alphabetical. Recomputed when the setting changes
    // (open editors get it too), and if the list grew, voiceprint backfill starts.
    QStringList activeVoiceModelIds() const;
    // The set of models in use (an unloaded copy; the caller's own instance).
    processing::VoiceEmbedderSet voiceEmbedderSet() const;
    // Utterance-embedder factory for the models in use (SpeakerEditor / PeopleService); empty if
    // there is no usable model.
    processing::UtteranceEmbedderFactory utteranceEmbedderFactory() const;
    // Test seam: the loader of the models (a fake "model" without ONNX). Empty = the real one.
    // Recomputes the set immediately.
    void setVoiceEmbedderLoader(processing::PcmEmbedderLoader loader);

    // Lazy voiceprint backfill: for every sample (sampleRef) of every person that has no
    // voiceprint yet for a model in use, computes the vector on a background thread (1-thread
    // pool) from the sample's audio (sourceRefs, else sampleRef) and addPrint-s it (sibling
    // voiceprint: same sampleRef / source / createdAt / sourceRefs). Missing audio is skipped
    // (logged once). Does not block the main thread; if already running, it reruns at the end.
    // Done: voiceprintBackfillFinished(added). Starts by itself at startup and when the model list grows.
    void backfillVoiceprints();
    bool voiceprintBackfillRunning() const;

    // ---- tags and tag suggestions -------------------------------------------------------
    // The tag set and per-meeting tags (library/tags/TagService.h); the classic similarity
    // (library/tags/MeetingProfiles.h); embeddings and library preparation (embedding/).
    library::TagService*        tags() const;
    library::MeetingProfiles*   profiles() const;
    library::EmbeddingIndex*    embeddings() const;
    library::EmbeddingPreparer* embeddingPreparer() const;
    // The meeting's last suggestion list (from any source; already applied and rejected tags
    // filtered out), for a view model that connects later.
    QVector<tanara::TagSuggestion> pendingTagSuggestions(const QString& meetingId) const;
    // Suggestion for a not-yet-existing meeting (import window, before recording): by title. Synchronous.
    QVector<tanara::TagSuggestion> draftTagSuggestions(const QString& title) const;
    // Tags given DURING recording (the meeting is not in the store yet): they go onto the
    // meeting when the recording ends. No effect outside a recording; the next recording starts empty.
    void setRecordingTags(const QStringList& tagIds);
    QStringList recordingTags() const;

    // Known person names (global, reused across meetings), for autocomplete.
    QStringList knownPeople() const;

    // Renames/deletes a person GLOBALLY: in the name list + carried through EVERY meeting's
    // speakerMap (transcript.md files regenerated). peopleChanged signal.
    // The person's record (nicknames, note) moves as a whole: on rename it goes to the new name
    // (if the target name exists, the two people's data merge), on delete it is deleted. (The
    // old name is kept as a nickname by the People window action, PeopleService.)
    void renamePerson(const QString& oldName, const QString& newName);
    void removePerson(const QString& name);

    // The meeting's per-topic analyses completed so far (persisted, summary.analyses.json).
    // The UI loads the topic cards' "Done" state from this when the editor opens.
    QVector<tanara::TopicAnalysis> topicAnalyses(const QString& meetingId) const;

    // ---- transcript editor (speaker correction) ------------------------------
    // The meeting's speaker-editor session (see editing/session/SpeakerEditor.h). ONE instance lives per
    // meeting (AppController is its parent); the undo stack survives until it is closed.
    // Unknown meeting = nullptr. If a voice-ID model exists, the embedder is wired in.
    editing::SpeakerEditor* speakerEditor(const QString& meetingId);
    // Closes the session (undo stack dropped; the corrections stay on disk).
    void closeSpeakerEditor(const QString& meetingId);

    // Known people for the picker panel: name, whether there is a voiceprint, in how many meetings they appear.
    // (Filtering: editing/session/PeopleDirectory.h, filterPeople / matchesSearch.)
    QVector<tanara::editing::PersonInfo> peopleDirectory() const;

    // Whether the summary is stale (the speaker assignment changed since it was made), and how
    // many speakers were corrected since. Callable without an open editor.
    // The single-meeting variant also fills the TARGETED staleness (targeted, affected...: which
    // statements / action items have source lines whose speaker changed); the Meeting (library
    // row) variant is cheap and only gives the whole-document flag.
    tanara::SummaryStaleInfo summaryStale(const QString& meetingId) const;
    tanara::SummaryStaleInfo summaryStale(const tanara::Meeting& meeting) const;
    // The quick summary's statements (executive-summary sentences, decisions, action items) with
    // their source references; staleBecause / ownerStaleBecause filled per the current speaker
    // state. Empty for an old / per-topic summary.
    QVector<tanara::SummaryStatement> summaryStatements(const QString& meetingId) const;
    // "Not said like this? / Flag": marks the statement (summary.json, flagged = true).
    // Emits summaryStatementsChanged on success. false: no such meeting / statement.
    bool flagStatement(const QString& meetingId, const QString& statementId);
    // What is lost on re-transcription (number of manual corrections, for the confirmation dialog).
    tanara::editing::RetranscribeImpact retranscribeImpact(const QString& meetingId) const;

    // ---- participants ("Who was there?") and per-track speech share --------------------------------
    // The meeting's participants (manual + the voice-analysis candidates, with evidence) from meeting.json.
    QVector<tanara::Participant> participants(const QString& meetingId) const;
    // Whether the meeting's pre-transcript voice analysis is running (analyzeParticipants).
    bool participantAnalysisRunning(const QString& meetingId) const;
    // The analysis is done but there is no decision yet (approve / skip / "only me"): the banner shows.
    bool participantApprovalPending(const QString& meetingId) const;

    // ---- listening to tracks: speech strip, speaker -> track binding ----------------------------
    // Activity of EVERY track of the meeting (including those left out of the mixdown) for the
    // speech strip: one TrackActivity per logical track (trackId = id of the leading span), in meeting time.
    // From memory / the tracks.activity.view.bin cache (if every track is included,
    // tracks.activity.bin works too); if absent, returns an EMPTY result, computes in the
    // background, and emits trackActivityReady(meetingId) when done. No such meeting / track:
    // empty, no signal.
    tanara::MeetingActivity trackActivity(const QString& meetingId);
    bool trackActivityComputing(const QString& meetingId) const;
    // Speaker -> track binding ("Which track do they speak on?"). id: participant id (Participant::id)
    // or speaker key (after the transcript). Goes into Participant::tracks; if there is a transcript,
    // into the editor's manual track assignment for each of the participant's speaker keys too
    // (SpeakerOverlay::speakerTracks, undoable): ONE piece of data shared with the "Why them?" chips.
    // An empty list clears the binding (back to the inferred side). Unknown track ids are
    // dropped. true if changed; participantsChanged.
    bool setParticipantTracks(const QString& meetingId, const QString& id, const QStringList& trackIds);
    // The binding: the manual tracks (manual), otherwise the included tracks of the inferred side
    // (after the transcript the editor's side analysis, before it Participant::sides).
    tanara::ParticipantTrackBinding participantTracks(const QString& meetingId, const QString& id);
    // Test seam: a person's manual tags (tag ids, in the Meeting::tagIds space) instead of the
    // TagService's manual tags; the learned association (c(p,t)) still comes from the TagService.
    void setPersonTagsProvider(std::function<QStringList(const QString& personName)> provider);

    // Test seam: factory of the capture engine for the next recordings (empty = the real
    // miniaudio engine). See RecordingSession::setEngineFactory.
    void setRecordingEngineFactory(std::function<std::unique_ptr<tanara::recording::AudioEngine>()> factory);

    // ==== shared undo, participants after transcription, model operations (core) ============
    // The meeting's shared undo stack (editing/undo/MeetingUndo.h): the editor, the meeting's tag
    // steps and the participant operations, in time order. ONE instance per meeting (created
    // lazily; opening the editor creates it too). Unknown meeting = nullptr. It ends when the
    // meeting is deleted.
    editing::MeetingUndoService* meetingUndo(const QString& meetingId);
    // Participants with a name that are NOT bound to the transcript's speakers (added by hand, or
    // candidates that never spoke): they have no raw speaker and their name matches no transcript
    // speaker. Empty without a transcript (participants() is then the whole list).
    QVector<tanara::Participant> unheardParticipants(const QString& meetingId) const;
    // Samples skipped by the latest voiceprint backfill (missing source meeting / audio, or not
    // computable), sorted by person. Cleared when a new run starts.
    QVector<tanara::processing::VoiceprintBackfillSkip> backfillSkippedSamples() const;

public slots:
    // Re-enumerates devices (-> devicesChanged()).
    void refreshDevices();

    // Live level monitoring (for the recorder's level meters). Opens all capture devices and
    // emits deviceLevelPeak(name, rms, peak). It also runs DURING recording, on devices that are
    // NOT on a track (the level of recorded ones comes from the recording), so the recorder can
    // move every device's meter.
    void startLevelMonitoring();
    void stopLevelMonitoring();

    // Starts a further device's track DURING recording (the track plays from that moment; the
    // file runs from the open, its start is Track::startOffsetMs). Re-enabling a previously
    // switched-off / detached device is a new SPAN of the device (new file). false if no
    // recording is running or the device cannot be opened. On success: recordingTrackAdded + the selection is saved.
    bool addRecordingDevice(const tanara::AudioDeviceInfo& device);
    // Switches off a device's track DURING recording: its file closes here (stays part of the
    // meeting), the recording continues with the other tracks; re-enabling starts a new span.
    // false if no recording is running, the device has no (open) track, or it is the last open track.
    // On success: recordingTrackClosed (the name does NOT go into the detached ones).
    bool stopRecordingDevice(const QString& deviceName);

    // Enables the AUTOMATIC mixdown after recording in this process (on by default).
    // The standalone recorder process (tanara --record) turns it off: it exits after recording,
    // and an interrupted ffmpeg would leave only a truncated mixdown.part.mp3. The analyzer then
    // produces the mixdown when needed (transcription first builds a missing/stale mix).
    void setAutoMixdownAfterRecording(bool on);

    // Manual override/persistence of the last used devices.
    void setLastUsedDeviceNames(const QStringList& names);

    // Starts recording with the given devices (empty = all capture devices).
    void startRecording(const QString& title, const QVector<tanara::AudioDeviceInfo>& devices = {});
    void stopRecording();

    // Renames the meeting's title (meeting.json + the index updates).
    void renameMeeting(const QString& meetingId, const QString& newTitle);

    // Saves the meeting's few-word context description (topic/participants); it goes into the
    // STT context envelope and the LLM summary context. Persistent.
    void setMeetingContextNote(const QString& meetingId, const QString& note);
    // The call app detected by the watcher (e.g. "Microsoft Teams"): an informational field only;
    // does not touch the note and is not sent to the transcriber. Empty = clears it.
    void setMeetingDetectedCall(const QString& meetingId, const QString& appName);

    // PERMANENT deletion of a meeting (folder + index). The store emits meetingRemoved.
    void deleteMeeting(const QString& meetingId);

    // Restores a track dropped after recording to active; the file was still on disk.
    void restoreTrack(const QString& meetingId, const QString& trackId);
    // Speech check of the tracks (Track::speechRatio; a track without speech is left out of the mixdown),
    // on a background thread. Runs by itself after recording / import (before the automatic mixdown);
    // for a meeting made elsewhere (standalone recorder process) the view calls it on open. Only measures
    // tracks not yet measured; when done: tracksChanged (if changed). false: nothing to measure / already running.
    bool checkTrackSpeech(const QString& meetingId);
    // Whether the tracks' speech check is running (for the view's "running" state).
    bool speechCheckRunning(const QString& meetingId) const;
    // "Include": the (speechless or manually removed) track goes back into the mixdown; "Remove":
    // manual exclusion (excludedReason "manual"). Acts on all spans of a device. After both,
    // tracksChanged and a remix (if an included track remains).
    void includeTrack(const QString& meetingId, const QString& trackId);
    void excludeTrack(const QString& meetingId, const QString& trackId);
    // PERMANENT deletion of a track: the audio file is physically deleted and removed from the meeting.
    void deleteTrack(const QString& meetingId, const QString& trackId);

    // Remixes mixdown.mp3 from the meeting's ACTIVE tracks (after track changes the mixdown
    // becomes "dirty"; this refreshes it). Asynchronous (a media::MediaJob, ffmpeg; non-blocking):
    // at the end emits mixdownUpdated(meetingId, ok) + tracksChanged. Also emits jobProgress
    // with the state. No-op if there is no active track or a mix is already running.
    void regenerateMixdown(const QString& meetingId);

    // Imports audio files into a NEW meeting (see library/import/AudioImporter.h): one track per source,
    // split per channel on request; asynchronous (ffmpeg), the UI does not freeze. Returns the
    // future meeting's id (empty + errorOccurred if an import is already running). Progress shows
    // in jobs() as a JobKind::Import task on THIS id (real percentage, cancellable:
    // cancelJob(id, JobKind::Import)). The meeting is created only on success (importFinished);
    // after an error / cancel nothing stays on disk. The mixdown starts the same way as after a
    // recording: mixdownMode "auto" = immediately in the background, "manual" = waits for the user
    // (and with setAutoMixdownAfterRecording(false) it does not start here either). Transcription
    // does NOT start by itself.
    QString importAudio(const tanara::library::ImportRequest& request);

    // ---- meeting archive (*.tanara.zip, see library/store/MeetingArchive.h) ----
    // Export on a background thread (the GUI does not freeze even for a 1 GB recording). Returns
    // at once with the operation id ("export:<meetingId>"); progress shows in jobs() as a
    // JobKind::Export task on the meeting (real percentage, cancellable:
    // cancelJob(meetingId, JobKind::Export)) and also comes as the archiveProgress signal. The end
    // is archiveFinished. Empty id + errorOccurred: no such meeting / it is being recorded /
    // an archive operation is already running.
    QString exportMeetingArchive(const QString& meetingId, const QString& zipPath);
    // Import on a background thread: unpack into an .import-* folder under the recordings
    // folder, then (on the main thread) MeetingRepository::adoptMeetingFolder + resolve the tags to the
    // local tag set (same id = that one; otherwise by name, created if needed).
    // Returns an operation id ("import:<uuid>"); empty + errorOccurred if one is already running.
    // The end is archiveFinished (on success meetingId = the new meeting).
    QString importMeetingArchive(const QString& zipPath);
    // Pulls in a plain meeting folder (copied from another machine, no zip): a source outside the
    // recordings folder is copied on a background thread (.import-*), then the same path as for
    // an archive (adopt + tags). A source under the recordings folder stays in place.
    // Returns "import:<uuid>"; the end is archiveFinished.
    QString importMeetingFolder(const QString& folder);
    // The same, synchronously (CLI, tests). Error: false / empty Meeting + *error.
    bool exportMeetingArchiveNow(const QString& meetingId, const QString& zipPath, QString* error,
                                 std::function<void(int)> progress = {});
    tanara::Meeting importMeetingArchiveNow(const QString& zipPath, QString* error,
                                            std::function<void(int)> progress = {});
    // Whether an archive operation (export or import) is running.
    bool archiveBusy() const;

    // Sets the user's own (mic track) speaker name, in the settings AND in the people DB.
    void setUserSpeakerName(const QString& name);

    // Transcribes a meeting. The transcript is built from the MIXDOWN (a single audio stream: no
    // per-track overlap merging/duplication, 1x Soniox cost instead of ~Nx). If the mixdown is
    // missing/stale, it builds it first and starts the transcription on its done signal.
    // The key comes from the SecretStore.
    void transcribeMeeting(const QString& meetingId);

    // Re-transcription with an existing transcript: clears the speaker assignments (another
    // provider may split speakers differently; names would stick wrongly), then transcribeMeeting.
    // keepBackup: a copy of the current transcript files stays in the meeting folder
    // (transcript-backup-<timestamp>/). The manual line corrections (overlay) are deleted when the
    // new transcript is ready, because the utterance boundaries change.
    void retranscribeMeeting(const QString& meetingId, bool keepBackup = false);
    // "It's fine as is": dismisses the summary's stale flag without regenerating.
    void dismissSummaryStale(const QString& meetingId);
    // Summarizes a meeting (LM Studio/Gemma -> summary.md + a copy into notesDir).
    void summarizeMeeting(const QString& meetingId);

    // Complex (multi-pass) summary, pass 1: extracts TOPICS from the full transcript and returns
    // them with topicsReady(meetingId, topics) (the UI shows them for editing).
    // If summary.topics.json already exists, it returns that (no re-extraction).
    void extractMeetingTopics(const QString& meetingId);
    // Pass 2 + reduce (batch): of the (edited) topics queues only those not yet analyzed into the
    // topic-analysis job queue, then, once every topic's analysis exists, reduce -> summary.md
    // (summaryReady). Every finished analysis is persisted to summary.analyses.json IMMEDIATELY,
    // so after a cancel/error a restart resumes where it stopped.
    void generateComplexSummary(const QString& meetingId,
                                const QVector<tanara::SummaryTopic>& topics);
    // (Re)starts the analysis of ONE topic; it goes into the job queue (the local model runs with
    // parallel=1, so one at a time). The topic is also carried into summary.topics.json (editing).
    // Result: topicAnalysisReady/Failed; a finished analysis overwrites the earlier one in the JSON.
    void analyzeTopic(const QString& meetingId, const tanara::SummaryTopic& topic);
    // Reduce from the analyses ON DISK (summary.analyses.json, in topics.json order)
    // -> summary.md + summaryReady. Callable separately, e.g. after rerunning a failed topic.
    void finalizeComplexSummary(const QString& meetingId);

    // Renames a speaker in a meeting (raw label -> real name). Persists (Meeting.speakerMap +
    // people.json), regenerates transcript.md with the names, and emits speakerMapChanged.
    // Empty/same name = clears the mapping.
    // If enroll=true (default) and there is a voice model, it also records a voiceprint of the
    // speaker's voice under the name (manual labeling "teaches" voice ID).
    void renameSpeaker(const QString& meetingId, const QString& rawLabel,
                       const QString& displayName, bool enroll = true);

    // Explicitly records a speaker's voiceprint in the voiceprint DB (from the representative
    // audio of the meeting's raw label). voiceprintsChanged signal.
    void enrollSpeaker(const QString& meetingId, const QString& rawLabel, const QString& name);

    // Auto-identification: matches the meeting's still anonymous (unmapped) raw speakers against
    // the voiceprint DB; above the threshold it pre-fills the speakerMap.
    // Emits speakerMapChanged. Can be rerun any time (with the fresh DB).
    // onProgress(done,total): optional; returning false cancels (for the UI, called on the
    // main thread; the callback may draw progress / check "cancel").
    void autoIdentifyMeeting(const QString& meetingId,
                             const std::function<bool(int,int)>& onProgress = {});

    // Fingerprint test for ONE speaker: the best match from the voiceprint DB for its voice
    // (name + cosine score). Modifies nothing; { "", -1 } if there is no model/match.
    tanara::VoiceMatch testSpeakerMatch(const QString& meetingId, const QString& rawLabel);

    // PRE-TRANSCRIPT participant identification: samples the audio per track with windowing,
    // clusters it into speakers, and matches against the voiceprint DB -> who was in the
    // meeting (a name or "unknown"). Local, no Soniox; synchronous (a few seconds).
    // onProgress(done,total): optional; false cancels (see autoIdentifyMeeting).
    QVector<tanara::ParticipantGuess> identifyParticipants(
        const QString& meetingId, const std::function<bool(int,int)>& onProgress = {});

    // ---- participants ("Who was there?") ----------------------------------------------------
    // Pre-transcript voice analysis on a background thread (no Soniox): embedding from the tracks'
    // speech windows, clustering, matching with the voiceprints -> Participant candidates with
    // evidence (processing/analysis/ParticipantAnalysis.h). Works from the RAW track files, independent of the
    // mixdown; maybeAnalyzeParticipants starts it by itself. If the transcript already exists, the
    // binding runs too. End: participantAnalysisFinished (+ participantsChanged). false: no model /
    // already running / no such meeting. Meanwhile participantAnalysisProgress (track k / N, percent).
    bool analyzeParticipants(const QString& meetingId);
    // Starts the analysis if warranted: a usable model exists, it is not running, the tracks exist
    // and their speech check is not running / not missing, there is no decision (approval) yet, and
    // the tracks changed since the previous analysis (or there was none). Callers: end of recording,
    // end of the speech check, end of the mixdown, transcription start and opening the meeting
    // (Overview). true: it started now.
    bool maybeAnalyzeParticipants(const QString& meetingId);
    // Binds the candidates to the raw STT speakers (time overlap; a two-sided raw speaker is
    // split between two participants). Runs by itself when the transcript arrives. participantsChanged if changed.
    bool bindRawSpeakers(const QString& meetingId);
    // "Continue": approves the checked participants. Existing ones by id (the name is editable),
    // those with an unknown id as new manual participants. If there is a transcript: the raw
    // speakers get the names in ONE undo step (SpeakerEditor::applyBindings), raw speakers bound
    // to unchecked ones stay anonymous; if not, the binding runs when the transcript arrives. approval saved.
    bool approveParticipants(const QString& meetingId, const QVector<tanara::Participant>& accepted);
    // "Skip": a decision without binding (the banner disappears, the candidates stay).
    bool skipApproval(const QString& meetingId);
    // "They were not there": the participant's lines become anonymous (one undo step), their
    // marker is cleared.
    bool unbindParticipant(const QString& meetingId, const QString& participantId);
    // Adds a manual participant (also before transcription): source = Manual; gets priority in the analysis.
    // Returns the participant's id (for an existing name, that one's); empty if no such meeting / name.
    QString addParticipant(const QString& meetingId, const QString& personName);
    // "Only I spoke": every raw speaker gets the user's own name (now, or when the transcript arrives),
    // identification is skipped. false: no own name set / no such meeting.
    bool setSoloMeeting(const QString& meetingId);

    // Records a voiceprint from an arbitrary audio segment (for naming before transcription):
    // computes an embedding from the [startMs,endMs] part of the meeting's given track and saves
    // it under the name. Times are in MEETING time (like the sampleRef of identifyParticipants);
    // the function subtracts the track's offset (Track::startOffsetMs). voiceprintsChanged signal.
    void enrollVoiceprintFromSample(const QString& name, const QString& meetingId,
                                    const QString& trackId, qint64 startMs, qint64 endMs);

    // ---- structured layer: cancel, topics, identification, waveform ----------------
    // Cancels a running task. The meeting stays in a consistent state, no recording is
    // deleted:
    //  - Transcribe: in the mixdown phase ffmpeg stops (the old mix is untouched); at the
    //    provider the job is cancelled and the uploaded audio/transcript deleted (Soniox cleanup);
    //    the earlier transcript (if any) stays. In the identification phase: only the
    //    identification is skipped, the transcript is done.
    //  - Summarize / ExtractTopics: the running LLM request is cancelled, the earlier summary stays.
    //  - AnalyzeTopics: the meeting's queued topics are removed and the running one cancelled;
    //    finished (persisted) analyses stay, a resume continues from there.
    //  - Mixdown: ffmpeg stops, the half-written file is deleted, the earlier mix is untouched.
    //  - Identify: matches already found are saved, the other speakers stay anonymous.
    // true if there was something to cancel. A cancel emits NO errorOccurred and leaves no error.
    bool cancelJob(const QString& meetingId, tanara::JobKind kind);
    // Cancels ALL running tasks of the meeting (e.g. before deleting).
    void cancelAllJobs(const QString& meetingId);
    // "Load with a larger context" (LM Studio): before the NEXT LLM task the model is reloaded with
    // at least `tokens` of context (and parallel = 1), if the loaded instance is smaller. Applies
    // once; the caller restarts the task. No effect with Tanara Cloud.
    void requestLlmContext(int tokens);
    // Removes a single topic from the queue / cancels its running analysis.
    bool cancelTopicAnalysis(const QString& meetingId, const QString& topicId);

    // Saves the topic list in the given ORDER (add / delete / edit / reorder all persist
    // through this). Topics with an empty id (new) get an id, topics with an empty title are
    // dropped; remembered errors of deleted topics are cleared. Finished analyses
    // (summary.analyses.json) are untouched; those of deleted topics simply do not go into the
    // summary. Returns the saved list. topicsChanged signal.
    QVector<tanara::SummaryTopic> setMeetingTopics(const QString& meetingId,
                                                   const QVector<tanara::SummaryTopic>& topics);

    // Identifies participants by voice ASYNCHRONOUSLY (the non-blocking counterpart of
    // autoIdentifyMeeting): audio decoding + embedding run on a background thread, matching and
    // saving on the main thread. Progress: jobs() (Identify task, "3 / 5 speakers"); cancellable.
    // At the end speakerMapChanged (if there is a new name). false if no voice model / transcript / already running.
    bool identifyMeetingAsync(const QString& meetingId);

    // Automatic voice identification after transcription on/off for ONE meeting (the "Identify
    // participants by voice" toggle). Default: on. When off, the "identify" phase of the
    // transcription task is Skipped and the transcript is built with anonymous speakers. The choice
    // lasts for the life of the process (not persisted); it does not affect a later identifyMeetingAsync.
    void setIdentifyAfterTranscription(const QString& meetingId, bool enabled);
    bool identifyAfterTranscription(const QString& meetingId) const;
    // Whether a usable voice model exists (without it there is no identify phase; the toggle can be disabled).
    bool voiceIdentificationAvailable() const;

    // Requests waveform peaks for all existing tracks of the meeting (+ the mix, trackId
    // "mixdown"). Result: waveforms()->peaksReady / peaksFailed.
    void requestWaveforms(const QString& meetingId);

    // ---- tag suggestions (asynchronous; signals: tagSuggestionsComputing -> tagSuggestionsReady) ----
    // From similar meetings (+ embeddings, if available). Call when a meeting is displayed / on a
    // new transcript; cached per meeting until the tags, the transcript or the index change.
    // With suggestions off it returns an empty list. The computation runs on a background thread
    // (the main thread does not stall); from the cache the signal arrives immediately, within the
    // call. Repeated requests for the same meeting are merged.
    void requestTagSuggestions(const QString& meetingId);
    // The co-occurring tags after a tag is applied (source = Cooccur). If there are none, no signal
    // is emitted (earlier suggestions stay, without the applied tag).
    void requestCooccurSuggestions(const QString& meetingId, const QString& tagId);
    // The language model's suggestion from the summary (source = Llm; new name ideas with isNew).
    // Runs by itself after the summary is done, if the llmTagSuggestions setting is on.
    void requestLlmTagSuggestions(const QString& meetingId);
    // Sets the embedding provider (also in settings.json). A model change drops the index and
    // restarts preparation. Empty id = no embedding (basic level).
    void setEmbeddingProvider(const QString& providerId, const tanara::providers::ProviderConfig& config);

    // Sets a secret (e.g. the Soniox API key) in the SecretStore. name e.g. "soniox.apiKey".
    void setSecret(const QString& name, const QString& value);
    bool hasSecret(const QString& name) const;
    // The stored secret's value ("" if none), for the Settings key field (eye button) and the
    // "Test connection" probe. Do not display it anywhere else.
    QString secret(const QString& name) const;
    // Where the secrets are: "keychain" | "file" (foundation/secrets/SecretStore.h), and why it is the
    // file although the keychain was wanted (empty = by choice / sandbox / no problem).
    QString secretBackend() const;
    QString secretBackendWarning() const;

    // Counted level monitoring for several consumers (the Settings "Recording" page alongside the
    // recorder): while at least one consumer requests it, or the start/stopLevelMonitoring pair
    // (the recorder) is alive, monitoring runs; it stops when the last one releases. The request
    // of a destroyed `owner` is released automatically.
    void retainLevelMonitoring(QObject* owner);
    void releaseLevelMonitoring(QObject* owner);

    // ==== shared undo, permanent delete, model operations ============================
    // PERMANENT deletion of a participant from the meeting (meeting.json): their lines become
    // anonymous (like unbindParticipant), the participant and their approval entry are removed.
    // One step in the shared undo (together with the editor's binding step). false: no such
    // meeting / participant. (A later, manually started re-analysis may add the voice candidate
    // again.) If there is no such participant record, the id may be an utterance speaker's key
    // (the meeting got names before participant analysis): the lines of the named speaker become anonymous.
    bool removeParticipant(const QString& meetingId, const QString& participantId);
    // Rechecks the model files (e.g. a manually copied ONNX): recomputes the models in use;
    // voiceModelsChanged is ALWAYS emitted (the file state may have changed too), and if the
    // list in use grew, voiceprint backfill starts.
    void rescanVoiceModels();
    // Deletes a downloaded model file (only the models folder, AppPaths::modelsDir(); a bundled one is not touched),
    // then rescanVoiceModels. false (+ *error): unknown id / no downloaded file / currently
    // downloading or not deletable.
    bool deleteVoiceModelFile(const QString& id, QString* error = nullptr);

signals:
    void devicesChanged();
    // Live level BY DEVICE NAME, with peak (~30 Hz): before recording for every monitored device,
    // during recording for the recorded tracks and (from the level monitoring) the others too.
    void deviceLevelPeak(QString deviceName, float rms, float peak);
    void recordingTrackAdded(QString deviceName);    // a new track started during recording
    void recordingTrackClosed(QString deviceName);   // the recorded device was detached
    void recordingDeviceReturned(QString deviceName); // the detached device returned: a new span starts
    void recordingStateChanged(tanara::RecordingState state);
    void elapsedChanged(qint64 ms);
    void recordingFinished(tanara::Meeting meeting);
    // The import is done: the meeting is in the store (the library picked it up).
    void importFinished(tanara::Meeting meeting);
    // The import failed (nothing is left behind). detail: a technical line, may be empty.
    void importFailed(QString importId, QString message, QString detail);
    void importCancelled(QString importId);
    // Progress and end of an archive operation (opId: the return value of exportMeetingArchive /
    // importMeetingArchive). ok == false: message is the error ("Cancelled." on cancel).
    // For export path is the finished file, for import meetingId is the new meeting.
    void archiveProgress(QString opId, int percent);
    void archiveFinished(QString opId, bool ok, QString meetingId, QString path, QString message);
    // The call ended during recording (the detector saw inactive in 2 consecutive polls after a
    // previously active call). The UI asks whether to stop.
    void callEnded(QString appName);
    // During recording every track has been silent for at least `minutes` minutes (silenceAskMinutes).
    // The UI asks whether to stop; it re-arms when audio returns.
    void silenceDetected(int minutes);
    void transcriptReady(QString meetingId, QString markdownPath);
    void summaryReady(QString meetingId, QString markdownPath);
    void topicsReady(QString meetingId, QVector<tanara::SummaryTopic> topics);  // complex pass 1
    // Complex pass 2: PER-TOPIC lifecycle (the UI addresses the card by topicId):
    void topicAnalysisQueued(QString meetingId, QString topicId);    // queued
    void topicAnalysisStarted(QString meetingId, QString topicId);   // analysis running
    void topicAnalysisReady(QString meetingId, tanara::TopicAnalysis analysis);  // done + persisted
    void topicAnalysisFailed(QString meetingId, QString topicId, QString error); // only this topic failed
    void topicAnalysisQueueFinished(QString meetingId, int okCount, int failCount); // the queue is empty
    // The topic list changed (suggestion arrived / setMeetingTopics / analyzeTopic edit).
    void topicsChanged(QString meetingId);
    // A topic's state changed (see topicStatuses), for incremental card updates.
    void topicStatusChanged(QString meetingId, QString topicId);
    void speakerMapChanged(QString meetingId);              // after a speaker rename
    void summaryStaleChanged(QString meetingId);            // the summary's stale flag changed
    // The summary's statements changed (flagging). A change of the targeted staleness is
    // signaled by summaryStaleChanged; summaryStatements must be reread afterwards.
    void summaryStatementsChanged(QString meetingId);
    void peopleChanged();                                   // the people list changed
    void voiceprintsChanged();                              // the voice-ID voiceprint DB changed
    // Lazy backfill: started (total: voiceprints to compute), progress (done / total), done
    // (added: new voiceprints, skipped: skipped samples, see backfillSkippedSamples). If there is
    // nothing to compute, only Finished is emitted.
    void voiceprintBackfillStarted(int total);
    void voiceprintBackfillProgress(int done, int total);
    void voiceprintBackfillFinished(int added, int skipped);
    void voiceModelsChanged();                              // the list of models in use changed
    void tracksChanged(QString meetingId);                  // track active/dropped/deleted/speech share
    void participantAnalysisStarted(QString meetingId);     // the voice analysis started
    void participantAnalysisFinished(QString meetingId);    // the voice analysis is done (candidates on the meeting)
    // Progress of the voice analysis: processed track k / N (from 1; N = number of tracks with
    // speech) and the percentage by speech windows, 0..100. Emitted only on change.
    void participantAnalysisProgress(QString meetingId, int tracksDone, int tracksTotal, int percent);
    void participantsChanged(QString meetingId);            // participants / approval / binding changed
    void trackActivityReady(QString meetingId);             // track activity (speech strip) is ready
    void mixdownUpdated(QString meetingId, bool ok);        // result of regenerateMixdown
    void mixdownProgress(QString meetingId, int pct);       // mixdown progress 0..100
    // ---- Tanara Cloud processing feedback ----
    // Successful end of a run (transcription / summary / complex batch): sum of the per-call
    // charges (given by the gateway; the client does not compute), call count, new balance.
    // kind: transcribe | summary | topics | complex.
    void cloudCharged(QString meetingId, QString kind, tanara::cloud::Money total, int calls,
                      tanara::cloud::Money balance, QString vatMode);
    // The transcription ended with an upstream error; the gateway refunded the charge automatically.
    void cloudRefunded(QString meetingId, tanara::cloud::Money refund, tanara::cloud::Money balance,
                       QString requestId);
    // Structured gateway error in a run. chargedSoFar: sum of the calls the run already charged
    // (> 0 on a partial failure; it is then NOT true that "nothing was charged").
    void cloudError(QString meetingId, QString kind, tanara::cloud::CloudError error,
                    tanara::cloud::Money chargedSoFar);

    // Tag suggestions: computation started / done (the list may be empty). Ready always comes
    // after Computing, but may arrive in the same event-loop turn.
    void tagSuggestionsComputing(QString meetingId);
    void tagSuggestionsReady(QString meetingId, QVector<tanara::TagSuggestion> suggestions);

    void jobProgress(QString meetingId, QString message);   // transcription/summary state
    void errorOccurred(QString message);

private:
    // Call-end watching during recording (askStopOnCallEnd), using the watcher's detector core.
    void startCallEndMonitor();
    void stopCallEndMonitor();
    void pollCallEnd();
    // (Re)starts the level monitor with the right device set; without force only if the set
    // changed. During recording the recorded devices are left out.
    void restartLevelMonitor(bool force);
    void beginLevelMonitoring();
    void endLevelMonitoring();
    // After a device re-enumeration: during recording, closes the track of a recorded device that disappeared.
    void handleDeviceSetChange();
    // Transcribes the finished (fresh) mixdown with a single Soniox request; transcribeMeeting
    // chains the mixdown's completion to it. Speaker separation comes from Soniox diarization
    // ("Speaker N" labels); the name is resolved afterwards by voice ID / manual rename.
    void transcribeFromMixdown(const QString& meetingId);

    // Topic-analysis job queue: topics are queued (topicAnalysisQueued), ONE runs at a time
    // (local model), the finished result is persisted immediately. A topic's failure does not
    // stop the queue. reduceWhenDone=true: when the queue empties (if nothing failed) finalize.
    void enqueueTopicAnalyses(const QString& meetingId,
                              const QVector<tanara::SummaryTopic>& topics, bool reduceWhenDone);
    void startNextTopicJob();

    // Tanara Cloud processing run (X-Tanara-Job-Id, charge totals, last error): cloud/CloudRunLedger.h.
    using CloudRunPtr = std::shared_ptr<cloud::CloudRun>;
    CloudRunPtr newCloudRun(const QString& meetingId, const QString& kind) const;
    // The step's gateway config (baseUrl, model, max tokens; the key comes from the context's auth).
    providers::ProviderConfig cloudConfig(providers::WorkflowStep step) const;
    // The provider context of a call: a cloud run -> the ledger's gateway context (auth, X-Tanara-*
    // headers incl. summaryMode, response observer, cost sink); no run -> the shared HTTP client only.
    providers::ProviderContext providerContext(const CloudRunPtr& run, const QString& summaryMode) const;
    // BYO: the selected LLM config + key (run stays null). Cloud: gateway config; if the run is
    // still null, a new run starts (by kind).
    providers::ProviderConfig llmConfigFor(CloudRunPtr& run, const QString& meetingId, const QString& kind) const;
    void finishCloudRun(const CloudRunPtr& run);                       // cloudCharged
    // Per-track speech share (background thread), then applied to the finished meeting; thenMixdown = remix.
    void startSpeechCheck(const QString& meetingId, bool thenMixdown);
    void finishSpeechCheck(const QString& meetingId, const tanara::MeetingActivity& activity,
                           bool thenMixdown);
    void finishParticipantAnalysis(const QString& meetingId, tanara::ClusterSet clusters,
                                   const QString& tracksKey);
    void finishTrackActivity(const QString& meetingId, const tanara::MeetingActivity& activity);
    // The speaker keys of a participant (id) or speaker key in the editor (after the transcript).
    QStringList speakerKeysFor(const tanara::Meeting& m, const QString& id);
    // When the transcript arrives: the participants' manual track binding into the still-empty speaker assignments.
    void copyParticipantTracksToOverlay(const QString& meetingId);
    // The transcript arrived: binding + applying the decision (approve / "only me"). true if the
    // decision covered the speakers (automatic identification is skipped).
    bool applyParticipantsToTranscript(const QString& meetingId);
    // The bindings through the editor, in one undo step.
    bool applySpeakerBindings(const QString& meetingId, const QVector<tanara::SpeakerBinding>& bindings,
                              const QString& undoText);
    // Automatic identification after the transcript, as the last phase of the transcription task (asynchronous).
    bool startIdentify(const QString& meetingId, bool asTranscribeStage);
    // Tag suggestions: publishing the computed list (filtered), and setting the embedding.
    void publishTagSuggestions(const QString& meetingId, QVector<tanara::TagSuggestion> list);
    void computeTagSuggestions(const QString& meetingId);
    // The similarity-based list on a background thread (see the "tag suggestions" part of app/AppController.cpp).
    void scheduleTagSuggestions(const QString& meetingId);
    void startTagSuggestionJob(const QString& meetingId);
    void finishTagSuggestionJob(const QString& meetingId, quint64 dataGen, QVector<tanara::TagSuggestion> list);
    void invalidateTagSuggestions();
    void startLlmTagSuggestions(const QString& meetingId, const QVector<tanara::TagSuggestion>& suggested);
    void applyEmbeddingSettings(bool restart);
    // Recomputes the models in use (on a setting / loader change); backfill when it grows.
    void applyVoiceModels();
    // Error: a structured gateway error -> cloudError, otherwise errorOccurred(fallback).
    void failCloudRun(const CloudRunPtr& run, const QString& fallbackMessage);

    // Shared undo step for participant operations (RAII, see app/AppController.cpp).
    struct ParticipantUndoScope;

    struct Impl;
    std::unique_ptr<Impl> d;
};

// The SecretStore key names (in one place).
namespace keys {
inline const QString SonioxApiKey = QStringLiteral("soniox.apiKey");
inline const QString LlmApiKey    = QStringLiteral("llm.apiKey");
}

} // namespace tanara::app