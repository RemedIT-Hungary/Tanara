#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/MeetingLayout.h"
#include "tanara/library/store/detail/MeetingJson.h"
#include "tanara/library/store/MeetingArchive.h"
#include "tanara/foundation/log/Logging.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/foundation/ProcessLock.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStorageInfo>
#include <QUuid>
#include <QVariant>

namespace tanara::library {

namespace {

// index.db schema version (PRAGMA user_version). 2: a row stores the folder as (root, dir) instead of
// an absolute path. An older index is dropped and rebuilt from disk (it is only a cache).
constexpr int kSchemaVersion = 2;

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

// A root as configured -> cleaned absolute path ("~" expanded).
QString normRoot(const QString& root)
{
    const QString r = paths::expandHome(root.trimmed());
    return r.isEmpty() ? QString() : normFolder(r);
}

QString joinFolder(const QString& root, const QString& dir)
{
    if (dir.isEmpty()) return QString();
    return root.isEmpty() ? dir : QDir(root).filePath(dir);
}

QString meetingJsonPath(const QString& folder)
{
    return MeetingLayout::path(folder, MeetingLayout::meetingJson());
}

QStringList trackFiles(const QString& folder)
{
    return QDir(folder).entryList({MeetingLayout::trackFilePattern()}, QDir::Files, QDir::Name);
}

bool isMeetingFolder(const QString& folder)
{
    return QFile::exists(meetingJsonPath(folder)) || !trackFiles(folder).isEmpty();
}

// The "id" field of the meeting.json in a folder (empty if absent / unreadable).
QString idInMeetingJson(const QString& jsonPath)
{
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("id")).toString();
}

// Whether a live recording is going into the folder (from any process): recording.lock points to
// this folder and its PID is alive. A stale (dead-PID) lock does not count.
bool isLiveRecordingFolder(const QString& folder)
{
    const ProcessLock::Info lock = ProcessLock::read(recordingLockPath());
    return lock.active && !lock.meetingFolder.isEmpty()
        && normFolder(lock.meetingFolder) == normFolder(folder);
}

// For this long a track file counts as "fresh": one written to more recently than this is very
// likely STILL being written (a live recording in another process), so it is not an orphan.
constexpr qint64 kLiveTrackGraceSecs = 5 * 60;

// Prefix of a folder copy in progress while moving a root (removed if the move is interrupted).
const QString kMovePartPrefix = QStringLiteral(".move-");

qint64 probeDurationMs(const media::MediaTool* media, const QString& path)
{
    if (!media) return 0;
    const media::MediaResult r = media->probe(path, 5000);
    return r.info ? r.info->durationMs() : 0;
}

// Recursive folder copy (the target does not exist yet). On error / cancel false; the caller cleans up
// the half-made target.
bool copyDirRecursive(const QString& from, const QString& to, const std::atomic<bool>* cancel = nullptr)
{
    QDir src(from);
    if (!QDir().mkpath(to)) return false;
    for (const QFileInfo& e : src.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        if (cancel && cancel->load()) return false;
        const QString dst = QDir(to).filePath(e.fileName());
        if (e.isDir()) { if (!copyDirRecursive(e.absoluteFilePath(), dst, cancel)) return false; }
        else if (!QFile::copy(e.absoluteFilePath(), dst)) return false;
    }
    return true;
}

// Relative file path -> size, recursively (hidden files too).
QMap<QString, qint64> fileSizes(const QString& folder)
{
    QMap<QString, qint64> out;
    const QDir base(folder);
    QDirIterator it(folder, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        out.insert(base.relativeFilePath(it.filePath()), it.fileInfo().size());
    }
    return out;
}

qint64 folderBytes(const QString& folder)
{
    qint64 sum = 0;
    for (qint64 v : fileSizes(folder)) sum += v;
    return sum;
}

// The nearest existing folder of a path (for the free-space query of a root not created yet).
QString existingAncestor(QString path)
{
    while (!path.isEmpty() && !QFileInfo(path).isDir()) {
        const QString parent = QFileInfo(path).absolutePath();
        if (parent == path) break;
        path = parent;
    }
    return path;
}

bool isInside(const QString& path, const QString& folder)
{
    return path == folder || path.startsWith(folder + QLatin1Char('/'));
}

} // namespace

