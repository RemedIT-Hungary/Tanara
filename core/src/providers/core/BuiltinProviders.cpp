#include "tanara/providers/core/ProviderRegistry.h"
#include "tanara/providers/core/ProviderConfig.h"

// The concrete headers of the built-in providers appear ONLY here, so the registry itself stays
// provider-agnostic and the dependency is concentrated in one place.
#include "tanara/providers/stt/SonioxProvider.h"
#include "tanara/providers/stt/WhisperCompatProvider.h"
#include "tanara/providers/chat/OpenAiCompatibleProvider.h"
#include "tanara/providers/embedding/EmbeddingProviderRegistry.h"
#include "tanara/providers/embedding/OpenAiCompatibleEmbeddingProvider.h"

#include <QCoreApplication>
#include <QObject>

namespace tanara::providers {

namespace {

// --- Soniox STT descriptor ---
ProviderDescriptor sonioxDescriptor()
{
    ProviderDescriptor d;
    d.id                  = QStringLiteral("soniox");
    d.displayName         = QStringLiteral("Soniox");
    d.kind                = ProviderKind::Stt;
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
    d.kind              = ProviderKind::Llm;
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
    d.kind     = ProviderKind::Stt;
    d.authMode = AuthMode::ApiKey;
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
ProviderDescriptor tanaraCloudDescriptor(ProviderKind kind)
{
    ProviderDescriptor d;
    d.id                  = QStringLiteral("tanara-cloud");
    d.displayName         = QCoreApplication::translate("BuiltinProviders", "Tanara Cloud — bejelentkezés");
    d.kind                = kind;
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
    d.kind        = ProviderKind::Embedding;
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
    d.kind            = ProviderKind::Embedding;
    d.authMode        = AuthMode::Login;
    d.loginSecretKey  = QStringLiteral("tanara.cloud.apiKey");
    d.networkRequired = true;
    return d;
}

} // namespace

void registerCloudProviders()
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;

    SttProviderRegistry::instance().registerProvider(
        tanaraCloudDescriptor(ProviderKind::Stt),
        [](const ProviderConfig& c, QObject* p) -> ISttProvider* {
            return new SonioxProvider(c, p);           // the gateway is Soniox-shaped
        });
    LlmProviderRegistry::instance().registerProvider(
        tanaraCloudDescriptor(ProviderKind::Llm),
        [](const ProviderConfig& c, QObject* p) -> ILlmProvider* {
            return new OpenAiCompatibleProvider(c, p); // the gateway is OpenAI-shaped
        });
}

void registerBuiltinProviders()
{
    // Idempotent: repeated calls (e.g. several AppControllers) must not duplicate.
    static bool registered = false;
    if (registered)
        return;
    registered = true;

    SttProviderRegistry::instance().registerProvider(
        sonioxDescriptor(),
        [](const ProviderConfig& c, QObject* p) -> ISttProvider* {
            return new SonioxProvider(c, p);
        });

    SttProviderRegistry::instance().registerProvider(
        whisperCompatDescriptor(),
        [](const ProviderConfig& c, QObject* p) -> ISttProvider* {
            return new WhisperCompatProvider(c, p);
        });

    LlmProviderRegistry::instance().registerProvider(
        openAiCompatDescriptor(),
        [](const ProviderConfig& c, QObject* p) -> ILlmProvider* {
            return new OpenAiCompatibleProvider(c, p);
        });

    // Embedding (tag suggestions): both descriptors are always registered; the Tanara Cloud variant stops
    // with a structured gateway error without sign-in (the UI offers the login).
    EmbeddingProviderRegistry::instance().registerProvider(
        openAiCompatEmbeddingDescriptor(),
        [](const ProviderConfig& c, QObject* p) -> IEmbeddingProvider* {
            return new OpenAiCompatibleEmbeddingProvider(c, p);
        });
    EmbeddingProviderRegistry::instance().registerProvider(
        tanaraHostedEmbeddingDescriptor(),
        [](const ProviderConfig& c, QObject* p) -> IEmbeddingProvider* {
            return new OpenAiCompatibleEmbeddingProvider(c, p);   // the gateway is OpenAI-shaped
        });
}

} // namespace tanara::providers