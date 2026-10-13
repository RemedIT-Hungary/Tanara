#include "SettingsProviderModel.h"

#include "SettingsViewModel.h"

#include "tanara/app/AppController.h"
#include "tanara/providers/embedding/EmbeddingProviderRegistry.h"
#include "tanara/providers/core/ProviderRegistry.h"

#include <QLocale>
#include <QUrl>

#include <algorithm>

using namespace tanara;

namespace tanara_qml {

namespace {

// The ProviderConfig's well-known fields (the rest -> extra).
bool isWellKnownKey(const QString& key)
{
    return key == QLatin1String("baseUrl") || key == QLatin1String("model")
        || key == QLatin1String("temperature") || key == QLatin1String("maxTokens");
}

QVariant configValue(const ProviderConfig& cfg, const providers::ConfigField& f)
{
    if (f.key == QLatin1String("baseUrl")) return cfg.baseUrl;
    if (f.key == QLatin1String("model")) return cfg.model;
    if (f.key == QLatin1String("temperature")) return cfg.temperature;
    if (f.key == QLatin1String("maxTokens")) return cfg.maxTokens;
    return cfg.extra.value(f.key);
}

void setConfigValue(ProviderConfig& cfg, const providers::ConfigField& f, const QVariant& v)
{
    // The text goes into the draft RAW (while typing, the trimmed value would be written back
    // into the field); spaces are trimmed by the save and the runtime config (trimConfigs / runtimeConfig).
    if (f.key == QLatin1String("baseUrl")) cfg.baseUrl = v.toString();
    else if (f.key == QLatin1String("model")) cfg.model = v.toString();
    else if (f.key == QLatin1String("temperature")) cfg.temperature = v.toDouble();
    else if (f.key == QLatin1String("maxTokens")) cfg.maxTokens = v.toInt();
    else if (f.type == providers::ConfigFieldType::Number) cfg.extra.insert(f.key, v.toDouble());
    else cfg.extra.insert(f.key, v.toString());
}

QString typeName(providers::ConfigFieldType t)
{
    switch (t) {
    case providers::ConfigFieldType::Secret: return QStringLiteral("secret");
    case providers::ConfigFieldType::Number: return QStringLiteral("number");
    case providers::ConfigFieldType::Combo:  return QStringLiteral("combo");
    case providers::ConfigFieldType::Url:    return QStringLiteral("url");
    case providers::ConfigFieldType::Text:   break;
    }
    return QStringLiteral("text");
}

} // namespace

SettingsProviderModel::SettingsProviderModel(SettingsViewModel* vm, providers::ProviderKind kind)
    : QObject(vm), m_vm(vm), m_kind(kind)
{
    setTester(new providers::ConnectionTester(this));
}

void SettingsProviderModel::setTester(providers::ConnectionTester* tester)
{
    if (m_tester == tester) return;
    if (m_tester) {
        m_tester->cancelAll();
        disconnect(m_tester, nullptr, this, nullptr);
        if (m_tester->parent() == this) m_tester->deleteLater();
    }
    m_tester = tester;
    m_testId = m_fetchId = 0;
    if (m_tester)
        connect(m_tester, &providers::ConnectionTester::finished, this, &SettingsProviderModel::onFinished);
}

QString SettingsProviderModel::kindName() const
{
    return m_kind == providers::ProviderKind::Stt ? QStringLiteral("stt")
         : m_kind == providers::ProviderKind::Embedding ? QStringLiteral("embedding") : QStringLiteral("llm");
}

QMap<QString, ProviderConfig>& SettingsProviderModel::configs() const
{
    return m_kind == providers::ProviderKind::Stt ? m_vm->draft().sttConfigs
         : m_kind == providers::ProviderKind::Embedding ? m_vm->draft().embeddingConfigs : m_vm->draft().llmConfigs;
}

QString& SettingsProviderModel::selectedId() const
{
    return m_kind == providers::ProviderKind::Stt ? m_vm->draft().sttProviderId
         : m_kind == providers::ProviderKind::Embedding ? m_vm->draft().embeddingProviderId : m_vm->draft().llmProviderId;
}

QVector<providers::ProviderDescriptor> SettingsProviderModel::allDescriptors() const
{
    QVector<providers::ProviderDescriptor> all = m_kind == providers::ProviderKind::Stt ? providers::SttProviderRegistry::instance().all()
        : m_kind == providers::ProviderKind::Embedding ? providers::EmbeddingProviderRegistry::instance().all()
                                            : providers::LlmProviderRegistry::instance().all();
    // The registry's hash order is not stable: own-key ones by name, the sign-in one last.
    std::sort(all.begin(), all.end(), [](const providers::ProviderDescriptor& a, const providers::ProviderDescriptor& b) {
        const bool la = a.authMode == providers::AuthMode::Login, lb = b.authMode == providers::AuthMode::Login;
        if (la != lb) return lb;
        return a.displayName.localeAwareCompare(b.displayName) < 0;
    });
    return all;
}

providers::ProviderDescriptor SettingsProviderModel::descriptor(const QString& id) const
{
    if (m_kind == providers::ProviderKind::Embedding) return providers::EmbeddingProviderRegistry::instance().descriptor(id);
    return m_kind == providers::ProviderKind::Stt ? providers::SttProviderRegistry::instance().descriptor(id)
                                       : providers::LlmProviderRegistry::instance().descriptor(id);
}

providers::ProviderDescriptor SettingsProviderModel::descriptor() const
{
    return descriptor(providerId());
}

QString SettingsProviderModel::providerId() const
{
    return selectedId();
}

QString SettingsProviderModel::providerLabel() const
{
    const providers::ProviderDescriptor d = descriptor();
    return d.displayName.isEmpty() ? providerId() : d.displayName;
}

bool SettingsProviderModel::loginProvider() const
{
    return descriptor().authMode == providers::AuthMode::Login;
}

QVariantList SettingsProviderModel::providers() const
{
    QVariantList out;
    const QString current = providerId();
    for (const providers::ProviderDescriptor& d : allDescriptors()) {
        // The sign-in (Tanara Cloud) provider is chosen with the mode selector above; it is listed
        // here only if it is the selected one (preserving a mixed setting).
        if (d.authMode == providers::AuthMode::Login && d.id != current) continue;
        QString tag;
        if (m_kind == providers::ProviderKind::Stt && d.supportsDiarization && d.authMode != providers::AuthMode::Login)
            tag = tr("beszélőket elkülönít");
        out << QVariantMap{{QStringLiteral("value"), d.id},
                           {QStringLiteral("label"), d.displayName},
                           {QStringLiteral("tag"), tag}};
    }
    return out;
}

void SettingsProviderModel::fillDefaults(ProviderConfig& cfg, const providers::ProviderDescriptor& d) const
{
    cfg.type = d.id;
    for (const providers::ConfigField& f : d.fields) {
        if (f.isSecret || f.defaultValue.isEmpty()) continue;
        const QVariant v = configValue(cfg, f);
        // For number fields 0 also means "not set" (because of old, partially filled configs).
        const bool zeroIsValid = f.minValue != f.maxValue && f.minValue <= 0.0;   // e.g. temperature 0
        const bool empty = f.type == providers::ConfigFieldType::Number ? (v.toDouble() == 0.0 && !zeroIsValid)
                                                             : v.toString().trimmed().isEmpty();
        if (!empty) continue;
        if (f.type == providers::ConfigFieldType::Number)
            setConfigValue(cfg, f, f.defaultValue.toDouble());
        else
            setConfigValue(cfg, f, f.defaultValue);
    }
}

void SettingsProviderModel::setProviderId(const QString& id)
{
    if (id.isEmpty() || id == selectedId()) return;
    const providers::ProviderDescriptor d = descriptor(id);
    if (d.id.isEmpty()) return;   // unregistered provider
    selectedId() = id;
    // The other provider's settings stay in the map; the new one starts with the defaults.
    ProviderConfig cfg = configs().value(id);
    fillDefaults(cfg, d);
    configs().insert(id, cfg);
    m_models.clear();
    m_modelsFor.clear();
    m_fetchError.clear();
    invalidateTest();
    emit providerChanged();
    emit providersChanged();
    emit fieldsChanged();
    bumpValues();
    emit fetchChanged();
    m_vm->touch();
}

QVariantList SettingsProviderModel::fieldList(bool advanced) const
{
    QVariantList out;
    const providers::ProviderDescriptor d = descriptor();
    for (const providers::ConfigField& f : d.fields) {
        if (f.advanced != advanced) continue;
        QVariantMap m;
        m[QStringLiteral("key")] = f.key;
        m[QStringLiteral("label")] = f.label.isEmpty() ? f.key : f.label;
        m[QStringLiteral("type")] = typeName(f.type);
        m[QStringLiteral("required")] = f.required;
        m[QStringLiteral("help")] = f.help;
        m[QStringLiteral("dynamic")] = f.dynamicOptions;
        m[QStringLiteral("minValue")] = f.minValue;
        m[QStringLiteral("maxValue")] = f.maxValue;
        m[QStringLiteral("decimals")] = f.key == QLatin1String("temperature") ? 2 : 0;
        out << m;
    }
    return out;
}

QString SettingsProviderModel::fieldError(const QString& key) const
{
    return fieldErrors().value(key);
}

QString SettingsProviderModel::placeholder(const QString& key) const
{
    const providers::ProviderDescriptor d = descriptor();
    for (const providers::ConfigField& f : d.fields) {
        if (f.key != key) continue;
        if (!f.isSecret) return f.defaultValue;
        const bool local = providers::isLocalEndpoint(configs().value(d.id).baseUrl);
        return !f.required && local ? tr("nem kell (helyi végpont)")
             : f.required ? tr("illeszd be a kulcsot") : tr("nem kötelező");
    }
    return {};
}

QStringList SettingsProviderModel::options(const QString& key) const
{
    const providers::ProviderDescriptor d = descriptor();
    QStringList out;
    for (const providers::ConfigField& f : d.fields) {
        if (f.key != key) continue;
        for (const providers::ConfigOption& o : f.options) out << o.value;
        if (f.dynamicOptions && m_modelsFor == d.id)
            for (const QString& id : m_models)
                if (!out.contains(id)) out << id;
    }
    return out;
}

QVariant SettingsProviderModel::value(const QString& key) const
{
    const providers::ProviderDescriptor d = descriptor();
    for (const providers::ConfigField& f : d.fields) {
        if (f.key != key) continue;
        if (f.isSecret) return m_vm->draftSecret(f.secretKey);
        return configValue(configs().value(d.id), f);
    }
    return {};
}

void SettingsProviderModel::setValue(const QString& key, const QVariant& value)
{
    const providers::ProviderDescriptor d = descriptor();
    for (const providers::ConfigField& f : d.fields) {
        if (f.key != key) continue;
        if (f.isSecret) {
            const QString v = value.toString();
            if (m_vm->draftSecret(f.secretKey) == v) return;
            m_vm->setDraftSecret(f.secretKey, v);
        } else {
            ProviderConfig cfg = configs().value(d.id);
            cfg.type = d.id;
            const QVariant before = configValue(cfg, f);
            setConfigValue(cfg, f, value);
            if (configValue(cfg, f) == before) return;
            configs().insert(d.id, cfg);
        }
        invalidateTest();
        m_vm->touch();
        bumpValues();
        return;
    }
}

bool SettingsProviderModel::reasoningAvailable() const
{
    return m_kind == providers::ProviderKind::Llm && !loginProvider();
}

QString SettingsProviderModel::reasoning() const
{
    const QString r = configs().value(descriptor().id).reasoning;
    return r == QLatin1String("off") || r == QLatin1String("on") ? r : QStringLiteral("auto");
}

void SettingsProviderModel::setReasoning(const QString& mode)
{
    if (!reasoningAvailable()) return;
    const QString m = mode == QLatin1String("off") || mode == QLatin1String("on") ? mode : QStringLiteral("auto");
    const providers::ProviderDescriptor d = descriptor();
    ProviderConfig cfg = configs().value(d.id);
    if (cfg.reasoning == m) return;
    cfg.type = d.id;
    cfg.reasoning = m;
    configs().insert(d.id, cfg);
    m_vm->touch();          // does not affect the connection test (that looks only at the address / key)
    bumpValues();
}

int SettingsProviderModel::contextLength() const
{
    return qMax(0, configs().value(descriptor().id).contextLength);
}

void SettingsProviderModel::setContextLength(int tokens)
{
    if (!reasoningAvailable()) return;
    const int v = qMax(0, tokens);
    const providers::ProviderDescriptor d = descriptor();
    ProviderConfig cfg = configs().value(d.id);
    if (cfg.contextLength == v) return;
    cfg.type = d.id;
    cfg.contextLength = v;
    configs().insert(d.id, cfg);
    m_vm->touch();          // does not affect the connection test
    bumpValues();
}

QVariantList SettingsProviderModel::contextOptions() const
{
    QVariantList out;
    out << QVariantMap{{QStringLiteral("value"), QStringLiteral("0")},
                       {QStringLiteral("label"), tr("Automatikus (a feladathoz igazítva)")}};
    QList<int> steps;
    for (int s : providers::llmctx::contextSteps())
        if (s <= 131072) steps << s;
    const int cur = contextLength();
    if (cur > 0 && !steps.contains(cur)) { steps << cur; std::sort(steps.begin(), steps.end()); }
    const int max = m_server.maxContext;
    for (int s : std::as_const(steps)) {
        if (max > 0 && s > max && s != cur) continue;   // we do not offer more than the model's maximum
        out << QVariantMap{{QStringLiteral("value"), QString::number(s)},
                           {QStringLiteral("label"), tr("%1 token").arg(QLocale().toString(s))}};
    }
    return out;
}

int SettingsProviderModel::serverLoadedContext() const
{
    return m_server.loaded() ? m_server.instances.first().contextLength : -1;
}

void SettingsProviderModel::setServerInfo(const providers::llmctx::LlmServerInfo& info, bool withWarnings)
{
    m_server = info;
    QString text, warning;
    providers::describeLlmServer(info, &text, &warning);
    m_serverText = !withWarnings || warning.isEmpty() ? text : text + QLatin1Char(' ') + warning;
    emit serverInfoChanged();
    bumpValues();   // the step list adapts to the model's maximum
}

void SettingsProviderModel::clearServerInfo()
{
    if (m_serverProbe) m_serverProbe->cancel();
    if (!m_server.isLmStudio() && m_serverText.isEmpty()) return;
    m_server = {};
    m_serverText.clear();
    emit serverInfoChanged();
}

void SettingsProviderModel::refreshServerInfo()
{
    if (!reasoningAvailable() || !m_vm->controller()) return;
    if (!m_serverProbe) {
        m_serverProbe = new providers::LlmServerProbe(this);
        connect(m_serverProbe, &providers::LlmServerProbe::finished, this,
                [this](const providers::llmctx::LlmServerInfo& info) { setServerInfo(info); });
    }
    m_serverProbe->probe(runtimeConfig());
}

void SettingsProviderModel::setAdvancedOpen(bool open)
{
    if (m_advancedOpen == open) return;
    m_advancedOpen = open;
    emit advancedOpenChanged();
    if (open) refreshServerInfo();
}

QHash<QString, QString> SettingsProviderModel::fieldErrors() const
{
    QHash<QString, QString> out;
    const providers::ProviderDescriptor d = descriptor();
    const ProviderConfig cfg = configs().value(d.id);
    for (const providers::ConfigField& f : d.fields) {
        if (f.isSecret) continue;   // a missing key is not an obstacle to saving (the dot signals it)
        const QVariant v = configValue(cfg, f);
        const QString text = v.toString().trimmed();
        if (f.type == providers::ConfigFieldType::Url) {
            if (text.isEmpty()) {
                if (f.required) out.insert(f.key, tr("A cím kötelező."));
                continue;
            }
            const QUrl url(text);
            const bool http = url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https");
            if (!url.isValid() || !http || url.host().isEmpty())
                out.insert(f.key, tr("http:// vagy https:// kezdetű címet adj meg."));
        } else if (f.type == providers::ConfigFieldType::Number) {
            // A stored out-of-range value stays until the user touches it.
            const QMap<QString, ProviderConfig>& baseConfigs = m_kind == providers::ProviderKind::Stt ? m_vm->base().sttConfigs
                : m_kind == providers::ProviderKind::Embedding ? m_vm->base().embeddingConfigs : m_vm->base().llmConfigs;
            const bool untouched = baseConfigs.contains(d.id) && configValue(baseConfigs.value(d.id), f) == v;
            if (!untouched && f.minValue != f.maxValue && f.key != QLatin1String("temperature")
                && (v.toDouble() < f.minValue || v.toDouble() > f.maxValue))
                out.insert(f.key, tr("%1 és %2 közötti szám kell.")
                                      .arg(QLocale().toString(qlonglong(f.minValue)),
                                           QLocale().toString(qlonglong(f.maxValue))));
        } else if (f.required && text.isEmpty()) {
            out.insert(f.key, tr("Ez a mező kötelező."));
        }
    }
    return out;
}

ProviderConfig SettingsProviderModel::runtimeConfig() const
{
    const providers::ProviderDescriptor d = descriptor();
    ProviderConfig cfg = configs().value(d.id);
    for (const providers::ConfigField& f : d.fields)
        if (f.isSecret && f.type == providers::ConfigFieldType::Secret)
            cfg.apiKey = m_vm->draftSecret(f.secretKey).trimmed();
    cfg.baseUrl = cfg.baseUrl.trimmed();
    cfg.model = cfg.model.trimmed();
    return cfg;
}

bool SettingsProviderModel::testable() const
{
    const providers::ProviderDescriptor d = descriptor();
    return d.authMode != providers::AuthMode::Login && d.probe.isValid();
}

QString SettingsProviderModel::statusText() const
{
    if (m_testState == QLatin1String("testing")) return tr("Tesztelés…");
    if (m_testState == QLatin1String("ok"))
        return m_result.latencyMs >= 0 ? tr("Kapcsolódva · %1 ms").arg(m_result.latencyMs)
                                        : tr("Kapcsolódva");
    if (m_testState == QLatin1String("failed")) {
        switch (m_result.status) {
        case providers::ConnectionTestResult::Status::AuthFailed: return tr("A kulcs nem jó");
        case providers::ConnectionTestResult::Status::BadConfig:  return tr("Hibás cím");
        case providers::ConnectionTestResult::Status::NotAnApi:   return tr("Nem API-cím");
        case providers::ConnectionTestResult::Status::ServerError: return tr("A szolgáltató hibát jelzett");
        default: return tr("Nem érhető el");
        }
    }
    return QString();
}

bool SettingsProviderModel::highlighted() const
{
    return m_vm->focusField() == kindName();
}

bool SettingsProviderModel::configured() const
{
    if (m_kind == providers::ProviderKind::Embedding)
        return !descriptor().id.isEmpty() && fieldErrors().isEmpty();
    return m_vm->draftReadiness(m_kind == providers::ProviderKind::Stt ? providers::WorkflowStep::Transcribe
                                                            : providers::WorkflowStep::Summarize).runnable;
}

void SettingsProviderModel::setExpanded(bool expanded)
{
    if (m_expanded == expanded) return;
    m_expanded = expanded;
    emit expandedChanged();
}

void SettingsProviderModel::notifyFocusChanged()
{
    emit highlightedChanged();
    if (highlighted()) setExpanded(true);    // the deep link's card is always open
}

QString SettingsProviderModel::summaryText() const
{
    const providers::ProviderDescriptor d = descriptor();
    if (d.id.isEmpty()) return QString();
    const ProviderConfig cfg = configs().value(d.id);
    QString label = d.displayName;
    // From the local server's well-known port we show its name (the descriptor's name is generic).
    const QUrl url(cfg.baseUrl.trimmed());
    if (d.authMode != providers::AuthMode::Login && providers::isLocalEndpoint(cfg.baseUrl)) {
        if (url.port() == 1234) label = QStringLiteral("LM Studio");
        else if (url.port() == 11434) label = QStringLiteral("Ollama");
    }
    if (m_kind == providers::ProviderKind::Stt || d.authMode == providers::AuthMode::Login) return label;
    QString model = cfg.model.trimmed();
    model = model.mid(model.lastIndexOf(QLatin1Char('/')) + 1);
    return model.isEmpty() ? label : label + QStringLiteral(" · ") + model;
}

void SettingsProviderModel::invalidateTest()
{
    // The fetch in progress belonged to the OLD address / provider: we drop it so its list does
    // not show under the new setting.
    if (m_fetchId) {
        if (m_tester) m_tester->cancel(m_fetchId);
        m_fetchId = 0;
        emit fetchChanged();
    }
    if (m_testId && m_tester) m_tester->cancel(m_testId);
    m_testId = 0;
    clearServerInfo();   // the address / model may have changed; the old server data would mislead
    if (m_testState.isEmpty()) return;
    m_testState.clear();
    m_result = {};
    emit testChanged();
}

void SettingsProviderModel::test()
{
    if (!testable() || m_testState == QLatin1String("testing")) return;
    m_result = {};
    m_testState = QStringLiteral("testing");
    emit testChanged();
    if (!m_vm->controller()) {
        // Demo: no network; a fictional but realistic result.
        providers::ConnectionTestResult r;
        r.status = providers::ConnectionTestResult::Status::Ok;
        r.latencyMs = m_kind == providers::ProviderKind::Stt ? 210 : 48;
        m_result = r;
        m_testState = QStringLiteral("ok");
        emit testChanged();
        return;
    }
    m_testId = m_tester->test(descriptor(), runtimeConfig());
}

void SettingsProviderModel::fetchModels()
{
    if (!testable() || m_fetchId) return;
    m_fetchError.clear();
    if (!m_vm->controller()) {
        m_models = m_kind == providers::ProviderKind::Embedding
            ? QStringList{QStringLiteral("text-embedding-nomic-embed-text-v1.5"),
                          QStringLiteral("nomic-embed-text-v2-moe"), QStringLiteral("text-embedding-bge-m3")}
            : QStringList{QStringLiteral("google/gemma-4-12b-qat"), QStringLiteral("qwen/qwen3-coder-30b")};
        m_modelsFor = providerId();
        emit fetchChanged();
        bumpValues();
        return;
    }
    m_fetchId = m_tester->test(descriptor(), runtimeConfig());
    emit fetchChanged();
}

void SettingsProviderModel::onFinished(int id, const providers::ConnectionTestResult& result)
{
    if (id == m_fetchId) {
        m_fetchId = 0;
        if (result.ok()) {
            m_models = result.models;
            m_modelsFor = providerId();
            m_fetchError = m_models.isEmpty() ? tr("A szolgáltató üres modell-listát adott.") : QString();
            bumpValues();
        } else {
            m_fetchError = tr("Nem sikerült lekérni a modelleket: %1").arg(result.message);
        }
        emit fetchChanged();
        return;
    }
    if (id != m_testId) return;
    m_testId = 0;
    m_result = result;
    m_testState = result.ok() ? QStringLiteral("ok") : QStringLiteral("failed");
    if (!result.ok()) setExpanded(true);
    if (result.ok() && m_kind == providers::ProviderKind::Llm) setServerInfo(result.server, /*withWarnings*/ false);
    // A successful probe's model list also updates the "Fetch" result (same request).
    if (result.ok() && !result.models.isEmpty()) {
        m_models = result.models;
        m_modelsFor = providerId();
        bumpValues();
    }
    emit testChanged();
    m_vm->updateServicesWarn();
}

void SettingsProviderModel::setDemoResult(const QString& state, int latencyMs,
                                          const QString& message, const QString& code)
{
    m_result = {};
    m_result.latencyMs = latencyMs;
    m_result.message = message;
    m_result.code = code;
    m_result.status = state == QLatin1String("ok") ? providers::ConnectionTestResult::Status::Ok
                                                   : providers::ConnectionTestResult::Status::Unreachable;
    m_testState = state;
    if (state == QLatin1String("failed")) setExpanded(true);
    emit testChanged();
}

void SettingsProviderModel::reset()
{
    if (m_tester) m_tester->cancelAll();
    m_testId = m_fetchId = 0;
    m_testState.clear();
    m_result = {};
    m_fetchError.clear();
    m_models.clear();
    m_modelsFor.clear();
    clearServerInfo();
    // A configured role starts collapsed; an incomplete / highlighted one open.
    setExpanded(!configured() || highlighted());
    emit providersChanged();
    emit providerChanged();
    emit fieldsChanged();
    bumpValues();
    emit testChanged();
    emit fetchChanged();
    emit highlightedChanged();
}

} // namespace tanara_qml
