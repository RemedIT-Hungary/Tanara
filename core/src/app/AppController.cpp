#include "tanara/app/AppController.h"
#include "tanara/providers/core/ProviderConfig.h"

#include "tanara/foundation/log/Logging.h"
#include "tanara/foundation/text/Text.h"
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
#include "tanara/processing/summary/ComplexSummaryService.h"
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
#include "tanara/library/tags/TagSuggester.h"
#include "tanara/processing/analysis/LlmTagSuggester.h"
#include "tanara/library/index/EmbeddingIndex.h"
#include "tanara/library/index/EmbeddingPreparer.h"
#include "tanara/providers/embedding/ITextEmbedder.h"
#include "tanara/editing/undo/MeetingUndo.h"
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
#include <QUuid>
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

// Which track to take audio from for a raw speaker label:
//  - an exact match with a track's fixed speaker (mic) -> that track;
//  - otherwise (a diarized "Remote N") -> the loopback track;
//  - last fallback: the first track.
const Track* resolveTrackForLabel(const Meeting& m, const MergedTranscript& mt,
                                  const QString& rawLabel) {
    // 1) Primarily by the trackId of the label's tokens (diarized mic/loopback speakers, "Microphone N" /
    //    "Remote N", thus resolve to the RIGHT track).
    QString tid;
    for (const TranscriptToken& t : mt.tokens)
        if (t.speaker == rawLabel && !t.trackId.isEmpty()) { tid = t.trackId; break; }
    if (!tid.isEmpty())
        for (const Track& t : m.tracks)
            if (t.id == tid) return &t;
    // 2) Fallback: an exact track speaker name match (the fixed mic name).
    for (const Track& t : m.tracks)
        if (t.speakerLabel == rawLabel) return &t;
    // 3) Fallback: the first loopback track.
    for (const Track& t : m.tracks)
        if (t.kind == TrackKind::Loopback) return &t;
    return m.tracks.isEmpty() ? nullptr : &m.tracks.first();
}

// A speaker's representative embedding (with every model in use): from their longest utterances it
// collects ~3-12 s of audio from the given track, and computes one vector per model from ONE concatenated
// PCM. Empty = not enough audio / an error. The utterances are in meeting time; offsetMs is the audio
// file's start in the meeting (Track::startOffsetMs for a track file, 0 for the mixdown); the part before
// the file is dropped. refs (optional): the spans used in "fileRel#start-end" form (meeting time), the
// voiceprint's sourceRefs.
EmbeddingSet embeddingForLabel(const media::MediaTool& media, const processing::VoiceEmbedderSet& set,
                               const QString& audioPath, qint64 offsetMs,
                               const MergedTranscript& mt, const QString& rawLabel,
                               const QString& fileRel = QString(), QStringList* refs = nullptr) {
    QVector<Utterance> utts;
    for (const Utterance& u : mt.segments())
        if (u.speaker == rawLabel) utts.append(u);
    std::sort(utts.begin(), utts.end(), [](const Utterance& a, const Utterance& b) {
        return (a.endMs - a.startMs) > (b.endMs - b.startMs);
    });
    const QString& path = audioPath;
    QVector<float> pcm;
    qint64 accMs = 0;
    for (const Utterance& u : utts) {
        if (accMs >= 3000 || pcm.size() > 16000 * 12) break;
        const tanara::tracktiming::FileRange r = tanara::tracktiming::meetingToFileRange(offsetMs, u.startMs, u.endMs);
        if (!r.valid()) continue;   // the track was not sounding yet at this time
        const QVector<float> part = processing::VoiceEmbedder::decodePcm16kMono(media, path, r.startMs, r.endMs);
        if (part.isEmpty()) continue;
        pcm += part;
        accMs += (r.endMs - r.startMs);
        if (refs)
            *refs << QStringLiteral("%1#%2-%3").arg(fileRel).arg(r.startMs + offsetMs).arg(r.endMs + offsetMs);
    }
    if (pcm.isEmpty()) return {};
    return set.embedPcm(pcm);
}

// Parsing a sample reference ("file#startMs-endMs", meeting time); false if there is no range.
bool parseRangeRef(const QString& ref, QString* file, qint64* startMs, qint64* endMs) {
    const int hash = ref.lastIndexOf(QLatin1Char('#'));
    if (hash <= 0) return false;
    *file = ref.left(hash);
    const QString range = ref.mid(hash + 1);
    const int dash = range.indexOf(QLatin1Char('-'));
    if (dash <= 0) return false;
    bool ok1 = false, ok2 = false;
    *startMs = range.left(dash).toLongLong(&ok1);
    *endMs = range.mid(dash + 1).toLongLong(&ok2);
    return ok1 && ok2 && *endMs > *startMs;
}

// One voiceprint with ONE model, recomputed from its audio (lazy backfill). It concatenates the PCM of
// the spans (sourceRefs, failing that the sampleRef) and embeds it as a whole, the same way
// enrollSpeaker's voiceprint is made (embeddingForLabel); this differs slightly from the editor's
// weighted per-line average, but is self-consistent per model. The spans are in meeting time: for a track
// file the track's offset is subtracted. missingAudio: the referenced file is missing.
QVector<float> embedPrintSource(const media::MediaTool& media, const processing::VoiceEmbedderSet& set,
                                const QString& modelId, const Meeting& m,
                                const Voiceprint& vp, bool* missingAudio) {
    *missingAudio = false;
    const QStringList refs = vp.sourceRefs.isEmpty() ? QStringList{vp.sampleRef} : vp.sourceRefs;
    QVector<float> pcm;
    for (const QString& ref : refs) {
        QString file;
        qint64 s = 0, e = 0;
        if (!parseRangeRef(ref, &file, &s, &e)) continue;
        const QString abs = QDir(m.folder).filePath(file);
        if (!QFileInfo::exists(abs)) { *missingAudio = true; continue; }
        qint64 offset = 0;
        for (const Track& t : m.tracks)
            if (t.file == file) { offset = t.startOffsetMs; break; }
        const tanara::tracktiming::FileRange r = tanara::tracktiming::meetingToFileRange(offset, s, e);
        if (!r.valid()) continue;
        pcm += processing::VoiceEmbedder::decodePcm16kMono(media, abs, r.startMs, r.endMs);
    }
    if (pcm.isEmpty()) return {};
    return set.embedPcmWith(pcm, {modelId}).value(modelId);
}

// Voiceprints from one sample: one per model (same sampleRef / source / createdAt / sourceRefs, own id
// and model). The template's embedding, id and model fields do not count.
QVector<Voiceprint> printsFromSet(const EmbeddingSet& set, const Voiceprint& tmpl) {
    QVector<Voiceprint> out;
    for (auto it = set.cbegin(); it != set.cend(); ++it) {
        if (it.value().isEmpty()) continue;
        Voiceprint vp = tmpl;
        vp.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        vp.model = it.key();
        vp.embedding = it.value();
        vp.dim = it.value().size();
        out.append(vp);
    }
    return out;
}

// The reference of the playable representative segment stored with the voiceprint:
// "track_file#startMs-endMs" (the longest utterance). The times are in MEETING time (as in the
// transcript); for a track file the player converts to file time with the track's offset (PeopleService).
// For playback the People panel resolves the folder from sourceMeetingId.
QString representativeSampleRef(const MergedTranscript& mt, const QString& rawLabel,
                                const QString& fileRel) {
    qint64 bestS = -1, bestE = -1, bestDur = -1;
    for (const Utterance& u : mt.segments())
        if (u.speaker == rawLabel && (u.endMs - u.startMs) > bestDur) {
            bestDur = u.endMs - u.startMs; bestS = u.startMs; bestE = u.endMs;
        }
    if (bestS < 0)
        return fileRel;
    return QStringLiteral("%1#%2-%3").arg(fileRel).arg(bestS).arg(bestE);
}

// The voice-ID audio source: NOW the mixdown (the transcript is made from it too, so the diarized
// "Speaker N" labels' time windows fit the mix directly). If there is no mixdown (an old, per-track
// meeting), for back-compat we fall back to the label's resolved track.
struct VoiceSource { QString absPath; QString fileRel; QString trackId; QString device;
                     qint64 offsetMs = 0; };   // the file's start in the meeting
VoiceSource resolveVoiceSource(const Meeting& m, const MergedTranscript& mt,
                               const QString& rawLabel) {
    const QString mixRel = MeetingLayout::mixdownOf(m);
    const QString mixAbs = QDir(m.folder).filePath(mixRel);
    if (QFile::exists(mixAbs))
        return { mixAbs, mixRel, QStringLiteral("mixdown"), QString() };
    if (const Track* t = resolveTrackForLabel(m, mt, rawLabel))
        return { QDir(m.folder).filePath(t->file), t->file, t->id, t->deviceName, t->startOffsetMs };
    return {};
}

using processing::participants::clusterEmbeddings;   // agglomerative clustering (processing/analysis/ParticipantAnalysis.h)

// Cosine threshold for auto-identification (validation: same 0.77, cross <= 0.35).
constexpr double kVoiceMatchThreshold = 0.5;

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

QString slugify(const QString& s) {
    return text::slug(s, QStringLiteral("meeting"));
}

// The last FAILED HTTP exchange of a provider call (for the remembered error's technical line).
struct FailureSink { bool has = false; net::HttpExchange ex; };
using FailureSinkPtr = std::shared_ptr<FailureSink>;

// Extends the context's response observer: besides the existing (cloud) observer, non-2xx responses and
// network errors are also stored in the sink. The failure of cleanup DELETEs does not count.
void captureFailures(providers::ProviderContext& ctx, const FailureSinkPtr& sink) {
    const auto prev = ctx.observe;
    ctx.observe = [prev, sink](const net::HttpExchange& ex) {
        if (prev) prev(ex);
        const bool ok = ex.status >= 200 && ex.status < 300;
        if (!ok && ex.method != "DELETE") { sink->has = true; sink->ex = ex; }
    };
}

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
    library::PeopleService* peopleService = nullptr;
    std::unique_ptr<library::VoiceprintStore> voiceprints;
    QString          voiceModelPath;    // expected location of the base model (diagnostics)
    QString          appDir;            // location of the packaged models (<appDir>/models)
    // The speaker models in use (enabled intersect available): the main thread's set, loads lazily.
    processing::VoiceEmbedderSet voiceSet;
    QStringList      voiceModelIds;     // ids of voiceSet (alphabetical)
    processing::PcmEmbedderLoader voiceLoader;      // test seam; empty = the real VoiceEmbedder
    // Lazy voiceprint backfill (backfillVoiceprints): a 1-thread pool, one run at a time.
    QThreadPool      backfillPool;
    bool             backfillRunning = false;
    bool             backfillAgain = false;
    std::shared_ptr<std::atomic<bool>> backfillCancel = std::make_shared<std::atomic<bool>>(false);
    QSet<QString>    backfillLogged;    // voiceprints already logged because of missing audio
    // Per-track speech share and pre-transcript voice analysis (background thread; the GUI thread does not wait).
    QThreadPool      analysisPool;
    QSet<QString>    speechCheckRunning;  // meetingIds being measured now
    QSet<QString>    speechChecked;       // already measured in this process (not repeated even on error)
    QHash<QString, MeetingActivity> activityView;   // activity for the speech strip (every track), with a fingerprint
    QSet<QString>    activityViewRunning; // meetingIds being computed now
    QHash<QString, std::shared_ptr<std::atomic<bool>>> analysisRuns;   // meetingId -> cancel flag
    std::shared_ptr<std::atomic<bool>> analysisShutdown = std::make_shared<std::atomic<bool>>(false);
    std::function<QStringList(const PersonId&)> personTags;  // test seam (a person's tags)
    processing::participants::CandidateContext candidateContext() const;
    bool             voiceModelsApplied = false;
    // The editor's model list: the models in use; if none is usable (no model file), the enabled ones:
    // with vectors computed earlier in the meeting's cache the analysis works anyway.
    QStringList editorModelIds(const SettingsManager* st) const {
        return voiceModelIds.isEmpty() ? st->enabledVoiceModels() : voiceModelIds;
    }
    int              backfillAdded = 0;
    // ---- shared undo + backfill report ----
    QHash<QString, QPointer<editing::MeetingUndoService>> undoServices;   // meetingId -> shared stack
    QHash<QString, int> undoScopeDepth;                           // nesting of ParticipantUndoScope
    QVector<processing::VoiceprintBackfillSkip> backfillSkipped;              // samples skipped by the latest backfill
    int              backfillDone = 0;
    int              backfillTotal = 0;
    void noteBackfillSkip(const PersonId& person, const QString& model, const Voiceprint& vp,
                          processing::VoiceprintBackfillSkip::Reason reason);
    // A voiceprint to backfill: the sibling group's representative + the missing model + the source meeting.
    struct BackfillJob { PersonId person; QString model; Voiceprint print; Meeting meeting; };
    // ---- people ----
    // The current display names (people.json); for transcript.md, summaries, source references.
    PersonNames personNames() const
    {
        if (!people) return {};
        people->refresh();
        return people->names();
    }
    SelfPerson selfPerson() const
    {
        SelfPerson self;
        if (people) { people->refresh(); self.id = people->selfId(); }
        if (settings) self.name = settings->settings().recording.userSpeakerName.trimmed();
        return self;
    }
    // Makes sure the user's own person exists in people.json ("self"), named after the settings' name.
    void ensureSelf();
    // Whether the meeting refers to the person anywhere (speakerMap, participants, approval, overlay).
    static bool meetingRefersTo(const Meeting& m, const PersonId& person);
    // transcript.md with the current names + speakerMapChanged (after a person change).
    void refreshMeetingNames(const Meeting& m);
    void finishBackfillJob(const BackfillJob& job, const QVector<float>& vec, bool missingAudio);
    void finishBackfill();
    QString          metaDir;     // AppPaths::dataDir()
    QString          modelsDir;   // AppPaths::modelsDir()
    QString          notesDir;

    // The recording and the mixdown: services()->recording() / mixdown().
    recording::RecordingService* recording = nullptr;
    recording::MixdownService*   mixdown = nullptr;
    bool        autoMixdown = true;             // automatic mixdown after recording / import
    QHash<QString, MergedTranscript> mergedCache;

    // Topic-analysis job queue (complex pass 2): ONE LLM call runs at a time (local model, parallel=1),
    // the other topics wait in line. Each topic can be (re)started separately.
    struct TopicJob { QString meetingId; SummaryTopic topic; };
    QVector<TopicJob> topicJobQueue;
    bool    topicJobActive = false;
    QString activeTopicMeetingId;   // addressee of the running job (dedup + lifecycle signals)
    QString activeTopicId;
    QSet<QString> reduceWhenDone;             // meetingIds where an auto-reduce follows at the end of the queue
    QHash<QString, QPair<int,int>> jobCounts; // meetingId -> (ok, fail) in the current batch

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
    // Per meeting the cloud run of the open complex summary (topic gathering -> topic analyses -> reduce):
    // ONE X-Tanara-Job-Id, the per-call charges summed up (the "12 parts" case).
    // On completion (done / error / cancel) it is removed from here; a continuation is thus a new run.
    QHash<QString, CloudRunPtr> complexRuns;

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
    processing::LlmTagSuggester*   llmTags = nullptr;
    QString            llmTagsMeetingId;         // the meeting of the running LLM suggestion
    QPointer<QObject>  llmTagsProvider;
    CloudRunPtr        llmTagsRun;
    QHash<QString, QVector<TagSuggestion>> similarCache;    // meetingId -> the similarity-based list
    QHash<QString, QVector<TagSuggestion>> pendingTags;     // meetingId -> the last list published
    QSet<QString>      tagRequests;              // requests waiting for the profiles to be ready
    // The similarity-based list is made on a background thread. At most one runs per meeting; a request
    // arriving meanwhile gets its result. tagDataGen grows on every cache clear: a result computed from an
    // older state is dropped and (if still awaited) recomputed.
    struct TagRun {
        bool running = false;
        bool publish = false;    // requestTagSuggestions is waiting for it
        bool llm = false;        // requestLlmTagSuggestions is waiting for it (for the candidate list)
    };
    QHash<QString, TagRun> tagRuns;
    quint64            tagDataGen = 0;
    QThreadPool        tagPool;                  // 1 thread; ~AppController waits for it
    QString            embeddingKey;             // provider|baseUrl|model, to detect a model change

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

    // A running LLM call (summary / topic gathering / combining / the active topic analysis).
    struct LlmRun {
        QPointer<QObject> svc; QPointer<QObject> provider; CloudRunPtr cloudRun;
        QPointer<providers::ILocalModelServer> preparer;   // the model preparation (LM Studio) while it lasts
    };
    QHash<QString, LlmRun> llmRuns;            // llmKey(meetingId, kind) -> run
    LlmRun activeTopicRun;
    QHash<QString, int> topicBatchTotal;       // meetingId -> number of topics queued into the current batch

    // ---- LLM context (see providers/chat/LlmContext.h, providers/chat/LlmServer.h) ----
    // A run's context knowledge for classifying errors: the estimated need, and what the server probe / the
    // load said.
    struct LlmContextState { int need = 0; bool lmStudio = false; int loadedContext = -1; };
    using LlmContextPtr = std::shared_ptr<LlmContextState>;
    // The minimum requested by the user ("Load with a larger context"): the next preparation uses it ONCE.
    // Key: server root|model.
    QHash<QString, int> contextFloor;
    // For a server without a native API, the context learned from the error message: the next summary
    // already adapts the parts to it (in memory only).
    QHash<QString, int> learnedContext;
    static QString serverKey(const providers::ProviderConfig& cfg) {
        return providers::llmctx::nativeApiRoot(cfg.baseUrl) + QLatin1Char('|') + cfg.model.trimmed();
    }
    // Tanara is currently running ANOTHER LLM request too (then we do not unload the model).
    bool otherLlmBusy() const { return llmRuns.size() + (topicJobActive ? 1 : 0) > 1; }
    // Preparing the model before an LLM task. For Cloud (and if there is nothing to do) `go` runs at once;
    // for LM Studio, if needed, a load / reload as the task's "model" stage. The caller binds the returned
    // preparer to the run (for cancelling).
    providers::ILocalModelServer* prepareLlm(const QString& meetingId, JobKind kind, const providers::ProviderConfig& cfg,
                                 bool cloud, const LlmContextPtr& ctx, std::function<void()> go,
                                 std::function<void(const JobError&)> onFail);

    // A running (background-thread) identification.
    struct IdentifyRun { QThread* thread = nullptr; std::shared_ptr<std::atomic<bool>> cancel; bool asStage = false; };
    // The transcript's state at the start of an identification (see startIdentify): if it differs at the
    // end, the result refers to the labels of the OLD transcript, so we drop it.
    QString transcriptStamp(const QString& meetingId, const QString& folder) const {
        const FileStamp fs = FileStamp::of(TranscriptRepository::tokensPath(folder));
        return QStringLiteral("%1/%2/%3").arg(transcriptGen.value(meetingId)).arg(fs.mtimeMs).arg(fs.size);
    }
    QHash<QString, IdentifyRun> identifyRuns;

    static QString llmKey(const QString& meetingId, JobKind kind) {
        return meetingId + QLatin1Char('|') + QString::number(int(kind));
    }
    bool transcribeInMixPhase(const QString& meetingId) const {
        const auto it = transcribeRuns.constFind(meetingId);
        return it != transcribeRuns.constEnd() && it->mixPhase;
    }
    bool voiceModelUsable() const;
    void updateTopicCounts(const QString& meetingId);
    void abortLlmRun(const LlmRun& run);
    JobError describeLlmFailure(JobKind kind, const QString& raw, const CloudRunPtr& run,
                                const FailureSinkPtr& sink,
                                const std::shared_ptr<LlmContextState>& ctx = nullptr) const;
    void applyIdentification(const QString& meetingId,
                             const QVector<QPair<QString, EmbeddingSet>>& embeddings);
    bool matchSpeaker(Meeting& m, const QString& rawLabel, const EmbeddingSet& embedding);
    void commitIdentification(const Meeting& m, MergedTranscript merged,
                              const QStringList& identifiedLabels);
};

