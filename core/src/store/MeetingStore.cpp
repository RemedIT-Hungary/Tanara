#include "tanara/store/MeetingStore.h"
#include "tanara/store/JsonSerialization.h"
#include "tanara/store/MeetingArchive.h"
#include "tanara/SettingsManager.h"
#include "tanara/Logging.h"
#include "tanara/Paths.h"
#include "tanara/detect/RecordingLock.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QProcess>
#include <QDateTime>
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

// Mappa-utak összehasonlításához: tisztított abszolút út (záró perjel, „..” nélkül).
QString normFolder(const QString& folder)
{
    return folder.isEmpty() ? QString() : QDir::cleanPath(QDir(folder).absolutePath());
}

// A mappában lévő meeting.json „id” mezője (üres, ha nincs / nem olvasható).
QString idInMeetingJson(const QString& jsonPath)
{
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("id")).toString();
}

// Ennyi ideig számít „frissnek” egy sáv-fájl: amibe ennél frissebben írtak, azt nagy
// valószínűséggel MÉG ÍRJÁK (élő felvétel egy másik folyamatban) → nem árva.
constexpr qint64 kLiveTrackGraceSecs = 5 * 60;

} // namespace

MeetingStore::MeetingStore(const QString& audioDir,
                           const QString& metadataDir,
                           QObject* parent)
    : QObject(parent)
    , m_audioDir(expandHome(audioDir))
    , m_metadataDir(metadataDir.isEmpty() ? paths::defaultMetadataDir() : expandHome(metadataDir))
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
        m_metadataDir = paths::resolveMetadataDir(settings->settings().metadataDir);
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
    // EGY mappa = EGY sor: ha ugyanarra a mappára más id-vel maradt bejegyzés (pl. a mappa
    // meeting.json-ját egy másik folyamat új id-vel írta felül), az a sor elavult. A törlése
    // CSAK az indexet érinti, a lemezt nem.
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

    // Atomikus írás (ideiglenes fájl + átnevezés): megszakadt írás / betelt lemez esetén a
    // korábbi meeting.json sértetlen marad, és más folyamat sosem lát félkész fájlt.
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

