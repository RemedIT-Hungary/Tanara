#include "tanara/library/store/MeetingStore.h"
#include "tanara/library/store/JsonSerialization.h"
#include "tanara/library/store/MeetingArchive.h"
#include "tanara/foundation/log/Logging.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/foundation/ProcessLock.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QDateTime>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>

namespace tanara::library {

namespace {

// Title -> a file-system-friendly slug (accents kept, only forbidden chars replaced).
QString slugify(const QString& title)
{
    QString s = title.simplified();
    // Whitespace → '-'
    s.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral("-"));
    // Removing characters that are problematic in file names.
    s.remove(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")));
    // Only ASCII alphanumerics, '-' and '_' stay; the rest (e.g. accents) stays too, but the truly
    // problematic ones are already gone. Empty -> "meeting".
    if (s.isEmpty())
        s = QStringLiteral("meeting");
    // Length limit, so the folder name stays manageable.
    if (s.length() > 48)
        s = s.left(48);
    return s;
}

// For comparing folder paths: a cleaned absolute path (trailing slash, no "..").
QString normFolder(const QString& folder)
{
    return folder.isEmpty() ? QString() : QDir::cleanPath(QDir(folder).absolutePath());
}

// The "id" field of the meeting.json in a folder (empty if absent / unreadable).
QString idInMeetingJson(const QString& jsonPath)
{
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("id")).toString();
}

// For this long a track file counts as "fresh": one written to more recently than this is very
// likely STILL being written (a live recording in another process), so it is not an orphan.
constexpr qint64 kLiveTrackGraceSecs = 5 * 60;

} // namespace

MeetingStore::MeetingStore(const QString& audioDir,
                           const QString& metadataDir,
                           QObject* parent)
    : QObject(parent)
    , m_audioDir(paths::expandHome(audioDir.trimmed()))
    , m_metadataDir(metadataDir.trimmed().isEmpty() ? paths::current().dataDir() : paths::expandHome(metadataDir.trimmed()))
{
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

    const QString dbPath = QDir(m_metadataDir).filePath(paths::files::IndexDb);

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName);
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        qCWarning(lcStore, "MeetingStore: nem sikerult megnyitni az index.db-t: %s",
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
        qCWarning(lcStore, "MeetingStore: schema hiba: %s", qPrintable(q.lastError().text()));
}

QString MeetingStore::meetingJsonPath(const QString& folder) const
{
    return QDir(folder).filePath(QStringLiteral("meeting.json"));
}

void MeetingStore::upsertIndex(const Meeting& m)
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    // ONE folder = ONE row: if an entry with another id was left for the same folder (e.g. another
    // process overwrote the folder's meeting.json with a new id), that row is stale. Deleting it touches
    // ONLY the index, not the disk.
    if (!m.folder.isEmpty()) {
        const QString mine = normFolder(m.folder);
        QStringList staleIds;
        QSqlQuery stale(db);
        stale.prepare(QStringLiteral("SELECT id, folder FROM meetings WHERE id != :id"));
        stale.bindValue(QStringLiteral(":id"), m.id);
        if (stale.exec())
            while (stale.next())
                if (normFolder(stale.value(1).toString()) == mine)
                    staleIds << stale.value(0).toString();
        for (const QString& sid : std::as_const(staleIds)) {
            QSqlQuery del(db);
            del.prepare(QStringLiteral("DELETE FROM meetings WHERE id = :id"));
            del.bindValue(QStringLiteral(":id"), sid);
            del.exec();
            qCWarning(lcStore).noquote()
                << "MeetingStore: elavult index-sor törölve (ugyanaz a mappa, más id):"
                << sid << "->" << m.id << m.folder;
        }
    }
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
        qCWarning(lcStore, "MeetingStore: index upsert hiba: %s", qPrintable(q.lastError().text()));
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

    // Writing an empty meeting.json so the folder is immediately "ready".
    saveMeeting(m);

    emit meetingAdded(m.id);
    return m;
}

