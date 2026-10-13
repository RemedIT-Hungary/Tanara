#pragma once
//
// Transitional COLLECTOR header (cleanup phase 2, P4). The shared value types now live in the domain
// module (tanara/domain/*.h); this header only includes them, plus the two settings types that have
// not moved yet: ProviderConfig (-> providers, P9) and AppSettings (-> app, P6).
// RULE: core code includes the precise domain headers; only code that needs ProviderConfig /
// AppSettings / opusBitrateKbps includes this file. gui / cli / tests may still use it as a collector.
#include "tanara/domain/Activity.h"
#include "tanara/domain/Audio.h"
#include "tanara/domain/Job.h"
#include "tanara/domain/Meeting.h"
#include "tanara/domain/Overlay.h"
#include "tanara/domain/Participant.h"
#include "tanara/domain/Summary.h"
#include "tanara/domain/Tag.h"
#include "tanara/domain/Transcript.h"
#include "tanara/domain/Voice.h"
#include "tanara/foundation/Version.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QMetaType>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>

namespace tanara {

struct HttpExchange;   // tanara/cloud/CloudTypes.h

// ---- provider configuration / settings ----------------------------------------
struct ProviderConfig {
    QString type;            // "soniox" | "openai-compat" | ...
    QString baseUrl;
    QString apiKey;          // runtime only; goes to the keychain when stored
    QString model;
    double temperature = 0.2; // LLM sampling temperature (summary); not used by STT
    int maxTokens = 8000;     // max tokens of the LLM reply (plenty for a reasoning model); not used by STT
    // The model's "thinking" (reasoning), LLM only. Measured: it must be OFF for the summary
    // (otherwise the output budget is spent on thinking, and it is 2-5x slower):
    //  "auto" - off; the method depends on the model name (Gemma: reasoning_effort "none";
    //           Qwen 3+: assistant message prefilled with an empty <think> block; unknown:
    //           reasoning_effort "none", which servers that do not know it ignore),
    //  "off"  - same as auto (explicit off),
    //  "on"   - no switch is sent: the model's default behavior (a thinking model thinks).
    QString reasoning{QStringLiteral("auto")};
    // The model's context (in tokens), LLM only. 0 = automatic: at task start Tanara requests
    // the smallest standard step (8k / 16k / 20k / 32k ...) that covers the task (up to the
    // model's maximum). Fixed value: always loaded with that size. Only effective with
    // LM Studio (loaded via its native API); for other servers set the load there.
    int contextLength = 0;
    QVariantMap extra;

    // --- runtime (NOT persisted) gateway hooks, filled only by the Tanara Cloud path ---
    // Headers added to every request (X-Tanara-Client, Accept-Language, X-Tanara-Job-Id ...).
    QList<QPair<QByteArray, QByteArray>> extraHeaders;
    // Feedback of every HTTP response (success, error, network error) to the caller; the cloud
    // layer reads the load, the write-back, the request id and the structured error from it.
    std::function<void(const HttpExchange&)> onExchange;
};

struct AppSettings {
    QString audioDir;            // where recordings are saved
    QString notesDir;            // where the summary .md copy goes (vault Meetings/)
    QString metadataDir;         // pl. ~/.tanara
    QString userSpeakerName;     // fixed name of the mic track (e.g. "Adam")
    bool autoRecordAllDevices = true;  // true = record every device (silent ones are dropped afterwards)
    QStringList languageHints{QStringLiteral("hu")};

    // UI language: "auto" (system locale decides) | "hu" | "en". The source language is Hungarian;
    // a restart is needed after switching (no runtime retranslate). See Localization.h.
    QString uiLanguage{QStringLiteral("auto")};

    // Recording audio quality (per-track Opus bitrate). The raw per-track .ogg files are also
    // used by the STT, so even the lowest step is "STT-safe" (24 kbps). Lower step =
    // smaller files. Values: "best"|"high"|"medium"|"low".
    QString audioQuality{QStringLiteral("best")};

    // Timing of the mixdown after recording stops. "auto" = in the background,
    // non-blocking, immediately; "manual" = only on manual start (the review panel's button).
    // The mixdown is needed ONLY for listening (the STT works from the per-track .ogg files),
    // so stop() never waits for it and the main thread does not freeze.
    QString mixdownMode{QStringLiteral("auto")};

    // System prompts of the summary LLM. EMPTY = the file override / built-in default
    // (PromptLibrary) applies (so future improvements of the code default take effect
    // automatically until the user writes their own).
    //  summaryPrompt - single-step prompt for a meeting with ONE part (short) ("single":
    //                  memo notes + executive summary and lists in one call),
    //  notesPrompt   - per-part notes for a longer meeting ("notes"),
    //  mergePrompt   - combining the part notes ("merge").
    QString summaryPrompt;
    QString notesPrompt;
    QString mergePrompt;

    // The two editable prompts of the complex (multi-pass) summary. EMPTY = the code default
    // (ComplexSummaryService::defaultTopicPrompt() / defaultAnalysisPrompt()).
    QString topicExtractionPrompt;   // pass 1: topic extraction
    QString topicAnalysisPrompt;     // pass 2: per-topic analysis

    // The summary's TARGET LANGUAGE (free text, e.g. Hungarian, English or German); it goes into
    // the prompts' {{NYELV}} placeholder (PromptLibrary::applySummaryLanguage).
    // Independent of the UI language. The structural section headings (the Hungarian decisions / action-item headings)
    // stay Hungarian regardless of the language (the parser matches on them).
    QString summaryLanguage{QStringLiteral("magyar")};

