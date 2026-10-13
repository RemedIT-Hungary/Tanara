#pragma once
//
// Loads/saves AppSettings from <metadataDir>/settings.json.
// Default metadataDir: ~/.tanara
#include "tanara/domain/Types.h"
#include <QObject>

namespace tanara::app {

class SettingsManager : public QObject {
    Q_OBJECT
public:
    // Empty metadataDir = ~/.tanara. The constructor loads the settings
    // (or sensible defaults if there is no settings.json yet).
    explicit SettingsManager(const QString& metadataDir = QString(),
                             QObject* parent = nullptr);

    const AppSettings& settings() const { return m_settings; }

    // Sets and persists, then emits settingsChanged().
    void setSettings(const AppSettings& s);

    // Enabled speaker-embedding models ("voiceModels"; alphabetical, unique ids).
    QStringList enabledVoiceModels() const { return m_settings.voiceModels; }
    // Normalizes (alphabetical, unique) and, if changed, persists + emits settingsChanged().
    void setEnabledVoiceModels(const QStringList& ids);

    // Loads from settings.json. If there is NO file: defaults + save (first run). If there is
    // one but it is unreadable / unparseable: defaults in memory ONLY; the file is not silently
    // overwritten (the user's settings, or a file another process just wrote, must not be lost).
    void load();

    // Writes to disk atomically (and creates missing folders). If the file was unreadable at
    // load time, first moves it aside as "settings.json.corrupt-<timestamp>".
    void save() const;

    // settings.json existed at load time but was unreadable/unparseable (the running values
    // are defaults; the original file is untouched on disk).
    bool loadFailed() const { return m_loadFailed; }

    // First run: settings.json did not exist at this load (mode selection).
    bool isFirstRun() const { return m_firstRun; }

    // Absolute path of the settings.json in use.
    QString settingsFilePath() const;

    // Sensible default settings (fixed in the contract).
    static AppSettings defaults(const QString& metadataDir = QString());

signals:
    void settingsChanged();

private:
    void ensureDirs() const;

    // Safety-net merge of the provider layer (STT/LLM) with the `def` defaults: with an empty
    // provider list or a missing/invalid selected id, fall back to the default. The actual
    // loading + migration (new "sttProviders" / old "stt" shape) was already done by
    // appSettingsFromJson in `loaded`; only the gaps are filled here.
    static void applyProviderDefaults(AppSettings& loaded, const AppSettings& def);

    QString     m_metadataDir;
    AppSettings m_settings;
    bool        m_firstRun = false;
    mutable bool m_loadFailed = false;   // unreadable file on disk (set aside on save)
};

} // namespace tanara::app