// Meeting archive (store/MeetingArchive) and adopting a recording folder
// (MeetingRepository::adoptMeetingFolder), only in temporary folders, with made-up data:
//  - export: manifest, the skipped files (lock, *.tmp, transcript-backup-*, embedding,
//    profile.json) are not in it, the rest are;
//  - import into another store: meeting.json is identical (no folder field), the
//    tags, transcript, summary exist, the temporary .import-* folder disappears;
//  - rejection: missing manifest, wrong version, a "../" entry, two top-level folders, an
//    already existing folder name, an already existing id;
//  - adoptMeetingFolder: copying from outside, in place, moving from .import-*, recovery
//    without meeting.json, name clash;
//  - AppController: background export / import, resolving tags by name.
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <miniz.h>

#include <cstring>

#include "tanara/app/AppController.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/detail/MeetingJson.h"
#include "tanara/library/store/ParticipantJson.h"
#include "tanara/library/store/MeetingArchive.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/tags/TagService.h"

using namespace tanara;

namespace {

void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    f.write(data);
}

QByteArray readFile(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QJsonObject readJson(const QString& path)
{
    return QJsonDocument::fromJson(readFile(path)).object();
}

// The archive's entry names (with the miniz reader). The file goes through QFile and the in-memory
// reader: miniz's own fopen() takes an 8-bit path, which on Windows is the ANSI code page, so a
// name such as "Ügyfél-egyeztetés" would not open there (the app itself reads through QFile too).
QStringList zipEntries(const QString& zipPath)
{
    QStringList out;
    const QByteArray bytes = readFile(zipPath);
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_mem(&zip, bytes.constData(), size_t(bytes.size()), 0)) return out;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
        mz_zip_archive_file_stat st;
        if (mz_zip_reader_file_stat(&zip, i, &st)) out << QString::fromUtf8(st.m_filename);
    }
    mz_zip_reader_end(&zip);
    return out;
}

QByteArray zipEntryData(const QString& zipPath, const QString& name)
{
    const QByteArray bytes = readFile(zipPath);
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_mem(&zip, bytes.constData(), size_t(bytes.size()), 0)) return {};
    size_t size = 0;
    void* p = mz_zip_reader_extract_file_to_heap(&zip, name.toUtf8().constData(), &size, 0);
    QByteArray out = p ? QByteArray(static_cast<const char*>(p), qsizetype(size)) : QByteArray();
    mz_free(p);
    mz_zip_reader_end(&zip);
    return out;
}

// A hand-assembled archive (for the broken / malicious cases); built in memory, written with QFile.
void writeZip(const QString& zipPath, const QList<QPair<QByteArray, QByteArray>>& entries)
{
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    QVERIFY(mz_zip_writer_init_heap(&zip, 0, 0));
    for (const auto& [name, data] : entries)
        QVERIFY(mz_zip_writer_add_mem(&zip, name.constData(), data.constData(), size_t(data.size()),
                                      MZ_DEFAULT_LEVEL));
    void* buf = nullptr;
    size_t size = 0;
    QVERIFY(mz_zip_writer_finalize_heap_archive(&zip, &buf, &size));
    writeFile(zipPath, QByteArray(static_cast<const char*>(buf), qsizetype(size)));
    mz_free(buf);
    QVERIFY(mz_zip_writer_end(&zip));
}

// The miniz writer does not allow ".." / absolute names: we write a harmless name of the same
// length, then replace it byte for byte in the file (local header + central directory).
void patchZipNames(const QString& zipPath, const QByteArray& from, const QByteArray& to)
{
    QCOMPARE(from.size(), to.size());
    QByteArray data = readFile(zipPath);
    QCOMPARE(data.count(from), 2);
    data.replace(from, to);
    writeFile(zipPath, data);
}

QByteArray manifest(int version = 1)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("version"), version},
                                     {QStringLiteral("app"), QStringLiteral("0.0.0-test")},
                                     {QStringLiteral("meeting"), QJsonObject{}}})
        .toJson();
}

} // namespace

class MeetingArchiveTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { qputenv("TANARA_CLOUD", "off"); }
    void init();
    void cleanup();

    void exportContainsManifestAndSkipsDerivedFiles();
    void exportImportRoundTrip();
    void exportRefusesMissingFolder();
    void exportCancelRemovesPartialFile();
    void importRefusesMissingManifest();
    void importRefusesWrongVersion();
    void importRefusesPathTraversal();
    void importRefusesTwoTopLevelDirs();
    void importRefusesExistingFolderName();
    void importRefusesExistingMeetingId();

    void adoptCopiesFromOutside();
    void adoptInPlace();
    void adoptMovesFromImportTemp();
    void adoptRecoversWithoutJson();
    void adoptRefusesDuplicateName();

    void controllerExportImportWithTags();

private:
    // A made-up meeting in the source store, with every kind of file.
    Meeting makeFixture(library::MeetingRepository& store, const QString& title = QStringLiteral("Ügyfél-egyeztetés"));
    QString recDir(const char* name) const { return m_dir->filePath(QString::fromLatin1(name)); }

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<library::MeetingRepository> m_src;
    std::unique_ptr<library::MeetingRepository> m_dst;
};

void MeetingArchiveTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_src = std::make_unique<library::MeetingRepository>(recDir("src-rec"), recDir("src-meta"));
    m_dst = std::make_unique<library::MeetingRepository>(recDir("dst-rec"), recDir("dst-meta"));
}

void MeetingArchiveTest::cleanup()
{
    m_src.reset();
    m_dst.reset();
    m_dir.reset();
}

Meeting MeetingArchiveTest::makeFixture(library::MeetingRepository& store, const QString& title)
{
    Meeting m = store.createMeeting(title);
    const QDir dir(m.folder);
    writeFile(dir.filePath("track_mic.ogg"), QByteArray(64 * 1024, 'm'));
    writeFile(dir.filePath("track_loopback.ogg"), QByteArray(32 * 1024, 'l'));
    writeFile(dir.filePath("mixdown.mp3"), QByteArray(16 * 1024, 'x'));
    writeFile(dir.filePath("transcript.md"), "**Beszélő 1:** Jó napot, kezdhetjük.\n");
    writeFile(dir.filePath("transcript.speakers.json"), R"({"corrections":[]})");
    writeFile(dir.filePath("summary.md"), "# Összefoglaló\n- Döntés: jövő héten folytatjuk.\n");
    writeFile(dir.filePath("track_mic.peaks.json"), "[0,1,2]");
    // Skipped:
    writeFile(dir.filePath("profile.json"), "{}");
    writeFile(dir.filePath("transcript.embeddings.bin"), QByteArray(128, 'e'));
    writeFile(dir.filePath("recording.lock"), "{}");
    writeFile(dir.filePath("summary.md.tmp"), "félkész");
    writeFile(dir.filePath("transcript-backup-20260101-1200/transcript.md"), "régi");

    Track mic;
    mic.id = QStringLiteral("mic");
    mic.file = QStringLiteral("track_mic.ogg");
    mic.speakerLabel = QStringLiteral("Én");
    Track lb;
    lb.id = QStringLiteral("loopback");
    lb.file = QStringLiteral("track_loopback.ogg");
    lb.kind = TrackKind::Loopback;
    lb.startOffsetMs = 57200;   // a track switched on later: the offset travels with meeting.json
    m.tracks = {mic, lb};
    m.mixdownFile = QStringLiteral("mixdown.mp3");
    m.hasTranscript = true;
    m.hasSummary = true;
    m.durationMs = 125000;
    m.speakerMap.insert(QStringLiteral("Távoli 1"), PersonId::fromString(QStringLiteral("p-anna")));
    m.contextNote = QStringLiteral("Negyedéves áttekintés");
    m.tagIds = {QStringLiteral("tag-a"), QStringLiteral("tag-b")};
    store.saveMeeting(m);
    return m;
}