    // Multi-provider: the selected provider id per type + per-provider config (so switching
    // does not delete the other provider's settings). The old single `stt`/`llm` shape is
    // migrated from the JSON (see JsonSerialization).
    // Tanara Cloud (the gateway contract: docs/cloud-gateway-api.yaml).
    //  cloudEnabled - the sign-in cloud mode is live ("after launch"); false = the Settings
    //                 Tanara Cloud section shows the "Coming soon" (waitlist) panel.
    //                 The TANARA_CLOUD_URL environment variable overrides it for development (turns it on).
    //  cloudBaseUrl - gateway base URL (without /v1); empty = the production default.
    //  cloudSttTier / cloudLlmTier - Fast / Accurate tier per task (fast | accurate).
    //  cloudSttModel / cloudLlmModel - Expert mode: a concrete catalog id; empty = the tier's virtual model.
    //  waitlistEmail - address subscribed from the "Coming soon" panel (non-empty = do not ask again).
    bool    cloudEnabled = false;
    QString cloudBaseUrl;
    QString cloudSttTier{QStringLiteral("accurate")};
    QString cloudLlmTier{QStringLiteral("accurate")};
    QString cloudSttModel;
    QString cloudLlmModel;
    QString waitlistEmail;
    //  cloudEstimateBeforeRun - always show a cost estimate + confirmation before Tanara Cloud
    //                 processing (on by default). When off, the run starts without an estimate; the
    //                 error branches (out of balance, terms of service) still stop it.
    bool    cloudEstimateBeforeRun = true;

    // User-facing names of audio devices: raw OS device name -> friendly name. Shown everywhere
    // the device or its track appears by name (recorder, Tracks tab, notification).
    // Resolution happens in ONE place: tanara::devicenames (audio/TrackCatalog.h); the
    // SettingsManager loads it on every load / save.
    QMap<QString, QString> deviceNames;

    // The "Getting started" window (OnboardingWindow.qml) has already run: it was closed, skipped
    // or completed. Only turns off the automatic opening (once on first launch); it can be
    // reopened any time from File > "Getting started..." and from Settings.
    bool onboardingDone = false;

    QString sttProviderId{QStringLiteral("soniox")};
    QString llmProviderId{QStringLiteral("openai-compat")};
    QMap<QString, ProviderConfig> sttConfigs;   // id -> config
    QMap<QString, ProviderConfig> llmConfigs;   // id -> config

    // Back-compat accessors: the runtime config of the selected provider.
    // Most call sites can switch to these (s.stt -> s.sttSelected()).
    ProviderConfig sttSelected() const { return sttConfigs.value(sttProviderId); }
    ProviderConfig llmSelected() const { return llmConfigs.value(llmProviderId); }

    // Embedding model for tag suggestions. Empty id = none ("basic" level: suggestions come
    // from shared participants, phrases and the title). See embedding/EmbeddingProviderRegistry.h:
    // "openai-compat-embedding" | "tanara-hosted-embedding".
    QString embeddingProviderId;
    QMap<QString, ProviderConfig> embeddingConfigs;   // id -> config
    ProviderConfig embeddingSelected() const { return embeddingConfigs.value(embeddingProviderId); }
    // Tag suggestions on/off, and whether the language model suggests after the summary.
    bool tagSuggestions = true;
    bool llmTagSuggestions = true;

    // To review (focus view): the line stepped onto on the card plays by itself (line + 0.3 s).
    // Written by the view header's toggle; on by default.
    bool reviewAutoPlay = true;

    // Enabled speaker-embedding models (VoiceModelRegistry ids), alphabetical. Every one counts
    // for voiceprints and analysis (fusion); a missing model file is skipped at run time, the
    // setting stays. The "voiceModels" key of settings.json; missing = ["wespeaker-resnet34-lm"].
    QStringList voiceModels{QStringLiteral("wespeaker-resnet34-lm")};

    // Meeting watcher (background detector + tray). The watcher reads it; the analyzer/recorder
    // does not depend on it. Empty detectorId = the registry picks the first available detector.
    bool detectorEnabled = true;              // active-call detection on/off
    int  detectorIntervalSec = 8;             // poll interval (s); sensible range 5-30
    bool watcherAutostart = false;            // whether the watcher starts at login
    // During recording the recorder also watches the call: when it ends (the app quits / releases
    // the microphone), it asks whether to stop. It never stops by itself.
    bool askStopOnCallEnd = true;
    // Silence watch during recording: if ALL tracks stay silent this many minutes, it asks
    // whether to stop (with Teams/browser, mic release is not always visible to the detector;
    // this net is INDEPENDENT of the detector). 0 = off.
    int  silenceAskMinutes = 3;
    QString detectorId;                       // empty = MeetingDetectorRegistry::createBest()
    // Known call apps (binary/app-name fragments); the detector signals on these.
    QStringList knownCallApps{
        QStringLiteral("zoom"), QStringLiteral("teams"), QStringLiteral("webex"),
        QStringLiteral("slack"), QStringLiteral("discord"), QStringLiteral("meet"),
        QStringLiteral("skype"), QStringLiteral("chromium"), QStringLiteral("firefox")};
};

// Recording quality step -> per-track Opus bitrate (kbps). Unknown = 64 ("best").
// The lowest step (24 kbps) is the "STT floor": we do not go below it, so the accuracy of
// transcription working from the per-track .ogg does not degrade.
inline int opusBitrateKbps(const QString& quality) {
    if (quality == QStringLiteral("low"))    return 24;
    if (quality == QStringLiteral("medium")) return 32;
    if (quality == QStringLiteral("high"))   return 48;
    return 64;   // "best" / unknown
}

} // namespace tanara

Q_DECLARE_METATYPE(tanara::ProviderConfig)
Q_DECLARE_METATYPE(tanara::AppSettings)
