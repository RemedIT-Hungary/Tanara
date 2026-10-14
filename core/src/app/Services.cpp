#include "tanara/app/Services.h"

#include "tanara/app/SettingsManager.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/library/import/ImportService.h"
#include "tanara/library/store/ArchiveService.h"
#include "tanara/library/tags/SimilarityTagSuggestionProvider.h"
#include "tanara/processing/ProcessingProviders.h"
#include "tanara/providers/core/ProviderRegistry.h"
#include "tanara/recording/MixdownService.h"
#include "tanara/recording/RecordingService.h"
#include "tanara/watcher/CallMonitor.h"
// --- P12d ---
#include "tanara/editing/participants/ParticipantService.h"
#include "tanara/editing/people/PeopleEditingAdapter.h"
#include "tanara/editing/session/SpeakerEditor.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/editing/undo/MeetingUndoRegistry.h"
#include "tanara/library/people/PeopleService.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/processing/analysis/ParticipantAnalysis.h"
#include "tanara/processing/analysis/TrackActivity.h"
#include "tanara/processing/analysis/VoiceAnalysisService.h"
// --- end P12d ---
// --- P16: detector registry ---
#include "tanara/watcher/detect/DetectorRegistry.h"
// --- end P16 ---
// --- P12e: summary and topic queue ---
#include "tanara/processing/summary/SummarizationService.h"
// --- end P12e ---
// --- P12f: transcription, workflows, editor sessions ---
#include "tanara/app/Workflows.h"
#include "tanara/editing/session/EditorSessions.h"
#include "tanara/library/tags/TagService.h"
#include "tanara/processing/stt/TranscriptionService.h"
// --- end P12f ---

namespace tanara::app {

namespace {

recording::RecordingService::Deps recordingDeps(const ServicesDeps& deps, watcher::CallMonitor* calls)
{
    recording::RecordingService::Deps rd;
    rd.store = deps.store;
    rd.tags = deps.tags;
    rd.media = deps.media;
    rd.callMonitor = calls;
    if (SettingsManager* st = deps.settings) {
        const paths::AppPaths& ap = st->paths();
        rd.audioDir = ap.primaryRecordingRoot();
        rd.statePath = ap.stateFile(paths::files::State);
        rd.config = [st]() {
            const AppSettings s = st->settings();
            return recording::RecordingService::Config{s.recording, s.watcher};
        };
    }
    return rd;
}

// P12c: the tag suggesters join the controller's registry (the local adapters), and the service reads its
// live configuration from the settings.
library::TagSuggestionService::Deps tagSuggestionDeps(const ServicesDeps& deps)
{
    library::TagSuggestionService::Deps td;
    td.store = deps.store;
    td.tags = deps.tags;
    td.profiles = deps.profiles;
    td.embeddings = deps.embeddings;
    td.embeddingPreparer = deps.embeddingPreparer;
    td.llm = deps.tagLlm;
    td.embedder = deps.embedder;
    if (providers::ProviderRegistry* reg = deps.registry) {
        library::registerInto(*reg, deps.tags, deps.profiles, deps.embeddings);
        processing::registerTagSuggester(*reg, deps.tags);
    }
    SettingsManager* st = deps.settings;
    providers::ProviderRegistry* reg = deps.registry;
    td.config = [st, reg]() {
        library::TagSuggestionService::Config c;
        if (!st) return c;
        const AppSettings s = st->settings();
        c.tagSuggestions = s.processing.tagSuggestions;
        c.llmTagSuggestions = s.processing.llmTagSuggestions;
        const QString id = reg && reg->has(providers::CapabilityKind::TextEmbedding, s.providers.embedding.selectedId)
            ? s.providers.embedding.selectedId : QString();
        const providers::ProviderConfig base = s.providers.embedding.selected();
        // The Tanara Cloud variant has no model field: the gateway's default model.
        c.embeddingId = id;
        c.embeddingBaseUrl = base.baseUrl;
        c.embeddingModel = id.isEmpty() ? QString()
            : (!base.model.trimmed().isEmpty() ? base.model.trimmed()
                                               : (id == providers::embeddingproviders::CloudId ? QStringLiteral("default")
                                                                                              : QString()));
        return c;
    };
    return td;
}

// --- P12d ---
// The participant operations' voice-analysis side (editing::ParticipantAnalysisPort) over the processing
// module: the candidate context of the voice analysis, the participant-analysis file's clusters and the track
// activity cache. Lives here because editing must not depend on processing (cleanup plan T 1.2).
class ParticipantAnalysisAdapter final : public editing::ParticipantAnalysisPort {
public:
    explicit ParticipantAnalysisAdapter(processing::VoiceAnalysisService* voice) : m_voice(voice) {}

