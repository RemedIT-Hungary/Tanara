#pragma once
//
// SettingsViewModel: view model of the Settings window (SettingsWindow.qml).
//
// Draft model: on opening, two copies of the core's settings are made: `base` (what is on
// disk) and `draft` (what the user edits). The pages write the draft; "Save" applies the
// DIFFERENCE onto the core's currently effective settings (so what this window does not show,
// or what someone else changed meanwhile, is not lost); "Cancel" discards it. Exception: the
// theme shows immediately (preview) but is kept only on save.
//
// Besides the settings, the draft includes: the secrets (API keys: they go to the SecretStore,
// not settings.json), the default sources selection (state.json, shared with the recorder)
// and the theme (ui-state.json).
//
// Sub-models: `devices` (device list with live level), `stt` / `llm` (provider cards from the
// provider registry), `embedding` (the Embedding
// card, library preparation and tag-suggestion switches), `voiceModels` (the Speakers page's
// voice models and the backfill state).
//
// It works on a tanara::app::ISettingsFacade (and the voice-model list on an IVoiceModelsFacade):
// the QML default constructor takes them from AppContext::services(); on the fictional facades
// (tanara_demo: --qml-shot / --demo, tests) `demoState` ("B01" ... "B07", "B07notes", "advanced",
// "dirty", "unsaved", "schema", "cloudComingSoon", "cloudSelected", "B04embedding" /
// "B04embeddingRunning" / "B04embeddingError" / "B04embeddingDone" / "B04embeddingCloud" /
// "B04embeddingNone" / "B04modelChange", "B08" / "B08download" / "B08none" / "B08fallback" /
// "B08error" / "B08offline" / "B08backfill" / "B08partial" / "B08bundled") picks the design state.
//
#include "SettingsDeviceModel.h"
#include "tanara/app/AppSettings.h"
#include "SettingsEmbeddingModel.h"
#include "SettingsProviderModel.h"
#include "VoiceModelListModel.h"

#include "tanara/app/ISettingsFacade.h"
#include "tanara/providers/core/ReadinessModel.h"

#include <QAbstractItemModel>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace tanara {
namespace watcher { class IMeetingDetector; }
}

