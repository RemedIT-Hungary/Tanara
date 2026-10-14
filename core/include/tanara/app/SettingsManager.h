#pragma once
//
// SettingsManager: the typed interface of settings.json (format version 2, see AppSettings.h) and
// the owner of the resolved AppPaths.
//
// The file lives in AppPaths' config folder (platform convention or TANARA_HOME). It never
// migrates an old (flat, v1) file: that yields defaults + a log warning, and the old file is set
// aside on the next save (`tanara-cli settings import-legacy` takes the values over).
#include "tanara/app/AppSettings.h"
#include "tanara/foundation/paths/AppPaths.h"

#include <QObject>

namespace tanara { class SettingsStore; }

namespace tanara::app {

class SettingsManager : public QObject {
    Q_OBJECT
public:
    // Empty configDir = AppPaths' config folder. The constructor loads the settings (or sensible
    // defaults if there is no settings.json yet) and publishes the resolved paths
    // (paths::setCurrent).
    explicit SettingsManager(const QString& configDir = QString(), QObject* parent = nullptr);
    ~SettingsManager() override;

    const AppSettings& settings() const { return m_settings; }
    // The resolved locations (the "paths" section + platform defaults + TANARA_HOME).
    const paths::AppPaths& paths() const { return m_paths; }

    // Sets and persists, then emits settingsChanged().
    void setSettings(const AppSettings& s);

    // Enabled speaker-embedding models ("voiceModels"; alphabetical, unique ids).
    QStringList enabledVoiceModels() const { return m_settings.processing.voiceModels; }
    // Normalizes (alphabetical, unique) and, if changed, persists + emits settingsChanged().
    void setEnabledVoiceModels(const QStringList& ids);

    // Loads settings.json. No file: defaults + save (first run). An unreadable / unparseable or
    // old-format file: defaults in memory ONLY; the file is not silently overwritten.
    void load();

    // Writes to disk atomically. An unreadable / old file is first moved aside
    // ("settings.json.corrupt-<ts>" / "settings.json.v1-<ts>").
    void save() const;

    // settings.json existed at load time but was unreadable / unparseable (running on defaults).
    bool loadFailed() const { return m_loadFailed; }
    // settings.json was of the old (flat) format at load time (running on defaults).
    bool legacyFormat() const { return m_legacy; }

    // First run: settings.json did not exist at this load (mode selection).
    bool isFirstRun() const { return m_firstRun; }

    // Absolute path of the settings.json in use.
    QString settingsFilePath() const;

    // Sensible default settings (fixed in the contract). Paths stay empty (= platform default).
    static AppSettings defaults();
    // Fills the gaps of a loaded settings value from the defaults (empty speaker name, provider
    // lists / selections).
    static void applyDefaults(AppSettings& loaded);

signals:
    void settingsChanged();

private:
    void publishPaths();

    QString      m_configDir;
    AppSettings  m_settings;
    paths::AppPaths m_paths;
    SettingsStore* m_store = nullptr;
    bool         m_firstRun = false;
    bool         m_legacy = false;
    mutable bool m_loadFailed = false;
};

} // namespace tanara::app
