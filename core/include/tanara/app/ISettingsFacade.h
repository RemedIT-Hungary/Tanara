#pragma once
//
// ISettingsFacade: what the Settings window and the "Getting started" window need from the core,
// as ONE abstract surface: the effective settings and their saving, the secrets (API keys), the
// own name, the default sources and the level meters, the own voiceprints, the folders, the
// provider registry with the connection test, the call detector, the library preparation
// (embeddings), the rejected tag suggestions and the Tanara Cloud account.
//
// The real implementation composes SettingsManager, the provider registry, the recording service,
// the voiceprint store, the tag service, the embedding preparer and the detector registry
// (gui/qml/src/SettingsServicesFacade); the fictional one (demo/SettingsDemoFacade) serves --demo,
// --gallery, --qml-shot and the tests: it keeps everything in memory, never touches the network or
// the disk, and describes the design state it was made for (scenario()). It is a QObject only
// because it carries signals.
#include "tanara/app/AppSettings.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/library/index/EmbeddingPreparer.h"
#include "tanara/providers/chat/LlmServer.h"
#include "tanara/providers/core/ConnectionTester.h"
#include "tanara/providers/core/ProviderRegistry.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace tanara::recording { class IRecorderFacade; }
namespace tanara::watcher { class IMeetingDetector; }

namespace tanara::app {

// A folder's size and (for a recording root) its meeting folders.
struct FolderUsage {
    qint64 bytes = 0;
    int meetings = 0;
    bool exists = false;
};

// The own person's voiceprints: samples (per-model siblings count once) and source meetings.
struct VoiceprintSummary {
    int samples = 0;
    int meetings = 0;
};

// The Tanara Cloud account as the Settings > Tanara Cloud page sees it (cloud::CloudSession's
// last known state, plus the cloud mode).
struct CloudAccountState {
    QString mode{QStringLiteral("off")};   // "off" | "teaser" | "live" (cloud::CloudMode)
    bool signedIn = false;
    QString email;
    cloud::AccountInfo account;            // last known (valid = false: not fetched yet)
    QVector<cloud::CloudModel> models;     // last known catalog (empty: not fetched yet)
    bool signingIn = false;                // the device flow runs: the code below is shown
    cloud::DeviceCode deviceCode;
    QString error;                         // the last call's error, one sentence ("" = none)
};

// The design state of a fictional facade (screenshots, the demo): what the window shows on top
// of the facade's data. The real facade has none.
struct SettingsScenario {
    // ---- Settings window ----
    QString page;          // "" -> general; a page key or an old name (as SettingsViewModel::openPage)
    QString section;       // the page's jump point ("" -> the old name's / the focusField's)
    QString focusField;    // "" | "stt" | "llm"
    // The unsaved draft (differs from the saved settings), if the state shows one.
    std::optional<AppSettings> draft;
    bool liveCallActive = false;
    QString liveCallApp, liveCallAppId;
    // A connection test's shown result ("ok" | "failed").
    struct TestResult {
        QString state;
        int latencyMs = -1;
        QString message, code;
    };
    std::optional<TestResult> sttTest, llmTest;
    bool llmAdvancedOpen = false;
    std::optional<providers::llmctx::LlmServerInfo> llmServer;
    int promptIndex = 0;
    bool summaryAdvancedOpen = false;   // the summary card's "Haladó" block (prompts) is open

    // ---- "Getting started" window ----
    QString onboardingStep;                       // "" -> the first step
    QHash<QString, QString> onboardingStatus;     // step -> "done" | "skipped"
};

class ISettingsFacade : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~ISettingsFacade() override = default;

    // False: fictional data; nothing reaches the disk, the network or the system.
    virtual bool live() const { return true; }
    // The fictional facade's design state; the real facade has none.
    virtual std::optional<SettingsScenario> scenario() const { return std::nullopt; }

