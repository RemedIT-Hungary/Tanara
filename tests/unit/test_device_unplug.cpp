// Tanara: unplugging a device during a recording, at the AppController level (fake capture
// engine and simulated device enumeration, no real audio device). A change of the device set
// does NOT stop the recording: the unplugged device's track closes after two consecutive
// absences (recordingTrackClosed), the other tracks go on, and at the end of the recording
// every track is part of the meeting. The encoder is the real ffmpeg (without it QSKIP). Isolated TANARA_HOME.
#include <QtTest>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <memory>

#include "tanara/app/AppController.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/recording/audio/DeviceManager.h"

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

    auto app = std::make_unique<app::AppController>();
    QVERIFY(app->settings()->paths().primaryRecordingRoot().startsWith(home.path()));
    app->setAutoMixdownAfterRecording(false);

    const AudioDeviceInfo mic = dev(QStringLiteral("Trust USB mikrofon"), TrackKind::Mic);
    const AudioDeviceInfo headset = dev(QStringLiteral("Sennheiser - Füles"), TrackKind::Mic);
    const AudioDeviceInfo mon = dev(QStringLiteral("Monitor of Kanto"), TrackKind::Loopback);
    app->devices()->setDeviceListOverride(QVector<AudioDeviceInfo>{mic, headset, mon});
    app->refreshDevices();

    FakeEngine* engine = nullptr;
    std::shared_ptr<std::atomic<bool>> alive;
    app->setRecordingEngineFactory([&engine, &alive] {
        auto e = std::make_unique<FakeEngine>();
        engine = e.get();
        alive = engine->alive;
        return e;
    });
    QTimer feeder;
    feeder.setInterval(5);
    QObject::connect(&feeder, &QTimer::timeout, [&engine, &alive] { if (engine && alive && *alive) engine->feed(); });

    QSignalSpy closed(app.get(), &app::AppController::recordingTrackClosed);
    QSignalSpy finished(app.get(), &app::AppController::recordingFinished);
    QStringList errors;
    connect(app.get(), &app::AppController::errorOccurred, this, [&errors](const QString& e) { errors << e; });

    app->startRecording(QStringLiteral("Kihúzás"), {mic, headset, mon});
    QCOMPARE(app->recordingState(), RecordingState::Recording);
    QVERIFY(engine);
    feeder.start();
    QTest::qWait(600);

    // Unplug: the device disappears from the enumeration, its capture dies.
    engine->vanish(headset.name);
    app->devices()->setDeviceListOverride(QVector<AudioDeviceInfo>{mic, mon});
    app->refreshDevices();
    // A single absence does not close yet (a transient disappearance), and the recording never stops.
    QCOMPARE(closed.size(), 0);
    QCOMPARE(app->recordingState(), RecordingState::Recording);
    // After the second (timed) enumeration the track closes.
    QVERIFY(closed.wait(4000));
    QCOMPARE(closed.size(), 1);
    QCOMPARE(closed.at(0).at(0).toString(), headset.name);
    QCOMPARE(app->recordingState(), RecordingState::Recording);
    QCOMPARE(app->recordingDeviceNames(), (QStringList{mic.name, mon.name}));
    QCOMPARE(app->disconnectedRecordingDeviceNames(), QStringList{headset.name});
    QCOMPARE(engine->closeCalls, 1);

    // Further device changes (and a temporarily empty enumeration) do not stop it either.
    app->refreshDevices();
    app->devices()->setDeviceListOverride(QVector<AudioDeviceInfo>{});
    app->refreshDevices();
    app->devices()->setDeviceListOverride(QVector<AudioDeviceInfo>{mic, mon});
    app->refreshDevices();
    QTest::qWait(1500);
    QCOMPARE(app->recordingState(), RecordingState::Recording);
    QCOMPARE(closed.size(), 1);

    // Plug back in: the device is in the enumeration again. One sighting is not enough
    // (stability); after the second (timed) enumeration we record it again by ourselves: a new
    // track, with an offset.
    QSignalSpy added(app.get(), &app::AppController::recordingTrackAdded);
    QSignalSpy returned(app.get(), &app::AppController::recordingDeviceReturned);
    engine->revive(headset.name);
    app->devices()->setDeviceListOverride(QVector<AudioDeviceInfo>{mic, headset, mon});
    app->refreshDevices();
    QCOMPARE(added.size(), 0);
    QVERIFY(added.wait(4000));
    QCOMPARE(added.at(0).at(0).toString(), headset.name);
    QCOMPARE(returned.size(), 1);
    QVERIFY(app->disconnectedRecordingDeviceNames().isEmpty());
    QVERIFY(app->recordingDeviceNames().contains(headset.name));
    QCOMPARE(app->recordingState(), RecordingState::Recording);
    QTest::qWait(500);

    app->stopRecording();
    QVERIFY(finished.wait(20000));
    feeder.stop();
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));

    const Meeting m = finished.at(0).at(0).value<Meeting>();
    QCOMPARE(m.tracks.size(), 4);
    for (int i = 0; i < 3; ++i) QCOMPARE(m.tracks.at(i).startOffsetMs, qint64(0));
    // The returned device's new segment: the same device name, a later start, its own file.
    QCOMPARE(m.tracks.at(3).deviceName, headset.name);
    QVERIFY2(m.tracks.at(3).startOffsetMs > 1000, qPrintable(QString::number(m.tracks.at(3).startOffsetMs)));
    QVERIFY(m.tracks.at(3).file != m.tracks.at(1).file);
    const qint64 dMic = probeMs(QDir(m.folder).filePath(m.tracks.at(0).file));
    const qint64 dHs = probeMs(QDir(m.folder).filePath(m.tracks.at(1).file));
    QVERIFY2(qAbs(dMic - m.durationMs) < 300, qPrintable(QStringLiteral("%1 vs %2").arg(dMic).arg(m.durationMs)));
    QVERIFY2(dHs > 400 && dHs < 900, qPrintable(QString::number(dHs)));   // until the unplug (~0.6 s)

    app.reset();
    qunsetenv("TANARA_HOME");
}

QTEST_GUILESS_MAIN(DeviceUnplugTest)
#include "test_device_unplug.moc"
