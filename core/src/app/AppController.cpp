#include "tanara/app/AppController.h"

#include "tanara/app/ProviderRuns.h"
#include "tanara/app/Services.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/app/Workflows.h"
#include "tanara/cloud/CloudCapabilities.h"
#include "tanara/cloud/CloudEstimator.h"
#include "tanara/cloud/CloudMode.h"
#include "tanara/cloud/CloudRunLedger.h"
#include "tanara/cloud/CloudSession.h"
#include "tanara/editing/participants/ParticipantService.h"
#include "tanara/editing/recheck/RecheckService.h"
#include "tanara/editing/session/EditorSessions.h"
#include "tanara/editing/session/PeopleDirectory.h"
#include "tanara/editing/session/SpeakerEditSession.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/editing/undo/MeetingUndoRegistry.h"
#include "tanara/foundation/i18n/Localization.h"
#include "tanara/foundation/log/Logging.h"
#include "tanara/foundation/net/HttpClient.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/foundation/secrets/SecretStore.h"
#include "tanara/library/MeetingLayout.h"
#include "tanara/library/MeetingLibrary.h"
#include "tanara/library/import/ImportService.h"
#include "tanara/library/index/EmbeddingIndex.h"
#include "tanara/library/index/EmbeddingPreparer.h"
#include "tanara/library/jobs/JobStats.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/people/PeopleService.h"
#include "tanara/library/people/PeopleStats.h"
#include "tanara/library/store/ArchiveService.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/library/store/UiStateRepository.h"
#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/library/summary/SummaryRepository.h"
#include "tanara/library/tags/MeetingProfiles.h"
#include "tanara/library/tags/TagService.h"
#include "tanara/library/tags/TagSuggestionService.h"
#include "tanara/library/tracks/TrackCatalog.h"
#include "tanara/library/tracks/WaveformService.h"
#include "tanara/media/FfmpegMediaTool.h"
#include "tanara/processing/analysis/VoiceAnalysisService.h"
#include "tanara/processing/stt/TranscriptionService.h"
#include "tanara/processing/summary/SummarizationService.h"
#include "tanara/processing/summary/TopicQueue.h"
#include "tanara/processing/voiceid/VoiceEmbedderSet.h"
#include "tanara/processing/voiceid/VoiceModelDownloader.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/providers/core/ProviderRegistry.h"
#include "tanara/recording/MixdownService.h"
#include "tanara/recording/RecordingService.h"
#include "tanara/recording/audio/DeviceManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QTimer>

#include <memory>

namespace tanara::app {

namespace {


// The transcript / summary files are read and written through the library repositories
// (library::TranscriptRepository, library::SummaryRepository; names: library::MeetingLayout).
using library::MeetingLayout;
using library::SummaryRepository;
using library::TranscriptRepository;

} // namespace

struct AppController::Impl {
    SettingsManager* settings = nullptr;
    library::MeetingRepository*    store    = nullptr;
    std::unique_ptr<library::TranscriptRepository> transcripts;   // + the UI's cached transcript facts
    std::unique_ptr<library::UiStateRepository>    uiState;       // the editor's per-meeting view state
    std::unique_ptr<SecretStore> secrets;   // keychain or file (foundation/secrets)
    QString          secretsWarning;
    std::unique_ptr<library::PeopleStore> people;
    library::PeopleStats*   peopleStats = nullptr;
    std::unique_ptr<library::VoiceprintStore> voiceprints;
    QString          voiceModelPath;    // expected location of the base model (diagnostics)
    QString          appDir;            // location of the packaged models (<appDir>/models)
    // ---- P12d: participants, people, voice analysis, shared undo (app::Services) ----
    processing::VoiceAnalysisService* voice = nullptr;
    library::PeopleService*           peopleSvc = nullptr;
    editing::ParticipantService*      participants = nullptr;
    editing::MeetingUndoRegistry*     undo = nullptr;
    // The current display names (people.json); for transcript.md, summaries, source references.
    PersonNames personNames() const { return peopleSvc ? peopleSvc->personNames() : PersonNames(); }
    SelfPerson selfPerson() const { return peopleSvc ? peopleSvc->selfPerson() : SelfPerson(); }
    QString          metaDir;     // AppPaths::dataDir()
    QString          modelsDir;   // AppPaths::modelsDir()
    QString          notesDir;

    // The recording and the mixdown: services()->recording() / mixdown().
    recording::RecordingService* recording = nullptr;
    recording::MixdownService*   mixdown = nullptr;

    // ---- P12e: the summary and the topic queue (processing::SummarizationService, app::Services) ----
    processing::SummarizationService* summaries = nullptr;

    // ---- P12f: the transcription step, the chains, the editor sessions (app::Services) ----
    processing::TranscriptionService* transcription = nullptr;
    app::Workflows*                   workflows = nullptr;
    editing::EditorSessions*          editors = nullptr;
    // Who runs a step and who pays: readiness, own key vs. Tanara Cloud run, the processing steps' ports.
    std::unique_ptr<app::ProviderRuns> runs;

    // Tanara Cloud.
    cloud::CloudSession* cloud = nullptr;
    cloud::CloudRunLedger* cloudLedger = nullptr;   // cloud run bookkeeping (cloud/CloudRunLedger.h)
    cloud::CloudEstimator* cloudEstimator = nullptr; // cost estimates (cloud/CloudEstimator.h)
    net::HttpClient* http = nullptr;                 // the shared HTTP client of providers + cloud
    providers::ProviderRegistry providerRegistry;    // this controller's providers (not a singleton)
    cloud::CloudMode cloudMode;                      // live / teaser / base URL (cloud/CloudMode.h)

    // ---- structured layer (redesigned main window) ----------------------------------
    AppController*     q = nullptr;
    library::MeetingJobTracker* jobs = nullptr;
    library::MeetingLibrary*    library = nullptr;
    library::TrackCatalog*      tracks = nullptr;
    library::WaveformService*   waveforms = nullptr;
    std::unique_ptr<app::Services> services;
    media::MediaToolPtr         media;      // ffmpeg / ffprobe port (created once in the constructor)
    std::unique_ptr<library::JobStats> jobStats;      // speed of earlier runs (for estimates)

    // ---- tags ----
    library::TagService*        tags = nullptr;
    library::MeetingProfiles*   profiles = nullptr;
    library::EmbeddingIndex*    embeddings = nullptr;
    library::EmbeddingPreparer* embeddingPreparer = nullptr;

