#pragma once
//
// Provider descriptor + declarative config schema (headless value types).
// The Settings UI is rendered generically from these, and the ReadinessModel gating is based on them.
// Does NOT link Qt Widgets.
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
enum class ConfigFieldType { Text, Secret, Number, Combo, Url };

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
    QVector<ConfigOption> options;      // for Combo
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
// as "Authorization: Bearer". It must NEVER be a paid / state-changing call.
struct ConnectionProbe {
    QString path;                       // pl. "/models"
    // The response gives a model list ({"data":[{"id":...}]} or {"models":[{"id":...}]}): the tester checks
    // whether the configured model is in it, and the list can be passed to the UI too.
    bool listsModels = false;
    bool isValid() const { return !path.isEmpty(); }
};

struct ProviderDescriptor {
    QString id;                         // stable id: "soniox", "openai-compat", "tanara-hosted"
    QString displayName;                // human-readable name
    CapabilityKind capability = CapabilityKind::Transcription;   // set by ProviderRegistry::add<Port>()
    Origin origin = Origin::CustomEndpoint;
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
};

} // namespace tanara::providers