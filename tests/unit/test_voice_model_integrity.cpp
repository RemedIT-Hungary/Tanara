// Voice-model integrity (D2): the model KINDS of the registry (speaker-embedding vs segmentation), the
// pinned SHA-256 of every catalog entry, the downloader's hash check (ok / mismatch parked aside /
// override persisted as "unverified"), companion files (the LICENSE next to the segmentation models)
// and the catalog's listing / states. Served by a local fake HTTP server; NO real network.
#include <QCryptographicHash>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "fake_http.h"
#include "tanara/processing/voiceid/LocalVoiceModelCatalog.h"
#include "tanara/processing/voiceid/VoiceModelDownloader.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

using namespace tanara;
using processing::LocalVoiceModelCatalog;
using processing::VoiceModelDownloader;
using processing::VoiceModelFile;
using processing::VoiceModelKind;
using processing::VoiceModelRegistry;
using processing::VoiceModelSpec;
using VR = VoiceModelDownloader::VerifyResult;

namespace {
QString sha(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}
QByteArray readAll(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}
} // namespace

class TestVoiceModelIntegrity : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<fakehttp::Server> m_srv;
    QHash<QString, QByteArray> m_files;   // server path -> body
    QString m_models;                     // the models dir

    const QByteArray m_float = QByteArray(200 * 1024, 'f') + "float-end";
    const QByteArray m_int8 = QByteArray(60 * 1024, 'i') + "int8-end";
    const QByteArray m_license = "MIT License\n\nCopyright (c) 2022 CNRS\n";

    QString url(const QString& path) const { return m_srv->base() + path; }

    // A segmentation-like pair of specs (one folder, one LICENSE companion) served by the fake server.
    QVector<VoiceModelSpec> segSpecs() const
    {
        VoiceModelFile lic;
        lic.fileName = QStringLiteral("LICENSE");
        lic.downloadUrl = url(QStringLiteral("/seg/LICENSE"));
        lic.sizeBytes = m_license.size();
        lic.sha256 = sha(m_license);

        VoiceModelSpec f;
        f.id = QStringLiteral("seg-float");
        f.kind = VoiceModelKind::Segmentation;
        f.displayName = QStringLiteral("Seg");
        f.fileName = QStringLiteral("seg-dir/model.onnx");
        f.downloadUrl = url(QStringLiteral("/seg/model.onnx"));
        f.sizeBytes = m_float.size();
        f.sha256 = sha(m_float);
        f.license = QStringLiteral("MIT");
        f.companions = {lic};

        VoiceModelSpec q = f;
        q.id = QStringLiteral("seg-int8");
        q.fileName = QStringLiteral("seg-dir/model.int8.onnx");
        q.downloadUrl = url(QStringLiteral("/seg/model.int8.onnx"));
        q.sizeBytes = m_int8.size();
        q.sha256 = sha(m_int8);
        q.note = QStringLiteral("optional");
        return {f, q};
    }

    // Starts a download and waits for it; returns whether it succeeded.
    bool download(VoiceModelDownloader& dl, const VoiceModelSpec& s, bool overwrite = false)
    {
        QSignalSpy fin(&dl, &VoiceModelDownloader::finished);
        if (!dl.start(s, VoiceModelDownloader::targetPath(s, m_models), overwrite) && fin.isEmpty()) return false;
        if (fin.isEmpty() && !fin.wait(10000)) return false;
        return fin.last().at(1).toBool();
    }

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        m_models = m_dir->filePath(QStringLiteral("models"));
        m_srv = std::make_unique<fakehttp::Server>();
        m_files = {{QStringLiteral("/v1/seg/model.onnx"), m_float},
                   {QStringLiteral("/v1/seg/model.int8.onnx"), m_int8},
                   {QStringLiteral("/v1/seg/LICENSE"), m_license}};
        m_srv->handler = [this](const fakehttp::Request& r) {
            fakehttp::Reply rep;
            if (m_files.contains(r.path)) rep.body = m_files.value(r.path);
            else rep.status = 404;
            return rep;
        };
    }

    // ---- the registry: kinds and pinned hashes ----
    void everyCatalogEntryPinsASha256()
    {
        const QVector<VoiceModelSpec> all = VoiceModelRegistry::catalog();
        QCOMPARE(all.size(), 5);
        for (const VoiceModelSpec& s : all) {
            QVERIFY2(s.sha256.size() == 64, qPrintable(s.id));
            QVERIFY2(QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(s.sha256).hasMatch(), qPrintable(s.id));
            QVERIFY2(s.sizeBytes > 0, qPrintable(s.id));
            QVERIFY2(!s.downloadUrl.isEmpty(), qPrintable(s.id));
            for (const VoiceModelFile& c : s.companions) {
                QCOMPARE(c.sha256.size(), 64);
                QVERIFY(c.sizeBytes > 0);
            }
        }
        // The CAM++ hash is the one the THIRD_PARTY_NOTICES document.
        QCOMPARE(VoiceModelRegistry::spec(QStringLiteral("campplus"))->sha256,
                 QStringLiteral("aa3cfc16963a10586a9393f5035d6d6b57e98d358b347f80c2a30bf4f00ceba2"));
    }

    void segmentationKindIsListedSeparately()
    {
        // builtin() / available() / active() keep meaning "speaker-embedding models".
        for (const VoiceModelSpec& s : VoiceModelRegistry::builtin())
            QCOMPARE(s.kind, VoiceModelKind::SpeakerEmbedding);
        QCOMPARE(VoiceModelRegistry::builtin().size(), 3);

        const QVector<VoiceModelSpec> seg = VoiceModelRegistry::segmentation();
        QCOMPARE(seg.size(), 2);
        QCOMPARE(seg.at(0).id, VoiceModelRegistry::defaultSegmentationModelId());   // float = the default
        QCOMPARE(seg.at(0).fileName, QStringLiteral("pyannote-segmentation-3.0/model.onnx"));
        QCOMPARE(seg.at(0).sizeBytes, qint64(5992913));
        QCOMPARE(seg.at(1).id, QStringLiteral("pyannote-segmentation-3.0-int8"));   // int8 = optional
        QCOMPARE(seg.at(1).sizeBytes, qint64(1540506));
        QVERIFY(seg.at(1).note.contains(QStringLiteral("optional")));
        for (const VoiceModelSpec& s : seg) {
            QCOMPARE(s.kind, VoiceModelKind::Segmentation);
            QCOMPARE(s.license, QStringLiteral("MIT"));
            QCOMPARE(s.companions.size(), 1);
            QCOMPARE(s.companions.first().fileName, QStringLiteral("LICENSE"));
            QVERIFY(s.downloadUrl.startsWith(QStringLiteral("https://")));
        }
        // spec() finds every kind; the non-commercial reverb models are never listed.
        QVERIFY(VoiceModelRegistry::spec(QStringLiteral("pyannote-segmentation-3.0")).has_value());
        for (const VoiceModelSpec& s : VoiceModelRegistry::catalog()) {
            QVERIFY(!s.id.contains(QStringLiteral("reverb"), Qt::CaseInsensitive));
            QVERIFY(!s.downloadUrl.contains(QStringLiteral("reverb"), Qt::CaseInsensitive));
        }
        // A segmentation id is never a usable speaker-embedding model, even if enabled by mistake.
        const QString dir = m_dir->filePath(QStringLiteral("m2"));
        writeFile(QDir(dir).filePath(seg.at(0).fileName), "x");
        QVERIFY(VoiceModelRegistry::active({seg.at(0).id}, dir, QString()).isEmpty());
        QVERIFY(VoiceModelRegistry::available(dir, QString()).isEmpty());
    }

    // ---- the downloader: sha ok / mismatch / override ----
    void matchingShaDownloadsAndVerifies()
    {
        const VoiceModelSpec s = segSpecs().at(0);
        VoiceModelDownloader dl;
        dl.setReachableOverride(true);
        QVERIFY(download(dl, s));
        const QString target = VoiceModelDownloader::targetPath(s, m_models);
        QCOMPARE(readAll(target), m_float);
        QCOMPARE(readAll(QDir(QFileInfo(target).absolutePath()).filePath(QStringLiteral("LICENSE"))), m_license);
        QVERIFY(!QFileInfo::exists(target + QStringLiteral(".mismatch")));
        QVERIFY(!QFileInfo::exists(target + QStringLiteral(".part")));
        QCOMPARE(VoiceModelDownloader::verifyFile(s, target), VR::Ok);
        QCOMPARE(VoiceModelDownloader::sha256OfFile(target), s.sha256);
    }

    void companionsGoFirstAndAreFetchedOnce()
    {
        const QVector<VoiceModelSpec> specs = segSpecs();
        VoiceModelDownloader dl;
        dl.setReachableOverride(true);
        QSignalSpy progress(&dl, &VoiceModelDownloader::progress);
        QVERIFY(download(dl, specs.at(0)));
        QCOMPARE(m_srv->count("GET", "/seg/LICENSE"), 1);
        // Progress counts the companion and the model together and ends at the full total.
        QCOMPARE(progress.last().at(1).toLongLong(), qint64(m_float.size() + m_license.size()));
        QCOMPARE(progress.last().at(2).toLongLong(), qint64(m_float.size() + m_license.size()));
        // The second model of the folder finds the LICENSE in place.
        QVERIFY(download(dl, specs.at(1)));
        QCOMPARE(m_srv->count("GET", "/seg/LICENSE"), 1);
        QCOMPARE(m_srv->count("GET", "/seg/model.int8.onnx"), 1);
        QCOMPARE(readAll(VoiceModelDownloader::targetPath(specs.at(1), m_models)), m_int8);
    }

    void shaMismatchWarnsAndParksTheFile()
    {
        VoiceModelSpec s = segSpecs().at(0);
        s.sha256 = QString(64, QLatin1Char('0'));   // pinned hash differs from what the server sends
        VoiceModelDownloader dl;
        dl.setReachableOverride(true);
        QSignalSpy fin(&dl, &VoiceModelDownloader::finished);
        const QString target = VoiceModelDownloader::targetPath(s, m_models);
        QVERIFY(dl.start(s, target));
        QVERIFY(fin.wait(10000));
        QVERIFY(!fin.last().at(1).toBool());
        QCOMPARE(dl.lastFailure(s.id), VoiceModelDownloader::Failure::ShaMismatch);
        const QString msg = fin.last().at(2).toString();
        QVERIFY2(msg.contains(QStringLiteral("SHA-256")), qPrintable(msg));
        QVERIFY2(msg.contains(sha(m_float)), qPrintable(msg));          // the actual hash is reported
        // Nothing is in use: the file waits aside, the registry does not see it.
        QVERIFY(!QFileInfo::exists(target));
        QVERIFY(QFileInfo::exists(VoiceModelDownloader::quarantinePath(s, m_models)));
        QVERIFY(VoiceModelDownloader::hasQuarantined(s, m_models));
        QCOMPARE(VoiceModelRegistry::resolvePath(s, m_models, QString()), target);   // the expected place, no file
        QVERIFY(!QFileInfo::exists(VoiceModelRegistry::resolvePath(s, m_models, QString())));
        QVERIFY(!QFileInfo::exists(target + QStringLiteral(".part")));
    }

    void overrideMovesTheFileInAndIsPersisted()
    {
        VoiceModelSpec s = segSpecs().at(0);
        s.sha256 = QString(64, QLatin1Char('0'));
        const QString target = VoiceModelDownloader::targetPath(s, m_models);
        {
            VoiceModelDownloader dl;
            dl.setReachableOverride(true);
            QVERIFY(!download(dl, s));
            QCOMPARE(dl.lastFailure(s.id), VoiceModelDownloader::Failure::ShaMismatch);
        }
        QString error;
        QVERIFY2(VoiceModelDownloader::acceptMismatch(s, m_models, &error), qPrintable(error));
        QCOMPARE(readAll(target), m_float);
        QVERIFY(!VoiceModelDownloader::hasQuarantined(s, m_models));
        QString detail;
        QCOMPARE(VoiceModelDownloader::verifyFile(s, target, &detail), VR::Unverified);
        QVERIFY(detail.contains(QStringLiteral("accepted")));
        // The size-only (cheap) check does not look at the hash.
        QCOMPARE(VoiceModelDownloader::verifyFile(s, target, nullptr, false), VR::Ok);

        // Persisted: a fresh downloader / catalog (a restart) still says "unverified".
        LocalVoiceModelCatalog cat(m_models, QString(), {s});
        auto* job = cat.list(providers::CapabilityKind::Diarization);
        QSignalSpy fin(job, &providers::CapabilityJob::finished);
        QVERIFY(fin.wait(2000));
        QCOMPARE(job->result().size(), 1);
        QCOMPARE(job->result().first().state, providers::ModelState::Unverified);
        QVERIFY(job->result().first().available);

        // A different file at the same place is a mismatch again: the override covers one exact file.
        writeFile(target, "something else");
        QCOMPARE(VoiceModelDownloader::verifyFile(s, target, nullptr, false), VR::SizeMismatch);
        VoiceModelSpec noSize = s;
        noSize.sizeBytes = 0;
        QCOMPARE(VoiceModelDownloader::verifyFile(noSize, target), VR::ShaMismatch);

        // Nothing left to accept afterwards.
        QVERIFY(!VoiceModelDownloader::acceptMismatch(segSpecs().at(1), m_models, &error));
        QVERIFY(!error.isEmpty());
    }

    void theRegistryStillLoadsAnAcceptedModel()
    {
        // The real "campplus" descriptor with a different file in the models folder (a test build of the
        // model): it is a mismatch until the user accepts it; afterwards the registry loads it.
        const VoiceModelSpec cam = *VoiceModelRegistry::spec(QStringLiteral("campplus"));
        const QString path = VoiceModelDownloader::targetPath(cam, m_models);
        writeFile(path, "a different campplus build");
        QCOMPARE(VoiceModelDownloader::verifyFile(cam, path), VR::SizeMismatch);   // another build: another size too
        QVERIFY(!VoiceModelDownloader::hasAcceptedRecord(cam, m_models));
        QVERIFY(VoiceModelDownloader::acceptMismatch(cam, m_models));
        QVERIFY(VoiceModelDownloader::hasAcceptedRecord(cam, m_models));
        QCOMPARE(VoiceModelDownloader::verifyFile(cam, path), VR::Unverified);   // the override covers exactly this file
        const QVector<VoiceModelSpec> active = VoiceModelRegistry::active({cam.id}, m_models, QString());
        QCOMPARE(active.size(), 1);
        QCOMPARE(active.first().id, cam.id);
    }

    void aMismatchAlreadyInPlaceCanBeAcceptedToo()
    {
        // A file copied by hand whose hash differs (same size): ShaMismatch, then "use it anyway".
        VoiceModelSpec s = segSpecs().at(0);
        const QString target = VoiceModelDownloader::targetPath(s, m_models);
        writeFile(target, QByteArray(m_float.size(), 'z'));
        QCOMPARE(VoiceModelDownloader::verifyFile(s, target), VR::ShaMismatch);
        QVERIFY(VoiceModelDownloader::acceptMismatch(s, m_models));
        QCOMPARE(VoiceModelDownloader::verifyFile(s, target), VR::Unverified);
    }

    void aMismatchingCompanionIsParkedAndAcceptedLikeTheModel()
    {
        VoiceModelSpec s = segSpecs().at(0);
        s.companions[0].sha256 = QString(64, QLatin1Char('1'));
        VoiceModelDownloader dl;
        dl.setReachableOverride(true);
        QVERIFY(!download(dl, s));
        QCOMPARE(dl.lastFailure(s.id), VoiceModelDownloader::Failure::ShaMismatch);
        const QString dir = QFileInfo(VoiceModelDownloader::targetPath(s, m_models)).absolutePath();
        QVERIFY(QFileInfo::exists(dir + QStringLiteral("/LICENSE.mismatch")));
        QVERIFY(QFileInfo::exists(VoiceModelDownloader::targetPath(s, m_models)));   // the model itself matched
        QVERIFY(VoiceModelDownloader::hasQuarantined(s, m_models));
        QVERIFY(VoiceModelDownloader::acceptMismatch(s, m_models));
        QCOMPARE(readAll(dir + QStringLiteral("/LICENSE")), m_license);
        QVERIFY(!VoiceModelDownloader::hasQuarantined(s, m_models));
    }

    void sizeMismatchStillWritesNothing()
    {
        VoiceModelSpec s = segSpecs().at(0);
        s.sizeBytes += 1;
        VoiceModelDownloader dl;
        dl.setReachableOverride(true);
        QVERIFY(!download(dl, s));
        QCOMPARE(dl.lastFailure(s.id), VoiceModelDownloader::Failure::SizeMismatch);
        QVERIFY(!QFileInfo::exists(VoiceModelDownloader::targetPath(s, m_models)));
        QVERIFY(!VoiceModelDownloader::hasQuarantined(s, m_models));
        QVERIFY(!QFileInfo::exists(VoiceModelDownloader::targetPath(s, m_models) + QStringLiteral(".part")));
    }

    void deleteRemovesTheParkedFileAndTheLicenseWithTheLastModel()
    {
        const QVector<VoiceModelSpec> specs = segSpecs();
        VoiceModelDownloader dl;
        dl.setReachableOverride(true);
        QVERIFY(download(dl, specs.at(0)));
        QVERIFY(download(dl, specs.at(1)));
        const QString dir = QFileInfo(VoiceModelDownloader::targetPath(specs.at(0), m_models)).absolutePath();
        QString error;
        QVERIFY(VoiceModelDownloader::deleteModelFile(specs.at(0), m_models, &error));
        QVERIFY(QFileInfo::exists(dir + QStringLiteral("/LICENSE")));          // the int8 model still needs it
        QVERIFY(VoiceModelDownloader::deleteModelFile(specs.at(1), m_models, &error));
        QVERIFY(!QFileInfo::exists(dir + QStringLiteral("/LICENSE")));
        QVERIFY(!QFileInfo::exists(dir));                                      // the empty folder goes too

        // A parked (mismatching) download can be deleted although no model file exists.
        VoiceModelSpec bad = specs.at(0);
        bad.sha256 = QString(64, QLatin1Char('0'));
        QVERIFY(!download(dl, bad, true));
        QVERIFY(VoiceModelDownloader::hasQuarantined(bad, m_models));
        QVERIFY(VoiceModelDownloader::deleteModelFile(bad, m_models, &error));
        QVERIFY(!VoiceModelDownloader::hasQuarantined(bad, m_models));
    }

    // ---- the catalog ----
    void catalogListsTheKindsByCapability()
    {
        LocalVoiceModelCatalog cat(m_models, QString());   // the built-in catalog, nothing downloaded
        auto* seg = cat.list(providers::CapabilityKind::Diarization);
        QSignalSpy f1(seg, &providers::CapabilityJob::finished);
        QVERIFY(f1.wait(2000));
        QCOMPARE(seg->result().size(), 2);
        QCOMPARE(seg->result().at(0).id, VoiceModelRegistry::defaultSegmentationModelId());
        QVERIFY(seg->result().at(0).recommended);
        QVERIFY(!seg->result().at(1).recommended);
        QVERIFY(seg->result().at(1).note.contains(QStringLiteral("optional")));
        for (const auto& m : seg->result()) {
            QCOMPARE(m.capability, providers::CapabilityKind::Diarization);
            QVERIFY(!m.available);
            QCOMPARE(m.sha256.size(), 64);
            QCOMPARE(m.state, providers::ModelState::Unchecked);
        }
        auto* emb = cat.list(providers::CapabilityKind::VoiceEmbedding);
        QSignalSpy f2(emb, &providers::CapabilityJob::finished);
        QVERIFY(f2.wait(2000));
        QCOMPARE(emb->result().size(), 3);
        for (const auto& m : emb->result()) QVERIFY(m.id.indexOf(QStringLiteral("pyannote")) < 0);
        auto* other = cat.list(providers::CapabilityKind::Chat);
        QSignalSpy f3(other, &providers::CapabilityJob::finished);
        QVERIFY(f3.wait(2000));
        QVERIFY(other->result().isEmpty());
    }

    void ensureAvailableDownloadsTheSegmentationModelWithItsLicense()
    {
        LocalVoiceModelCatalog cat(m_models, QString(), segSpecs());
        cat.downloader()->setReachableOverride(true);
        auto* job = cat.ensureAvailable(QStringLiteral("seg-float"));
        QSignalSpy fin(job, &providers::CapabilityJob::finished);
        QSignalSpy bad(job, &providers::CapabilityJob::failed);
        QVERIFY(fin.wait(10000) || !bad.isEmpty());
        QVERIFY(bad.isEmpty());
        QVERIFY(job->result().isLocal());
        QCOMPARE(readAll(job->result().localPath), m_float);
        QVERIFY(QFileInfo::exists(QFileInfo(job->result().localPath).absolutePath() + QStringLiteral("/LICENSE")));

        auto* list = cat.list(providers::CapabilityKind::Diarization);
        QSignalSpy lf(list, &providers::CapabilityJob::finished);
        QVERIFY(lf.wait(2000));
        QCOMPARE(list->result().at(0).state, providers::ModelState::Verified);
        QVERIFY(list->result().at(0).available);
        QVERIFY(!list->result().at(1).available);          // the int8 model is optional: not fetched
        QCOMPARE(m_srv->count("GET", "model.int8.onnx"), 0);
    }

    void ensureAvailableOnAShaMismatchFailsWithACodeAndTheOverrideRecovers()
    {
        QVector<VoiceModelSpec> specs = segSpecs();
        specs[0].sha256 = QString(64, QLatin1Char('0'));
        LocalVoiceModelCatalog cat(m_models, QString(), specs);
        cat.downloader()->setReachableOverride(true);
        auto* job = cat.ensureAvailable(QStringLiteral("seg-float"));
        QSignalSpy bad(job, &providers::CapabilityJob::failed);
        QVERIFY(bad.wait(10000));
        QCOMPARE(job->error().code, QStringLiteral("sha_mismatch"));
        QVERIFY(job->error().detail.contains(QStringLiteral("SHA-256")));

        auto* l1 = cat.list(providers::CapabilityKind::Diarization);
        QSignalSpy f1(l1, &providers::CapabilityJob::finished);
        QVERIFY(f1.wait(2000));
        QCOMPARE(l1->result().at(0).state, providers::ModelState::Mismatch);   // parked, not available
        QVERIFY(!l1->result().at(0).available);

        QString error;
        QVERIFY2(cat.acceptMismatch(QStringLiteral("seg-float"), &error), qPrintable(error));
        QVERIFY(!cat.acceptMismatch(QStringLiteral("nope"), &error));
        auto* l2 = cat.list(providers::CapabilityKind::Diarization);
        QSignalSpy f2(l2, &providers::CapabilityJob::finished);
        QVERIFY(f2.wait(2000));
        QCOMPARE(l2->result().at(0).state, providers::ModelState::Unverified);
        QVERIFY(l2->result().at(0).available);

        // ensureAvailable now just hands out the local file, no new download.
        const int gets = m_srv->count("GET", "model.onnx");
        auto* again = cat.ensureAvailable(QStringLiteral("seg-float"));
        QSignalSpy fin(again, &providers::CapabilityJob::finished);
        QVERIFY(fin.wait(2000));
        QCOMPARE(m_srv->count("GET", "model.onnx"), gets);
    }

    void aCorruptFileIsReportedAsCorruptByTheCatalog()
    {
        const VoiceModelSpec s = segSpecs().at(0);
        writeFile(VoiceModelDownloader::targetPath(s, m_models), "short");
        LocalVoiceModelCatalog cat(m_models, QString(), {s});
        auto* job = cat.list(providers::CapabilityKind::Diarization);
        QSignalSpy fin(job, &providers::CapabilityJob::finished);
        QVERIFY(fin.wait(2000));
        QCOMPARE(job->result().first().state, providers::ModelState::Corrupt);
    }
};

QTEST_GUILESS_MAIN(TestVoiceModelIntegrity)
#include "test_voice_model_integrity.moc"