void MeetingStore::saveMeeting(const Meeting& m)
{
    if (m.folder.isEmpty())
        return;

    QDir().mkpath(m.folder);

    // Atomic write (temporary file + rename): on an interrupted write / full disk the earlier
    // meeting.json stays intact, and no other process ever sees a half-written file.
    const QString path = meetingJsonPath(m.folder);
    const QByteArray data = QJsonDocument(toJson(m)).toJson(QJsonDocument::Indented);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit())
        qCWarning(lcStore, "MeetingStore: nem sikerult irni a meeting.json-t: %s", qPrintable(path));

    upsertIndex(m);
    emit meetingUpdated(m.id);
}

Meeting MeetingStore::load(const QString& id)
{
    // We look the folder up from the index, then load from disk (the source of truth).
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT folder FROM meetings WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);

    QString folder;
    if (q.exec() && q.next())
        folder = q.value(0).toString();

    if (folder.isEmpty())
        return Meeting{};
    return readMeetingFolder(folder);
}

QString MeetingStore::folderOf(const QString& id)
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT folder FROM meetings WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return QString();
}

Meeting MeetingStore::readMeetingFolder(const QString& folder)
{
    if (folder.isEmpty())
        return Meeting{};
    QFile f(QDir(folder).filePath(QStringLiteral("meeting.json")));
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

bool MeetingStore::deleteMeeting(const QString& id)
{
    if (id.isEmpty())
        return false;
    QSqlDatabase db = QSqlDatabase::database(m_connName);

    // Looking the folder up, then deleting from disk (the source of truth).
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT folder FROM meetings WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    QString folder;
    if (q.exec() && q.next())
        folder = q.value(0).toString();

    bool any = false;
    if (!folder.isEmpty() && folderOwnedOnlyBy(id, folder)) {
        QDir dir(folder);
        if (dir.exists() && dir.removeRecursively())
            any = true;
    }

    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM meetings WHERE id = :id"));
    del.bindValue(QStringLiteral(":id"), id);
    if (del.exec() && del.numRowsAffected() > 0)
        any = true;

    if (any)
        emit meetingRemoved(id);
    return any;
}

// Can the folder be deleted when deleting the given meeting? NO if
//  - another, still listed meeting id also points to this folder,
//  - the meeting.json in the folder already contains ANOTHER id (the folder became someone else's),
//  - a live recording is going into the folder (recording.lock with a live PID).
// In those cases only the index row is deleted, the recording stays on disk.
bool MeetingStore::folderOwnedOnlyBy(const QString& id, const QString& folder) const
{
    const QString mine = normFolder(folder);
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id, folder FROM meetings WHERE id != :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (q.exec()) {
        while (q.next()) {
            if (normFolder(q.value(1).toString()) != mine) continue;
            qCWarning(lcStore).noquote()
                << "MeetingStore: a mappa NEM törlődik — másik meeting is erre mutat:"
                << q.value(0).toString() << folder;
            return false;
        }
    }
    const QString diskId = idInMeetingJson(meetingJsonPath(folder));
    if (!diskId.isEmpty() && diskId != id) {
        qCWarning(lcStore).noquote()
            << "MeetingStore: a mappa NEM törlődik — a meeting.json más id-t tartalmaz:"
            << diskId << "!=" << id << folder;
        return false;
    }
    if (isLiveRecordingFolder(folder)) {
        qCWarning(lcStore).noquote()
            << "MeetingStore: a mappa NEM törlődik — élő felvétel megy bele:" << folder;
        return false;
    }
    return true;
}

// Whether a live recording is going into the folder (from any process): recording.lock points to
// this folder and its PID is alive. A stale (dead-PID) lock does not count.
bool MeetingStore::isLiveRecordingFolder(const QString& folder) const
{
    const RecordingLock::Info lock =
        RecordingLock::read(recordingLockPath());
    return lock.active && !lock.meetingFolder.isEmpty()
        && normFolder(lock.meetingFolder) == normFolder(folder);
}

QVector<Meeting> MeetingStore::loadAll()
{
    QVector<Meeting> out;

    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "SELECT id, title, folder, startedAt, durationMs, hasTranscript, hasSummary"
            " FROM meetings ORDER BY startedAt DESC"))) {
        qCWarning(lcStore, "MeetingStore: loadAll hiba: %s", qPrintable(q.lastError().text()));
        return out;
    }

    QHash<QString, int> byFolder;   // normalized folder -> position in out
    while (q.next()) {
        Meeting m;
        m.id            = q.value(0).toString();
        m.title         = q.value(1).toString();
        m.folder        = q.value(2).toString();
        m.startedAt     = QDateTime::fromString(q.value(3).toString(), Qt::ISODateWithMs);
        m.durationMs    = q.value(4).toLongLong();
        m.hasTranscript = q.value(5).toInt() != 0;
        m.hasSummary    = q.value(6).toInt() != 0;
        // A folder must NEVER appear twice (an old index may have left such): the row whose id is in the
        // folder's meeting.json stays; otherwise the first.
        const QString key = normFolder(m.folder);
        if (!key.isEmpty()) {
            const auto dup = byFolder.constFind(key);
            if (dup != byFolder.constEnd()) {
                if (idInMeetingJson(meetingJsonPath(m.folder)) == m.id)
                    out[*dup] = m;
                continue;
            }
            byFolder.insert(key, int(out.size()));
        }
        out.append(m);
    }
    return out;
}

