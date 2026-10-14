#pragma once
//
// app::Services: the composition root of the services split out of AppController (strangler, P12).
// It creates the services from their dependencies (constructor injection) and owns them; the
// AppController owns the Services and hands it out through services().
#include "tanara/library/tags/TagSuggestionService.h"
#include "tanara/domain/Meeting.h"
#include "tanara/domain/Transcript.h"   // P12e
#include "tanara/media/MediaTool.h"

#include <functional>
#include <memory>

namespace tanara::providers { class ProviderRegistry; }
namespace tanara::library {
class ArchiveService;
class ImportService;
class MeetingJobTracker;
class MeetingRepository;
class TagService;
class MeetingProfiles;
class EmbeddingIndex;
class EmbeddingPreparer;
}
namespace tanara::recording {
class MixdownService;
class RecordingService;
}
namespace tanara::watcher { class CallMonitor; }
// --- P12d: participants, people, voice analysis, shared undo ---
namespace tanara::library {
class PeopleService;
class PeopleStats;
class PeopleStore;
class VoiceprintStore;
}
namespace tanara::editing {
class MeetingUndoRegistry;
class ParticipantAnalysisPort;
class ParticipantService;
class PeopleEditingAdapter;
class SpeakerEditor;
}
namespace tanara::processing { class VoiceAnalysisService; }
// --- end P12d ---
// --- P16: detector registry ---
namespace tanara::watcher { class MeetingDetectorRegistry; }
// --- end P16 ---
// --- P12e: summary and topic queue ---
namespace tanara::library { class JobStats; }
namespace tanara::processing {
class SummarizationService;
class SummaryLlmPort;
}
// --- end P12e ---
// --- P12f: transcription, workflows, editor sessions ---
#include "tanara/providers/core/ReadinessModel.h"
namespace tanara::processing {
class TranscriptionPort;
class TranscriptionService;
}
namespace tanara::editing { class EditorSessions; }
namespace tanara::app { class Workflows; }
// --- end P12f ---

namespace tanara::app {

class SettingsManager;

struct ServicesDeps {
    library::MeetingRepository* store = nullptr;
    library::TagService*        tags = nullptr;
    library::MeetingJobTracker* jobs = nullptr;
    media::MediaToolPtr         media;
    SettingsManager*            settings = nullptr;   // settings + AppPaths (recording root, state.json)

    // ---- P12c: tag suggestions ----
    library::MeetingProfiles*   profiles = nullptr;
    library::EmbeddingIndex*    embeddings = nullptr;
    library::EmbeddingPreparer* embeddingPreparer = nullptr;
    providers::ProviderRegistry* registry = nullptr;   // the tag suggesters are registered here
    // The LLM session of one run / the embedder of a provider id (the app owns the cloud ledger, secrets, http).
    std::function<library::TagSuggestionService::LlmSession(const QString&, QObject*)> tagLlm;
    std::function<providers::ITextEmbedder*(const QString&, QObject*)> embedder;

    // ---- P12d: participants, people, voice analysis, shared undo ----
    library::PeopleStore*     people = nullptr;
    library::VoiceprintStore* voiceprints = nullptr;
    library::PeopleStats*     peopleStats = nullptr;
    QString modelsDir;          // AppPaths::modelsDir()
    QString appDir;             // location of the packaged models
    // The transcript-editor sessions (editing::EditorSessions, P12f): created on demand / only an open one.
    std::function<editing::SpeakerEditor*(const QString&)> editorFor;
    std::function<editing::SpeakerEditor*(const QString&)> existingEditor;
    // The transcript's identity (the identification drops a result that belongs to an older transcript).
    std::function<QString(const QString& meetingId, const QString& folder)> transcriptStamp;
    // The tracks' speech check is still pending (running, or not yet done for a meeting that needs it).
    std::function<bool(const Meeting&)> speechCheckPending;

    // ---- P12e: summary and topic queue ----
    processing::SummaryLlmPort* summaryLlm = nullptr;   // settings, LLM key, Tanara Cloud runs (the app)
    library::JobStats*          jobStats = nullptr;     // speed of earlier runs (estimates)
    QString notesDir;                                   // AppPaths::notesDir() (the summary's copy)
    // The in-memory transcript of a fresh transcription (empty: read from disk).
    std::function<MergedTranscript(const QString& meetingId)> cachedTranscript;

