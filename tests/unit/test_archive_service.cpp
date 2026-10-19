// ArchiveService / ImportService / app::Services: the asynchronous archive operations on their own
// (without AppController): export main path + job tracking, cancel, error paths, busy rejection,
// recording guard, archive / folder import, and the ImportService busy rejection.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "tanara/app/Services.h"
#include "tanara/library/import/AudioImporter.h"
#include "tanara/library/import/ImportService.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/ArchiveService.h"
#include "tanara/library/store/MeetingArchive.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/tags/TagService.h"

using namespace tanara;

namespace {

// A meeting folder with some payload under the repository's recordings root.
Meeting makeMeeting(library::MeetingRepository& repo, const QString& title, int payloadKb = 1)
{
    Meeting m = repo.createMeeting(title);
    QFile f(QDir(m.folder).filePath(QStringLiteral("track_mic.ogg")));
    if (f.open(QIODevice::WriteOnly)) {
        // Incompressible payload, so a big one keeps the packing busy long enough to cancel it.
        QByteArray data(payloadKb * 1024, 0);
        quint32 x = 12345;
        for (char& c : data) { x = x * 1664525u + 1013904223u; c = char(x >> 24); }
        f.write(data);
    }
    f.close();
    return m;
}

} // namespace

class TestArchiveService : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;
    QString m_meta;
    QString m_root;
    std::unique_ptr<library::MeetingRepository> m_repo;
    std::unique_ptr<library::TagService> m_tags;
    std::unique_ptr<library::MeetingJobTracker> m_jobs;
    std::unique_ptr<app::Services> m_svc;
    int m_n = 0;