bool MeetingStore::deleteMeeting(const QString& id)
{
    if (id.isEmpty())
        return false;
    QSqlDatabase db = QSqlDatabase::database(m_connName);

    // A mappa kikeresése, majd a lemezről törlés (az igazság forrása).
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

// Törölhető-e a mappa a megadott meeting törlésekor? NEM, ha
//  - egy másik, még listázott meeting-id is erre a mappára mutat,
//  - a mappában lévő meeting.json már MÁS id-t tartalmaz (a mappa másé lett),
//  - a mappába épp élő felvétel megy (recording.lock élő PID-del).
// Ilyenkor csak az index-sor törlődik, a felvétel a lemezen marad.
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

// Élő felvétel megy-e a mappába (bármelyik folyamatból): a recording.lock erre a mappára
// mutat és a PID-je él. Elavult (halott PID-ű) lock nem számít.
bool MeetingStore::isLiveRecordingFolder(const QString& folder) const
{
    const RecordingLock::Info lock =
        RecordingLock::read(QDir(m_metadataDir).filePath(QStringLiteral("recording.lock")));
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

    QHash<QString, int> byFolder;   // normalizált mappa → hely az out-ban
    while (q.next()) {
        Meeting m;
        m.id            = q.value(0).toString();
        m.title         = q.value(1).toString();
        m.folder        = q.value(2).toString();
        m.startedAt     = QDateTime::fromString(q.value(3).toString(), Qt::ISODateWithMs);
        m.durationMs    = q.value(4).toLongLong();
        m.hasTranscript = q.value(5).toInt() != 0;
        m.hasSummary    = q.value(6).toInt() != 0;
        // Egy mappa SOSEM szerepelhet kétszer (régi indexben maradhatott ilyen): az a sor
        // marad, amelyiknek az id-je a mappa meeting.json-jában áll; különben az első.
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
qint64 probeDurationMs(const QString& path)
{
    QProcess p;
    p.start(QStringLiteral("ffprobe"), {QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
        QStringLiteral("-of"), QStringLiteral("csv=p=0"), path});
    if (!p.waitForFinished(5000)) { p.kill(); return 0; }
    return qint64(QString::fromUtf8(p.readAllStandardOutput()).trimmed().toDouble() * 1000.0);
}
} // namespace

// Sáv-fájlokból (track_*.ogg) épített meeting egy olyan mappához, amelynek nincs meeting.json-ja
// (összeomlott felvétel, vagy más gépről kézzel átmásolt sávok).
Meeting MeetingStore::meetingFromTrackFolder(const QString& folder) const
{
    const QFileInfo fi(folder);
    const QStringList oggs = QDir(folder).entryList({QStringLiteral("track_*.ogg")}, QDir::Files, QDir::Name);
    Meeting m;
    m.id     = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m.folder = folder;
    // Mappanév: "yyyy-MM-dd_HHmm_<cím-slug>" (RecordingSession::start írja így).
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
        const QString stem = f.left(f.size() - 4).mid(6);   // "track_" + ".ogg" nélkül
        t.id         = stem;
        t.file       = f;
        t.deviceName = QString(stem).replace(QLatin1Char('-'), QLatin1Char(' '));
        const bool loop = stem.startsWith(QStringLiteral("monitor-of")) || stem.contains(QStringLiteral("loopback"));
        t.kind         = loop ? TrackKind::Loopback : TrackKind::Mic;
        t.speakerLabel = loop ? QStringLiteral("Rendszer")
                              : QStringLiteral("Mikrofon ") + QString::number(micNo++);
        t.active = true;
        m.durationMs = qMax(m.durationMs, probeDurationMs(QDir(folder).filePath(f)));
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

        // NEM árva, amit MÉG ÍRNAK: a RecordingSession csak a felvétel VÉGÉN ír meeting.json-t,
        // így egy másik folyamat (tanara --record) élő felvétele ugyanúgy néz ki, mint egy
        // összeomlott. Kimarad, ha a recording.lock (élő PID-del) erre a mappára mutat, vagy
        // ha a legfrissebb sáv-fájlba pár percen belül még írtak. (A következő indításkor /
        // index-újraépítéskor újra sorra kerül.)
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
// Mappa rekurzív másolása (a cél még nem létezik). Hiba esetén false, a félig kész célt a hívó takarítja.
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

Meeting MeetingStore::adoptMeetingFolder(const QString& sourceDir, QString* error)
{
    const QString audioRoot = QDir(m_audioDir).absolutePath();
    const QFileInfo src(sourceDir);
    const QString source = src.absoluteFilePath();

    // Archívum-import: a forrás a felvételek mappája alatti .import-<uuid> ideiglenes mappában
    // van → áthelyezés (nem másolás), és az ideiglenes mappa a végén MINDIG törlődik.
    const QFileInfo parentInfo(src.absolutePath());
    const bool fromImportTemp = parentInfo.fileName().startsWith(MeetingArchive::tempDirPrefix())
        && QDir(parentInfo.absolutePath()).absolutePath() == audioRoot;
    struct TempCleanup {
        QString dir;
        ~TempCleanup() { if (!dir.isEmpty()) QDir(dir).removeRecursively(); }
    } tempCleanup{fromImportTemp ? parentInfo.absoluteFilePath() : QString()};

    auto fail = [&](const QString& msg) { if (error) *error = msg; return Meeting(); };
    if (!src.isDir())
        return fail(QStringLiteral("Nincs ilyen mappa: %1").arg(sourceDir));
    const bool hasJson = QFile::exists(meetingJsonPath(source));
    const bool hasTracks = !QDir(source).entryList({QStringLiteral("track_*.ogg")}, QDir::Files).isEmpty();
    if (!hasJson && !hasTracks)
        return fail(QStringLiteral("A mappában nincs meeting.json és nincs track_*.ogg sáv: %1").arg(sourceDir));

    // A meeting.json beolvasása MÉG a forrásból: a hibás / már meglévő megbeszélés így semmit
    // nem hagy maga után a felvételek mappájában.
    Meeting m;
    if (hasJson) {
        QFile f(meetingJsonPath(source));
        QJsonParseError err{};
        const QJsonDocument doc = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll(), &err)
                                                              : QJsonDocument();
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            return fail(QStringLiteral("A meeting.json nem olvasható: %1").arg(meetingJsonPath(source)));
        m = meetingFromJson(doc.object());
        if (m.id.isEmpty())
            m.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    // Cél: a felvételek mappája alatt, ugyanazzal a névvel. Ha már ott van, helyben marad.
    QString folder = source;
    if (parentInfo.absoluteFilePath() != audioRoot) {
        folder = QDir(audioRoot).filePath(src.fileName());
        if (QFileInfo::exists(folder))
            return fail(QStringLiteral("Már van ilyen nevű felvétel: %1 — a meglévőt nem írom felül.").arg(folder));
    }
    // Ugyanaz a megbeszélés (azonosító) már a könyvtárban van, másik mappában.
    if (hasJson) {
        const Meeting existing = load(m.id);
        if (!existing.id.isEmpty()
            && QDir(existing.folder).absolutePath() != QDir(folder).absolutePath())
            return fail(QStringLiteral("Ez a megbeszélés már szerepel a könyvtárban: „%1” (%2).")
                            .arg(existing.title, existing.folder));
    }

    if (folder != source) {
        if (!QDir().mkpath(audioRoot))
            return fail(QStringLiteral("A felvételek mappája nem hozható létre: %1").arg(audioRoot));
        // Ugyanazon a köteten (pl. a .import-* mappából) átnevezés; különben másolás.
        const bool moved = fromImportTemp && QDir().rename(source, folder);
        if (!moved && !copyDirRecursive(source, folder)) {
            QDir(folder).removeRecursively();
            return fail(QStringLiteral("A mappa másolása nem sikerült: %1 → %2").arg(source, folder));
        }
    }

    if (!hasJson)
        m = meetingFromTrackFolder(folder);
    // A lemezen lévő mappa az igazság: a más gépről hozott (pl. D:\…) útvonal helyett ez megy az indexbe.
    m.folder = folder;
    saveMeeting(m);   // meeting.json (javított folder) + index
    emit meetingAdded(m.id);
    qCInfo(lcStore).noquote() << "Felvétel-mappa behúzva:" << folder << "id:" << m.id;
    return m;
}

void MeetingStore::rebuildIndexFromDisk()
{
    recoverOrphanRecordings();
    QSqlDatabase db = QSqlDatabase::database(m_connName);

    // Tabula rasa: a DB csak cache, nyugodtan üríthető.
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
        // A lemezen lévő mappa az igazság: a folder mezőt szinkronban tartjuk.
        m.folder = fi.absoluteFilePath();
        if (m.id.isEmpty())
            continue;
        upsertIndex(m);
    }
}

} // namespace tanara
