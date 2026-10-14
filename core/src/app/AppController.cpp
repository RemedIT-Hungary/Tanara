#include "tanara/app/AppController.h"
#include "tanara/providers/core/ProviderConfig.h"

#include "tanara/foundation/log/Logging.h"
#include "tanara/processing/summary/PromptLibrary.h"
#include <QTimer>
#include "tanara/app/SettingsManager.h"
#include "tanara/recording/audio/DeviceManager.h"
#include "tanara/recording/MixdownService.h"
#include "tanara/recording/RecordingService.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/app/Services.h"
#include "tanara/library/import/ImportService.h"
#include "tanara/library/store/ArchiveService.h"
#include "tanara/foundation/secrets/SecretStore.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/people/PeopleService.h"
#include "tanara/library/people/PeopleStats.h"
#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/processing/voiceid/VoiceEmbedder.h"
#include "tanara/processing/voiceid/VoiceEmbedderSet.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"
#include "tanara/providers/stt/ISttProvider.h"
#include "tanara/providers/chat/IChatProvider.h"
#include "tanara/providers/chat/ILocalModelServer.h"
#include "tanara/providers/chat/LlmServer.h"
#include "tanara/providers/core/ProviderRegistry.h"
#include "tanara/processing/summary/SummaryService.h"
#include "tanara/processing/summary/SummarizationService.h"
#include "tanara/processing/summary/TopicQueue.h"
#include "tanara/processing/ProviderFailureSink.h"
#include "tanara/processing/summary/TranscriptMerger.h"
#include "tanara/foundation/i18n/Localization.h"
#include "tanara/cloud/CloudSession.h"
#include "tanara/cloud/CloudRunLedger.h"
#include "tanara/cloud/CloudCapabilities.h"
#include "tanara/cloud/CloudEstimator.h"
#include "tanara/cloud/CloudMode.h"
#include "tanara/foundation/net/HttpClient.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/jobs/JobErrors.h"
#include "tanara/library/jobs/JobStats.h"
#include "tanara/library/MeetingLibrary.h"
#include "tanara/library/MeetingNotes.h"
#include "tanara/recording/audio/TrackTiming.h"
#include "tanara/library/tracks/TrackCatalog.h"
#include "tanara/library/tracks/WaveformService.h"
#include "tanara/media/FfmpegMediaTool.h"
#include "tanara/editing/session/PeopleDirectory.h"
#include "tanara/editing/session/SpeakerEditor.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/editing/session/TrackSpeech.h"
#include "tanara/processing/analysis/ParticipantAnalysis.h"
#include "tanara/library/tags/TagService.h"
#include "tanara/library/tags/TagNames.h"
#include "tanara/library/tags/MeetingProfiles.h"
#include "tanara/library/tags/TagSuggestionService.h"
#include "tanara/processing/analysis/LlmTagSuggestionProvider.h"
#include "tanara/library/index/EmbeddingIndex.h"
#include "tanara/library/index/EmbeddingPreparer.h"
#include "tanara/providers/embedding/ITextEmbedder.h"
#include "tanara/editing/undo/MeetingUndo.h"
#include "tanara/editing/undo/MeetingUndoRegistry.h"
#include "tanara/editing/participants/ParticipantService.h"
#include "tanara/processing/analysis/VoiceAnalysisService.h"
#include "tanara/library/store/ParticipantJson.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/library/store/UiStateRepository.h"
#include "tanara/library/MeetingLayout.h"
#include "tanara/processing/voiceid/VoiceModelDownloader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QHash>
#include <QSet>
#include <QFileInfo>
#include <QDateTime>
#include <QPointer>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QThread>
#include <QThreadPool>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

namespace tanara::app {

namespace {


// The transcript / summary files are read and written through the library repositories
// (library::TranscriptRepository, library::SummaryRepository; names: library::MeetingLayout).
using library::MeetingLayout;
using library::SummaryRepository;
using library::TranscriptRepository;


QString sttPhase(SttJobState s) {
    switch (s) {
    case SttJobState::Uploading:  return QCoreApplication::translate("AppController", "Hang feltöltése…");
    case SttJobState::Queued:     return QCoreApplication::translate("AppController", "Várakozás a Soniox sorban…");
    case SttJobState::Processing: return QCoreApplication::translate("AppController", "Átírás folyamatban…");
    case SttJobState::Completed:  return QCoreApplication::translate("AppController", "Sáv kész");
    case SttJobState::Failed:     return QCoreApplication::translate("AppController", "Hiba");
    default:                   return QStringLiteral("…");
    }
}

// The last FAILED HTTP exchange of a provider call (for the remembered error's technical line).
using processing::FailureSink;
using processing::captureFailures;

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
    // Per-track speech share: the measurement runs on the voice analysis's pool (P12f moves the rest).
    QSet<QString>    speechCheckRunning;  // meetingIds being measured now
    QSet<QString>    speechChecked;       // already measured in this process (not repeated even on error)
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
    bool        autoMixdown = true;             // automatic mixdown after recording / import
    QHash<QString, MergedTranscript> mergedCache;

    // ---- P12e: the summary and the topic queue (processing::SummarizationService, app::Services) ----
    processing::SummarizationService* summaries = nullptr;
    struct SummaryLlmAdapter;   // the summarization's SummaryLlmPort (settings, key, Tanara Cloud runs)
    std::unique_ptr<SummaryLlmAdapter> summaryLlm;

    // Transcript-editor sessions (one per meeting; see speakerEditor()).
    QHash<QString, QPointer<editing::SpeakerEditor>> speakerEditors;
    bool editorEmitting = false;   // the editor's own speakerMap signal is going out (do not reload)

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

    // A running transcription: the connection of the job / the mixdown chain, for cancelling.
    struct TranscribeRun {
        QPointer<providers::SttJob> job;
        std::shared_ptr<QMetaObject::Connection> mixConn;
        bool mixPhase = false;         // still waiting for the mixdown
        bool ownsMixdown = false;      // this transcription started the mixdown
        bool cancelRequested = false;
    };
    QHash<QString, TranscribeRun> transcribeRuns;
    QSet<QString> clearSpeakersOnTranscript;   // re-transcription: the old names are cleared with the new transcript
    // Meetings where the user TURNED OFF the automatic voice identification after transcription (the
    // toggle). A process-wide (not persisted) choice.
    QSet<QString> skipIdentifyAfterTranscribe;

    // Per meeting the transcript's "generation": every new transcript write increments it. The background
    // identification uses it (and the tokens file's state) to see that a new transcript arrived meanwhile.
    QHash<QString, int> transcriptGen;

    // The transcript's state at the start of an identification (see startIdentify): if it differs at the
    // end, the result refers to the labels of the OLD transcript, so it is dropped.
    QString transcriptStamp(const QString& meetingId, const QString& folder) const {
        const FileStamp fs = FileStamp::of(TranscriptRepository::tokensPath(folder));
        return QStringLiteral("%1/%2/%3").arg(transcriptGen.value(meetingId)).arg(fs.mtimeMs).arg(fs.size);
    }

