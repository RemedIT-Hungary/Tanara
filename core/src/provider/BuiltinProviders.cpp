#include "tanara/provider/ProviderRegistry.h"

// A beépített providerek konkrét fejlécei KIZÁRÓLAG itt jelennek meg — így a
// registry maga provider-agnosztikus marad, a függés egy helyre koncentrálódik.
#include "tanara/stt/SonioxProvider.h"
#include "tanara/llm/OpenAiCompatibleProvider.h"

#include <QObject>

namespace tanara {

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
    baseUrl.label        = QStringLiteral("Alap URL");
    baseUrl.type         = ConfigFieldType::Url;
    baseUrl.defaultValue = QStringLiteral("https://api.soniox.com/v1");

    ConfigField model;
    model.key          = QStringLiteral("model");
    model.label        = QStringLiteral("Modell");
    model.type         = ConfigFieldType::Text;
    model.defaultValue = QStringLiteral("stt-async-v5");

    ConfigField apiKey;
    apiKey.key       = QStringLiteral("apiKey");
    apiKey.label     = QStringLiteral("API-kulcs");
    apiKey.type      = ConfigFieldType::Secret;
    apiKey.required  = true;
    apiKey.isSecret  = true;
    apiKey.secretKey = QStringLiteral("soniox.apiKey");

    d.fields = { baseUrl, model, apiKey };
    return d;
}

// --- OpenAI-kompatibilis LLM descriptor ---
ProviderDescriptor openAiCompatDescriptor()
{
    ProviderDescriptor d;
    d.id                = QStringLiteral("openai-compat");
    d.displayName       = QStringLiteral("OpenAI-kompatibilis végpont");
    d.kind              = ProviderKind::Llm;
    d.authMode          = AuthMode::ApiKey;
    d.supportsStreaming = false;

    ConfigField baseUrl;
    baseUrl.key          = QStringLiteral("baseUrl");
    baseUrl.label        = QStringLiteral("Alap URL");
    baseUrl.type         = ConfigFieldType::Url;
    baseUrl.required     = true;
    baseUrl.defaultValue = QStringLiteral("http://localhost:1234/v1");

    ConfigField model;
    model.key            = QStringLiteral("model");
    model.label          = QStringLiteral("Modell");
    model.type           = ConfigFieldType::Combo;
    model.dynamicOptions = true;
    model.defaultValue   = QStringLiteral("google/gemma-4-12b"); // #27: üres config se legyen üres mező

    ConfigField temperature;
    temperature.key          = QStringLiteral("temperature");
    temperature.label        = QStringLiteral("Hőmérséklet");
    temperature.type         = ConfigFieldType::Number;
    temperature.defaultValue = QStringLiteral("0.2");

    ConfigField maxTokens;
    maxTokens.key          = QStringLiteral("maxTokens");
    maxTokens.label        = QStringLiteral("Max. tokenek");
    maxTokens.type         = ConfigFieldType::Number;
    maxTokens.defaultValue = QStringLiteral("8000");

    ConfigField apiKey;
    apiKey.key       = QStringLiteral("apiKey");
    apiKey.label     = QStringLiteral("API-kulcs");
    apiKey.type      = ConfigFieldType::Secret;
    apiKey.required  = false;
    apiKey.isSecret  = true;
    apiKey.secretKey = QStringLiteral("llm.apiKey");

    d.fields = { baseUrl, model, temperature, maxTokens, apiKey };
    return d;
}

} // namespace

void registerBuiltinProviders()
{
    // Idempotens: a többszöri hívás (pl. több AppController) ne duplikáljon.
    static bool registered = false;
    if (registered)
        return;
    registered = true;

    SttProviderRegistry::instance().registerProvider(
        sonioxDescriptor(),
        [](const ProviderConfig& c, QObject* p) -> ISttProvider* {
            return new SonioxProvider(c, p);
        });

    LlmProviderRegistry::instance().registerProvider(
        openAiCompatDescriptor(),
        [](const ProviderConfig& c, QObject* p) -> ILlmProvider* {
            return new OpenAiCompatibleProvider(c, p);
        });
}

} // namespace tanara