    bool voiceModelUsable() const { return voice && voice->usable(); }
};

AppController::AppController(QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
    d->q = this;
    providers::registerBuiltinProviders(d->providerRegistry);

    d->settings = new SettingsManager(QString(), this);
    const AppSettings s = d->settings->settings();

    // Tanara Cloud mode: TANARA_CLOUD=live|teaser|off overrides settings.cloud.enabled (cloud/CloudMode.h).
    // The cloud providers are registered below, once the session exists.
    d->cloudMode = cloud::CloudMode::fromEnvironment(s.cloud);

    // Every location from AppPaths (platform convention / "paths" section / TANARA_HOME).
    const paths::AppPaths& ap = d->settings->paths();
    d->metaDir   = ap.dataDir();
    d->modelsDir = ap.modelsDir();
    d->notesDir  = ap.notesDir();
    QDir().mkpath(ap.primaryRecordingRoot());
    QDir().mkpath(d->metaDir);

    {
        // The known secret keys (preloaded from the keychain): the descriptors' secret fields + the
        // fixed keys.
        QStringList known{keys::SonioxApiKey, keys::LlmApiKey, cloud::ApiKeySecret,
                          providers::embeddingproviders::ApiKeySecret};
        auto collect = [&known](const QVector<providers::ProviderDescriptor>& descs) {
            for (const auto& desc : descs) {
                for (const auto& f : desc.fields)
                    if (f.isSecret && !f.secretKey.isEmpty() && !known.contains(f.secretKey)) known << f.secretKey;
                if (!desc.loginSecretKey.isEmpty() && !known.contains(desc.loginSecretKey)) known << desc.loginSecretKey;
            }
        };
        collect(d->providerRegistry.descriptors(providers::CapabilityKind::Transcription));
        collect(d->providerRegistry.descriptors(providers::CapabilityKind::Chat));
        collect(d->providerRegistry.descriptors(providers::CapabilityKind::TextEmbedding));
        secrets::OpenResult opened = secrets::open(s.secrets, ap.dataFile(paths::files::Secrets), known);
        d->secrets = std::move(opened.store);
        d->secretsWarning = opened.warning;
    }

    d->http = new net::HttpClient(this);
    d->cloud = new cloud::CloudSession(d->secrets.get(), d->metaDir, this);
    d->cloud->setHttpClient(d->http);
    d->cloud->setBaseUrl(d->cloudMode.baseUrl);
    d->cloud->setLanguage(activeUiLanguage());
    d->cloudLedger = new cloud::CloudRunLedger(d->cloud, this);
    d->cloudEstimator = new cloud::CloudEstimator(d->cloud, this);
    connect(d->cloudLedger, &cloud::CloudRunLedger::charged, this, &AppController::cloudCharged);
    connect(d->cloudLedger, &cloud::CloudRunLedger::refunded, this, &AppController::cloudRefunded);
    connect(d->cloudLedger, &cloud::CloudRunLedger::failed, this, &AppController::cloudError);
    if (d->cloudMode.live) {
        // Feature flag (kill criterion): without it the "tanara-cloud" ids do not even exist.
        cloud::CloudCapabilityDeps deps;
        deps.media = [self = QPointer<AppController>(this)]() {
            return self ? self->d->media : media::MediaToolPtr();
        };
        cloud::registerInto(d->providerRegistry, *d->cloud, deps);
    }
    d->media   = media::FfmpegMediaTool::fromSettings(s.media);
    // Every recording root (the first is the primary: new recordings, imports).
    d->store   = new library::MeetingRepository(ap.recordingRoots(), d->metaDir, this);
    d->store->setMediaTool(d->media);   // the track lengths of recovered folders
    // Bringing orphaned recordings left after a crash (tracks without meeting.json) back into the list.
    d->store->recoverOrphanRecordings();
    // Temporary folders of an interrupted archive import / root move (e.g. quitting during unpacking). Only
    // older ones: the folder of a currently running (e.g. CLI) import is left alone.
    d->store->removeStaleImportFolders(6 * 3600);
    d->runs = std::make_unique<app::ProviderRuns>(app::ProviderRuns::Deps{
        d->settings, d->secrets.get(), d->http, d->cloud, d->cloudLedger, &d->providerRegistry, &d->cloudMode,
        d->store, d->metaDir,
        [this](const QString& id) { return d->transcription->cachedTranscript(id); },
        [this]() { return d->personNames(); }});
    d->transcripts = std::make_unique<library::TranscriptRepository>();
    d->uiState = std::make_unique<library::UiStateRepository>(ap.stateFile(paths::files::UiTranscript));

    // Structured layer: processing state, library queries, tracks, waveform.
    d->jobs      = new library::MeetingJobTracker(d->store, this);
    d->library   = new library::MeetingLibrary(d->store, d->jobs, this);
    d->tracks    = new library::TrackCatalog(d->store, this);
    d->waveforms = new library::WaveformService(d->media, this);
    d->jobStats  = std::make_unique<library::JobStats>(ap.dataFile(paths::files::JobStats));
    connect(d->tracks, &library::TrackCatalog::tracksChanged, this, &AppController::tracksChanged);
    // The summary's staleness (speaker-editor layer) is part of the derived state: the library icon's
    // "stale" state and the "Waiting for you" list refresh from it.
    d->jobs->setSummaryStaleProbe([this](const Meeting& m) {
        const SummaryStaleInfo info = summaryStale(m);
        return info.stale ? info.correctedSpeakers : -1;
    });
    connect(this, &AppController::summaryStaleChanged, d->jobs, &library::MeetingJobTracker::notifyStateChanged);

    d->people = std::make_unique<library::PeopleStore>(ap.dataFile(paths::files::People));
    library::ensureSelfPerson(d->people.get(), s.recording.userSpeakerName);

    // Voice ID: the voiceprint DB + the model's location: <metaDir>/models/..., if absent, the
    // <appDir>/models/... packaged next to the application (Windows zip / installer).
    d->voiceprints = std::make_unique<library::VoiceprintStore>(
        ap.dataFile(paths::files::Voiceprints));
    d->appDir = QCoreApplication::instance() ? QCoreApplication::applicationDirPath() : QString();
    d->voiceModelPath = processing::VoiceModelRegistry::resolveDefaultModelPath(d->modelsDir, d->appDir);

    // People window: statistics computed in the background; the operations (merge, moving a sample ...) are
    // library::PeopleService (app::Services). Nicknames / the note live in the people.json records.
    d->peopleStats = new library::PeopleStats(d->store, this);
    // Whatever affects participants or speaking time affects the statistics too (refreshed coalesced).
    connect(this, &AppController::speakerMapChanged, d->peopleStats, &library::PeopleStats::scheduleRefresh);
    connect(this, &AppController::transcriptReady, d->peopleStats, &library::PeopleStats::scheduleRefresh);
    connect(d->store, &library::MeetingRepository::meetingAdded, d->peopleStats, &library::PeopleStats::scheduleRefresh);
    connect(d->store, &library::MeetingRepository::meetingUpdated, d->peopleStats, &library::PeopleStats::scheduleRefresh);
    connect(d->store, &library::MeetingRepository::meetingRemoved, d->peopleStats, &library::PeopleStats::scheduleRefresh);
    // The transcript repository's cached facts (e.g. the speaker count of the meeting header) are
    // dropped when a transcript or its names change.
    auto dropTranscriptCache = [this](const QString& meetingId) {
        d->transcripts->invalidate(d->store->folderOf(meetingId));
    };
    connect(this, &AppController::transcriptReady, this, dropTranscriptCache);
    connect(this, &AppController::speakerMapChanged, this, dropTranscriptCache);

    // Tags: the set + per-meeting tags, classic similarity (profiles built on a background thread),
    // embeddings and library preparation, an LLM suggestion after the summary.
    d->tags = new library::TagService(d->store, ap.dataFile(paths::files::Tags), this);
    d->profiles = new library::MeetingProfiles(d->store, this);
    d->embeddings = new library::EmbeddingIndex(d->store, this);
    d->embeddings->setProfiles(d->profiles);
    d->embeddingPreparer = new library::EmbeddingPreparer(
        d->store, d->embeddings, ap.dataFile(paths::files::EmbeddingState), this);
    {
        app::ServicesDeps sd;
        sd.store = d->store;
        sd.tags  = d->tags;
        sd.jobs  = d->jobs;
        sd.media = d->media;
        sd.settings = d->settings;
        // P12c: tag suggestions (the LLM run's cloud bookkeeping and the embedder's credentials stay here).
        sd.profiles = d->profiles;
        sd.embeddings = d->embeddings;
        sd.embeddingPreparer = d->embeddingPreparer;
        sd.registry = &d->providerRegistry;
        sd.tagLlm = [this](const QString& meetingId, QObject* parent) { return d->runs->tagLlmSession(meetingId, parent); };
        sd.embedder = [this](const QString& id, QObject* parent) { return d->runs->makeEmbedder(id, parent); };
        // P12d: participants, people, voice analysis, shared undo. The editor sessions stay here until P12f.
        sd.people = d->people.get();
        sd.voiceprints = d->voiceprints.get();
        sd.peopleStats = d->peopleStats;
        sd.modelsDir = d->modelsDir;
        sd.appDir = d->appDir;
        sd.editorFor = [this](const QString& id) { return editSession(id); };
        // (The services are reset before the facade dies: then there is no open editor.)
        sd.existingEditor = [this](const QString& id) -> editing::SpeakerEditSession* {
            return d->services ? d->services->editorSessions().existing(id) : nullptr;
        };
        // The transcript's identity and the speech check's state: the transcription step / the workflows (P12f).
        sd.transcriptStamp = [this](const QString& id, const QString& folder) {
            return d->services->transcription().transcriptStamp(id, folder);
        };
        sd.speechCheckPending = [this](const Meeting& m) { return d->services->workflows().speechCheckPending(m); };
        // P12e: the summary and the topic queue (the app side: settings, key, Tanara Cloud runs).
        sd.summaryLlm = &d->runs->summaryPort();
        sd.jobStats = d->jobStats.get();
        sd.notesDir = d->notesDir;
        sd.cachedTranscript = [this](const QString& id) { return d->services->transcription().cachedTranscript(id); };
        // P12f: the transcription's app side (settings, STT key, Tanara Cloud runs) and the readiness check.
        sd.transcriptionPort = &d->runs->transcriptionPort();
        sd.readiness = [this](providers::WorkflowStep step, const QString& id) { return canRun(step, id); };
        d->services = std::make_unique<app::Services>(sd);
        d->voice = &d->services->voiceAnalysis();
        d->peopleSvc = &d->services->people();
        d->participants = &d->services->participants();
        d->undo = &d->services->meetingUndo();
        d->recording = &d->services->recording();
        d->mixdown = &d->services->mixdown();
        d->transcription = &d->services->transcription();
        d->workflows = &d->services->workflows();
        d->editors = &d->services->editorSessions();
        // Facade: the services' signals are re-emitted unchanged.
        library::ArchiveService& arch = d->services->archive();
        arch.setRecordingGuard([this](const Meeting& m) {
            const QString folder = d->recording->currentFolder();
            return d->recording->state() != RecordingState::Idle && !folder.isEmpty()
                && QDir(folder).absolutePath() == QDir(m.folder).absolutePath();
        });
        connect(&arch, &library::ArchiveService::rejected, this, &AppController::errorOccurred);
        connect(&arch, &library::ArchiveService::progress, this, &AppController::archiveProgress);
        connect(&arch, &library::ArchiveService::finished, this, &AppController::archiveFinished);
        library::ImportService& imp = d->services->import();
        connect(&imp, &library::ImportService::rejected, this, &AppController::errorOccurred);
        connect(&imp, &library::ImportService::finished, this, [this](const Meeting& m) {
            emit importFinished(m);
            d->workflows->afterImport(m);   // speech check -> mixdown, as at the end of a recording
        });
        connect(&imp, &library::ImportService::failed, this, &AppController::importFailed);
        connect(&imp, &library::ImportService::cancelled, this, &AppController::importCancelled);

        // Recording: devices, levels, tracks, state, the call-end and silence questions.
        recording::RecordingService& rec = *d->recording;
        connect(rec.devices(), &recording::DeviceManager::devicesChanged, this, &AppController::devicesChanged);
        connect(&rec, &recording::RecordingService::levelPeak, this, &AppController::deviceLevelPeak);
        connect(&rec, &recording::RecordingService::trackAdded, this, &AppController::recordingTrackAdded);
        connect(&rec, &recording::RecordingService::trackClosed, this, &AppController::recordingTrackClosed);
        connect(&rec, &recording::RecordingService::deviceReturned, this, &AppController::recordingDeviceReturned);
        connect(&rec, &recording::RecordingService::stateChanged, this, &AppController::recordingStateChanged);
        connect(&rec, &recording::RecordingService::elapsedChanged, this, &AppController::elapsedChanged);
        connect(&rec, &recording::RecordingService::finished, this, &AppController::recordingFinished);
        connect(&rec, &recording::RecordingService::error, this, &AppController::errorOccurred);
        connect(&rec, &recording::RecordingService::callEnded, this, &AppController::callEnded);
        connect(&rec, &recording::RecordingService::silenceDetected, this, &AppController::silenceDetected);
        // The speech check -> mixdown -> voice analysis chain after a recording (app::Workflows).
        connect(&rec, &recording::RecordingService::readyForProcessing, d->workflows, &app::Workflows::afterRecording);

        // Mixdown: the signals go out first, then the chains follow (a transcription's "mixdown" stage, the
        // voice analysis after a remix).
        recording::MixdownService& mix = *d->mixdown;
        connect(&mix, &recording::MixdownService::progress, this, [this](const QString& id, int pct) {
            emit mixdownProgress(id, pct);
            d->workflows->mixdownProgress(id, pct);
        });
        connect(&mix, &recording::MixdownService::error, this, &AppController::errorOccurred);
        connect(&mix, &recording::MixdownService::finished, this, &AppController::mixdownUpdated);
        connect(&mix, &recording::MixdownService::runEnded, this, [this](const QString& id, bool ok) {
            emit tracksChanged(id);   // the view should refresh (button state, mixdownFile)
            d->workflows->mixdownRunEnded(id, ok);
        });
    }
    d->tags->setProfiles(d->profiles);
    // Tags on people: manual tags in people.json; the own person is left out.
    d->tags->setPeopleStore(d->people.get());
    d->tags->setSelfProvider([this]() { return d->people ? d->people->selfId() : PersonId(); });
    connect(this, &AppController::peopleChanged, d->tags, &library::TagService::invalidatePersonStats);
    d->library->setTagService(d->tags);
    d->library->setPeopleStore(d->people.get());
    // The profiles' rows (meeting.json files) and phrases are built on a background thread already at
    // startup; from then on the main thread's readers (tag profile, draft suggestion) never load or compute
    // synchronously, they get the finished snapshot.
    d->profiles->ensureBuilt();
    // The person <-> tag statistics also on a background thread (the main thread reads the finished snapshot).
    d->tags->ensurePersonStats();
    // Tag suggestions (library::TagSuggestionService): the service's own signals are re-emitted unchanged; the
    // facade forwards the events it owns (participants, transcript, summary, settings).
    library::TagSuggestionService& tagSvc = d->services->tagSuggestions();
    connect(&tagSvc, &library::TagSuggestionService::suggestionsReady, this, &AppController::tagSuggestionsReady);
    connect(&tagSvc, &library::TagSuggestionService::computing, this, &AppController::tagSuggestionsComputing);
    // The participants (manual / approved) changed: the "by participants" suggestion goes out again.
    connect(this, &AppController::participantsChanged, &tagSvc, &library::TagSuggestionService::republish);
    connect(this, &AppController::transcriptReady, &tagSvc, &library::TagSuggestionService::transcriptChanged);
    // summarize -> tags (app::Workflows): the new summary's LLM tag suggestions.
    connect(this, &AppController::summaryReady, d->workflows, &app::Workflows::summaryReady);
    connect(d->settings, &SettingsManager::settingsChanged, this, [this]() {
        d->services->tagSuggestions().applyEmbeddingSettings(true);
    });
    tagSvc.applyEmbeddingSettings(false);

    // P12d facade: the services' signals are re-emitted unchanged.
    {
        processing::VoiceAnalysisService& voice = *d->voice;
        connect(&voice, &processing::VoiceAnalysisService::voiceprintsChanged, this, &AppController::voiceprintsChanged);
        connect(&voice, &processing::VoiceAnalysisService::peopleChanged, this, &AppController::peopleChanged);
        connect(&voice, &processing::VoiceAnalysisService::backfillStarted, this, &AppController::voiceprintBackfillStarted);
        connect(&voice, &processing::VoiceAnalysisService::backfillProgress, this, &AppController::voiceprintBackfillProgress);
        connect(&voice, &processing::VoiceAnalysisService::backfillFinished, this, &AppController::voiceprintBackfillFinished);
        connect(&voice, &processing::VoiceAnalysisService::participantAnalysisStarted, this, &AppController::participantAnalysisStarted);
        connect(&voice, &processing::VoiceAnalysisService::participantAnalysisFinished, this, &AppController::participantAnalysisFinished);
        connect(&voice, &processing::VoiceAnalysisService::participantAnalysisProgress, this, &AppController::participantAnalysisProgress);
        connect(&voice, &processing::VoiceAnalysisService::participantsChanged, this, &AppController::participantsChanged);
        connect(&voice, &processing::VoiceAnalysisService::trackActivityReady, this, &AppController::trackActivityReady);
        library::PeopleService& people = *d->peopleSvc;
        connect(&people, &library::PeopleService::peopleChanged, this, &AppController::peopleChanged);
        connect(&people, &library::PeopleService::voiceprintsChanged, this, &AppController::voiceprintsChanged);
        connect(&people, &library::PeopleService::speakerMapChanged, this, &AppController::speakerMapChanged);
        connect(&people, &library::PeopleService::summaryStaleChanged, this, &AppController::summaryStaleChanged);
        connect(&people, &library::PeopleService::participantsChanged, this, &AppController::participantsChanged);
        editing::ParticipantService& parts = *d->participants;
        connect(&parts, &editing::ParticipantService::participantsChanged, this, &AppController::participantsChanged);
        connect(&parts, &editing::ParticipantService::peopleChanged, this, &AppController::peopleChanged);
        connect(&parts, &editing::ParticipantService::speakerMapChanged, this, &AppController::speakerMapChanged);
        connect(&parts, &editing::ParticipantService::summaryStaleChanged, this, &AppController::summaryStaleChanged);
        editing::MeetingUndoRegistry& undo = *d->undo;
        connect(&undo, &editing::MeetingUndoRegistry::participantsChanged, this, &AppController::participantsChanged);
        connect(&undo, &editing::MeetingUndoRegistry::tracksChanged, this, &AppController::tracksChanged);
        connect(&undo, &editing::MeetingUndoRegistry::remixRequested, this, &AppController::regenerateMixdown);
    }

    // P12e facade: the summarization's and the topic queue's signals are re-emitted unchanged.
    {
        d->summaries = &d->services->summarization();
        processing::SummarizationService& sum = *d->summaries;
        connect(&sum, &processing::SummarizationService::summaryReady, this, &AppController::summaryReady);
        connect(&sum, &processing::SummarizationService::summaryStatementsChanged, this,
                &AppController::summaryStatementsChanged);
        connect(&sum, &processing::SummarizationService::jobProgress, this, &AppController::jobProgress);
        connect(&sum, &processing::SummarizationService::errorOccurred, this, &AppController::errorOccurred);
        processing::TopicQueue& topics = sum.topics();
        connect(&topics, &processing::TopicQueue::topicsReady, this, &AppController::topicsReady);
        connect(&topics, &processing::TopicQueue::topicAnalysisQueued, this, &AppController::topicAnalysisQueued);
        connect(&topics, &processing::TopicQueue::topicAnalysisStarted, this, &AppController::topicAnalysisStarted);
        connect(&topics, &processing::TopicQueue::topicAnalysisReady, this, &AppController::topicAnalysisReady);
        connect(&topics, &processing::TopicQueue::topicAnalysisFailed, this, &AppController::topicAnalysisFailed);
        connect(&topics, &processing::TopicQueue::topicAnalysisQueueFinished, this,
                &AppController::topicAnalysisQueueFinished);
        connect(&topics, &processing::TopicQueue::topicsChanged, this, &AppController::topicsChanged);
        connect(&topics, &processing::TopicQueue::topicStatusChanged, this, &AppController::topicStatusChanged);
        connect(&topics, &processing::TopicQueue::jobProgress, this, &AppController::jobProgress);
        connect(&topics, &processing::TopicQueue::errorOccurred, this, &AppController::errorOccurred);
    }

    // Speaker models: the set per the settings now, again on change (backfill when it grows).
    // The startup backfill runs in the first event-loop turn (under the GUI / the CLI's long commands; for a
    // short CLI command without an event loop it does not even start).
    applyVoiceModels();
    connect(d->settings, &SettingsManager::settingsChanged, this, [this]() { applyVoiceModels(); });
    QTimer::singleShot(0, this, [this]() { backfillVoiceprints(); });

    // Transcript editor: a new summary clears the stale flag; a new transcript drops the manual line
    // corrections (the utterance boundaries changed).
    connect(this, &AppController::summaryReady, d->editors, &editing::EditorSessions::summaryRegenerated);
    connect(this, &AppController::transcriptReady, d->editors, &editing::EditorSessions::transcriptReplaced);

    // P12f facade: the transcription step's, the chains' and the editor sessions' signals are re-emitted unchanged.
    {
        processing::TranscriptionService& stt = *d->transcription;
        connect(&stt, &processing::TranscriptionService::jobProgress, this, &AppController::jobProgress);
        connect(&stt, &processing::TranscriptionService::transcriptReady, this, &AppController::transcriptReady);
        connect(&stt, &processing::TranscriptionService::errorOccurred, this, &AppController::errorOccurred);
        app::Workflows& wf = *d->workflows;
        connect(&wf, &app::Workflows::jobProgress, this, &AppController::jobProgress);
        connect(&wf, &app::Workflows::tracksChanged, this, &AppController::tracksChanged);
        connect(&wf, &app::Workflows::errorOccurred, this, &AppController::errorOccurred);
        editing::EditorSessions& eds = *d->editors;
        connect(&eds, &editing::EditorSessions::speakerMapChanged, this, &AppController::speakerMapChanged);
        connect(&eds, &editing::EditorSessions::peopleChanged, this, &AppController::peopleChanged);
        connect(&eds, &editing::EditorSessions::voiceprintsChanged, this, &AppController::voiceprintsChanged);
        connect(&eds, &editing::EditorSessions::summaryStaleChanged, this, &AppController::summaryStaleChanged);
        // A change from outside (old UI rename, auto-identification, person rename) -> the open editor rereads
        // the disk; new voiceprints refresh its speakers.
        connect(this, &AppController::speakerMapChanged, &eds, &editing::EditorSessions::externalSpeakerMapChanged);
        connect(this, &AppController::voiceprintsChanged, &eds, &editing::EditorSessions::notifyVoiceprintsChanged);
    }
}

AppController::~AppController()
{
    // The voiceprint backfill, the track measurement / voice analysis and the background identifications: the
    // queued ones are skipped, the running ones exit at the next item (and no result is handed over).
    if (d->voice) d->voice->shutdown();
    // A running tag suggestion reads the profiles / the index: it must finish before them.
    if (d->services) d->services->tagSuggestions().shutdown();
    d->services.reset();   // joins a running archive thread
}

SettingsManager* AppController::settings() const { return d->settings; }
recording::DeviceManager*   AppController::devices()  const { return d->recording->devices(); }
library::MeetingRepository*    AppController::store()     const { return d->store; }
library::TranscriptRepository* AppController::transcripts() const { return d->transcripts.get(); }
library::UiStateRepository*    AppController::uiState()    const { return d->uiState.get(); }
library::PeopleStore*     AppController::people() const { return d->people.get(); }
library::VoiceprintStore* AppController::voiceprints() const { return d->voiceprints.get(); }
library::PeopleService*   AppController::peopleService() const { return d->peopleSvc; }
library::PeopleStats*     AppController::peopleStats() const { return d->peopleStats; }
library::TagService*        AppController::tags() const { return d->tags; }
library::MeetingProfiles*   AppController::profiles() const { return d->profiles; }
library::EmbeddingIndex*    AppController::embeddings() const { return d->embeddings; }
library::EmbeddingPreparer* AppController::embeddingPreparer() const { return d->embeddingPreparer; }
QString          AppController::voiceModelPath() const { return d->voiceModelPath; }

QStringList AppController::activeVoiceModelIds() const { return d->voice->modelIds(); }

processing::VoiceEmbedderSet AppController::voiceEmbedderSet() const { return d->voice->embedderSet(); }

processing::UtteranceEmbedderFactory AppController::utteranceEmbedderFactory() const
{
    return d->voice->utteranceEmbedderFactory();
}

void AppController::setVoiceEmbedderLoader(processing::PcmEmbedderLoader loader)
{
    d->voice->setLoader(std::move(loader));   // the set is rebuilt together with the loader (without backfill)
    applyVoiceModels();
}

void AppController::applyVoiceModels()
{
    const processing::VoiceAnalysisService::ModelChange change = d->voice->applyModels();
    if (!change.changed) return;   // another setting changed
    // The open editors continue with the new model list and factory.
    d->editors->forEach([this](editing::SpeakerEditSession* ed, editing::RecheckService* recheck) {
        const auto factory = utteranceEmbedderFactory();
        ed->setEmbedderFactory(factory);
        if (recheck) recheck->setEmbedderFactory(factory);
        ed->setVoiceModelIds(d->voice->editorModelIds());
    });
    emit voiceModelsChanged();
    if (change.grew && !change.first) backfillVoiceprints();
}

bool AppController::voiceprintBackfillRunning() const { return d->voice->backfillRunning(); }

void AppController::backfillVoiceprints() { d->voice->backfill(); }

// ---- recording (recording::RecordingService) ------------------------------------------------
RecordingState   AppController::recordingState() const { return d->recording->state(); }
QString AppController::currentMeetingFolder() const { return d->recording->currentFolder(); }

void AppController::refreshDevices() { d->recording->devices()->refresh(); }

void AppController::setRecordingEngineFactory(std::function<std::unique_ptr<recording::AudioEngine>()> factory) {
    d->recording->setEngineFactory(std::move(factory));
}

QStringList AppController::lastUsedDeviceNames() const { return d->recording->lastUsedDeviceNames(); }

void AppController::setLastUsedDeviceNames(const QStringList& names) {
    d->recording->setLastUsedDeviceNames(names);
}

QStringList AppController::knownPeople() const { return d->peopleSvc->knownNames(); }

SelfPerson AppController::selfPerson() const { return d->selfPerson(); }

void AppController::renameSpeaker(const QString& meetingId, const QString& rawLabel,
                                 const PersonChoice& choice, bool enroll) {
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }

    const bool hadPeople = d->people && !choice.isAnonymous() && choice.id.isNull();
    const PersonId person = choice.isAnonymous() || !d->people ? PersonId() : d->people->resolve(choice);
    if (person.isNull()) m.speakerMap.remove(rawLabel);
    else m.speakerMap.insert(rawLabel, person);
    d->store->saveMeeting(m);
    if (hadPeople && !person.isNull()) emit peopleChanged();

    // Regenerating transcript.md with the FULL mapping (from the raw tokens).
    // segments.json STAYS RAW so the UI can relabel any time.
    MergedTranscript merged =
        TranscriptRepository::readTokens(m.folder);
    if (!merged.tokens.isEmpty()) {
        editing::speakeredit::applyResolvedSpeakers(merged, m, d->personNames());
        TranscriptRepository::writeMarkdown(m.folder, merged.renderMarkdown());
    }
    if (editing::speakeredit::markSummaryStale(m, {rawLabel})) emit summaryStaleChanged(meetingId);
    emit speakerMapChanged(meetingId);

    // Manual labeling "teaches" voice ID: we record a voiceprint for the person.
    if (enroll && !person.isNull())
        enrollSpeaker(meetingId, rawLabel, person);
}

void AppController::enrollSpeaker(const QString& meetingId, const QString& rawLabel, const PersonId& person) {
    d->voice->enrollSpeaker(meetingId, rawLabel, person);   // voiceprintsChanged on success
}