    // ---- settings ----
    virtual AppSettings settings() const = 0;
    virtual void setSettings(const AppSettings& settings) = 0;
    // The own name: carried through the person DB, the meetings and the voiceprints.
    virtual void setUserSpeakerName(const QString& name) = 0;
    // A summary prompt's default text (the file override in the data folder, or the built-in one).
    virtual QString promptDefault(const QString& id) const = 0;
    // The effective folder of a role without a "paths" setting (with TANARA_HOME: the sandbox's).
    virtual QString defaultFolder(paths::Role role) const = 0;
    // The data folder is pinned by TANARA_HOME (the sandbox).
    virtual bool dataDirPinned() const = 0;
    // The UI language currently running ("hu" | "en"; empty: unknown).
    virtual QString activeUiLanguage() const = 0;
    // Folder size (recursive) + meeting folders (countMeetings). Called from a worker thread too.
    virtual FolderUsage folderUsage(const QString& path, bool countMeetings) const = 0;
    // The meeting folders directly under a recording root.
    virtual int meetingFolderCount(const QString& path) const = 0;
    // Starting the watcher at sign-in (immediately; never in a sandbox / test).
    virtual void applyWatcherAutostart(bool on) = 0;

    // ---- secrets (API keys; the SecretStore, not settings.json) ----
    virtual QString secret(const QString& key) const = 0;
    virtual bool hasSecret(const QString& key) const = 0;
    virtual void setSecret(const QString& key, const QString& value) = 0;
    // "keychain" | "file", and why the keys are in a file although the keychain was wanted.
    virtual QString secretBackend() const = 0;
    virtual QString secretBackendWarning() const = 0;

    // ---- devices (the default sources), level meters ----
    // The device list, renames, the default selection (shared with the recorder).
    virtual recording::IRecorderFacade* devices() const = 0;
    // Level metering while at least one owner asks for it.
    virtual void retainLevelMonitoring(QObject* owner) = 0;
    virtual void releaseLevelMonitoring(QObject* owner) = 0;

    // ---- own voiceprints ----
    virtual VoiceprintSummary ownVoiceprints() const = 0;

    // ---- providers ----
    virtual const providers::ProviderRegistry& registry() const = 0;
    // Starts a connection probe; the result comes in connectionTested(id, ...) (never synchronously).
    virtual int testConnection(const providers::ProviderDescriptor& descriptor,
                               const providers::ProviderConfig& config) = 0;
    virtual void cancelConnectionTest(int id) = 0;
    // The local LLM server's native data (LM Studio); the result comes in llmServerProbed.
    virtual void probeLlmServer(const providers::ProviderConfig& config) = 0;

    // ---- call watcher ----
    // A detector for the live call indicator (nullptr: none on this platform). The caller owns it.
    virtual watcher::IMeetingDetector* createDetector(const QString& detectorId) const = 0;
    // The running processes' names (the "add an application" list).
    virtual QStringList runningApps() const = 0;

    // ---- library preparation (embeddings), tag suggestions ----
    virtual library::EmbeddingState embeddingState() const = 0;
    virtual void startEmbedding() = 0;
    virtual void cancelEmbedding() = 0;
    virtual void resumeEmbedding() = 0;
    // Drops every embedding and prepares the library again.
    virtual void reprepareEmbeddings() = 0;
    virtual int rejectedSuggestionCount() const = 0;
    virtual void clearRejectedSuggestions() = 0;

    // ---- Tanara Cloud: the account (Settings > Tanara Cloud) ----
    // The defaults describe a build without a cloud client ("off"); the calls only act in live mode.
    virtual CloudAccountState cloudState() const { return {}; }
    // Starts the device-code sign-in (cloudStateChanged carries the code, then the signed-in state).
    virtual void cloudSignIn() {}
    virtual void cloudCancelSignIn() {}
    virtual void cloudSignOut() {}
    // Refreshes the account and the catalog (asynchronous; cloudStateChanged when they arrive).
    virtual void cloudRefresh() {}
    // Asks for a one-time top-up link; the result comes in cloudTopupReady (or an error in the state).
    virtual void cloudRequestTopup() {}

signals:
    void settingsChanged();
    // The people / their voiceprints changed (the own voiceprint line).
    void voiceprintsChanged();
    void connectionTested(int id, const tanara::providers::ConnectionTestResult& result);
    void llmServerProbed(const tanara::providers::llmctx::LlmServerInfo& info);
    void embeddingStateChanged();
    void rejectedSuggestionsChanged();
    void cloudStateChanged();
    void cloudTopupReady(const QString& url);
};

} // namespace tanara::app
