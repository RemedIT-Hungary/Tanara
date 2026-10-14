#include "tanara/processing/ProcessingProviders.h"
#include "tanara/processing/analysis/LlmTagSuggestionProvider.h"
#include "tanara/processing/analysis/LocalParticipantIdentifier.h"
#include "tanara/processing/summary/LlmSummarizer.h"
#include "tanara/processing/voiceid/LocalVoiceModelCatalog.h"
#include "tanara/processing/voiceid/OnnxVoiceEmbedder.h"
#include "tanara/providers/catalog/IModelCatalog.h"
#include "tanara/providers/chat/IChatProvider.h"
#include "tanara/providers/summary/ISummarizer.h"
#include "tanara/providers/tags/ITagSuggester.h"
#include "tanara/providers/voice/IParticipantIdentifier.h"
#include "tanara/providers/voice/IVoiceEmbedder.h"

#include <QCoreApplication>

namespace tanara::processing {

using namespace providers;

namespace {

ProviderDescriptor localDescriptor(const QString& id, const QString& displayName, InputKind input)
{
    ProviderDescriptor d;
    d.id = id;
    d.displayName = displayName;
    d.origin = Origin::Local;
    d.traits.dataLeavesDevice = false;
    d.traits.metered = false;
    d.traits.worksOffline = true;
    d.traits.inputs = { input };
    d.authMode = AuthMode::None;
    d.networkRequired = false;
    return d;
}

// The chat provider of an LLM-based provider: from the registry, by the config's type.
IChatProvider* chatFor(const ProviderRegistry& registry, const ProviderContext& ctx, const ProviderConfig& cfg,
                       QObject* parent)
{
    const QString id = cfg.type.isEmpty() ? DefaultChatProviderId : cfg.type;
    return registry.create<IChatProvider>(id, ctx, cfg, parent);
}

} // namespace

void registerInto(ProviderRegistry& registry, ProcessingProviderDeps deps)
{
    // Voice embedding: the ONNX models (needs the model files).
    ProviderDescriptor voice = localDescriptor(OnnxVoiceEmbedder::Id,
        QCoreApplication::translate("BuiltinProviders", "Helyi hangmodellek"), InputKind::AudioClip);
    voice.traits.inputs = { InputKind::AudioClip, InputKind::Pcm };
    voice.traits.needsModelFile = true;
    voice.catalogRef = LocalVoiceModelCatalog::Id;
    const auto voiceSet = deps.voiceSet;
    const auto media = deps.media;
    registry.add<IVoiceEmbedder>(voice, [voiceSet, media](const ProviderContext&, const ProviderConfig&,
                                                          QObject* parent) -> IVoiceEmbedder* {
        return new OnnxVoiceEmbedder(voiceSet ? voiceSet() : VoiceEmbedderSet(), media ? media() : media::MediaToolPtr(),
                                     parent);
    });

    registry.add<IParticipantIdentifier>(
        localDescriptor(LocalParticipantIdentifier::Id,
                        QCoreApplication::translate("BuiltinProviders", "Helyi hang-azonosítás"), InputKind::Text),
        [](const ProviderContext&, const ProviderConfig&, QObject* parent) -> IParticipantIdentifier* {
            return new LocalParticipantIdentifier(parent);
        });

    // The LLM-based ones: the pipeline runs here; where the text goes is the chat provider's business
    // (its own descriptor tells: a local server or the Tanara Cloud).
    ProviderRegistry* reg = &registry;
    ProviderDescriptor summary = localDescriptor(LlmSummarizer::Id,
        QCoreApplication::translate("BuiltinProviders", "Nyelvi modell"), InputKind::Text);
    registry.add<ISummarizer>(summary, [reg](const ProviderContext& ctx, const ProviderConfig& cfg,
                                             QObject* parent) -> ISummarizer* {
        auto* s = new LlmSummarizer(nullptr, cfg, parent);
        IChatProvider* chat = chatFor(*reg, ctx, cfg, s);   // the summarizer's child
        if (!chat) { delete s; return nullptr; }
        s->setChatProvider(chat);
        return s;
    });
    if (deps.tags) {
        library::TagService* tags = deps.tags;
        registry.add<ITagSuggester>(
            localDescriptor(LlmTagSuggestionProvider::Id,
                            QCoreApplication::translate("BuiltinProviders", "Nyelvi modell"), InputKind::Text),
            [reg, tags](const ProviderContext& ctx, const ProviderConfig& cfg, QObject* parent) -> ITagSuggester* {
                auto* t = new LlmTagSuggestionProvider(tags, nullptr, cfg, parent);
                IChatProvider* chat = chatFor(*reg, ctx, cfg, t);   // the suggester's child
                if (!chat) { delete t; return nullptr; }
                t->setChatProvider(chat);
                return t;
            });
    }

    ProviderDescriptor catalog = localDescriptor(LocalVoiceModelCatalog::Id,
        QCoreApplication::translate("BuiltinProviders", "Helyi hangmodellek"), InputKind::Text);
    catalog.traits.inputs.clear();
    catalog.traits.worksOffline = false;   // a download needs the network (listing does not)
    const QString modelsDir = deps.modelsDir, appDir = deps.appDir;
    registry.add<IModelCatalog>(catalog, [modelsDir, appDir](const ProviderContext& ctx, const ProviderConfig&,
                                                             QObject* parent) -> IModelCatalog* {
        return new LocalVoiceModelCatalog(modelsDir, appDir, {}, ctx.http, parent);
    });
}

} // namespace tanara::processing
