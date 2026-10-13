#include "tanara/SettingsManager.h"
#include "tanara/store/JsonSerialization.h"
#include "tanara/Paths.h"
#include "tanara/Logging.h"
#include "tanara/audio/TrackCatalog.h"
#include "tanara/voiceid/VoiceModelRegistry.h"

#include <QDateTime>
#include <QSaveFile>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#if defined(Q_OS_WIN)
#  define SECURITY_WIN32
#  include <windows.h>
#  include <security.h>
#else
#  include <pwd.h>
#  include <unistd.h>
#endif
#include <iterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace tanara {

namespace {

// The default of the metadata folder and the TANARA_HOME override in ONE place: tanara/Paths.h.
QString defaultMetadataDir() { return paths::defaultMetadataDir(); }
QString expandHome(const QString& path) { return paths::expandHome(path); }

} // namespace

SettingsManager::SettingsManager(const QString& metadataDir, QObject* parent)
    : QObject(parent)
    , m_metadataDir(metadataDir.isEmpty() ? defaultMetadataDir() : metadataDir)
{
    load();
}

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

AppSettings SettingsManager::defaults(const QString& metadataDir)
{
    const QString meta = metadataDir.isEmpty() ? defaultMetadataDir() : metadataDir;

    AppSettings s;
    s.audioDir        = paths::defaultAudioDir();   // with TANARA_HOME into the sandbox
    s.notesDir        = paths::defaultNotesDir();
    s.metadataDir     = meta;
    s.userSpeakerName = defaultUserSpeakerName();
    s.languageHints   = QStringList{QStringLiteral("hu")};

    // STT: selected provider = "soniox", with its config.
    s.sttProviderId = QStringLiteral("soniox");
    {
        ProviderConfig stt;
        stt.type    = QStringLiteral("soniox");
        stt.baseUrl = QStringLiteral("https://api.soniox.com/v1");
        stt.model   = QStringLiteral("stt-async-v5");
        s.sttConfigs.insert(s.sttProviderId, stt);
    }

    // LLM: selected provider = "openai-compat" (a local LM Studio), the gemma default.
    s.llmProviderId = QStringLiteral("openai-compat");
    {
        ProviderConfig llm;
        llm.type        = QStringLiteral("openai-compat");
        llm.baseUrl     = QStringLiteral("http://localhost:1234/v1");
        llm.model       = QStringLiteral("google/gemma-4-12b"); // the gemma default STAYS
        llm.temperature = 0.2;
        llm.maxTokens   = 8000;
        s.llmConfigs.insert(s.llmProviderId, llm);
    }

    // Embedding: none by default (basic level); the local endpoint is prefilled if they turn it on.
    s.embeddingProviderId.clear();
    {
        ProviderConfig emb;
        emb.type    = QStringLiteral("openai-compat-embedding");
        emb.baseUrl = QStringLiteral("http://localhost:1234/v1");
        emb.model   = QStringLiteral("text-embedding-bge-m3");
        s.embeddingConfigs.insert(emb.type, emb);
    }

    return s;
}

QString SettingsManager::settingsFilePath() const
{
    return QDir(m_metadataDir).filePath(QStringLiteral("settings.json"));
}

void SettingsManager::ensureDirs() const
{
    QDir().mkpath(m_metadataDir);
    if (!m_settings.audioDir.isEmpty())
        QDir().mkpath(expandHome(m_settings.audioDir));
    if (!m_settings.notesDir.isEmpty())
        QDir().mkpath(expandHome(m_settings.notesDir));
    if (!m_settings.metadataDir.isEmpty())
        QDir().mkpath(expandHome(m_settings.metadataDir));
}