void MeetingArchiveTest::exportContainsManifestAndSkipsDerivedFiles()
{
    const Meeting m = makeFixture(*m_src);
    const QString zip = m_dir->filePath(library::MeetingArchive::suggestedFileName(m));
    QVERIFY(zip.endsWith(QStringLiteral(".tanara.zip")));
    QList<int> pcts;
    QString err;
    Tag ta{QStringLiteral("tag-a"), QStringLiteral("Ügyfél"), {}};
    QVERIFY2(library::MeetingArchive::exportMeeting(m, zip, &err, [&](int p) { pcts << p; }, {ta}), qPrintable(err));
    QVERIFY(!QFile::exists(zip + QStringLiteral(".part")));
    QVERIFY(!pcts.isEmpty());
    QCOMPARE(pcts.last(), 100);

    const QString top = QFileInfo(m.folder).fileName();
    const QStringList entries = zipEntries(zip);
    QCOMPARE(entries.first(), QStringLiteral("tanara-archive.json"));
    for (const char* keep : {"meeting.json", "track_mic.ogg", "track_loopback.ogg", "mixdown.mp3",
                             "transcript.md", "transcript.speakers.json", "summary.md", "track_mic.peaks.json"})
        QVERIFY2(entries.contains(top + QLatin1Char('/') + QLatin1String(keep)), keep);
    for (const QString& e : entries) {
        QVERIFY2(e == QStringLiteral("tanara-archive.json") || e.startsWith(top + QLatin1Char('/')), qPrintable(e));
        QVERIFY2(!e.contains(QStringLiteral("profile.json")) && !e.contains(QStringLiteral(".embeddings.bin"))
                     && !e.contains(QStringLiteral("recording.lock")) && !e.endsWith(QStringLiteral(".tmp"))
                     && !e.contains(QStringLiteral("transcript-backup-")),
                 qPrintable(e));
    }

    const QJsonObject man = QJsonDocument::fromJson(zipEntryData(zip, QStringLiteral("tanara-archive.json"))).object();
    QCOMPARE(man.value("version").toInt(), 1);
    QVERIFY(!man.value("app").toString().isEmpty());
    QVERIFY(QDateTime::fromString(man.value("exportedAt").toString(), Qt::ISODateWithMs).isValid());
    const QJsonObject mm = man.value("meeting").toObject();
    QCOMPARE(mm.value("id").toString(), m.id);
    QCOMPARE(mm.value("title").toString(), m.title);
    QCOMPARE(qint64(mm.value("durationMs").toDouble()), m.durationMs);
    QCOMPARE(mm.value("trackCount").toInt(), 2);
    QVERIFY(QDateTime::fromString(mm.value("startedAt").toString(), Qt::ISODateWithMs).isValid());
    QCOMPARE(mm.value("tags").toArray().size(), 1);
    QCOMPARE(mm.value("tags").toArray().first().toObject().value("name").toString(), QStringLiteral("Ügyfél"));

    QCOMPARE(zipEntryData(zip, top + QStringLiteral("/track_mic.ogg")), QByteArray(64 * 1024, 'm'));
}