// ==== the shared undo step of the participant operations ===================================
// RAII: BEFORE the operation it takes a snapshot of the meeting's "participant state" (participants,
// approval, description, track inclusion), and AFTER it, if changed, pushes one step onto the meeting's
// shared stack (MeetingUndoService) that restores this state. An editor step created during the operation
// (bindings) is absorbed by the step. When nested, only the outermost records (e.g. removeParticipant ->
// unbindParticipant).
namespace {
QJsonObject participantUndoState(const Meeting& m)
{
    QJsonArray ps;
    for (const Participant& p : m.participants) ps.append(library::toJson(p));
    QJsonArray ts;
    for (const Track& t : m.tracks)
        ts.append(QJsonObject{{QStringLiteral("id"), t.id}, {QStringLiteral("active"), t.active},
                              {QStringLiteral("excludedReason"), t.excludedReason}});
    return QJsonObject{{QStringLiteral("participants"), ps},
                       {QStringLiteral("approval"), m.approval ? QJsonValue(library::toJson(*m.approval)) : QJsonValue()},
                       {QStringLiteral("contextNote"), m.contextNote},
                       {QStringLiteral("tracks"), ts}};
}

quint64 editorTop(editing::SpeakerEditor* ed)
{
    if (!ed) return 0;
    const QVector<quint64> s = ed->undoSerials();
    return s.isEmpty() ? 0 : s.last();
}
} // namespace

struct AppController::ParticipantUndoScope {
    AppController* app = nullptr;
    QString meetingId;
    QString text;
    bool    active = false;
    Meeting before;
    quint64 editorTopBefore = 0;

    ParticipantUndoScope(AppController* a, const QString& id, const QString& label)
        : app(a), meetingId(id), text(label)
    {
        if (id.isEmpty() || app->d->undoScopeDepth[id]++ > 0) return;
        before = app->d->store->load(id);
        if (before.id.isEmpty()) return;
        editorTopBefore = editorTop(app->d->speakerEditors.value(id));
        active = true;
    }
    ~ParticipantUndoScope()
    {
        if (!meetingId.isEmpty() && --app->d->undoScopeDepth[meetingId] <= 0)
            app->d->undoScopeDepth.remove(meetingId);
        if (!active) return;
        const Meeting after = app->d->store->load(meetingId);
        if (after.id.isEmpty() || participantUndoState(after) == participantUndoState(before)) return;
        const quint64 top = editorTop(app->d->speakerEditors.value(meetingId));
        editing::MeetingUndoService* undo = app->meetingUndo(meetingId);
        if (!undo) return;
        QPointer<AppController> self(app);
        const Meeting snap = before;
        undo->pushStep(text, [self, snap]() { if (self) restore(self, snap); },
                       top != editorTopBefore ? top : 0);
    }
    // Writing the snapshot back (the rest of the meeting: title, tags, transcript, is untouched).
    static void restore(AppController* app, const Meeting& snap)
    {
        Meeting m = app->d->store->load(snap.id);
        if (m.id.isEmpty()) return;
        bool tracksChanged = false;
        for (Track& t : m.tracks)
            for (const Track& o : snap.tracks)
                if (o.id == t.id && (o.active != t.active || o.excludedReason != t.excludedReason)) {
                    t.active = o.active;
                    t.excludedReason = o.excludedReason;
                    tracksChanged = true;
                }
        m.participants = snap.participants;
        m.approval = snap.approval;
        m.contextNote = snap.contextNote;
        if (tracksChanged) m.mixdownDirty = true;
        app->d->store->saveMeeting(m);
        emit app->participantsChanged(m.id);
        if (tracksChanged) {
            emit app->tracksChanged(m.id);
            bool anyIncluded = false;
            for (const Track& t : m.tracks) anyIncluded = anyIncluded || t.included();
            if (anyIncluded) app->regenerateMixdown(m.id);
        }
    }
};

bool AppController::Impl::voiceModelUsable() const
{
    if (voiceSet.isEmpty()) return false;
#ifdef TANARA_HAVE_VOICEID
    return true;
#else
    return bool(voiceLoader);   // the build has no voice ID; only with the test loader
#endif
}

void AppController::Impl::updateTopicCounts(const QString& meetingId)
{
    if (!jobs->isRunning(meetingId, JobKind::AnalyzeTopics)) return;
    const QPair<int, int> c = jobCounts.value(meetingId);
    jobs->setCounts(meetingId, JobKind::AnalyzeTopics, c.first + c.second,
                    topicBatchTotal.value(meetingId));
}

// Cancelling a running LLM call: we detach the service's signals, delete the provider (with it the job
// and the network request); the connection to the provider breaks, no result arrives.
void AppController::Impl::abortLlmRun(const LlmRun& run)
{
    if (run.preparer) {
        run.preparer->cancel();   // the query / load request is dropped (no signal comes)
        run.preparer->deleteLater();
    }
    if (auto* summary = qobject_cast<processing::SummaryService*>(run.svc.data()))
        summary->cancel();   // the running call is cancelled, the next one does not start (the part notes stay)
    if (run.svc) {
        run.svc->disconnect(q);
        run.svc->deleteLater();
    }
    if (run.provider) run.provider->deleteLater();
}

JobError AppController::Impl::describeLlmFailure(JobKind kind, const QString& raw,
                                                 const CloudRunPtr& run,
                                                 const FailureSinkPtr& sink,
                                                 const LlmContextPtr& ctx) const
{
    if (run && run->lastError.isError())
        return library::describeCloudFailure(kind, cloud::toError(run->lastError));
    library::ContextFailureHint hint;
    hint.cloud = bool(run);
    if (ctx) {
        hint.lmStudio = ctx->lmStudio && !run;
        if (ctx->need > 0) hint.recommendedContext = providers::llmctx::contextStepFor(ctx->need);
    }
    return describeJobFailure(kind, raw, (sink && sink->has) ? &sink->ex : nullptr, &hint);
}

providers::ILocalModelServer* AppController::Impl::prepareLlm(const QString& meetingId, JobKind kind,
                                                  const providers::ProviderConfig& cfg, bool cloud,
                                                  const LlmContextPtr& ctx, std::function<void()> go,
                                                  std::function<void(const JobError&)> onFail)
{
    // Behind Tanara Cloud there is no native API: neither a probe nor a load.
    if (cloud) { go(); return nullptr; }
    // The selected chat provider's local-server handler (LM Studio for "openai-compat"); none = nothing
    // to prepare.
    providers::ProviderContext pctx;
    pctx.http = http;
    auto* prep = providerRegistry.create<providers::ILocalModelServer>(
        settings->settings().providers.llm.selectedId, pctx, cfg, q);
    if (!prep) { go(); return nullptr; }
    prep->setBusyCheck([this] { return otherLlmBusy(); });
    const int floor = contextFloor.take(serverKey(cfg));
    QObject::connect(prep, &providers::ILocalModelServer::loadingStarted, prep, [this, prep, meetingId, kind](int ctxLen) {
        // Visible first stage: "Loading the model" (cancellable like the rest of the task).
        QVector<JobStage> stages = jobs->job(meetingId, kind).stages;
        const QStringList others = prep->serverInfo().otherLoaded;
        QString detail = AppController::tr("%1 tokenes kontextus, egy szálon").arg(QStringLiteral("%L1").arg(ctxLen));
        if (!others.isEmpty())
            detail += AppController::tr(" · a szerveren más modell is betöltve: %1").arg(others.join(QStringLiteral(", ")));
        stages.prepend({ QStringLiteral("model"), AppController::tr("Modell betöltése"), StageState::Running, -1, detail });
        jobs->setStages(meetingId, kind, stages);
        jobs->setMessage(meetingId, kind, AppController::tr("Modell betöltése…"));
        emit q->jobProgress(meetingId, AppController::tr("Modell betöltése (%1 tokenes kontextus)…")
                                           .arg(QStringLiteral("%L1").arg(ctxLen)));
    });
    QObject::connect(prep, &providers::ILocalModelServer::ready, prep, [this, prep, meetingId, kind, ctx, go] {
        ctx->lmStudio = prep->serverInfo().isLmStudio();
        ctx->loadedContext = prep->loadedContext();
        if (jobs->job(meetingId, kind).stage(QStringLiteral("model")))
            jobs->setStage(meetingId, kind, QStringLiteral("model"), StageState::Done);
        prep->deleteLater();
        go();
    });
    QObject::connect(prep, &providers::ILocalModelServer::failed, prep, [this, prep, meetingId, kind, onFail](JobError e) {
        e.kind = kind;
        if (jobs->job(meetingId, kind).stage(QStringLiteral("model")))
            jobs->setStage(meetingId, kind, QStringLiteral("model"), StageState::Failed);
        prep->deleteLater();
        onFail(e);
    });
    prep->start(ctx->need, floor);
    return prep;
}

// Matching the speaker embeddings computed on a background thread against the voiceprint DB + saving
// (main thread). The same rule as in autoIdentifyMeeting: only the still anonymous labels, above the
// threshold; transcript.md is regenerated with the names; speakerMapChanged signal.
void AppController::Impl::applyIdentification(
    const QString& meetingId, const QVector<QPair<QString, EmbeddingSet>>& embeddings)
{
    if (embeddings.isEmpty() || !voiceprints) return;
    Meeting m = store->load(meetingId);
    if (m.id.isEmpty()) return;
    QStringList identified;
    for (const auto& pair : embeddings) {
        if (pair.second.isEmpty() || m.speakerMap.contains(pair.first)) continue;
        if (matchSpeaker(m, pair.first, pair.second)) identified << pair.first;
    }
    if (identified.isEmpty()) return;
    commitIdentification(m, TranscriptRepository::readTokens(m.folder),
                         identified);
}

// THE identification rule in ONE place (shared by the synchronous autoIdentifyMeeting and the
// asynchronous path): the speaker's embedding against the voiceprint DB; above the threshold the name goes
// into the speakerMap, the person into the name list, the score into the overlay (the editor's
// "recognized by voice (82%)").
bool AppController::Impl::matchSpeaker(Meeting& m, const QString& rawLabel,
                                       const EmbeddingSet& embedding)
{
    if (!voiceprints || embedding.isEmpty()) return false;
    const VoiceMatch match = voiceprints->bestMatch(embedding, embedding.keys());
    if (match.score < kVoiceMatchThreshold || match.personId.isNull()) return false;
    if (people && !people->contains(match.personId)) return false;   // a voiceprint of an unknown person
    m.speakerMap.insert(rawLabel, match.personId);
    editing::speakeredit::recordIdentification(m.folder, rawLabel, match.personId, match.score);
    return true;
}

// Saving the identification result: meeting.json, transcript.md with the resolved names, marking the
// summary stale (if there is one), and the signals.
void AppController::Impl::commitIdentification(const Meeting& m, MergedTranscript merged,
                                               const QStringList& identifiedLabels)
{
    store->saveMeeting(m);
    if (!merged.tokens.isEmpty()) {
        editing::speakeredit::applyResolvedSpeakers(merged, m, personNames());
        TranscriptRepository::writeMarkdown(m.folder, merged.renderMarkdown());
    }
    if (editing::speakeredit::markSummaryStale(m, identifiedLabels)) emit q->summaryStaleChanged(m.id);
    emit q->speakerMapChanged(m.id);
}

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
    d->ensureSelf();

    // Voice ID: the voiceprint DB + the model's location: <metaDir>/models/..., if absent, the
    // <appDir>/models/... packaged next to the application (Windows zip / installer).
    d->voiceprints = std::make_unique<library::VoiceprintStore>(
        ap.dataFile(paths::files::Voiceprints));
    d->appDir = QCoreApplication::instance() ? QCoreApplication::applicationDirPath() : QString();
    d->voiceModelPath = processing::VoiceModelRegistry::resolveDefaultModelPath(d->modelsDir, d->appDir);
    d->backfillPool.setMaxThreadCount(1);
    d->backfillPool.setObjectName(QStringLiteral("tanara-voiceprint-backfill"));
    d->analysisPool.setMaxThreadCount(2);
    d->analysisPool.setObjectName(QStringLiteral("tanara-participant-analysis"));

    // People window: statistics computed in the background, and the operations (merge, moving a sample ...).
    // Nicknames / the note live in the people.json records.
    d->peopleStats = new library::PeopleStats(d->store, this);
    d->peopleService = new library::PeopleService(this, d->store, d->people.get(), d->voiceprints.get(),
                                         d->peopleStats, this);
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
    {
        app::ServicesDeps sd;
        sd.store = d->store;
        sd.tags  = d->tags;
        sd.jobs  = d->jobs;
        sd.media = d->media;
        sd.settings = d->settings;
        d->services = std::make_unique<app::Services>(sd);
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
    d->profiles = new library::MeetingProfiles(d->store, this);
    d->tags->setProfiles(d->profiles);
    // Tags on people: manual tags in people.json; the own person is left out.
    d->tags->setPeopleStore(d->people.get());
    d->tags->setSelfProvider([this]() { return d->people ? d->people->selfId() : PersonId(); });
    connect(this, &AppController::peopleChanged, d->tags, &library::TagService::invalidatePersonStats);
    d->library->setTagService(d->tags);
    d->library->setPeopleStore(d->people.get());
    d->embeddings = new library::EmbeddingIndex(d->store, this);
    d->embeddings->setProfiles(d->profiles);
    d->embeddingPreparer = new library::EmbeddingPreparer(
        d->store, d->embeddings, ap.dataFile(paths::files::EmbeddingState), this);
    d->llmTags = new processing::LlmTagSuggester(d->tags, this);
    d->tagPool.setMaxThreadCount(1);
    d->tagPool.setObjectName(QStringLiteral("tanara-tag-suggest"));
    connect(d->profiles, &library::MeetingProfiles::idle, this, [this]() {
        const QSet<QString> waiting = d->tagRequests;
        d->tagRequests.clear();
        for (const QString& id : waiting) startTagSuggestionJob(id);
    });
    // The profiles' rows (meeting.json files) and phrases are built on a background thread already at
    // startup; from then on the main thread's readers (tag profile, draft suggestion) never load or compute
    // synchronously, they get the finished snapshot.
    d->profiles->ensureBuilt();
    // The person <-> tag statistics also on a background thread (the main thread reads the finished snapshot).
    d->tags->ensurePersonStats();
    // The similarity-based list stays valid until the tags, transcripts and the index change.
    auto dropTagCache = [this]() { invalidateTagSuggestions(); };
    connect(d->tags, &library::TagService::tagsChanged, this, dropTagCache);
    connect(d->embeddings, &library::EmbeddingIndex::indexChanged, this, dropTagCache);
    connect(d->embeddings, &library::EmbeddingIndex::indexReset, this, dropTagCache);
    // An applied / rejected suggestion: the published list goes out again filtered (without recomputation).
    connect(d->tags, &library::TagService::meetingTagsChanged, this, [this](const QString& id) {
        invalidateTagSuggestions();
        if (d->pendingTags.contains(id)) publishTagSuggestions(id, d->pendingTags.value(id));
    });
    // The participants (manual / approved) and the people's tags changed: the "by participants" suggestion
    // goes out again (the published list keeps the similarity-based part).
    connect(this, &AppController::participantsChanged, this, [this](const QString& id) {
        if (d->pendingTags.contains(id)) publishTagSuggestions(id, d->pendingTags.value(id));
    });
    connect(d->tags, &library::TagService::personTagsChanged, this, [this]() {
        const QStringList ids = d->pendingTags.keys();
        for (const QString& id : ids) publishTagSuggestions(id, d->pendingTags.value(id));
    });
    connect(d->tags, &library::TagService::rejectedChanged, this, [this]() {
        invalidateTagSuggestions();
        const QStringList ids = d->pendingTags.keys();
        for (const QString& id : ids) {
            const QVector<TagSuggestion> before = d->pendingTags.value(id);
            publishTagSuggestions(id, before);
        }
    });
    connect(this, &AppController::transcriptReady, this, [this](const QString& meetingId) {
        invalidateTagSuggestions();
        d->profiles->invalidate(meetingId);
        d->embeddingPreparer->enqueue(meetingId);   // only if there is an embedding provider
    });
    connect(this, &AppController::summaryReady, this, [this](const QString& meetingId) {
        // Only if a tag set already exists: with an empty set the user is not tagging yet, and a model call
        // after every summary (with new-name ideas) would be intrusive and superfluous.
        const AppSettings st = d->settings->settings();
        if (st.processing.tagSuggestions && st.processing.llmTagSuggestions && !d->tags->all().isEmpty())
            requestLlmTagSuggestions(meetingId);
    });
    connect(d->llmTags, &processing::LlmTagSuggester::finished, this,
            [this](const QString& meetingId, const QVector<TagSuggestion>& list) {
        if (d->llmTagsProvider) d->llmTagsProvider->deleteLater();
        d->llmTagsProvider = nullptr;
        finishCloudRun(d->llmTagsRun);
        d->llmTagsRun.reset();
        d->llmTagsMeetingId.clear();
        // An empty LLM response: the earlier suggestions stay.
        publishTagSuggestions(meetingId, list.isEmpty() ? d->pendingTags.value(meetingId) : list);
    });
    connect(d->llmTags, &processing::LlmTagSuggester::failed, this, [this](const QString& meetingId, const QString& error) {
        if (d->llmTagsProvider) d->llmTagsProvider->deleteLater();
        d->llmTagsProvider = nullptr;
        d->llmTagsRun.reset();
        d->llmTagsMeetingId.clear();
        // A suggestion is not an obstacle: on error there is no popup, only a log entry; the earlier list stays.
        qWarning().noquote() << "[tags] LLM-javaslat sikertelen:" << meetingId << error;
        publishTagSuggestions(meetingId, d->pendingTags.value(meetingId));
    });
    connect(d->settings, &SettingsManager::settingsChanged, this, [this]() { applyEmbeddingSettings(true); });
    applyEmbeddingSettings(false);

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
    // The voiceprint backfill: a queued task is skipped, the running one exits at the next voiceprint.
    d->backfillCancel->store(true);
    d->backfillPool.clear();
    d->backfillPool.waitForDone();
    // The track measurement / voice analysis: the queued ones are skipped, the running one exits at the next window.
    d->analysisShutdown->store(true);
    for (const auto& c : std::as_const(d->analysisRuns)) c->store(true);
    d->analysisPool.clear();
    d->analysisPool.waitForDone();
    // A running tag suggestion reads the profiles / the index: it must finish before them.
    d->tagPool.clear();
    d->tagPool.waitForDone();
    // Stopping and awaiting the background-thread identifications (the thread no longer hands over a result
    // referring to d: the QPointer is null by then).
    for (auto it = d->identifyRuns.begin(); it != d->identifyRuns.end(); ++it) {
        it->cancel->store(true);
        if (it->thread) {
            it->thread->disconnect(this);
            it->thread->wait();
            delete it->thread;
        }
    }
    d->identifyRuns.clear();
    d->services.reset();   // joins a running archive thread
}

SettingsManager* AppController::settings() const { return d->settings; }
recording::DeviceManager*   AppController::devices()  const { return d->recording->devices(); }
library::MeetingRepository*    AppController::store()     const { return d->store; }
library::TranscriptRepository* AppController::transcripts() const { return d->transcripts.get(); }
library::UiStateRepository*    AppController::uiState()    const { return d->uiState.get(); }
library::PeopleStore*     AppController::people() const { return d->people.get(); }
library::VoiceprintStore* AppController::voiceprints() const { return d->voiceprints.get(); }
library::PeopleService*   AppController::peopleService() const { return d->peopleService; }
library::PeopleStats*     AppController::peopleStats() const { return d->peopleStats; }
library::TagService*        AppController::tags() const { return d->tags; }
library::MeetingProfiles*   AppController::profiles() const { return d->profiles; }
library::EmbeddingIndex*    AppController::embeddings() const { return d->embeddings; }
library::EmbeddingPreparer* AppController::embeddingPreparer() const { return d->embeddingPreparer; }
QString          AppController::voiceModelPath() const { return d->voiceModelPath; }

QStringList AppController::activeVoiceModelIds() const { return d->voiceModelIds; }

processing::VoiceEmbedderSet AppController::voiceEmbedderSet() const { return d->voiceSet.freshCopy(); }

processing::UtteranceEmbedderFactory AppController::utteranceEmbedderFactory() const
{
    if (!d->voiceModelUsable()) return {};
    return voiceUtteranceEmbedderFactory(d->voiceSet, d->media);
}

