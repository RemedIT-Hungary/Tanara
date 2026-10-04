#include "tanara/provider/ProviderRegistry.h"

// A beépített providerek konkrét fejlécei KIZÁRÓLAG itt jelennek meg — így a
// registry maga provider-agnosztikus marad, a függés egy helyre koncentrálódik.
#include "tanara/stt/SonioxProvider.h"
#include "tanara/stt/WhisperCompatProvider.h"
#include "tanara/llm/OpenAiCompatibleProvider.h"

#include <QCoreApplication>
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
    // GET /models: kulccsal védett, ingyenes lista-végpont (nem indít átírást).
    d.probe = { QStringLiteral("/models"), true };
    return d;
}

// --- OpenAI-kompatibilis LLM descriptor ---
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
    model.defaultValue   = QStringLiteral("google/gemma-4-12b"); // #27: üres config se legyen üres mező

    ConfigField temperature;
    temperature.key          = QStringLiteral("temperature");
    temperature.label        = QCoreApplication::translate("BuiltinProviders", "Hőmérséklet");
    temperature.type         = ConfigFieldType::Number;
    temperature.defaultValue = QStringLiteral("0.2");
    temperature.advanced     = true;
    temperature.minValue     = 0.0;
    temperature.maxValue     = 1.0;   // összefoglaláshoz e fölött nincs értelme

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
    d.probe = { QStringLiteral("/models"), true };   // ugyanaz, amit a modell-lekérés használ
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
    d.supportsDiarization = false;   // minden token „Beszélő 1" — a nevet a voice-ID adja

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
    apiKey.required  = false;   // lokális szervernek nem kell
    apiKey.isSecret  = true;
    apiKey.secretKey = QStringLiteral("stt.whisper.apiKey");

    d.fields = {baseUrl, model, language, apiKey};
    d.probe = { QStringLiteral("/models"), true };   // OpenAI API és a helyi whisper-szerverek is adják
    return d;
}

// --- Tanara Cloud (STT és LLM ugyanazzal az id-vel, a két registryben külön) ---
ProviderDescriptor tanaraCloudDescriptor(ProviderKind kind)
{
    ProviderDescriptor d;
    d.id                  = QStringLiteral("tanara-cloud");
    d.displayName         = QCoreApplication::translate("BuiltinProviders", "Tanara Cloud — bejelentkezés");
    d.kind                = kind;
    d.authMode            = AuthMode::Login;
    d.loginSecretKey      = QStringLiteral("tanara.cloud.apiKey");
    d.supportsDiarization = true;    // a Pontos szint diarizál; a Gyors nem (katalógus-metaadat)
    d.networkRequired     = true;
    // Nincs kulcs-mező: egy bejelentkezés (device flow) mindkét providerhez; a szintet
    // (Gyors / Pontos / Expert) a Beállítások Tanara Cloud panelje és a becslés-dialógus állítja.
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
            return new SonioxProvider(c, p);           // a gateway Soniox-alakú
        });
    LlmProviderRegistry::instance().registerProvider(
        tanaraCloudDescriptor(ProviderKind::Llm),
        [](const ProviderConfig& c, QObject* p) -> ILlmProvider* {
            return new OpenAiCompatibleProvider(c, p); // a gateway OpenAI-alakú
        });
}

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
}

} // namespace tanara
