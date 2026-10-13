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
// Besides the settings, the draft includes: the secrets (API keys: they go to the KeyStore,
// not settings.json), the default sources selection (state.json, shared with the recorder)
// and the theme (ui-state.json).
//
// Sub-models: `devices` (device list with live level), `stt` / `llm` (provider cards from the
// provider registry), `cloud` (Tanara Cloud account / waitlist), `embedding` (the Embedding
// card, library preparation and tag-suggestion switches), `voiceModels` (the Speakers page's
// voice models and the backfill state).
//
// Without a controller (--qml-shot / --demo, tests) it provides FICTIONAL data; `demoState`
// ("B01" ... "B07", "B07notes", "advanced", "dirty", "unsaved", "schema", "teaser", "cloudOut",
// "B04embedding" / "B04embeddingRunning" / "B04embeddingError" / "B04embeddingDone" /
// "B04embeddingCloud" / "B04embeddingNone" / "B04modelChange", "B08" / "B08download" / "B08none" / "B08fallback" /
// "B08error" / "B08offline" / "B08backfill" / "B08partial" / "B08bundled") sets the design states.
//
#include "SettingsCloudModel.h"
#include "SettingsDeviceModel.h"
#include "SettingsEmbeddingModel.h"
#include "SettingsProviderModel.h"
#include "VoiceModelListModel.h"

#include "tanara/Types.h"
#include "tanara/provider/ReadinessModel.h"

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
class AppController;
class IMeetingDetector;
}

namespace tanara_qml {

class SettingsDialogs;

class SettingsViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* controller READ controllerObject WRITE setControllerObject NOTIFY controllerChanged)
    Q_PROPERTY(QObject* dialogs READ dialogsObject WRITE setDialogsObject NOTIFY dialogsChanged)
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)
    // True: there is no core, saving happens in memory only (demo / screenshot).
    Q_PROPERTY(bool demo READ demo NOTIFY controllerChanged)

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
    // "live" (Tanara Cloud with sign-in) | "teaser" (waitlist) | "none"
    Q_PROPERTY(QString cloudAvailability READ cloudAvailability NOTIFY controllerChanged)
    // "own" | "cloud": which mode is selected (in teaser mode it only switches the view).
    Q_PROPERTY(QString serviceMode READ serviceMode WRITE setServiceMode NOTIFY serviceModeChanged)
    Q_PROPERTY(tanara_qml::SettingsProviderModel* stt READ stt CONSTANT)
    Q_PROPERTY(tanara_qml::SettingsProviderModel* llm READ llm CONSTANT)
    Q_PROPERTY(tanara_qml::SettingsCloudModel* cloud READ cloud CONSTANT)
    Q_PROPERTY(tanara_qml::SettingsEmbeddingModel* embedding READ embedding CONSTANT)

    // ---- Speakers: voice models (download immediate, enabling goes into the draft), backfill ----
    Q_PROPERTY(tanara_qml::VoiceModelListModel* voiceModels READ voiceModels CONSTANT)

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
    explicit SettingsViewModel(QObject* parent = nullptr);
    ~SettingsViewModel() override;

    QObject* controllerObject() const;
    void setControllerObject(QObject* controller);
    tanara::AppController* controller() const;
    void setController(tanara::AppController* controller);
    QObject* dialogsObject() const;
    void setDialogsObject(QObject* dialogs);
    SettingsDialogs* dialogs() const;
    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);
    bool demo() const { return m_controller.isNull(); }

    QString page() const { return m_page; }
    void setPage(const QString& page);
    QString focusField() const { return m_focusField; }
    QString focusBannerText() const;
    bool dirty() const { return m_changeCount > 0; }
    int changeCount() const { return m_changeCount; }
    QString footerText() const;
    bool servicesWarn() const { return m_servicesWarn; }
    QVariantMap errors() const { return m_errors; }

    QString userName() const { return m_draft.userSpeakerName; }
    void setUserName(const QString& name);
    QString voiceprintText() const { return m_voiceprintText; }
    bool hasVoiceprint() const { return m_hasVoiceprint; }
    QString uiLanguage() const { return m_draft.uiLanguage; }
    void setUiLanguage(const QString& lang);
    QVariantList uiLanguageOptions() const;
    bool languageNeedsRestart() const;
    QString themeMode() const { return m_draftTheme; }
    void setThemeMode(const QString& mode);
    QVariantList folders() const;
    bool onboardingAvailable() const;

    QAbstractItemModel* devicesModel() const;
    int deviceCount() const;
    bool monitoring() const { return m_monitoring; }
    void setMonitoring(bool on);
    bool autoRecordAll() const { return m_draft.autoRecordAllDevices; }
    void setAutoRecordAll(bool on);
    QString audioQuality() const { return m_draft.audioQuality; }
    void setAudioQuality(const QString& quality);
    QVariantList audioQualityOptions() const;
    QString audioQualityHint() const;
    QString mixdownMode() const { return m_draft.mixdownMode; }
    void setMixdownMode(const QString& mode);

    bool detectorEnabled() const { return m_draft.detectorEnabled; }
    void setDetectorEnabled(bool on);
    bool watcherAutostart() const { return m_draft.watcherAutostart; }
    void setWatcherAutostart(bool on);
    bool askStopOnCallEnd() const { return m_draft.askStopOnCallEnd; }
    void setAskStopOnCallEnd(bool on);
    int silenceAskMinutes() const { return m_draft.silenceAskMinutes; }
    void setSilenceAskMinutes(int minutes);
    int detectorIntervalSec() const { return m_draft.detectorIntervalSec; }
    void setDetectorIntervalSec(int sec);
    QVariantList watchedApps() const;
    bool watching() const { return m_watching; }
    void setWatching(bool on);
    bool detectorAvailable() const { return m_detectorAvailable; }
    bool liveCallActive() const { return m_liveActive; }
    QString liveCallApp() const { return m_liveApp; }

    QString cloudAvailability() const;
    QString serviceMode() const { return m_serviceMode; }
    void setServiceMode(const QString& mode);
    SettingsProviderModel* stt() const { return m_stt; }
    SettingsProviderModel* llm() const { return m_llm; }
    SettingsCloudModel* cloud() const { return m_cloud; }
    SettingsEmbeddingModel* embedding() const { return m_embedding; }
    VoiceModelListModel* voiceModels() const { return m_voiceModels; }

    QString summaryLanguage() const { return m_draft.summaryLanguage; }
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

    // ---- the draft for the sub-models ----
    tanara::AppSettings& draft() { return m_draft; }
    const tanara::AppSettings& base() const { return m_base; }
    QString draftSecret(const QString& key) const { return m_draftSecrets.value(key); }
    void setDraftSecret(const QString& key, const QString& value);
    // The draft changed: recomputes the counter, errors, warning dot.
    void touch();
    // Whether the step could run with the draft (required fields, sign-in).
    tanara::ReadinessResult draftReadiness(tanara::WorkflowStep step) const;
    // SettingsDeviceModel keeps the selection here (saving writes it to state.json).
    QStringList& draftSelectedDevices() { return m_draftSelected; }

    // For tests: replaces the call detector (the view model owns it).
    void setDetectorForTest(tanara::IMeetingDetector* detector);
    // For tests: the difference of draft and base on the core settings' JSON form.
    static QJsonObject mergedSettings(const QJsonObject& base, const QJsonObject& draft,
                                      const QJsonObject& onto);
    static int countChanges(const QJsonObject& base, const QJsonObject& draft);
    // Bytes -> "12.4 GB" / "38 MB" in the UI language.
    static QString formatBytes(qint64 bytes);