    bool transcribeInMixPhase(const QString& meetingId) const {
        const auto it = transcribeRuns.constFind(meetingId);
        return it != transcribeRuns.constEnd() && it->mixPhase;
    }
    bool voiceModelUsable() const { return voice && voice->usable(); }
    // Tag suggestions (P12c): the LLM run's session (cloud bookkeeping) and the embedder factory, handed to
    // library::TagSuggestionService through app::Services.
    library::TagSuggestionService::LlmSession tagLlmSession(const QString& meetingId, QObject* parent);
    providers::ITextEmbedder* makeEmbedder(const QString& providerId, QObject* parent);
};

// P12e: the summarization's app side (processing::SummaryLlmPort): the settings, the LLM key and the Tanara
// Cloud run bookkeeping, through the same facade members as before the split (canRun, llmConfigFor,
// providerContext, finishCloudRun, the ledger's fail). A cloud run travels as an opaque handle.
struct AppController::Impl::SummaryLlmAdapter final : processing::SummaryLlmPort {
    struct CloudRunHandle final : RunHandle { CloudRunPtr run; };

    explicit SummaryLlmAdapter(AppController* owner) : q(owner) {}

    static CloudRunPtr unwrap(const Run& run)
    {
        const auto* h = static_cast<const CloudRunHandle*>(run.get());
        return h ? h->run : CloudRunPtr();
    }
    static Run wrap(const CloudRunPtr& run)
    {
        if (!run) return {};
        auto h = std::make_shared<CloudRunHandle>();
        h->run = run;
        return h;
    }

    providers::ReadinessResult readiness(const QString& meetingId) const override
    {
        return q->canRun(providers::WorkflowStep::Summarize, meetingId);
    }
    processing::ProcessingSettings processingSettings() const override { return q->d->settings->settings().processing; }
    QString chatProviderId() const override { return q->d->settings->settings().providers.llm.selectedId; }
    providers::ProviderConfig localConfig() const override { return q->d->settings->settings().providers.llm.selected(); }
    bool cloudSelected() const override { return q->usesCloud(providers::WorkflowStep::Summarize); }

    Run newRun(const QString& meetingId, const QString& kind) override { return wrap(q->newCloudRun(meetingId, kind)); }
    void setRunKind(const Run& run, const QString& kind) override
    {
        if (const CloudRunPtr r = unwrap(run)) r->kind = kind;
    }
    providers::ProviderConfig config(Run& run, const QString& meetingId, const QString& kind) override
    {
        CloudRunPtr r = unwrap(run);
        const providers::ProviderConfig cfg = q->llmConfigFor(r, meetingId, kind);
        if (!run && r) run = wrap(r);
        return cfg;
    }
    providers::ProviderContext context(const Run& run, const QString& summaryMode) const override
    {
        return q->providerContext(unwrap(run), summaryMode);
    }
    void finishRun(const Run& run) override { q->finishCloudRun(unwrap(run)); }
    bool failRun(const Run& run) override { return q->d->cloudLedger->fail(unwrap(run)); }
    bool runError(const Run& run, Error* out) const override
    {
        const CloudRunPtr r = unwrap(run);
        if (!r || !r->lastError.isError()) return false;
        if (out) *out = cloud::toError(r->lastError);
        return true;
    }
    QString runErrorMessage(const Run& run) const override
    {
        const CloudRunPtr r = unwrap(run);
        return r ? r->lastError.message : QString();
    }
    void refreshAccount(const Run& run) override
    {
        const CloudRunPtr r = unwrap(run);
        if (r && (r->balance.isValid() || r->calls > 0)) q->d->cloud->refreshAccount();
    }

    AppController* q;
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
        // Feature flag (kill criterion): without it the "tanara-cloud" ids do not even exist. The default
        // registry (GUI listing, TODO(P15): remove) gets them too.
        cloud::CloudCapabilityDeps deps;
        deps.media = [self = QPointer<AppController>(this)]() {
            return self ? self->d->media : media::MediaToolPtr();
        };
        cloud::registerInto(d->providerRegistry, *d->cloud, deps);
        cloud::registerInto(providers::ProviderRegistry::defaultInstance(), *d->cloud, deps);
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
        sd.tagLlm = [this](const QString& meetingId, QObject* parent) { return d->tagLlmSession(meetingId, parent); };
        sd.embedder = [this](const QString& id, QObject* parent) { return d->makeEmbedder(id, parent); };
        // P12d: participants, people, voice analysis, shared undo. The editor sessions stay here until P12f.
        sd.people = d->people.get();
        sd.voiceprints = d->voiceprints.get();
        sd.peopleStats = d->peopleStats;
        sd.modelsDir = d->modelsDir;
        sd.appDir = d->appDir;
        sd.editorFor = [this](const QString& id) { return speakerEditor(id); };
        sd.existingEditor = [this](const QString& id) -> editing::SpeakerEditor* { return d->speakerEditors.value(id); };
        sd.transcriptStamp = [this](const QString& id, const QString& folder) { return d->transcriptStamp(id, folder); };
        sd.speechCheckPending = [this](const Meeting& m) {
            // The measurement is running (its end calls again), or the speechless tracks are not filtered out yet.
            return d->speechCheckRunning.contains(m.id)
                || (!m.hasTranscript && editing::trackspeech::needsSpeechCheck(m) && !d->speechChecked.contains(m.id));
        };
        // P12e: the summary and the topic queue (the app side: settings, key, Tanara Cloud runs).
        d->summaryLlm = std::make_unique<Impl::SummaryLlmAdapter>(this);
        sd.summaryLlm = d->summaryLlm.get();
        sd.jobStats = d->jobStats.get();
        sd.notesDir = d->notesDir;
        sd.cachedTranscript = [this](const QString& id) { return d->mergedCache.value(id); };
        d->services = std::make_unique<app::Services>(sd);
        d->voice = &d->services->voiceAnalysis();
        d->peopleSvc = &d->services->people();
        d->participants = &d->services->participants();
        d->undo = &d->services->meetingUndo();
        d->recording = &d->services->recording();
        d->mixdown = &d->services->mixdown();
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
            // Mixdown: exactly as at the end of a recording (see the RecordingService::readyForProcessing handler): first the
            // tracks' speech check (a speechless track is left out of the mix).
            if (d->autoMixdown) {
                const bool mix = d->settings->settings().recording.mixdownMode != QStringLiteral("manual");
                if (editing::trackspeech::needsSpeechCheck(m)) startSpeechCheck(m.id, mix);
                else if (mix) regenerateMixdown(m.id);
            }
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
        connect(&rec, &recording::RecordingService::readyForProcessing, this, [this](const Meeting& m) {
            // The mixdown is decoupled from stop(): we start it here ASYNCHRONOUSLY, only if the setting is "auto".
            // It does not block, so a new recording can start at once. In manual mode the user starts it with the
            // review panel's "Mix" button. (The mixdown is needed only for listening; for transcription the per-track
            // .ogg files suffice.)
            // In the standalone recorder process (setAutoMixdownAfterRecording(false)) it does not start: that process
            // exits after recording, and the analyzer makes the mix when needed.
            // Before it, the tracks' speech check (on a background thread): a speechless track is left out of the mix
            // (Track::excludedReason "noSpeech"); the mix starts at the end of the measurement.
            if (d->autoMixdown) {
                const bool mix = d->settings->settings().recording.mixdownMode != QStringLiteral("manual")
                    && m.mixdownFile.isEmpty();
                if (editing::trackspeech::needsSpeechCheck(m)) startSpeechCheck(m.id, mix);
                else if (mix) regenerateMixdown(m.id);
                // The pre-transcript voice analysis from the raw tracks, right after the recording (if the speech
                // measurement is still pending, its end starts it).
                maybeAnalyzeParticipants(m.id);
            }
        });

