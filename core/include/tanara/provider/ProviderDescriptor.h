#pragma once
//
// Provider descriptor + declarative config schema (headless value types).
// The Settings UI is rendered generically from these, and the ReadinessModel gating is based on them.
// Does NOT link Qt Widgets.
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

// Embedding: an embedding model for the tag suggestions (embedding/EmbeddingProviderRegistry.h).
enum class ProviderKind { Stt, Llm, Embedding };
enum class AuthMode { None, ApiKey, Login };          // Login = a future hosted "sign-in"
enum class ConfigFieldType { Text, Secret, Number, Combo, Url };

struct ConfigOption {
    QString value;
    QString label;
};

// Declarative description of a config field. The `key` points either to a well-known field of a
// ProviderConfig (baseUrl/model/temperature/maxTokens) or to a key of ProviderConfig.extra. For a secret
// the value goes into the KeyStore (under secretKey), not into the config.
struct ConfigField {
    QString key;
    QString label;
    ConfigFieldType type = ConfigFieldType::Text;
    bool required = false;
    QString help;                       // the "why": source of the tooltip/help
    QString defaultValue;               // in string form; numbers are interpreted by the renderer
    QVector<ConfigOption> options;      // for Combo
    bool isSecret = false;              // true = KeyStore (not persisted into settings.json)
    QString secretKey;                  // KeyStore key if isSecret (e.g. "soniox.apiKey")
    bool dynamicOptions = false;        // a Combo whose options are fetched at run time (e.g. a model list)
    bool advanced = false;              // the UI puts it under the "Advanced" part (collapsed by default)
    // Number: the range the UI offers (minValue == maxValue = no constraint). A stored value outside the
    // range stays until the user touches it.
    double minValue = 0.0;
    double maxValue = 0.0;
};

// "Test connection": what the tester asks the provider (provider/ConnectionTester.h).
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
    ProviderKind kind = ProviderKind::Stt;
    AuthMode authMode = AuthMode::ApiKey;
    bool supportsLive = false;          // STT live/streaming
    bool supportsStreaming = false;     // LLM streaming
    bool supportsDiarization = false;
    bool networkRequired = true;
    QStringList languages;              // e.g. {"hu","en"}; empty = any
    QVector<ConfigField> fields;        // the declarative config schema
    // AuthMode::Login: the KeyStore key whose presence = "signed in".
    // Empty = "<id>.token" (the earlier convention).
    QString loginSecretKey;
    ConnectionProbe probe;              // "Test connection" (empty = none)
};

} // namespace tanara
