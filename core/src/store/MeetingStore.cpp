#include "tanara/store/MeetingStore.h"
#include "tanara/store/JsonSerialization.h"
#include "tanara/SettingsManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>

namespace tanara {

namespace {

QString expandHome(const QString& path)
{
    if (path.startsWith(QLatin1String("~/")))
        return QDir(QDir::homePath()).filePath(path.mid(2));
    if (path == QLatin1String("~"))
        return QDir::homePath();
    return path;
}

// Cím → fájlrendszer-barát slug (ékezetek megtartva, csak a tiltott chars cserélve).
QString slugify(const QString& title)
{
    QString s = title.simplified();
    // Whitespace → '-'
    s.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral("-"));
    // Fájlnévben problémás karakterek eltávolítása.
    s.remove(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")));
    // Csak ASCII alfanumerikus, '-' és '_' maradjon — a többi (pl. ékezet) marad,
    // de a tényleg problémásak már kiestek. Üres → "meeting".
    if (s.isEmpty())
        s = QStringLiteral("meeting");
    // Hossz-limit, hogy a mappanév kezelhető legyen.
    if (s.length() > 48)
        s = s.left(48);
    return s;
}

} // namespace

MeetingStore::MeetingStore(const QString& audioDir,
                           const QString& metadataDir,
                           QObject* parent)
    : QObject(parent)
    , m_audioDir(expandHome(audioDir))
    , m_metadataDir(expandHome(metadataDir))
{
    m_connName = QStringLiteral("tanara_meetingstore_%1")
                     .arg(QUuid::createUuid().toString(QUuid::Id128));
    openDb();
}

MeetingStore::MeetingStore(SettingsManager* settings, QObject* parent)
    : QObject(parent)
{
    if (settings) {
        m_audioDir    = expandHome(settings->settings().audioDir);
        m_metadataDir = expandHome(settings->settings().metadataDir);
    }
    m_connName = QStringLiteral("tanara_meetingstore_%1")
                     .arg(QUuid::createUuid().toString(QUuid::Id128));
    openDb();
}

MeetingStore::~MeetingStore()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connName, false);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(m_connName);
}

QString MeetingStore::dbConnectionName() const
{
    return m_connName;
}

void MeetingStore::openDb()
{
    QDir().mkpath(m_metadataDir);
    QDir().mkpath(m_audioDir);

    const QString dbPath = QDir(m_metadataDir).filePath(QStringLiteral("index.db"));

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName);
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        qWarning("MeetingStore: nem sikerult megnyitni az index.db-t: %s",
                 qPrintable(db.lastError().text()));
        return;
    }
    ensureSchema();
}

void MeetingStore::ensureSchema()
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    const bool ok = q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS meetings ("
        " id TEXT PRIMARY KEY,"
        " title TEXT,"
        " folder TEXT,"
        " startedAt TEXT,"
        " durationMs INTEGER,"
        " hasTranscript INTEGER,"
        " hasSummary INTEGER"
        ")"));
    if (!ok)
        qWarning("MeetingStore: schema hiba: %s", qPrintable(q.lastError().text()));
}

QString MeetingStore::meetingJsonPath(const QString& folder) const
{
    return QDir(folder).filePath(QStringLiteral("meeting.json"));
}

void MeetingStore::upsertIndex(const Meeting& m)
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO meetings (id, title, folder, startedAt, durationMs, hasTranscript, hasSummary)"
        " VALUES (:id, :title, :folder, :startedAt, :durationMs, :hasTranscript, :hasSummary)"
        " ON CONFLICT(id) DO UPDATE SET"
        "  title=excluded.title, folder=excluded.folder, startedAt=excluded.startedAt,"
        "  durationMs=excluded.durationMs, hasTranscript=excluded.hasTranscript,"
        "  hasSummary=excluded.hasSummary"));
    q.bindValue(QStringLiteral(":id"), m.id);
    q.bindValue(QStringLiteral(":title"), m.title);
    q.bindValue(QStringLiteral(":folder"), m.folder);
    q.bindValue(QStringLiteral(":startedAt"), m.startedAt.toString(Qt::ISODateWithMs));
    q.bindValue(QStringLiteral(":durationMs"), static_cast<qlonglong>(m.durationMs));
    q.bindValue(QStringLiteral(":hasTranscript"), m.hasTranscript ? 1 : 0);
    q.bindValue(QStringLiteral(":hasSummary"), m.hasSummary ? 1 : 0);
    if (!q.exec())
        qWarning("MeetingStore: index upsert hiba: %s", qPrintable(q.lastError().text()));
}