namespace {
qint64 probeDurationMs(const media::MediaTool* media, const QString& path)
{
    if (!media) return 0;
    const media::MediaResult r = media->probe(path, 5000);
    return r.info ? r.info->durationMs() : 0;
}
} // namespace

// A meeting built from track files (track_*.ogg) for a folder that has no meeting.json
// (a crashed recording, or tracks copied over by hand from another machine).
Meeting MeetingStore::meetingFromTrackFolder(const QString& folder) const
{
    const QFileInfo fi(folder);
    const QStringList oggs = QDir(folder).entryList({QStringLiteral("track_*.ogg")}, QDir::Files, QDir::Name);
    Meeting m;
    m.id     = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m.folder = folder;
    // Folder name: "yyyy-MM-dd_HHmm_<title slug>" (RecordingSession::start writes it this way).
    const QString name = fi.fileName();
    m.startedAt = QDateTime::fromString(name.left(15), QStringLiteral("yyyy-MM-dd_HHmm"));
    if (!m.startedAt.isValid()) m.startedAt = fi.birthTime().isValid() ? fi.birthTime() : fi.lastModified();
    QString slug = name.size() > 16 ? name.mid(16) : name;
    slug.replace(QLatin1Char('-'), QLatin1Char(' '));
    m.title = slug.trimmed().isEmpty() ? QStringLiteral("Meeting") : slug.trimmed();
    m.title += QStringLiteral(" (helyreállított)");

    int micNo = 1;
    for (const QString& f : oggs) {
        Track t;
        const QString stem = f.left(f.size() - 4).mid(6);   // "track_" + without ".ogg"
        t.id         = stem;
        t.file       = f;
        t.deviceName = QString(stem).replace(QLatin1Char('-'), QLatin1Char(' '));
        const bool loop = stem.startsWith(QStringLiteral("monitor-of")) || stem.contains(QStringLiteral("loopback"));
        t.kind         = loop ? TrackKind::Loopback : TrackKind::Mic;
        t.speakerLabel = loop ? QStringLiteral("Rendszer")
                              : QStringLiteral("Mikrofon ") + QString::number(micNo++);
        t.active = true;
        m.durationMs = qMax(m.durationMs, probeDurationMs(m_media.get(), QDir(folder).filePath(f)));
        m.tracks.append(t);
    }
    return m;
}

