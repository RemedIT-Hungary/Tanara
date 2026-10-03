#include "tanara/SettingsManager.h"
#include "tanara/store/JsonSerialization.h"
#include "tanara/Paths.h"
#include "tanara/Logging.h"

#include <QDateTime>
#include <QSaveFile>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace tanara {

namespace {

// A metaadat-mappa alapértelmezése és a TANARA_HOME felülírás EGY helyen: tanara/Paths.h.
QString defaultMetadataDir() { return paths::defaultMetadataDir(); }
QString expandHome(const QString& path) { return paths::expandHome(path); }

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

    AppSettings s;
    s.audioDir        = paths::defaultAudioDir();   // TANARA_HOME mellett a sandboxba
    s.notesDir        = paths::defaultNotesDir();
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

    // Első indítás: a folyamat ELSŐ SettingsManager-e hozza létre a fájlt (a fordító-telepítés
    // már az AppController előtt betölt) → a jelzés folyamatszintű.
    static bool s_createdInThisProcess = false;
    if (!f.exists()) s_createdInThisProcess = true;
    m_firstRun = s_createdInThisProcess;

    m_loadFailed = false;
    if (!f.exists()) {
        // Nincs még config → defaultok + lemezre írás.
        m_settings = defaults(m_metadataDir);
        save();
        return;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        // VAN fájl, de nem nyitható: defaultok a memóriában, a fájlhoz NEM nyúlunk.
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
        // Sérült / félkész fájl (pl. egy másik folyamat régi buildje épp írta): defaultok a
        // memóriában, de a fájlt NEM írjuk felül csendben — a felhasználó beállításai abban
        // vannak. Csak egy későbbi, kifejezett mentés teszi félre (lásd save()).
        qCWarning(lcApp).noquote() << "settings.json nem értelmezhető (" << err.errorString()
                                   << ") — alapértelmezések a memóriában, a fájl érintetlen:" << path;
        m_settings = defaults(m_metadataDir);
        m_loadFailed = true;
        return;
    }

    // A teljes AppSettings-et (MINDEN skalár + a provider-réteg régi→új shape
    // migrációja) EGY helyen, az appSettingsFromJson olvassa be — így új mező
    // hozzáadásakor nem kell két helyen átvezetni (a JSON-szerializáció és ez a
    // betöltő nem csúszhat szét). Utána csak a defaultokkal pótoljuk a hiányt.
    const QJsonObject obj = doc.object();
    const AppSettings def = defaults(m_metadataDir);
    AppSettings loaded = appSettingsFromJson(obj);

    // Üres kötelező mappa-/név-mezők → sensible default (az appSettingsFromJson nem
    // tölt defaultot; az üres string valódi „nincs megadva", nem „töröld").
    if (loaded.audioDir.isEmpty())        loaded.audioDir = def.audioDir;
    if (loaded.notesDir.isEmpty())        loaded.notesDir = def.notesDir;
    if (loaded.metadataDir.isEmpty())     loaded.metadataDir = def.metadataDir;
    // TANARA_HOME: a settings.json-beli metadataDir NEM térítheti vissza az appot a valódi
    // ~/.tanara-ba (pl. egy átmásolt minta-settings) — a futó érték mindig a felülírás.
    if (!paths::homeOverride().isEmpty()) loaded.metadataDir = paths::homeOverride();
    if (loaded.userSpeakerName.isEmpty()) loaded.userSpeakerName = def.userSpeakerName;
    // languageHints: az ÜRES lista érvényes érték („Automatikus” nyelv, K-04) — csak a
    // hiányzó kulcs kap defaultot (azt az appSettingsFromJson már megadta).

    // A provider-réteg már be van töltve+migrálva; csak a hiányokat pótoljuk.
    applyProviderDefaults(loaded, def);

    m_settings = loaded;
    ensureDirs();
}

void SettingsManager::applyProviderDefaults(AppSettings& loaded, const AppSettings& def)
{
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

    // A betöltéskor olvashatatlan fájlt mentés ELŐTT félretesszük (nem semmisítjük meg):
    // kézzel még kimenthető belőle, amit a felhasználó beállított.
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

    // Atomikus írás (ideiglenes fájl + átnevezés): több folyamat (elemző, felvevő, figyelő)
    // olvassa ugyanezt a fájlt — egyik se lásson csonka tartalmat, és megszakadt írás után
    // a korábbi fájl maradjon meg.
    const QByteArray data = QJsonDocument(toJson(m_settings)).toJson(QJsonDocument::Indented);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit())
        qCWarning(lcApp).noquote() << "settings.json: a mentés nem sikerült:" << path << f.errorString();
}

void SettingsManager::setSettings(const AppSettings& s)
{
    m_settings = s;
    save();
    emit settingsChanged();
}

} // namespace tanara