void AppController::autoIdentifyMeeting(const QString& meetingId,
                                       const std::function<bool(int,int)>& onProgress) {
    // The matching (processing), then the commit into the meeting / overlay / transcript.md (editing).
    d->participants->commitIdentification(meetingId, d->voice->identifyTranscriptSpeakers(meetingId, onProgress));
}

VoiceMatch AppController::testSpeakerMatch(const QString& meetingId, const QString& rawLabel) {
    return d->voice->testSpeakerMatch(meetingId, rawLabel);
}

QVector<ParticipantGuess> AppController::identifyParticipants(
    const QString& meetingId, const std::function<bool(int,int)>& onProgress) {
    return d->voice->identifyParticipants(meetingId, onProgress);
}

PersonId AppController::enrollVoiceprintFromSample(const PersonChoice& choice, const QString& meetingId,
                                                  const QString& trackId, qint64 startMs, qint64 endMs) {
    return d->voice->enrollFromSample(choice, meetingId, trackId, startMs, endMs);
}

// The global person operations: library::PeopleService (P12d).
void AppController::renamePerson(const PersonId& person, const QString& newName) {
    d->peopleSvc->renameEverywhere(person, newName);
}

void AppController::removePerson(const PersonId& person) { d->peopleSvc->removeEverywhere(person); }