void AppController::setVoiceEmbedderLoader(processing::PcmEmbedderLoader loader)
{
    d->voiceLoader = std::move(loader);
    d->voiceModelsApplied = false;   // the set is rebuilt together with the loader (without backfill)
    d->voiceModelIds.clear();
    applyVoiceModels();
}

void AppController::applyVoiceModels()
{
    const QStringList enabled = d->settings->enabledVoiceModels();
    processing::VoiceEmbedderSet set = processing::VoiceEmbedderSet::fromSettings(enabled, d->modelsDir, d->appDir, d->voiceLoader);
    const QStringList ids = set.modelIds();
    if (d->voiceModelsApplied && ids == d->voiceModelIds) return;   // another setting changed
    bool grew = false;
    for (const QString& id : ids)
        if (!d->voiceModelIds.contains(id)) grew = true;
    const bool first = !d->voiceModelsApplied;
    d->voiceModelsApplied = true;
    d->voiceSet = std::move(set);
    d->voiceModelIds = ids;
    for (const QString& id : enabled)
        if (!ids.contains(id))
            qCInfo(lcVoice).noquote() << "Hangmodell bekapcsolva, de nem használható (ismeretlen id vagy"
                                         " hiányzó fájl):" << id;
    qCInfo(lcVoice).noquote() << "Használt hangmodellek:" << (ids.isEmpty() ? QStringLiteral("-") : ids.join(QStringLiteral(", ")));
    // The open editors continue with the new model list and factory.
    for (const QPointer<editing::SpeakerEditor>& ed : std::as_const(d->speakerEditors)) {
        if (!ed) continue;
        ed->setEmbedderFactory(utteranceEmbedderFactory());
        ed->setVoiceModelIds(d->editorModelIds(d->settings));
    }
    emit voiceModelsChanged();
    if (grew && !first) backfillVoiceprints();
}

bool AppController::voiceprintBackfillRunning() const { return d->backfillRunning; }

void AppController::backfillVoiceprints()
{
    if (!d->voiceprints) return;
    if (d->backfillRunning) { d->backfillAgain = true; return; }
    if (!d->voiceModelUsable()) { emit voiceprintBackfillFinished(0, 0); return; }
    d->backfillSkipped.clear();   // a new run = a new report

    // The task list is made on the main thread (only the main thread reaches the DB); the background thread
    // only decodes and embeds. We load each meeting once.
    d->voiceprints->refresh();
    QVector<Impl::BackfillJob> jobs;
    QHash<QString, Meeting> meetings;
    for (const PersonId& person : d->voiceprints->people()) {
        for (const QString& model : std::as_const(d->voiceModelIds)) {
            // A sibling-exact list (printsMissingModel works at sampleRef level: it would mix up the same span of two
            // meetings' identically named mixes).
            for (const Voiceprint& vp : d->voiceprints->samplesMissingModel(person, model)) {
                auto it = meetings.constFind(vp.sourceMeetingId);
                if (it == meetings.constEnd())
                    it = meetings.insert(vp.sourceMeetingId,
                                         vp.sourceMeetingId.isEmpty() ? Meeting() : d->store->load(vp.sourceMeetingId));
                if (it->id.isEmpty()) {
                    d->noteBackfillSkip(person, model, vp, processing::VoiceprintBackfillSkip::Reason::MissingMeeting);
                    const QString key = vp.id + QLatin1Char('|') + model;
                    if (!d->backfillLogged.contains(key)) {
                        d->backfillLogged.insert(key);
                        qCInfo(lcVoice).noquote() << "Lenyomat-pótlás: a forrás-megbeszélés nincs meg, kihagyva:"
                                                  << person.toString() << vp.sampleRef << model;
                    }
                    continue;
                }
                jobs.append({person, model, vp, it.value()});
            }
        }
    }
    if (jobs.isEmpty()) { emit voiceprintBackfillFinished(0, int(d->backfillSkipped.size())); return; }

    d->backfillRunning = true;
    d->backfillAdded = 0;
    d->backfillDone = 0;                       // k / N progress
    d->backfillTotal = int(jobs.size());
    emit voiceprintBackfillStarted(d->backfillTotal);
    qCInfo(lcVoice).noquote() << "Lenyomat-pótlás indul:" << jobs.size() << "lenyomat";
    const processing::VoiceEmbedderSet proto = d->voiceSet.freshCopy();
    const auto cancel = d->backfillCancel;
    QPointer<AppController> self(this);
    const media::MediaToolPtr media = d->media;
    d->backfillPool.start([self, jobs, proto, cancel, media]() {
        const processing::VoiceEmbedderSet emb = proto.freshCopy();   // the thread's own model instances
        for (const Impl::BackfillJob& job : jobs) {
            if (cancel->load()) break;
            bool missing = false;
            const QVector<float> vec = embedPrintSource(*media, emb, job.model, job.meeting, job.print, &missing);
            QMetaObject::invokeMethod(qApp, [self, job, vec, missing]() {
                if (self) self->d->finishBackfillJob(job, vec, missing);
            }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(qApp, [self]() {
            if (self) self->d->finishBackfill();
        }, Qt::QueuedConnection);
    });
}

// One backfilled vector into the DB (main thread). The DB may have changed meanwhile (a deleted / moved
// sample, another process backfilled): it goes in only if the model is still missing from the group.
void AppController::Impl::finishBackfillJob(const BackfillJob& job, const QVector<float>& vec,
                                            bool missingAudio)
{
    const QString key = job.print.id + QLatin1Char('|') + job.model;
    ++backfillDone;
    emit q->voiceprintBackfillProgress(backfillDone, backfillTotal);
    if (vec.isEmpty()) {
        noteBackfillSkip(job.person, job.model, job.print,
                         missingAudio ? processing::VoiceprintBackfillSkip::Reason::MissingAudio
                                      : processing::VoiceprintBackfillSkip::Reason::Failed);
        if (!backfillLogged.contains(key)) {
            backfillLogged.insert(key);
            qCInfo(lcVoice).noquote() << (missingAudio ? "Lenyomat-pótlás: hiányzó hang, kihagyva:"
                                                       : "Lenyomat-pótlás: nem számolható, kihagyva:")
                                      << job.person.toString() << job.print.sampleRef << job.model;
        }
        return;
    }
    voiceprints->refresh();
    const QString sib = library::VoiceprintStore::siblingKey(job.print);
    bool stillMissing = false;
    for (const Voiceprint& p : voiceprints->samplesMissingModel(job.person, job.model))
        if (library::VoiceprintStore::siblingKey(p) == sib) { stillMissing = true; break; }
    if (!stillMissing) return;
    Voiceprint vp = job.print;
    vp.id.clear();   // addPrint generates one
    vp.model = job.model;
    vp.embedding = vec;
    vp.dim = vec.size();
    voiceprints->addPrint(job.person, vp);
    ++backfillAdded;
}

void AppController::Impl::finishBackfill()
{
    backfillRunning = false;
    const int added = backfillAdded;
    backfillAdded = 0;
    if (added > 0) {
        qCInfo(lcVoice).noquote() << "Lenyomat-pótlás kész:" << added << "új lenyomat";
        emit q->voiceprintsChanged();
    }
    emit q->voiceprintBackfillFinished(added, int(backfillSkipped.size()));
    if (backfillAgain) {
        backfillAgain = false;
        q->backfillVoiceprints();
    }
}
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

QStringList AppController::knownPeople() const {
    QStringList out;
    if (!d->people) return out;
    d->people->refresh();
    for (const Person& p : d->people->people()) out << p.displayName;
    return out;
}

SelfPerson AppController::selfPerson() const { return d->selfPerson(); }

void AppController::Impl::ensureSelf()
{
    if (!people || people->legacy() || !settings) return;
    people->refresh();
    if (!people->selfId().isNull() && people->contains(people->selfId())) return;
    const QString name = settings->settings().recording.userSpeakerName.trimmed();
    if (name.isEmpty()) return;
    const PersonId id = people->ensure(name);
    if (!id.isNull()) people->setSelfId(id);
}

bool AppController::Impl::meetingRefersTo(const Meeting& m, const PersonId& person)
{
    if (person.isNull()) return false;
    for (const PersonId& p : m.speakerMap) if (p == person) return true;
    for (const Participant& p : m.participants) if (p.personId == person) return true;
    if (m.approval)
        for (const ParticipantApprovalEntry& c : m.approval->candidates) if (c.personId == person) return true;
    if (m.folder.isEmpty() || !QFile::exists(editing::speakeredit::overlayPath(m.folder))) return false;
    const SpeakerOverlay ov = editing::speakeredit::loadOverlay(m.folder);
    for (const OverlayParticipant& p : ov.participants) if (p.personId == person) return true;
    for (const OverlayIdentification& i : ov.identified) if (i.personId == person) return true;
    return false;
}

void AppController::Impl::refreshMeetingNames(const Meeting& m)
{
    MergedTranscript merged = TranscriptRepository::readTokens(m.folder);
    if (!merged.tokens.isEmpty()) {
        editing::speakeredit::applyResolvedSpeakers(merged, m, personNames());
        TranscriptRepository::writeMarkdown(m.folder, merged.renderMarkdown());
    }
    emit q->speakerMapChanged(m.id);
}

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
    if (person.isNull()) return;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    // No model: silently skipped (manual labeling works anyway).
    if (!d->voiceModelUsable() || !d->voiceSet.ensureLoaded()) return;

    const MergedTranscript merged =
        TranscriptRepository::readTokens(m.folder);
    if (merged.tokens.isEmpty()) return;
    const VoiceSource src = resolveVoiceSource(m, merged, rawLabel);
    if (src.absPath.isEmpty()) return;

    QStringList refs;
    const EmbeddingSet set = embeddingForLabel(*d->media, d->voiceSet, src.absPath, src.offsetMs, merged, rawLabel,
                                               src.fileRel, &refs);
    if (set.isEmpty()) return;

    Voiceprint tmpl;
    tmpl.sourceMeetingId = m.id;
    tmpl.sourceTrack = src.trackId;
    tmpl.device = src.device;
    tmpl.sampleRef = representativeSampleRef(merged, rawLabel, src.fileRel);
    tmpl.sourceRefs = refs;
    tmpl.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    for (const Voiceprint& vp : printsFromSet(set, tmpl)) d->voiceprints->addPrint(person, vp);
    emit voiceprintsChanged();
}

void AppController::autoIdentifyMeeting(const QString& meetingId,
                                       const std::function<bool(int,int)>& onProgress) {
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    if (!d->voiceModelUsable() || !d->voiceSet.ensureLoaded()) return;

    const MergedTranscript merged =
        TranscriptRepository::readTokens(m.folder);
    if (merged.tokens.isEmpty()) return;

    // Distinct raw speaker labels.
    QStringList labels;
    for (const TranscriptToken& t : merged.tokens)
        if (!t.speaker.isEmpty() && !labels.contains(t.speaker))
            labels << t.speaker;

    // We match EVERY unnamed speaker (mic + loopback) against the voiceprint DB. There is no
    // loudness/position-based assumption and no auto-enroll: the NAME goes onto the speaker by fingerprint
    // (or the user's one-time manual rename).
    bool mapChanged = false;
    QStringList identified;   // raw labels that got a name now
    int progressDone = 0;
    for (const QString& label : labels) {
        if (onProgress && !onProgress(++progressDone, labels.size()))
            break;   // the user cancelled: the matches already found are saved
        if (m.speakerMap.contains(label))
            continue;   // already named (by hand or an earlier match)
        const VoiceSource src = resolveVoiceSource(m, merged, label);
        if (src.absPath.isEmpty()) continue;
        const EmbeddingSet e = embeddingForLabel(*d->media, d->voiceSet, src.absPath, src.offsetMs, merged, label);
        if (e.isEmpty()) continue;
        if (d->matchSpeaker(m, label, e)) {
            identified << label;
            mapChanged = true;
        }
    }

    if (mapChanged)
        d->commitIdentification(m, merged, identified);
}

VoiceMatch AppController::testSpeakerMatch(const QString& meetingId, const QString& rawLabel) {
    VoiceMatch none;   // { "", -1 }
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return none;
    if (!d->voiceModelUsable() || !d->voiceSet.ensureLoaded()) return none;
    const MergedTranscript merged =
        TranscriptRepository::readTokens(m.folder);
    if (merged.tokens.isEmpty()) return none;
    const VoiceSource src = resolveVoiceSource(m, merged, rawLabel);
    if (src.absPath.isEmpty()) return none;
    const EmbeddingSet e = embeddingForLabel(*d->media, d->voiceSet, src.absPath, src.offsetMs, merged, rawLabel);
    if (e.isEmpty()) return none;
    return d->voiceprints->bestMatch(e, e.keys());
}

QVector<ParticipantGuess> AppController::identifyParticipants(
    const QString& meetingId, const std::function<bool(int,int)>& onProgress) {
    QVector<ParticipantGuess> out;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return out;
    if (!d->voiceModelUsable() || !d->voiceSet.ensureLoaded()) return out;
    // Clustering runs on the fused vector (with every loadable model); a window counts only if every model gave a vector for it.
    const QStringList modelIds = d->voiceSet.loadedModelIds();

    constexpr qint64 kWinMs = 3000;          // window length
    constexpr int    kTargetWindows = 50;    // number of windows per track (upper limit)
    constexpr double kMergeThreshold = 0.55; // cosine threshold for cluster merging
    constexpr float  kSilenceRms = 0.004f;   // a window quieter than this = silence (skipped)

    // Progress: all windows to process (active tracks x windows per track), for the callback.
    int progressTotal = 0, progressDone = 0;
    if (m.durationMs >= kWinMs) {
        const qint64 step0 = std::max<qint64>(kWinMs, m.durationMs / kTargetWindows);
        int perTrack = 0;
        for (qint64 s = 0; s + kWinMs <= m.durationMs; s += step0) ++perTrack;
        int activeTracks = 0;
        for (const Track& t : m.tracks) if (t.active) ++activeTracks;
        progressTotal = perTrack * activeTracks;
    }

    for (const Track& t : m.tracks) {
        if (!t.active) continue;
        const qint64 D = m.durationMs;
        if (D < kWinMs) continue;
        const QString path = QDir(m.folder).filePath(t.file);
        const qint64 step = std::max<qint64>(kWinMs, D / kTargetWindows);

        QVector<QVector<float>> embs;      // fused vectors
        QVector<EmbeddingSet> sets;        // the same per model (for voiceprint matching)
        QVector<qint64> winStart;
        for (qint64 s = 0; s + kWinMs <= D; s += step) {
            if (onProgress && !onProgress(++progressDone, progressTotal))
                return out;   // the user cancelled
            // The window is in meeting time; a window outside the file of a track that starts later / ends earlier
            // is empty (skipped).
            const tanara::tracktiming::FileRange fr = tanara::tracktiming::fileRange(t, s, s + kWinMs);
            if (!fr.valid()) continue;
            QVector<float> pcm = processing::VoiceEmbedder::decodePcm16kMono(*d->media, path, fr.startMs, fr.endMs);
            if (pcm.isEmpty()) continue;
            double sum = 0.0;
            for (float x : pcm) sum += static_cast<double>(x) * x;
            const double rms = std::sqrt(sum / static_cast<double>(pcm.size()));
            if (rms < kSilenceRms) continue;          // silence -> skipped
            const EmbeddingSet set = d->voiceSet.embedPcmWith(pcm, modelIds);
            const QVector<float> e = processing::fusion::fuse(set, modelIds);
            if (!e.isEmpty()) { embs.append(e); sets.append(set); winStart.append(s); }
        }
        if (embs.isEmpty()) continue;

        const QVector<int> labels = clusterEmbeddings(embs, kMergeThreshold);
        QMap<int, QVector<int>> groups;
        for (int i = 0; i < labels.size(); ++i) groups[labels[i]].append(i);

        for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
            const QVector<int>& idxs = it.value();
            // Noise filter: a 1-window cluster is dropped if there is another (overlap/click).
            if (idxs.size() < 2 && groups.size() > 1) continue;

            QVector<float> cent(embs[idxs[0]].size(), 0.0f);
            for (int i : idxs)
                for (int k = 0; k < cent.size(); ++k) cent[k] += embs[i][k];
            for (int k = 0; k < cent.size(); ++k) cent[k] /= idxs.size();
            cent = library::VoiceprintStore::l2normalize(cent);

            // Per-model centroid for matching (voiceprints are stored per model).
            EmbeddingSet centSet;
            for (const QString& id : modelIds) {
                QVector<float> c;
                for (int i : idxs) {
                    const QVector<float>& v = sets[i].value(id);
                    if (c.isEmpty()) c.fill(0.0f, v.size());
                    for (int k = 0; k < c.size() && k < v.size(); ++k) c[k] += v[k];
                }
                centSet.insert(id, library::VoiceprintStore::l2normalize(c));
            }
            const VoiceMatch mt = d->voiceprints->bestMatch(centSet, modelIds);
            // Representative window = the nearest to the centroid (medoid).
            qint64 repStart = winStart[idxs[0]]; double bestSim = -2.0;
            for (int i : idxs) {
                const double s = library::VoiceprintStore::cosineSimilarity(cent, embs[i]);
                if (s > bestSim) { bestSim = s; repStart = winStart[i]; }
            }

            ParticipantGuess g;
            g.trackId = t.id;
            g.deviceName = t.deviceName;
            g.personId = (mt.score >= kVoiceMatchThreshold) ? mt.personId : PersonId();
            g.displayName = d->people ? d->people->displayName(g.personId) : QString();
            g.score = mt.score;
            g.windows = idxs.size();
            g.sampleRef = QStringLiteral("%1#%2-%3").arg(t.file).arg(repStart).arg(repStart + kWinMs);
            out.append(g);
        }
    }
    // The most windows first (dominant speakers first).
    std::sort(out.begin(), out.end(),
              [](const ParticipantGuess& a, const ParticipantGuess& b) { return a.windows > b.windows; });
    return out;
}

PersonId AppController::enrollVoiceprintFromSample(const PersonChoice& choice, const QString& meetingId,
                                                  const QString& trackId, qint64 startMs, qint64 endMs) {
    if (choice.isAnonymous()) return {};
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return {};
    if (!d->voiceModelUsable() || !d->voiceSet.ensureLoaded()) return {};
    const Track* track = nullptr;
    for (const Track& t : m.tracks) if (t.id == trackId) { track = &t; break; }
    if (!track) return {};
    const QString path = QDir(m.folder).filePath(track->file);
    // startMs/endMs in meeting time (like the sampleRef) -> the track file's time.
    const tanara::tracktiming::FileRange fr = tanara::tracktiming::fileRange(*track, startMs, endMs);
    if (!fr.valid()) return {};
    const EmbeddingSet set =
        d->voiceSet.embedPcm(processing::VoiceEmbedder::decodePcm16kMono(*d->media, path, fr.startMs, fr.endMs));
    if (set.isEmpty()) return {};
    const PersonId person = d->people ? d->people->resolve(choice) : choice.id;
    if (person.isNull()) return {};

    Voiceprint tmpl;
    tmpl.sourceMeetingId = m.id;
    tmpl.sourceTrack = track->id;
    tmpl.device = track->deviceName;
    tmpl.sampleRef = QStringLiteral("%1#%2-%3").arg(track->file).arg(startMs).arg(endMs);
    tmpl.sourceRefs = QStringList{tmpl.sampleRef};
    tmpl.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    for (const Voiceprint& vp : printsFromSet(set, tmpl)) d->voiceprints->addPrint(person, vp);
    emit voiceprintsChanged();
    emit peopleChanged();
    return person;
}

// A rename writes ONE field (people.json). transcript.md is a derived file with the names in it: the meetings
// showing the person regenerate it, and their speakerMapChanged makes open views reread.
void AppController::renamePerson(const PersonId& person, const QString& newName) {
    const QString n = newName.trimmed();
    if (person.isNull() || n.isEmpty() || !d->people) return;
    if (!d->people->rename(person, n)) return;
    if (d->settings && d->people->selfId() == person) {
        AppSettings s = d->settings->settings();
        if (s.recording.userSpeakerName.trimmed() != n) {
            s.recording.userSpeakerName = n;
            d->settings->setSettings(s);
        }
    }
    for (const Meeting& idx : d->store->loadAll()) {
        const Meeting m = d->store->load(idx.id);
        if (Impl::meetingRefersTo(m, person)) d->refreshMeetingNames(m);
    }
    emit peopleChanged();
}

