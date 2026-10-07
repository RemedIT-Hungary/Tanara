#include "tanara/store/MeetingArchive.h"
#include "tanara/Logging.h"

#include <miniz.h>

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <cstring>

namespace tanara {

QString libraryVersion();   // Version.cpp

namespace {

// A manifeszt legfeljebb ekkora lehet (védelem a felfújt bejegyzés ellen).
constexpr mz_uint64 kMaxManifestBytes = 1024 * 1024;

// Már tömörített formátumok: tárolva (gyors, a tömörítés úgysem nyerne rajtuk).
bool storeOnly(const QString& name)
{
    static const QStringList exts = {QStringLiteral(".ogg"), QStringLiteral(".opus"), QStringLiteral(".mp3"),
                                     QStringLiteral(".m4a"), QStringLiteral(".flac"), QStringLiteral(".bin")};
    for (const QString& e : exts)
        if (name.endsWith(e, Qt::CaseInsensitive)) return true;
    return false;
}

// Haladás-számláló: csak a változó százalékot jelenti.
struct ProgressTracker {
    MeetingArchive::Progress fn;
    qint64 total = 0;
    qint64 done = 0;
    int last = -1;
    void add(qint64 bytes)
    {
        done += bytes;
        report(total > 0 ? int(qMin<qint64>(100, done * 100 / total)) : 100);
    }
    void report(int pct)
    {
        if (pct == last || !fn) return;
        last = pct;
        fn(pct);
    }
};

// ---- írás -------------------------------------------------------------------------------

struct WriteCtx {
    QFile* out = nullptr;
};

size_t writeToFile(void* opaque, mz_uint64 ofs, const void* buf, size_t n)
{
    auto* ctx = static_cast<WriteCtx*>(opaque);
    if (qint64(ofs) != ctx->out->pos() && !ctx->out->seek(qint64(ofs))) return 0;
    const qint64 w = ctx->out->write(static_cast<const char*>(buf), qint64(n));
    return w < 0 ? 0 : size_t(w);
}

struct ReadSourceCtx {
    QFile* in = nullptr;
    ProgressTracker* progress = nullptr;
    const std::atomic<bool>* cancel = nullptr;
};

size_t readFromSource(void* opaque, mz_uint64 ofs, void* buf, size_t n)
{
    auto* ctx = static_cast<ReadSourceCtx*>(opaque);
    if (ctx->cancel && ctx->cancel->load()) return 0;   // a miniz hibával áll le
    if (qint64(ofs) != ctx->in->pos() && !ctx->in->seek(qint64(ofs))) return 0;
    const qint64 r = ctx->in->read(static_cast<char*>(buf), qint64(n));
    if (r <= 0) return 0;
    ctx->progress->add(r);
    return size_t(r);
}

QJsonObject manifestJson(const Meeting& m, const QVector<Tag>& tags)
{
    QJsonArray tagArr;
    for (const Tag& t : tags)
        tagArr.append(QJsonObject{{QStringLiteral("id"), t.id}, {QStringLiteral("name"), t.name}});
    QJsonObject meeting{
        {QStringLiteral("id"), m.id},
        {QStringLiteral("title"), m.title},
        {QStringLiteral("startedAt"), m.startedAt.toString(Qt::ISODateWithMs)},
        {QStringLiteral("durationMs"), double(m.durationMs)},
        {QStringLiteral("trackCount"), int(m.tracks.size())},
        {QStringLiteral("tags"), tagArr},
    };
    return QJsonObject{
        {QStringLiteral("version"), MeetingArchive::kFormatVersion},
        {QStringLiteral("app"), libraryVersion()},
        {QStringLiteral("exportedAt"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs)},
        {QStringLiteral("meeting"), meeting},
    };
}

ArchiveManifest manifestFromJson(const QJsonObject& o)
{
    ArchiveManifest a;
    a.version = o.value(QStringLiteral("version")).toInt(0);
    a.app = o.value(QStringLiteral("app")).toString();
    a.exportedAt = QDateTime::fromString(o.value(QStringLiteral("exportedAt")).toString(), Qt::ISODateWithMs);
    const QJsonObject m = o.value(QStringLiteral("meeting")).toObject();
    a.meetingId = m.value(QStringLiteral("id")).toString();
    a.title = m.value(QStringLiteral("title")).toString();
    a.startedAt = QDateTime::fromString(m.value(QStringLiteral("startedAt")).toString(), Qt::ISODateWithMs);
    a.durationMs = qint64(m.value(QStringLiteral("durationMs")).toDouble());
    a.trackCount = m.value(QStringLiteral("trackCount")).toInt();
    for (const QJsonValue& v : m.value(QStringLiteral("tags")).toArray()) {
        Tag t;
        t.id = v.toObject().value(QStringLiteral("id")).toString();
        t.name = v.toObject().value(QStringLiteral("name")).toString();
        if (!t.name.trimmed().isEmpty()) a.tags.append(t);
    }
    return a;
}

// ---- olvasás ----------------------------------------------------------------------------

size_t readFromZip(void* opaque, mz_uint64 ofs, void* buf, size_t n)
{
    auto* in = static_cast<QFile*>(opaque);
    if (qint64(ofs) != in->pos() && !in->seek(qint64(ofs))) return 0;
    const qint64 r = in->read(static_cast<char*>(buf), qint64(n));
    return r < 0 ? 0 : size_t(r);
}

struct ExtractCtx {
    QFile* out = nullptr;
    ProgressTracker* progress = nullptr;
};

size_t extractToFile(void* opaque, mz_uint64 ofs, const void* buf, size_t n)
{
    auto* ctx = static_cast<ExtractCtx*>(opaque);
    if (qint64(ofs) != ctx->out->pos() && !ctx->out->seek(qint64(ofs))) return 0;
    const qint64 w = ctx->out->write(static_cast<const char*>(buf), qint64(n));
    if (w <= 0) return 0;
    ctx->progress->add(w);
    return size_t(w);
}

// Biztonságos-e a bejegyzés útja: relatív, perjeles, nincs „..” / „.” / üres elem, nincs
// meghajtó-betűjel és visszaper.
bool safeEntryPath(const QString& p)
{
    if (p.isEmpty() || p.startsWith(QLatin1Char('/')) || p.contains(QLatin1Char('\\'))
        || p.contains(QLatin1Char(':')) || p.contains(QChar(0)))
        return false;
    QString s = p;
    if (s.endsWith(QLatin1Char('/'))) s.chop(1);   // mappa-bejegyzés
    for (const QString& part : s.split(QLatin1Char('/'))) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String(".."))
            return false;
    }
    return true;
}

