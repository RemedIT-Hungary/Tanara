// VoiceModelDownloader — a CLI és a GUI közös modell-letöltője. HÁLÓZAT NÉLKÜL: a leíró
// letöltési címe egy ideiglenes mappában lévő file:// URL (a QNetworkAccessManager helyi fájlt
// olvas), az elérhetőség felülírva. A célmappa is ideiglenes (valódi ~/.tanara-t nem érint).
#include "tanara/voiceid/VoiceModelDownloader.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

using tanara::VoiceModelDownloader;
using tanara::VoiceModelSpec;

class TestVoiceModelDownloader : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;

    QString writeSource(const QString& name, const QByteArray& data)
    {
        const QString path = m_dir->filePath(QStringLiteral("src/") + name);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) return {};
        f.write(data);
        return path;
    }
    VoiceModelSpec spec(const QString& srcPath, qint64 size)
    {
        VoiceModelSpec s;
        s.id = QStringLiteral("fake-model");
        s.displayName = QStringLiteral("Fake");
        s.fileName = QStringLiteral("fake.onnx");
        s.downloadUrl = QUrl::fromLocalFile(srcPath).toString();
        s.sizeBytes = size;
        return s;
    }
    QString target() const { return m_dir->filePath(QStringLiteral("meta/models/fake.onnx")); }
    static QByteArray payload() { return QByteArray(300 * 1024, 'x') + QByteArray("end"); }
    static QByteArray readAll(const QString& path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
    }

    void targetIsUnderMetaModels()
    {
        VoiceModelSpec s;
        s.fileName = QStringLiteral("a.onnx");
        QCOMPARE(VoiceModelDownloader::targetPath(s, QStringLiteral("/x/meta")), QStringLiteral("/x/meta/models/a.onnx"));
    }

    void downloadsFromAFileUrlWithProgress()
    {
        const QByteArray data = payload();
        const VoiceModelSpec s = spec(writeSource(QStringLiteral("m.onnx"), data), data.size());
        VoiceModelDownloader dl;
        dl.setReachableOverride(false);   // file:// URL-nél a hálózat nem számít
        QSignalSpy started(&dl, &VoiceModelDownloader::started);
        QSignalSpy progress(&dl, &VoiceModelDownloader::progress);
        QSignalSpy finished(&dl, &VoiceModelDownloader::finished);
        QVERIFY(dl.start(s, target()));
        QVERIFY(dl.isRunning(s.id));
        QCOMPARE(dl.runningIds(), QStringList{s.id});
        QCOMPARE(started.count(), 1);
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.at(0).at(0).toString(), s.id);
        QVERIFY2(finished.at(0).at(1).toBool(), qPrintable(finished.at(0).at(2).toString()));
        QVERIFY(!dl.isRunning(s.id));
        QCOMPARE(dl.lastFailure(s.id), VoiceModelDownloader::Failure::None);
        QCOMPARE(readAll(target()), data);
        QVERIFY(progress.count() >= 2);
        const QList<QVariant> last = progress.last();
        QCOMPARE(last.at(1).toLongLong(), qint64(data.size()));
        QCOMPARE(last.at(2).toLongLong(), qint64(data.size()));
    }

    void refusesToOverwriteAnExistingFile()
    {
        const QByteArray data = payload();
        const VoiceModelSpec s = spec(writeSource(QStringLiteral("m.onnx"), data), data.size());
        QDir().mkpath(QFileInfo(target()).absolutePath());
        {
            QFile f(target());
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("old");
        }
        VoiceModelDownloader dl;
        QSignalSpy finished(&dl, &VoiceModelDownloader::finished);
        QVERIFY(!dl.start(s, target()));
        QCOMPARE(finished.count(), 1);           // szinkron válasz
        QVERIFY(!finished.at(0).at(1).toBool());
        QCOMPARE(dl.lastFailure(s.id), VoiceModelDownloader::Failure::Exists);
        QCOMPARE(readAll(target()), QByteArray("old"));

        // Kifejezett felülírással (CLI --force, sérült fájl újratöltése) lecseréli.
        QVERIFY(dl.start(s, target(), true));
        QVERIFY(finished.wait(5000));
        QVERIFY(finished.last().at(1).toBool());
        QCOMPARE(readAll(target()), data);
    }

    void sizeMismatchLeavesNothingBehind()
    {
        const QByteArray data = payload();
        const VoiceModelSpec s = spec(writeSource(QStringLiteral("m.onnx"), data), data.size() + 1);
        VoiceModelDownloader dl;
        QSignalSpy finished(&dl, &VoiceModelDownloader::finished);
        QVERIFY(dl.start(s, target()));
        QVERIFY(finished.wait(5000));
        QVERIFY(!finished.at(0).at(1).toBool());
        QCOMPARE(dl.lastFailure(s.id), VoiceModelDownloader::Failure::SizeMismatch);
        QVERIFY(!QFileInfo::exists(target()));
    }

    void missingSourceIsANetworkFailure()
    {
        VoiceModelSpec s = spec(m_dir->filePath(QStringLiteral("nincs-ilyen.onnx")), 10);
        VoiceModelDownloader dl;
        QSignalSpy finished(&dl, &VoiceModelDownloader::finished);
        QVERIFY(dl.start(s, target()));
        QVERIFY(finished.wait(5000));
        QVERIFY(!finished.at(0).at(1).toBool());
        QCOMPARE(dl.lastFailure(s.id), VoiceModelDownloader::Failure::Network);
        QVERIFY(!QFileInfo::exists(target()));
    }

    void cancelBeforeTheFirstByte()
    {
        const QByteArray data = payload();
        const VoiceModelSpec s = spec(writeSource(QStringLiteral("m.onnx"), data), data.size());
        VoiceModelDownloader dl;
        QSignalSpy finished(&dl, &VoiceModelDownloader::finished);
        QVERIFY(dl.start(s, target()));
        dl.cancel(s.id);
        QVERIFY(finished.count() == 1 || finished.wait(5000));
        QCOMPARE(finished.count(), 1);
        QVERIFY(!finished.at(0).at(1).toBool());
        QCOMPARE(dl.lastFailure(s.id), VoiceModelDownloader::Failure::Cancelled);
        QVERIFY(!dl.isRunning(s.id));
        QVERIFY(!QFileInfo::exists(target()));
        dl.cancel(s.id);                          // már nem fut: nincs újabb jel
        QCOMPARE(finished.count(), 1);
    }

    void offlineDoesNotStartAHttpDownload()
    {
        VoiceModelSpec s;
        s.id = QStringLiteral("net-model");
        s.fileName = QStringLiteral("net.onnx");
        s.downloadUrl = QStringLiteral("https://example.invalid/net.onnx");   // sosem kérjük le
        VoiceModelDownloader dl;
        dl.setReachableOverride(false);
        QSignalSpy finished(&dl, &VoiceModelDownloader::finished);
        QSignalSpy started(&dl, &VoiceModelDownloader::started);
        QVERIFY(!dl.start(s, m_dir->filePath(QStringLiteral("meta/models/net.onnx"))));
        QCOMPARE(started.count(), 0);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(dl.lastFailure(s.id), VoiceModelDownloader::Failure::Offline);
        QVERIFY(!QFileInfo::exists(m_dir->filePath(QStringLiteral("meta/models"))));
    }

    void noUrlAndBusyAreRefused()
    {
        VoiceModelSpec s;
        s.id = QStringLiteral("no-url");
        VoiceModelDownloader dl;
        QVERIFY(!dl.start(s, target()));
        QCOMPARE(dl.lastFailure(s.id), VoiceModelDownloader::Failure::NoUrl);

        const QByteArray data = payload();
        const VoiceModelSpec ok = spec(writeSource(QStringLiteral("m.onnx"), data), data.size());
        QSignalSpy finished(&dl, &VoiceModelDownloader::finished);
        QVERIFY(dl.start(ok, target()));
        QVERIFY(!dl.start(ok, target()));          // ugyanaz még fut: jel nélkül elutasítja
        QCOMPARE(finished.count(), 0);
        QVERIFY(dl.isRunning(ok.id));              // az eredeti letöltés megy tovább
        QVERIFY(finished.wait(5000));
        QVERIFY(finished.last().at(1).toBool());
        QCOMPARE(readAll(target()), data);
    }

    // G4 (B1): „ellenőrzés” állapot (méret-egyezés) és a letöltött fájl törlése.
    void verifyStatesAndDelete()
    {
        const QByteArray data = payload();
        VoiceModelSpec s = spec(writeSource(QStringLiteral("m.onnx"), data), data.size());
        const QString meta = m_dir->filePath(QStringLiteral("meta"));
        using VR = VoiceModelDownloader::VerifyResult;
        QCOMPARE(VoiceModelDownloader::verifyFile(s, target()), VR::Missing);

        QDir().mkpath(QFileInfo(target()).absolutePath());
        {
            QFile f(target());
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data.left(1000));   // csonka
        }
        QString detail;
        QCOMPARE(VoiceModelDownloader::verifyFile(s, target(), &detail), VR::SizeMismatch);
        QVERIFY(detail.contains(QStringLiteral("1000")));
        VoiceModelSpec noSize = s;
        noSize.sizeBytes = 0;
        QCOMPARE(VoiceModelDownloader::verifyFile(noSize, target()), VR::Unverifiable);

        // Aszinkron: a hívásban „ellenőrzés” állapot, a következő körben az eredmény.
        VoiceModelDownloader dl;
        QSignalSpy started(&dl, &VoiceModelDownloader::verifyStarted);
        QSignalSpy verified(&dl, &VoiceModelDownloader::verified);
        QVERIFY(!dl.lastVerify(s.id).has_value());
        QVERIFY(dl.verify(s, target()));
        QCOMPARE(started.count(), 1);
        QVERIFY(dl.isVerifying(s.id));
        QVERIFY(!dl.verify(s, target()));   // már ellenőrződik
        QVERIFY(verified.wait(5000));
        QVERIFY(!dl.isVerifying(s.id));
        QCOMPARE(verified.first().at(1).value<VR>(), VR::SizeMismatch);
        QCOMPARE(*dl.lastVerify(s.id), VR::SizeMismatch);

        // Törlés: csak a <meta>/models alatti fájl; másodszorra nincs mit.
        QString error;
        QVERIFY(VoiceModelDownloader::deleteModelFile(s, meta, &error));
        QVERIFY(!QFileInfo::exists(target()));
        QVERIFY(!VoiceModelDownloader::deleteModelFile(s, meta, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(QFileInfo::exists(writeSource(QStringLiteral("m.onnx"), data)));   // a forrás érintetlen

        // Letöltés után rendben.
        QSignalSpy finished(&dl, &VoiceModelDownloader::finished);
        QVERIFY(dl.start(s, target()));
        QVERIFY(finished.wait(5000));
        QCOMPARE(VoiceModelDownloader::verifyFile(s, target()), VR::Ok);
    }

    void offlineErrorClassification()
    {
        QVERIFY(VoiceModelDownloader::isOfflineError(3));    // HostNotFoundError
        QVERIFY(!VoiceModelDownloader::isOfflineError(203)); // ContentNotFoundError
        QVERIFY(!VoiceModelDownloader::isOfflineError(0));
    }
};

QTEST_GUILESS_MAIN(TestVoiceModelDownloader)
#include "test_voice_model_downloader.moc"