namespace tanara_qml {

class SettingsDialogs;
struct UiServices;

class SettingsViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* dialogs READ dialogsObject WRITE setDialogsObject NOTIFY dialogsChanged)
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)
    // True: fictional facade, saving happens in memory only (demo / screenshot).
    Q_PROPERTY(bool demo READ demo NOTIFY facadeChanged)

    // ---- window ----
    // "general" | "recording" | "watcher" | "services" | "speakers" | "summary"
    Q_PROPERTY(QString page READ page WRITE setPage NOTIFY pageChanged)
    // Deep link: "" | "stt" | "llm": the card whose missing setting we came for.
    Q_PROPERTY(QString focusField READ focusField NOTIFY focusChanged)
    Q_PROPERTY(QString focusBannerText READ focusBannerText NOTIFY focusChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(int changeCount READ changeCount NOTIFY dirtyChanged)
    Q_PROPERTY(QString footerText READ footerText NOTIFY dirtyChanged)
    // The warning dot next to "Services": a required provider is incomplete or the latest
    // connection test failed.
    Q_PROPERTY(bool servicesWarn READ servicesWarn NOTIFY servicesWarnChanged)
    // Where the API keys are: "keychain" | "file" (foundation/secrets), the one-line text for the
    // Services page, and the warning when the keychain was wanted but the keys are in a file.
    Q_PROPERTY(QString secretsBackend READ secretsBackend NOTIFY facadeChanged)
    Q_PROPERTY(QString secretsLocationText READ secretsLocationText NOTIFY facadeChanged)
    Q_PROPERTY(QString secretsWarning READ secretsWarning NOTIFY facadeChanged)
    // Field errors: { "userName": "...", "folder.audio": "...", "watchedApps": "..." }.
    Q_PROPERTY(QVariantMap errors READ errors NOTIFY errorsChanged)

    // ---- General ----
    Q_PROPERTY(QString userName READ userName WRITE setUserName NOTIFY generalChanged)
    Q_PROPERTY(QString voiceprintText READ voiceprintText NOTIFY voiceprintChanged)
    Q_PROPERTY(bool hasVoiceprint READ hasVoiceprint NOTIFY voiceprintChanged)
    Q_PROPERTY(QString uiLanguage READ uiLanguage WRITE setUiLanguage NOTIFY generalChanged)
    Q_PROPERTY(QVariantList uiLanguageOptions READ uiLanguageOptions CONSTANT)
    // The language switches on the next start (the draft differs from the currently running language).
    Q_PROPERTY(bool languageNeedsRestart READ languageNeedsRestart NOTIFY generalChanged)
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    // [{ key: audio|notes|meta, label, path, usage, hint, error, locked }]
    Q_PROPERTY(QVariantList folders READ folders NOTIFY foldersChanged)
    // The further recording roots besides the primary one (the "audio" folder row):
    // [{ path, meetings, removable }]; removable only while the root holds no meeting.
    Q_PROPERTY(QVariantList recordingRoots READ recordingRoots NOTIFY foldersChanged)
    // The "Getting started" window can be reopened from here too (only in the main window's process; shown in the demo).
    Q_PROPERTY(bool onboardingAvailable READ onboardingAvailable NOTIFY dialogsChanged)

    // ---- Recording ----
    Q_PROPERTY(QAbstractItemModel* devices READ devicesModel CONSTANT)
    Q_PROPERTY(int deviceCount READ deviceCount NOTIFY deviceCountChanged)
    // Level monitoring runs only while the page is visible (QML sets it).
    Q_PROPERTY(bool monitoring READ monitoring WRITE setMonitoring NOTIFY monitoringChanged)
    Q_PROPERTY(bool autoRecordAll READ autoRecordAll WRITE setAutoRecordAll NOTIFY recordingChanged)
    Q_PROPERTY(QString audioQuality READ audioQuality WRITE setAudioQuality NOTIFY recordingChanged)
    Q_PROPERTY(QVariantList audioQualityOptions READ audioQualityOptions CONSTANT)
    Q_PROPERTY(QString audioQualityHint READ audioQualityHint NOTIFY recordingChanged)
    Q_PROPERTY(QString mixdownMode READ mixdownMode WRITE setMixdownMode NOTIFY recordingChanged)

    // ---- Call watcher ----
    Q_PROPERTY(bool detectorEnabled READ detectorEnabled WRITE setDetectorEnabled NOTIFY watcherChanged)
    Q_PROPERTY(bool watcherAutostart READ watcherAutostart WRITE setWatcherAutostart NOTIFY watcherChanged)
    Q_PROPERTY(bool askStopOnCallEnd READ askStopOnCallEnd WRITE setAskStopOnCallEnd NOTIFY watcherChanged)
    Q_PROPERTY(int silenceAskMinutes READ silenceAskMinutes WRITE setSilenceAskMinutes NOTIFY watcherChanged)
    Q_PROPERTY(int detectorIntervalSec READ detectorIntervalSec WRITE setDetectorIntervalSec NOTIFY watcherChanged)
    // [{ match, label, active }]; active: this very application is using the microphone.
    Q_PROPERTY(QVariantList watchedApps READ watchedApps NOTIFY watchedAppsChanged)
    // Live detection (only while the page is visible: `watching`).
    Q_PROPERTY(bool watching READ watching WRITE setWatching NOTIFY watchingChanged)
    Q_PROPERTY(bool detectorAvailable READ detectorAvailable NOTIFY liveCallChanged)
    Q_PROPERTY(bool liveCallActive READ liveCallActive NOTIFY liveCallChanged)
    Q_PROPERTY(QString liveCallApp READ liveCallApp NOTIFY liveCallChanged)

    // ---- Services ----
    // "own" | "cloud" (S3: a shim over the routes until S2): "cloud" iff the stt or llm route is Cloud, or the
    // "coming soon" tab is shown (cloud not live). Setting "cloud" puts stt + llm on the Cloud route when
    // allowed (else only the tab changes); "own" moves a Cloud route back to the own provider.
    Q_PROPERTY(QString serviceMode READ serviceMode WRITE setServiceMode NOTIFY serviceModeChanged)
    Q_PROPERTY(tanara_qml::SettingsProviderModel* stt READ stt CONSTANT)
    Q_PROPERTY(tanara_qml::SettingsProviderModel* llm READ llm CONSTANT)
    // The draft's stt or llm route is Cloud.
    Q_PROPERTY(bool cloudSelected READ cloudSelected NOTIFY dirtyChanged)
    Q_PROPERTY(tanara_qml::SettingsEmbeddingModel* embedding READ embedding CONSTANT)

    // ---- Speakers: voice models (download immediate, enabling goes into the draft), backfill ----
    Q_PROPERTY(tanara_qml::VoiceModelListModel* voiceModels READ voiceModels CONSTANT)
    // D5: the local speaker separation (S3: read-only shim = the draft's diarization route is Local; set it
    // with setRoute("diarization", ...)) and the segmentation models' list (same rows as voiceModels, kind
    // "segmentation", no enabled switch).
    Q_PROPERTY(bool localDiarization READ localDiarization NOTIFY diarizationChanged)
    Q_PROPERTY(tanara_qml::VoiceModelListModel* segmentationModels READ segmentationModels CONSTANT)

    // ---- Summary ----
    Q_PROPERTY(QString summaryLanguage READ summaryLanguage WRITE setSummaryLanguage NOTIFY summaryChanged)
    Q_PROPERTY(QStringList summaryLanguageOptions READ summaryLanguageOptions CONSTANT)
    // The prompts in two groups: "quick" (quick summary: single / notes / merge) and "topics"
    // (per-topic analysis: topic / analysis). [{ value, label, note, tabs }]; the note is one
    // sentence on when which prompt runs; tabs is the index of the group's prompts (promptTabs).
    Q_PROPERTY(QVariantList promptGroups READ promptGroups CONSTANT)
    Q_PROPERTY(QString promptGroup READ promptGroup WRITE setPromptGroup NOTIFY promptIndexChanged)
    // [{ id, label, group, modified }]: all five prompts; promptIndex indexes into this list.
    Q_PROPERTY(QVariantList promptTabs READ promptTabs NOTIFY promptsChanged)
    Q_PROPERTY(int promptIndex READ promptIndex WRITE setPromptIndex NOTIFY promptIndexChanged)
    Q_PROPERTY(QString promptText READ promptText WRITE setPromptText NOTIFY promptTextChanged)
    Q_PROPERTY(bool promptModified READ promptModified NOTIFY promptsChanged)
    // [{ token, description }]: what the code really substitutes.
    Q_PROPERTY(QVariantList promptVariables READ promptVariables CONSTANT)
    Q_PROPERTY(QString schemaSummary READ schemaSummary NOTIFY promptIndexChanged)
    Q_PROPERTY(QString schemaBody READ schemaBody NOTIFY promptIndexChanged)
    Q_PROPERTY(QString schemaKind READ schemaKind NOTIFY promptIndexChanged)

public:
    // QML: the facades of AppContext::services() (the fictional ones follow demoState).
    explicit SettingsViewModel(QObject* parent = nullptr);
    // An explicit set (tests): its settings facade, and the voice-model list its voice-model facade.
    explicit SettingsViewModel(const UiServices& services, QObject* parent = nullptr);
    ~SettingsViewModel() override;

    tanara::app::ISettingsFacade* facade() const { return m_facade; }
    QObject* dialogsObject() const;
    void setDialogsObject(QObject* dialogs);
    SettingsDialogs* dialogs() const;
    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);
    bool demo() const { return !m_live; }

    QString page() const { return m_page; }
    void setPage(const QString& page);
    QString focusField() const { return m_focusField; }
    QString focusBannerText() const;
    bool dirty() const { return m_changeCount > 0; }
    int changeCount() const { return m_changeCount; }
    QString footerText() const;
    bool servicesWarn() const { return m_servicesWarn; }
    QString secretsBackend() const;
    QString secretsLocationText() const;
    QString secretsWarning() const;
    QVariantMap errors() const { return m_errors; }

    QString userName() const { return m_draft.recording.userSpeakerName; }
    void setUserName(const QString& name);
    QString voiceprintText() const { return m_voiceprintText; }
    bool hasVoiceprint() const { return m_hasVoiceprint; }
    QString uiLanguage() const { return m_draft.ui.language; }
    void setUiLanguage(const QString& lang);
    QVariantList uiLanguageOptions() const;
    bool languageNeedsRestart() const;
    QString themeMode() const { return m_draftTheme; }
    void setThemeMode(const QString& mode);
    QVariantList folders() const;
    QVariantList recordingRoots() const;
    bool onboardingAvailable() const;

    QAbstractItemModel* devicesModel() const;
    int deviceCount() const;
    bool monitoring() const { return m_monitoring; }
    void setMonitoring(bool on);
    bool autoRecordAll() const { return m_draft.recording.autoRecordAllDevices; }
    void setAutoRecordAll(bool on);
    QString audioQuality() const { return m_draft.recording.audioQuality; }
    void setAudioQuality(const QString& quality);
    QVariantList audioQualityOptions() const;
    QString audioQualityHint() const;
    QString mixdownMode() const { return m_draft.recording.mixdownMode; }
    void setMixdownMode(const QString& mode);

    bool detectorEnabled() const { return m_draft.watcher.detectorEnabled; }
    void setDetectorEnabled(bool on);
    bool watcherAutostart() const { return m_draft.watcher.autostart; }
    void setWatcherAutostart(bool on);
    bool askStopOnCallEnd() const { return m_draft.watcher.askStopOnCallEnd; }
    void setAskStopOnCallEnd(bool on);
    int silenceAskMinutes() const { return m_draft.watcher.silenceAskMinutes; }
    void setSilenceAskMinutes(int minutes);
    int detectorIntervalSec() const { return m_draft.watcher.detectorIntervalSec; }
    void setDetectorIntervalSec(int sec);
    QVariantList watchedApps() const;
    bool watching() const { return m_watching; }
    void setWatching(bool on);
    bool detectorAvailable() const { return m_detectorAvailable; }
    bool liveCallActive() const { return m_liveActive; }
    QString liveCallApp() const { return m_liveApp; }

    bool cloudSelected() const;
    QString serviceMode() const;
    void setServiceMode(const QString& mode);
    SettingsProviderModel* stt() const { return m_stt; }
    SettingsProviderModel* llm() const { return m_llm; }
    SettingsEmbeddingModel* embedding() const { return m_embedding; }
    VoiceModelListModel* voiceModels() const { return m_voiceModels; }
    VoiceModelListModel* segmentationModels() const { return m_segModels; }
    bool localDiarization() const;

    QString summaryLanguage() const { return m_draft.processing.summaryLanguage; }
    void setSummaryLanguage(const QString& language);
    QStringList summaryLanguageOptions() const;
    QVariantList promptGroups() const;
    QString promptGroup() const;
    void setPromptGroup(const QString& group);
    QVariantList promptTabs() const;
    int promptIndex() const { return m_promptIndex; }
    void setPromptIndex(int index);
    QString promptText() const;
    void setPromptText(const QString& text);
    bool promptModified() const;
    QVariantList promptVariables() const;
    QString schemaSummary() const;
    QString schemaBody() const;
    QString schemaKind() const;

    // ---- actions ----
    // Opens on a page. page: ShellActions.openSettings's name ("" | "providers" |
    // "watcher" | "cloud" | "summary" | "recording" | "general") or an internal page name.
    // focusField: "" | "stt" | "llm".
    Q_INVOKABLE void openPage(const QString& page, const QString& focusField = QString());
    // Save. False if a field is invalid (then it switches to the faulty page); nothing is written.
    Q_INVOKABLE bool save();
    // Discards the draft (the theme preview reverts too).
    Q_INVOKABLE void discard();
    // Reload from the core (when the window opens, if there is no unsaved change).
    Q_INVOKABLE void reload();

    Q_INVOKABLE void browseFolder(const QString& key);
    Q_INVOKABLE void setFolder(const QString& key, const QString& path);
    Q_INVOKABLE void openFolder(const QString& key);
    // Further recording roots (take effect after a restart, like the other folders).
    Q_INVOKABLE void addRecordingRoot();                       // folder picker
    Q_INVOKABLE void addRecordingRootPath(const QString& path);
    Q_INVOKABLE void makePrimaryRecordingRoot(const QString& path);
    Q_INVOKABLE void removeRecordingRoot(const QString& path);  // only an empty root
    Q_INVOKABLE void openPeople();
    // The People window on the given person (from the list of skipped samples).
    Q_INVOKABLE void openPerson(const QString& person);
    // The "Getting started" window (Settings > General link).
    Q_INVOKABLE void openOnboarding();

    Q_INVOKABLE void toggleDevice(int row);
    // Empty name -> back to the default (abbreviated) name.
    Q_INVOKABLE void renameDevice(int row, const QString& name);

    // New watched application (process name or part of it). An empty return = OK; otherwise
    // the error text (empty, already listed).
    Q_INVOKABLE QString addWatchedApp(const QString& match);
    Q_INVOKABLE void removeWatchedApp(int index);
    // The running processes not yet on the list: [{ match, label }].
    Q_INVOKABLE QVariantList runningApps() const;

    Q_INVOKABLE void resetPrompt();

    // ---- S3: the processing route per capability (cap: stt | diarization | voice | llm | embedding) ----
    // The draft's route: "local" | "external" | "cloud" | "ask" | "basic" ("" = unknown capability).
    Q_INVOKABLE QString route(const QString& cap) const;
    // false (nothing changes): unknown value or not allowed (routeOptions tells why).
    Q_INVOKABLE bool setRoute(const QString& cap, const QString& route);
    // The segmented control's options: [{ value, enabled, code, reason }] (code: the blocker's machine
    // name, reason: its sentence; both empty when enabled). Embedding: basic, local, external, cloud;
    // the others: local, external, cloud, ask.
    Q_INVOKABLE QVariantList routeOptions(const QString& cap) const;
    // The route segment of a provider id: "local" | "external" | "cloud".
    Q_INVOKABLE QString providerKind(const QString& providerId) const;

    // ---- the draft for the sub-models ----
    tanara::app::AppSettings& draft() { return m_draft; }
    const tanara::app::AppSettings& base() const { return m_base; }
    QString draftSecret(const QString& key) const { return m_draftSecrets.value(key); }
    void setDraftSecret(const QString& key, const QString& value);
    // The draft changed: recomputes the counter, errors, warning dot.
    void touch();
    // Whether the step could run with the draft (required fields, sign-in).
    tanara::providers::ReadinessResult draftReadiness(tanara::providers::WorkflowStep step) const;
    // SettingsDeviceModel keeps the selection here (saving writes it to state.json).
    QStringList& draftSelectedDevices() { return m_draftSelected; }

    // For tests: replaces the call detector (the view model owns it).
    void setDetectorForTest(tanara::watcher::IMeetingDetector* detector);
    // For tests: the difference of draft and base on the core settings' JSON form.
    static QJsonObject mergedSettings(const QJsonObject& base, const QJsonObject& draft,
                                      const QJsonObject& onto);
    static int countChanges(const QJsonObject& base, const QJsonObject& draft);
    static int routeChanges(const tanara::app::AppSettings& base, const tanara::app::AppSettings& draft);
    // Bytes -> "12.4 GB" / "38 MB" in the UI language.
    static QString formatBytes(qint64 bytes);