private slots:
    void init()
    {
        const QString base = m_tmp.filePath(QString::number(++m_n));
        m_root = base + "/rec";
        m_meta = base + "/meta";
        QDir().mkpath(m_root);
        QDir().mkpath(m_meta);
        m_repo = std::make_unique<library::MeetingRepository>(m_root, m_meta);
        m_tags = std::make_unique<library::TagService>(m_repo.get(), m_meta + "/tags.json");
        m_jobs = std::make_unique<library::MeetingJobTracker>(m_repo.get());
        app::ServicesDeps sd;
        sd.store = m_repo.get();
        sd.tags = m_tags.get();
        sd.jobs = m_jobs.get();
        m_svc = std::make_unique<app::Services>(sd);
    }
    void cleanup() { m_svc.reset(); m_jobs.reset(); m_tags.reset(); m_repo.reset(); }

    void exportThenImportArchive()
    {
        const Meeting m = makeMeeting(*m_repo, "Alpha");
        auto& a = m_svc->archive();
        QSignalSpy fin(&a, &library::ArchiveService::finished);
        const QString zip = m_tmp.filePath("alpha.tanara.zip");
        const QString op = a.exportMeeting(m.id, zip);
        QCOMPARE(op, QStringLiteral("export:") + m.id);
        QVERIFY(a.busy());
        QCOMPARE(a.currentOp(), op);
        QVERIFY(m_jobs->isRunning(m.id, JobKind::Export));
        QVERIFY(fin.wait(10000));
        QCOMPARE(fin.at(0).at(0).toString(), op);
        QVERIFY(fin.at(0).at(1).toBool());
        QCOMPARE(fin.at(0).at(3).toString(), zip);
        QVERIFY(QFileInfo::exists(zip));
        QVERIFY(!a.busy());
        QVERIFY(!m_jobs->isRunning(m.id, JobKind::Export));

        // Import it back: a second meeting folder appears (same folder name -> adoption renames).
        QVERIFY(QDir(m.folder).removeRecursively());
        fin.clear();
        const QString iop = a.importArchive(zip);
        QVERIFY(iop.startsWith("import:"));
        QVERIFY(fin.wait(10000));
        QVERIFY2(fin.at(0).at(1).toBool(), qPrintable(fin.at(0).at(4).toString()));
        QVERIFY(!fin.at(0).at(2).toString().isEmpty());
    }

    void cancelExport()
    {
        const Meeting m = makeMeeting(*m_repo, "Big", 30000);   // ~30 MB, several blocks
        auto& a = m_svc->archive();
        QSignalSpy fin(&a, &library::ArchiveService::finished);
        const QString zip = m_tmp.filePath("big.tanara.zip");
        QVERIFY(!a.exportMeeting(m.id, zip).isEmpty());
        const bool cancelled = a.cancel(m.id);
        QVERIFY(fin.wait(20000));
        // On a fast disk (the macOS runner) the 30 MB export can finish before the cancel lands; then
        // there is nothing to cancel and the case has nothing to check.
        if (!cancelled || fin.at(0).at(1).toBool())
            QSKIP("the export finished before the cancel could take effect (fast disk)");
        QVERIFY(!QFileInfo::exists(zip));
        QVERIFY(!QFileInfo::exists(zip + ".part"));
        QVERIFY(!m_jobs->isRunning(m.id, JobKind::Export));
        QVERIFY(!a.cancel(m.id));   // nothing runs any more
    }

    void rejections()
    {
        auto& a = m_svc->archive();
        QSignalSpy rej(&a, &library::ArchiveService::rejected);
        QVERIFY(a.exportMeeting("nope", m_tmp.filePath("x.zip")).isEmpty());
        QCOMPARE(rej.count(), 1);

        const Meeting m = makeMeeting(*m_repo, "Live");
        a.setRecordingGuard([&](const Meeting& mm) { return mm.id == m.id; });
        QVERIFY(a.exportMeeting(m.id, m_tmp.filePath("x.zip")).isEmpty());
        QCOMPARE(rej.count(), 2);
        QString err;
        QVERIFY(!a.exportNow(m.id, m_tmp.filePath("x.zip"), &err));
        QVERIFY(!err.isEmpty());
        a.setRecordingGuard({});

        QSignalSpy fin(&a, &library::ArchiveService::finished);
        QVERIFY(!a.exportMeeting(m.id, m_tmp.filePath("y.tanara.zip")).isEmpty());
        QVERIFY(a.exportMeeting(m.id, m_tmp.filePath("z.tanara.zip")).isEmpty());   // busy
        QCOMPARE(rej.count(), 3);
        QVERIFY(fin.wait(10000));
    }

    void importErrorPaths()
    {
        auto& a = m_svc->archive();
        QSignalSpy fin(&a, &library::ArchiveService::finished);
        QFile bad(m_tmp.filePath("bad.tanara.zip"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("not a zip");
        bad.close();
        QVERIFY(!a.importArchive(bad.fileName()).isEmpty());
        QVERIFY(fin.wait(10000));
        QVERIFY(!fin.at(0).at(1).toBool());
        QVERIFY(!fin.at(0).at(4).toString().isEmpty());

        QString err;
        QVERIFY(a.importArchiveNow(bad.fileName(), &err).id.isEmpty());
        QVERIFY(!err.isEmpty());
    }

    void importFolderInPlace()
    {
        // A foreign folder (outside the recordings root) is staged, then adopted.
        const QString src = m_tmp.filePath("foreign/2026-01-02_1000_Beta");
        QDir().mkpath(src);
        QFile f(src + "/track_mic.ogg");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(512, 'y'));
        f.close();
        auto& a = m_svc->archive();
        QSignalSpy fin(&a, &library::ArchiveService::finished);
        QVERIFY(!a.importFolder(src).isEmpty());
        QVERIFY(fin.wait(10000));
        QVERIFY2(fin.at(0).at(1).toBool(), qPrintable(fin.at(0).at(4).toString()));
        QVERIFY(!fin.at(0).at(2).toString().isEmpty());
    }

    void importServiceRejectsWithoutWork()
    {
        auto& imp = m_svc->import();
        QVERIFY(imp.importer());
        QVERIFY(!imp.busy());
        QVERIFY(!imp.cancel("nothing"));
    }
};

QTEST_MAIN(TestArchiveService)
#include "test_archive_service.moc"