signals:
    void controllerChanged();
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
    void summaryChanged();
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
    friend class SettingsCloudModel;
    friend class SettingsEmbeddingModel;

    void attach();
    void loadFromCore();
    void applyDefaultSelection();
    void loadDemo();
    void normalize(tanara::AppSettings& s) const;
    void emitAllChanged();
    void onExternalSettingsChanged();
    void validate();
    void updateServicesWarn();
    void refreshVoiceprint();
    void refreshFolderUsage();
    void pollDetector();
    QString folderPath(const QString& key) const;
    QString promptId(int index) const;
    void storePromptsInDraft();
    QStringList selectedDevicesForSave() const;
    QString lastOwnProvider(bool stt) const;

    QPointer<tanara::AppController> m_controller;
    bool m_controllerSet = false;
    QPointer<SettingsDialogs> m_dialogs;
    QString m_demoState;
    QList<QMetaObject::Connection> m_connections;

    tanara::AppSettings m_base, m_draft;
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
    std::unique_ptr<tanara::IMeetingDetector> m_detector;
    bool m_detectorTried = false;
    bool m_detectorAvailable = false;
    bool m_liveActive = false;
    QString m_liveApp, m_liveAppId;

    QString m_serviceMode = QStringLiteral("own");
    QString m_ownStt, m_ownLlm;              // the own-key choice (before switching to Cloud)

    SettingsDeviceModel* m_devices = nullptr;
    SettingsProviderModel* m_stt = nullptr;
    SettingsProviderModel* m_llm = nullptr;
    SettingsCloudModel* m_cloud = nullptr;
    SettingsEmbeddingModel* m_embedding = nullptr;
    VoiceModelListModel* m_voiceModels = nullptr;
};

} // namespace tanara_qml
