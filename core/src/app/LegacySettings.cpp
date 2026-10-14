#include "tanara/app/LegacySettings.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/domain/Voice.h"

#include <QDir>
#include <QJsonArray>

namespace tanara::app::legacy {

namespace {

QStringList toList(const QJsonValue& v)
{
    QStringList out;
    for (const QJsonValue& e : v.toArray()) out << e.toString();
    return out;
}

QMap<QString, providers::ProviderConfig> configsFromJson(const QJsonObject& o)
{
    QMap<QString, providers::ProviderConfig> configs;
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        configs.insert(it.key(), providers::ProviderConfig::fromJson(it.value().toObject()));
    return configs;
}

// "~"-shaped value kept as written; empty stays empty.
QString str(const QJsonObject& o, const char* key) { return o.value(QLatin1String(key)).toString().trimmed(); }

} // namespace

bool isFlat(const QJsonObject& o)
{
    return o.value(QStringLiteral("version")).toInt(0) != 2;
}

paths::PathSettings legacyPaths(const QJsonObject& flat, const QString& home)
{
    const QDir h(home);
    paths::PathSettings p;
    const QString meta = str(flat, "metadataDir");
    p.dataDir = meta.isEmpty() ? h.filePath(QStringLiteral(".tanara")) : meta;
    p.modelsDir = QDir::cleanPath(p.dataDir + QStringLiteral("/models"));   // keeps a "~" form
    const QString audio = str(flat, "audioDir");
    p.recordingRoots = {paths::RecordingRoot{
        audio.isEmpty() ? h.filePath(QStringLiteral("Tanara/recordings")) : audio, true}};
    const QString notes = str(flat, "notesDir");
    p.notesDir = notes.isEmpty() ? h.filePath(QStringLiteral("Tanara/notes")) : notes;
    return p;
}

AppSettings importFlat(const QJsonObject& o, const AppSettings& base)
{
    AppSettings s = base;
    auto has = [&o](const char* k) { return o.contains(QLatin1String(k)); };

    // recording
    if (has("userSpeakerName") && !str(o, "userSpeakerName").isEmpty())
        s.recording.userSpeakerName = str(o, "userSpeakerName");
    s.recording.autoRecordAllDevices = o.value(QStringLiteral("autoRecordAllDevices")).toBool(s.recording.autoRecordAllDevices);
    s.recording.audioQuality = o.value(QStringLiteral("audioQuality")).toString(s.recording.audioQuality);
    s.recording.mixdownMode = o.value(QStringLiteral("mixdownMode")).toString(s.recording.mixdownMode);
    if (has("deviceNames")) {
        s.recording.deviceNames.clear();
        const QJsonObject names = o.value(QStringLiteral("deviceNames")).toObject();
        for (auto it = names.constBegin(); it != names.constEnd(); ++it) {
            const QString name = it.value().toString().simplified();
            if (!it.key().isEmpty() && !name.isEmpty()) s.recording.deviceNames.insert(it.key(), name);
        }
    }

    // processing
    if (has("languageHints")) s.processing.languageHints = toList(o.value(QStringLiteral("languageHints")));
    s.processing.summaryLanguage = o.value(QStringLiteral("summaryLanguage")).toString(s.processing.summaryLanguage);
    s.processing.summaryPrompt = o.value(QStringLiteral("summaryPrompt")).toString(s.processing.summaryPrompt);
    s.processing.notesPrompt = o.value(QStringLiteral("notesPrompt")).toString(s.processing.notesPrompt);
    s.processing.mergePrompt = o.value(QStringLiteral("mergePrompt")).toString(s.processing.mergePrompt);
    s.processing.topicExtractionPrompt =
        o.value(QStringLiteral("topicExtractionPrompt")).toString(s.processing.topicExtractionPrompt);
    s.processing.topicAnalysisPrompt =
        o.value(QStringLiteral("topicAnalysisPrompt")).toString(s.processing.topicAnalysisPrompt);
    if (has("voiceModels"))
        s.processing.voiceModels = voicemodels::normalizeIds(toList(o.value(QStringLiteral("voiceModels"))));
    s.processing.tagSuggestions = o.value(QStringLiteral("tagSuggestions")).toBool(s.processing.tagSuggestions);
    s.processing.llmTagSuggestions = o.value(QStringLiteral("llmTagSuggestions")).toBool(s.processing.llmTagSuggestions);

    // providers: the newer flat shape (sttProviders) or the oldest single `stt` / `llm` object.
    if (has("sttProviders")) {
        s.providers.stt.selectedId = o.value(QStringLiteral("sttProviderId")).toString(s.providers.stt.selectedId);
        s.providers.stt.configs = configsFromJson(o.value(QStringLiteral("sttProviders")).toObject());
    } else if (has("stt")) {
        const auto cfg = providers::ProviderConfig::fromJson(o.value(QStringLiteral("stt")).toObject());
        const QString id = cfg.type.isEmpty() ? QStringLiteral("soniox") : cfg.type;
        s.providers.stt.selectedId = id;
        s.providers.stt.configs = {{id, cfg}};
    }
    if (has("llmProviders")) {
        s.providers.llm.selectedId = o.value(QStringLiteral("llmProviderId")).toString(s.providers.llm.selectedId);
        s.providers.llm.configs = configsFromJson(o.value(QStringLiteral("llmProviders")).toObject());
    } else if (has("llm")) {
        const auto cfg = providers::ProviderConfig::fromJson(o.value(QStringLiteral("llm")).toObject());
        const QString id = cfg.type.isEmpty() ? QStringLiteral("openai-compat") : cfg.type;
        s.providers.llm.selectedId = id;
        s.providers.llm.configs = {{id, cfg}};
    }
    if (has("embeddingProviderId"))
        s.providers.embedding.selectedId = o.value(QStringLiteral("embeddingProviderId")).toString();
    if (has("embeddingProviders")) {
        const auto cfgs = configsFromJson(o.value(QStringLiteral("embeddingProviders")).toObject());
        for (auto it = cfgs.constBegin(); it != cfgs.constEnd(); ++it)
            s.providers.embedding.configs.insert(it.key(), it.value());
    }

    // watcher
    s.watcher.detectorEnabled = o.value(QStringLiteral("detectorEnabled")).toBool(s.watcher.detectorEnabled);
    s.watcher.detectorIntervalSec = o.value(QStringLiteral("detectorIntervalSec")).toInt(s.watcher.detectorIntervalSec);
    s.watcher.autostart = o.value(QStringLiteral("watcherAutostart")).toBool(s.watcher.autostart);
    s.watcher.askStopOnCallEnd = o.value(QStringLiteral("askStopOnCallEnd")).toBool(s.watcher.askStopOnCallEnd);
    s.watcher.silenceAskMinutes = o.value(QStringLiteral("silenceAskMinutes")).toInt(s.watcher.silenceAskMinutes);
    s.watcher.detectorId = o.value(QStringLiteral("detectorId")).toString(s.watcher.detectorId);
    if (has("knownCallApps")) s.watcher.knownCallApps = toList(o.value(QStringLiteral("knownCallApps")));

    // cloud
    s.cloud.enabled = o.value(QStringLiteral("cloudEnabled")).toBool(s.cloud.enabled);
    s.cloud.baseUrl = o.value(QStringLiteral("cloudBaseUrl")).toString(s.cloud.baseUrl);
    s.cloud.sttTier = o.value(QStringLiteral("cloudSttTier")).toString(s.cloud.sttTier);
    s.cloud.llmTier = o.value(QStringLiteral("cloudLlmTier")).toString(s.cloud.llmTier);
    s.cloud.sttModel = o.value(QStringLiteral("cloudSttModel")).toString(s.cloud.sttModel);
    s.cloud.llmModel = o.value(QStringLiteral("cloudLlmModel")).toString(s.cloud.llmModel);
    s.cloud.waitlistEmail = o.value(QStringLiteral("waitlistEmail")).toString(s.cloud.waitlistEmail);
    s.cloud.estimateBeforeRun = o.value(QStringLiteral("cloudEstimateBeforeRun")).toBool(s.cloud.estimateBeforeRun);

    // ui
    s.ui.language = o.value(QStringLiteral("uiLanguage")).toString(s.ui.language);
    s.ui.onboardingDone = o.value(QStringLiteral("onboardingDone")).toBool(s.ui.onboardingDone);
    s.ui.reviewAutoPlay = o.value(QStringLiteral("reviewAutoPlay")).toBool(s.ui.reviewAutoPlay);
    return s;
}

} // namespace tanara::app::legacy