QString zipErrorText(mz_zip_archive* zip)
{
    return QString::fromUtf8(mz_zip_get_error_string(mz_zip_get_last_error(zip)));
}

} // namespace

QString MeetingArchive::suggestedFileName(const Meeting& m)
{
    QString base = QFileInfo(m.folder).fileName();
    if (base.isEmpty()) base = m.id;
    return base + fileSuffix();
}

bool MeetingArchive::isExcluded(const QString& relativePath)
{
    const QStringList parts = relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (int i = 0; i + 1 < parts.size(); ++i)
        if (parts.at(i).startsWith(QLatin1String("transcript-backup-"))) return true;
    const QString name = parts.isEmpty() ? relativePath : parts.last();
    return name == QLatin1String("recording.lock") || name.endsWith(QLatin1String(".tmp"))
        || name.endsWith(QLatin1String(".embeddings.bin")) || name == QLatin1String("profile.json")
        || name.startsWith(QLatin1String("transcript-backup-"));
}

bool MeetingArchive::exportMeeting(const Meeting& m, const QString& zipPath, QString* error,
                                   Progress progress, const QVector<Tag>& tags,
                                   const std::atomic<bool>* cancel)
{
    auto fail = [&](const QString& msg) { if (error) *error = msg; return false; };
    const QFileInfo folderInfo(m.folder);
    if (m.folder.isEmpty() || !folderInfo.isDir())
        return fail(QCoreApplication::translate("MeetingArchive", "A megbeszélés mappája nem található: %1").arg(m.folder));
    const QString folderName = folderInfo.fileName();
    const QDir root(folderInfo.absoluteFilePath());

    // A becsomagolandó fájlok (rendezve, hogy az archívum determinisztikus legyen).
    QStringList files;
    qint64 totalBytes = 0;
    QDirIterator it(root.absolutePath(), QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString abs = it.next();
        const QString rel = root.relativeFilePath(abs);
        if (isExcluded(rel)) continue;
        files << rel;
        totalBytes += it.fileInfo().size();
    }
    files.sort();

    const QString partPath = zipPath + QStringLiteral(".part");
    QFile out(partPath);
    if (!out.open(QIODevice::ReadWrite | QIODevice::Truncate))
        return fail(QCoreApplication::translate("MeetingArchive", "Az archívum nem hozható létre: %1 (%2)").arg(zipPath, out.errorString()));

    WriteCtx wctx{&out};
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    zip.m_pWrite = &writeToFile;
    zip.m_pIO_opaque = &wctx;
    auto abort = [&](const QString& msg) {
        mz_zip_writer_end(&zip);
        out.close();
        QFile::remove(partPath);
        return fail(msg);
    };
    if (!mz_zip_writer_init_v2(&zip, 0, 0))
        return abort(QCoreApplication::translate("MeetingArchive", "Az archívum írása nem sikerült: %1").arg(zipErrorText(&zip)));

    // Manifeszt elöl (a fogadó oldal így gyorsan ellenőrizhet).
    const QByteArray manifest = QJsonDocument(manifestJson(m, tags)).toJson(QJsonDocument::Indented);
    if (!mz_zip_writer_add_mem(&zip, manifestFileName().toUtf8().constData(), manifest.constData(),
                               size_t(manifest.size()), MZ_DEFAULT_LEVEL))
        return abort(QCoreApplication::translate("MeetingArchive", "Az archívum írása nem sikerült: %1").arg(zipErrorText(&zip)));

    ProgressTracker pt{std::move(progress), totalBytes};
    pt.report(0);
    for (const QString& rel : files) {
        if (cancel && cancel->load()) return abort(QCoreApplication::translate("MeetingArchive", "Megszakítva."));
        QFile in(root.filePath(rel));
        if (!in.open(QIODevice::ReadOnly))
            return abort(QCoreApplication::translate("MeetingArchive", "A fájl nem olvasható: %1 (%2)").arg(in.fileName(), in.errorString()));
        const QByteArray name = (folderName + QLatin1Char('/') + rel).toUtf8();
        const MZ_TIME_T mtime = MZ_TIME_T(QFileInfo(in).lastModified().toSecsSinceEpoch());
        ReadSourceCtx rctx{&in, &pt, cancel};
        const mz_uint level = storeOnly(rel) ? MZ_NO_COMPRESSION : MZ_DEFAULT_LEVEL;
        if (!mz_zip_writer_add_read_buf_callback(&zip, name.constData(), &readFromSource, &rctx,
                                                 mz_uint64(in.size()), &mtime, nullptr, 0, level,
                                                 nullptr, 0, nullptr, 0)) {
            if (cancel && cancel->load()) return abort(QCoreApplication::translate("MeetingArchive", "Megszakítva."));
            return abort(QCoreApplication::translate("MeetingArchive", "Az archívum írása nem sikerült (%1): %2").arg(rel, zipErrorText(&zip)));
        }
    }
    if (!mz_zip_writer_finalize_archive(&zip))
        return abort(QCoreApplication::translate("MeetingArchive", "Az archívum lezárása nem sikerült: %1").arg(zipErrorText(&zip)));
    mz_zip_writer_end(&zip);
    if (!out.flush()) {
        out.close();
        QFile::remove(partPath);
        return fail(QCoreApplication::translate("MeetingArchive", "Az archívum írása nem sikerült: %1").arg(out.errorString()));
    }
    out.close();

    if (QFile::exists(zipPath) && !QFile::remove(zipPath)) {
        QFile::remove(partPath);
        return fail(QCoreApplication::translate("MeetingArchive", "A meglévő fájl nem írható felül: %1").arg(zipPath));
    }
    if (!QFile::rename(partPath, zipPath)) {
        QFile::remove(partPath);
        return fail(QCoreApplication::translate("MeetingArchive", "Az archívum nem nevezhető át: %1").arg(zipPath));
    }
    pt.report(100);
    qCInfo(lcStore).noquote() << "Megbeszélés exportálva:" << m.id << "→" << zipPath
                              << QStringLiteral("(%1 fájl)").arg(files.size());
    return true;
}