library::PersonMergeUndo AppController::mergePeople(const PersonId& keep, const PersonId& drop) {
    return d->peopleSvc->mergeEverywhere(keep, drop);
}

bool AppController::undoMergePeople(const library::PersonMergeUndo& u) {
    return d->peopleSvc->undoMergeEverywhere(u);
}

void AppController::renameMeeting(const QString& meetingId, const QString& newTitle) {
    const QString t = newTitle.trimmed();
    if (t.isEmpty()) return;
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }
    m.title = t;
    d->store->saveMeeting(m);   // meeting.json + the index refresh -> meetingUpdated signal
}

void AppController::setMeetingContextNote(const QString& meetingId, const QString& note) {
    editing::ParticipantUndoScope undoScope(d->undo, meetingId, tr("Leírás módosítva"));
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    const QString n = note.trimmed();
    if (m.contextNote == n) return;   // no change
    m.contextNote = n;
    d->store->saveMeeting(m);
}

void AppController::setMeetingDetectedCall(const QString& meetingId, const QString& appName) {
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    const QString a = appName.trimmed();
    if (m.detectedCallApp == a) return;
    m.detectedCallApp = a;
    d->store->saveMeeting(m);
}

void AppController::deleteMeeting(const QString& meetingId) {
    if (meetingId.isEmpty()) return;
    // Stopping the running tasks before the folder disappears under them.
    cancelAllJobs(meetingId);
    d->voice->cancelAnalysis(meetingId);   // the voice analysis's result no longer goes to the deleted meeting
    d->waveforms->cancel(meetingId);
    closeEditSession(meetingId);   // the open speaker editor must not write into the deleted folder
    d->transcription->forget(meetingId);
    d->store->deleteMeeting(meetingId);   // the meetingRemoved signal from the store
}

