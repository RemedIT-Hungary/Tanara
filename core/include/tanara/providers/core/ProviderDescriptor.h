#pragma once
//
// Provider descriptor + declarative config schema (headless value types).
// The Settings UI is rendered generically from these, and the ReadinessModel gating is based on them.
// Does NOT link Qt Widgets.
#include <QByteArray>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::providers {

// The capability a provider gives (one registry key part; providers/core/ProviderRegistry.h).
enum class CapabilityKind {
    Transcription,              // ISttProvider (providers/stt)
    Chat,                       // IChatProvider (providers/chat)
    TextEmbedding,              // ITextEmbedder (providers/embedding): the tag suggestions' related-topic level
    VoiceEmbedding,             // IVoiceEmbedder (providers/voice): raw clip or PCM -> speaker vectors
    Summarization,              // ISummarizer (providers/summary): LLM pipeline or the server-side one
    TagSuggestion,              // ITagSuggester (providers/tags)
    ParticipantIdentification,  // IParticipantIdentifier (providers/voice): ready vectors -> who
    ModelCatalog,               // IModelCatalog (providers/catalog): models of a capability, download
    Diarization,                // IDiarizer (providers/voice): audio tracks -> who speaks when
    LocalModelServer,           // optional: ILocalModelServer (LM Studio model preparation), keyed by the chat id
};
// Stable machine name ("transcription", "chat", ...): logs, tests, the CLI.
QString capabilityName(CapabilityKind kind);

// Port <-> capability: every port header specializes it (`static constexpr CapabilityKind kind`), so
// ProviderRegistry::add<Port>() / create<Port>() can only meet under the same capability.
template <class Port> struct ProviderTraits;

// Where the provider runs, from the user's point of view:
//  Local          - on this machine, inside Tanara (ONNX models ...);
//  Cloud          - ONLY the Tanara Cloud (sign-in, metered by Tanara);
//  CustomEndpoint - a service reached with the user's own key / address: Soniox with a BYO key,
//                   OpenAI, LM Studio, Ollama, a Whisper server - local or remote alike.
enum class Origin { Local, Cloud, CustomEndpoint };

// The processing-route segment a provider belongs to (providers/core/Routes.h):
//  Local    - your machine or network, no key (a Whisper server, LM Studio, an OpenAI-compatible endpoint
//             at your own address, the local embedding endpoint);
//  External - a third-party service with your own key (Soniox; the Anthropic and OpenAI chat presets);
//  Cloud    - the Tanara Cloud.
enum class ProviderKind { Local, External, Cloud };

// What a provider consumes (Traits::inputs).
enum class InputKind { AudioFile, AudioClip, Pcm, Text };

// Declarative properties the UI and the gating can read without creating the provider.
struct Traits {
    // The user's data leaves the machine with the DEFAULT configuration (Soniox: yes; LM Studio on
    // localhost: no). A custom endpoint's real address decides at run time (isLocalEndpoint()).
    bool dataLeavesDevice = true;
    bool metered = false;           // the calls cost money per use
    bool worksOffline = false;      // works without internet (a local server / model)
    bool needsModelFile = false;    // needs a downloaded model file (ONNX ...)
    QVector<InputKind> inputs;
};

enum class AuthMode { None, ApiKey, Login };          // Login = a future hosted "sign-in"
// Choice: one of a few fixed `options` (value + label), shown as a segmented selector; the value is a
// string in ProviderConfig.extra, an empty one means the field's defaultValue.
enum class ConfigFieldType { Text, Secret, Number, Combo, Url, Choice };

struct ConfigOption {
    QString value;
    QString label;
};

// Declarative description of a config field. The `key` points either to a well-known field of a
// ProviderConfig (baseUrl/model/temperature/maxTokens) or to a key of ProviderConfig.extra. For a secret
// the value goes into the SecretStore (under secretKey), not into the config.
struct ConfigField {
    QString key;
    QString label;
    ConfigFieldType type = ConfigFieldType::Text;
    bool required = false;
    QString help;                       // the "why": source of the tooltip/help
    QString defaultValue;               // in string form; numbers are interpreted by the renderer
    QVector<ConfigOption> options;      // for Combo and Choice
    bool isSecret = false;              // true = SecretStore (not persisted into settings.json)
    QString secretKey;                  // SecretStore key if isSecret (e.g. "soniox.apiKey")
    bool dynamicOptions = false;        // a Combo whose options are fetched at run time (e.g. a model list)
    bool advanced = false;              // the UI puts it under the "Advanced" part (collapsed by default)
    // Number: the range the UI offers (minValue == maxValue = no constraint). A stored value outside the
    // range stays until the user touches it.
    double minValue = 0.0;
    double maxValue = 0.0;
};

