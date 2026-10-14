#pragma once
//
// The processing module's local capability providers and their registration:
//
//   capability                 id                    provider
//   VoiceEmbedding             onnx                  OnnxVoiceEmbedder         (catalogRef: local-voice-models)
//   ParticipantIdentification  local                 LocalParticipantIdentifier
//   Summarization              llm                   LlmSummarizer             (over the chat provider cfg.type)
//   TagSuggestion              llm                   LlmTagSuggestionProvider  (over the chat provider cfg.type)
//   ModelCatalog               local-voice-models    LocalVoiceModelCatalog
//
// The LLM-based providers take their chat provider from the SAME registry, by the config's type
// (ProviderConfig::type: "openai-compat", or "tanara-cloud" = the Tanara Cloud LLM proxy), created
// with the same context: so one config decides where the text goes.
#include "tanara/media/MediaTool.h"
#include "tanara/processing/voiceid/VoiceEmbedderSet.h"
#include "tanara/providers/core/ProviderRegistry.h"

#include <QString>

#include <functional>

namespace tanara::library { class TagService; }

namespace tanara::processing {

struct ProcessingProviderDeps {
    // The current voice-model set and media tool (functions: Settings may replace them at run time).
    std::function<VoiceEmbedderSet()> voiceSet;
    std::function<media::MediaToolPtr()> media;
    QString modelsDir;
    QString appDir;
    library::TagService* tags = nullptr;   // not owned; null = the LLM tag suggester is not registered
};

// The default chat provider id when the config gives none.
inline const QString DefaultChatProviderId = QStringLiteral("openai-compat");

void registerInto(providers::ProviderRegistry& registry, ProcessingProviderDeps deps);

// Only the LLM tag suggester (TagSuggestion / "llm"); registerInto calls it when deps.tags is set.
void registerTagSuggester(providers::ProviderRegistry& registry, library::TagService* tags);

} // namespace tanara::processing