        // Mixdown: a mix running as a transcription's stage moves that task's "mixdown" stage instead of its own task.
        recording::MixdownService& mix = *d->mixdown;
        mix.setTranscriptionStageProbe([this](const QString& id) { return d->transcribeInMixPhase(id); });
        connect(&mix, &recording::MixdownService::progress, this, [this](const QString& id, int pct) {
            emit mixdownProgress(id, pct);
            // (100 is only the UI's "done" tick; the stage is closed by the transcription chain.)
            if (pct < 100 && d->transcribeInMixPhase(id))
                d->jobs->setStage(id, JobKind::Transcribe, QStringLiteral("mixdown"), StageState::Running, pct);
        });
        connect(&mix, &recording::MixdownService::error, this, &AppController::errorOccurred);
        connect(&mix, &recording::MixdownService::finished, this, &AppController::mixdownUpdated);
        connect(&mix, &recording::MixdownService::runEnded, this, [this](const QString& id, bool ok) {
            emit tracksChanged(id);   // the view should refresh (button state, mixdownFile)
            // The pre-transcript voice analysis works from the raw tracks, independent of the mixdown; we try here
            // only because a mix can also run after a track change (an excluded track).
            if (ok) maybeAnalyzeParticipants(id);
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
    connect(this, &AppController::summaryReady, &tagSvc, &library::TagSuggestionService::summaryReady);
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
        // The asynchronous identification's matches are written by the participant service.
        connect(&voice, &processing::VoiceAnalysisService::speakersIdentified, this,
                [this](const QString& meetingId, const QVector<SpeakerVoiceMatch>& matches) {
                    d->participants->commitIdentification(meetingId, matches);
                });
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
    connect(this, &AppController::summaryReady, this, [this](const QString& meetingId) {
        if (editing::SpeakerEditor* ed = d->speakerEditors.value(meetingId)) {
            ed->notifySummaryRegenerated();
        } else {
            const Meeting m = d->store->load(meetingId);
            if (!m.folder.isEmpty()) editing::speakeredit::clearSummaryStale(m.folder);
        }
        emit summaryStaleChanged(meetingId);
    });
    connect(this, &AppController::transcriptReady, this, [this](const QString& meetingId) {
        const Meeting m = d->store->load(meetingId);
        if (m.folder.isEmpty()) return;
        editing::speakeredit::discardForNewTranscript(m.folder);
        if (editing::SpeakerEditor* ed = d->speakerEditors.value(meetingId)) ed->reloadTranscript();
    });
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
    for (const QPointer<editing::SpeakerEditor>& ed : std::as_const(d->speakerEditors)) {
        if (!ed) continue;
        ed->setEmbedderFactory(utteranceEmbedderFactory());
        ed->setVoiceModelIds(d->voice->editorModelIds());
    }
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
    closeSpeakerEditor(meetingId);   // the open speaker editor must not write into the deleted folder
    d->mergedCache.remove(meetingId);
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

void AppController::setAutoMixdownAfterRecording(bool on) { d->autoMixdown = on; }

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
    const Meeting meeting = d->store->load(meetingId);
    providers::ReadinessModel model(d->settings->settings().providers,
                         [this](const QString& k) { return !d->secrets->get(k).isEmpty(); },
                         &d->providerRegistry);
    providers::ReadinessResult r = model.check(step, meeting);
    if (!r.runnable || !usesCloud(step))
        return r;

    // Tanara Cloud-specific obstacles (exactly one CTA in the task strip).
    const cloud::CloudSession::Blocker blocker = d->cloud->blocker();
    if (blocker == cloud::CloudSession::Blocker::ClientTooOld) {
        providers::ReadinessResult c;
        c.blockerKind   = providers::BlockerKind::Cloud;
        c.detail        = tr("A Tanara Cloudhoz frissítés kell (legalább %1).").arg(d->cloud->minClient());
        c.fixActionHint = QStringLiteral("cloud:update");
        c.providerId    = cloud::ProviderId;
        return c;
    }
    if (blocker == cloud::CloudSession::Blocker::BalanceEmpty) {
        providers::ReadinessResult c;
        c.blockerKind   = providers::BlockerKind::Cloud;
        c.detail        = tr("töltsd fel az egyenleged");
        c.fixActionHint = QStringLiteral("cloud:topup");
        c.providerId    = cloud::ProviderId;
        return c;
    }
    return r;
}

// ---- Tanara Cloud ------------------------------------------------------------------------

cloud::CloudSession* AppController::cloud() const { return d->cloud; }
bool AppController::cloudLive() const { return d->cloudMode.live; }
providers::ProviderRegistry& AppController::providerRegistry() const { return d->providerRegistry; }
bool AppController::cloudTeaser() const { return d->cloudMode.teaser; }

bool AppController::usesCloud(providers::WorkflowStep step) const
{
    return d->cloudMode.selects(step, d->settings->settings().providers);
}

QString AppController::cloudModelFor(providers::WorkflowStep step) const
{
    return d->cloud->modelFor(step == providers::WorkflowStep::Transcribe ? QStringLiteral("stt") : QStringLiteral("llm"),
                              d->settings->settings().cloud);
}

namespace {
constexpr int kCloudMaxTokens = cloud::CloudEstimator::kMaxTokens;
}

// Gathers the meeting's facts for the estimate (the planning rule is cloud::CloudEstimator::plan).
cloud::EstimateRequest AppController::makeEstimateRequest(const QString& meetingId, const QString& task,
                                                   const QString& summaryMode) const
{
    const Meeting m = d->store->load(meetingId);
    const AppSettings s = d->settings->settings();
    cloud::EstimateInputs in;
    in.task = task;
    in.summaryMode = summaryMode;
    in.durationMs = m.durationMs;
    for (const Track& t : m.tracks) if (t.active) ++in.tracks;
    in.language = s.processing.languageHints.value(0);
    if (task != QLatin1String("summarize"))
        in.sttModel = cloudModelFor(providers::WorkflowStep::Transcribe);
    if (task != QLatin1String("transcribe")) {
        in.llmModel = cloudModelFor(providers::WorkflowStep::Summarize);
        // The actual input: system prompt + context + transcript (markdown).
        MergedTranscript merged = d->mergedCache.value(meetingId);
        if (merged.tokens.isEmpty() && !m.folder.isEmpty())
            merged = TranscriptRepository::readTokens(m.folder);
        if (!merged.tokens.isEmpty()) {
            editing::speakeredit::applyResolvedSpeakers(merged, m, d->personNames());
            in.hasTranscript = true;
            in.transcriptChars = int(merged.renderMarkdown().size() + m.contextNote.size());
            in.contextNoteChars = int(m.contextNote.size());
            if (summaryMode == QLatin1String("complex")) {
                in.topicPromptChars = int(processing::SummarizationService::resolvedPrompt(
                    s.processing, s.processing.topicExtractionPrompt, "topic").size());
                in.analysisPromptChars = int(processing::SummarizationService::resolvedPrompt(
                    s.processing, s.processing.topicAnalysisPrompt, "analysis").size());
                const QVector<SummaryTopic> topics =
                    SummaryRepository::readTopics(m.folder);
                in.topicsKnown = !topics.isEmpty();
                if (in.topicsKnown) {
                    QSet<QString> done;
                    for (const TopicAnalysis& a : SummaryRepository::readAnalyses(m.folder))
                        done.insert(a.topicId);
                    for (const SummaryTopic& t : topics) if (!done.contains(t.id)) ++in.missingAnalyses;
                }
            } else {
                const processing::SummaryRequest req =
                    processing::SummarizationService::requestFor(s.processing, merged, m, in.llmModel, kCloudMaxTokens);
                const processing::SummaryPlan plan = processing::SummaryService::plan(req);
                in.singleCall = plan.singleCall;
                in.parts = plan.parts;
                in.cachedParts = plan.cachedParts;
                in.partChars = plan.partChars;
                in.singlePromptChars = int(req.singlePrompt.size());
                in.notesPromptChars = int(req.notesPrompt.size());
                in.mergePromptChars = int(req.mergePrompt.size());
                in.notesMaxTokens = req.notesMaxTokens;
            }
        }
    }
    return cloud::CloudEstimator::plan(in);
}

AppController::CloudRunPtr AppController::newCloudRun(const QString& meetingId, const QString& kind) const
{
    return d->cloudLedger->newRun(meetingId, kind);
}

providers::ProviderConfig AppController::cloudConfig(providers::WorkflowStep step) const
{
    return d->cloudLedger->configFor(cloudModelFor(step), kCloudMaxTokens);
}

providers::ProviderContext AppController::providerContext(const CloudRunPtr& run, const QString& summaryMode) const
{
    if (run) return d->cloudLedger->contextFor(run, summaryMode, d->http);
    providers::ProviderContext ctx;   // own key / address: the config's apiKey, no hooks
    ctx.http = d->http;
    return ctx;
}

providers::ProviderConfig AppController::llmConfigFor(CloudRunPtr& run, const QString& meetingId,
                                           const QString& kind) const
{
    if (!usesCloud(providers::WorkflowStep::Summarize)) {
        providers::ProviderConfig cfg = d->settings->settings().providers.llm.selected();
        cfg.apiKey = d->secrets->get(keys::LlmApiKey);   // LM Studio: may be empty
        return cfg;
    }
    if (!run) run = newCloudRun(meetingId, kind);
    return cloudConfig(providers::WorkflowStep::Summarize);
}

void AppController::finishCloudRun(const CloudRunPtr& run)
{
    d->cloudLedger->finish(run);   // cloudCharged (forwarded)
}

void AppController::failCloudRun(const CloudRunPtr& run, const QString& fallbackMessage)
{
    if (!d->cloudLedger->fail(run))   // cloudRefunded / cloudError (forwarded)
        emit errorOccurred(fallbackMessage);
}

void AppController::transcribeMeeting(const QString& meetingId)
{
    if (d->jobs->isRunning(meetingId, JobKind::Transcribe))
        return;   // already running on this meeting (a double click / two views)

    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) {
        d->clearSpeakersOnTranscript.remove(meetingId);
        emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId));
        return;
    }
    // The voice analysis runs in parallel with transcription (if it has not run on these tracks yet).
    maybeAnalyzeParticipants(meetingId);

