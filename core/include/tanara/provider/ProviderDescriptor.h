#pragma once
//
// Provider-leíró + deklaratív konfig-séma (headless érték-típusok).
// Ezekből renderelődik a Beállítások UI generikusan, és ezeken alapul a
// ReadinessModel kapuzás (Fázis 3). NEM linkel Qt Widgetset.
//
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

enum class ProviderKind { Stt, Llm };
enum class AuthMode { None, ApiKey, Login };          // Login = jövőbeli hosztolt „bejelentkezés"
enum class ConfigFieldType { Text, Secret, Number, Combo, Url };

struct ConfigOption {
    QString value;
    QString label;
};

// Egy konfig-mező deklaratív leírása. A `key` vagy egy ProviderConfig jól-ismert
// mezőjére mutat (baseUrl/model/temperature/maxTokens), vagy a ProviderConfig.extra
// kulcsa. Titok esetén az érték a KeyStore-ba megy (secretKey alatt), nem a configba.
struct ConfigField {
    QString key;
    QString label;
    ConfigFieldType type = ConfigFieldType::Text;
    bool required = false;
    QString help;                       // a „miért" — tooltip/súgó forrása
    QString defaultValue;               // string-formában; számokat a renderer értelmez
    QVector<ConfigOption> options;      // Combo esetén
    bool isSecret = false;              // true → KeyStore (nem perzisztálódik a settings.json-be)
    QString secretKey;                  // KeyStore-kulcs, ha isSecret (pl. "soniox.apiKey")
    bool dynamicOptions = false;        // Combo, aminek opcióit futásidőben kérjük le (pl. modell-lista)
};

struct ProviderDescriptor {
    QString id;                         // stabil azonosító: "soniox", "openai-compat", "tanara-hosted"
    QString displayName;                // ember-olvasható név
    ProviderKind kind = ProviderKind::Stt;
    AuthMode authMode = AuthMode::ApiKey;
    bool supportsLive = false;          // STT élő/streaming
    bool supportsStreaming = false;     // LLM streaming
    bool supportsDiarization = false;
    bool networkRequired = true;
    QStringList languages;              // pl. {"hu","en"}; üres = bármi
    QVector<ConfigField> fields;        // a deklaratív konfig-séma
};

} // namespace tanara
