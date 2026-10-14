#pragma once
//
// AppPaths: the single source of truth for every on-disk location of Tanara.
//
// Roles and their defaults (platform convention):
//
//   role        Linux (XDG)                      Windows                       macOS
//   config      $XDG_CONFIG_HOME/tanara           %APPDATA%\Tanara              ~/Library/Application Support/Tanara
//   data        $XDG_DATA_HOME/tanara             %APPDATA%\Tanara\data         .../Application Support/Tanara/data
//   state       $XDG_STATE_HOME/tanara            %LOCALAPPDATA%\Tanara         .../Application Support/Tanara/state
//   logs        <state>/logs                      %LOCALAPPDATA%\Tanara\logs    ~/Library/Logs/Tanara
//   models      $XDG_DATA_HOME/tanara/models      %LOCALAPPDATA%\Tanara\models  .../Application Support/Tanara/models
//   recordings  <Documents>/Tanara/recordings     (same)                        (same)
//   notes       <Documents>/Tanara/notes          (same)                        (same)
//
// The "paths" settings section (PathSettings) overrides data / models / recordings / notes.
// TANARA_HOME=<H> overrides EVERYTHING (the settings too): config, data and state are <H>, logs
// <H>/logs, models <H>/models, recordings <H>/recordings, notes <H>/notes. This keeps test runs and
// sandboxes completely away from the user's real data. (Exception: recording roots of the settings
// that lie INSIDE <H> are kept, so several roots can be tried in a sandbox.)
//
// The folders do not depend on QCoreApplication's organization / application name: the platform
// base folders (generic locations, %APPDATA%, %LOCALAPPDATA%) get a fixed "tanara"/"Tanara" suffix,
// so the GUI, the CLI and the watcher always agree.
//
// RULE: no other code computes a Tanara folder; file names of the data / state files are the
// constants in paths::files.
#include "tanara/foundation/paths/PathSettings.h"

#include <QString>
#include <QStringList>

namespace tanara::paths {

enum class Platform { Linux, Windows, MacOS };
Platform hostPlatform();

enum class Role { Config, Data, State, Logs, Models, Recordings, Notes };
enum class Source { Default, Settings, TanaraHome };
QString roleName(Role r);       // "config", "data", ...
QString sourceName(Source s);   // "default", "settings", "TANARA_HOME"

// The platform base folders the defaults are derived from. systemBase() asks the OS
// (QStandardPaths / environment); tests build one by hand (e.g. a Windows layout on Linux).
struct PlatformBase {
    Platform platform = Platform::Linux;
    QString home;          // the user's home
    QString documents;     // Documents (Linux: XDG_DOCUMENTS_DIR)
    QString configBase;    // Linux: $XDG_CONFIG_HOME; Windows: %APPDATA%; macOS: ~/Library/Application Support
    QString dataBase;      // Linux: $XDG_DATA_HOME;   Windows: %APPDATA%; macOS: ~/Library/Application Support
    QString stateBase;     // Linux: $XDG_STATE_HOME;  Windows: %LOCALAPPDATA%; macOS: ~/Library/Application Support
    QString localBase;     // Windows: %LOCALAPPDATA% (models); others: unused
    QString logBase;       // macOS: ~/Library/Logs; others: unused
};
PlatformBase systemBase();

class AppPaths {
public:
    AppPaths() = default;

    // Pure resolution (testable for every platform): base + settings + TANARA_HOME (empty = unset).
    static AppPaths resolve(const PlatformBase& base, const PathSettings& settings,
                            const QString& tanaraHome);
    // The running system: systemBase() + the given settings + the TANARA_HOME variable.
    static AppPaths system(const PathSettings& settings = PathSettings());

    QString configDir() const { return m_config; }
    QString dataDir() const { return m_data; }
    QString stateDir() const { return m_state; }
    QString logDir() const { return m_logs; }
    QString modelsDir() const { return m_models; }
    QString notesDir() const { return m_notes; }
    QStringList recordingRoots() const { return m_roots; }   // the first one is the primary
    QString primaryRecordingRoot() const { return m_roots.value(0); }

    QString dir(Role r) const;
    Source source(Role r) const;
    bool sandboxed() const { return m_sandboxed; }   // TANARA_HOME is in effect

    QString configFile(const QString& name) const;
    QString dataFile(const QString& name) const;
    QString stateFile(const QString& name) const;
    QString settingsFile() const;

    // Creates the folders of every role (the recording roots and notes too).
    void ensureDirs() const;

private:
    QString m_config, m_data, m_state, m_logs, m_models, m_notes;
    QStringList m_roots;
    Source m_dataSrc = Source::Default, m_modelsSrc = Source::Default,
           m_notesSrc = Source::Default, m_rootsSrc = Source::Default;
    bool m_sandboxed = false;
};

// Process-wide current paths. Until a SettingsManager publishes the resolved paths (setCurrent),
// this is AppPaths::system() without settings: the settings-independent roles (config, state,
// logs) are always right; data / models / recordings / notes are right once the settings loaded.
AppPaths current();
void setCurrent(const AppPaths& p);

// Value of TANARA_HOME as an absolute, "~"-expanded path; empty if unset.
QString homeOverride();
// Expands "~" / "~/x" to the user's home (other paths unchanged).
QString expandHome(const QString& path);
// "~"-expansion + clean path; a relative path is taken relative to `relativeTo`. Empty stays empty.
QString normalize(const QString& path, const QString& relativeTo = QString());

// File / folder names inside the roles (in ONE place).
namespace files {
// config
inline const QString Settings      = QStringLiteral("settings.json");
// data
inline const QString Secrets       = QStringLiteral("secrets.json");      // file secret backend
inline const QString People        = QStringLiteral("people.json");
inline const QString Voiceprints   = QStringLiteral("voiceprints.json");
inline const QString Tags          = QStringLiteral("tags.json");
inline const QString IndexDb       = QStringLiteral("index.db");
inline const QString CloudState    = QStringLiteral("cloud-state.json");
inline const QString EmbeddingState = QStringLiteral("embedding-state.json");
inline const QString JobStats      = QStringLiteral("job-stats.json");
inline const QString Prompts       = QStringLiteral("prompts");
// state
inline const QString State         = QStringLiteral("state.json");
inline const QString UiState       = QStringLiteral("ui-state.json");
inline const QString UiTranscript  = QStringLiteral("ui-transcript.json");
inline const QString RecorderIni   = QStringLiteral("recorder.ini");
inline const QString RecordingLock = QStringLiteral("recording.lock");
inline const QString WatcherLock   = QStringLiteral("watcher.lock");
} // namespace files

} // namespace tanara::paths