    providers::ReadinessResult res = canRun(providers::WorkflowStep::Transcribe, meetingId);
    if (!res.runnable) {
        d->clearSpeakersOnTranscript.remove(meetingId);
        emit errorOccurred(res.detail);
        return;
    }

    // A standalone identification refers to the labels of the CURRENT transcript: we stop it so its names
    // do not land on the NEW transcript about to arrive (its result is dropped at completion by the
    // transcript-change check too; see startIdentify).
    cancelJob(meetingId, JobKind::Identify);

    // The transcript is made from the MIXDOWN (a single audio stream: no per-track overlap
    // merging/duplication, 1x Soniox cost instead of ~Nx). If the mixdown is missing or stale, we make it first
    // and start the transcription on the mixdownUpdated signal.
    const QString mixPath =
        QDir(m.folder).filePath(MeetingLayout::mixdownOf(m));
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
        if (d->voiceModelUsable())
            stages.append({QStringLiteral("identify"), tr("Résztvevők azonosítása"),
                           d->skipIdentifyAfterTranscribe.contains(meetingId) ? StageState::Skipped
                                                                              : StageState::Waiting,
                           -1, QString()});
        d->jobs->begin(meetingId, JobKind::Transcribe, tr("Átírás folyamatban"), stages);
    }
    d->transcribeRuns.insert(meetingId, Impl::TranscribeRun{});

    if (needMix) {
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(this, &AppController::mixdownUpdated, this,
            [this, meetingId, conn](const QString& id, bool ok) {
                if (id != meetingId) return;
                QObject::disconnect(*conn);
                auto it = d->transcribeRuns.find(meetingId);
                if (it == d->transcribeRuns.end()) return;   // cancelled meanwhile
                it->mixPhase = false;
                it->mixConn.reset();
                if (ok) {
                    d->jobs->setStage(meetingId, JobKind::Transcribe, QStringLiteral("mixdown"),
                                      StageState::Done);
                    transcribeFromMixdown(meetingId);
                    return;
                }
                d->transcribeRuns.erase(it);
                d->clearSpeakersOnTranscript.remove(meetingId);
                const QString msg = tr("A lekeverés sikertelen — az átírás nem indult.");
                JobError je;
                je.message = msg;
                je.detail = d->mixdown->takeFailReason(meetingId);   // e.g. which track's file is missing
                d->jobs->fail(meetingId, JobKind::Transcribe, je);
                emit errorOccurred(msg);
            });
        Impl::TranscribeRun& tr_ = d->transcribeRuns[meetingId];
        tr_.mixPhase = true;
        tr_.mixConn = conn;
        // If a standalone mix was already running, we wait for it; on cancel we do not stop that one.
        tr_.ownsMixdown = !d->mixdown->running(meetingId);
        d->jobs->setStage(meetingId, JobKind::Transcribe, QStringLiteral("mixdown"),
                          StageState::Running, 0);
        emit jobProgress(m.id, tr("Lekeverés az átíráshoz…"));
        regenerateMixdown(meetingId);
        return;
    }
    transcribeFromMixdown(meetingId);
}

