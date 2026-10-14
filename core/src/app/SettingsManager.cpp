#include "tanara/app/SettingsManager.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/foundation/settings/SettingsStore.h"
#include "tanara/foundation/log/Logging.h"
#include "tanara/library/tracks/TrackCatalog.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <QDir>
#include <QFile>
#if defined(Q_OS_WIN)
#  define SECURITY_WIN32
#  include <windows.h>
#  include <security.h>
#else
#  include <pwd.h>
#  include <unistd.h>
#endif
#include <iterator>

namespace tanara::app {

SettingsManager::SettingsManager(const QString& configDir, QObject* parent)
    : QObject(parent)
    , m_configDir(configDir.isEmpty() ? paths::AppPaths::system().configDir() : configDir)
{
    m_store = new SettingsStore(QDir(m_configDir).filePath(paths::files::Settings), this);
    load();
}

SettingsManager::~SettingsManager() = default;

namespace {
// Default name of the own microphone track: the OS account's display name (Windows: NameDisplay;
// POSIX: the first field of gecos), otherwise the login name capitalized. The user can change it at
// first start and in Settings any time.
QString defaultUserSpeakerName()
{
    QString name;
#if defined(Q_OS_WIN)
    wchar_t buf[256];
    ULONG len = ULONG(std::size(buf));
    if (GetUserNameExW(NameDisplay, buf, &len) && len > 0)
        name = QString::fromWCharArray(buf, int(len));
    if (name.trimmed().isEmpty())
        name = qEnvironmentVariable("USERNAME");
#else
    if (const passwd* pw = getpwuid(getuid())) {
        const QString gecos = QString::fromLocal8Bit(pw->pw_gecos).section(QLatin1Char(','), 0, 0);
        name = gecos.trimmed().isEmpty() ? QString::fromLocal8Bit(pw->pw_name) : gecos;
    }
    if (name.trimmed().isEmpty())
        name = qEnvironmentVariable("USER");
#endif
    name = name.trimmed();
    if (name.isEmpty())
        return QStringLiteral("Én");
    // Login name (all lowercase): capitalized so it looks like a name.
    if (name == name.toLower())
        name[0] = name.at(0).toUpper();
    return name;
}
} // namespace

AppSettings SettingsManager::defaults()
{
    AppSettings s;
    s.recording.userSpeakerName = defaultUserSpeakerName();
    s.processing.languageHints  = QStringList{QStringLiteral("hu")};

    // STT: selected provider = "soniox", with its config.
    s.providers.stt.selectedId = QStringLiteral("soniox");
    {
        providers::ProviderConfig stt;
        stt.type    = QStringLiteral("soniox");
        stt.baseUrl = QStringLiteral("https://api.soniox.com/v1");
        stt.model   = QStringLiteral("stt-async-v5");
        s.providers.stt.configs.insert(s.providers.stt.selectedId, stt);
    }

    // LLM: selected provider = "openai-compat" (a local LM Studio), the gemma default.
    s.providers.llm.selectedId = QStringLiteral("openai-compat");
    {
        providers::ProviderConfig llm;
        llm.type        = QStringLiteral("openai-compat");
        llm.baseUrl     = QStringLiteral("http://localhost:1234/v1");
        llm.model       = QStringLiteral("google/gemma-4-12b"); // the gemma default STAYS
        llm.temperature = 0.2;
        llm.maxTokens   = 8000;
        s.providers.llm.configs.insert(s.providers.llm.selectedId, llm);
    }

    // Embedding: none by default (basic level); the local endpoint is prefilled if they turn it on.
    s.providers.embedding.selectedId.clear();
    {
        providers::ProviderConfig emb;
        emb.type    = QStringLiteral("openai-compat-embedding");
        emb.baseUrl = QStringLiteral("http://localhost:1234/v1");
        emb.model   = QStringLiteral("text-embedding-bge-m3");
        s.providers.embedding.configs.insert(emb.type, emb);
    }
    return s;
}

QString SettingsManager::settingsFilePath() const
{
    return m_store->filePath();
}

void SettingsManager::publishPaths()
{
    m_paths = paths::AppPaths::system(m_settings.paths);
    paths::setCurrent(m_paths);
    library::devicenames::setOverrides(m_settings.recording.deviceNames);   // the single resolver of device names
}

void SettingsManager::load()
{
    // First run: the process's FIRST SettingsManager creates the file (translator installation already
    // loads before AppController), so the flag is process-wide.
    static bool s_createdInThisProcess = false;
    const SettingsStore::LoadResult r = m_store->load();
    if (r == SettingsStore::LoadResult::Missing) s_createdInThisProcess = true;
    m_firstRun = s_createdInThisProcess;
    m_loadFailed = r == SettingsStore::LoadResult::Unreadable;
    m_legacy = r == SettingsStore::LoadResult::OldFormat;

    if (r == SettingsStore::LoadResult::Missing) {
        // No config yet: defaults + write to disk.
        m_settings = defaults();
        publishPaths();
        save();
        return;
    }
    if (r != SettingsStore::LoadResult::Ok) {
        // Unreadable or old format: defaults in memory, the file stays untouched until a save.
        m_settings = defaults();
        publishPaths();
        return;
    }

    AppSettings loaded;
    for (const QString& name : AppSettings::sectionNames())
        loaded.setSectionJson(name, m_store->section(name));
    applyDefaults(loaded);
    m_settings = loaded;
    publishPaths();
    m_paths.ensureDirs();
}

void SettingsManager::applyDefaults(AppSettings& loaded)
{
    const AppSettings def = defaults();
    // An empty speaker name -> the default (an empty string really means "not given").
    if (loaded.recording.userSpeakerName.isEmpty())
        loaded.recording.userSpeakerName = def.recording.userSpeakerName;

    // ---- STT / LLM safety net: no empty provider list / selected id --------------------------
    auto fill = [](providers::CapabilityProviders& c, const providers::CapabilityProviders& d) {
        if (c.configs.isEmpty()) c.configs = d.configs;
        if (c.selectedId.isEmpty()) c.selectedId = d.selectedId;
        // The selected id must have a config (otherwise fall back to the default).
        if (!c.configs.contains(c.selectedId)) {
            if (d.configs.contains(c.selectedId))
                c.configs.insert(c.selectedId, d.configs.value(c.selectedId));
            else
                c.selectedId = c.configs.firstKey();
        }
    };
    fill(loaded.providers.stt, def.providers.stt);
    fill(loaded.providers.llm, def.providers.llm);

    // ---- Embedding: filling in missing prefilled configs (an empty id = none) ----
    const auto& emb = def.providers.embedding.configs;
    for (auto it = emb.constBegin(); it != emb.constEnd(); ++it)
        if (!loaded.providers.embedding.configs.contains(it.key()))
            loaded.providers.embedding.configs.insert(it.key(), it.value());
}

void SettingsManager::save() const
{
    for (const QString& name : AppSettings::sectionNames())
        m_store->setSection(name, m_settings.sectionJson(name));
    m_paths.ensureDirs();
    if (m_store->save()) m_loadFailed = false;
}

void SettingsManager::setEnabledVoiceModels(const QStringList& ids)
{
    const QStringList norm = processing::VoiceModelRegistry::normalizeIds(ids);
    if (norm == m_settings.processing.voiceModels) return;
    AppSettings s = m_settings;
    s.processing.voiceModels = norm;
    setSettings(s);
}

void SettingsManager::setSettings(const AppSettings& s)
{
    m_settings = s;
    m_settings.processing.voiceModels = processing::VoiceModelRegistry::normalizeIds(s.processing.voiceModels);
    publishPaths();
    save();
    emit settingsChanged();
}

} // namespace tanara::app