void AppController::restoreTrack(const QString& meetingId, const QString& trackId) {
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    bool changed = false;
    for (Track& t : m.tracks)
        if (t.id == trackId && !t.included()) {
            t.active = true;
            t.excludedReason.clear();
            changed = true;
        }
    if (!changed) return;
    m.mixdownDirty = true;   // the active track set changed, so the mixdown is stale
    d->store->saveMeeting(m);
    emit tracksChanged(meetingId);
}

void AppController::deleteTrack(const QString& meetingId, const QString& trackId) {
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    QVector<Track> kept;
    bool removed = false;
    for (const Track& t : m.tracks) {
        if (t.id == trackId) {
            // PHYSICAL deletion of the audio file (an explicit user action), unless the file is the MIXDOWN (in an
            // old meeting.json it may be listed as a track), or another track also refers to this file: then only the
            // track entry is deleted.
            bool shared = t.file.isEmpty() || (!m.mixdownFile.isEmpty() && t.file == m.mixdownFile);
            for (const Track& o : m.tracks)
                if (o.id != trackId && o.file == t.file) shared = true;
            if (!shared) {
                QFile::remove(QDir(m.folder).filePath(t.file));
                library::WaveformService::removeCache(QDir(m.folder).filePath(t.file));   // the waveform cache too
            }
            removed = true;
        } else {
            kept.push_back(t);
        }
    }
    if (!removed) return;
    m.tracks = kept;
    m.mixdownDirty = true;   // the active track set changed, so the mixdown is stale
    d->store->saveMeeting(m);
    emit tracksChanged(meetingId);
}

void AppController::regenerateMixdown(const QString& meetingId) { d->mixdown->regenerate(meetingId); }

void AppController::setUserSpeakerName(const QString& name) { d->peopleSvc->setSelfName(name); }

void AppController::startLevelMonitoring() { d->recording->startLevelMonitoring(); }

void AppController::stopLevelMonitoring() { d->recording->stopLevelMonitoring(); }

void AppController::retainLevelMonitoring(QObject* owner) { d->recording->retainLevelMonitoring(owner); }

void AppController::releaseLevelMonitoring(QObject* owner) { d->recording->releaseLevelMonitoring(owner); }

void AppController::setAutoMixdownAfterRecording(bool on) { d->workflows->setAutoMixdown(on); }

QStringList AppController::recordingDeviceNames() const { return d->recording->liveDeviceNames(); }

QStringList AppController::disconnectedRecordingDeviceNames() const { return d->recording->disconnectedDeviceNames(); }

bool AppController::addRecordingDevice(const AudioDeviceInfo& device) { return d->recording->addDevice(device); }

bool AppController::stopRecordingDevice(const QString& deviceName) { return d->recording->stopDevice(deviceName); }

void AppController::setSecret(const QString& name, const QString& value) { d->secrets->set(name, value); }
bool AppController::hasSecret(const QString& name) const { return !d->secrets->get(name).isEmpty(); }
QString AppController::secret(const QString& name) const { return d->secrets->get(name); }
QString AppController::secretBackend() const { return d->secrets->backendName(); }
QString AppController::secretBackendWarning() const { return d->secretsWarning; }

void AppController::startRecording(const QString& title, const QVector<AudioDeviceInfo>& devices)
{
    d->recording->start(title, devices);
}

void AppController::stopRecording() { d->recording->stop(); }

providers::ReadinessResult AppController::canRun(providers::WorkflowStep step, const QString& meetingId) const
{
    return d->runs->canRun(step, meetingId);
}

// ---- Tanara Cloud ------------------------------------------------------------------------