void AppController::retranscribeMeeting(const QString& meetingId, bool keepBackup)
{
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }
    if (d->jobs->isRunning(meetingId, JobKind::Transcribe))
        return;   // a transcription is already running on this meeting
    // The current transcript (with the names and manual corrections) stays as a copy, if requested.
    if (keepBackup) editing::speakeredit::backupTranscript(m, d->personNames());
    // Both the speaker assignments (speakerMap) AND the manual line corrections (overlay) are cleared when the
    // NEW transcript arrives, not beforehand: the speakerMap in the transcription's finished branch, the
    // overlay in the transcriptReady hook. If the re-transcription is cancelled or fails, the old transcript
    // stays untouched with its names and per-line corrections.
    d->clearSpeakersOnTranscript.insert(meetingId);
    transcribeMeeting(meetingId);
}

void AppController::transcribeFromMixdown(const QString& meetingId)
{
    // Closing the task with an error before the provider call (an internal helper).
    auto abortJob = [this, meetingId](const QString& msg) {
        d->transcribeRuns.remove(meetingId);
        d->clearSpeakersOnTranscript.remove(meetingId);
        JobError je;
        je.message = msg;
        d->jobs->fail(meetingId, JobKind::Transcribe, je);
        emit errorOccurred(msg);
    };

    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) {
        d->transcribeRuns.remove(meetingId);
        d->clearSpeakersOnTranscript.remove(meetingId);
        d->jobs->cancelled(meetingId, JobKind::Transcribe);
        return;
    }

    const AppSettings s = d->settings->settings();
    const QString sttId = s.providers.stt.selectedId;
    providers::ProviderConfig cfg = s.providers.stt.selected();
    // Tanara Cloud: the same Soniox provider with the gateway config; the run (job id, charge,
    // write-back, error) is filled from the response hook.
    CloudRunPtr run;
    bool cloudDiarization = true;
    if (usesCloud(providers::WorkflowStep::Transcribe)) {
        run = newCloudRun(meetingId, QStringLiteral("transcribe"));
        cfg = cloudConfig(providers::WorkflowStep::Transcribe);
        const std::optional<cloud::CloudModel> cm = findModel(d->cloud->models(), cfg.model);
        cloudDiarization = !cm.has_value() || cm->diarization;
    } else {
        // The API key's slot is provider-dependent: the descriptor's secretKey tells it (soniox.apiKey /
        // stt.whisper.apiKey / ...); we do not hardcode it to Soniox.
        const providers::ProviderDescriptor sttDesc =
            d->providerRegistry.descriptor(providers::CapabilityKind::Transcription, sttId);
        for (const providers::ConfigField& f : sttDesc.fields)
            if (f.isSecret && !f.secretKey.isEmpty()) {
                cfg.apiKey = d->secrets->get(f.secretKey);
                break;
            }
    }
    // Remembering the last failed HTTP exchange (in own-key mode too): this becomes the remembered error's
    // technical line ("HTTP 401 | invalid_api_key").
    const auto sink = std::make_shared<FailureSink>();
    providers::ProviderContext pctx = providerContext(run, QString());
    captureFailures(pctx, sink);

    providers::ISttProvider* provider =
        d->providerRegistry.create<providers::ISttProvider>(sttId, pctx, cfg, this);
    if (!provider) {
        abortJob(tr("Ismeretlen STT-provider: %1.").arg(sttId));
        return;
    }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    emit jobProgress(m.id, tr("Átírás indítása…"));

    const QString mixPath =
        QDir(m.folder).filePath(MeetingLayout::mixdownOf(m));

    // A SINGLE request with the mixdown. Speaker separation comes from Soniox diarization ("Speaker N"
    // labels); the NAME is resolved afterwards by voice ID / manual rename.
    providers::SttRequest req;
    req.audioFilePath = mixPath;
    req.trackId = QStringLiteral("mixdown");
    req.languageHints = s.processing.languageHints;
    // Context envelope -> Soniox's structured "context" object (general/text/terms): the title + the user's
    // note (names, jargon, mishearing corrections) + the participant names. The detected call app is NOT sent
    // (it tells the transcriber nothing).
    library::meetingnotes::fillSttContext(req, m);
    req.diarization = true;
    if (run && !cloudDiarization) {
        // The chosen cloud tier does not diarize (e.g. Fast): everyone becomes "Speaker 1" (the recording's tracks
        // stay; the pre-run warning covered this).
        req.diarization = false;
        req.speakerLabel = QStringLiteral("1");
    }
    const bool diarizing = req.diarization;

    providers::SttJob* job = provider->transcribe(req);

    // Structured state: the job for cancelling, the earlier runs' speed for the estimate (with the same
    // provider/model); without a sample there is no estimate.
    const QString statsKey = QStringLiteral("transcribe/%1/%2").arg(sttId, cfg.model);
    {
        Impl::TranscribeRun& tr_ = d->transcribeRuns[meetingId];
        tr_.job = job;
        tr_.mixPhase = false;
        const int est = d->jobStats->estimateSec(statsKey, double(m.durationMs) / 1000.0);
        if (est >= 0) {
            const JobProgress jp = d->jobs->job(meetingId, JobKind::Transcribe);
            const int elapsed = jp.startedAt.isValid()
                ? int(jp.startedAt.secsTo(QDateTime::currentDateTime())) : 0;
            d->jobs->setEstimate(meetingId, JobKind::Transcribe, elapsed + est);
        }
        if (!diarizing)
            d->jobs->setStage(meetingId, JobKind::Transcribe, QStringLiteral("diarize"), StageState::Skipped);
        d->jobs->setStage(meetingId, JobKind::Transcribe, QStringLiteral("upload"), StageState::Running, 0);
    }
    auto sttClock = std::make_shared<QElapsedTimer>();
    sttClock->start();

    connect(job, &providers::SttJob::uploadProgress, this, [this, meetingId](qint64 sent, qint64 total) {
        if (total <= 0) return;
        // Real byte ratio. After 100% the provider's state change closes the stage.
        const JobProgress jp = d->jobs->job(meetingId, JobKind::Transcribe);
        const JobStage* st = jp.stage(QStringLiteral("upload"));
        if (st && st->state == StageState::Running)
            d->jobs->setStagePercent(meetingId, JobKind::Transcribe, QStringLiteral("upload"),
                                     int(sent * 100 / total));
    });
    connect(job, &providers::SttJob::stateChanged, this, [this, id = m.id, diarizing](SttJobState st) {
        emit jobProgress(id, sttPhase(st));
        // During transcription the providers give NO percentage, only a state (upload / queued / processing).
        // The provider does speaker separation in the same pass as transcription, so the two stages run and
        // close together.
        const JobKind k = JobKind::Transcribe;
        if (st == SttJobState::Uploading) {
            d->jobs->setStage(id, k, QStringLiteral("upload"), StageState::Running, 0);
        } else if (st == SttJobState::Queued || st == SttJobState::Processing) {
            d->jobs->setStage(id, k, QStringLiteral("upload"), StageState::Done);
            d->jobs->setStage(id, k, QStringLiteral("transcribe"), StageState::Running);
            d->jobs->setStageDetail(id, k, QStringLiteral("transcribe"),
                                    st == SttJobState::Queued ? tr("sorban áll") : QString());
            if (diarizing)
                d->jobs->setStage(id, k, QStringLiteral("diarize"), StageState::Running);
        }
    });
    // The detailed poll message (elapsed time + heartbeat) should also reach the UI, so that during a long
    // async processing it shows it is running and the connection is alive.
    connect(job, &providers::SttJob::progress, this, [this, id = m.id](int, const QString& msg) {
        emit jobProgress(id, msg);
        d->jobs->setMessage(id, JobKind::Transcribe, msg);
    });
    connect(job, &providers::SttJob::finished, this,
            [this, meetingId, providerObj, run, job, statsKey, sttClock, diarizing](const TrackTranscript& result) {
        d->transcribeRuns.remove(meetingId);
        if (providerObj) providerObj->deleteLater();
        // The job still sends its best-effort cleanup DELETEs; after that it can be deleted.
        QTimer::singleShot(30000, job, &QObject::deleteLater);

        // A FRESH meeting: transcription may run for minutes, during which the user may have renamed or edited
        // (or deleted) the meeting; saving the old instance would overwrite that.
        Meeting mm = d->store->load(meetingId);
        if (mm.id.isEmpty()) {
            d->clearSpeakersOnTranscript.remove(meetingId);
            d->jobs->cancelled(meetingId, JobKind::Transcribe);
            finishCloudRun(run);
            return;
        }

        // An EMPTY result (the provider returned not a single word) = an ERROR: we do not write it over the
        // existing transcript. The earlier transcript, speaker names and manual corrections are untouched.
        if (result.tokens.isEmpty()) {
            d->clearSpeakersOnTranscript.remove(meetingId);
            const QString msg = mm.hasTranscript
                ? tr("Az átírás üres eredményt adott (a szolgáltató nem talált beszédet a felvételen) — "
                     "a korábbi átirat megmaradt.")
                : tr("Az átírás üres eredményt adott: a szolgáltató nem talált beszédet a felvételen.");
            JobError je;
            je.message = msg;
            d->jobs->fail(meetingId, JobKind::Transcribe, je);
            finishCloudRun(run);   // the run closes (any charge shows up this way too)
            emit errorOccurred(msg);
            return;
        }
        // Whether there was a transcript BEFORE the run (independent of the flag; see below).
        const bool hadTranscript = mm.hasTranscript
            || TranscriptRepository::hasTokens(mm.folder);

        TrackTranscript res = result;
        // We translate Soniox's diarization ids (1,2,...) into a neutral "Speaker N" label.
        for (TranscriptToken& tok : res.tokens)
            tok.speaker = tok.speaker.isEmpty()
                ? QStringLiteral("Beszélő")
                : QStringLiteral("Beszélő %1").arg(tok.speaker);

        QVector<TrackTranscript> single{res};
        MergedTranscript merged = processing::mergeTranscripts(single);
        TranscriptRepository::writeMarkdown(mm.folder, merged.renderMarkdown());
        TranscriptRepository::writeTokens(mm.folder, merged);
        TranscriptRepository::writeSegments(mm.folder, merged.segments());
        d->mergedCache.insert(mm.id, merged);
        ++d->transcriptGen[meetingId];   // a new transcript: the running identifications' results are stale
        // Re-transcription: the new diarization's "Speaker N" labels may mean OTHER people, so the old
        // assignments are cleared together with the NEW transcript. NOT only on retranscribeMeeting's flag: that
        // is lost on failure, and a plain (retried) transcription after it would leave the old names on the new
        // labels. So whenever a transcript already existed, we always clear.
        const bool flagged = d->clearSpeakersOnTranscript.remove(meetingId);
        if (flagged || hadTranscript)
            mm.speakerMap.clear();
        mm.hasTranscript = true;
        d->store->saveMeeting(mm);

        d->jobStats->addSample(statsKey, double(sttClock->elapsed()) / 1000.0,
                               double(mm.durationMs) / 1000.0);
        const JobKind k = JobKind::Transcribe;
        d->jobs->setStage(meetingId, k, QStringLiteral("upload"), StageState::Done);
        d->jobs->setStage(meetingId, k, QStringLiteral("transcribe"), StageState::Done);
        if (diarizing)
            d->jobs->setStage(meetingId, k, QStringLiteral("diarize"), StageState::Done);
        d->jobs->markIdentified(meetingId, false);   // identification has not run on the new transcript yet

        emit transcriptReady(mm.id, TranscriptRepository::markdownPath(mm.folder));
        finishCloudRun(run);   // "This transcription cost $0.41." (cloud runs only)
        // Voice ID: auto-matching the diarized speakers against the voiceprint DB, on a background thread, as
        // the last stage of the transcription task (the UI stays usable, cancellable). (If the user skipped it
        // with the toggle, the transcript finishes with anonymous speakers.) The "Who was there?" decision
        // (approval / "only me") was made before the transcript: we apply that, and automatic identification is
        // skipped. Otherwise the candidates' binding + identification.
        const bool decided = applyParticipantsToTranscript(meetingId);
        if (decided && d->voiceModelUsable())
            d->jobs->setStage(meetingId, JobKind::Transcribe, QStringLiteral("identify"),
                              StageState::Skipped);
        if (decided || d->skipIdentifyAfterTranscribe.contains(meetingId)
            || !startIdentify(meetingId, /*asTranscribeStage*/ true))
            d->jobs->finish(meetingId, JobKind::Transcribe);
    });
    connect(job, &providers::SttJob::failed, this, [this, meetingId, providerObj, run, job, sink](QString e) {
        const Impl::TranscribeRun info = d->transcribeRuns.take(meetingId);
        d->clearSpeakersOnTranscript.remove(meetingId);
        if (providerObj) providerObj->deleteLater();
        QTimer::singleShot(30000, job, &QObject::deleteLater);   // after the cleanup DELETEs

        if (info.cancelRequested) {
            // Cancel: no error, no errorOccurred. At the provider the job cleaned up (the uploaded audio +
            // transcript deleted); the earlier transcript (if any) is untouched.
            if (run) {
                if (!run->transcriptionId.isEmpty())
                    d->cloud->removePendingTranscription(run->transcriptionId);
                if (run->calls > 0 || run->balance.isValid())
                    d->cloud->refreshAccount();
            }
            d->jobs->cancelled(meetingId, JobKind::Transcribe);
            return;
        }
        JobError je = (run && run->lastError.isError())
            ? library::describeCloudFailure(JobKind::Transcribe, cloud::toError(run->lastError))
            : library::describeJobFailure(JobKind::Transcribe, e, sink->has ? &sink->ex : nullptr);
        d->jobs->fail(meetingId, JobKind::Transcribe, je);
        failCloudRun(run, tr("Átírás-hiba: %1").arg(e));
    });
}

