#pragma once
//
// SettingsViewModel — a Beállítások-ablak (SettingsWindow.qml, B01–B07) nézetmodellje.
//
// Piszkozat-modell: megnyitáskor a core beállításairól két másolat készül — `alap` (ami a
// lemezen van) és `piszkozat` (amit a felhasználó szerkeszt). A lapok a piszkozatot írják; a
// „Mentés” a KÜLÖNBSÉGET vezeti rá a core éppen érvényes beállításaira (így amit ez az ablak
// nem mutat, vagy amit közben más módosított, nem vész el), a „Mégse” eldobja. Kivétel a
// téma: azonnal látszik (előnézet), de csak mentéskor marad meg.
//
// A beállításokon túl a piszkozat része: a titkok (API-kulcsok — a KeyStore-ba mennek, nem a
// settings.json-ba), az alapértelmezett források kijelölése (state.json, a felvevővel közös)
// és a téma (ui-state.json).
//
// Al-modellek: `devices` (B02 eszközlista élő szinttel), `stt` / `llm` (szolgáltató-kártyák a
// provider-registryből), `cloud` (Tanara Cloud fiók / várólista).
//
// Controller nélkül (--qml-shot / --demo, tesztek) KITALÁLT adatot ad; a `demoState`
// ("B01" … "B07", "dirty", "unsaved", "schema", "teaser", "cloudOut") a design-állapotokat
// állítja be.
//
#include "SettingsCloudModel.h"
#include "SettingsDeviceModel.h"
#include "SettingsProviderModel.h"

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
    // Igaz: nincs core, a mentés csak a memóriában történik (demó / képernyőkép).
    Q_PROPERTY(bool demo READ demo NOTIFY controllerChanged)

    // ---- ablak ----
    // "general" | "recording" | "watcher" | "services" | "summary"
    Q_PROPERTY(QString page READ page WRITE setPage NOTIFY pageChanged)
    // Mély hivatkozás (B04): "" | "stt" | "llm" — melyik kártya hiányzó beállításához jött.
    Q_PROPERTY(QString focusField READ focusField NOTIFY focusChanged)
    Q_PROPERTY(QString focusBannerText READ focusBannerText NOTIFY focusChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(int changeCount READ changeCount NOTIFY dirtyChanged)
    Q_PROPERTY(QString footerText READ footerText NOTIFY dirtyChanged)
    // A „Szolgáltatások” melletti figyelmeztető pötty: kötelező szolgáltató hiányos vagy a
    // legutóbbi kapcsolat-teszt elbukott.
    Q_PROPERTY(bool servicesWarn READ servicesWarn NOTIFY servicesWarnChanged)
    // Mező-hibák: { "userName": "…", "folder.audio": "…", "watchedApps": "…" }.
    Q_PROPERTY(QVariantMap errors READ errors NOTIFY errorsChanged)

    // ---- B01 Általános ----
    Q_PROPERTY(QString userName READ userName WRITE setUserName NOTIFY generalChanged)
    Q_PROPERTY(QString voiceprintText READ voiceprintText NOTIFY voiceprintChanged)
    Q_PROPERTY(bool hasVoiceprint READ hasVoiceprint NOTIFY voiceprintChanged)
    Q_PROPERTY(QString uiLanguage READ uiLanguage WRITE setUiLanguage NOTIFY generalChanged)
    Q_PROPERTY(QVariantList uiLanguageOptions READ uiLanguageOptions CONSTANT)
    // A nyelv a következő indításkor vált (a piszkozat eltér a most futó nyelvtől).
    Q_PROPERTY(bool languageNeedsRestart READ languageNeedsRestart NOTIFY generalChanged)
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    // [{ key: audio|notes|meta, label, path, usage, hint, error, locked }]
    Q_PROPERTY(QVariantList folders READ folders NOTIFY foldersChanged)

    // ---- B02 Rögzítés ----
    Q_PROPERTY(QAbstractItemModel* devices READ devicesModel CONSTANT)
    Q_PROPERTY(int deviceCount READ deviceCount NOTIFY deviceCountChanged)
    // A szintfigyelés csak addig megy, amíg a lap látszik (a QML állítja).
    Q_PROPERTY(bool monitoring READ monitoring WRITE setMonitoring NOTIFY monitoringChanged)
    Q_PROPERTY(bool autoRecordAll READ autoRecordAll WRITE setAutoRecordAll NOTIFY recordingChanged)
    Q_PROPERTY(QString audioQuality READ audioQuality WRITE setAudioQuality NOTIFY recordingChanged)
    Q_PROPERTY(QVariantList audioQualityOptions READ audioQualityOptions CONSTANT)
    Q_PROPERTY(QString audioQualityHint READ audioQualityHint NOTIFY recordingChanged)
    Q_PROPERTY(QString mixdownMode READ mixdownMode WRITE setMixdownMode NOTIFY recordingChanged)

    // ---- B03 Hívásfigyelő ----
    Q_PROPERTY(bool detectorEnabled READ detectorEnabled WRITE setDetectorEnabled NOTIFY watcherChanged)
    Q_PROPERTY(bool watcherAutostart READ watcherAutostart WRITE setWatcherAutostart NOTIFY watcherChanged)
    Q_PROPERTY(bool askStopOnCallEnd READ askStopOnCallEnd WRITE setAskStopOnCallEnd NOTIFY watcherChanged)
    Q_PROPERTY(int silenceAskMinutes READ silenceAskMinutes WRITE setSilenceAskMinutes NOTIFY watcherChanged)
    Q_PROPERTY(int detectorIntervalSec READ detectorIntervalSec WRITE setDetectorIntervalSec NOTIFY watcherChanged)
    // [{ match, label, active }] — active: épp ez az alkalmazás használja a mikrofont.
    Q_PROPERTY(QVariantList watchedApps READ watchedApps NOTIFY watchedAppsChanged)
    // Élő észlelés (csak amíg a lap látszik — `watching`).
    Q_PROPERTY(bool watching READ watching WRITE setWatching NOTIFY watchingChanged)
    Q_PROPERTY(bool detectorAvailable READ detectorAvailable NOTIFY liveCallChanged)
    Q_PROPERTY(bool liveCallActive READ liveCallActive NOTIFY liveCallChanged)
    Q_PROPERTY(QString liveCallApp READ liveCallApp NOTIFY liveCallChanged)

    // ---- B04–B06 Szolgáltatások ----
    // "live" (bejelentkezős Tanara Cloud) | "teaser" (várólista) | "none"
    Q_PROPERTY(QString cloudAvailability READ cloudAvailability NOTIFY controllerChanged)
    // "own" | "cloud" — melyik mód van kiválasztva (teaser-módban csak a nézetet váltja).
    Q_PROPERTY(QString serviceMode READ serviceMode WRITE setServiceMode NOTIFY serviceModeChanged)
    Q_PROPERTY(tanara_qml::SettingsProviderModel* stt READ stt CONSTANT)
    Q_PROPERTY(tanara_qml::SettingsProviderModel* llm READ llm CONSTANT)
    Q_PROPERTY(tanara_qml::SettingsCloudModel* cloud READ cloud CONSTANT)

    // ---- B07 Összefoglaló ----
    Q_PROPERTY(QString summaryLanguage READ summaryLanguage WRITE setSummaryLanguage NOTIFY summaryChanged)
    Q_PROPERTY(QStringList summaryLanguageOptions READ summaryLanguageOptions CONSTANT)
    // [{ id, label, modified }]
    Q_PROPERTY(QVariantList promptTabs READ promptTabs NOTIFY promptsChanged)
    Q_PROPERTY(int promptIndex READ promptIndex WRITE setPromptIndex NOTIFY promptIndexChanged)
    Q_PROPERTY(QString promptText READ promptText WRITE setPromptText NOTIFY promptTextChanged)
    Q_PROPERTY(bool promptModified READ promptModified NOTIFY promptsChanged)
    // [{ token, description }] — amit a kód tényleg behelyettesít.
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

    QString summaryLanguage() const { return m_draft.summaryLanguage; }
    void setSummaryLanguage(const QString& language);
    QStringList summaryLanguageOptions() const;
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

    // ---- műveletek ----
    // Megnyitás egy lapon. page: a ShellActions.openSettings neve ("" | "providers" |
    // "watcher" | "cloud" | "summary" | "recording" | "general") vagy belső lapnév.
    // focusField: "" | "stt" | "llm" (B04).
    Q_INVOKABLE void openPage(const QString& page, const QString& focusField = QString());
    // Mentés. false, ha érvénytelen mező van (ilyenkor a hibás lapra vált) — semmi nem íródik.
    Q_INVOKABLE bool save();
    // A piszkozat eldobása (a téma-előnézet is visszaáll).
    Q_INVOKABLE void discard();
    // Újratöltés a core-ból (az ablak megnyitásakor, ha nincs mentetlen változás).
    Q_INVOKABLE void reload();

    Q_INVOKABLE void browseFolder(const QString& key);
    Q_INVOKABLE void setFolder(const QString& key, const QString& path);
    Q_INVOKABLE void openFolder(const QString& key);
    Q_INVOKABLE void openPeople();

    Q_INVOKABLE void toggleDevice(int row);
    // Üres név → vissza az alapértelmezett (rövidített) névre.
    Q_INVOKABLE void renameDevice(int row, const QString& name);

    // Új figyelt alkalmazás (folyamatnév vagy annak része). Üres vissza = rendben; különben
    // a hiba szövege (üres, már szerepel).
    Q_INVOKABLE QString addWatchedApp(const QString& match);
    Q_INVOKABLE void removeWatchedApp(int index);
    // A most futó folyamatok, amelyek még nincsenek a listán: [{ match, label }].
    Q_INVOKABLE QVariantList runningApps() const;

    Q_INVOKABLE void resetPrompt();

    // ---- a piszkozat az al-modelleknek ----
    tanara::AppSettings& draft() { return m_draft; }
    const tanara::AppSettings& base() const { return m_base; }
    QString draftSecret(const QString& key) const { return m_draftSecrets.value(key); }
    void setDraftSecret(const QString& key, const QString& value);
    // A piszkozat változott: számláló, hibák, figyelmeztető pötty újraszámolása.
    void touch();
    // A lépés futtatható lenne-e a piszkozattal (kötelező mezők, bejelentkezés).
    tanara::ReadinessResult draftReadiness(tanara::WorkflowStep step) const;
    // A SettingsDeviceModel a kijelölést itt tartja (a mentés a state.json-ba írja).
    QStringList& draftSelectedDevices() { return m_draftSelected; }

    // Tesztekhez: a hívás-detektor cseréje (a nézetmodell birtokolja).
    void setDetectorForTest(tanara::IMeetingDetector* detector);
    // Tesztekhez: a piszkozat és az alap különbsége a core-beállítások JSON-alakján.
    static QJsonObject mergedSettings(const QJsonObject& base, const QJsonObject& draft,
                                      const QJsonObject& onto);
    static int countChanges(const QJsonObject& base, const QJsonObject& draft);
    // Bájt → „12,4 GB” / „38 MB” a UI nyelvén.
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

    // Sikeres mentés után.
    void saved();
    // A téma mentendő (ui-state.json — a befoglaló ablak / folyamat írja).
    void themeModeSaved(const QString& mode);
    // B04: a mély hivatkozás hiánya megszűnt — vissza a megbeszéléshez.
    void returnRequested();

private:
    friend class SettingsDeviceModel;
    friend class SettingsProviderModel;
    friend class SettingsCloudModel;

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
    QHash<QString, QString> m_baseSecrets, m_draftSecrets;   // secretKey → érték
    QStringList m_baseSelected, m_draftSelected;             // alapértelmezett források (nyers nevek)
    QString m_baseTheme, m_draftTheme;
    QHash<QString, QString> m_promptText;                    // id → a szerkesztő szövege
    QHash<QString, QString> m_promptDefault;                 // id → alapértelmezett (fájl / beépített)
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
    QHash<QString, QString> m_folderUsage;   // key → „12,4 GB · 86 megbeszélés”
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
    QString m_ownStt, m_ownLlm;              // a saját kulcsos választás (Cloudra váltás előtt)

    SettingsDeviceModel* m_devices = nullptr;
    SettingsProviderModel* m_stt = nullptr;
    SettingsProviderModel* m_llm = nullptr;
    SettingsCloudModel* m_cloud = nullptr;
};

} // namespace tanara_qml
