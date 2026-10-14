#pragma once
//
// SettingsProviderModel: one provider card in Settings > Services: the local endpoint's
// fields for "Transcription (STT)", "Summary (LLM)" or "Embedding" (SettingsEmbeddingModel
// ties that one together).
//
// An already configured role shows as a one-line, collapsed card (`configured`,
// `expanded`, `summaryText`); it opens on click. A deep link (focusField) and a failed
// connection test always open it.
//
// DATA-DRIVEN: the selectable providers come from the provider registry, the fields from the
// selected ProviderDescriptor.fields (type, required, secret, default, advanced, fetchable
// model list); a new provider appears here without code. The fields write the
// SettingsViewModel's draft (secrets the secret draft); on a provider switch the other
// provider's settings are kept.
//
// "Test connection" and "Fetch": tanara::ConnectionTester with the DRAFT's address and key
// (can be tried before saving). Changing any field makes the previous test's result stale.
//
#include "tanara/domain/Types.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/providers/core/ConnectionTester.h"
#include "tanara/providers/chat/LlmServer.h"
#include "tanara/providers/core/ProviderDescriptor.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class SettingsViewModel;

class SettingsProviderModel : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(QString kind READ kindName CONSTANT)   // "stt" | "llm" | "embedding"
    // [{ value (the provider's id), label, tag }]: the own-key providers (+ Tanara Cloud, if that is the selected one).
    Q_PROPERTY(QVariantList providers READ providers NOTIFY providersChanged)
    Q_PROPERTY(QString providerId READ providerId WRITE setProviderId NOTIFY providerChanged)
    Q_PROPERTY(QString providerLabel READ providerLabel NOTIFY providerChanged)
    // The selected provider is Tanara Cloud (with sign-in): no fields, no test.
    Q_PROPERTY(bool loginProvider READ loginProvider NOTIFY providerChanged)
    // The STRUCTURE of the main and "Advanced" fields (changes only on a provider switch, so
    // the editors are not rebuilt while typing):
    // [{ key, label, type: text|url|secret|number|combo, help, required, dynamic,
    //    minValue, maxValue, decimals }]
    // The values come from value(key) / fieldError(key) / placeholder(key) / options(key) calls;
    // QML binds to `revision` to notice the change.
    Q_PROPERTY(QVariantList fields READ fields NOTIFY fieldsChanged)
    Q_PROPERTY(QVariantList advancedFields READ advancedFields NOTIFY fieldsChanged)
    Q_PROPERTY(int revision READ revision NOTIFY valuesChanged)
    Q_PROPERTY(bool advancedOpen READ advancedOpen WRITE setAdvancedOpen NOTIFY advancedOpenChanged)
    // Connection test: "" (none / stale) | "testing" | "ok" | "failed"
    Q_PROPERTY(bool testable READ testable NOTIFY providerChanged)
    Q_PROPERTY(QString testState READ testState NOTIFY testChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY testChanged)     // "Connected · 210 ms"
    Q_PROPERTY(QString errorText READ errorText NOTIFY testChanged)       // human-readable sentence
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY testChanged)       // ECONNREFUSED, HTTP 401 …
    Q_PROPERTY(QString warningText READ warningText NOTIFY testChanged)   // success, but e.g. the model is not in the list
    Q_PROPERTY(bool fetching READ fetching NOTIFY fetchChanged)
    Q_PROPERTY(QString fetchError READ fetchError NOTIFY fetchChanged)
    // The model's "thinking" for the summary (providers::ProviderConfig::reasoning): "auto" | "off" |
    // "on". Only for the own-key LLM provider (in the Advanced part); the cloud decides itself.
    Q_PROPERTY(bool reasoningAvailable READ reasoningAvailable NOTIFY providerChanged)
    Q_PROPERTY(QString reasoning READ reasoning WRITE setReasoning NOTIFY valuesChanged)
    // The model's context (providers::ProviderConfig::contextLength): 0 = automatic, otherwise tokens.
    // Only for the own-key LLM provider (in the Advanced part). contextOptions:
    // [{ value ("0" | "16384" ...), label }]: the standard steps (+ the stored custom value).
    Q_PROPERTY(bool contextAvailable READ contextAvailable NOTIFY providerChanged)
    Q_PROPERTY(int contextLength READ contextLength WRITE setContextLength NOTIFY valuesChanged)
    Q_PROPERTY(QVariantList contextOptions READ contextOptions NOTIFY valuesChanged)
    // Per the server (LM Studio native API): the loaded context, parallelism, the model's
    // maximum, on one line; empty if unknown (not LM Studio, or not asked yet).
    Q_PROPERTY(QString serverInfo READ serverInfo NOTIFY serverInfoChanged)
    Q_PROPERTY(int serverLoadedContext READ serverLoadedContext NOTIFY serverInfoChanged)
    Q_PROPERTY(int serverMaxContext READ serverMaxContext NOTIFY serverInfoChanged)
    // The deep link led to this card.
    Q_PROPERTY(bool highlighted READ highlighted NOTIFY highlightedChanged)
    // The role can run with the draft (required fields, key / sign-in present).
    Q_PROPERTY(bool configured READ configured NOTIFY valuesChanged)
    // Expanded card (by default: one that is not set up, highlighted, or whose test failed).
    Q_PROPERTY(bool expanded READ expanded WRITE setExpanded NOTIFY expandedChanged)
    // The collapsed row's right side: "Soniox", "LM Studio · gemma-4-12b".
    Q_PROPERTY(QString summaryText READ summaryText NOTIFY valuesChanged)