int MeetingStore::recoverOrphanRecordings()
{
    QDir root(m_audioDir);
    if (!root.exists()) return 0;
    int recovered = 0;
    const QFileInfoList dirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : dirs) {
        const QString folder = fi.absoluteFilePath();
        if (QFile::exists(meetingJsonPath(folder)))
            continue;
        const QStringList oggs = QDir(folder).entryList({QStringLiteral("track_*.ogg")}, QDir::Files, QDir::Name);
        if (oggs.isEmpty())
            continue;

        // NOT an orphan if it is STILL being written: RecordingSession writes meeting.json only at the END
        // of the recording, so a live recording of another process (tanara --record) looks the same as a
        // crashed one. Skipped if recording.lock (with a live PID) points to this folder, or if the freshest
        // track file was written to within a few minutes. (It is considered again at the next start / index rebuild.)
        if (isLiveRecordingFolder(folder)) {
            qCInfo(lcStore).noquote() << "Felvétel folyamatban (recording.lock) — nem árva:" << fi.fileName();
            continue;
        }
        QDateTime newest;
        for (const QString& f : oggs) {
            const QDateTime mt = QFileInfo(QDir(folder).filePath(f)).lastModified();
            if (!newest.isValid() || mt > newest) newest = mt;
        }
        if (newest.isValid() && newest.secsTo(QDateTime::currentDateTime()) < kLiveTrackGraceSecs) {
            qCInfo(lcStore).noquote()
                << "Friss sáv-fájl (talán még íródik) — egyelőre nem állítjuk helyre:" << fi.fileName();
            continue;
        }

        const Meeting m = meetingFromTrackFolder(folder);
        saveMeeting(m);   // meeting.json + index
        ++recovered;
        qCInfo(lcStore).noquote() << "Árva felvétel helyreállítva:" << fi.fileName()
                                  << "sávok:" << m.tracks.size() << "hossz(ms):" << m.durationMs;
    }
    return recovered;
}

namespace {
// Recursive folder copy (the target does not exist yet). On error false; the caller cleans up the half-made target.
bool copyDirRecursive(const QString& from, const QString& to)
{
    QDir src(from);
    if (!QDir().mkpath(to)) return false;
    for (const QFileInfo& e : src.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        const QString dst = QDir(to).filePath(e.fileName());
        if (e.isDir()) { if (!copyDirRecursive(e.absoluteFilePath(), dst)) return false; }
        else if (!QFile::copy(e.absoluteFilePath(), dst)) return false;
    }
    return true;
}
} // namespace