QString MeetingArchive::importArchive(const QString& zipPath, const QString& tempRoot,
                                      QString* error, Progress progress, ArchiveManifest* manifestOut)
{
    auto fail = [&](const QString& msg) { if (error) *error = msg; return QString(); };
    QFile in(zipPath);
    if (!in.open(QIODevice::ReadOnly))
        return fail(QCoreApplication::translate("MeetingArchive", "Az archívum nem nyitható meg: %1 (%2)").arg(zipPath, in.errorString()));

    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    zip.m_pRead = &readFromZip;
    zip.m_pIO_opaque = &in;
    if (!mz_zip_reader_init(&zip, mz_uint64(in.size()), 0))
        return fail(QCoreApplication::translate("MeetingArchive", "A fájl nem érvényes ZIP-archívum: %1").arg(zipPath));
    struct ReaderGuard {
        mz_zip_archive* z;
        ~ReaderGuard() { mz_zip_reader_end(z); }
    } readerGuard{&zip};

    // 1) Bejegyzések ellenőrzése — még semmi nem kerül a lemezre.
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    int manifestIndex = -1;
    QString topDir;
    bool hasMeetingJson = false, hasTrack = false;
    qint64 totalBytes = 0;
    QVector<QPair<mz_uint, QString>> entries;   // (index, relatív út a felső mappán belül)
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st))
            return fail(QCoreApplication::translate("MeetingArchive", "Sérült archívum: %1").arg(zipErrorText(&zip)));
        const QString name = QString::fromUtf8(st.m_filename);
        if (!safeEntryPath(name))
            return fail(QCoreApplication::translate("MeetingArchive", "Az archívum tiltott útvonalat tartalmaz: %1").arg(name));
        if (st.m_is_encrypted || !st.m_is_supported)
            return fail(QCoreApplication::translate("MeetingArchive", "Az archívum titkosított vagy nem támogatott bejegyzést tartalmaz: %1").arg(name));
        if (name == manifestFileName()) {
            manifestIndex = int(i);
            continue;
        }
        const int slash = name.indexOf(QLatin1Char('/'));
        if (slash < 0)
            return fail(QCoreApplication::translate("MeetingArchive", "Az archívum gyökerében ismeretlen fájl van: %1").arg(name));
        const QString top = name.left(slash);
        if (top.startsWith(QLatin1Char('.')))
            return fail(QCoreApplication::translate("MeetingArchive", "Az archívum tiltott útvonalat tartalmaz: %1").arg(name));
        if (topDir.isEmpty()) topDir = top;
        else if (top != topDir)
            return fail(QCoreApplication::translate("MeetingArchive", "Az archívumban több megbeszélés-mappa van (%1, %2) — egyszerre egy hozható be.")
                            .arg(topDir, top));
        if (st.m_is_directory) continue;
        const QString rel = name.mid(slash + 1);
        if (rel == QLatin1String("meeting.json")) hasMeetingJson = true;
        if (!rel.contains(QLatin1Char('/')) && rel.startsWith(QLatin1String("track_"))
            && rel.endsWith(QLatin1String(".ogg")))
            hasTrack = true;
        entries.append({i, rel});
        totalBytes += qint64(st.m_uncomp_size);
    }
    if (manifestIndex < 0)
        return fail(QCoreApplication::translate("MeetingArchive", "Ez nem Tanara-archívum (hiányzik a %1).").arg(manifestFileName()));

    mz_zip_archive_file_stat mst;
    if (!mz_zip_reader_file_stat(&zip, mz_uint(manifestIndex), &mst) || mst.m_uncomp_size > kMaxManifestBytes)
        return fail(QCoreApplication::translate("MeetingArchive", "Sérült archívum-manifeszt."));
    size_t msize = 0;
    void* mbuf = mz_zip_reader_extract_to_heap(&zip, mz_uint(manifestIndex), &msize, 0);
    if (!mbuf) return fail(QCoreApplication::translate("MeetingArchive", "Sérült archívum-manifeszt: %1").arg(zipErrorText(&zip)));
    const QByteArray mdata(static_cast<const char*>(mbuf), qsizetype(msize));
    mz_free(mbuf);
    QJsonParseError perr{};
    const QJsonDocument mdoc = QJsonDocument::fromJson(mdata, &perr);
    if (perr.error != QJsonParseError::NoError || !mdoc.isObject())
        return fail(QCoreApplication::translate("MeetingArchive", "Sérült archívum-manifeszt."));
    const ArchiveManifest manifest = manifestFromJson(mdoc.object());
    if (manifest.version != kFormatVersion)
        return fail(QCoreApplication::translate("MeetingArchive", "Az archívum formátum-verziója (%1) nem támogatott; ez a Tanara a(z) %2. verziót ismeri.")
                        .arg(manifest.version).arg(kFormatVersion));
    if (topDir.isEmpty() || (!hasMeetingJson && !hasTrack))
        return fail(QCoreApplication::translate("MeetingArchive", "Az archívumban nincs megbeszélés (meeting.json vagy track_*.ogg)."));

    // 2) Kicsomagolás az ideiglenes mappába.
    const QString tempDir = QDir(tempRoot).filePath(tempDirPrefix()
                                                    + QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString target = QDir(tempDir).filePath(topDir);
    if (!QDir().mkpath(target))
        return fail(QCoreApplication::translate("MeetingArchive", "Az ideiglenes mappa nem hozható létre: %1").arg(tempDir));
    auto abort = [&](const QString& msg) {
        QDir(tempDir).removeRecursively();
        return fail(msg);
    };
    ProgressTracker pt{std::move(progress), totalBytes};
    pt.report(0);
    for (const auto& [index, rel] : entries) {
        const QString path = QDir(target).filePath(rel);
        if (!QDir().mkpath(QFileInfo(path).absolutePath()))
            return abort(QCoreApplication::translate("MeetingArchive", "A mappa nem hozható létre: %1").arg(QFileInfo(path).absolutePath()));
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return abort(QCoreApplication::translate("MeetingArchive", "A fájl nem írható: %1 (%2)").arg(path, out.errorString()));
        ExtractCtx ctx{&out, &pt};
        if (!mz_zip_reader_extract_to_callback(&zip, index, &extractToFile, &ctx, 0))
            return abort(QCoreApplication::translate("MeetingArchive", "A kicsomagolás nem sikerült (%1): %2").arg(rel, zipErrorText(&zip)));
        out.close();
    }
    pt.report(100);
    if (manifestOut) *manifestOut = manifest;
    qCInfo(lcStore).noquote() << "Archívum kicsomagolva:" << zipPath << "→" << target;
    return target;
}

} // namespace tanara