// "Test connection": what the tester asks the provider (providers/core/ConnectionTester.h).
// Declarative, so a new provider only needs to give a descriptor. An empty `path` = the provider
// cannot be tested this way (the UI offers no button). The request: GET <baseUrl><path>, with the API key
// as "Authorization: Bearer" (or in `keyHeader`). It must NEVER be a paid / state-changing call.
struct ConnectionProbe {
    QString path;                       // pl. "/models"
    // The response gives a model list ({"data":[{"id":...}]} or {"models":[{"id":...}]}): the tester checks
    // whether the configured model is in it, and the list can be passed to the UI too.
    bool listsModels = false;
    // Optional second probe, tried only when the main one answers 404 / 405: GET <origin><path>, where
    // origin = baseUrl without a trailing slash and a trailing "/v1" (e.g. "/health" for whisper.cpp's
    // whisper-server, which has no /v1/models). A 2xx there counts as reachable, with no model list.
    QString originFallbackPath;
    // How the key travels. Empty = "Authorization: Bearer <key>"; otherwise the key goes as the value of
    // this header (Anthropic's native API: "x-api-key").
    QByteArray keyHeader;
    // Fixed headers the endpoint needs on every probe (Anthropic: "anthropic-version").
    QVector<QPair<QByteArray, QByteArray>> headers;
    // Regular expression (case-insensitive) an id must match to be OFFERED in the model list; empty =
    // every id. For a service whose list mixes chat models with others (OpenAI: embeddings, speech,
    // images). The "configured model is not in the list" check still uses the whole list.
    QString modelFilter;
    bool isValid() const { return !path.isEmpty(); }
};

// How a chat provider's /chat/completions request differs from the plain OpenAI-compatible shape (a local
// LM Studio / Ollama server). The descriptor carries it, the factory hands it to OpenAiCompatibleProvider:
// a new service preset is a descriptor, not a code branch.
struct ChatDialect {
    // false = the "temperature" field is left out (the service fixes the sampling itself: the current
    // Claude and the OpenAI reasoning models answer a non-default value with HTTP 400).
    bool sendTemperature = true;
    // Name of the reply-size field: "max_tokens", or "max_completion_tokens" where the old name is refused.
    QString maxTokensField{QStringLiteral("max_tokens")};
    // false = no thinking switch at all (neither "reasoning_effort" nor a prefill): the service ignores it
    // (Anthropic's OpenAI-compatible layer), so ProviderConfig::reasoning has no effect there.
    bool reasoningSwitch = true;
    // Floor of the reply-size field (0 = none). Where thinking cannot be reliably turned off, its tokens
    // count against the reply size: a small cap (the tag suggester's 400) would leave no room for the answer.
    int minMaxTokens = 0;
    // The service's context window in tokens when nothing reports it at run time (0 = unknown). A
    // conservative figure: the summary uses it to decide whether a whole transcript fits one call.
    int contextTokens = 0;
};

struct ProviderDescriptor {
    QString id;                         // stable id: "soniox", "openai-compat", "tanara-hosted"
    QString displayName;                // human-readable name
    CapabilityKind capability = CapabilityKind::Transcription;   // set by ProviderRegistry::add<Port>()
    Origin origin = Origin::CustomEndpoint;
    ProviderKind kind = ProviderKind::External;   // the route segment (Routes.h); providerKindOf(id) agrees
    Traits traits;
    QString catalogRef;                 // which IModelCatalog (its id) lists the models (empty = none)
    AuthMode authMode = AuthMode::ApiKey;
    bool supportsLive = false;          // STT live/streaming
    bool supportsStreaming = false;     // LLM streaming
    bool supportsDiarization = false;
    bool networkRequired = true;
    QStringList languages;              // e.g. {"hu","en"}; empty = any
    QVector<ConfigField> fields;        // the declarative config schema
    // AuthMode::Login: the SecretStore key whose presence = "signed in".
    // Empty = "<id>.token" (the earlier convention).
    QString loginSecretKey;
    ConnectionProbe probe;              // "Test connection" (empty = none)
    ChatDialect chat;                   // Chat providers only: the request's deviations (default = none)
};

} // namespace tanara::providers