// ---- construction / index ----------------------------------------------------------------------

MeetingRepository::MeetingRepository(const QStringList& roots, const QString& metadataDir, QObject* parent)
    : QObject(parent)
    , m_metadataDir(metadataDir.trimmed().isEmpty() ? paths::current().dataDir()
                                                    : paths::expandHome(metadataDir.trimmed()))
{
    setRoots(roots);
    m_connName = QStringLiteral("tanara_meetingrepo_%1")
                     .arg(QUuid::createUuid().toString(QUuid::Id128));
    openDb();
}

MeetingRepository::MeetingRepository(const QString& audioDir, const QString& metadataDir, QObject* parent)
    : MeetingRepository(QStringList{audioDir}, metadataDir, parent)
{
}

MeetingRepository::~MeetingRepository()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connName, false);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(m_connName);
}

void MeetingRepository::setRoots(const QStringList& roots)
{
    QStringList clean;
    for (const QString& r : roots) {
        const QString n = normRoot(r);
        if (!n.isEmpty() && !clean.contains(n)) clean << n;
    }
    m_roots = clean;
    if (!primaryRoot().isEmpty())
        QDir().mkpath(primaryRoot());
}

QString MeetingRepository::rootOf(const QString& meetingFolder)
{
    if (meetingFolder.isEmpty()) return QString();
    return QFileInfo(normFolder(meetingFolder)).absolutePath();
}

int MeetingRepository::meetingFolderCount(const QString& root)
{
    const QString r = normRoot(root);
    if (r.isEmpty() || !QDir(r).exists()) return 0;
    int n = 0;
    for (const QFileInfo& fi : QDir(r).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (isMeetingFolder(fi.absoluteFilePath())) ++n;
    return n;
}

bool MeetingRepository::isRoot(const QString& path) const
{
    return m_roots.contains(normFolder(path));
}

QString MeetingRepository::dbConnectionName() const
{
    return m_connName;
}

void MeetingRepository::openDb()
{
    QDir().mkpath(m_metadataDir);

    const QString dbPath = QDir(m_metadataDir).filePath(paths::files::IndexDb);

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName);
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        qCWarning(lcStore, "MeetingRepository: nem sikerult megnyitni az index.db-t: %s",
                 qPrintable(db.lastError().text()));
        return;
    }
    ensureSchema();
}

void MeetingRepository::ensureSchema()
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    int version = 0;
    {
        QSqlQuery v(db);
        if (v.exec(QStringLiteral("PRAGMA user_version")) && v.next()) version = v.value(0).toInt();
    }
    bool rebuild = false;
    if (version != kSchemaVersion) {
        // An older index (absolute folder paths) or a foreign one: it is only a cache -> dropped and
        // rebuilt from the meeting.json files of the roots.
        QSqlQuery drop(db);
        drop.exec(QStringLiteral("DROP TABLE IF EXISTS meetings"));
        rebuild = version != 0;
        QSqlQuery set(db);
        set.exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion));
    }
    QSqlQuery q(db);
    const bool ok = q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS meetings ("
        " id TEXT PRIMARY KEY,"
        " title TEXT,"
        " root TEXT,"          // the recording root the folder lies in
        " dir TEXT,"           // the folder's name inside the root
        " startedAt TEXT,"
        " durationMs INTEGER,"
        " hasTranscript INTEGER,"
        " hasSummary INTEGER"
        ")"));
    if (!ok)
        qCWarning(lcStore, "MeetingRepository: schema hiba: %s", qPrintable(q.lastError().text()));
    if (rebuild) {
        qCInfo(lcStore) << "MeetingRepository: régi index.db séma — újraépítés a mappákból";
        reindex();
    }
}

QString MeetingRepository::folderFromIndex(const QString& id) const
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT root, dir FROM meetings WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (q.exec() && q.next())
        return joinFolder(q.value(0).toString(), q.value(1).toString());
    return QString();
}