public:
    SettingsProviderModel(SettingsViewModel* vm, tanara::providers::CapabilityKind kind);

    QString kindName() const;
    QVariantList providers() const;
    QString providerId() const;
    void setProviderId(const QString& id);
    QString providerLabel() const;
    bool loginProvider() const;
    QVariantList fields() const { return fieldList(false); }
    QVariantList advancedFields() const { return fieldList(true); }
    bool advancedOpen() const { return m_advancedOpen; }
    void setAdvancedOpen(bool open);
    bool testable() const;
    QString testState() const { return m_testState; }
    QString statusText() const;
    QString errorText() const { return m_testState == QLatin1String("failed") ? m_result.message : QString(); }
    QString errorCode() const { return m_testState == QLatin1String("failed") ? m_result.code : QString(); }
    QString warningText() const { return m_testState == QLatin1String("ok") ? m_result.warning : QString(); }
    bool fetching() const { return m_fetchId != 0; }
    QString fetchError() const { return m_fetchError; }
    bool highlighted() const;
    bool configured() const;
    bool expanded() const { return m_expanded; }
    void setExpanded(bool expanded);
    QString summaryText() const;
    bool reasoningAvailable() const;
    QString reasoning() const;
    void setReasoning(const QString& mode);
    bool contextAvailable() const { return reasoningAvailable(); }
    int contextLength() const;
    void setContextLength(int tokens);
    QVariantList contextOptions() const;
    QString serverInfo() const { return m_serverText; }
    int serverLoadedContext() const;
    int serverMaxContext() const { return m_server.maxContext; }
    // Refreshes the server data (read-only GET; also on its own when the Advanced part opens).
    Q_INVOKABLE void refreshServerInfo();

    Q_INVOKABLE void setValue(const QString& key, const QVariant& value);
    Q_INVOKABLE QVariant value(const QString& key) const;
    Q_INVOKABLE QString fieldError(const QString& key) const;
    Q_INVOKABLE QString placeholder(const QString& key) const;
    Q_INVOKABLE QStringList options(const QString& key) const;
    int revision() const { return m_revision; }
    Q_INVOKABLE void test();
    Q_INVOKABLE void fetchModels();

    // ---- a SettingsViewModelnek ----
    tanara::providers::ProviderDescriptor descriptor() const;
    tanara::providers::ProviderDescriptor descriptor(const QString& id) const;
    QVector<tanara::providers::ProviderDescriptor> allDescriptors() const;
    // Fills the missing (empty) fields with the descriptor's defaults.
    void fillDefaults(tanara::providers::ProviderConfig& cfg, const tanara::providers::ProviderDescriptor& d) const;
    // The draft's runtime config for the selected provider (with the key).
    tanara::providers::ProviderConfig runtimeConfig() const;
    // Field errors for the pre-save validation: key -> text.
    QHash<QString, QString> fieldErrors() const;
    // The draft / the provider changed (reload, discard, external change).
    void reset();
    void notifyFocusChanged();
    bool lastTestFailed() const { return m_testState == QLatin1String("failed"); }
    // Demo: a fixed test result.
    void setDemoResult(const QString& state, int latencyMs, const QString& message,
                       const QString& code);
    // Demo: fixed server data (with a loaded LM Studio instance).
    void setDemoServerInfo(const tanara::providers::llmctx::LlmServerInfo& info) { setServerInfo(info); }
    void setTester(tanara::providers::ConnectionTester* tester);   // for tests (e.g. a short timeout)
    tanara::providers::ConnectionTester* tester() const { return m_tester; }

signals:
    void providersChanged();
    void providerChanged();
    void fieldsChanged();
    void valuesChanged();
    void advancedOpenChanged();
    void testChanged();
    void fetchChanged();
    void highlightedChanged();
    void expandedChanged();
    void serverInfoChanged();

private:
    QVariantList fieldList(bool advanced) const;
    void bumpValues() { ++m_revision; emit valuesChanged(); }
    QMap<QString, tanara::providers::ProviderConfig>& configs() const;
    QString& selectedId() const;
    void invalidateTest();
    void onFinished(int id, const tanara::providers::ConnectionTestResult& result);

    SettingsViewModel* m_vm;
    tanara::providers::CapabilityKind m_kind;
    tanara::providers::ConnectionTester* m_tester = nullptr;
    bool m_advancedOpen = false;
    bool m_expanded = true;
    int m_revision = 0;
    QString m_testState;
    tanara::providers::ConnectionTestResult m_result;
    int m_testId = 0;
    int m_fetchId = 0;
    QString m_fetchError;
    // withWarnings: warnings go into the line too (for the connection test they show in
    // warningText, so we do not repeat them there).
    void setServerInfo(const tanara::providers::llmctx::LlmServerInfo& info, bool withWarnings = true);
    void clearServerInfo();
    tanara::providers::LlmServerProbe* m_serverProbe = nullptr;
    tanara::providers::llmctx::LlmServerInfo m_server;
    QString m_serverText;
    QStringList m_models;          // the fetched model list (for the selected provider)
    QString m_modelsFor;           // which provider + address the list belongs to
};

} // namespace tanara_qml