QString MeetingStore::stageFolderForImport(const QString& sourceDir, QString* error) const
{
    auto fail = [&](const QString& msg) { if (error) *error = msg; return QString(); };
    const QFileInfo src(sourceDir);
    if (!src.isDir())
        return fail(QCoreApplication::translate("MeetingStore", "Nincs ilyen mappa: %1").arg(sourceDir));
    const QString source = src.absoluteFilePath();
    const bool hasJson = QFile::exists(meetingJsonPath(source));
    const bool hasTracks = !QDir(source).entryList({QStringLiteral("track_*.ogg")}, QDir::Files).isEmpty();
    if (!hasJson && !hasTracks)
        return fail(QCoreApplication::translate("MeetingStore", "A mappában nincs meeting.json és nincs track_*.ogg sáv: %1").arg(sourceDir));
    const QString audioRoot = QDir(m_audioDir).absolutePath();
    if (QFileInfo::exists(QDir(audioRoot).filePath(src.fileName())))
        return fail(QCoreApplication::translate("MeetingStore", "Már van ilyen nevű felvétel: %1 — a meglévőt nem írom felül.")
                        .arg(QDir(audioRoot).filePath(src.fileName())));
    const QString temp = QDir(audioRoot).filePath(MeetingArchive::tempDirPrefix()
                                                  + QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString staged = QDir(temp).filePath(src.fileName());
    if (!QDir().mkpath(audioRoot) || !copyDirRecursive(source, staged)) {
        QDir(temp).removeRecursively();
        return fail(QCoreApplication::translate("MeetingStore", "A mappa másolása nem sikerült: %1 → %2").arg(source, staged));
    }
    return staged;
}

Meeting MeetingStore::adoptMeetingFolder(const QString& sourceDir, QString* error)
{
    const QString audioRoot = QDir(m_audioDir).absolutePath();
    const QFileInfo src(sourceDir);
    const QString source = src.absoluteFilePath();

    // Archive import: the source is in the temporary .import-<uuid> folder under the recordings folder
    // -> moved (not copied), and the temporary folder is ALWAYS deleted at the end.
    const QFileInfo parentInfo(src.absolutePath());
    const bool fromImportTemp = parentInfo.fileName().startsWith(MeetingArchive::tempDirPrefix())
        && QDir(parentInfo.absolutePath()).absolutePath() == audioRoot;
    struct TempCleanup {
        QString dir;
        ~TempCleanup() { if (!dir.isEmpty()) QDir(dir).removeRecursively(); }
    } tempCleanup{fromImportTemp ? parentInfo.absoluteFilePath() : QString()};

    auto fail = [&](const QString& msg) { if (error) *error = msg; return Meeting(); };
    if (!src.isDir())
        return fail(QCoreApplication::translate("MeetingStore", "Nincs ilyen mappa: %1").arg(sourceDir));
    const bool hasJson = QFile::exists(meetingJsonPath(source));
    const bool hasTracks = !QDir(source).entryList({QStringLiteral("track_*.ogg")}, QDir::Files).isEmpty();
    if (!hasJson && !hasTracks)
        return fail(QCoreApplication::translate("MeetingStore", "A mappában nincs meeting.json és nincs track_*.ogg sáv: %1").arg(sourceDir));

    // Reading meeting.json while still in the source: a faulty / already existing meeting thus leaves
    // nothing behind in the recordings folder.
    Meeting m;
    if (hasJson) {
        QFile f(meetingJsonPath(source));
        QJsonParseError err{};
        const QJsonDocument doc = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll(), &err)
                                                              : QJsonDocument();
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            return fail(QCoreApplication::translate("MeetingStore", "A meeting.json nem olvasható: %1").arg(meetingJsonPath(source)));
        m = meetingFromJson(doc.object());
        if (m.id.isEmpty())
            m.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    // Target: under the recordings folder, with the same name. If it is already there, it stays in place.
    QString folder = source;
    if (parentInfo.absoluteFilePath() != audioRoot) {
        folder = QDir(audioRoot).filePath(src.fileName());
        if (QFileInfo::exists(folder))
            return fail(QCoreApplication::translate("MeetingStore", "Már van ilyen nevű felvétel: %1 — a meglévőt nem írom felül.").arg(folder));
    }
    // The same meeting (id) is already in the library, in another folder.
    if (hasJson) {
        const Meeting existing = load(m.id);
        if (!existing.id.isEmpty()
            && QDir(existing.folder).absolutePath() != QDir(folder).absolutePath())
            return fail(QCoreApplication::translate("MeetingStore", "Ez a megbeszélés már szerepel a könyvtárban: „%1” (%2).")
                            .arg(existing.title, existing.folder));
    }

    if (folder != source) {
        if (!QDir().mkpath(audioRoot))
            return fail(QCoreApplication::translate("MeetingStore", "A felvételek mappája nem hozható létre: %1").arg(audioRoot));
        // On the same volume (e.g. from the .import-* folder) a rename; otherwise a copy.
        const bool moved = fromImportTemp && QDir().rename(source, folder);
        if (!moved && !copyDirRecursive(source, folder)) {
            QDir(folder).removeRecursively();
            return fail(QCoreApplication::translate("MeetingStore", "A mappa másolása nem sikerült: %1 → %2").arg(source, folder));
        }
    }

    if (!hasJson)
        m = meetingFromTrackFolder(folder);
    // The folder on disk is the truth: this goes into the index instead of a path brought from another machine (e.g. D:\...).
    m.folder = folder;
    saveMeeting(m);   // meeting.json (fixed folder) + index
    emit meetingAdded(m.id);
    qCInfo(lcStore).noquote() << "Felvétel-mappa behúzva:" << folder << "id:" << m.id;
    return m;
}

void MeetingStore::rebuildIndexFromDisk()
{
    recoverOrphanRecordings();
    QSqlDatabase db = QSqlDatabase::database(m_connName);

    // Tabula rasa: the DB is only a cache, safe to clear.
    {
        QSqlQuery clear(db);
        if (!clear.exec(QStringLiteral("DELETE FROM meetings")))
            qCWarning(lcStore, "MeetingStore: index urites hiba: %s", qPrintable(clear.lastError().text()));
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
        // The folder on disk is the truth: we keep the folder field in sync.
        m.folder = fi.absoluteFilePath();
        if (m.id.isEmpty())
            continue;
        upsertIndex(m);
    }
}

} // namespace tanara::library