void MeetingRepository::upsertIndex(const Meeting& m)
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    const QString folder = normFolder(m.folder);
    const QString root = rootOf(folder);
    const QString dir = folder.isEmpty() ? QString() : QFileInfo(folder).fileName();
    // ONE folder = ONE row: if an entry with another id was left for the same folder (e.g. another
    // process overwrote the folder's meeting.json with a new id), that row is stale. Deleting it touches
    // ONLY the index, not the disk.
    if (!folder.isEmpty()) {
        QStringList staleIds;
        QSqlQuery stale(db);
        stale.prepare(QStringLiteral("SELECT id, root, dir FROM meetings WHERE id != :id"));
        stale.bindValue(QStringLiteral(":id"), m.id);
        if (stale.exec())
            while (stale.next())
                if (normFolder(joinFolder(stale.value(1).toString(), stale.value(2).toString())) == folder)
                    staleIds << stale.value(0).toString();
        for (const QString& sid : std::as_const(staleIds)) {
            QSqlQuery del(db);
            del.prepare(QStringLiteral("DELETE FROM meetings WHERE id = :id"));
            del.bindValue(QStringLiteral(":id"), sid);
            del.exec();
            qCWarning(lcStore).noquote()
                << "MeetingRepository: elavult index-sor törölve (ugyanaz a mappa, más id):"
                << sid << "->" << m.id << m.folder;
        }
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO meetings (id, title, root, dir, startedAt, durationMs, hasTranscript, hasSummary)"
        " VALUES (:id, :title, :root, :dir, :startedAt, :durationMs, :hasTranscript, :hasSummary)"
        " ON CONFLICT(id) DO UPDATE SET"
        "  title=excluded.title, root=excluded.root, dir=excluded.dir, startedAt=excluded.startedAt,"
        "  durationMs=excluded.durationMs, hasTranscript=excluded.hasTranscript,"
        "  hasSummary=excluded.hasSummary"));
    q.bindValue(QStringLiteral(":id"), m.id);
    q.bindValue(QStringLiteral(":title"), m.title);
    q.bindValue(QStringLiteral(":root"), root);
    q.bindValue(QStringLiteral(":dir"), dir);
    q.bindValue(QStringLiteral(":startedAt"), m.startedAt.toString(Qt::ISODateWithMs));
    q.bindValue(QStringLiteral(":durationMs"), static_cast<qlonglong>(m.durationMs));
    q.bindValue(QStringLiteral(":hasTranscript"), m.hasTranscript ? 1 : 0);
    q.bindValue(QStringLiteral(":hasSummary"), m.hasSummary ? 1 : 0);
    if (!q.exec())
        qCWarning(lcStore, "MeetingRepository: index upsert hiba: %s", qPrintable(q.lastError().text()));
}

// ---- meetings -----------------------------------------------------------------------------------

Meeting MeetingRepository::createMeeting(const QString& title)
{
    Meeting m;
    m.startedAt = QDateTime::currentDateTime();
    m.title     = title;

    const QString shortId = QUuid::createUuid().toString(QUuid::Id128).left(8);
    m.id = shortId;

    const QString stamp   = m.startedAt.toString(QStringLiteral("yyyy-MM-dd_HHmm"));
    const QString folderName = QStringLiteral("%1_%2_%3")
                                   .arg(stamp, slugify(title), shortId);

    const QString folderPath = QDir(primaryRoot()).filePath(folderName);
    QDir().mkpath(folderPath);
    m.folder = folderPath;

    // Writing an empty meeting.json so the folder is immediately "ready".
    saveMeeting(m);

    emit meetingAdded(m.id);
    return m;
}

bool MeetingRepository::writeMeetingFile(const Meeting& m)
{
    if (m.folder.isEmpty())
        return false;
    QDir().mkpath(m.folder);
    // Atomic write (temporary file + rename): on an interrupted write / full disk the earlier
    // meeting.json stays intact, and no other process ever sees a half-written file.
    const QString path = meetingJsonPath(m.folder);
    const QByteArray data = QJsonDocument(toJson(m)).toJson(QJsonDocument::Indented);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        qCWarning(lcStore, "MeetingRepository: nem sikerult irni a meeting.json-t: %s", qPrintable(path));
        return false;
    }
    return true;
}

void MeetingRepository::saveMeeting(const Meeting& m)
{
    if (m.folder.isEmpty())
        return;
    writeMeetingFile(m);
    upsertIndex(m);
    emit meetingUpdated(m.id);
}