void AppController::removePerson(const PersonId& person) {
    if (person.isNull()) return;
    if (d->people) d->people->remove(person);
    if (d->tags) d->tags->forgetPerson(person);
    if (d->voiceprints) { d->voiceprints->removePerson(person); emit voiceprintsChanged(); }

    const QVector<Meeting> all = d->store->loadAll();
    for (const Meeting& idx : all) {
        Meeting m = d->store->load(idx.id);
        bool changed = false;
        const QList<QString> keys = m.speakerMap.keys();
        for (const QString& key : keys)
            if (m.speakerMap.value(key) == person) { m.speakerMap.remove(key); changed = true; }
        for (Participant& p : m.participants)
            if (p.personId == person) { p.personId = PersonId(); changed = true; }
        if (m.approval)
            for (ParticipantApprovalEntry& c : m.approval->candidates)
                if (c.personId == person) { c.personId = PersonId(); changed = true; }
        const bool overlay = editing::speakeredit::removePersonFromOverlay(m.folder, person);
        if (!changed && !overlay) continue;
        if (changed) d->store->saveMeeting(m);
        d->refreshMeetingNames(m);
    }
    emit peopleChanged();
}

library::PersonMergeUndo AppController::mergePeople(const PersonId& keep, const PersonId& drop) {
    library::PersonMergeUndo u;
    if (!d->people || keep.isNull() || drop.isNull() || keep == drop) return u;
    d->people->refresh();
    if (!d->people->contains(keep) || !d->people->contains(drop)) return u;
    u.keep = keep;
    u.drop = drop;
    u.keepBefore = d->people->person(keep);
    u.dropBefore = d->people->person(drop);
    u.selfWasDrop = d->people->selfId() == drop;
    u.selfNameBefore = d->settings ? d->settings->settings().recording.userSpeakerName : QString();

    // 1) The records: drop folds into keep (its name becomes a nickname; "self" moves to keep).
    d->people->merge(keep, drop, /*dropNameAsAlias*/ true);
    if (u.selfWasDrop && d->settings) {
        AppSettings s = d->settings->settings();
        s.recording.userSpeakerName = u.keepBefore.displayName;
        d->settings->setSettings(s);
    }
    // 2) Voiceprints and tag rejections.
    if (d->voiceprints) u.movedPrintIds = d->voiceprints->merge(drop, keep);
    if (d->tags) u.rejections = d->tags->movePersonRefs(drop, keep);
    // 3) Every meeting's references.
    for (const Meeting& idx : d->store->loadAll()) {
        Meeting m = d->store->load(idx.id);
        library::PersonMergeMeeting mm;
        mm.meetingId = m.id;
        for (auto it = m.speakerMap.begin(); it != m.speakerMap.end(); ++it)
            if (it.value() == drop) { it.value() = keep; mm.speakerMapKeys << it.key(); }
        for (Participant& p : m.participants)
            if (p.personId == drop) { p.personId = keep; mm.participantIds << p.id; }
        if (m.approval)
            for (ParticipantApprovalEntry& c : m.approval->candidates)
                if (c.personId == drop) { c.personId = keep; mm.approvalParticipantIds << c.participantId; }
        mm.overlay = editing::speakeredit::replacePersonInOverlay(m.folder, drop, keep);
        if (mm.isEmpty()) continue;
        if (!mm.speakerMapKeys.isEmpty() || !mm.participantIds.isEmpty() || !mm.approvalParticipantIds.isEmpty())
            d->store->saveMeeting(m);
        u.meetings.append(mm);
        d->refreshMeetingNames(m);
        emit participantsChanged(m.id);
    }
    emit voiceprintsChanged();
    emit peopleChanged();
    return u;
}

bool AppController::undoMergePeople(const library::PersonMergeUndo& u) {
    if (!u.isValid() || !d->people) return false;
    // 1) The records as they were (the dropped person comes back with its id).
    d->people->restore(u.dropBefore);
    d->people->restore(u.keepBefore);
    if (u.selfWasDrop) {
        d->people->setSelfId(u.drop);
        if (d->settings) {
            AppSettings s = d->settings->settings();
            s.recording.userSpeakerName = u.selfNameBefore;
            d->settings->setSettings(s);
        }
    }
    // 2) Voiceprints and tag rejections.
    if (d->voiceprints) {
        d->voiceprints->refresh();
        for (const QString& printId : u.movedPrintIds) {
            PersonId owner;
            Voiceprint vp;
            if (!d->voiceprints->findPrint(printId, &owner, &vp) || owner != u.keep) continue;
            d->voiceprints->removePrint(printId);
            d->voiceprints->addPrint(u.drop, vp);
        }
    }
    if (d->tags) d->tags->undoMovePersonRefs(u.keep, u.drop, u.rejections);
    // 3) The recorded meeting references that still point to keep.
    for (const library::PersonMergeMeeting& mm : u.meetings) {
        Meeting m = d->store->load(mm.meetingId);
        if (m.id.isEmpty()) continue;
        bool changed = false;
        for (const QString& key : mm.speakerMapKeys)
            if (m.speakerMap.value(key) == u.keep) { m.speakerMap.insert(key, u.drop); changed = true; }
        for (Participant& p : m.participants)
            if (mm.participantIds.contains(p.id) && p.personId == u.keep) { p.personId = u.drop; changed = true; }
        if (m.approval)
            for (ParticipantApprovalEntry& c : m.approval->candidates)
                if (mm.approvalParticipantIds.contains(c.participantId) && c.personId == u.keep) {
                    c.personId = u.drop;
                    changed = true;
                }
        if (changed) d->store->saveMeeting(m);
        editing::speakeredit::restorePersonInOverlay(m.folder, mm.overlay, u.keep, u.drop);
        d->refreshMeetingNames(m);
        emit participantsChanged(m.id);
    }
    emit voiceprintsChanged();
    emit peopleChanged();
    return true;
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
    ParticipantUndoScope undoScope(this, meetingId, tr("Leírás módosítva"));
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
    if (const auto it = d->analysisRuns.constFind(meetingId); it != d->analysisRuns.constEnd())
        it.value()->store(true);   // the voice analysis's result no longer goes to the deleted meeting
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

void AppController::setUserSpeakerName(const QString& name) {
    const QString n = name.trimmed();
    if (n.isEmpty()) return;
    AppSettings s = d->settings->settings();
    if (s.recording.userSpeakerName.trimmed() != n) {
        s.recording.userSpeakerName = n;
        d->settings->setSettings(s);
    }
    // The own person's record: ONE field (renamePerson refreshes the meetings showing it).
    d->ensureSelf();
    const PersonId self = d->people ? d->people->selfId() : PersonId();
    if (!self.isNull() && d->people->displayName(self) != n) renamePerson(self, n);
    else emit peopleChanged();
}

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

static QString resolvedPrompt(const AppSettings& s, const QString& userOverride, const char* id);
static processing::SummaryRequest summaryRequestFor(const AppSettings& s, const MergedTranscript& merged,
                                        const Meeting& m, const QString& model, int maxTokens);

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
                in.topicPromptChars = int(resolvedPrompt(s, s.processing.topicExtractionPrompt, "topic").size());
                in.analysisPromptChars = int(resolvedPrompt(s, s.processing.topicAnalysisPrompt, "analysis").size());
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
                const processing::SummaryRequest req = summaryRequestFor(s, merged, m, in.llmModel, kCloudMaxTokens);
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

// The system prompt actually to be sent: settings override -> file override
// (<data>/prompts/<id>.md) -> built-in default (PromptLibrary), finally with the target language
// applied ({{NYELV}} placeholder / directive).
static QString resolvedPrompt(const AppSettings& s, const QString& userOverride, const char* id)
{
    const QString base = userOverride.trimmed().isEmpty()
        ? processing::promptDefault(QLatin1String(id))   // the current data folder
        : userOverride;
    return processing::applySummaryLanguage(base, s.processing.summaryLanguage);
}

void AppController::summarizeMeeting(const QString& meetingId)
{
    if (d->jobs->isRunning(meetingId, JobKind::Summarize))
        return;   // already running

    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }

    providers::ReadinessResult res = canRun(providers::WorkflowStep::Summarize, meetingId);
    if (!res.runnable) { emit errorOccurred(res.detail); return; }

    MergedTranscript merged = d->mergedCache.value(meetingId);
    if (merged.tokens.isEmpty())
        merged = TranscriptRepository::readTokens(m.folder);
    // A content guard: canRun(Summarize) gates on the meeting.hasTranscript flag, but if tokens.json is
    // missing/empty (deleted by hand, a partial write, a wrongly copied meeting), no summary should start on
    // an empty transcript; this mirrors the original content-based guard.
    if (merged.tokens.isEmpty()) {
        emit errorOccurred(tr("Nincs átirat — előbb futtass átírást."));
        return;
    }
    editing::speakeredit::applyResolvedSpeakers(merged, m, d->personNames());   // Gemma should see the real names

    const AppSettings s = d->settings->settings();
    const QString llmId = s.providers.llm.selectedId;
    CloudRunPtr run;   // cloud: ONE run (one job id) for all the note and merge calls
    providers::ProviderConfig cfg = llmConfigFor(run, meetingId, QStringLiteral("summary"));
    const auto sink = std::make_shared<FailureSink>();
    providers::ProviderContext pctx = providerContext(run, QStringLiteral("quick"));
    captureFailures(pctx, sink);
    providers::IChatProvider* provider = d->providerRegistry.create<providers::IChatProvider>(llmId, pctx, cfg, this);
    if (!provider) {
        emit errorOccurred(tr("Ismeretlen LLM-provider: %1.").arg(llmId));
        return;
    }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    auto* svc = new processing::SummaryService(provider, this);
    emit jobProgress(meetingId, run ? tr("Összefoglalás a Tanara Cloudban…")
                                    : tr("Összefoglalás a helyi modellel…"));

    processing::SummaryRequest req = summaryRequestFor(s, merged, m, cfg.model, cfg.maxTokens);
    req.temperature = cfg.temperature;
    // For the source references: the utterances with their current (resolved) speaker; this becomes the
    // summary's speaker snapshot (the targeted staleness compares against it).
    req.sourceLines = editing::speakeredit::resolvedSourceLines(m, d->personNames());
    req.adaptToContext = !run;   // with Tanara Cloud the context is the gateway's business
    const processing::SummaryPlan plan = processing::SummaryService::plan(req);
    const auto ctx = std::make_shared<Impl::LlmContextState>();
    ctx->need = processing::SummaryService::contextNeed(req);

    // Structured task: for a short meeting one stage (one call), for a longer one "Notes per part" (k / n
    // parts) + "Summarizing". The estimate comes only from earlier runs measured with the same model (unit:
    // the transcript's length).
    const QString key = Impl::llmKey(meetingId, JobKind::Summarize);
    const QString statsKey = QStringLiteral("summary-notes/%1/%2").arg(llmId, cfg.model);
    const double units = double(merged.renderMarkdown().size());
    QVector<JobStage> stages;
    if (plan.singleCall) {
        stages.append({ QStringLiteral("single"), tr("Összefoglalás"), StageState::Waiting, -1, QString() });
    } else {
        // Parts finished in an earlier run are visible at startup (during the model load too).
        QString notesDetail = tr("%n rész", nullptr, plan.parts);
        if (plan.cachedParts > 0) notesDetail += tr(" · %n korábbi futásból", nullptr, plan.cachedParts);
        stages.append({ QStringLiteral("notes"), tr("Jegyzetelés részenként"), StageState::Waiting, -1,
                        notesDetail });
        stages.append({ QStringLiteral("merge"), tr("Összegzés"), StageState::Waiting, -1, QString() });
    }
    d->jobs->begin(meetingId, JobKind::Summarize, tr("Összefoglaló készítése"), stages);
    if (!plan.singleCall)
        d->jobs->setCounts(meetingId, JobKind::Summarize, plan.cachedParts, plan.parts);
    d->jobs->setEstimate(meetingId, JobKind::Summarize, d->jobStats->estimateSec(statsKey, units));
    d->llmRuns.insert(key, Impl::LlmRun{svc, providerObj, run});
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();

    // Context-adapted re-split: the stages switch to per-part notes.
    connect(svc, &processing::SummaryService::partsChanged, this, [this, meetingId](int parts, int cached) {
        if (!d->jobs->isRunning(meetingId, JobKind::Summarize)) return;
        QVector<JobStage> stages;
        if (const JobStage* model = d->jobs->job(meetingId, JobKind::Summarize).stage(QStringLiteral("model")))
            stages.append(*model);
        stages.append({ QStringLiteral("notes"), tr("Jegyzetelés részenként"), StageState::Waiting, -1,
                        tr("%n rész (a modell kontextusához igazítva)", nullptr, parts) });
        stages.append({ QStringLiteral("merge"), tr("Összegzés"), StageState::Waiting, -1, QString() });
        d->jobs->setStages(meetingId, JobKind::Summarize, stages);
        d->jobs->setCounts(meetingId, JobKind::Summarize, cached, parts);
    });
    connect(svc, &processing::SummaryService::contextLimitDetected, this, [this, cfg, ctx](int tokens) {
        if (!ctx->lmStudio) d->learnedContext.insert(Impl::serverKey(cfg), tokens);
    });
    connect(svc, &processing::SummaryService::progress, this,
            [this, meetingId, cached = plan.cachedParts](const QString& stage, int done, int total) {
        if (!d->jobs->isRunning(meetingId, JobKind::Summarize)) return;
        if (stage == QLatin1String("single")) {
            d->jobs->setStage(meetingId, JobKind::Summarize, stage, StageState::Running);
            return;
        }
        if (stage == QLatin1String("notes")) {
            QString detail = done < total ? tr("%1/%2. rész").arg(done + 1).arg(total)
                                          : tr("%n rész kész", nullptr, total);
            if (cached > 0) detail += tr(" · %n korábbi futásból", nullptr, cached);
            d->jobs->setStage(meetingId, JobKind::Summarize, stage,
                              done < total ? StageState::Running : StageState::Done, -1, detail);
            d->jobs->setCounts(meetingId, JobKind::Summarize, done, total);
            if (done < total)
                emit jobProgress(meetingId, tr("Jegyzetelés: %1/%2. rész…").arg(done + 1).arg(total));
            return;
        }
        if (stage == QLatin1String("merge")) {
            d->jobs->setStage(meetingId, JobKind::Summarize, QStringLiteral("merge"), StageState::Running);
            emit jobProgress(meetingId, tr("Összegzés a részjegyzetekből…"));
        }
    });
    connect(svc, &processing::SummaryService::summaryReady, this,
            [this, meetingId, providerObj, svc, run, key, statsKey, units, clock, llmId,
             model = cfg.model, note = req.contextNotes](const Summary& sum) {
        d->llmRuns.remove(key);
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
        // A FRESH meeting (it may have changed / been deleted during the summarization).
        Meeting mm = d->store->load(meetingId);
        if (mm.id.isEmpty()) {
            d->jobs->cancelled(meetingId, JobKind::Summarize);
            finishCloudRun(run);
            return;
        }
        const QString md = sum.renderMarkdown();
        SummaryRepository::writeMarkdown(mm.folder, md);
        // The structured form + the provenance (summary.json); the summary view draws from this.
        library::SummaryDocument doc;
        doc.exists = true;
        doc.summary = sum;
        doc.markdown = md;
        doc.meta.createdAt = QDateTime::currentDateTime();
        doc.meta.providerId = llmId;
        doc.meta.model = model;
        doc.meta.mode = library::SummaryMode::Quick;
        doc.meta.contextNote = note;            // the note it was made with
        doc.meta.contextNoteKnown = true;
        library::SummaryRepository::save(mm.folder, doc);
        // a copy into the notes (vault) folder
        const QString noteName = QStringLiteral("%1 %2.md")
            .arg(mm.startedAt.toString(QStringLiteral("yyyy-MM-dd")), slugify(mm.title));
        SummaryRepository::writeNoteCopy(d->notesDir, noteName, md);
        mm.hasSummary = true;
        d->store->saveMeeting(mm);
        d->jobStats->addSample(statsKey, double(clock->elapsed()) / 1000.0, units);
        d->jobs->finish(meetingId, JobKind::Summarize);
        emit summaryReady(mm.id, SummaryRepository::markdownPath(mm.folder));
        finishCloudRun(run);
    });
    connect(svc, &processing::SummaryService::summaryFailed, this,
            [this, meetingId, providerObj, svc, run, key, sink, ctx](const QString& e) {
        d->llmRuns.remove(key);
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
        const JobError je = d->describeLlmFailure(JobKind::Summarize, e, run, sink, ctx);
        d->jobs->fail(meetingId, JobKind::Summarize, je);
        failCloudRun(run, tr("Összefoglaló hiba: %1").arg(je.message));
    });

    // Preparing the model (LM Studio: loading with the needed context), then it starts.
    providers::ILocalModelServer* prep = d->prepareLlm(meetingId, JobKind::Summarize, cfg, bool(run), ctx,
        [this, svc = QPointer<processing::SummaryService>(svc), req, ctx, cfg, clock]() mutable {
            if (!svc) return;
            clock->restart();   // the model load does not count in the run's speed (estimate)
            req.contextLimit = ctx->loadedContext > 0 ? ctx->loadedContext
                                                      : d->learnedContext.value(Impl::serverKey(cfg));
            svc->summarize(req);
        },
        [this, meetingId, providerObj = QPointer<QObject>(providerObj),
         svc = QPointer<processing::SummaryService>(svc), key](const JobError& je) {
            d->llmRuns.remove(key);
            if (providerObj) providerObj->deleteLater();
            if (svc) svc->deleteLater();
            d->jobs->fail(meetingId, JobKind::Summarize, je);
            emit errorOccurred(tr("Összefoglaló hiba: %1").arg(je.message));
        });
    if (prep && d->llmRuns.contains(key)) d->llmRuns[key].preparer = prep;
}

// The quick summary request from the settings: the three prompts (override -> file -> built-in, with the
// target language), the context and the part-note cache path. model / maxTokens come from the provider
// config actually used (cloud: a moderate max_tokens because of the coverage rule).
static processing::SummaryRequest summaryRequestFor(const AppSettings& s, const MergedTranscript& merged,
                                        const Meeting& m, const QString& model, int maxTokens)
{
    constexpr int kNotesMaxTokens = 3000;   // one part note fits well within it
    processing::SummaryRequest req;
    req.transcript = merged;
    req.contextNotes = m.contextNote.trimmed();
    req.singlePrompt = resolvedPrompt(s, s.processing.summaryPrompt, "single");
    req.notesPrompt = resolvedPrompt(s, s.processing.notesPrompt, "notes");
    req.mergePrompt = resolvedPrompt(s, s.processing.mergePrompt, "merge");
    req.language = s.processing.summaryLanguage;
    req.model = model;
    req.maxTokens = maxTokens > 0 ? maxTokens : 8000;
    req.notesMaxTokens = qMin(req.maxTokens, kNotesMaxTokens);
    if (!m.folder.isEmpty())
        req.cachePath = QDir(m.folder).filePath(processing::SummaryService::cacheFileName());
    return req;
}

// ---- complex (multi-pass) summary ------------------------------------------

