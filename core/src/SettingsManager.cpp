#include "tanara/SettingsManager.h"
#include "tanara/store/JsonSerialization.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace tanara {

namespace {

QString defaultMetadataDir()
{
    return QDir(QDir::homePath()).filePath(QStringLiteral(".tanara"));
}

QString expandHome(const QString& path)
{
    if (path.startsWith(QLatin1String("~/")))
        return QDir(QDir::homePath()).filePath(path.mid(2));
    if (path == QLatin1String("~"))
        return QDir::homePath();
    return path;
}

} // namespace

SettingsManager::SettingsManager(const QString& metadataDir, QObject* parent)
    : QObject(parent)
    , m_metadataDir(metadataDir.isEmpty() ? defaultMetadataDir() : metadataDir)
{
    load();
}

AppSettings SettingsManager::defaults(const QString& metadataDir)
{
    const QString meta = metadataDir.isEmpty() ? defaultMetadataDir() : metadataDir;
    const QString home = QDir::homePath();

    AppSettings s;
    s.audioDir        = QDir(home).filePath(QStringLiteral("Tanara/recordings"));
    s.notesDir        = QDir(home).filePath(QStringLiteral("Tanara/notes"));
    s.metadataDir     = meta;
    s.userSpeakerName = QStringLiteral("Ádám");
    s.languageHints   = QStringList{QStringLiteral("hu")};

    // STT: kiválasztott provider = "soniox", a hozzá tartozó configgal.
    s.sttProviderId = QStringLiteral("soniox");
    {
        ProviderConfig stt;
        stt.type    = QStringLiteral("soniox");
        stt.baseUrl = QStringLiteral("https://api.soniox.com/v1");
        stt.model   = QStringLiteral("stt-async-v5");
        s.sttConfigs.insert(s.sttProviderId, stt);
    }

    // LLM: kiválasztott provider = "openai-compat" (helyi LM Studio), gemma-default.
    s.llmProviderId = QStringLiteral("openai-compat");
    {
        ProviderConfig llm;
        llm.type        = QStringLiteral("openai-compat");
        llm.baseUrl     = QStringLiteral("http://localhost:1234/v1");
        llm.model       = QStringLiteral("google/gemma-4-12b"); // #27: gemma-default MARAD
        llm.temperature = 0.2;
        llm.maxTokens   = 8000;
        s.llmConfigs.insert(s.llmProviderId, llm);
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

    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        // Nincs még config → defaultok + lemezre írás.
        m_settings = defaults(m_metadataDir);
        save();
        return;
    }

    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        m_settings = defaults(m_metadataDir);
        save();
        return;
    }

    // Defaultokból indulunk, majd a fájlból érkező mezők felülírják azokat
    // (így a hiányzó/új mezők is értelmes értéket kapnak; első indításkor sem
    // ürül ki egyetlen provider-config sem).
    const QJsonObject obj = doc.object();
    const AppSettings def = defaults(m_metadataDir);

    // Skalár mezők beolvasása közvetlenül a JSON-ból; a hiányzókra default.
    // (A provider-réteget külön a loadProviders() tölti — lásd lent.)
    AppSettings loaded;
    loaded.audioDir        = obj.value(QStringLiteral("audioDir")).toString();
    loaded.notesDir        = obj.value(QStringLiteral("notesDir")).toString();
    loaded.metadataDir     = obj.value(QStringLiteral("metadataDir")).toString();
    loaded.userSpeakerName = obj.value(QStringLiteral("userSpeakerName")).toString();
    loaded.autoRecordAllDevices =
        obj.value(QStringLiteral("autoRecordAllDevices")).toBool(def.autoRecordAllDevices);
    if (obj.contains(QStringLiteral("languageHints"))) {
        loaded.languageHints.clear();
        const QJsonArray hints = obj.value(QStringLiteral("languageHints")).toArray();
        for (const auto& v : hints)
            loaded.languageHints.append(v.toString());
    }

    if (loaded.audioDir.isEmpty())        loaded.audioDir = def.audioDir;
    if (loaded.notesDir.isEmpty())        loaded.notesDir = def.notesDir;
    if (loaded.metadataDir.isEmpty())     loaded.metadataDir = def.metadataDir;
    if (loaded.userSpeakerName.isEmpty()) loaded.userSpeakerName = def.userSpeakerName;
    if (loaded.languageHints.isEmpty())   loaded.languageHints = def.languageHints;

    // --- Provider-réteg: új shape betöltés + régi shape migráció ----------
    // Új shape: "sttProviders"/"llmProviders" + "sttProviderId"/"llmProviderId".
    // Régi shape (egyetlen "stt"/"llm"): config → id (type, fallback default-típus),
    //   sttProviderId=id, sttConfigs[id]=config.
    loadProviders(obj, loaded, def);

    m_settings = loaded;
    ensureDirs();
}

void SettingsManager::loadProviders(const QJsonObject& obj,
                                    AppSettings& loaded,
                                    const AppSettings& def)
{
    // EGYETLEN migrációs forrás: a régi→új shape leképezést (type→id fallback,
    // baseUrl/model/temperature/maxTokens/extra megőrzés, apiKey SOHA) az
    // appSettingsFromJson végzi. Itt csak az ÍGY kapott provider-rétegt vesszük
    // át, majd ráhúzzuk a produkciós "safety net"-et.
    const AppSettings migrated = appSettingsFromJson(obj);
    loaded.sttProviderId = migrated.sttProviderId;
    loaded.sttConfigs    = migrated.sttConfigs;
    loaded.llmProviderId = migrated.llmProviderId;
    loaded.llmConfigs    = migrated.llmConfigs;

    // ---- STT safety net ---------------------------------------------------
    // Defaultokkal merge: ne legyen üres provider-lista / kiválasztott id.
    if (loaded.sttConfigs.isEmpty())
        loaded.sttConfigs = def.sttConfigs;
    if (loaded.sttProviderId.isEmpty())
        loaded.sttProviderId = def.sttProviderId;
    // A kiválasztott id-hez tartozzon config (különben essünk vissza defaultra).
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
}

void SettingsManager::save() const
{
    ensureDirs();

    const QString path = settingsFilePath();
    QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QJsonDocument(toJson(m_settings)).toJson(QJsonDocument::Indented));
    f.close();
}

void SettingsManager::setSettings(const AppSettings& s)
{
    m_settings = s;
    save();
    emit settingsChanged();
}

} // namespace tanara