signals:
    void facadeChanged();
    void dialogsChanged();
    void demoStateChanged();
    void pageChanged();
    void focusChanged();
    void dirtyChanged();
    void servicesWarnChanged();
    void errorsChanged();
    void generalChanged();
    void voiceprintChanged();
    void themeModeChanged();
    void foldersChanged();
    void deviceCountChanged();
    void monitoringChanged();
    void recordingChanged();
    void watcherChanged();
    void watchedAppsChanged();
    void watchingChanged();
    void liveCallChanged();
    void serviceModeChanged();
    void routesChanged();
    void summaryChanged();
    void diarizationChanged();
    void promptsChanged();
    void promptIndexChanged();
    void promptTextChanged();

    // After a successful save.
    void saved();
    // The theme is to be saved (ui-state.json: written by the enclosing window / process).
    void themeModeSaved(const QString& mode);
    // The deep link's gap is gone: back to the meeting.
    void returnRequested();

private:
    friend class SettingsDeviceModel;
    friend class SettingsProviderModel;
    friend class SettingsEmbeddingModel;

    void init();
    void resolveServices();
    void useFacade(tanara::app::ISettingsFacade* facade);
    void loadFromCore();
    void applyScenario();
    void applyDefaultSelection();
    void normalize(tanara::app::AppSettings& s) const;
    // S3: what a capability's route can be set to (the registry: kinds of the registered providers; the
    // stt / llm cloud adapters are registered only while the cloud is live).
    tanara::providers::RouteContext routeContext(tanara::providers::Capability cap) const;
    bool applyRoute(tanara::providers::Capability cap, tanara::providers::Route route);
    void showServiceTab(const QString& mode);   // the shown tab only (deep links), no draft change
    void emitAllChanged();
    void onExternalSettingsChanged();
    void validate();
    void updateServicesWarn();
    void refreshVoiceprint();
    void refreshFolderUsage();
    void pollDetector();
    QString folderPath(const QString& key) const;
    QString defaultFolder(const QString& key) const;
    static QStringList folderKeys();
    QString promptId(int index) const;
    void storePromptsInDraft();
    QStringList selectedDevicesForSave() const;

    QPointer<tanara::app::ISettingsFacade> m_facade;
    bool m_explicitFacade = false;
    bool m_live = false;
    QPointer<SettingsDialogs> m_dialogs;
    QString m_demoState;
    QList<QMetaObject::Connection> m_connections;

    tanara::app::AppSettings m_base, m_draft;
    QHash<QString, QString> m_baseSecrets, m_draftSecrets;   // secretKey -> value
    QStringList m_baseSelected, m_draftSelected;             // default sources (raw names)
    QString m_baseTheme, m_draftTheme;
    QHash<QString, QString> m_promptText;                    // id -> the editor's text
    QHash<QString, QString> m_promptDefault;                 // id -> default (file / built-in)
    int m_promptIndex = 0;

    QString m_page = QStringLiteral("general");
    QString m_focusField;
    int m_changeCount = 0;
    bool m_servicesWarn = false;
    QVariantMap m_errors;
    bool m_saving = false;
    bool m_loading = false;

    QString m_voiceprintText;
    bool m_hasVoiceprint = false;
    QHash<QString, QString> m_folderUsage;   // key -> "12.4 GB · 86 meetings"
    int m_usageGeneration = 0;

    bool m_monitoring = false;
    bool m_watching = false;
    QTimer m_detectTimer;
    std::unique_ptr<tanara::watcher::IMeetingDetector> m_detector;
    bool m_detectorTried = false;
    bool m_detectorAvailable = false;
    bool m_liveActive = false;
    QString m_liveApp, m_liveAppId;

    QString m_serviceMode = QStringLiteral("own");   // the shown tab (the shim adds the Cloud routes)

    SettingsDeviceModel* m_devices = nullptr;
    SettingsProviderModel* m_stt = nullptr;
    SettingsProviderModel* m_llm = nullptr;
    SettingsEmbeddingModel* m_embedding = nullptr;
    VoiceModelListModel* m_voiceModels = nullptr;
    VoiceModelListModel* m_segModels = nullptr;
};

} // namespace tanara_qml
