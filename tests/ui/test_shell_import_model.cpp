// ShellImportModel — a „Hangfájl importálása” ablak modellje: demó-állapotok controller
// nélkül, dátum-értelmezés, majd izolált TANARA_HOME-on valódi ffmpeg-gel: fájlok
// hozzáadása (útvonal / file:// URL, ismétlődés és nem-hang fájl), bontás, saját sáv,
// indítás → kész megbeszélés, és megszakítás (az űrlap megmarad, meeting nem keletkezik).
#include "ShellActions.h"
#include "ShellImportModel.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/store/MeetingStore.h"

#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

using tanara_qml::ShellActions;
using tanara_qml::ShellImportModel;

namespace {

bool ffmpeg(const QStringList& args)
{
    QProcess p;
    p.start(QStringLiteral("ffmpeg"), QStringList{"-hide_banner", "-loglevel", "error", "-y"} + args);
    return p.waitForFinished(60000) && p.exitCode() == 0;
}

} // namespace

class TestShellImportModel : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<tanara::AppController> m_app;
    bool m_ffmpeg = false;

    QString src(const QString& name) const { return m_home->filePath("src/" + name); }

private slots:
    void initTestCase()
    {
        m_ffmpeg = !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()
                && !QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty();
        qputenv("TANARA_CLOUD", "off");
    }
    void init()
    {
        m_home = std::make_unique<QTemporaryDir>();
        QVERIFY(m_home->isValid());
        QVERIFY(QDir().mkpath(m_home->filePath("src")));
        qputenv("TANARA_HOME", m_home->filePath("home").toUtf8());
    }
    void cleanup()
    {
        m_app.reset();
        m_home.reset();
    }

    void parsesDates()
    {
        QCOMPARE(ShellImportModel::parseDate("2026-03-05 14:30"), QDateTime(QDate(2026, 3, 5), QTime(14, 30)));
        QCOMPARE(ShellImportModel::parseDate("2026. 03. 05. 9:05"), QDateTime(QDate(2026, 3, 5), QTime(9, 5)));
        QCOMPARE(ShellImportModel::parseDate("2026-03-05T14:30"), QDateTime(QDate(2026, 3, 5), QTime(14, 30)));
        QCOMPARE(ShellImportModel::parseDate(" 2026.3.5 "), QDateTime(QDate(2026, 3, 5), QTime(12, 0)));
        QVERIFY(!ShellImportModel::parseDate("tegnap").isValid());
        QVERIFY(!ShellImportModel::parseDate("2026-13-05 10:00").isValid());
        QVERIFY(!ShellImportModel::parseDate("2026-03-05 25:00").isValid());
        QVERIFY(!ShellImportModel::parseDate("").isValid());
    }

    void demoStatesWithoutController()
    {
        ShellImportModel model;
        QCOMPARE(model.fileCount(), 0);
        QVERIFY(!model.canStart());
        QVERIFY(model.trackSummary().isEmpty());

        model.setDemoState(QStringLiteral("files"));
        QCOMPARE(model.fileCount(), 3);
        QCOMPARE(model.trackCount(), 3);
        QCOMPARE(model.trackSummary(), QStringLiteral("3 sáv lesz belőle."));
        QCOMPARE(model.title(), QStringLiteral("Fókuszcsoport 3"));
        QVERIFY(model.dateValid());
        QVERIFY(model.files().at(2).toMap().value("video").toBool());

        model.setDemoState(QStringLiteral("split"));
        QCOMPARE(model.trackNames(), QStringList({"Bal csatorna", "Jobb csatorna"}));
        QCOMPARE(model.ownTrack(), 0);

        model.setDemoState(QStringLiteral("probing"));
        QVERIFY(model.probing());
        QVERIFY(!model.canStart());

        model.setDemoState(QStringLiteral("error"));
        QCOMPARE(model.files().at(1).toMap().value("state").toString(), QStringLiteral("error"));
        QCOMPARE(model.trackSummary(), QStringLiteral("1 sáv lesz belőle. 1 fájl kimarad."));

        model.setDemoState(QStringLiteral("progress"));
        QVERIFY(model.running());
        QCOMPARE(model.percent(), 42);
        QVERIFY(!model.start());   // controller nélkül nem indul semmi
    }

    void importsThroughTheModel()
    {
        if (!m_ffmpeg) QSKIP("ffmpeg / ffprobe nincs telepítve");
        m_app = std::make_unique<tanara::AppController>();
        m_app->setAutoMixdownAfterRecording(false);
        const QString stereo = src("Páros interjú.wav");
        QVERIFY(ffmpeg({"-f", "lavfi", "-i", "aevalsrc=0.5*sin(440*2*PI*t)|0.5*sin(660*2*PI*t):d=2:s=16000", stereo}));
        QFile text(src("jegyzet.txt"));
        QVERIFY(text.open(QIODevice::WriteOnly));
        text.write("nem hang");
        text.close();

        ShellImportModel model;
        model.setController(m_app.get());
        ShellActions shell;
        shell.setController(m_app.get());
        QSignalSpy imported(&model, &ShellImportModel::imported);

        // Útvonal, file:// URL (ugyanaz a fájl: egyszer kerül fel), nem-hang fájl, mappa (kimarad).
        QCOMPARE(model.addFiles({stereo, QUrl::fromLocalFile(stereo), text.fileName(), m_home->path()}), 2);
        QVERIFY(model.probing());
        QVERIFY(!model.canStart());
        QTRY_VERIFY_WITH_TIMEOUT(!model.probing(), 10000);
        QCOMPARE(model.fileCount(), 2);
        const QVariantMap first = model.files().at(0).toMap();
        QCOMPARE(first.value("state").toString(), QStringLiteral("ok"));
        QVERIFY(first.value("canSplit").toBool());
        QVERIFY(!first.value("split").toBool());                 // sima sztereó: alapból nem bontjuk
        QVERIFY(first.value("meta").toString().contains(QStringLiteral("sztereó")));
        const QVariantMap second = model.files().at(1).toMap();
        QCOMPARE(second.value("state").toString(), QStringLiteral("error"));
        QVERIFY(!second.value("error").toString().isEmpty());
        QVERIFY(!second.value("error").toString().contains("jegyzet.txt"));   // a sorban a név már ott van

        QCOMPARE(model.title(), QStringLiteral("Páros interjú"));
        QVERIFY(model.dateValid());
        QVERIFY(!model.dateHint().isEmpty());
        QCOMPARE(model.trackSummary(), QStringLiteral("1 sáv lesz belőle. 1 fájl kimarad."));
        QVERIFY(model.canStart());

        model.setSplit(0, true);
        QCOMPARE(model.trackNames(), QStringList({"Bal csatorna", "Jobb csatorna"}));
        model.setOwnTrack(1);
        model.setOwnTrack(7);                                    // nincs ilyen sáv → nincs saját
        QCOMPARE(model.ownTrack(), -1);
        model.setOwnTrack(1);
        model.setTitle(QStringLiteral("Interjú a kutatáshoz"));
        model.setDateText(QStringLiteral("rossz"));
        QVERIFY(!model.canStart());
        model.setDateText(QStringLiteral("2025-06-10 08:45"));
        QVERIFY(model.canStart());

        QVERIFY(model.start());
        QVERIFY(model.running());
        QVERIFY(!model.canStart());
        QCOMPARE(model.runningTitle(), QStringLiteral("Interjú a kutatáshoz"));
        QTRY_COMPARE_WITH_TIMEOUT(imported.size(), 1, 20000);
        QVERIFY(!model.running());
        QCOMPARE(model.fileCount(), 0);                          // az űrlap kiürült

        const QString id = imported.first().at(0).toString();
        const tanara::Meeting m = m_app->store()->load(id);
        QCOMPARE(m.title, QStringLiteral("Interjú a kutatáshoz"));
        QCOMPARE(m.startedAt, QDateTime(QDate(2025, 6, 10), QTime(8, 45)));
        QCOMPARE(m.tracks.size(), 2);
        QCOMPARE(m.tracks[0].kind, tanara::TrackKind::Other);
        QCOMPARE(m.tracks[1].kind, tanara::TrackKind::Mic);
        QVERIFY(!m.hasTranscript);
        // A héj kijelölte az új megbeszélést (az átirat előtti nézettel).
        QCOMPARE(shell.currentMeetingId(), id);
        QCOMPARE(shell.currentTab(), 0);
    }

    void cancelKeepsTheForm()
    {
        if (!m_ffmpeg) QSKIP("ffmpeg / ffprobe nincs telepítve");
        m_app = std::make_unique<tanara::AppController>();
        const QString big = src("hosszu.wav");
        QVERIFY(ffmpeg({"-f", "lavfi", "-i", "sine=frequency=440:duration=2400:sample_rate=16000", "-ac", "1", big}));

        ShellImportModel model;
        model.setController(m_app.get());
        QSignalSpy imported(&model, &ShellImportModel::imported);
        QSignalSpy failed(&model, &ShellImportModel::failed);
        model.addFiles({big});
        QTRY_VERIFY_WITH_TIMEOUT(!model.probing(), 10000);
        QVERIFY(model.start());
        QTRY_VERIFY_WITH_TIMEOUT(model.percent() > 0, 20000);
        QVERIFY(model.progressText().contains(QLatin1Char('%')));
        model.cancel();
        QVERIFY(model.cancelling());
        QTRY_VERIFY_WITH_TIMEOUT(!model.running(), 10000);
        QVERIFY(imported.isEmpty());
        QVERIFY(failed.isEmpty());
        QVERIFY(model.error().isEmpty());
        QCOMPARE(model.fileCount(), 1);                          // javítható, újraindítható
        QVERIFY(model.canStart());
        QVERIFY(m_app->store()->loadAll().isEmpty());

        // Hiányzó fájl menet közben: a hiba az ablakban marad.
        QVERIFY(QFile::remove(big));
        QVERIFY(model.start());
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 10000);
        QVERIFY(!model.error().isEmpty());
        QVERIFY(!model.running());
        model.reset();
        QVERIFY(model.error().isEmpty());
        QCOMPARE(model.fileCount(), 0);
    }
};

QTEST_MAIN(TestShellImportModel)
#include "test_shell_import_model.moc"