void AppController::extractMeetingTopics(const QString& meetingId)
{
    if (d->jobs->isRunning(meetingId, JobKind::ExtractTopics))
        return;   // already running

    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }

    // If a (possibly edited) topic list already exists, we return that; no re-extraction.
    const QVector<SummaryTopic> existing = SummaryRepository::readTopics(m.folder);
    if (!existing.isEmpty()) { emit topicsReady(m.id, existing); return; }

    providers::ReadinessResult res = canRun(providers::WorkflowStep::Summarize, meetingId);
    if (!res.runnable) { emit errorOccurred(res.detail); return; }

    MergedTranscript merged = d->mergedCache.value(meetingId);
    if (merged.tokens.isEmpty())
        merged = TranscriptRepository::readTokens(m.folder);
    if (merged.tokens.isEmpty()) { emit errorOccurred(tr("Nincs átirat — előbb futtass átírást.")); return; }
    editing::speakeredit::applyResolvedSpeakers(merged, m, d->personNames());
    const QString transcriptMd = merged.renderMarkdown();

    const AppSettings s = d->settings->settings();
    CloudRunPtr run;
    providers::ProviderConfig cfg = llmConfigFor(run, meetingId, QStringLiteral("topics"));
    const auto sink = std::make_shared<FailureSink>();
    providers::ProviderContext pctx = providerContext(run, QStringLiteral("complex"));
    captureFailures(pctx, sink);
    providers::IChatProvider* provider =
        d->providerRegistry.create<providers::IChatProvider>(s.providers.llm.selectedId, pctx, cfg, this);
    if (!provider) { emit errorOccurred(tr("Ismeretlen LLM-provider: %1.").arg(s.providers.llm.selectedId)); return; }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    auto* svc = new processing::ComplexSummaryService(provider, this);
    svc->setLanguage(s.processing.summaryLanguage);
    emit jobProgress(meetingId, run ? tr("Témák kigyűjtése a Tanara Cloudban…")
                                    : tr("Témák kigyűjtése a helyi modellel…"));

    const QString key = Impl::llmKey(meetingId, JobKind::ExtractTopics);
    const QString statsKey = QStringLiteral("topics/%1/%2").arg(s.providers.llm.selectedId, cfg.model);
    const double units = double(transcriptMd.size());
    d->jobs->begin(meetingId, JobKind::ExtractTopics, tr("Témák javaslása"));
    d->jobs->setEstimate(meetingId, JobKind::ExtractTopics, d->jobStats->estimateSec(statsKey, units));
    d->llmRuns.insert(key, Impl::LlmRun{svc, providerObj, run});
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();

    connect(svc, &processing::ComplexSummaryService::topicsReady, this,
            [this, id = m.id, folder = m.folder, providerObj, svc, run, key, statsKey, units, clock]
            (const QVector<SummaryTopic>& topics) {
        d->llmRuns.remove(key);
        if (QDir(folder).exists())   // (the meeting may have been deleted meanwhile)
            SummaryRepository::writeTopics(folder, topics);
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
        // Cloud: topic gathering is the FIRST part of the complex run; the run (and the X-Tanara-Job-Id) stays
        // open, the topic analyses and the merge use the same one; at the end of the run the total is shown
        // (one run = one id). Here only the balance chip refreshes.
        if (run) {
            d->complexRuns.insert(id, run);
            if (run->balance.isValid() || run->calls > 0)
                d->cloud->refreshAccount();
        }
        d->jobStats->addSample(statsKey, double(clock->elapsed()) / 1000.0, units);
        d->jobs->finish(id, JobKind::ExtractTopics);
        emit topicsChanged(id);
        emit topicsReady(id, topics);
    });
    const QString topicPrompt = resolvedPrompt(s, s.processing.topicExtractionPrompt, "topic");
    const auto ctx = std::make_shared<Impl::LlmContextState>();
    ctx->need = providers::llmctx::callContextNeed(topicPrompt.size() + transcriptMd.size()
                                        + m.contextNote.size() + 300, cfg.maxTokens);
    connect(svc, &processing::ComplexSummaryService::failed, this,
            [this, id = m.id, providerObj, svc, run, key, sink, ctx](const QString& e) {
        d->llmRuns.remove(key);
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
        const JobError je = d->describeLlmFailure(JobKind::ExtractTopics, e, run, sink, ctx);
        d->jobs->fail(id, JobKind::ExtractTopics, je);
        failCloudRun(run, tr("Téma-kinyerés hiba: %1").arg(je.message));
    });

    providers::ILocalModelServer* prep = d->prepareLlm(m.id, JobKind::ExtractTopics, cfg, bool(run), ctx,
        [svc = QPointer<processing::ComplexSummaryService>(svc), transcriptMd, note = m.contextNote.trimmed(),
         topicPrompt, cfg, clock]() {
            clock->restart();   // the model load does not count in the run's speed (estimate)
            if (svc) svc->requestTopics(transcriptMd, note, topicPrompt, cfg.model, cfg.temperature, cfg.maxTokens);
        },
        [this, id = m.id, providerObj = QPointer<QObject>(providerObj),
         svc = QPointer<processing::ComplexSummaryService>(svc), key](const JobError& je) {
            d->llmRuns.remove(key);
            if (providerObj) providerObj->deleteLater();
            if (svc) svc->deleteLater();
            d->jobs->fail(id, JobKind::ExtractTopics, je);
            emit errorOccurred(tr("Téma-kinyerés hiba: %1").arg(je.message));
        });
    if (prep && d->llmRuns.contains(key)) d->llmRuns[key].preparer = prep;
}

QVector<TopicAnalysis> AppController::topicAnalyses(const QString& meetingId) const
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return {};
    return SummaryRepository::readAnalyses(m.folder);
}

void AppController::generateComplexSummary(const QString& meetingId, const QVector<SummaryTopic>& topics)
{
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }
    if (topics.isEmpty()) { emit errorOccurred(tr("Nincs téma a komplex összefoglalóhoz.")); return; }

    // Persisting the (edited) topic list: it stays resumable.
    SummaryRepository::writeTopics(m.folder, topics);
    emit topicsChanged(meetingId);

    // Only the topics NOT YET ANALYZED go into the queue (finished analyses are on disk), so after a
    // cancel/error a restart resumes where it stopped. Rerunning an already finished topic can be requested
    // with the card's own button (analyzeTopic).
    const QVector<TopicAnalysis> existing =
        SummaryRepository::readAnalyses(m.folder);
    QSet<QString> doneIds;
    for (const TopicAnalysis& a : existing) doneIds.insert(a.topicId);

    QVector<SummaryTopic> missing;
    for (const SummaryTopic& t : topics)
        if (!doneIds.contains(t.id)) missing.append(t);

    // Cloud: the complex summary (topic gathering + topic analyses + reduce) is ONE run with one
    // X-Tanara-Job-Id. If the topic-gathering run (or an already running batch) is still open, we continue it;
    // otherwise (e.g. continuing after a cancel / error, where the run has already closed) a new run starts
    // with a new id.
    if (usesCloud(providers::WorkflowStep::Summarize)) {
        CloudRunPtr run = d->complexRuns.value(meetingId);
        if (!run) {
            run = newCloudRun(meetingId, QStringLiteral("complex"));
            d->complexRuns.insert(meetingId, run);
        }
        run->kind = QStringLiteral("complex");
    }
    if (missing.isEmpty()) { finalizeComplexSummary(meetingId); return; }
    enqueueTopicAnalyses(meetingId, missing, /*reduceWhenDone*/ true);
}

void AppController::analyzeTopic(const QString& meetingId, const SummaryTopic& topic)
{
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }
    if (topic.id.isEmpty() || topic.title.trimmed().isEmpty()) {
        emit errorOccurred(tr("A témához cím kell az elemzéshez."));
        return;
    }

    // Carrying the (edited) topic into topics.json: the reduce's order/content comes from there.
    QVector<SummaryTopic> topics = SummaryRepository::readTopics(m.folder);
    bool found = false;
    for (SummaryTopic& t : topics)
        if (t.id == topic.id) { t = topic; found = true; break; }
    if (!found) topics.append(topic);
    SummaryRepository::writeTopics(m.folder, topics);
    emit topicsChanged(meetingId);

    enqueueTopicAnalyses(meetingId, { topic }, /*reduceWhenDone*/ false);
}

void AppController::enqueueTopicAnalyses(const QString& meetingId,
                                         const QVector<SummaryTopic>& topics, bool reduceWhenDone)
{
    if (reduceWhenDone) d->reduceWhenDone.insert(meetingId);
    if (!d->jobCounts.contains(meetingId)) d->jobCounts.insert(meetingId, {0, 0});

    int added = 0;
    for (const SummaryTopic& t : topics) {
        // Dedup: if the same topic is already running or queued, it does not go in again.
        if (d->topicJobActive && d->activeTopicMeetingId == meetingId && d->activeTopicId == t.id)
            continue;
        const bool queued = std::any_of(d->topicJobQueue.cbegin(), d->topicJobQueue.cend(),
            [&](const Impl::TopicJob& j) { return j.meetingId == meetingId && j.topic.id == t.id; });
        if (queued)
            continue;
        d->topicJobQueue.append({ meetingId, t });
        ++added;
        d->jobs->clearTopicError(meetingId, t.id);   // a new attempt: the old error loses its validity
        emit topicAnalysisQueued(meetingId, t.id);
        emit topicStatusChanged(meetingId, t.id);
    }
    // Structured task: ONE "Analyzing topics" per meeting (with count progress).
    if (added > 0 || d->jobs->isRunning(meetingId, JobKind::AnalyzeTopics)) {
        d->topicBatchTotal[meetingId] += added;
        if (!d->jobs->isRunning(meetingId, JobKind::AnalyzeTopics))
            d->jobs->begin(meetingId, JobKind::AnalyzeTopics, tr("Témák elemzése"));
        d->updateTopicCounts(meetingId);
    }
    if (!d->topicJobActive)
        startNextTopicJob();
}

void AppController::startNextTopicJob()
{
    // Did we reach the end of the queue? We close the batch per meeting: queueFinished + (if requested and
    // nothing failed) auto-reduce. Jobs of several meetings may in principle mix in the queue, so we close a
    // meeting only when no job is waiting for it.
    if (d->topicJobQueue.isEmpty()) {
        d->topicJobActive = false;
        d->activeTopicMeetingId.clear();
        d->activeTopicId.clear();
        d->activeTopicRun = {};
        const auto counts = d->jobCounts;
        d->jobCounts.clear();
        for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
            const QString& id = it.key();
            const int ok = it.value().first, fail = it.value().second;
            // Structured closing: a failed topic -> the summary step's error (the per-topic details are in
            // topicStatuses); otherwise done.
            d->topicBatchTotal.remove(id);
            if (d->jobs->isRunning(id, JobKind::AnalyzeTopics)) {
                if (fail > 0) {
                    JobError je;
                    je.message = tr("%1 téma elemzése nem sikerült.").arg(fail);
                    d->jobs->fail(id, JobKind::AnalyzeTopics, je);
                } else {
                    d->jobs->finish(id, JobKind::AnalyzeTopics);
                }
            }
            emit topicAnalysisQueueFinished(id, ok, fail);
            const bool wantReduce = d->reduceWhenDone.remove(id);
            if (wantReduce && fail == 0) {
                finalizeComplexSummary(id);   // the cloud run continues with the reduce
                continue;
            }
            if (wantReduce)
                emit jobProgress(id, tr(
                    "%1 téma elemzése nem sikerült — futtasd újra a kártyáján, majd kérd a végső összegzést.")
                    .arg(fail));
            // A cloud run closing without a reduce (rerunning one topic, or a non-cloud error): the sum of the
            // charges so far goes to the cost feedback.
            const CloudRunPtr run = d->complexRuns.take(id);
            if (run && !run->lastError.isError())
                finishCloudRun(run);
        }
        return;
    }

    const Impl::TopicJob job = d->topicJobQueue.takeFirst();
    // An early (pre-provider-call) failure: a remembered topic error + the queue goes on.
    auto failEarly = [this, &job](const QString& msg) {
        d->jobCounts[job.meetingId].second++;
        JobError je;
        je.message = msg;
        d->jobs->setTopicError(job.meetingId, job.topic.id, je);
        d->updateTopicCounts(job.meetingId);
        emit topicAnalysisFailed(job.meetingId, job.topic.id, msg);
        emit topicStatusChanged(job.meetingId, job.topic.id);
        startNextTopicJob();
    };

    Meeting m = d->store->load(job.meetingId);
    if (m.id.isEmpty()) { failEarly(tr("Ismeretlen meeting.")); return; }

    MergedTranscript merged = d->mergedCache.value(m.id);
    if (merged.tokens.isEmpty())
        merged = TranscriptRepository::readTokens(m.folder);
    if (merged.tokens.isEmpty()) { failEarly(tr("Nincs átirat — előbb futtass átírást.")); return; }
    editing::speakeredit::applyResolvedSpeakers(merged, m, d->personNames());

    const AppSettings s = d->settings->settings();
    CloudRunPtr run = d->complexRuns.value(m.id);
    if (usesCloud(providers::WorkflowStep::Summarize) && !run) {   // rerunning one topic without a batch
        run = newCloudRun(m.id, QStringLiteral("complex"));
        d->complexRuns.insert(m.id, run);
    }
    if (run) run->kind = QStringLiteral("complex");   // after topic gathering, the analysis stage
    providers::ProviderConfig cfg = llmConfigFor(run, m.id, QStringLiteral("complex"));
    const auto sink = std::make_shared<FailureSink>();
    providers::ProviderContext pctx = providerContext(run, QStringLiteral("complex"));
    captureFailures(pctx, sink);
    providers::IChatProvider* provider =
        d->providerRegistry.create<providers::IChatProvider>(s.providers.llm.selectedId, pctx, cfg, this);
    if (!provider) { failEarly(tr("Ismeretlen LLM-provider: %1.").arg(s.providers.llm.selectedId)); return; }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    auto* svc = new processing::ComplexSummaryService(provider, this);
    svc->setLanguage(s.processing.summaryLanguage);

    d->topicJobActive = true;
    d->activeTopicMeetingId = m.id;
    d->activeTopicId = job.topic.id;
    d->activeTopicRun = Impl::LlmRun{svc, providerObj, run};
    d->jobs->setMessage(m.id, JobKind::AnalyzeTopics, tr("„%1” téma elemzése…").arg(job.topic.title));
    emit topicAnalysisStarted(m.id, job.topic.id);
    emit topicStatusChanged(m.id, job.topic.id);
    emit jobProgress(m.id, tr("„%1” téma elemzése…").arg(job.topic.title));

    auto cleanup = [providerObj, svc]() {
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
    };

    connect(svc, &processing::ComplexSummaryService::topicAnalysisReady, this,
            [this, meetingId = m.id, folder = m.folder, cleanup](const TopicAnalysis& a) {
        SummaryRepository::upsertAnalysis(folder, a);   // IMMEDIATELY to disk: the work must not be lost
        d->jobCounts[meetingId].first++;
        cleanup();
        // The topic is no longer "running": the state query (topicStatuses) now gives "done".
        d->activeTopicId.clear();
        d->activeTopicRun = {};
        d->jobs->clearTopicError(meetingId, a.topicId);
        d->updateTopicCounts(meetingId);
        emit topicAnalysisReady(meetingId, a);
        emit topicStatusChanged(meetingId, a.topicId);
        startNextTopicJob();
    });
    const QString transcriptMd = merged.renderMarkdown();
    const QString analysisPrompt = resolvedPrompt(s, s.processing.topicAnalysisPrompt, "analysis");
    const auto ctx = std::make_shared<Impl::LlmContextState>();
    const auto prepError = std::make_shared<JobError>();   // the preparation's error (if that failed)
    ctx->need = providers::llmctx::callContextNeed(analysisPrompt.size() + transcriptMd.size() + m.contextNote.size()
                                        + job.topic.title.size() + job.topic.summary.size() + 400,
                                        cfg.maxTokens);
    connect(svc, &processing::ComplexSummaryService::failed, this,
            [this, meetingId = m.id, topicId = job.topic.id, cleanup, run, sink, ctx, prepError](const QString& raw) {
        d->jobCounts[meetingId].second++;
        cleanup();
        d->activeTopicId.clear();
        d->activeTopicRun = {};
        const JobError je = prepError->isValid()
            ? *prepError : d->describeLlmFailure(JobKind::AnalyzeTopics, raw, run, sink, ctx);
        const QString e = je.message;
        d->jobs->setTopicError(meetingId, topicId, je);
        if (run && run->lastError.isError()) {
            // Cloud error (402 / 403 / 426 / 429 / 503 ...): the meeting's remaining topics would get the same, so we
            // take them out of the queue; the partial-failure dialog shows the amount charged so far and "Continue"
            // (it pays only for the remaining parts).
            emit topicAnalysisFailed(meetingId, topicId, run->lastError.message.isEmpty() ? e : run->lastError.message);
            emit topicStatusChanged(meetingId, topicId);
            for (int i = d->topicJobQueue.size() - 1; i >= 0; --i) {
                if (d->topicJobQueue.at(i).meetingId != meetingId) continue;
                const QString dropped = d->topicJobQueue.takeAt(i).topic.id;
                d->jobCounts[meetingId].second++;
                emit topicAnalysisFailed(meetingId, dropped, tr("Megszakítva"));
                emit topicStatusChanged(meetingId, dropped);
            }
            d->reduceWhenDone.remove(meetingId);
            d->complexRuns.remove(meetingId);
            failCloudRun(run, e);
        } else {
            emit topicAnalysisFailed(meetingId, topicId, e);   // the other topics go on
            emit topicStatusChanged(meetingId, topicId);
        }
        d->updateTopicCounts(meetingId);
        startNextTopicJob();
    });

    providers::ILocalModelServer* prep = d->prepareLlm(m.id, JobKind::AnalyzeTopics, cfg, bool(run), ctx,
        [svc = QPointer<processing::ComplexSummaryService>(svc), transcriptMd, topic = job.topic,
         note = m.contextNote.trimmed(), analysisPrompt, cfg]() {
            if (svc) svc->requestTopicAnalysis(transcriptMd, topic, note, analysisPrompt,
                                               cfg.model, cfg.temperature, cfg.maxTokens);
        },
        [svc = QPointer<processing::ComplexSummaryService>(svc), prepError](const JobError& je) {
            // The same path as for a provider error: the topic's error is remembered, the queue goes on.
            *prepError = je;
            if (svc) emit svc->failed(je.message);
        });
    if (prep) d->activeTopicRun.preparer = prep;
}