void SettingsManager::load()
{
    const QString path = settingsFilePath();
    QFile f(path);

    // First run: the process's FIRST SettingsManager creates the file (translator installation already
    // loads before AppController), so the flag is process-wide.
    static bool s_createdInThisProcess = false;
    if (!f.exists()) s_createdInThisProcess = true;
    m_firstRun = s_createdInThisProcess;

    m_loadFailed = false;
    if (!f.exists()) {
        // No config yet: defaults + write to disk.
        m_settings = defaults(m_metadataDir);
        devicenames::setOverrides(m_settings.deviceNames);
        save();
        return;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        // A file EXISTS but cannot be opened: defaults in memory, we do NOT touch the file.
        qCWarning(lcApp).noquote() << "settings.json nem olvasható — alapértelmezések a memóriában,"
                                      " a fájl érintetlen:" << path << f.errorString();
        m_settings = defaults(m_metadataDir);
        m_loadFailed = true;
        return;
    }

    const QByteArray data = f.readAll();
    f.close();


    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        // A corrupt / half-written file (e.g. an old build of another process was just writing it): defaults
        // in memory, but we do NOT silently overwrite the file, since the user's settings are in it. Only a later,
        // explicit save sets it aside (see save()).
        qCWarning(lcApp).noquote() << "settings.json nem értelmezhető (" << err.errorString()
                                   << ") — alapértelmezések a memóriában, a fájl érintetlen:" << path;
        m_settings = defaults(m_metadataDir);
        m_loadFailed = true;
        return;
    }

    // The whole AppSettings (EVERY scalar + the provider layer's old -> new shape migration) is read in ONE
    // place, appSettingsFromJson, so when a field is added it need not be wired in two places (the JSON
    // serialization and this loader cannot drift apart). Afterwards we only fill the gaps with defaults.
    const QJsonObject obj = doc.object();
    const AppSettings def = defaults(m_metadataDir);
    AppSettings loaded = appSettingsFromJson(obj);

    // Empty required folder/name fields -> a sensible default (appSettingsFromJson does not fill defaults;
    // an empty string really means "not given", not "delete").
    if (loaded.audioDir.isEmpty())        loaded.audioDir = def.audioDir;
    if (loaded.notesDir.isEmpty())        loaded.notesDir = def.notesDir;
    if (loaded.metadataDir.isEmpty())     loaded.metadataDir = def.metadataDir;
    // TANARA_HOME: the metadataDir in settings.json must NOT send the app back to the real ~/.tanara (e.g.
    // a copied sample settings file); the running value is always the override.
    if (!paths::homeOverride().isEmpty()) loaded.metadataDir = paths::homeOverride();
    if (loaded.userSpeakerName.isEmpty()) loaded.userSpeakerName = def.userSpeakerName;
    // languageHints: the EMPTY list is a valid value ("Automatic" language); only a missing key gets the
    // default (appSettingsFromJson already supplied that).

    // The provider layer is already loaded + migrated; we only fill the gaps.
    applyProviderDefaults(loaded, def);

    m_settings = loaded;
    devicenames::setOverrides(m_settings.deviceNames);   // the single resolver of device names
    ensureDirs();
}

void SettingsManager::applyProviderDefaults(AppSettings& loaded, const AppSettings& def)
{
    // ---- STT safety net ---------------------------------------------------
    // Merge with the defaults: there must be no empty provider list / selected id.
    if (loaded.sttConfigs.isEmpty())
        loaded.sttConfigs = def.sttConfigs;
    if (loaded.sttProviderId.isEmpty())
        loaded.sttProviderId = def.sttProviderId;
    // The selected id must have a config (otherwise fall back to the default).
    if (!loaded.sttConfigs.contains(loaded.sttProviderId)) {
        if (def.sttConfigs.contains(loaded.sttProviderId))
            loaded.sttConfigs.insert(loaded.sttProviderId, def.sttConfigs.value(loaded.sttProviderId));
        else
            loaded.sttProviderId = loaded.sttConfigs.firstKey();
    }

    // ---- LLM safety net ---------------------------------------------------
    if (loaded.llmConfigs.isEmpty())
        loaded.llmConfigs = def.llmConfigs;
    if (loaded.llmProviderId.isEmpty())
        loaded.llmProviderId = def.llmProviderId;
    if (!loaded.llmConfigs.contains(loaded.llmProviderId)) {
        if (def.llmConfigs.contains(loaded.llmProviderId))
            loaded.llmConfigs.insert(loaded.llmProviderId, def.llmConfigs.value(loaded.llmProviderId));
        else
            loaded.llmProviderId = loaded.llmConfigs.firstKey();
    }

    // ---- Embedding: filling in missing prefilled configs (an empty id = none) ----
    for (auto it = def.embeddingConfigs.constBegin(); it != def.embeddingConfigs.constEnd(); ++it)
        if (!loaded.embeddingConfigs.contains(it.key()))
            loaded.embeddingConfigs.insert(it.key(), it.value());
}

void SettingsManager::save() const
{
    ensureDirs();

    const QString path = settingsFilePath();
    QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());

    // A file unreadable at load is set aside BEFORE saving (not destroyed): what the user configured can
    // still be rescued from it by hand.
    if (m_loadFailed && QFile::exists(path)) {
        const QString aside = path + QStringLiteral(".corrupt-")
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        if (!QFile::rename(path, aside)) {
            qCWarning(lcApp).noquote() << "settings.json: a sérült fájl nem tehető félre — a mentés"
                                          " kimarad, a fájl érintetlen:" << path;
            return;
        }
        qCWarning(lcApp).noquote() << "settings.json: a sérült fájl félretéve:" << aside;
    }
    m_loadFailed = false;

    // Atomic write (temporary file + rename): several processes (analyzer, recorder, watcher) read this
    // same file, and none may see truncated content; after an interrupted write the earlier file stays.
    const QByteArray data = QJsonDocument(toJson(m_settings)).toJson(QJsonDocument::Indented);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit())
        qCWarning(lcApp).noquote() << "settings.json: a mentés nem sikerült:" << path << f.errorString();
}

void SettingsManager::setEnabledVoiceModels(const QStringList& ids)
{
    const QStringList norm = VoiceModelRegistry::normalizeIds(ids);
    if (norm == m_settings.voiceModels) return;
    AppSettings s = m_settings;
    s.voiceModels = norm;
    setSettings(s);
}

void SettingsManager::setSettings(const AppSettings& s)
{
    m_settings = s;
    m_settings.voiceModels = VoiceModelRegistry::normalizeIds(s.voiceModels);
    devicenames::setOverrides(m_settings.deviceNames);
    save();
    emit settingsChanged();
}

} // namespace tanara