    // ---- P12f: transcription, workflows, editor sessions ----
    processing::TranscriptionPort* transcriptionPort = nullptr;   // settings, STT key, Tanara Cloud runs (the app)
    // Whether a step can run on a meeting (provider, key, Tanara Cloud blockers; the app's canRun).
    std::function<providers::ReadinessResult(providers::WorkflowStep, const QString&)> readiness;
};

class Services {
public:
    explicit Services(const ServicesDeps& deps);
    ~Services();
    Services(const Services&) = delete;
    Services& operator=(const Services&) = delete;

    library::ArchiveService&     archive() const     { return *m_archive; }
    library::ImportService&      import() const      { return *m_import; }
    watcher::CallMonitor&        callMonitor() const { return *m_callMonitor; }
    recording::RecordingService& recording() const   { return *m_recording; }
    recording::MixdownService&   mixdown() const     { return *m_mixdown; }
    library::TagSuggestionService& tagSuggestions() const { return *m_tagSuggestions; }   // P12c
    // --- P12d ---
    processing::VoiceAnalysisService& voiceAnalysis() const { return *m_voiceAnalysis; }
    library::PeopleService&           people() const        { return *m_people; }
    editing::ParticipantService&      participants() const  { return *m_participants; }
    editing::MeetingUndoRegistry&     meetingUndo() const   { return *m_meetingUndo; }
    // --- end P12d ---
    // --- P16: detector registry ---
    // This platform's meeting detectors (an instance, not a singleton; the call monitor uses it).
    watcher::MeetingDetectorRegistry& detectors() const { return *m_detectors; }
    // --- end P16 ---
    // --- P12e: summary and topic queue ---
    // The quick summary, and (topics()) the complex summary's topic flow.
    processing::SummarizationService& summarization() const { return *m_summarization; }
    // --- end P12e ---
    // --- P12f: transcription, workflows, editor sessions ---
    processing::TranscriptionService& transcription() const { return *m_transcription; }   // the STT step
    // The transcribe -> mixdown -> STT -> identify chain, the post-recording / import chains, cancel per kind.
    app::Workflows&                   workflows() const     { return *m_workflows; }
    editing::EditorSessions&          editorSessions() const { return *m_editorSessions; }   // one editor per meeting
    // --- end P12f ---

private:
    std::unique_ptr<library::ArchiveService>     m_archive;
    std::unique_ptr<library::ImportService>      m_import;
    std::unique_ptr<watcher::CallMonitor>        m_callMonitor;   // before the recording (it subscribes)
    std::unique_ptr<recording::RecordingService> m_recording;
    std::unique_ptr<recording::MixdownService>   m_mixdown;
    std::unique_ptr<library::TagSuggestionService> m_tagSuggestions;   // P12c
    // --- P12d --- (declaration order = construction order; destroyed in reverse)
    std::unique_ptr<processing::VoiceAnalysisService> m_voiceAnalysis;
    std::unique_ptr<editing::PeopleEditingAdapter>    m_peopleEditing;   // the library ports' editing side
    std::unique_ptr<library::PeopleService>           m_people;
    std::unique_ptr<editing::MeetingUndoRegistry>     m_meetingUndo;
    std::unique_ptr<editing::ParticipantAnalysisPort> m_participantAnalysis;   // over the voice analysis
    std::unique_ptr<editing::ParticipantService>      m_participants;
    // --- end P12d ---
    // --- P16: detector registry ---
    std::unique_ptr<watcher::MeetingDetectorRegistry> m_detectors;
    // --- end P16 ---
    // --- P12e: summary and topic queue ---
    std::unique_ptr<processing::SummarizationService> m_summarization;
    // --- end P12e ---
    // --- P12f: transcription, workflows, editor sessions --- (the editors are destroyed before the services)
    std::unique_ptr<processing::TranscriptionService> m_transcription;
    std::unique_ptr<app::Workflows>                   m_workflows;
    std::unique_ptr<editing::EditorSessions>          m_editorSessions;
    // --- end P12f ---
};

} // namespace tanara::app