    QVector<Evidence> evidence(const Participant& p, bool heard, double voiceScore, const Meeting& m) override
    {
        return processing::participants::participantEvidence(p, heard, voiceScore, m, m_voice->candidateContext());
    }
    ParticipantGroup group(const Participant& p) override { return processing::participants::participantGroup(p); }
    QStringList analysisModelIds(const QString& meetingFolder) override
    {
        return processing::ParticipantAnalysisFile::load(meetingFolder).modelIds;
    }
    bool bindRawSpeakers(QVector<Participant>& participants, const QString& meetingFolder,
                         const QVector<TranscriptLine>& lines) override
    {
        const processing::ParticipantAnalysisFile file = processing::ParticipantAnalysisFile::load(meetingFolder);
        if (file.isEmpty()) return false;
        return processing::participants::bindRawSpeakers(participants, file.clusters, lines);
    }
    QVector<SpeakerBinding> approvalBindings(const QVector<Participant>& all, const QStringList& acceptedIds,
                                             const QVector<TranscriptLine>& lines, const Meeting& m,
                                             bool trackVeto) override
    {
        const processing::ParticipantAnalysisFile file = processing::ParticipantAnalysisFile::load(m.folder);
        const MeetingActivity act =
            MeetingActivity::load(m.folder, processing::trackactivity::activityFingerprint(m, m.folder));
        return processing::participants::approvalBindings(all, acceptedIds, lines, file.clusters,
                                                          act.isEmpty() ? nullptr : &act, trackVeto ? &m : nullptr);
    }
    bool analysisRunning(const QString& meetingId) const override { return m_voice->analysisRunning(meetingId); }
    void cancelAnalysis(const QString& meetingId) override { m_voice->cancelAnalysis(meetingId); }

private:
    processing::VoiceAnalysisService* m_voice;
};

processing::VoiceAnalysisService::Deps voiceDeps(const ServicesDeps& deps)
{
    processing::VoiceAnalysisService::Deps vd;
    vd.store = deps.store;
    vd.voiceprints = deps.voiceprints;
    vd.people = deps.people;
    vd.tags = deps.tags;
    vd.jobs = deps.jobs;
    vd.media = deps.media;
    vd.modelsDir = deps.modelsDir;
    vd.appDir = deps.appDir;
    SettingsManager* st = deps.settings;
    library::PeopleStore* people = deps.people;
    vd.enabledModels = [st]() { return st ? st->enabledVoiceModels() : QStringList(); };
    vd.self = [st, people]() {
        SelfPerson self;
        if (people) { people->refresh(); self.id = people->selfId(); }
        if (st) self.name = st->settings().recording.userSpeakerName.trimmed();
        return self;
    };
    vd.transcriptLines = [](const QString& folder) { return editing::speakeredit::loadTranscriptLines(folder); };
    vd.transcriptStamp = deps.transcriptStamp;
    vd.speechCheckPending = deps.speechCheckPending;
    return vd;
}

editing::PeopleEditingAdapter::Deps peopleEditingDeps(const ServicesDeps& deps,
                                                      processing::VoiceAnalysisService* voice)
{
    editing::PeopleEditingAdapter::Deps pd;
    pd.store = deps.store;
    pd.people = deps.people;
    pd.voiceprints = deps.voiceprints;
    pd.embedderFactory = [voice]() { return voice->utteranceEmbedderFactory(); };
    pd.editorModelIds = [voice]() { return voice->editorModelIds(); };
    return pd;
}

library::PeopleService::Deps peopleDeps(const ServicesDeps& deps, editing::PeopleEditingAdapter* editing)
{
    library::PeopleService::Deps pd;
    pd.store = deps.store;
    pd.people = deps.people;
    pd.voiceprints = deps.voiceprints;
    pd.stats = deps.peopleStats;
    pd.tags = deps.tags;
    pd.references = editing;
    pd.material = editing;
    SettingsManager* st = deps.settings;
    pd.selfName = [st]() { return st ? st->settings().recording.userSpeakerName : QString(); };
    pd.setSelfName = [st](const QString& name) {
        if (!st) return;
        AppSettings s = st->settings();
        s.recording.userSpeakerName = name;
        st->setSettings(s);
    };
    return pd;
}
// --- end P12d ---

// --- P12e: summary and topic queue ---
// The speaker names of the summary come from the people service (the editing module's overlay resolves them).
processing::SummarizationService::Deps summarizationDeps(const ServicesDeps& deps, library::PeopleService* people)
{
    processing::SummarizationService::Deps sd;
    sd.store = deps.store;
    sd.jobs = deps.jobs;
    sd.jobStats = deps.jobStats;
    sd.registry = deps.registry;
    sd.llm = deps.summaryLlm;
    sd.notesDir = deps.notesDir;
    sd.cachedTranscript = deps.cachedTranscript;
    sd.resolveSpeakers = [people](MergedTranscript& merged, const Meeting& m) {
        editing::speakeredit::applyResolvedSpeakers(merged, m, people->personNames());
    };
    sd.sourceLines = [people](const Meeting& m) {
        return editing::speakeredit::resolvedSourceLines(m, people->personNames());
    };
    return sd;
}
// --- end P12e ---

// --- P12f: transcription, workflows, editor sessions ---
processing::TranscriptionService::Deps transcriptionDeps(const ServicesDeps& deps)
{
    return {deps.store, deps.jobs, deps.jobStats, deps.registry, deps.transcriptionPort};
}

// The editor sessions: the voice side of a new editor comes from the voice analysis (processing), which
// the editing module must not include.
editing::EditorSessions::Deps editorSessionDeps(const ServicesDeps& deps, processing::VoiceAnalysisService* voice,
                                                library::PeopleService* people, editing::MeetingUndoRegistry* undo)
{
    editing::EditorSessions::Deps ed;
    ed.store = deps.store;
    ed.people = deps.people;
    ed.voiceprints = deps.voiceprints;
    ed.undo = undo;
    ed.self = [people]() { return people->selfPerson(); };
    ed.personNames = [people]() { return people->personNames(); };
    ed.configure = [voice, media = deps.media, tags = deps.tags](editing::SpeakerEditor* editor) {
        editor->setVoiceModelIds(voice->editorModelIds());
        editor->setEmbedderFactory(voice->utteranceEmbedderFactory());
        // The track activity of the side check: cache, else decoded via the media port and saved.
        editor->setActivityProvider([media](const Meeting& mm) {
            return processing::loadOrComputeSideActivity(mm, *media);
        });
        // The tag evidence for the candidate ranking (manual tag + learned association).
        editor->setTagEvidenceProvider(voice->candidateContext().tagEvidence);
        // Manual tags for the quick "+ #Tag" offer (the test seam, otherwise the TagService).
        const auto personTags = voice->personTagsProvider();
        editor->setPersonTagsProvider(personTags ? personTags : [tags](const PersonId& p) {
            return tags ? tags->tagsOfPerson(p) : QStringList();
        });
    };
    return ed;
}

Workflows::Deps workflowDeps(const ServicesDeps& deps, const Services& s)
{
    Workflows::Deps wd;
    wd.store = deps.store;
    wd.jobs = deps.jobs;
    wd.mixdown = &s.mixdown();
    wd.transcription = &s.transcription();
    processing::VoiceAnalysisService* voice = &s.voiceAnalysis();
    editing::ParticipantService* participants = &s.participants();
    wd.voice.usable = [voice]() { return voice->usable(); };
    wd.voice.startIdentify = [voice](const QString& id, bool asStage) { return voice->startIdentify(id, asStage); };
    wd.voice.cancelIdentify = [voice](const QString& id, JobKind kind) { return voice->cancel(id, kind); };
    wd.voice.applyParticipants = [participants](const QString& id) {
        return participants->applyParticipantsToTranscript(id);
    };
    wd.voice.analyzeParticipants = [voice](const QString& id) { return voice->maybeAnalyzeParticipants(id); };
    wd.voice.measureActivity = [voice](const Meeting& m, std::function<void(const MeetingActivity&)> done) {
        voice->measureActivity(m, std::move(done));
    };
    wd.readiness = deps.readiness;
    library::PeopleService* people = &s.people();
    wd.personNames = [people]() { return people->personNames(); };
    SettingsManager* st = deps.settings;
    wd.mixdownMode = [st]() { return st ? st->settings().recording.mixdownMode : QString(); };
    wd.undo = &s.meetingUndo();
    wd.summarization = &s.summarization();
    wd.tagSuggestions = &s.tagSuggestions();
    wd.import = &s.import();
    wd.archive = &s.archive();
    return wd;
}
// --- end P12f ---

} // namespace

Services::Services(const ServicesDeps& deps)
    : m_archive(std::make_unique<library::ArchiveService>(deps.store, deps.tags, deps.jobs))
    , m_import(std::make_unique<library::ImportService>(deps.store, deps.media, deps.jobs))
    , m_callMonitor(std::make_unique<watcher::CallMonitor>())
    , m_recording(std::make_unique<recording::RecordingService>(recordingDeps(deps, m_callMonitor.get())))
    , m_mixdown(std::make_unique<recording::MixdownService>(deps.store, deps.jobs, deps.media))
    , m_tagSuggestions(std::make_unique<library::TagSuggestionService>(tagSuggestionDeps(deps)))
    // --- P12d ---
    , m_voiceAnalysis(std::make_unique<processing::VoiceAnalysisService>(voiceDeps(deps)))
    , m_peopleEditing(std::make_unique<editing::PeopleEditingAdapter>(peopleEditingDeps(deps, m_voiceAnalysis.get())))
    , m_people(std::make_unique<library::PeopleService>(peopleDeps(deps, m_peopleEditing.get())))
    , m_meetingUndo(std::make_unique<editing::MeetingUndoRegistry>(deps.store, deps.tags, deps.existingEditor))
    , m_participantAnalysis(std::make_unique<ParticipantAnalysisAdapter>(m_voiceAnalysis.get()))
    , m_participants(std::make_unique<editing::ParticipantService>(editing::ParticipantService::Deps{
          deps.store, deps.people, m_people.get(), m_meetingUndo.get(), m_participantAnalysis.get(),
          deps.editorFor, deps.existingEditor}))
    // --- end P12d ---
    // --- P16: detector registry ---
    , m_detectors(std::make_unique<watcher::MeetingDetectorRegistry>(watcher::MeetingDetectorRegistry::withBuiltins()))
    // --- end P16 ---
    // --- P12e: summary and topic queue ---
    , m_summarization(std::make_unique<processing::SummarizationService>(summarizationDeps(deps, m_people.get())))
    // --- end P12e ---
    // --- P12f: transcription, workflows, editor sessions ---
    , m_transcription(std::make_unique<processing::TranscriptionService>(transcriptionDeps(deps)))
    , m_workflows(std::make_unique<Workflows>(workflowDeps(deps, *this)))
    , m_editorSessions(std::make_unique<editing::EditorSessions>(
          editorSessionDeps(deps, m_voiceAnalysis.get(), m_people.get(), m_meetingUndo.get())))
    // --- end P12f ---
{
    // --- P16: detector registry ---
    m_callMonitor->setDetectorRegistry(m_detectors.get());
    // --- end P16 ---
    // --- P12f: transcription, workflows, editor sessions ---
    // The asynchronous identification's matches are written by the participant service.
    editing::ParticipantService* participants = m_participants.get();
    QObject::connect(m_voiceAnalysis.get(), &processing::VoiceAnalysisService::speakersIdentified, participants,
                     [participants](const QString& meetingId, const QVector<SpeakerVoiceMatch>& matches) {
                         participants->commitIdentification(meetingId, matches);
                     });
    // --- end P12f ---
}

Services::~Services()
{
    // --- P16: detector registry ---
    // The registry is the last member, so it is destroyed first: unhook it from the call monitor.
    m_callMonitor->setDetectorRegistry(nullptr);
    // --- end P16 ---
}

} // namespace tanara::app