void MeetingArchiveTest::exportImportRoundTrip()
{
    const Meeting m = makeFixture(*m_src);
    const QString zip = m_dir->filePath(QStringLiteral("ki.tanara.zip"));
    QString err;
    QVERIFY2(library::MeetingArchive::exportMeeting(m, zip, &err), qPrintable(err));

    QVERIFY(QDir().mkpath(m_dst->audioDir()));
    library::ArchiveManifest man;
    const QString extracted = library::MeetingArchive::importArchive(zip, m_dst->audioDir(), &err, {}, &man);
    QVERIFY2(!extracted.isEmpty(), qPrintable(err));
    QCOMPARE(man.version, 1);
    QCOMPARE(man.meetingId, m.id);
    QCOMPARE(man.trackCount, 2);
    QVERIFY(QFileInfo(extracted).dir().dirName().startsWith(QStringLiteral(".import-")));

    const Meeting imported = m_dst->adoptMeetingFolder(extracted, &err);
    QVERIFY2(!imported.id.isEmpty(), qPrintable(err));
    QCOMPARE(imported.id, m.id);
    const QString folder = QDir(m_dst->audioDir()).filePath(QFileInfo(m.folder).fileName());
    QCOMPARE(imported.folder, folder);
    QCOMPARE(imported.tagIds, m.tagIds);   // at store level the ids are unchanged
    QCOMPARE(imported.tracks.size(), 2);
    QCOMPARE(imported.tracks.at(1).startOffsetMs, qint64(57200));
    QCOMPARE(imported.tracks.at(0).startOffsetMs, qint64(0));

    // meeting.json: identical (it does not store the folder: that is where the file is).
    const QJsonObject a = readJson(QDir(m.folder).filePath("meeting.json"));
    const QJsonObject b = readJson(QDir(folder).filePath("meeting.json"));
    QCOMPARE(b, a);
    QVERIFY(!b.contains("folder"));

    for (const char* f : {"transcript.md", "transcript.speakers.json", "summary.md", "mixdown.mp3",
                          "track_mic.ogg", "track_loopback.ogg"})
        QCOMPARE(readFile(QDir(folder).filePath(f)), readFile(QDir(m.folder).filePath(f)));
    QVERIFY(!QFile::exists(QDir(folder).filePath("profile.json")));
    QVERIFY(!QFile::exists(QDir(folder).filePath("recording.lock")));

    // The temporary folder is gone; the meeting is in the index.
    QVERIFY(QDir(m_dst->audioDir()).entryList({QStringLiteral(".import-*")},
                                              QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
    QCOMPARE(m_dst->loadAll().size(), 1);
    QCOMPARE(m_dst->load(m.id).title, m.title);
}

void MeetingArchiveTest::exportRefusesMissingFolder()
{
    Meeting m;
    m.id = QStringLiteral("nincs");
    m.folder = m_dir->filePath(QStringLiteral("nemletezik"));
    QString err;
    QVERIFY(!library::MeetingArchive::exportMeeting(m, m_dir->filePath("x.tanara.zip"), &err));
    QVERIFY(!err.isEmpty());
    QVERIFY(!QFile::exists(m_dir->filePath("x.tanara.zip")));
}

void MeetingArchiveTest::exportCancelRemovesPartialFile()
{
    const Meeting m = makeFixture(*m_src);
    const QString zip = m_dir->filePath(QStringLiteral("megszakitott.tanara.zip"));
    std::atomic<bool> cancel{false};
    QString err;
    QVERIFY(!library::MeetingArchive::exportMeeting(m, zip, &err, [&](int p) { if (p > 0) cancel = true; }, {}, &cancel));
    QCOMPARE(err, QStringLiteral("Megszakítva."));
    QVERIFY(!QFile::exists(zip));
    QVERIFY(!QFile::exists(zip + QStringLiteral(".part")));
}

void MeetingArchiveTest::importRefusesMissingManifest()
{
    const QString zip = m_dir->filePath(QStringLiteral("nincs-manifeszt.zip"));
    writeZip(zip, {{"2026-01-01_1000_x/meeting.json", "{}"}});
    QString err;
    QVERIFY(library::MeetingArchive::importArchive(zip, m_dir->path(), &err).isEmpty());
    QVERIFY2(err.contains(QStringLiteral("tanara-archive.json")), qPrintable(err));
    QVERIFY(QDir(m_dir->path()).entryList({QStringLiteral(".import-*")}, QDir::Dirs | QDir::Hidden).isEmpty());
}

void MeetingArchiveTest::importRefusesWrongVersion()
{
    const QString zip = m_dir->filePath(QStringLiteral("v2.zip"));
    writeZip(zip, {{"tanara-archive.json", manifest(2)}, {"2026-01-01_1000_x/meeting.json", "{}"}});
    QString err;
    QVERIFY(library::MeetingArchive::importArchive(zip, m_dir->path(), &err).isEmpty());
    QVERIFY2(err.contains(QStringLiteral("(2)")), qPrintable(err));
}

void MeetingArchiveTest::importRefusesPathTraversal()
{
    const QString zip = m_dir->filePath(QStringLiteral("gonosz.zip"));
    writeZip(zip, {{"tanara-archive.json", manifest()},
                   {"2026-01-01_1000_x/meeting.json", "{}"},
                   {"2026-01-01_1000_x/XX/XX/evil", "pwned"}});
    patchZipNames(zip, "XX/XX/evil", "../../evil");
    const QString root = m_dir->filePath(QStringLiteral("rec"));
    QVERIFY(QDir().mkpath(root));
    QString err;
    QVERIFY(library::MeetingArchive::importArchive(zip, root, &err).isEmpty());
    QVERIFY2(err.contains(QStringLiteral("evil")), qPrintable(err));
    QVERIFY(!QFile::exists(m_dir->filePath(QStringLiteral("evil"))));
    QVERIFY(QDir(root).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());

    const QString zip2 = m_dir->filePath(QStringLiteral("gonosz2.zip"));
    writeZip(zip2, {{"tanara-archive.json", manifest()}, {"XX/evil/meeting.json", "{}"}});
    patchZipNames(zip2, "XX/evil/", "../evil/");
    QVERIFY(library::MeetingArchive::importArchive(zip2, root, &err).isEmpty());
    const QString zip3 = m_dir->filePath(QStringLiteral("gonosz3.zip"));
    writeZip(zip3, {{"tanara-archive.json", manifest()}, {"Xtmp/abs/meeting.json", "{}"}});
    patchZipNames(zip3, "Xtmp/abs/", "/tmp/abs/");
    QVERIFY(library::MeetingArchive::importArchive(zip3, root, &err).isEmpty());
    QVERIFY(QDir(root).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
}

void MeetingArchiveTest::importRefusesTwoTopLevelDirs()
{
    const QString zip = m_dir->filePath(QStringLiteral("ketto.zip"));
    writeZip(zip, {{"tanara-archive.json", manifest()},
                   {"a/meeting.json", "{}"},
                   {"b/meeting.json", "{}"}});
    QString err;
    QVERIFY(library::MeetingArchive::importArchive(zip, m_dir->path(), &err).isEmpty());
    QVERIFY(!err.isEmpty());

    const QString empty = m_dir->filePath(QStringLiteral("ures.zip"));
    writeZip(empty, {{"tanara-archive.json", manifest()}, {"a/notes.txt", "x"}});
    QVERIFY(library::MeetingArchive::importArchive(empty, m_dir->path(), &err).isEmpty());
    QVERIFY2(err.contains(QStringLiteral("meeting.json")), qPrintable(err));
}

void MeetingArchiveTest::importRefusesExistingFolderName()
{
    const Meeting m = makeFixture(*m_src);
    const QString zip = m_dir->filePath(QStringLiteral("ki.tanara.zip"));
    QString err;
    QVERIFY(library::MeetingArchive::exportMeeting(m, zip, &err));
    // The target already has a folder of that name (with other content).
    const QString clash = QDir(m_dst->audioDir()).filePath(QFileInfo(m.folder).fileName());
    writeFile(QDir(clash).filePath("track_mic.ogg"), "masik");

    const QString extracted = library::MeetingArchive::importArchive(zip, m_dst->audioDir(), &err);
    QVERIFY(!extracted.isEmpty());
    const Meeting none = m_dst->adoptMeetingFolder(extracted, &err);
    QVERIFY(none.id.isEmpty());
    QVERIFY2(err.startsWith(QStringLiteral("Már van ilyen nevű felvétel")), qPrintable(err));
    QCOMPARE(readFile(QDir(clash).filePath("track_mic.ogg")), QByteArray("masik"));   // untouched
    QVERIFY(QDir(m_dst->audioDir()).entryList({QStringLiteral(".import-*")},
                                              QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
}

void MeetingArchiveTest::importRefusesExistingMeetingId()
{
    // The same meeting (id) with another folder name is already in the store.
    const Meeting m = makeFixture(*m_src);
    const QString zip = m_dir->filePath(QStringLiteral("ki.tanara.zip"));
    QString err;
    QVERIFY(library::MeetingArchive::exportMeeting(m, zip, &err));
    const QString other = QDir(m_src->audioDir()).filePath(QStringLiteral("atnevezett"));
    QVERIFY(QDir().rename(m.folder, other));
    Meeting moved = m;
    moved.folder = other;
    m_src->saveMeeting(moved);

    const QString extracted = library::MeetingArchive::importArchive(zip, m_src->audioDir(), &err);
    QVERIFY(!extracted.isEmpty());
    QVERIFY(m_src->adoptMeetingFolder(extracted, &err).id.isEmpty());
    QVERIFY2(err.contains(QStringLiteral("már szerepel")), qPrintable(err));
    QVERIFY(!QFileInfo::exists(m.folder));
    QCOMPARE(m_src->loadAll().size(), 1);
}

void MeetingArchiveTest::adoptCopiesFromOutside()
{
    const Meeting m = makeFixture(*m_src);
    QString err;
    const Meeting a = m_dst->adoptMeetingFolder(m.folder, &err);
    QVERIFY2(!a.id.isEmpty(), qPrintable(err));
    QCOMPARE(a.id, m.id);
    QVERIFY(QFileInfo::exists(m.folder));   // the source stays (a copy)
    QCOMPARE(a.folder, QDir(m_dst->audioDir()).filePath(QFileInfo(m.folder).fileName()));
    QVERIFY(QFile::exists(QDir(a.folder).filePath("transcript.md")));
}

void MeetingArchiveTest::adoptInPlace()
{
    const Meeting m = makeFixture(*m_src);
    // Without an index (e.g. a hand-copied folder): a new store on the same folder.
    library::MeetingRepository fresh(m_src->audioDir(), recDir("fresh-meta"));
    QString err;
    const Meeting a = fresh.adoptMeetingFolder(m.folder, &err);
    QVERIFY2(!a.id.isEmpty(), qPrintable(err));
    QCOMPARE(a.folder, m.folder);
    QCOMPARE(fresh.loadAll().size(), 1);
    // Works a second time too (the same folder, the same id).
    QVERIFY2(!fresh.adoptMeetingFolder(m.folder, &err).id.isEmpty(), qPrintable(err));
    QCOMPARE(fresh.loadAll().size(), 1);
}

void MeetingArchiveTest::adoptMovesFromImportTemp()
{
    const Meeting m = makeFixture(*m_src);
    const QString temp = QDir(m_dst->audioDir()).filePath(QStringLiteral(".import-teszt"));
    const QString staged = QDir(temp).filePath(QFileInfo(m.folder).fileName());
    QVERIFY(QDir().mkpath(temp));
    QVERIFY(QDir().rename(m.folder, staged));
    QString err;
    const Meeting a = m_dst->adoptMeetingFolder(staged, &err);
    QVERIFY2(!a.id.isEmpty(), qPrintable(err));
    QVERIFY(!QFileInfo::exists(staged));
    QVERIFY(!QFileInfo::exists(temp));   // the temporary folder was deleted too
    QVERIFY(QFile::exists(QDir(a.folder).filePath("track_mic.ogg")));
}

void MeetingArchiveTest::adoptRecoversWithoutJson()
{
    const QString outside = m_dir->filePath(QStringLiteral("pendrive/2026-02-03_0930_Heti"));
    writeFile(QDir(outside).filePath("track_mic.ogg"), QByteArray(1024, 'a'));
    QString err;
    const Meeting a = m_dst->adoptMeetingFolder(outside, &err);
    QVERIFY2(!a.id.isEmpty(), qPrintable(err));
    QVERIFY(a.title.contains(QStringLiteral("helyreállított")));
    QCOMPARE(a.tracks.size(), 1);
    QCOMPARE(a.tracks.first().startOffsetMs, qint64(0));   // recovered: 0 (the CLI align --auto aligns)
    QVERIFY(QFile::exists(QDir(a.folder).filePath("meeting.json")));

    // Error: neither meeting.json nor a track.
    const QString empty = m_dir->filePath(QStringLiteral("ures"));
    QVERIFY(QDir().mkpath(empty));
    QVERIFY(m_dst->adoptMeetingFolder(empty, &err).id.isEmpty());
    QVERIFY(!err.isEmpty());
}

void MeetingArchiveTest::adoptRefusesDuplicateName()
{
    const Meeting m = makeFixture(*m_src);
    QString err;
    QVERIFY(!m_dst->adoptMeetingFolder(m.folder, &err).id.isEmpty());
    QVERIFY(m_dst->adoptMeetingFolder(m.folder, &err).id.isEmpty());
    QVERIFY2(err.startsWith(QStringLiteral("Már van ilyen nevű felvétel")), qPrintable(err));
}

void MeetingArchiveTest::controllerExportImportWithTags()
{
    const QString zip = m_dir->filePath(QStringLiteral("app.tanara.zip"));
    QString meetingId;
    QString exportedTitle;
    {
        qputenv("TANARA_HOME", m_dir->filePath("home-a").toUtf8());
        app::AppController app;
        Meeting m = app.store()->createMeeting(QStringLiteral("Partnertalálkozó"));
        writeFile(QDir(m.folder).filePath("track_mic.ogg"), QByteArray(256 * 1024, 'm'));
        Track t;
        t.id = QStringLiteral("mic");
        t.file = QStringLiteral("track_mic.ogg");
        m.tracks = {t};
        app.store()->saveMeeting(m);
        app.tags()->addTag(m.id, QStringLiteral("Nordvik"));
        app.tags()->addTag(m.id, QStringLiteral("Negyedéves"));
        meetingId = m.id;
        exportedTitle = m.title;

        QSignalSpy finished(&app, &app::AppController::archiveFinished);
        QSignalSpy progress(&app, &app::AppController::archiveProgress);
        const QString op = app.exportMeetingArchive(meetingId, zip);
        QCOMPARE(op, QStringLiteral("export:") + meetingId);
        QVERIFY(app.archiveBusy());
        QVERIFY(app.jobs()->isRunning(meetingId, JobKind::Export));
        QVERIFY(app.exportMeetingArchive(meetingId, zip).isEmpty());   // a second one does not start meanwhile
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
        QVERIFY2(finished.first().at(1).toBool(), qPrintable(finished.first().at(4).toString()));
        QCOMPARE(finished.first().at(3).toString(), zip);
        QVERIFY(!app.archiveBusy());
        QVERIFY(!app.jobs()->isRunning(meetingId, JobKind::Export));
        QVERIFY(QFile::exists(zip));
        QVERIFY(!progress.isEmpty());
    }
    {
        // Another "machine": there is already a "nordvik" tag here (with another id), so it maps to that.
        qputenv("TANARA_HOME", m_dir->filePath("home-b").toUtf8());
        app::AppController app;
        const Tag local = app.tags()->create(QStringLiteral("nordvik"));
        QSignalSpy finished(&app, &app::AppController::archiveFinished);
        const QString op = app.importMeetingArchive(zip);
        QVERIFY(op.startsWith(QStringLiteral("import:")));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
        QVERIFY2(finished.first().at(1).toBool(), qPrintable(finished.first().at(4).toString()));
        QCOMPARE(finished.first().at(2).toString(), meetingId);

        const Meeting m = app.store()->load(meetingId);
        QCOMPARE(m.title, exportedTitle);
        QCOMPARE(m.tagIds.size(), 2);
        QCOMPARE(m.tagIds.first(), local.id);
        QCOMPARE(app.tags()->tag(m.tagIds.at(1)).name, QStringLiteral("Negyedéves"));
        QCOMPARE(app.tags()->tagsOf(meetingId), m.tagIds);

        // The same a second time: an error, nothing changes.
        finished.clear();
        app.importMeetingArchive(zip);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
        QVERIFY(!finished.first().at(1).toBool());
        QVERIFY(finished.first().at(4).toString().startsWith(QStringLiteral("Már van ilyen nevű felvétel")));

        // Synchronous variant: a broken file.
        QString err;
        writeFile(m_dir->filePath("nem-zip.tanara.zip"), "ez nem zip");
        QVERIFY(app.importMeetingArchiveNow(m_dir->filePath("nem-zip.tanara.zip"), &err).id.isEmpty());
        QVERIFY(!err.isEmpty());
    }
    qunsetenv("TANARA_HOME");
}

QTEST_GUILESS_MAIN(MeetingArchiveTest)
#include "test_meeting_archive.moc"