cloud::CloudSession* AppController::cloud() const { return d->cloud; }
bool AppController::cloudLive() const { return d->cloudMode.live; }
providers::ProviderRegistry& AppController::providerRegistry() const { return d->providerRegistry; }
bool AppController::cloudTeaser() const { return d->cloudMode.teaser; }

bool AppController::usesCloud(providers::WorkflowStep step) const { return d->runs->usesCloud(step); }

QString AppController::cloudModelFor(providers::WorkflowStep step) const { return d->runs->cloudModelFor(step); }

cloud::EstimateRequest AppController::makeEstimateRequest(const QString& meetingId, const QString& task,
                                                          const QString& summaryMode) const
{
    return d->runs->estimateRequest(meetingId, task, summaryMode);
}

// ---- the transcription chain (app::Workflows: mixdown -> processing::TranscriptionService -> identify) ----

void AppController::transcribeMeeting(const QString& meetingId) { d->workflows->transcribe(meetingId); }

void AppController::retranscribeMeeting(const QString& meetingId, bool keepBackup)
{
    d->workflows->retranscribe(meetingId, keepBackup);
}

// ---- summary and topic queue (processing::SummarizationService / TopicQueue) -----------------------

void AppController::summarizeMeeting(const QString& meetingId) { d->workflows->summarize(meetingId); }

void AppController::extractMeetingTopics(const QString& meetingId) { d->summaries->topics().extractTopics(meetingId); }

QVector<TopicAnalysis> AppController::topicAnalyses(const QString& meetingId) const
{
    return d->summaries->topics().analyses(meetingId);
}

void AppController::generateComplexSummary(const QString& meetingId, const QVector<SummaryTopic>& topics)
{
    d->summaries->topics().generate(meetingId, topics);
}

void AppController::analyzeTopic(const QString& meetingId, const SummaryTopic& topic)
{
    d->summaries->topics().analyze(meetingId, topic);
}

void AppController::finalizeComplexSummary(const QString& meetingId) { d->summaries->topics().finalize(meetingId); }

// =========================================================================================
// Structured layer for the redesigned main window: state queries, cancel, topics, asynchronous
// identification, waveform. (It lives alongside the old jobProgress / errorOccurred.)
// =========================================================================================

library::MeetingJobTracker* AppController::jobs() const      { return d->jobs; }
library::MeetingLibrary*    AppController::library() const   { return d->library; }
library::TrackCatalog*      AppController::tracks() const    { return d->tracks; }
library::WaveformService*   AppController::waveforms() const { return d->waveforms; }
library::AudioImporter*     AppController::importer() const  { return d->services->import().importer(); }
media::MediaToolPtr         AppController::mediaTool() const { return d->media; }
app::Services*              AppController::services() const  { return d->services.get(); }

QString AppController::importAudio(const library::ImportRequest& request)
{
    const AppSettings s = d->settings->settings();
    return d->services->import().start(request, opusBitrateKbps(s.recording.audioQuality), d->selfPerson());
}

// ---- meeting archive ---------------------------------------------------------------

bool AppController::archiveBusy() const { return d->services->archive().busy(); }

bool AppController::exportMeetingArchiveNow(const QString& meetingId, const QString& zipPath,
                                            QString* error, std::function<void(int)> progress)
{
    return d->services->archive().exportNow(meetingId, zipPath, error, std::move(progress));
}

Meeting AppController::importMeetingArchiveNow(const QString& zipPath, QString* error,
                                               std::function<void(int)> progress)
{
    return d->services->archive().importArchiveNow(zipPath, error, std::move(progress));
}

QString AppController::exportMeetingArchive(const QString& meetingId, const QString& zipPath)
{
    return d->services->archive().exportMeeting(meetingId, zipPath);
}

QString AppController::importMeetingFolder(const QString& folder)
{
    return d->services->archive().importFolder(folder);
}

QString AppController::importMeetingArchive(const QString& zipPath)
{
    return d->services->archive().importArchive(zipPath);
}

MeetingProcessingState AppController::processingState(const QString& meetingId) const
{
    return d->jobs->state(meetingId);
}

// ---- tags and tag suggestions ------------------------------------------------------------

void AppController::requestTagSuggestions(const QString& meetingId)
{
    d->services->tagSuggestions().requestSuggestions(meetingId);
}

void AppController::requestCooccurSuggestions(const QString& meetingId, const QString& tagId)
{
    d->services->tagSuggestions().requestCooccur(meetingId, tagId);
}

void AppController::requestLlmTagSuggestions(const QString& meetingId)
{
    d->services->tagSuggestions().requestLlm(meetingId);
}

QVector<TagSuggestion> AppController::pendingTagSuggestions(const QString& meetingId) const
{
    return d->services->tagSuggestions().pending(meetingId);
}

QVector<TagSuggestion> AppController::draftTagSuggestions(const QString& title) const
{
    return d->services->tagSuggestions().draft(title);
}

void AppController::setRecordingTags(const QStringList& tagIds) { d->recording->setTags(tagIds); }

QStringList AppController::recordingTags() const { return d->recording->tags(); }

void AppController::setEmbeddingProvider(const QString& providerId, const providers::ProviderConfig& config)
{
    AppSettings s = d->settings->settings();
    s.providers.embedding.selectedId = providerId;
    if (!providerId.isEmpty()) {
        providers::ProviderConfig cfg = config;
        cfg.type = providerId;
        cfg.apiKey.clear();   // the key belongs in the SecretStore (setSecret), not in the settings
        s.providers.embedding.configs.insert(providerId, cfg);
    }
    d->settings->setSettings(s);   // settingsChanged → TagSuggestionService::applyEmbeddingSettings(true)
}

library::SummaryDocument AppController::summaryDocument(const QString& meetingId) const
{
    return d->summaries->document(meetingId);
}

QVector<SummaryTopic> AppController::meetingTopics(const QString& meetingId) const
{
    return d->summaries->topics().topics(meetingId);
}

QVector<TopicStatus> AppController::topicStatuses(const QString& meetingId) const
{
    return d->summaries->topics().statuses(meetingId);
}

QVector<SummaryTopic> AppController::setMeetingTopics(const QString& meetingId,
                                                      const QVector<SummaryTopic>& topics)
{
    return d->summaries->topics().setTopics(meetingId, topics);
}

bool AppController::cancelTopicAnalysis(const QString& meetingId, const QString& topicId)
{
    return d->summaries->topics().cancelTopic(meetingId, topicId);
}

void AppController::requestLlmContext(int tokens) { d->summaries->requestLlmContext(tokens); }

bool AppController::cancelJob(const QString& meetingId, JobKind kind)
{
    return d->workflows->cancel(meetingId, kind);   // per kind: the running step's own cancel
}

void AppController::cancelAllJobs(const QString& meetingId) { d->workflows->cancelAll(meetingId); }

void AppController::setIdentifyAfterTranscription(const QString& meetingId, bool enabled)
{
    d->workflows->setIdentifyAfterTranscription(meetingId, enabled);
}

bool AppController::identifyAfterTranscription(const QString& meetingId) const
{
    return d->workflows->identifyAfterTranscription(meetingId);
}