// ---- summary and topic queue (processing::SummarizationService / TopicQueue) -----------------------

void AppController::summarizeMeeting(const QString& meetingId) { d->summaries->summarize(meetingId); }

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

// The LLM run of the tag suggester: the chat provider is made by the registry's ITagSuggester ("llm"), the
// cloud run (if any) is closed when the answer arrives.
library::TagSuggestionService::LlmSession AppController::Impl::tagLlmSession(const QString& meetingId, QObject* parent)
{
    const AppSettings s = settings->settings();
    CloudRunPtr run;
    providers::ProviderConfig cfg = q->llmConfigFor(run, meetingId, QStringLiteral("tags"));
    cfg.type = s.providers.llm.selectedId;
    library::TagSuggestionService::LlmSession session;
    session.suggester = providerRegistry.create<providers::ITagSuggester>(
        processing::LlmTagSuggestionProvider::Id, q->providerContext(run, QStringLiteral("quick")), cfg, parent);
    if (!session.suggester) return session;
    session.model = cfg.model;
    session.systemPrompt = processing::promptDefault(QStringLiteral("tags"), metaDir);
    session.done = [this, run](bool ok) {
        if (ok) q->finishCloudRun(run);
    };
    return session;
}

providers::ITextEmbedder* AppController::Impl::makeEmbedder(const QString& id, QObject* parent)
{
    const AppSettings cur = settings->settings();
    providers::ProviderConfig cfg = cur.providers.embedding.configs.value(id);
    cfg.type = id;
    providers::ProviderContext pctx;
    pctx.http = http;
    if (id == providers::embeddingproviders::CloudId) {
        // The same sign-in and header set as on the LLM path (/v1/embeddings), outside any run.
        cfg.baseUrl = cloud->apiBase();
        pctx = cloudLedger->contextFor(nullptr, QString(), http);
    } else {
        cfg.apiKey = secrets->get(providers::embeddingproviders::ApiKeySecret);
    }
    return providerRegistry.create<providers::ITextEmbedder>(id, pctx, cfg, parent);
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
    switch (kind) {
    case JobKind::Transcribe: {
        auto it = d->transcribeRuns.find(meetingId);
        if (it == d->transcribeRuns.end()) {
            // The transcript is already done, the last (identification) stage is running, so only that one is skipped.
            return d->voice->cancel(meetingId, kind);
        }
        d->jobs->setCancelling(meetingId, kind);
        if (it->mixPhase) {
            // Mixdown phase: we break the chain; the ffmpeg we started ourselves is stopped (the half-made file is
            // deleted, the earlier mix and the tracks are untouched).
            if (it->mixConn) QObject::disconnect(*it->mixConn);
            const bool owns = it->ownsMixdown;
            d->transcribeRuns.erase(it);
            d->clearSpeakersOnTranscript.remove(meetingId);
            if (owns) d->mixdown->cancelProcess(meetingId);
            d->jobs->cancelled(meetingId, kind);
            return true;
        }
        if (it->job) {
            it->cancelRequested = true;
            it->job->cancel();   // a synchronous failed("cancelled"): the failed branch closes it (without an error)
            return true;
        }
        // (In principle it does not occur: neither a mix nor a job.)
        d->transcribeRuns.erase(it);
        d->clearSpeakersOnTranscript.remove(meetingId);
        d->jobs->cancelled(meetingId, kind);
        return true;
    }
    case JobKind::Summarize:
    case JobKind::ExtractTopics:
    case JobKind::AnalyzeTopics:
        return d->summaries->cancel(meetingId, kind);   // the LLM call / the topic batch
    case JobKind::Mixdown:
        return d->mixdown->cancel(meetingId);
    case JobKind::Import:
        return d->services->import().cancel(meetingId);
    case JobKind::Export:
        return d->services->archive().cancel(meetingId);
    case JobKind::Identify:
        return d->voice->cancel(meetingId, kind);   // the thread stops before the next speaker
    }
    return false;
}