Meeting MeetingStore::createMeeting(const QString& title)
{
    Meeting m;
    m.startedAt = QDateTime::currentDateTime();
    m.title     = title;

    const QString shortId = QUuid::createUuid().toString(QUuid::Id128).left(8);
    m.id = shortId;

    const QString stamp   = m.startedAt.toString(QStringLiteral("yyyy-MM-dd_HHmm"));
    const QString folderName = QStringLiteral("%1_%2_%3")
                                   .arg(stamp, slugify(title), shortId);

    const QString folderPath = QDir(m_audioDir).filePath(folderName);
    QDir().mkpath(folderPath);
    m.folder = folderPath;

    // Üres meeting.json kiírása, hogy a mappa azonnal "kész" legyen.
    saveMeeting(m);

    emit meetingAdded(m.id);
    return m;
}

void MeetingStore::saveMeeting(const Meeting& m)
{
    if (m.folder.isEmpty())
        return;

    QDir().mkpath(m.folder);

    const QString path = meetingJsonPath(m.folder);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(toJson(m)).toJson(QJsonDocument::Indented));
        f.close();
    } else {
        qWarning("MeetingStore: nem sikerult irni a meeting.json-t: %s", qPrintable(path));
    }

    upsertIndex(m);
    emit meetingUpdated(m.id);
}

Meeting MeetingStore::load(const QString& id)
{
    // Az indexből kikeressük a mappát, majd a lemezről (az igazság forrása) töltünk.
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT folder FROM meetings WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);

    QString folder;
    if (q.exec() && q.next())
        folder = q.value(0).toString();

    if (folder.isEmpty())
        return Meeting{};

    const QString path = meetingJsonPath(folder);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return Meeting{};
    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return Meeting{};

    return meetingFromJson(doc.object());
}

QVector<Meeting> MeetingStore::loadAll()
{
    QVector<Meeting> out;

    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "SELECT id, title, folder, startedAt, durationMs, hasTranscript, hasSummary"
            " FROM meetings ORDER BY startedAt DESC"))) {
        qWarning("MeetingStore: loadAll hiba: %s", qPrintable(q.lastError().text()));
        return out;
    }

    while (q.next()) {
        Meeting m;
        m.id            = q.value(0).toString();
        m.title         = q.value(1).toString();
        m.folder        = q.value(2).toString();
        m.startedAt     = QDateTime::fromString(q.value(3).toString(), Qt::ISODateWithMs);
        m.durationMs    = q.value(4).toLongLong();
        m.hasTranscript = q.value(5).toInt() != 0;
        m.hasSummary    = q.value(6).toInt() != 0;
        out.append(m);
    }
    return out;
}

void MeetingStore::rebuildIndexFromDisk()
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);

    // Tabula rasa: a DB csak cache, nyugodtan üríthető.
    {
        QSqlQuery clear(db);
        if (!clear.exec(QStringLiteral("DELETE FROM meetings")))
            qWarning("MeetingStore: index urites hiba: %s", qPrintable(clear.lastError().text()));
    }

    QDir root(m_audioDir);
    if (!root.exists())
        return;

    const QFileInfoList dirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : dirs) {
        const QString jsonPath = meetingJsonPath(fi.absoluteFilePath());
        QFile f(jsonPath);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QByteArray data = f.readAll();
        f.close();

        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        Meeting m = meetingFromJson(doc.object());
        // A lemezen lévő mappa az igazság: a folder mezőt szinkronban tartjuk.
        m.folder = fi.absoluteFilePath();
        if (m.id.isEmpty())
            continue;
        upsertIndex(m);
    }
}

} // namespace tanara