void AppController::finalizeComplexSummary(const QString& meetingId)
{
    if (d->jobs->isRunning(meetingId, JobKind::Summarize))
        return;   // a merge is already running

    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }

    // The analyses on disk, in the (edited) order of topics.json; orphan analyses (belonging to a deleted
    // topic) are left out.
    const QVector<SummaryTopic> topics =
        SummaryRepository::readTopics(m.folder);
    const QVector<TopicAnalysis> all =
        SummaryRepository::readAnalyses(m.folder);
    QVector<TopicAnalysis> ordered;
    for (const SummaryTopic& t : topics)
        for (const TopicAnalysis& a : all)
            if (a.topicId == t.id) { ordered.append(a); break; }
    if (ordered.isEmpty()) {
        emit errorOccurred(tr("Nincs kész téma-elemzés — előbb futtasd a témánkénti elemzést."));
        return;
    }

    const AppSettings s = d->settings->settings();
    CloudRunPtr run = d->complexRuns.take(meetingId);   // the batch's run (if any) continues
    providers::ProviderConfig cfg = llmConfigFor(run, meetingId, QStringLiteral("complex"));
    const auto sink = std::make_shared<FailureSink>();
    providers::ProviderContext pctx = providerContext(run, QStringLiteral("complex"));
    captureFailures(pctx, sink);
    providers::IChatProvider* provider =
        d->providerRegistry.create<providers::IChatProvider>(s.providers.llm.selectedId, pctx, cfg, this);
    if (!provider) { emit errorOccurred(tr("Ismeretlen LLM-provider: %1.").arg(s.providers.llm.selectedId)); return; }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    auto* svc = new processing::ComplexSummaryService(provider, this);
    svc->setLanguage(s.processing.summaryLanguage);
    svc->setReducePrompt(resolvedPrompt(s, QString(), "reduce"));
    emit jobProgress(meetingId, tr("Összegzés (vezetői összefoglaló + teendők)…"));

    const QString key = Impl::llmKey(meetingId, JobKind::Summarize);
    d->jobs->begin(meetingId, JobKind::Summarize, tr("Összegzés készítése"));
    d->llmRuns.insert(key, Impl::LlmRun{svc, providerObj, run});

    auto cleanup = [providerObj, svc]() {
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
    };

    const auto ctx = std::make_shared<Impl::LlmContextState>();
    {
        qint64 chars = resolvedPrompt(s, QString(), "reduce").size() + m.contextNote.size() + 400;
        for (const TopicAnalysis& a : ordered) chars += a.renderMarkdown().size() + a.title.size() + 40;
        ctx->need = providers::llmctx::callContextNeed(chars, cfg.maxTokens);
    }
    connect(svc, &processing::ComplexSummaryService::failed, this,
            [this, meetingId, cleanup, run, key, sink, ctx](const QString& e) {
        d->llmRuns.remove(key);
        cleanup();
        const JobError je = d->describeLlmFailure(JobKind::Summarize, e, run, sink, ctx);
        d->jobs->fail(meetingId, JobKind::Summarize, je);
        failCloudRun(run, tr("Komplex összefoglaló hiba: %1").arg(je.message));
    });
    connect(svc, &processing::ComplexSummaryService::reduceReady, this,
            [this, m, ordered, cleanup, run, key, llmId = s.providers.llm.selectedId, model = cfg.model]
            (const QString& execSummary, const QVector<ActionItem>&) {
        d->llmRuns.remove(key);
        // Action items from CODE (not from the LLM): we collect the per-topic analyses' action items with a
        // normalized-text dedup. Deterministic, model-independent; only the executive summary is left to the LLM
        // reduce (less room for "thinking out loud").
        QVector<ActionItem> mergedItems;
        QSet<QString> seen;
        for (const TopicAnalysis& a : ordered)
            for (const ActionItem& ai : a.actionItems) {
                const QString norm = ai.text.simplified().toLower();
                if (norm.isEmpty() || seen.contains(norm))
                    continue;
                seen.insert(norm);
                mergedItems.append(ai);
            }
        const QString md = SummaryRepository::renderComplexMarkdown(execSummary, mergedItems, ordered);
        Meeting mm = d->store->load(m.id);
        if (mm.id.isEmpty()) {   // the meeting was deleted meanwhile
            cleanup();
            d->jobs->cancelled(m.id, JobKind::Summarize);
            finishCloudRun(run);
            return;
        }
        SummaryRepository::writeMarkdown(mm.folder, md);
        // Structured form + metadata (summary.json): executive summary, merged action items, the union of the
        // topics' decisions, and the topic analyses themselves, in order.
        library::SummaryDocument doc;
        doc.exists = true;
        doc.markdown = md;
        doc.summary.execSummary = execSummary;
        doc.summary.actionItems = mergedItems;
        for (const TopicAnalysis& a : ordered)
            for (const QString& dec : a.decisions)
                if (!doc.summary.decisions.contains(dec)) doc.summary.decisions << dec;
        doc.summary.openQuestions = SummaryRepository::openQuestionsOf(ordered);
        doc.topics = ordered;
        doc.meta.createdAt = QDateTime::currentDateTime();
        doc.meta.providerId = llmId;
        doc.meta.model = model;
        doc.meta.mode = library::SummaryMode::Topics;
        doc.meta.contextNote = m.contextNote.trimmed();   // at the start of the final merge
        doc.meta.contextNoteKnown = true;
        library::SummaryRepository::save(mm.folder, doc);
        const QString noteName = QStringLiteral("%1 %2.md")
            .arg(mm.startedAt.toString(QStringLiteral("yyyy-MM-dd")), slugify(mm.title));
        SummaryRepository::writeNoteCopy(d->notesDir, noteName, md);
        mm.hasSummary = true;
        d->store->saveMeeting(mm);
        cleanup();
        d->jobs->finish(mm.id, JobKind::Summarize);
        emit summaryReady(mm.id, SummaryRepository::markdownPath(mm.folder));
        finishCloudRun(run);   // "The summary cost $0.46 (12 parts)."
    });

    providers::ILocalModelServer* prep = d->prepareLlm(meetingId, JobKind::Summarize, cfg, bool(run), ctx,
        [svc = QPointer<processing::ComplexSummaryService>(svc), ordered, note = m.contextNote.trimmed(), cfg]() {
            if (svc) svc->requestReduce(ordered, note, cfg.model, cfg.temperature, cfg.maxTokens);
        },
        [this, meetingId, key, providerObj = QPointer<QObject>(providerObj),
         svc = QPointer<processing::ComplexSummaryService>(svc)](const JobError& je) {
            d->llmRuns.remove(key);
            if (providerObj) providerObj->deleteLater();
            if (svc) svc->deleteLater();
            d->jobs->fail(meetingId, JobKind::Summarize, je);
            emit errorOccurred(tr("Komplex összefoglaló hiba: %1").arg(je.message));
        });
    if (prep && d->llmRuns.contains(key)) d->llmRuns[key].preparer = prep;
}

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

void AppController::publishTagSuggestions(const QString& meetingId, QVector<TagSuggestion> list)
{
    // The suggestion from the participants' manual tags ("By participants") is always computed fresh: earlier
    // People items drop out, new ones stand first (the row's source tag comes from these). If a tag is also
    // suggested by similarity, one item remains and the reasons add up. It works before transcription too
    // (manual / approved participants).
    list.erase(std::remove_if(list.begin(), list.end(), [](const TagSuggestion& sg) {
        return sg.source == SuggestionSource::People;
    }), list.end());
    if (d->settings->settings().processing.tagSuggestions) {
        QVector<TagSuggestion> people = d->tags->suggestMeetingTagsFromParticipants(meetingId);
        for (TagSuggestion& p : people) {
            const auto dup = std::find_if(list.begin(), list.end(), [&](const TagSuggestion& sg) {
                return !sg.isNew && sg.tagId == p.tagId;
            });
            if (dup == list.end()) continue;
            p.reasons += dup->reasons;
            p.similarMeetings = dup->similarMeetings;
            list.erase(dup);
        }
        list = people + list;
    }
    // An already applied and a rejected suggestion are dropped (the LLM's new-name idea too, if a tag with
    // that name has since been put on the meeting).
    const QStringList applied = d->tags->tagsOf(meetingId);
    QStringList appliedKeys;
    for (const QString& id : applied) appliedKeys << library::tagKey(d->tags->tag(id).name);
    list.erase(std::remove_if(list.begin(), list.end(), [&](const TagSuggestion& sg) {
        if (sg.isNew) return appliedKeys.contains(library::tagKey(sg.name)) || d->tags->isRejected(meetingId, sg.name);
        return applied.contains(sg.tagId) || !d->tags->tag(sg.tagId).isValid()
            || d->tags->isRejected(meetingId, sg.tagId);
    }), list.end());
    d->pendingTags.insert(meetingId, list);
    emit tagSuggestionsReady(meetingId, list);
}

// Publishing the cached similarity-based list (the computation runs on a background thread:
// startTagSuggestionJob).
void AppController::computeTagSuggestions(const QString& meetingId)
{
    tanara::PerfScope perfScope("AppController::computeTagSuggestions", 5);
    const auto it = d->similarCache.constFind(meetingId);
    if (it != d->similarCache.constEnd()) publishTagSuggestions(meetingId, *it);
}

void AppController::invalidateTagSuggestions()
{
    d->similarCache.clear();
    ++d->tagDataGen;
}

// The profiles (rows + transcript phrases) are built on a background thread; at the end of the round
// (idle) the suggestion computation starts.
void AppController::scheduleTagSuggestions(const QString& meetingId)
{
    d->tagRequests.insert(meetingId);
    d->profiles->ensureBuilt();
}

// Threading: TagService belongs to the main thread, so a small snapshot of the tag data (TagSnapshot) is
// made here, on the main thread; the background thread uses only that and the thread-safe readers of
// MeetingProfiles / EmbeddingIndex. The result comes back to the main thread via a queued call
// (finishTagSuggestionJob). ~AppController waits for the running computation.
void AppController::startTagSuggestionJob(const QString& meetingId)
{
    auto run = d->tagRuns.find(meetingId);
    if (run == d->tagRuns.end() || (!run->publish && !run->llm)) {
        d->tagRuns.remove(meetingId);
        return;
    }
    if (run->running) return;   // the running one's result answers (it restarts on a data change)
    const auto cached = d->similarCache.constFind(meetingId);
    if (cached != d->similarCache.constEnd()) {
        finishTagSuggestionJob(meetingId, d->tagDataGen, *cached);
        return;
    }
    run->running = true;
    library::TagSnapshot snap;
    {
        tanara::PerfScope perfScope("AppController::startTagSuggestionJob (pillanatkép, fő szál)", 5);
        snap = library::TagSuggester::snapshot(*d->tags, meetingId);
    }
    const quint64 gen = d->tagDataGen;
    const library::MeetingProfiles* profiles = d->profiles;
    const library::EmbeddingIndex* index = d->embeddings;
    d->tagPool.start([this, snap, gen, profiles, index]() {
        QVector<TagSuggestion> list;
        {
            tanara::PerfScope perfScope("TagSuggester::suggestFrom (háttérszál)", 0);
            list = library::TagSuggester::suggestFrom(snap, profiles, index);
        }
        const QString meetingId = snap.meetingId;
        QMetaObject::invokeMethod(this, [this, meetingId, gen, list]() {
            finishTagSuggestionJob(meetingId, gen, list);
        }, Qt::QueuedConnection);
    });
}

void AppController::finishTagSuggestionJob(const QString& meetingId, quint64 dataGen, QVector<TagSuggestion> list)
{
    auto run = d->tagRuns.find(meetingId);
    if (run == d->tagRuns.end()) return;
    run->running = false;
    if (dataGen != d->tagDataGen) {
        // The tags / transcript / index changed meanwhile: the result is stale.
        if (run->publish || run->llm) scheduleTagSuggestions(meetingId);
        else d->tagRuns.erase(run);
        return;
    }
    const Impl::TagRun done = *run;
    d->tagRuns.erase(run);
    d->similarCache.insert(meetingId, list);
    if (done.publish) publishTagSuggestions(meetingId, list);
    if (done.llm) startLlmTagSuggestions(meetingId, list);
}

void AppController::requestTagSuggestions(const QString& meetingId)
{
    if (meetingId.isEmpty()) return;
    emit tagSuggestionsComputing(meetingId);
    if (!d->settings->settings().processing.tagSuggestions) {
        publishTagSuggestions(meetingId, {});
        return;
    }
    if (d->similarCache.contains(meetingId)) {
        computeTagSuggestions(meetingId);
        return;
    }
    d->tagRuns[meetingId].publish = true;
    if (!d->tagRuns.value(meetingId).running) scheduleTagSuggestions(meetingId);
}

void AppController::requestCooccurSuggestions(const QString& meetingId, const QString& tagId)
{
    if (!d->settings->settings().processing.tagSuggestions) return;
    const library::TagSuggester suggester(d->tags, d->profiles, d->embeddings);
    const QVector<TagSuggestion> list = suggester.cooccur(meetingId, tagId);
    if (list.isEmpty()) return;
    emit tagSuggestionsComputing(meetingId);
    publishTagSuggestions(meetingId, list);
}

void AppController::requestLlmTagSuggestions(const QString& meetingId)
{
    const AppSettings s = d->settings->settings();
    if (!s.processing.tagSuggestions) return;
    const library::SummaryDocument doc = summaryDocument(meetingId);
    if (!doc.exists || doc.markdown.trimmed().isEmpty()) return;
    emit tagSuggestionsComputing(meetingId);
    // The candidate list (similarity-based suggestions) from the cache, or made on a background thread.
    if (d->similarCache.contains(meetingId)) {
        startLlmTagSuggestions(meetingId, d->similarCache.value(meetingId));
        return;
    }
    d->tagRuns[meetingId].llm = true;
    if (!d->tagRuns.value(meetingId).running) scheduleTagSuggestions(meetingId);
}

void AppController::startLlmTagSuggestions(const QString& meetingId, const QVector<TagSuggestion>& suggested)
{
    const AppSettings s = d->settings->settings();
    const library::SummaryDocument doc = summaryDocument(meetingId);
    if (!s.processing.tagSuggestions || !doc.exists || doc.markdown.trimmed().isEmpty()) {
        publishTagSuggestions(meetingId, d->pendingTags.value(meetingId));
        return;
    }
    // One LLM suggestion runs at a time (the local model works on one thread).
    if (d->llmTags->isRunning()) {
        const QString prev = d->llmTagsMeetingId;
        d->llmTags->cancel();
        if (d->llmTagsProvider) d->llmTagsProvider->deleteLater();
        d->llmTagsProvider = nullptr;
        d->llmTagsRun.reset();
        if (!prev.isEmpty() && prev != meetingId) publishTagSuggestions(prev, d->pendingTags.value(prev));
    }
    CloudRunPtr run;
    const providers::ProviderConfig cfg = llmConfigFor(run, meetingId, QStringLiteral("tags"));
    providers::IChatProvider* provider = d->providerRegistry.create<providers::IChatProvider>(
        s.providers.llm.selectedId, providerContext(run, QStringLiteral("quick")), cfg, this);
    if (!provider) {
        publishTagSuggestions(meetingId, d->pendingTags.value(meetingId));
        return;
    }
    d->llmTagsProvider = dynamic_cast<QObject*>(provider);
    d->llmTagsRun = run;
    d->llmTagsMeetingId = meetingId;
    d->llmTags->start(provider, cfg.model, processing::promptDefault(QStringLiteral("tags"), d->metaDir),
                      meetingId, doc.markdown, suggested);
}

QVector<TagSuggestion> AppController::pendingTagSuggestions(const QString& meetingId) const
{
    return d->pendingTags.value(meetingId);
}

QVector<TagSuggestion> AppController::draftTagSuggestions(const QString& title) const
{
    if (!d->settings->settings().processing.tagSuggestions) return {};
    const library::TagSuggester suggester(d->tags, d->profiles, d->embeddings);
    return suggester.suggestForDraft(title);
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
    d->settings->setSettings(s);   // settingsChanged → applyEmbeddingSettings(true)
}

void AppController::applyEmbeddingSettings(bool restart)
{
    const AppSettings s = d->settings->settings();
    const QString id = d->providerRegistry.has(providers::CapabilityKind::TextEmbedding, s.providers.embedding.selectedId)
        ? s.providers.embedding.selectedId : QString();
    providers::ProviderConfig base = s.providers.embedding.selected();
    // The Tanara Cloud variant has no model field: the gateway's default model.
    const QString model = id.isEmpty() ? QString()
        : (!base.model.trimmed().isEmpty() ? base.model.trimmed()
                                           : (id == providers::embeddingproviders::CloudId ? QStringLiteral("default") : QString()));
    const QString key = id.isEmpty() || model.isEmpty()
        ? QString() : id + QLatin1Char('|') + base.baseUrl.trimmed() + QLatin1Char('|') + model;
    if (key == d->embeddingKey && restart) return;   // another setting changed
    const QString oldModel = d->embeddingPreparer->state().model;
    const bool hadProvider = !d->embeddingKey.isEmpty();
    d->embeddingKey = key;

    library::EmbeddingPreparer::ProviderFactory factory;
    if (!key.isEmpty()) {
        factory = [this, id](QObject* parent) -> providers::ITextEmbedder* {
            const AppSettings cur = d->settings->settings();
            providers::ProviderConfig cfg = cur.providers.embedding.configs.value(id);
            cfg.type = id;
            providers::ProviderContext pctx;
            pctx.http = d->http;
            if (id == providers::embeddingproviders::CloudId) {
                // The same sign-in and header set as on the LLM path (/v1/embeddings), outside any run.
                cfg.baseUrl = d->cloud->apiBase();
                pctx = d->cloudLedger->contextFor(nullptr, QString(), d->http);
            } else {
                cfg.apiKey = d->secrets->get(providers::embeddingproviders::ApiKeySecret);
            }
            return d->providerRegistry.create<providers::ITextEmbedder>(id, pctx, cfg, parent);
        };
    }
    // A model change: the old embeddings cannot be compared to the new ones, so start over.
    if (restart && hadProvider && !key.isEmpty() && oldModel != model)
        d->embeddings->invalidateAll();
    d->embeddingPreparer->setProvider(key.isEmpty() ? QString() : id, model, factory);
    if (restart && !key.isEmpty()) d->embeddingPreparer->start();
}

library::SummaryDocument AppController::summaryDocument(const QString& meetingId) const
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return {};
    return library::SummaryRepository::load(m.folder);
}

QVector<SummaryTopic> AppController::meetingTopics(const QString& meetingId) const
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return {};
    return SummaryRepository::readTopics(m.folder);
}

QVector<TopicStatus> AppController::topicStatuses(const QString& meetingId) const
{
    QVector<TopicStatus> out;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return out;
    const QVector<SummaryTopic> topics =
        SummaryRepository::readTopics(m.folder);
    QSet<QString> done;
    for (const TopicAnalysis& a : SummaryRepository::readAnalyses(m.folder))
        done.insert(a.topicId);
    const QHash<QString, JobError> errors = d->jobs->topicErrors(meetingId);

    out.reserve(topics.size());
    for (const SummaryTopic& t : topics) {
        TopicStatus st;
        st.topicId = t.id;
        const bool running = d->topicJobActive && d->activeTopicMeetingId == meetingId
                             && d->activeTopicId == t.id;
        const bool queued = std::any_of(d->topicJobQueue.cbegin(), d->topicJobQueue.cend(),
            [&](const Impl::TopicJob& j) { return j.meetingId == meetingId && j.topic.id == t.id; });
        if (running) {
            st.state = TopicState::Running;
        } else if (queued) {
            st.state = TopicState::Queued;
        } else if (errors.contains(t.id)) {
            // The LAST attempt failed (this shows even if a finished analysis exists from earlier; that stays on
            // disk; a new successful run or a retry clears the error).
            st.state = TopicState::Failed;
            st.error = errors.value(t.id).message;
            st.errorDetail = errors.value(t.id).detail;
            st.fixActionHint = errors.value(t.id).fixActionHint;
        } else if (done.contains(t.id)) {
            st.state = TopicState::Done;
        }
        out.append(st);
    }
    return out;
}

QVector<SummaryTopic> AppController::setMeetingTopics(const QString& meetingId,
                                                      const QVector<SummaryTopic>& topics)
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return {}; }

    QVector<SummaryTopic> saved;
    QSet<QString> ids;
    for (SummaryTopic t : topics) {
        t.title = t.title.trimmed();
        t.summary = t.summary.trimmed();
        if (t.title.isEmpty()) continue;                      // a topic without a title cannot be saved
        if (t.id.isEmpty() || ids.contains(t.id))             // a new (or duplicate-id) topic
            t.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        ids.insert(t.id);
        saved.append(t);
    }
    // The deleted topics: out of the queue / the running one cancelled, their remembered error cleared.
    for (const SummaryTopic& old : SummaryRepository::readTopics(m.folder)) {
        if (ids.contains(old.id)) continue;
        cancelTopicAnalysis(meetingId, old.id);
        d->jobs->clearTopicError(meetingId, old.id);
    }
    SummaryRepository::writeTopics(m.folder, saved);
    emit topicsChanged(meetingId);
    return saved;
}

bool AppController::cancelTopicAnalysis(const QString& meetingId, const QString& topicId)
{
    bool any = false;
    for (int i = d->topicJobQueue.size() - 1; i >= 0; --i) {
        const Impl::TopicJob& j = d->topicJobQueue.at(i);
        if (j.meetingId != meetingId || j.topic.id != topicId) continue;
        d->topicJobQueue.removeAt(i);
        if (d->topicBatchTotal.value(meetingId) > 0) d->topicBatchTotal[meetingId]--;
        any = true;
    }
    const bool active = d->topicJobActive && d->activeTopicMeetingId == meetingId
                        && d->activeTopicId == topicId;
    if (active) {
        d->abortLlmRun(d->activeTopicRun);
        d->activeTopicRun = {};
        d->activeTopicId.clear();
        if (d->topicBatchTotal.value(meetingId) > 0) d->topicBatchTotal[meetingId]--;
        any = true;
    }
    if (!any) return false;
    // After removing a topic the batch is no longer complete, so there is no automatic final summary.
    d->reduceWhenDone.remove(meetingId);
    d->updateTopicCounts(meetingId);
    emit topicAnalysisFailed(meetingId, topicId, tr("Megszakítva"));
    emit topicStatusChanged(meetingId, topicId);
    if (active)
        startNextTopicJob();   // the queue goes on (or closes)
    return true;
}

void AppController::requestLlmContext(int tokens)
{
    if (tokens <= 0 || usesCloud(providers::WorkflowStep::Summarize)) return;
    const providers::ProviderConfig cfg = d->settings->settings().providers.llm.selected();
    d->contextFloor.insert(Impl::serverKey(cfg), tokens);
}

