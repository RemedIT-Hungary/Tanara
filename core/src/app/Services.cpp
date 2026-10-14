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
// --- P16: detector registry ---
#include "tanara/watcher/detect/DetectorRegistry.h"
// --- end P16 ---

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

} // namespace

Services::Services(const ServicesDeps& deps)
    : m_archive(std::make_unique<library::ArchiveService>(deps.store, deps.tags, deps.jobs))
    , m_import(std::make_unique<library::ImportService>(deps.store, deps.media, deps.jobs))
    , m_callMonitor(std::make_unique<watcher::CallMonitor>())
    , m_recording(std::make_unique<recording::RecordingService>(recordingDeps(deps, m_callMonitor.get())))
    , m_mixdown(std::make_unique<recording::MixdownService>(deps.store, deps.jobs, deps.media))
    , m_tagSuggestions(std::make_unique<library::TagSuggestionService>(tagSuggestionDeps(deps)))
    // --- P16: detector registry ---
    , m_detectors(std::make_unique<watcher::MeetingDetectorRegistry>(watcher::MeetingDetectorRegistry::withBuiltins()))
    // --- end P16 ---
{
    // --- P16: detector registry ---
    m_callMonitor->setDetectorRegistry(m_detectors.get());
    // --- end P16 ---
}

Services::~Services()
{
    // --- P16: detector registry ---
    // The registry is the last member, so it is destroyed first: unhook it from the call monitor.
    m_callMonitor->setDetectorRegistry(nullptr);
    // --- end P16 ---
}

} // namespace tanara::app