bool AppController::voiceIdentificationAvailable() const
{
    return d->voiceModelUsable();
}

bool AppController::identifyMeetingAsync(const QString& meetingId) { return d->workflows->identify(meetingId); }

void AppController::requestWaveforms(const QString& meetingId)
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    for (const Track& t : m.tracks) {
        const QString path = QDir(m.folder).filePath(t.file);
        if (!t.file.isEmpty() && QFile::exists(path))
            d->waveforms->request(meetingId, t.id, path);
    }
    if (!m.mixdownFile.isEmpty()) {
        const QString mix = QDir(m.folder).filePath(m.mixdownFile);
        if (QFile::exists(mix))
            d->waveforms->request(meetingId, QStringLiteral("mixdown"), mix);
    }
}

// ---- transcript editor (speaker correction) ----------------------------------

editing::SpeakerEditSession* AppController::editSession(const QString& meetingId)
{
    return d->services ? d->services->editorSessions().editor(meetingId) : nullptr;
}

editing::RecheckService* AppController::recheckService(const QString& meetingId)
{
    return d->services ? d->services->editorSessions().recheck(meetingId) : nullptr;
}

void AppController::closeEditSession(const QString& meetingId) { d->editors->close(meetingId); }

QVector<editing::PersonInfo> AppController::peopleDirectory() const
{
    if (d->people) d->people->refresh();
    return editing::listPeople(d->people.get(), d->voiceprints.get(), d->store);
}

SummaryStaleInfo AppController::summaryStale(const QString& meetingId) const
{
    return d->editors->summaryStale(meetingId);
}

QVector<SummaryStatement> AppController::summaryStatements(const QString& meetingId) const
{
    return d->editors->summaryStatements(meetingId);
}

bool AppController::flagStatement(const QString& meetingId, const QString& statementId)
{
    return d->summaries->flagStatement(meetingId, statementId);   // summaryStatementsChanged (forwarded)
}

// The same for an already loaded meeting (the library calls it per row; no further disk read for meeting.json).
SummaryStaleInfo AppController::summaryStale(const Meeting& meeting) const { return d->editors->summaryStale(meeting); }

editing::RetranscribeImpact AppController::retranscribeImpact(const QString& meetingId) const
{
    return d->editors->retranscribeImpact(meetingId);
}

void AppController::dismissSummaryStale(const QString& meetingId) { d->editors->dismissSummaryStale(meetingId); }

// ---- participants ("Who was there?") and per-track speech share ----------------------------------------
// The pure logic: editing/session/TrackSpeech.h, processing/analysis/ParticipantAnalysis.h. Here only the threading, saving and
// binding through the editor.


void AppController::setPersonTagsProvider(std::function<QStringList(const PersonId&)> provider)
{
    d->voice->setPersonTagsProvider(std::move(provider));
}

QVector<Participant> AppController::participants(const QString& meetingId) const
{
    return d->participants->participants(meetingId);
}

bool AppController::participantAnalysisRunning(const QString& meetingId) const
{
    return d->voice->analysisRunning(meetingId);
}

bool AppController::participantApprovalPending(const QString& meetingId) const
{
    return d->participants->participantApprovalPending(meetingId);
}

bool AppController::speechCheckRunning(const QString& meetingId) const
{
    return d->workflows->speechCheckRunning(meetingId);
}

bool AppController::checkTrackSpeech(const QString& meetingId) { return d->workflows->checkTrackSpeech(meetingId); }

// ---- listening to tracks: speech strip, speaker -> track binding ---------------------------------

MeetingActivity AppController::trackActivity(const QString& meetingId)
{
    return d->voice->trackActivity(meetingId);
}

bool AppController::trackActivityComputing(const QString& meetingId) const
{
    return d->voice->trackActivityComputing(meetingId);
}

bool AppController::setParticipantTracks(const QString& meetingId, const QString& id,
                                         const QStringList& trackIds)
{
    return d->participants->setParticipantTracks(meetingId, id, trackIds);
}

ParticipantTrackBinding AppController::participantTracks(const QString& meetingId, const QString& id)
{
    return d->participants->participantTracks(meetingId, id);
}


void AppController::includeTrack(const QString& meetingId, const QString& trackId)
{
    d->workflows->includeTrack(meetingId, trackId);
}

void AppController::excludeTrack(const QString& meetingId, const QString& trackId)
{
    d->workflows->excludeTrack(meetingId, trackId);
}

bool AppController::maybeAnalyzeParticipants(const QString& meetingId)
{
    return d->voice->maybeAnalyzeParticipants(meetingId);
}

bool AppController::analyzeParticipants(const QString& meetingId)
{
    return d->voice->analyzeParticipants(meetingId);
}

bool AppController::bindRawSpeakers(const QString& meetingId)
{
    return d->participants->bindRawSpeakers(meetingId);
}

bool AppController::approveParticipants(const QString& meetingId, const QVector<Participant>& accepted)
{
    return d->participants->approveParticipants(meetingId, accepted);
}

bool AppController::skipApproval(const QString& meetingId)
{
    return d->participants->skipApproval(meetingId);
}

bool AppController::unbindParticipant(const QString& meetingId, const QString& participantId)
{
    return d->participants->unbindParticipant(meetingId, participantId);
}

QString AppController::addParticipant(const QString& meetingId, const PersonChoice& choice)
{
    return d->participants->addParticipant(meetingId, choice);
}

bool AppController::setSoloMeeting(const QString& meetingId)
{
    return d->participants->setSoloMeeting(meetingId);
}

// ==== shared undo, permanent participant delete, participants after transcription, model operations ====

editing::MeetingUndoService* AppController::meetingUndo(const QString& meetingId)
{
    return d->undo->forMeeting(meetingId);
}

bool AppController::removeParticipant(const QString& meetingId, const QString& participantId)
{
    return d->participants->removeParticipant(meetingId, participantId);
}

QVector<Participant> AppController::unheardParticipants(const QString& meetingId) const
{
    return d->participants->unheardParticipants(meetingId);
}

QVector<processing::VoiceprintBackfillSkip> AppController::backfillSkippedSamples() const
{
    return d->voice->backfillSkipped();
}

void AppController::rescanVoiceModels()
{
    const QStringList before = d->voice->modelIds();
    applyVoiceModels();   // it signals by itself on change (and backfills on growth)
    if (d->voice->modelIds() == before) emit voiceModelsChanged();   // the file state may have changed anyway
}

bool AppController::deleteVoiceModelFile(const QString& id, QString* error)
{
    const std::optional<processing::VoiceModelSpec> spec = processing::VoiceModelRegistry::spec(id);
    if (!spec) {
        if (error) *error = QStringLiteral("Unknown voice model: %1").arg(id);
        return false;
    }
    if (!processing::VoiceModelDownloader::deleteModelFile(*spec, d->modelsDir, error)) return false;
    qCInfo(lcVoice).noquote() << "Hangmodell-fájl törölve:" << id;
    rescanVoiceModels();
    return true;
}


} // namespace tanara::app