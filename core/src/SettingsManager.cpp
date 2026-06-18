#include "tanara/SettingsManager.h"
#include "tanara/store/JsonSerialization.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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

    s.stt.type    = QStringLiteral("soniox");
    s.stt.baseUrl = QStringLiteral("https://api.soniox.com/v1");
    s.stt.model   = QStringLiteral("stt-async-v5");

    s.llm.type    = QStringLiteral("openai-compat");
    s.llm.baseUrl = QStringLiteral("http://localhost:1234/v1");
    s.llm.model   = QStringLiteral("gemma-4-12b");

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
    // (így a hiányzó/új mezők is értelmes értéket kapnak).
    AppSettings loaded = appSettingsFromJson(doc.object());
    const AppSettings def = defaults(m_metadataDir);

    if (loaded.audioDir.isEmpty())        loaded.audioDir = def.audioDir;
    if (loaded.notesDir.isEmpty())        loaded.notesDir = def.notesDir;
    if (loaded.metadataDir.isEmpty())     loaded.metadataDir = def.metadataDir;
    if (loaded.userSpeakerName.isEmpty()) loaded.userSpeakerName = def.userSpeakerName;
    if (loaded.languageHints.isEmpty())   loaded.languageHints = def.languageHints;
    if (loaded.stt.type.isEmpty())        loaded.stt = def.stt;
    if (loaded.llm.type.isEmpty())        loaded.llm = def.llm;

    m_settings = loaded;
    ensureDirs();
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