void AppController::cancelAllJobs(const QString& meetingId)
{
    for (JobKind k : {JobKind::Transcribe, JobKind::Summarize, JobKind::ExtractTopics,
                      JobKind::AnalyzeTopics, JobKind::Mixdown, JobKind::Identify})
        cancelJob(meetingId, k);
}

void AppController::setIdentifyAfterTranscription(const QString& meetingId, bool enabled)
{
    if (enabled) d->skipIdentifyAfterTranscribe.remove(meetingId);
    else         d->skipIdentifyAfterTranscribe.insert(meetingId);
}

bool AppController::identifyAfterTranscription(const QString& meetingId) const
{
    return !d->skipIdentifyAfterTranscribe.contains(meetingId);
}

bool AppController::voiceIdentificationAvailable() const
{
    return d->voiceModelUsable();
}

bool AppController::identifyMeetingAsync(const QString& meetingId)
{
    if (d->jobs->isRunning(meetingId, JobKind::Transcribe))
        return false;   // the transcription identifies by itself at the end
    return startIdentify(meetingId, /*asTranscribeStage*/ false);
}

bool AppController::startIdentify(const QString& meetingId, bool asStage)
{
    // The matches come back on VoiceAnalysisService::speakersIdentified (committed by the participant service).
    return d->voice->startIdentify(meetingId, asStage);
}

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