Meeting MeetingRepository::load(const QString& id)
{
    // We look the folder up from the index, then load from disk (the source of truth).
    const QString folder = folderFromIndex(id);
    if (folder.isEmpty())
        return Meeting{};
    return readMeetingFolder(folder);
}

QString MeetingRepository::folderOf(const QString& id)
{
    return folderFromIndex(id);
}

Meeting MeetingRepository::readMeetingFolder(const QString& folder)
{
    if (folder.isEmpty())
        return Meeting{};
    QFile f(meetingJsonPath(folder));
    if (!f.open(QIODevice::ReadOnly))
        return Meeting{};
    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return Meeting{};

    Meeting m = meetingFromJson(doc.object());
    m.folder = folder;   // the folder is where meeting.json is
    return m;
}

bool MeetingRepository::deleteMeeting(const QString& id)
{
    if (id.isEmpty())
        return false;
    QSqlDatabase db = QSqlDatabase::database(m_connName);

    // Looking the folder up, then deleting from disk (the source of truth).
    const QString folder = folderFromIndex(id);

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
bool MeetingRepository::folderOwnedOnlyBy(const QString& id, const QString& folder) const
{
    const QString mine = normFolder(folder);
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id, root, dir FROM meetings WHERE id != :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (q.exec()) {
        while (q.next()) {
            if (normFolder(joinFolder(q.value(1).toString(), q.value(2).toString())) != mine) continue;
            qCWarning(lcStore).noquote()
                << "MeetingRepository: a mappa NEM törlődik — másik meeting is erre mutat:"
                << q.value(0).toString() << folder;
            return false;
        }
    }
    const QString diskId = idInMeetingJson(meetingJsonPath(folder));
    if (!diskId.isEmpty() && diskId != id) {
        qCWarning(lcStore).noquote()
            << "MeetingRepository: a mappa NEM törlődik — a meeting.json más id-t tartalmaz:"
            << diskId << "!=" << id << folder;
        return false;
    }
    if (isLiveRecordingFolder(folder)) {
        qCWarning(lcStore).noquote()
            << "MeetingRepository: a mappa NEM törlődik — élő felvétel megy bele:" << folder;
        return false;
    }
    return true;
}

QVector<Meeting> MeetingRepository::loadAll()
{
    QVector<Meeting> out;

    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "SELECT id, title, root, dir, startedAt, durationMs, hasTranscript, hasSummary"
            " FROM meetings ORDER BY startedAt DESC"))) {
        qCWarning(lcStore, "MeetingRepository: loadAll hiba: %s", qPrintable(q.lastError().text()));
        return out;
    }

    QHash<QString, int> byFolder;   // normalized folder -> position in out
    while (q.next()) {
        Meeting m;
        m.id            = q.value(0).toString();
        m.title         = q.value(1).toString();
        m.folder        = joinFolder(q.value(2).toString(), q.value(3).toString());
        m.startedAt     = QDateTime::fromString(q.value(4).toString(), Qt::ISODateWithMs);
        m.durationMs    = q.value(5).toLongLong();
        m.hasTranscript = q.value(6).toInt() != 0;
        m.hasSummary    = q.value(7).toInt() != 0;
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

// A meeting built from track files (track_*.ogg) for a folder that has no meeting.json
// (a crashed recording, or tracks copied over by hand from another machine).
Meeting MeetingRepository::meetingFromTrackFolder(const QString& folder) const
{
    const QFileInfo fi(folder);
    const QStringList oggs = trackFiles(folder);
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

    const int prefix = int(MeetingLayout::trackFilePrefix().size());
    int micNo = 1;
    for (const QString& f : oggs) {
        Track t;
        const QString stem = f.left(f.size() - 4).mid(prefix);   // without "track_" and ".ogg"
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

int MeetingRepository::recoverOrphanRecordings()
{
    int recovered = 0;
    for (const QString& root : std::as_const(m_roots))
        recovered += recoverOrphansIn(root);
    return recovered;
}

int MeetingRepository::recoverOrphansIn(const QString& rootPath)
{
    QDir root(rootPath);
    if (!root.exists()) return 0;
    int recovered = 0;
    const QFileInfoList dirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : dirs) {
        const QString folder = fi.absoluteFilePath();
        if (QFile::exists(meetingJsonPath(folder)))
            continue;
        const QStringList oggs = trackFiles(folder);
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

QString MeetingRepository::stageFolderForImport(const QString& sourceDir, QString* error) const
{
    auto fail = [&](const QString& msg) { if (error) *error = msg; return QString(); };
    const QFileInfo src(sourceDir);
    if (!src.isDir())
        return fail(QCoreApplication::translate("MeetingRepository", "Nincs ilyen mappa: %1").arg(sourceDir));
    const QString source = src.absoluteFilePath();
    if (!isMeetingFolder(source))
        return fail(QCoreApplication::translate("MeetingRepository", "A mappában nincs meeting.json és nincs track_*.ogg sáv: %1").arg(sourceDir));
    const QString audioRoot = QDir(primaryRoot()).absolutePath();
    if (QFileInfo::exists(QDir(audioRoot).filePath(src.fileName())))
        return fail(QCoreApplication::translate("MeetingRepository", "Már van ilyen nevű felvétel: %1 — a meglévőt nem írom felül.")
                        .arg(QDir(audioRoot).filePath(src.fileName())));
    const QString temp = QDir(audioRoot).filePath(MeetingArchive::tempDirPrefix()
                                                  + QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString staged = QDir(temp).filePath(src.fileName());
    if (!QDir().mkpath(audioRoot) || !copyDirRecursive(source, staged)) {
        QDir(temp).removeRecursively();
        return fail(QCoreApplication::translate("MeetingRepository", "A mappa másolása nem sikerült: %1 → %2").arg(source, staged));
    }
    return staged;
}

void MeetingRepository::removeStaleImportFolders(qint64 maxAgeSecs)
{
    const QDateTime cutoff = QDateTime::currentDateTime().addSecs(-maxAgeSecs);
    for (const QString& r : std::as_const(m_roots)) {
        const QDir root(r);
        for (const QFileInfo& fi : root.entryInfoList({MeetingArchive::tempDirPrefix() + QLatin1Char('*'),
                                                       kMovePartPrefix + QLatin1Char('*')},
                                                      QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot))
            if (fi.lastModified() < cutoff) QDir(fi.absoluteFilePath()).removeRecursively();
    }
}

Meeting MeetingRepository::adoptMeetingFolder(const QString& sourceDir, QString* error)
{
    const QString audioRoot = QDir(primaryRoot()).absolutePath();
    const QFileInfo src(sourceDir);
    const QString source = src.absoluteFilePath();

    // Archive import: the source is in the temporary .import-<uuid> folder under the primary root
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
        return fail(QCoreApplication::translate("MeetingRepository", "Nincs ilyen mappa: %1").arg(sourceDir));
    const bool hasJson = QFile::exists(meetingJsonPath(source));
    const bool hasTracks = !trackFiles(source).isEmpty();
    if (!hasJson && !hasTracks)
        return fail(QCoreApplication::translate("MeetingRepository", "A mappában nincs meeting.json és nincs track_*.ogg sáv: %1").arg(sourceDir));

    // Reading meeting.json while still in the source: a faulty / already existing meeting thus leaves
    // nothing behind in the recordings folder.
    Meeting m;
    if (hasJson) {
        QFile f(meetingJsonPath(source));
        QJsonParseError err{};
        const QJsonDocument doc = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll(), &err)
                                                              : QJsonDocument();
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            return fail(QCoreApplication::translate("MeetingRepository", "A meeting.json nem olvasható: %1").arg(meetingJsonPath(source)));
        m = meetingFromJson(doc.object());
        if (m.id.isEmpty())
            m.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    // Target: directly under a recording root it stays in place; otherwise into the primary root,
    // with the same name.
    QString folder = source;
    if (!isRoot(parentInfo.absoluteFilePath())) {
        folder = QDir(audioRoot).filePath(src.fileName());
        if (QFileInfo::exists(folder))
            return fail(QCoreApplication::translate("MeetingRepository", "Már van ilyen nevű felvétel: %1 — a meglévőt nem írom felül.").arg(folder));
    }
    // The same meeting (id) is already in the library, in another folder.
    if (hasJson) {
        const Meeting existing = load(m.id);
        if (!existing.id.isEmpty()
            && QDir(existing.folder).absolutePath() != QDir(folder).absolutePath())
            return fail(QCoreApplication::translate("MeetingRepository", "Ez a megbeszélés már szerepel a könyvtárban: „%1” (%2).")
                            .arg(existing.title, existing.folder));
    }

    if (folder != source) {
        if (!QDir().mkpath(audioRoot))
            return fail(QCoreApplication::translate("MeetingRepository", "A felvételek mappája nem hozható létre: %1").arg(audioRoot));
        // On the same volume (e.g. from the .import-* folder) a rename; otherwise a copy.
        const bool moved = fromImportTemp && QDir().rename(source, folder);
        if (!moved && !copyDirRecursive(source, folder)) {
            QDir(folder).removeRecursively();
            return fail(QCoreApplication::translate("MeetingRepository", "A mappa másolása nem sikerült: %1 → %2").arg(source, folder));
        }
    }

    if (!hasJson)
        m = meetingFromTrackFolder(folder);
    // The folder on disk is the truth.
    m.folder = folder;
    saveMeeting(m);   // meeting.json + index
    emit meetingAdded(m.id);
    qCInfo(lcStore).noquote() << "Felvétel-mappa behúzva:" << folder << "id:" << m.id;
    return m;
}

void MeetingRepository::reindex()
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);

    // Tabula rasa: the DB is only a cache, safe to clear.
    {
        QSqlQuery clear(db);
        if (!clear.exec(QStringLiteral("DELETE FROM meetings")))
            qCWarning(lcStore, "MeetingRepository: index urites hiba: %s", qPrintable(clear.lastError().text()));
    }

    for (const QString& rootPath : std::as_const(m_roots)) {
        QDir root(rootPath);
        if (!root.exists())
            continue;
        const QFileInfoList dirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& fi : dirs) {
            const Meeting m = readMeetingFolder(fi.absoluteFilePath());
            if (m.id.isEmpty())
                continue;
            upsertIndex(m);
        }
    }
}

void MeetingRepository::rebuildIndexFromDisk()
{
    recoverOrphanRecordings();
    reindex();
}

// ---- moving a root ------------------------------------------------------------------------------

MeetingRepository::MovePlan MeetingRepository::planMove(const QString& fromRoot, const QString& toRoot) const
{
    MovePlan plan;
    const QString from = normRoot(fromRoot);
    const QString to = normRoot(toRoot);
    if (from.isEmpty() || to.isEmpty()) { plan.error = QCoreApplication::translate("MeetingRepository", "Hiányzó mappa."); return plan; }
    if (!QDir(from).exists()) { plan.error = QCoreApplication::translate("MeetingRepository", "Nincs ilyen mappa: %1").arg(from); return plan; }
    if (isInside(to, from) || isInside(from, to)) {
        plan.error = QCoreApplication::translate("MeetingRepository", "A cél nem lehet a forrás mappán belül (vagy fordítva): %1 → %2").arg(from, to);
        return plan;
    }
    if (QFileInfo(to).exists() && !QFileInfo(to).isDir()) {
        plan.error = QCoreApplication::translate("MeetingRepository", "A cél nem mappa: %1").arg(to);
        return plan;
    }
    for (const QFileInfo& fi : QDir(from).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString folder = fi.absoluteFilePath();
        if (!isMeetingFolder(folder)) continue;
        if (isLiveRecordingFolder(folder)) { plan.skipped << fi.fileName(); continue; }
        plan.folders << fi.fileName();
    }
    const QStorageInfo src(from);
    const QStorageInfo dst(existingAncestor(to));
    plan.sameFileSystem = src.isValid() && dst.isValid() && src.device() == dst.device();
    plan.bytesAvailable = dst.isValid() ? dst.bytesAvailable() : -1;
    if (!plan.sameFileSystem) {
        for (const QString& name : std::as_const(plan.folders)) {
            const QString target = QDir(to).filePath(name);
            if (!QFileInfo::exists(target)) plan.bytes += folderBytes(QDir(from).filePath(name));
        }
        // A small reserve: a filled-up disk would break the copy (and everything else) halfway.
        const qint64 reserve = 64LL * 1024 * 1024;
        if (plan.bytesAvailable >= 0 && plan.bytes + reserve > plan.bytesAvailable)
            plan.error = QCoreApplication::translate("MeetingRepository", "Nincs elég hely a célon: kell %1 MB, szabad %2 MB.")
                             .arg((plan.bytes + reserve) / (1024 * 1024)).arg(plan.bytesAvailable / (1024 * 1024));
    }
    return plan;
}

MeetingRepository::MoveResult MeetingRepository::moveMeetings(const QString& fromRoot, const QString& toRoot,
                                                              const std::atomic<bool>* cancel,
                                                              const MoveProgress& progress, MoveMode mode)
{
    MoveResult res;
    const MovePlan plan = planMove(fromRoot, toRoot);
    res.skipped = plan.skipped;
    if (!plan.error.isEmpty()) { res.error = plan.error; return res; }
    const QString from = normRoot(fromRoot);
    const QString to = normRoot(toRoot);
    if (!QDir().mkpath(to)) { res.error = QCoreApplication::translate("MeetingRepository", "A cél mappa nem hozható létre: %1").arg(to); return res; }
    // A partial copy left by an interrupted earlier run is never a valid meeting: start over.
    for (const QFileInfo& fi : QDir(to).entryInfoList({kMovePartPrefix + QLatin1Char('*')},
                                                      QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot))
        QDir(fi.absoluteFilePath()).removeRecursively();

    const int total = int(plan.folders.size());
    int done = 0;
    for (const QString& name : plan.folders) {
        if (cancel && cancel->load()) { res.cancelled = true; break; }
        if (progress) progress(done, total, name);
        const QString source = QDir(from).filePath(name);
        const QString target = QDir(to).filePath(name);
        // A recording may have started since the plan was made.
        if (isLiveRecordingFolder(source)) { res.skipped << name; ++done; continue; }

        bool placed = false;
        if (QFileInfo::exists(target)) {
            // Left by an interrupted run after the copy was complete (the source was not deleted yet):
            // identical content -> just finish (delete the source). Anything else is a name clash.
            if (fileSizes(source) != fileSizes(target)) {
                res.failed << QCoreApplication::translate("MeetingRepository", "%1: a célban már van ilyen nevű, eltérő mappa").arg(name);
                ++done;
                continue;
            }
            placed = true;
        }
        if (!placed && plan.sameFileSystem && mode == MoveMode::Auto)
            placed = QDir().rename(source, target);
        if (!placed) {
            const QString part = QDir(to).filePath(kMovePartPrefix + name);
            QDir(part).removeRecursively();
            const bool copied = copyDirRecursive(source, part, cancel);
            if (!copied) {
                QDir(part).removeRecursively();
                if (cancel && cancel->load()) { res.cancelled = true; break; }
                res.failed << QCoreApplication::translate("MeetingRepository", "%1: a másolás nem sikerült").arg(name);
                ++done;
                continue;
            }
            if (fileSizes(source) != fileSizes(part)) {
                QDir(part).removeRecursively();
                res.failed << QCoreApplication::translate("MeetingRepository", "%1: a másolat nem egyezik a forrással").arg(name);
                ++done;
                continue;
            }
            if (!QDir().rename(part, target)) {
                QDir(part).removeRecursively();
                res.failed << QCoreApplication::translate("MeetingRepository", "%1: a másolat nem nevezhető át").arg(name);
                ++done;
                continue;
            }
        }
        // The copy is in place and verified: the source goes (a rename already moved it).
        if (QFileInfo::exists(source) && !QDir(source).removeRecursively())
            res.failed << QCoreApplication::translate("MeetingRepository", "%1: a forrás mappa nem törölhető (a másolat a célban van)").arg(name);
        const Meeting m = readMeetingFolder(target);
        if (!m.id.isEmpty()) {
            upsertIndex(m);
            emit meetingUpdated(m.id);
        }
        res.moved << name;
        ++done;
        qCInfo(lcStore).noquote() << "Megbeszélés-mappa áthelyezve:" << source << "->" << target;
    }
    if (progress) progress(done, total, QString());
    return res;
}

} // namespace tanara::library