bool AppController::cancelJob(const QString& meetingId, JobKind kind)
{
    switch (kind) {
    case JobKind::Transcribe: {
        auto it = d->transcribeRuns.find(meetingId);
        if (it == d->transcribeRuns.end()) {
            // The transcript is already done, the last (identification) stage is running, so only that one is skipped.
            const auto ir = d->identifyRuns.constFind(meetingId);
            if (ir == d->identifyRuns.constEnd() || !ir->asStage) return false;
            ir->cancel->store(true);
            d->jobs->setCancelling(meetingId, kind);
            return true;
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
    case JobKind::ExtractTopics: {
        const QString key = Impl::llmKey(meetingId, kind);
        const auto it = d->llmRuns.find(key);
        if (it == d->llmRuns.end()) return false;
        const Impl::LlmRun run = *it;
        d->llmRuns.erase(it);
        d->abortLlmRun(run);
        // Cloud: there is no charge for the cancelled call; the run's charges so far (e.g. topic gathering +
        // analyses) go summed to the cost feedback.
        if (run.cloudRun && !run.cloudRun->lastError.isError())
            finishCloudRun(run.cloudRun);
        d->jobs->cancelled(meetingId, kind);
        return true;
    }
    case JobKind::AnalyzeTopics: {
        bool any = false;
        for (int i = d->topicJobQueue.size() - 1; i >= 0; --i) {
            if (d->topicJobQueue.at(i).meetingId != meetingId) continue;
            const QString dropped = d->topicJobQueue.takeAt(i).topic.id;
            emit topicAnalysisFailed(meetingId, dropped, tr("Megszakítva"));
            emit topicStatusChanged(meetingId, dropped);
            any = true;
        }
        const bool activeMine = d->topicJobActive && d->activeTopicMeetingId == meetingId
                                && !d->activeTopicId.isEmpty();
        if (activeMine) {
            const QString tid = d->activeTopicId;
            d->abortLlmRun(d->activeTopicRun);
            d->activeTopicRun = {};
            d->activeTopicId.clear();
            emit topicAnalysisFailed(meetingId, tid, tr("Megszakítva"));
            emit topicStatusChanged(meetingId, tid);
            any = true;
        }
        if (!any && !d->jobs->isRunning(meetingId, kind)) return false;
        // Closing the batch: no auto-summary, no "N topics failed" message; the analyses already finished stay on
        // disk, a continuation picks up from there.
        const QPair<int, int> counts = d->jobCounts.take(meetingId);
        d->reduceWhenDone.remove(meetingId);
        d->topicBatchTotal.remove(meetingId);
        const CloudRunPtr run = d->complexRuns.take(meetingId);
        if (run && !run->lastError.isError())
            finishCloudRun(run);
        emit topicAnalysisQueueFinished(meetingId, counts.first, counts.second);
        d->jobs->cancelled(meetingId, kind);
        if (activeMine)
            startNextTopicJob();   // other meetings' topics go on
        return true;
    }
    case JobKind::Mixdown:
        return d->mixdown->cancel(meetingId);
    case JobKind::Import:
        return d->services->import().cancel(meetingId);
    case JobKind::Export:
        return d->services->archive().cancel(meetingId);
    case JobKind::Identify: {
        const auto ir = d->identifyRuns.constFind(meetingId);
        if (ir == d->identifyRuns.constEnd() || ir->asStage) return false;
        ir->cancel->store(true);   // the thread stops before the next speaker
        d->jobs->setCancelling(meetingId, kind);
        return true;
    }
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
    if (d->identifyRuns.contains(meetingId) || !d->voiceModelUsable())
        return false;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return false;
    const MergedTranscript merged =
        TranscriptRepository::readTokens(m.folder);
    if (merged.tokens.isEmpty()) return false;

    // The still anonymous (unmapped) raw speaker labels and their audio source.
    struct Item { QString label; QString audioPath; qint64 offsetMs = 0; };
    QVector<Item> items;
    QStringList seen;
    for (const TranscriptToken& t : merged.tokens) {
        if (t.speaker.isEmpty() || seen.contains(t.speaker)) continue;
        seen << t.speaker;
        if (m.speakerMap.contains(t.speaker)) continue;
        const VoiceSource src = resolveVoiceSource(m, merged, t.speaker);
        if (!src.absPath.isEmpty()) items.append({t.speaker, src.absPath, src.offsetMs});
    }
    if (items.isEmpty()) {
        d->jobs->markIdentified(meetingId);   // no anonymous speaker, nothing to identify
        return false;
    }

    const int total = int(items.size());
    const JobKind kind = asStage ? JobKind::Transcribe : JobKind::Identify;
    if (asStage) {
        d->jobs->setStage(meetingId, kind, QStringLiteral("identify"), StageState::Running, 0,
                          tr("%1 / %2 beszélő").arg(0).arg(total));
    } else {
        d->jobs->begin(meetingId, kind, tr("Résztvevők azonosítása"));
        d->jobs->setCounts(meetingId, kind, 0, total);
    }

    // Decoding (ffmpeg) + embedding (ONNX) run on a background thread, with its OWN embedder instance (it
    // does not touch the main thread's embedder). Matching against the voiceprint DB and saving happen on the
    // main thread (applyIdentification): only the main thread reaches the DB.
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    auto results = std::make_shared<QVector<QPair<QString, EmbeddingSet>>>();
    const processing::VoiceEmbedderSet proto = d->voiceSet.freshCopy();
    QPointer<AppController> self(this);
    const media::MediaToolPtr media = d->media;
    QThread* th = QThread::create([self, items, merged, proto, cancel, results, meetingId,
                                   total, kind, asStage, media]() {
        const processing::VoiceEmbedderSet emb = proto.freshCopy();   // the thread's own model instances
        if (!emb.ensureLoaded()) return;
        int done = 0;
        for (const Item& it : items) {
            if (cancel->load()) break;
            const EmbeddingSet e = embeddingForLabel(*media, emb, it.audioPath, it.offsetMs, merged, it.label);
            ++done;
            // The result + real count progress back to the main thread.
            QMetaObject::invokeMethod(qApp, [self, results, label = it.label, e, meetingId,
                                             done, total, kind, asStage]() {
                if (!self) return;
                results->append({label, e});
                if (asStage) {
                    self->d->jobs->setStage(meetingId, kind, QStringLiteral("identify"),
                                            StageState::Running, done * 100 / total,
                                            AppController::tr("%1 / %2 beszélő").arg(done).arg(total));
                } else {
                    self->d->jobs->setCounts(meetingId, kind, done, total);
                }
            }, Qt::QueuedConnection);
        }
    });
    d->identifyRuns.insert(meetingId, Impl::IdentifyRun{th, cancel, asStage});
    const QString stampAtStart = d->transcriptStamp(meetingId, m.folder);
    connect(th, &QThread::finished, this,
            [this, th, meetingId, cancel, results, asStage, kind, stampAtStart, folder = m.folder]() {
        d->identifyRuns.remove(meetingId);
        th->deleteLater();
        // If the transcript changed during the run (a new transcription arrived), the embeddings belong to the
        // OLD transcript's labels: we do not write the names onto the new transcript; it counts as cancelled.
        // (Otherwise: even on cancel we save what was found so far.)
        const bool stale = d->transcriptStamp(meetingId, folder) != stampAtStart;
        const bool wasCancelled = cancel->load() || stale;
        if (!stale)
            d->applyIdentification(meetingId, *results);
        if (!wasCancelled)
            d->jobs->markIdentified(meetingId);
        if (asStage) {
            d->jobs->setStage(meetingId, kind, QStringLiteral("identify"),
                              wasCancelled ? StageState::Skipped : StageState::Done);
            d->jobs->finish(meetingId, kind);   // the transcript is done; the task closes successfully
        } else if (wasCancelled) {
            d->jobs->cancelled(meetingId, kind);
        } else {
            d->jobs->finish(meetingId, kind);
        }
    });
    th->start();
    return true;
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
    ed->setVoiceModelIds(d->editorModelIds(d->settings));
    ed->setEmbedderFactory(utteranceEmbedderFactory());
    // The track activity of the side check: cache, else decoded via the media port and saved.
    ed->setActivityProvider([media = d->media](const Meeting& mm) {
        return processing::loadOrComputeSideActivity(mm, *media);
    });
    // The tag evidence for the candidate ranking (manual tag + learned association).
    ed->setTagEvidenceProvider(d->candidateContext().tagEvidence);
    // Manual tags for the quick "+ #Tag" offer (the test seam, otherwise the TagService).
    {
        library::TagService* ts = d->tags;
        ed->setPersonTagsProvider(d->personTags ? d->personTags
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
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || m.folder.isEmpty()) return false;
    if (!library::SummaryRepository::setStatementFlagged(m.folder, statementId, true)) return false;
    emit summaryStatementsChanged(meetingId);
    return true;
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

processing::participants::CandidateContext AppController::Impl::candidateContext() const
{
    processing::participants::CandidateContext ctx;
    ctx.self = selfPerson();
    ctx.names = personNames();
    library::VoiceprintStore* vp = voiceprints.get();
    ctx.rank = [vp](const EmbeddingSet& set) {
        return vp ? vp->rankedMatches(set, set.keys()) : QVector<VoiceMatch>();
    };
    ctx.hasVoiceprint = [vp](const PersonId& person) { return vp && vp->printCount(person) > 0; };
    library::TagService* ts = tags;
    // The tag evidence (UX plan rule 4): the TagService snapshot, manual tag and learned association; the
    // configured seam (tests) overrides the manual tags.
    ctx.tagEvidence = [ts, override = personTags](const PersonId& person, const QStringList& meetingTags,
                                                  const QString& meetingId) {
        if (!ts) return library::TagEvidence();
        library::PersonTagSnapshot snap = ts->personTagSnapshot();
        if (override) snap.manual.insert(person, override(person));
        return snap.evidence(person, meetingTags, meetingId);
    };
    return ctx;
}

void AppController::setPersonTagsProvider(std::function<QStringList(const PersonId&)> provider)
{
    d->personTags = std::move(provider);
}

QVector<Participant> AppController::participants(const QString& meetingId) const
{
    return d->store->load(meetingId).participants;
}

bool AppController::participantAnalysisRunning(const QString& meetingId) const
{
    return d->analysisRuns.contains(meetingId);
}

bool AppController::participantApprovalPending(const QString& meetingId) const
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || m.approval || d->analysisRuns.contains(meetingId)) return false;
    for (const Participant& p : m.participants)
        if (p.source != ParticipantSource::Manual || !p.sides.isEmpty()) return true;
    return false;
}

void AppController::startSpeechCheck(const QString& meetingId, bool thenMixdown)
{
    if (d->speechCheckRunning.contains(meetingId)) return;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    d->speechCheckRunning.insert(meetingId);
    const QString folder = m.folder;
    const auto stop = d->analysisShutdown;
    QPointer<AppController> self(this);
    d->analysisPool.start([self, m, folder, stop, meetingId, thenMixdown, media = d->media]() {
        if (stop->load()) return;
        const QString fp = processing::trackactivity::activityFingerprint(m, folder);
        MeetingActivity act = MeetingActivity::load(folder, fp);
        if (act.isEmpty()) {
            act = processing::computeMeetingActivity(m, folder, *media);
            if (!act.isEmpty()) act.save(folder);
        }
        QMetaObject::invokeMethod(qApp, [self, meetingId, act, thenMixdown]() {
            if (self) self->finishSpeechCheck(meetingId, act, thenMixdown);
        }, Qt::QueuedConnection);
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

namespace {

// The leading ids of the meeting's included logical tracks on a given side (in track order).
QStringList includedLeaders(const Meeting& m, bool mic, bool loop)
{
    QStringList out;
    const QVector<library::tracknames::SegmentInfo> seg = library::tracknames::segments(m.tracks);
    for (int i = 0; i < m.tracks.size(); ++i) {
        const int lead = seg.value(i).leader >= 0 ? seg.value(i).leader : i;
        const Track& t = m.tracks.at(i);
        if (!t.included()) continue;
        if (!((mic && t.kind == TrackKind::Mic) || (loop && t.kind == TrackKind::Loopback))) continue;
        const QString id = m.tracks.at(lead).id;
        if (!out.contains(id)) out << id;
    }
    return out;
}

// Track ids mapped to the leading ids of the meeting's logical tracks (unknown ones dropped, no repeats).
QStringList normalizeTrackIds(const Meeting& m, const QStringList& trackIds)
{
    QStringList out;
    const QVector<library::tracknames::SegmentInfo> seg = library::tracknames::segments(m.tracks);
    for (const QString& id : trackIds) {
        for (int i = 0; i < m.tracks.size(); ++i) {
            if (m.tracks.at(i).id != id) continue;
            const int lead = seg.value(i).leader >= 0 ? seg.value(i).leader : i;
            const QString leadId = m.tracks.at(lead).id;
            if (!out.contains(leadId)) out << leadId;
            break;
        }
    }
    return out;
}

const Participant* participantById(const Meeting& m, const QString& id)
{
    for (const Participant& p : m.participants)
        if (!id.isEmpty() && p.id == id) return &p;
    return nullptr;
}

} // namespace

MeetingActivity AppController::trackActivity(const QString& meetingId)
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || m.tracks.isEmpty()) return {};
    // The strip shows EVERY track (the excluded one too): the fingerprint treats them all as active.
    Meeting all = m;
    for (Track& t : all.tracks) t.active = true;
    const QString fp = processing::trackactivity::activityFingerprint(all, m.folder);
    if (const auto it = d->activityView.constFind(meetingId);
        it != d->activityView.constEnd() && it->fingerprint == fp)
        return *it;
    MeetingActivity a = MeetingActivity::load(m.folder, fp);   // if every track is included, this one
    if (a.isEmpty()) a = MeetingActivity::load(m.folder, fp, MeetingActivity::viewFilePath(m.folder));
    if (!a.isEmpty()) {
        d->activityView.insert(meetingId, a);
        return a;
    }
    if (!d->activityViewRunning.contains(meetingId)) {
        d->activityViewRunning.insert(meetingId);
        const auto stop = d->analysisShutdown;
        QPointer<AppController> self(this);
        const QString folder = m.folder;
        d->analysisPool.start([self, all, folder, fp, stop, meetingId, media = d->media]() {
            MeetingActivity act;
            act.fingerprint = fp;
            if (!stop->load()) {
                act = processing::computeMeetingActivity(all, folder, *media);
                act.fingerprint = fp;
                if (!act.isEmpty()) act.save(folder, MeetingActivity::viewFilePath(folder));
            }
            QMetaObject::invokeMethod(qApp, [self, meetingId, act]() {
                if (self) self->finishTrackActivity(meetingId, act);
            }, Qt::QueuedConnection);
        });
    }
    MeetingActivity pending;
    pending.fingerprint = fp;
    return pending;
}

bool AppController::trackActivityComputing(const QString& meetingId) const
{
    return d->activityViewRunning.contains(meetingId);
}

void AppController::finishTrackActivity(const QString& meetingId, const MeetingActivity& activity)
{
    d->activityViewRunning.remove(meetingId);
    // An empty result also goes in with its fingerprint: we do not recompute until the tracks change.
    d->activityView.insert(meetingId, activity);
    emit trackActivityReady(meetingId);
}

QStringList AppController::speakerKeysFor(const Meeting& m, const QString& id)
{
    if (!m.hasTranscript || id.isEmpty()) return {};
    editing::SpeakerEditor* ed = speakerEditor(m.id);
    if (!ed || !ed->hasTranscript()) return {};
    const QVector<editing::EditorSpeaker> speakers = ed->speakers();
    const Participant* p = participantById(m, id);
    if (!p) {
        for (const editing::EditorSpeaker& s : speakers)
            if (s.key == id) return {id};
        return {};
    }
    QStringList raws;   // the bound raw speakers ("Speaker 1" or "Speaker 1@mic")
    for (const QString& r : p->rawSpeakerIds) raws << r.section(QLatin1Char('@'), 0, 0);
    QStringList keys;
    for (const editing::EditorSpeaker& s : speakers) {
        const bool byName = !p->personId.isNull() && s.personId == p->personId;
        const bool byRaw = !s.rawLabel.isEmpty() && raws.contains(s.rawLabel);
        if ((byName || byRaw) && !keys.contains(s.key)) keys << s.key;
    }
    return keys;
}

bool AppController::setParticipantTracks(const QString& meetingId, const QString& id,
                                         const QStringList& trackIds)
{
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || id.isEmpty()) return false;
    const QStringList ids = normalizeTrackIds(m, trackIds);
    int pIdx = -1;
    for (int i = 0; i < m.participants.size(); ++i)
        if (m.participants.at(i).id == id) pIdx = i;
    const QStringList keys = speakerKeysFor(m, id);
    if (pIdx < 0 && !keys.isEmpty()) {
        // Speaker key: the participant belonging to it (if any) gets it too.
        for (int i = 0; i < m.participants.size() && pIdx < 0; ++i)
            if (speakerKeysFor(m, m.participants.at(i).id).contains(id)) pIdx = i;
    }
    if (pIdx < 0 && keys.isEmpty()) return false;
    bool changed = false;
    if (pIdx >= 0 && m.participants.at(pIdx).tracks != ids) {
        Participant& p = m.participants[pIdx];
        p.tracks = ids;
        // The track-side evidence from the manual binding (the voice evidence is unchanged).
        double score = -1.0;
        const bool heard = !p.sides.isEmpty() || p.talkShare > 0.0;
        for (const Evidence& e : p.evidence)
            if (e.kind == EvidenceKind::Voice) score = e.value;
        p.evidence = processing::participants::participantEvidence(p, heard, score, m, d->candidateContext());
        d->store->saveMeeting(m);
        changed = true;
    }
    if (!keys.isEmpty())
        if (editing::SpeakerEditor* ed = speakerEditor(meetingId))
            for (const QString& key : keys)
                changed = ed->setTrackAssignment(key, ids) || changed;
    if (changed) emit participantsChanged(meetingId);
    return changed;
}

ParticipantTrackBinding AppController::participantTracks(const QString& meetingId, const QString& id)
{
    ParticipantTrackBinding b;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return b;
    const Participant* p = participantById(m, id);
    const QStringList keys = speakerKeysFor(m, id);
    editing::SpeakerEditor* ed = keys.isEmpty() ? nullptr : speakerEditor(meetingId);
    // Manual: after the transcript the editor's assignment is the truth (the "Why them?" chips), before it the participant's.
    if (ed) {
        for (const QString& key : keys) {
            const QStringList t = ed->trackAssignment(key);
            if (!t.isEmpty()) {
                b.trackIds = t;
                b.manual = true;
                return b;
            }
        }
    } else if (p && !p->tracks.isEmpty()) {
        b.trackIds = normalizeTrackIds(m, p->tracks);
        b.manual = !b.trackIds.isEmpty();
        if (b.manual) return b;
    }
    // Inferred: the editor's side analysis, otherwise the voice analysis's sides.
    bool mic = false, loop = false;
    if (ed) {
        const editing::SideReport r = ed->sideReport();
        for (const editing::PersonSide& ps : r.persons) {
            if (!keys.contains(ps.speakerKey)) continue;
            mic = mic || ps.side == editing::Side::Local || ps.side == editing::Side::Mixed;
            loop = loop || ps.side == editing::Side::Remote || ps.side == editing::Side::Mixed;
        }
    }
    if (!mic && !loop && p) {
        mic = p->sides.contains(QStringLiteral("mic"));
        loop = p->sides.contains(QStringLiteral("loopback"));
    }
    b.trackIds = includedLeaders(m, mic, loop);
    return b;
}

void AppController::copyParticipantTracksToOverlay(const QString& meetingId)
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || !m.hasTranscript) return;
    editing::SpeakerEditor* ed = speakerEditor(meetingId);
    if (!ed || !ed->hasTranscript()) return;
    for (const Participant& p : m.participants) {
        if (p.tracks.isEmpty()) continue;
        for (const QString& key : speakerKeysFor(m, p.id))
            if (ed->trackAssignment(key).isEmpty()) ed->setTrackAssignment(key, p.tracks);
    }
}

void AppController::includeTrack(const QString& meetingId, const QString& trackId)
{
    ParticipantUndoScope undoScope(this, meetingId, tr("Sáv beemelve"));
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
    ParticipantUndoScope undoScope(this, meetingId, tr("Sáv kivéve"));
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
    if (!d->voiceModelUsable() || d->analysisRuns.contains(meetingId)) return false;
    if (d->speechCheckRunning.contains(meetingId)) return false;   // the end of the measurement calls again
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || m.tracks.isEmpty() || m.approval.has_value()) return false;
    // The speechless tracks are not filtered out yet: the measurement first (its end calls again).
    if (!m.hasTranscript && editing::trackspeech::needsSpeechCheck(m) && !d->speechChecked.contains(meetingId))
        return false;
    if (processing::ParticipantAnalysisFile::load(m.folder).tracksKey == processing::trackactivity::activityFingerprint(m, m.folder))
        return false;   // it already ran on these same tracks
    return analyzeParticipants(meetingId);
}