editing::SpeakerEditor* AppController::speakerEditor(const QString& meetingId)
{
    if (editing::SpeakerEditor* existing = d->speakerEditors.value(meetingId)) return existing;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return nullptr;

    auto* ed = new editing::SpeakerEditor(d->store, d->people.get(), d->voiceprints.get(), meetingId, this);
    ed->setSelfPerson(d->selfPerson());
    // Without voice ID (no model / TANARA_BUILD_VOICEID=OFF) the editor is fully functional even without
    // uncertainty and suggestions.
    ed->setVoiceModelIds(d->voice->editorModelIds());
    ed->setEmbedderFactory(utteranceEmbedderFactory());
    // The track activity of the side check: cache, else decoded via the media port and saved.
    ed->setActivityProvider([media = d->media](const Meeting& mm) {
        return processing::loadOrComputeSideActivity(mm, *media);
    });
    // The tag evidence for the candidate ranking (manual tag + learned association).
    ed->setTagEvidenceProvider(d->voice->candidateContext().tagEvidence);
    // Manual tags for the quick "+ #Tag" offer (the test seam, otherwise the TagService).
    {
        library::TagService* ts = d->tags;
        const auto personTags = d->voice->personTagsProvider();
        ed->setPersonTagsProvider(personTags ? personTags
                                             : [ts](const PersonId& p) { return ts ? ts->tagsOfPerson(p) : QStringList(); });
    }
    d->speakerEditors.insert(meetingId, ed);
    meetingUndo(meetingId);   // the shared undo follows from the editor's first step

    // The editor's side effects go out on the usual signals (old UI, library).
    connect(ed, &editing::SpeakerEditor::speakerMapChanged, this, [this](const QString& id) {
        d->editorEmitting = true;
        emit speakerMapChanged(id);
        d->editorEmitting = false;
    });
    connect(ed, &editing::SpeakerEditor::peopleChanged, this, &AppController::peopleChanged);
    connect(ed, &editing::SpeakerEditor::voiceprintsChanged, this, &AppController::voiceprintsChanged);
    connect(ed, &editing::SpeakerEditor::summaryStaleChanged, this,
            [this, meetingId] { emit summaryStaleChanged(meetingId); });
    // A change from outside (old UI rename, auto-identification, person rename) -> the editor rereads the disk.
    connect(this, &AppController::speakerMapChanged, ed, [this, ed](const QString& id) {
        if (!d->editorEmitting && id == ed->meetingId()) ed->refreshFromDisk();
    });
    connect(this, &AppController::voiceprintsChanged, ed, &editing::SpeakerEditor::speakersChanged);
    return ed;
}

void AppController::closeSpeakerEditor(const QString& meetingId)
{
    if (editing::SpeakerEditor* ed = d->speakerEditors.take(meetingId)) ed->deleteLater();
}

QVector<editing::PersonInfo> AppController::peopleDirectory() const
{
    if (d->people) d->people->refresh();
    return editing::listPeople(d->people.get(), d->voiceprints.get(), d->store);
}

SummaryStaleInfo AppController::summaryStale(const QString& meetingId) const
{
    const Meeting m = d->store->load(meetingId);
    editing::SpeakerEditor* ed = d->speakerEditors.value(meetingId);
    SummaryStaleInfo info = ed ? ed->summaryStale() : editing::speakeredit::summaryStale(m);
    // The editor persists after every step, so the targeted part can be computed from disk.
    if (!m.id.isEmpty()) editing::speakeredit::fillTargetedStale(m, d->personNames(), info);
    return info;
}

QVector<SummaryStatement> AppController::summaryStatements(const QString& meetingId) const
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return {};
    SummaryStaleInfo info;
    QVector<SummaryStatement> out;
    editing::speakeredit::fillTargetedStale(m, d->personNames(), info, &out);
    return out;
}

bool AppController::flagStatement(const QString& meetingId, const QString& statementId)
{
    return d->summaries->flagStatement(meetingId, statementId);   // summaryStatementsChanged (forwarded)
}

// The same for an already loaded meeting (the library calls it per row; no further disk read for meeting.json).
SummaryStaleInfo AppController::summaryStale(const Meeting& meeting) const
{
    if (editing::SpeakerEditor* ed = d->speakerEditors.value(meeting.id)) return ed->summaryStale();
    return editing::speakeredit::summaryStale(meeting);
}

editing::RetranscribeImpact AppController::retranscribeImpact(const QString& meetingId) const
{
    if (editing::SpeakerEditor* ed = d->speakerEditors.value(meetingId)) return ed->retranscribeImpact();
    return editing::speakeredit::retranscribeImpact(d->store->load(meetingId));
}

void AppController::dismissSummaryStale(const QString& meetingId)
{
    if (editing::SpeakerEditor* ed = d->speakerEditors.value(meetingId)) {
        ed->dismissSummaryStale();   // summaryStaleChanged goes out from the editor's signal
        return;
    }
    const Meeting m = d->store->load(meetingId);
    if (!m.folder.isEmpty() && editing::speakeredit::clearSummaryStale(m.folder))
        emit summaryStaleChanged(meetingId);
}


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

void AppController::startSpeechCheck(const QString& meetingId, bool thenMixdown)
{
    if (d->speechCheckRunning.contains(meetingId)) return;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    d->speechCheckRunning.insert(meetingId);
    // The measurement runs on the voice analysis's pool (cache, else computed and saved).
    d->voice->measureActivity(m, [this, meetingId, thenMixdown](const MeetingActivity& act) {
        finishSpeechCheck(meetingId, act, thenMixdown);
    });
}

void AppController::finishSpeechCheck(const QString& meetingId, const MeetingActivity& activity,
                                      bool thenMixdown)
{
    d->speechCheckRunning.remove(meetingId);
    d->speechChecked.insert(meetingId);   // on error (an undecodable track) we do not repeat either
    Meeting m = d->store->load(meetingId);
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
        d->store->saveMeeting(m);
    }
    // We signal even if nothing changed: the view leaves the "measuring" state only on this.
    emit tracksChanged(meetingId);
    if (thenMixdown) regenerateMixdown(meetingId);
    // The speechless tracks are filtered out: the voice analysis can go on over the remaining raw tracks.
    if (d->autoMixdown) maybeAnalyzeParticipants(meetingId);
}

bool AppController::speechCheckRunning(const QString& meetingId) const
{
    return d->speechCheckRunning.contains(meetingId);
}

bool AppController::checkTrackSpeech(const QString& meetingId)
{
    if (d->speechCheckRunning.contains(meetingId) || d->speechChecked.contains(meetingId)) return false;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || !editing::trackspeech::needsSpeechCheck(m)) return false;
    startSpeechCheck(meetingId, /*thenMixdown*/ false);
    return true;
}

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
    editing::ParticipantUndoScope undoScope(d->undo, meetingId, tr("Sáv beemelve"));
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    const QVector<library::tracknames::SegmentInfo> seg = library::tracknames::segments(m.tracks);
    int lead = -1;
    for (int i = 0; i < m.tracks.size(); ++i)
        if (m.tracks.at(i).id == trackId) lead = seg.value(i).leader >= 0 ? seg.value(i).leader : i;
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
    d->store->saveMeeting(m);
    emit tracksChanged(meetingId);
    regenerateMixdown(meetingId);
}

void AppController::excludeTrack(const QString& meetingId, const QString& trackId)
{
    editing::ParticipantUndoScope undoScope(d->undo, meetingId, tr("Sáv kivéve"));
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    const QVector<library::tracknames::SegmentInfo> seg = library::tracknames::segments(m.tracks);
    int lead = -1;
    for (int i = 0; i < m.tracks.size(); ++i)
        if (m.tracks.at(i).id == trackId) lead = seg.value(i).leader >= 0 ? seg.value(i).leader : i;
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
    d->store->saveMeeting(m);
    emit tracksChanged(meetingId);
    bool anyIncluded = false;
    for (const Track& t : m.tracks) anyIncluded = anyIncluded || t.included();
    if (anyIncluded) regenerateMixdown(meetingId);
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

bool AppController::applyParticipantsToTranscript(const QString& meetingId)
{
    return d->participants->applyParticipantsToTranscript(meetingId);
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