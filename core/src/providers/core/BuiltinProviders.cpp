#include "tanara/providers/core/ProviderRegistry.h"
#include "tanara/providers/core/ProviderConfig.h"

// The concrete headers of the built-in providers appear ONLY here, so the registry itself stays
// provider-agnostic and the dependency is concentrated in one place.
#include "tanara/providers/stt/SonioxProvider.h"
#include "tanara/providers/stt/WhisperCompatProvider.h"
#include "tanara/providers/chat/OpenAiCompatibleProvider.h"
#include "tanara/providers/chat/LlmServer.h"
#include "tanara/providers/embedding/OpenAiCompatibleEmbeddingProvider.h"

#include <QCoreApplication>
#include <QObject>

namespace tanara::providers {

namespace {

// Origin (see ProviderDescriptor.h): Cloud = ONLY the Tanara Cloud; every service reached with the
// user's own key / address (Soniox BYO, OpenAI, LM Studio, Ollama, a Whisper server) = CustomEndpoint;
// Local = runs inside Tanara on this machine. dataLeavesDevice reflects the DEFAULT address.
Traits remoteTraits(InputKind input, bool metered)
{
    Traits t;
    t.dataLeavesDevice = true;
    t.metered = metered;
    t.worksOffline = false;
    t.inputs = {input};
    return t;
}

Traits localServerTraits(InputKind input)
{
    Traits t;
    t.dataLeavesDevice = false;   // the default address is localhost
    t.metered = false;
    t.worksOffline = true;
    t.inputs = {input};
    return t;
}

// --- Soniox STT descriptor ---
ProviderDescriptor sonioxDescriptor()
{
    ProviderDescriptor d;
    d.id                  = QStringLiteral("soniox");
    d.displayName         = QStringLiteral("Soniox");
    d.capability          = CapabilityKind::Transcription;
    d.origin              = Origin::CustomEndpoint;   // BYO key
    d.traits              = remoteTraits(InputKind::AudioFile, /*metered*/ true);
    d.authMode            = AuthMode::ApiKey;
    d.supportsDiarization = true;
    d.networkRequired     = true;
    d.languages           = { QStringLiteral("hu"), QStringLiteral("en") };

    ConfigField baseUrl;
    baseUrl.key          = QStringLiteral("baseUrl");
    baseUrl.label        = QCoreApplication::translate("BuiltinProviders", "Cím (URL)");
    baseUrl.type         = ConfigFieldType::Url;
    baseUrl.defaultValue = QStringLiteral("https://api.soniox.com/v1");

    ConfigField model;
    model.key          = QStringLiteral("model");
    model.label        = QCoreApplication::translate("BuiltinProviders", "Modell");
    model.type         = ConfigFieldType::Text;
    model.defaultValue = QStringLiteral("stt-async-v5");

    ConfigField apiKey;
    apiKey.key       = QStringLiteral("apiKey");
    apiKey.label     = QCoreApplication::translate("BuiltinProviders", "API-kulcs");
    apiKey.type      = ConfigFieldType::Secret;
    apiKey.required  = true;
    apiKey.isSecret  = true;
    apiKey.secretKey = QStringLiteral("soniox.apiKey");

    d.fields = { baseUrl, model, apiKey };
    // GET /models: a key-protected, free list endpoint (does not start a transcription).
    d.probe = { QStringLiteral("/models"), true };
    return d;
}

// --- OpenAI-compatible LLM descriptor ---
ProviderDescriptor openAiCompatDescriptor()
{
    ProviderDescriptor d;
    d.id                = QStringLiteral("openai-compat");
    d.displayName       = QCoreApplication::translate("BuiltinProviders", "OpenAI-kompatibilis végpont");
    d.capability        = CapabilityKind::Chat;
    d.origin            = Origin::CustomEndpoint;     // LM Studio / Ollama / OpenAI
    d.traits            = localServerTraits(InputKind::Text);
    d.authMode          = AuthMode::ApiKey;
    d.supportsStreaming = false;

    ConfigField baseUrl;
    baseUrl.key          = QStringLiteral("baseUrl");
    baseUrl.label        = QCoreApplication::translate("BuiltinProviders", "Cím (URL)");
    baseUrl.type         = ConfigFieldType::Url;
    baseUrl.required     = true;
    baseUrl.defaultValue = QStringLiteral("http://localhost:1234/v1");

    ConfigField model;
    model.key            = QStringLiteral("model");
    model.label          = QCoreApplication::translate("BuiltinProviders", "Modell");
    model.type           = ConfigFieldType::Combo;
    model.dynamicOptions = true;
    model.defaultValue   = QStringLiteral("google/gemma-4-12b"); // an empty config must not mean an empty field

    ConfigField temperature;
    temperature.key          = QStringLiteral("temperature");
    temperature.label        = QCoreApplication::translate("BuiltinProviders", "Hőmérséklet");
    temperature.type         = ConfigFieldType::Number;
    temperature.defaultValue = QStringLiteral("0.2");
    temperature.advanced     = true;
    temperature.minValue     = 0.0;
    temperature.maxValue     = 1.0;   // above this makes no sense for summarization

    ConfigField maxTokens;
    maxTokens.key          = QStringLiteral("maxTokens");
    maxTokens.label        = QCoreApplication::translate("BuiltinProviders", "Max. tokenek");
    maxTokens.type         = ConfigFieldType::Number;
    maxTokens.defaultValue = QStringLiteral("8000");
    maxTokens.advanced     = true;
    maxTokens.minValue     = 256;
    maxTokens.maxValue     = 1000000;

    ConfigField apiKey;
    apiKey.key       = QStringLiteral("apiKey");
    apiKey.label     = QCoreApplication::translate("BuiltinProviders", "API-kulcs");
    apiKey.type      = ConfigFieldType::Secret;
    apiKey.required  = false;
    apiKey.isSecret  = true;
    apiKey.secretKey = QStringLiteral("llm.apiKey");

    d.fields = { baseUrl, model, temperature, maxTokens, apiKey };
    d.probe = { QStringLiteral("/models"), true };   // the same as the model fetch uses
    return d;
}

ProviderDescriptor whisperCompatDescriptor()
{
    ProviderDescriptor d;
    d.id          = QStringLiteral("whisper-compat");
    d.displayName = QCoreApplication::translate("BuiltinProviders",
                                                "Whisper (OpenAI-kompatibilis)");
    d.capability = CapabilityKind::Transcription;
    d.origin     = Origin::CustomEndpoint;            // a local whisper server or the OpenAI API
    d.traits     = localServerTraits(InputKind::AudioFile);
    d.authMode   = AuthMode::ApiKey;
    d.supportsDiarization = false;   // every token is "Speaker 1"; the name comes from voice ID

    ConfigField baseUrl;
    baseUrl.key          = QStringLiteral("baseUrl");
    baseUrl.label        = QCoreApplication::translate("BuiltinProviders", "Cím (URL)");
    baseUrl.type         = ConfigFieldType::Url;
    baseUrl.required     = true;
    baseUrl.defaultValue = QStringLiteral("http://localhost:8000/v1");
    baseUrl.help         = QCoreApplication::translate("BuiltinProviders",
        "Lokális whisper-szerver (faster-whisper-server, speaches) vagy az OpenAI API "
        "(https://api.openai.com/v1 — ott 25 MB a fájllimit).");

    ConfigField model;
    model.key          = QStringLiteral("model");
    model.label        = QCoreApplication::translate("BuiltinProviders", "Modell");
    model.type         = ConfigFieldType::Text;
    model.defaultValue = QStringLiteral("whisper-1");
    model.help         = QCoreApplication::translate("BuiltinProviders",
        "Lokális szervernél a betöltött modell neve (pl. Systran/faster-whisper-large-v3), "
        "az OpenAI API-nál whisper-1.");

    ConfigField language;
    language.key          = QStringLiteral("language");
    language.label        = QCoreApplication::translate("BuiltinProviders", "Nyelv");
    language.type         = ConfigFieldType::Text;
    language.defaultValue = QString();
    language.help         = QCoreApplication::translate("BuiltinProviders",
        "ISO-639-1 nyelvkód (pl. en, de). Üresen hagyva a szerver automatikusan felismeri "
        "a nyelvet — vegyes/nem-magyar felvételekhez ezt hagyd üresen.");

    ConfigField apiKey;
    apiKey.key       = QStringLiteral("apiKey");
    apiKey.label     = QCoreApplication::translate("BuiltinProviders", "API-kulcs");
    apiKey.type      = ConfigFieldType::Secret;
    apiKey.required  = false;   // a local server does not need it
    apiKey.isSecret  = true;
    apiKey.secretKey = QStringLiteral("stt.whisper.apiKey");

    d.fields = {baseUrl, model, language, apiKey};
    d.probe = { QStringLiteral("/models"), true };   // both the OpenAI API and local whisper servers provide it
    return d;
}

// --- Tanara Cloud (STT and LLM with the same id, separately in the two registries) ---
ProviderDescriptor tanaraCloudDescriptor(CapabilityKind kind)
{
    ProviderDescriptor d;
    d.id                  = QStringLiteral("tanara-cloud");
    d.displayName         = QCoreApplication::translate("BuiltinProviders", "Tanara Cloud — bejelentkezés");
    d.capability          = kind;
    d.origin              = Origin::Cloud;
    d.traits              = remoteTraits(kind == CapabilityKind::Transcription ? InputKind::AudioFile
                                                                               : InputKind::Text,
                                         /*metered*/ true);
    d.authMode            = AuthMode::Login;
    d.loginSecretKey      = QStringLiteral("tanara.cloud.apiKey");
    d.supportsDiarization = true;    // the Accurate tier diarizes; Fast does not (catalog metadata)
    d.networkRequired     = true;
    // No key field: one sign-in (device flow) serves both providers; the tier (Fast / Accurate / Expert)
    // is set by the Settings Tanara Cloud panel and the estimate dialog.
    return d;
}

// --- Embedding: a local / own OpenAI-compatible endpoint ---
ProviderDescriptor openAiCompatEmbeddingDescriptor()
{
    ProviderDescriptor d;
    d.id          = embeddingproviders::LocalId;
    d.displayName = QCoreApplication::translate("BuiltinProviders", "Helyi végpont");
    d.capability  = CapabilityKind::TextEmbedding;
    d.origin      = Origin::CustomEndpoint;
    d.traits      = localServerTraits(InputKind::Text);
    d.authMode    = AuthMode::ApiKey;
    d.networkRequired = false;

    ConfigField baseUrl;
    baseUrl.key          = QStringLiteral("baseUrl");
    baseUrl.label        = QCoreApplication::translate("BuiltinProviders", "Cím (URL)");
    baseUrl.type         = ConfigFieldType::Url;
    baseUrl.required     = true;
    baseUrl.defaultValue = QStringLiteral("http://localhost:1234/v1");

    ConfigField model;
    model.key            = QStringLiteral("model");
    model.label          = QCoreApplication::translate("BuiltinProviders", "Modell");
    model.type           = ConfigFieldType::Combo;
    model.dynamicOptions = true;
    model.required       = true;
    model.defaultValue   = QStringLiteral("text-embedding-bge-m3");
    model.help           = QCoreApplication::translate("BuiltinProviders",
        "Többnyelvű beágyazó modell (pl. bge-m3). Modellváltáskor a könyvtár előkészítése "
        "elölről indul.");

    ConfigField apiKey;
    apiKey.key       = QStringLiteral("apiKey");
    apiKey.label     = QCoreApplication::translate("BuiltinProviders", "API-kulcs");
    apiKey.type      = ConfigFieldType::Secret;
    apiKey.required  = false;
    apiKey.isSecret  = true;
    apiKey.secretKey = embeddingproviders::ApiKeySecret;
    apiKey.advanced  = true;

    d.fields = { baseUrl, model, apiKey };
    d.probe = { QStringLiteral("/models"), true };
    return d;
}

// --- Embedding: Tanara Cloud (the gateway's /v1/embeddings route) ---
ProviderDescriptor tanaraHostedEmbeddingDescriptor()
{
    ProviderDescriptor d;
    d.id              = embeddingproviders::CloudId;
    d.displayName     = QCoreApplication::translate("BuiltinProviders", "Tanara Cloud");
    d.capability      = CapabilityKind::TextEmbedding;
    d.origin          = Origin::Cloud;
    d.traits          = remoteTraits(InputKind::Text, /*metered*/ true);
    d.authMode        = AuthMode::Login;
    d.loginSecretKey  = QStringLiteral("tanara.cloud.apiKey");
    d.networkRequired = true;
    return d;
}

// --- LM Studio: the local model server handler of the OpenAI-compatible chat provider ---
ProviderDescriptor lmStudioServerDescriptor()
{
    ProviderDescriptor d;
    d.id              = QStringLiteral("openai-compat");   // the chat provider it belongs to
    d.displayName     = QStringLiteral("LM Studio");
    d.capability      = CapabilityKind::LocalModelServer;
    d.origin          = Origin::CustomEndpoint;
    d.traits          = localServerTraits(InputKind::Text);
    d.authMode        = AuthMode::ApiKey;
    d.networkRequired = false;
    return d;
}

} // namespace

void registerCloudProviders(ProviderRegistry& registry)
{
    registry.add<ISttProvider>(
        tanaraCloudDescriptor(CapabilityKind::Transcription),
        [](const ProviderContext& ctx, const ProviderConfig& c, QObject* p) -> ISttProvider* {
            return new SonioxProvider(ctx, c, p);           // the gateway is Soniox-shaped
        });
    registry.add<IChatProvider>(
        tanaraCloudDescriptor(CapabilityKind::Chat),
        [](const ProviderContext& ctx, const ProviderConfig& c, QObject* p) -> IChatProvider* {
            return new OpenAiCompatibleProvider(ctx, c, p); // the gateway is OpenAI-shaped
        });
}

void registerBuiltinProviders(ProviderRegistry& registry)
{
    registry.add<ISttProvider>(
        sonioxDescriptor(),
        [](const ProviderContext& ctx, const ProviderConfig& c, QObject* p) -> ISttProvider* {
            return new SonioxProvider(ctx, c, p);
        });

    registry.add<ISttProvider>(
        whisperCompatDescriptor(),
        [](const ProviderContext& ctx, const ProviderConfig& c, QObject* p) -> ISttProvider* {
            return new WhisperCompatProvider(ctx, c, p);
        });

    registry.add<IChatProvider>(
        openAiCompatDescriptor(),
        [](const ProviderContext& ctx, const ProviderConfig& c, QObject* p) -> IChatProvider* {
            return new OpenAiCompatibleProvider(ctx, c, p);
        });
    // The optional local-server handler of the same chat provider (LM Studio's native API; for another
    // server it reports "unknown" and the task starts without preparation).
    registry.add<ILocalModelServer>(
        lmStudioServerDescriptor(),
        [](const ProviderContext& ctx, const ProviderConfig& c, QObject* p) -> ILocalModelServer* {
            return new LlmModelPreparer(ctx, c, p);
        });

    // Embedding (tag suggestions): both descriptors are always registered; the Tanara Cloud variant stops
    // with a structured gateway error without sign-in (the UI offers the login).
    registry.add<ITextEmbedder>(
        openAiCompatEmbeddingDescriptor(),
        [](const ProviderContext& ctx, const ProviderConfig& c, QObject* p) -> ITextEmbedder* {
            return new OpenAiCompatibleEmbeddingProvider(ctx, c, p);
        });
    registry.add<ITextEmbedder>(
        tanaraHostedEmbeddingDescriptor(),
        [](const ProviderContext& ctx, const ProviderConfig& c, QObject* p) -> ITextEmbedder* {
            return new OpenAiCompatibleEmbeddingProvider(ctx, c, p);   // the gateway is OpenAI-shaped
        });
}

} // namespace tanara::providers