bool AppController::analyzeParticipants(const QString& meetingId)
{
    if (d->analysisRuns.contains(meetingId)) return false;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || !d->voiceModelUsable()) return false;
    if (m.approval && m.approval->solo) return false;   // "only me": nothing to identify

    auto cancel = std::make_shared<std::atomic<bool>>(false);
    d->analysisRuns.insert(meetingId, cancel);
    emit participantAnalysisStarted(meetingId);
    const processing::VoiceEmbedderSet proto = d->voiceSet.freshCopy();
    const auto stop = d->analysisShutdown;
    QPointer<AppController> self(this);
    d->analysisPool.start([self, m, proto, cancel, stop, meetingId, media = d->media]() {
        const QString fp = processing::trackactivity::activityFingerprint(m, m.folder);
        ClusterSet cs;
        if (!stop->load() && !cancel->load()) {
            MeetingActivity act = MeetingActivity::load(m.folder, fp);
            if (act.isEmpty()) {
                act = processing::computeMeetingActivity(m, m.folder, *media);
                if (!act.isEmpty()) act.save(m.folder);
            }
            const processing::VoiceEmbedderSet emb = proto.freshCopy();   // the thread's own model instances
            // Progress: from the window index the track (k / N, only tracks with speech) and the percentage; a signal
            // goes to the main thread only on change.
            QVector<int> ends;   // cumulative window count of the tracks with speech
            int windows = 0;
            for (const TrackActivity& ta : act.tracks) {
                const int n = int(processing::participants::speechWindows(ta, QString()).size());
                if (n == 0) continue;
                windows += n;
                ends.append(windows);
            }
            auto last = std::make_shared<QPair<int, int>>(-1, -1);
            auto report = [self, meetingId, ends, last](int done, int total) {
                if (total <= 0 || ends.isEmpty()) return;
                int track = 1;
                while (track < ends.size() && done > ends.at(track - 1)) ++track;
                const int pct = qBound(0, done * 100 / total, 100);
                if (last->first == track && last->second == pct) return;
                *last = {track, pct};
                const int tracks = int(ends.size());
                QMetaObject::invokeMethod(qApp, [self, meetingId, track, tracks, pct]() {
                    if (self && self->d->analysisRuns.contains(meetingId))
                        emit self->participantAnalysisProgress(meetingId, track, tracks, pct);
                }, Qt::QueuedConnection);
            };
            cs = processing::participants::computeVoiceClusters(
                m, act, emb, processing::participants::filePcmReader(m, media),
                [cancel, stop, report](int done, int total) {
                    report(done, total);
                    return !cancel->load() && !stop->load();
                });
        } else {
            cs.cancelled = true;
        }
        QMetaObject::invokeMethod(qApp, [self, meetingId, cs, fp]() {
            if (self) self->finishParticipantAnalysis(meetingId, cs, fp);
        }, Qt::QueuedConnection);
    });
    return true;
}

void AppController::finishParticipantAnalysis(const QString& meetingId, ClusterSet clusters,
                                              const QString& tracksKey)
{
    d->analysisRuns.remove(meetingId);
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    if (clusters.cancelled || !clusters.error.isEmpty()) {
        if (!clusters.error.isEmpty())
            qCInfo(lcVoice).noquote() << "Résztvevő-elemzés nem futott:" << clusters.error;
        emit participantAnalysisFinished(meetingId);
        return;
    }
    if (m.approval && m.approval->solo) {   // it became "only me" meanwhile: the analysis result is not needed
        emit participantAnalysisFinished(meetingId);
        return;
    }
    m.participants = processing::participants::buildParticipants(clusters, m, d->candidateContext());

    processing::ParticipantAnalysisFile file;
    file.at = QDateTime::currentDateTime().toString(Qt::ISODate);
    file.modelIds = clusters.modelIds;
    file.tracksKey = tracksKey;
    file.clusters = clusters.clusters;
    if (!file.save(m.folder))
        qCWarning(lcVoice).noquote() << "Résztvevő-elemzés: nem írható:" << processing::ParticipantAnalysisFile::filePath(m.folder);

    // If the transcript already exists: the binding runs now (otherwise when the transcript arrives).
    const QVector<TranscriptLine> lines = editing::speakeredit::loadTranscriptLines(m.folder);
    if (!lines.isEmpty()) processing::participants::bindRawSpeakers(m.participants, file.clusters, lines);
    d->store->saveMeeting(m);
    qCInfo(lcVoice).noquote() << "Résztvevő-elemzés kész:" << m.participants.size() << "jelölt,"
                              << clusters.clusters.size() << "klaszter," << clusters.windows << "ablak";
    emit participantsChanged(meetingId);
    emit participantAnalysisFinished(meetingId);
}

bool AppController::bindRawSpeakers(const QString& meetingId)
{
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || m.participants.isEmpty()) return false;
    const processing::ParticipantAnalysisFile file = processing::ParticipantAnalysisFile::load(m.folder);
    const QVector<TranscriptLine> lines = editing::speakeredit::loadTranscriptLines(m.folder);
    if (file.isEmpty() || lines.isEmpty()) return false;
    if (!processing::participants::bindRawSpeakers(m.participants, file.clusters, lines)) return false;
    d->store->saveMeeting(m);
    emit participantsChanged(meetingId);
    return true;
}

bool AppController::applySpeakerBindings(const QString& meetingId, const QVector<SpeakerBinding>& bindings,
                                         const QString& undoText)
{
    if (bindings.isEmpty()) return false;
    editing::SpeakerEditor* ed = speakerEditor(meetingId);
    if (!ed || !ed->hasTranscript()) return false;
    return ed->applyBindings(bindings, undoText);
}

bool AppController::applyParticipantsToTranscript(const QString& meetingId)
{
    bindRawSpeakers(meetingId);
    // A manual track binding set before the transcript is carried into the speakers' assignment (one piece of data).
    copyParticipantTracksToOverlay(meetingId);
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || !m.approval || m.approval->skipped) return false;
    const QVector<TranscriptLine> lines = editing::speakeredit::loadTranscriptLines(m.folder);
    if (lines.isEmpty()) return false;
    QVector<SpeakerBinding> bindings;
    if (m.approval->solo) {
        d->ensureSelf();
        const PersonId self = d->selfPerson().id;
        if (self.isNull()) return false;
        QStringList raws;
        for (const TranscriptLine& l : lines)
            if (!l.rawLabel.isEmpty() && !raws.contains(l.rawLabel)) raws << l.rawLabel;
        for (const QString& r : raws) bindings.append({r, self, {}});
        applySpeakerBindings(meetingId, bindings, tr("Csak én beszéltem"));
        return true;
    }
    QStringList accepted;
    for (const Participant& p : m.participants)
        if (p.approved) accepted << p.id;
    const processing::ParticipantAnalysisFile file = processing::ParticipantAnalysisFile::load(m.folder);
    const MeetingActivity act =
        MeetingActivity::load(m.folder, processing::trackactivity::activityFingerprint(m, m.folder));
    bindings = processing::participants::approvalBindings(m.participants, accepted, lines, file.clusters,
                                              act.isEmpty() ? nullptr : &act, &m);
    applySpeakerBindings(meetingId, bindings, tr("Résztvevők jóváhagyása"));
    return true;
}

bool AppController::approveParticipants(const QString& meetingId, const QVector<Participant>& accepted)
{
    ParticipantUndoScope undoScope(this, meetingId, tr("Résztvevők jóváhagyva"));
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return false;
    const processing::participants::CandidateContext ctx = d->candidateContext();
    QStringList acceptedIds;
    for (const Participant& a : accepted) {
        int idx = -1;
        for (int i = 0; i < m.participants.size(); ++i)
            if (!a.id.isEmpty() && m.participants.at(i).id == a.id) idx = i;
        if (idx < 0) {
            // The new person added in the dialog (the caller resolved / created the person).
            Participant p = a;
            if (p.personId.isNull()) continue;
            p.id = QStringLiteral("m-") + QUuid::createUuid().toString(QUuid::Id128).left(8);
            p.source = ParticipantSource::Manual;
            p.evidence = processing::participants::participantEvidence(p, !p.sides.isEmpty(), -1.0, m, ctx);
            m.participants.append(p);
            idx = m.participants.size() - 1;
        } else if (!a.personId.isNull()) {
            m.participants[idx].personId = a.personId;   // person changed in the dialog
        }
        acceptedIds << m.participants.at(idx).id;
    }

    ParticipantApproval ap;
    ap.at = QDateTime::currentDateTime().toString(Qt::ISODate);
    ap.modelIds = processing::ParticipantAnalysisFile::load(m.folder).modelIds;
    for (Participant& p : m.participants) {
        p.approved = acceptedIds.contains(p.id) && !p.personId.isNull();
        ap.candidates.append({p.id, p.personId, processing::participants::participantGroup(p), p.approved,
                              p.rawSpeakerIds});
    }
    m.approval = ap;
    d->store->saveMeeting(m);
    emit participantsChanged(meetingId);
    // If the transcript already exists: the binding now (one undo step); otherwise when the transcript arrives.
    if (m.hasTranscript) applyParticipantsToTranscript(meetingId);
    return true;
}

bool AppController::skipApproval(const QString& meetingId)
{
    ParticipantUndoScope undoScope(this, meetingId, tr("Jóváhagyás kihagyva"));
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return false;
    ParticipantApproval ap;
    ap.at = QDateTime::currentDateTime().toString(Qt::ISODate);
    ap.modelIds = processing::ParticipantAnalysisFile::load(m.folder).modelIds;
    ap.skipped = true;
    for (const Participant& p : m.participants)
        ap.candidates.append({p.id, p.personId, processing::participants::participantGroup(p), false, p.rawSpeakerIds});
    m.approval = ap;
    d->store->saveMeeting(m);
    emit participantsChanged(meetingId);
    return true;
}

bool AppController::unbindParticipant(const QString& meetingId, const QString& participantId)
{
    ParticipantUndoScope undoScope(this, meetingId, tr("Nem volt ott"));
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return false;
    int idx = -1;
    for (int i = 0; i < m.participants.size(); ++i)
        if (m.participants.at(i).id == participantId) idx = i;
    if (idx < 0) return false;
    Participant& p = m.participants[idx];
    const QString pname = d->people ? d->people->displayName(p.personId) : QString();
    undoScope.text = tr("Nem volt ott: %1").arg(pname);

    // Its lines go anonymous: the bound raw speakers (for a two-sided one only that side's lines), and every
    // speaker bound to the person (e.g. from an earlier automatic identification).
    QVector<SpeakerBinding> bindings;
    if (m.hasTranscript) {
        const QVector<TranscriptLine> lines = editing::speakeredit::loadTranscriptLines(m.folder);
        const processing::ParticipantAnalysisFile file = processing::ParticipantAnalysisFile::load(m.folder);
        const MeetingActivity act =
            MeetingActivity::load(m.folder, processing::trackactivity::activityFingerprint(m, m.folder));
        Participant named = p;   // only their bindings (with a person, so two-sided lines come out)
        if (named.personId.isNull()) named.personId = PersonId::fromString(QStringLiteral("?"));
        for (SpeakerBinding b : processing::participants::approvalBindings({named}, {p.id}, lines, file.clusters,
                                                               act.isEmpty() ? nullptr : &act)) {
            b.personId = PersonId();
            bindings.append(b);
        }
        if (!p.personId.isNull())
            if (editing::SpeakerEditor* ed = speakerEditor(meetingId))
                for (const editing::EditorSpeaker& s : ed->speakers())
                    if (s.personId == p.personId)
                        bindings.append({s.key, PersonId(), {}});
    }
    p.approved = false;
    p.rawSpeakerIds.clear();
    if (m.approval)
        for (ParticipantApprovalEntry& e : m.approval->candidates)
            if (e.participantId == participantId) e.checked = false;
    d->store->saveMeeting(m);
    applySpeakerBindings(meetingId, bindings, tr("Nem volt ott: %1").arg(pname));
    emit participantsChanged(meetingId);
    return true;
}

QString AppController::addParticipant(const QString& meetingId, const PersonChoice& choice)
{
    const QString label = !choice.id.isNull() && d->people ? d->people->displayName(choice.id) : choice.newName;
    ParticipantUndoScope undoScope(this, meetingId, tr("Résztvevő felvéve: %1").arg(label));
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || choice.isAnonymous()) return {};
    if (!choice.id.isNull())
        for (const Participant& p : m.participants)
            if (p.personId == choice.id) return p.id;
    const PersonId person = d->people ? d->people->resolve(choice) : choice.id;
    if (person.isNull()) return {};
    if (choice.id.isNull()) emit peopleChanged();
    Participant p;
    p.id = QStringLiteral("m-") + QUuid::createUuid().toString(QUuid::Id128).left(8);
    p.personId = person;
    p.source = ParticipantSource::Manual;
    p.evidence = processing::participants::participantEvidence(p, false, -1.0, m, d->candidateContext());
    m.participants.append(p);
    d->store->saveMeeting(m);
    emit participantsChanged(meetingId);
    return p.id;
}

bool AppController::setSoloMeeting(const QString& meetingId)
{
    ParticipantUndoScope undoScope(this, meetingId, tr("Csak én beszéltem"));
    d->ensureSelf();
    const SelfPerson me = d->selfPerson();
    const PersonId self = me.id;
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || self.isNull() || me.name.isEmpty()) return false;   // no own name configured
    if (const auto it = d->analysisRuns.constFind(meetingId); it != d->analysisRuns.constEnd())
        it.value()->store(true);   // the running analysis's result is no longer needed

    bool have = false;
    for (Participant& p : m.participants) {
        const bool me = p.personId == self;
        p.approved = me;
        have = have || me;
    }
    if (!have) {
        Participant p;
        p.id = QStringLiteral("m-") + QUuid::createUuid().toString(QUuid::Id128).left(8);
        p.personId = self;
        p.source = ParticipantSource::Manual;
        p.approved = true;
        p.evidence = processing::participants::participantEvidence(p, false, -1.0, m, d->candidateContext());
        m.participants.append(p);
    }
    ParticipantApproval ap;
    ap.at = QDateTime::currentDateTime().toString(Qt::ISODate);
    ap.solo = true;
    for (const Participant& p : m.participants)
        ap.candidates.append({p.id, p.personId, processing::participants::participantGroup(p), p.approved, p.rawSpeakerIds});
    m.approval = ap;
    d->store->saveMeeting(m);
    if (m.hasTranscript) applyParticipantsToTranscript(meetingId);
    emit participantsChanged(meetingId);
    return true;
}

// ==== shared undo, permanent participant delete, participants after transcription, model operations ====

editing::MeetingUndoService* AppController::meetingUndo(const QString& meetingId)
{
    if (meetingId.isEmpty()) return nullptr;
    if (editing::MeetingUndoService* u = d->undoServices.value(meetingId)) {
        if (editing::SpeakerEditor* ed = d->speakerEditors.value(meetingId)) u->attachSpeakerEditor(ed);
        return u;
    }
    if (d->store->load(meetingId).id.isEmpty()) return nullptr;
    auto* u = new editing::MeetingUndoService(meetingId, this);
    u->attachTagService(d->tags);
    if (editing::SpeakerEditor* ed = d->speakerEditors.value(meetingId)) u->attachSpeakerEditor(ed);
    d->undoServices.insert(meetingId, u);
    connect(d->store, &library::MeetingRepository::meetingRemoved, u, [this, u](const QString& id) {
        if (id != u->meetingId()) return;
        d->undoServices.remove(id);
        u->deleteLater();
    });
    return u;
}

bool AppController::removeParticipant(const QString& meetingId, const QString& participantId)
{
    ParticipantUndoScope undoScope(this, meetingId, tr("Résztvevő eltávolítva"));
    const Meeting m0 = d->store->load(meetingId);
    if (m0.id.isEmpty()) return false;
    const auto has = [&participantId](const Participant& p) { return p.id == participantId; };
    const auto it0 = std::find_if(m0.participants.cbegin(), m0.participants.cend(), has);
    if (it0 == m0.participants.cend()) {
        // There is no participant record (the meeting got names before participant analysis): the id is the
        // transcript speaker's key; its lines go anonymous (one step).
        editing::SpeakerEditor* ed = m0.hasTranscript ? speakerEditor(meetingId) : nullptr;
        if (!ed) return false;
        for (const editing::EditorSpeaker& s : ed->speakers()) {
            if (s.key != participantId || s.anonymous) continue;
            undoScope.text = tr("Nem volt ott: %1").arg(s.displayName);
            applySpeakerBindings(meetingId, {{s.key, PersonId(), {}}}, undoScope.text);
            emit participantsChanged(meetingId);
            return true;
        }
        return false;
    }
    if (!it0->personId.isNull() && d->people)
        undoScope.text = tr("Résztvevő eltávolítva: %1").arg(d->people->displayName(it0->personId));

    // Its lines go anonymous (one editor step, which the shared step absorbs), then the deletion.
    unbindParticipant(meetingId, participantId);
    Meeting m = d->store->load(meetingId);
    m.participants.erase(std::remove_if(m.participants.begin(), m.participants.end(), has), m.participants.end());
    if (m.approval) {
        auto& c = m.approval->candidates;
        c.erase(std::remove_if(c.begin(), c.end(),
                               [&](const ParticipantApprovalEntry& e) { return e.participantId == participantId; }),
                c.end());
    }
    d->store->saveMeeting(m);
    emit participantsChanged(meetingId);
    return true;
}

QVector<Participant> AppController::unheardParticipants(const QString& meetingId) const
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || !m.hasTranscript) return {};
    // The persons of the transcript's speakers: from the open editor, otherwise resolved from disk.
    QSet<PersonId> heard;
    if (editing::SpeakerEditor* ed = d->speakerEditors.value(meetingId)) {
        for (const editing::EditorSpeaker& s : ed->speakers())
            if (s.utteranceCount > 0 && !s.personId.isNull()) heard.insert(s.personId);
    } else {
        const QVector<TranscriptLine> lines = editing::speakeredit::loadTranscriptLines(m.folder);
        const SpeakerOverlay ov = editing::speakeredit::loadOverlayFor(m.folder, lines);
        QSet<QString> keys;
        for (const TranscriptLine& l : lines) keys.insert(editing::speakeredit::resolveSpeakerKey(ov, l));
        for (const QString& k : std::as_const(keys)) {
            const PersonId person = editing::speakeredit::speakerPerson(ov, m.speakerMap, k);
            if (!person.isNull()) heard.insert(person);
        }
    }
    QVector<Participant> out;
    for (const Participant& p : m.participants)
        if (!p.personId.isNull() && p.rawSpeakerIds.isEmpty() && !heard.contains(p.personId))
            out.append(p);
    return out;
}

QVector<processing::VoiceprintBackfillSkip> AppController::backfillSkippedSamples() const
{
    QVector<processing::VoiceprintBackfillSkip> out = d->backfillSkipped;
    std::stable_sort(out.begin(), out.end(), [](const processing::VoiceprintBackfillSkip& a, const processing::VoiceprintBackfillSkip& b) {
        return a.person.localeAwareCompare(b.person) < 0;
    });
    return out;
}

void AppController::Impl::noteBackfillSkip(const PersonId& person, const QString& model, const Voiceprint& vp,
                                           processing::VoiceprintBackfillSkip::Reason reason)
{
    for (const processing::VoiceprintBackfillSkip& s : std::as_const(backfillSkipped))
        if (s.personId == person && s.model == model && s.sampleRef == vp.sampleRef
            && s.sourceMeetingId == vp.sourceMeetingId)
            return;
    const QString name = people ? people->displayName(person) : QString();
    backfillSkipped.append({person, name, model, vp.sampleRef, vp.sourceMeetingId, reason});
}

void AppController::rescanVoiceModels()
{
    const QStringList before = d->voiceModelIds;
    applyVoiceModels();   // it signals by itself on change (and backfills on growth)
    if (d->voiceModelIds == before) emit voiceModelsChanged();   // the file state may have changed anyway
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