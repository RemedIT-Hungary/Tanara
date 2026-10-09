//
// Tanara — eszköz kihúzása felvétel közben, az AppController szintjén (hamis capture-motor és
// szimulált eszköz-felsorolás, valódi hangeszköz nélkül). Az eszköz-halmaz változása NEM
// állítja le a felvételt: a kihúzott eszköz sávja két egymás utáni hiány után lezárul
// (recordingTrackClosed), a többi sáv megy tovább, a felvétel végén minden sáv a meeting része.
// Az encoder a valódi ffmpeg (nélküle QSKIP). Izolált TANARA_HOME.
//
#include <QtTest>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <memory>

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/audio/DeviceManager.h"

#include "fake_audio_engine.h"

using namespace tanara;

namespace {

bool haveFfmpeg()
{
    return !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()
        && !QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty();
}

qint64 probeMs(const QString& path)
{
    QProcess p;
    p.start(QStringLiteral("ffprobe"), {QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
        QStringLiteral("-of"), QStringLiteral("csv=p=0"), path});
    if (!p.waitForFinished(10000)) { p.kill(); return -1; }
    return qint64(QString::fromUtf8(p.readAllStandardOutput()).trimmed().toDouble() * 1000.0);
}

AudioDeviceInfo dev(const QString& name, TrackKind kind)
{
    AudioDeviceInfo d;
    d.id = name;
    d.name = name;
    d.kind = kind;
    d.channels = 1;
    return d;
}

} // namespace

class DeviceUnplugTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void unplugDuringRecordingClosesOnlyThatTrack();
};

void DeviceUnplugTest::initTestCase()
{
    qputenv("TANARA_CLOUD", "off");
}

void DeviceUnplugTest::unplugDuringRecordingClosesOnlyThatTrack()
{
    if (!haveFfmpeg()) QSKIP("ffmpeg/ffprobe nincs a PATH-on");
    QTemporaryDir home;
    QVERIFY(home.isValid());
    qputenv("TANARA_HOME", home.path().toUtf8());

    auto app = std::make_unique<AppController>();
    QVERIFY(app->settings()->settings().audioDir.startsWith(home.path()));
    app->setAutoMixdownAfterRecording(false);

    const AudioDeviceInfo mic = dev(QStringLiteral("Trust USB mikrofon"), TrackKind::Mic);
    const AudioDeviceInfo headset = dev(QStringLiteral("Sennheiser - Füles"), TrackKind::Mic);
    const AudioDeviceInfo mon = dev(QStringLiteral("Monitor of Kanto"), TrackKind::Loopback);
    app->devices()->setDeviceListOverride(QVector<AudioDeviceInfo>{mic, headset, mon});
    app->refreshDevices();

    FakeEngine* engine = nullptr;
    app->setRecordingEngineFactory([&engine] {
        auto e = std::make_unique<FakeEngine>();
        engine = e.get();
        return e;
    });
    QTimer feeder;
    feeder.setInterval(5);
    QObject::connect(&feeder, &QTimer::timeout, [&engine] { if (engine) engine->feed(); });

    QSignalSpy closed(app.get(), &AppController::recordingTrackClosed);
    QSignalSpy finished(app.get(), &AppController::recordingFinished);
    QStringList errors;
    connect(app.get(), &AppController::errorOccurred, this, [&errors](const QString& e) { errors << e; });

    app->startRecording(QStringLiteral("Kihúzás"), {mic, headset, mon});
    QCOMPARE(app->recordingState(), RecordingState::Recording);
    QVERIFY(engine);
    feeder.start();
    QTest::qWait(600);

    // Kihúzás: az eszköz eltűnik a felsorolásból, a capture-je elhal.
    engine->vanish(headset.name);
    app->devices()->setDeviceListOverride(QVector<AudioDeviceInfo>{mic, mon});
    app->refreshDevices();
    // Egyetlen hiány még nem zár (átmeneti eltűnés), és a felvétel semmiképp nem áll le.
    QCOMPARE(closed.size(), 0);
    QCOMPARE(app->recordingState(), RecordingState::Recording);
    // A második (időzített) felsorolás után lezárul a sáv.
    QVERIFY(closed.wait(4000));
    QCOMPARE(closed.size(), 1);
    QCOMPARE(closed.at(0).at(0).toString(), headset.name);
    QCOMPARE(app->recordingState(), RecordingState::Recording);
    QCOMPARE(app->recordingDeviceNames(), (QStringList{mic.name, mon.name}));
    QCOMPARE(app->disconnectedRecordingDeviceNames(), QStringList{headset.name});
    QCOMPARE(engine->closeCalls, 1);

    // További eszköz-változások (és egy átmenetileg üres felsorolás) sem állítják le.
    app->refreshDevices();
    app->devices()->setDeviceListOverride(QVector<AudioDeviceInfo>{});
    app->refreshDevices();
    app->devices()->setDeviceListOverride(QVector<AudioDeviceInfo>{mic, mon});
    app->refreshDevices();
    QTest::qWait(1500);
    QCOMPARE(app->recordingState(), RecordingState::Recording);
    QCOMPARE(closed.size(), 1);

    app->stopRecording();
    QVERIFY(finished.wait(20000));
    feeder.stop();
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));

    const Meeting m = finished.at(0).at(0).value<Meeting>();
    QCOMPARE(m.tracks.size(), 3);
    for (const Track& t : m.tracks) QCOMPARE(t.startOffsetMs, qint64(0));
    const qint64 dMic = probeMs(QDir(m.folder).filePath(m.tracks.at(0).file));
    const qint64 dHs = probeMs(QDir(m.folder).filePath(m.tracks.at(1).file));
    QVERIFY2(qAbs(dMic - m.durationMs) < 300, qPrintable(QStringLiteral("%1 vs %2").arg(dMic).arg(m.durationMs)));
    QVERIFY2(dHs > 400 && dHs < 900, qPrintable(QString::number(dHs)));   // a kihúzásig (~0,6 s)

    app.reset();
    qunsetenv("TANARA_HOME");
}

QTEST_GUILESS_MAIN(DeviceUnplugTest)
#include "test_device_unplug.moc"
