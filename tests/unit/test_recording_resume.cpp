// Tanara: resuming a recording after the machine slept or a device was lost (RecordingSession with
// a fake capture engine, an injected wall clock and an injected device list; no audio device, no
// real data). A wall-clock jump is detected as a sleep and recorded as a gap; every track is cut at
// the sleep and continues in a new segment (track_<slug>-2.ogg) whose offset carries the gap; a
// silent loopback's gap fill stops at the sleep (the gap is metadata, not silence); a detached or
// backend-stopped device is re-opened (by id, then by name) and continues the chain; re-opening
// gives up after the window. The encoder is the real ffmpeg (without it QSKIP).
#include <QtTest>
#include <QDateTime>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <atomic>
#include <memory>

#include "tanara/library/tracks/TrackCatalog.h"
#include "tanara/media/FfmpegMediaTool.h"
#include "tanara/recording/audio/AudioEngine.h"
#include "tanara/recording/audio/RecordingSession.h"
#include "tanara/recording/audio/TrackTiming.h"

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

AudioDeviceInfo dev(const QString& name, TrackKind kind, const QString& id = QString())
{
    AudioDeviceInfo d;
    d.id = id.isEmpty() ? name : id;
    d.name = name;
    d.kind = kind;
    return d;
}

const Track* byId(const Meeting& m, const QString& id)
{
    for (const Track& t : m.tracks)
        if (t.id == id) return &t;
    return nullptr;
}

// A session on the fake engine, a feeder and a wall clock that can jump ahead (a suspend).
struct Rig {
    QTemporaryDir dir;
    FakeEngine* engine = nullptr;
    std::shared_ptr<std::atomic<bool>> alive;
    std::shared_ptr<std::atomic<qint64>> skew = std::make_shared<std::atomic<qint64>>(0);
    QVector<AudioDeviceInfo> present;   // what the device lister reports
    int listed = 0;
    std::unique_ptr<recording::RecordingSession> rec;
    QTimer feeder;

    explicit Rig(bool withLister = true, QStringList silent = {})
    {
        rec = std::make_unique<recording::RecordingSession>(media::FfmpegMediaTool::fromSettings({}),
                                                            dir.path(), QStringLiteral("Folytatás"));
        rec->setEngineFactory([this, silent] {
            auto e = std::make_unique<FakeEngine>();
            e->silentNames = silent;
            engine = e.get();
            alive = engine->alive;
            return e;
        });
        auto sk = skew;
        rec->setClockSources({}, [sk] { return QDateTime::currentMSecsSinceEpoch() + sk->load(); });
        if (withLister)
            rec->setDeviceLister([this] { ++listed; return present; });
        rec->setReopenTiming(100, 5000);
        feeder.setInterval(5);
        QObject::connect(&feeder, &QTimer::timeout, [this] { if (engine && alive && *alive) engine->feed(); });
    }
    ~Rig() { feeder.stop(); }

    Meeting stopAndWait()
    {
        QSignalSpy finished(rec.get(), &recording::RecordingSession::finished);
        rec->stop();
        if (!finished.wait(20000)) return {};
        feeder.stop();
        return finished.at(0).at(0).value<Meeting>();
    }
};

} // namespace

class RecordingResumeTest : public QObject {
    Q_OBJECT
private slots:
    void sleepJumpRule();
    void sleepCutsTracksIntoSegments();
    void sleepWithoutListerKeepsTracks();
    void detachedDeviceReopensById();
    void backendStoppedDeviceIsReopened();
    void reopenGivesUpAfterWindow();
};

void RecordingResumeTest::sleepJumpRule()
{
    using namespace recording::tracktiming;
    QCOMPARE(sleepJumpMs(20, 20), qint64(0));                  // a normal tick
    QCOMPARE(sleepJumpMs(1900, 20), qint64(0));                // under the threshold
    QCOMPARE(sleepJumpMs(60020, 20), qint64(60000));           // suspended for a minute
    QCOMPARE(sleepJumpMs(20, 60020), qint64(0));               // the clock went back: not a sleep
    QCOMPARE(kSleepJumpMs, qint64(2000));
    QCOMPARE(kReopenPollMs, qint64(1000));
    QCOMPARE(kReopenWindowMs, qint64(600000));
}

// The wall clock jumps a minute ahead: a sleep. Both tracks (a microphone and a silent loopback whose
// gaps are filled) are cut at the sleep and continue in new segments placed after the gap; the old
// loopback file is NOT padded with a minute of silence.
void RecordingResumeTest::sleepCutsTracksIntoSegments()
{
    if (!haveFfmpeg()) QSKIP("ffmpeg/ffprobe nincs a PATH-on");
    const AudioDeviceInfo mic = dev(QStringLiteral("Mic A"), TrackKind::Mic);
    const AudioDeviceInfo sys = dev(QStringLiteral("Monitor of Speakers"), TrackKind::Loopback);
    Rig rig(true, {sys.name});
    rig.rec->setFillCaptureGaps(true);
    rig.present = {mic, sys};
    QSignalSpy slept(rig.rec.get(), &recording::RecordingSession::systemSlept);
    QSignalSpy reopening(rig.rec.get(), &recording::RecordingSession::reopenStarted);
    QSignalSpy reopened(rig.rec.get(), &recording::RecordingSession::trackReopened);
    QSignalSpy closed(rig.rec.get(), &recording::RecordingSession::trackClosed);
    QSignalSpy stalled(rig.rec.get(), &recording::RecordingSession::trackStalled);

    rig.rec->start({mic, sys});
    QCOMPARE(rig.rec->state(), RecordingState::Recording);
    rig.feeder.start();
    QTest::qWait(600);

    const qint64 wallBefore = QDateTime::currentMSecsSinceEpoch();
    rig.skew->store(60000);                       // the machine "slept" for a minute
    QVERIFY(slept.wait(2000));
    const qint64 fromWall = slept.at(0).at(0).toLongLong(), toWall = slept.at(0).at(1).toLongLong();
    QVERIFY2(qAbs(toWall - fromWall - 60000) < 200, qPrintable(QString::number(toWall - fromWall)));
    QVERIFY(qAbs(fromWall - wallBefore) < 200);
    QCOMPARE(rig.rec->gaps().size(), 1);
    const RecordingGap g = rig.rec->gaps().at(0);
    QCOMPARE(g.reason, QStringLiteral("sleep"));
    QVERIFY(g.trackId.isEmpty());
    QVERIFY2(qAbs(g.toMs - g.fromMs - 60000) < 200, qPrintable(QString::number(g.toMs - g.fromMs)));
    QVERIFY2(g.fromMs > 500 && g.fromMs < 900, qPrintable(QString::number(g.fromMs)));

    // Both tracks were cut and are re-opened (reported before their close, for the UI).
    QCOMPARE(reopening.size(), 2);
    QCOMPARE(reopening.at(0).at(1).toString(), QStringLiteral("sleep"));
    QCOMPARE(closed.size(), 2);
    QVERIFY(reopened.wait(2000));
    if (reopened.size() < 2) QVERIFY(reopened.wait(2000));
    QCOMPARE(reopened.size(), 2);
    QVERIFY(rig.rec->reopeningDevices().isEmpty());
    QVERIFY(stalled.isEmpty());                   // a sleep is not a stall
    QTest::qWait(600);
    const Meeting m = rig.stopAndWait();
    QCOMPARE(m.tracks.size(), 4);

    const Track* mic1 = byId(m, QStringLiteral("mic-a"));
    const Track* mic2 = byId(m, QStringLiteral("mic-a-2"));
    const Track* sys1 = byId(m, QStringLiteral("monitor-of-speakers"));
    const Track* sys2 = byId(m, QStringLiteral("monitor-of-speakers-2"));
    QVERIFY(mic1 && mic2 && sys1 && sys2);
    QCOMPARE(mic2->file, QStringLiteral("track_mic-a-2.ogg"));
    QCOMPARE(mic2->continuesTrackId, mic1->id);
    QCOMPARE(sys2->continuesTrackId, sys1->id);
    QVERIFY(mic1->continuesTrackId.isEmpty());
    QCOMPARE(mic2->speakerLabel, mic1->speakerLabel);   // one logical track, one label
    // The gap is in the offset: the new segments start after the minute.
    QVERIFY2(mic2->startOffsetMs >= g.toMs && mic2->startOffsetMs < g.toMs + 1000,
             qPrintable(QString::number(mic2->startOffsetMs)));
    QVERIFY(sys2->startOffsetMs >= g.toMs);
    QVERIFY(m.durationMs > 61000 && m.durationMs < 63000);
    QCOMPARE(m.gaps.size(), 1);

    // The first segments end at the sleep: the loopback's gap fill stopped at the boundary.
    const qint64 dMic1 = probeMs(QDir(m.folder).filePath(mic1->file));
    const qint64 dSys1 = probeMs(QDir(m.folder).filePath(sys1->file));
    const qint64 dSys2 = probeMs(QDir(m.folder).filePath(sys2->file));
    QVERIFY2(dMic1 > 400 && dMic1 < 1000, qPrintable(QString::number(dMic1)));
    QVERIFY2(dSys1 > 400 && dSys1 < 1000, qPrintable(QStringLiteral("loopback before the sleep: %1 ms").arg(dSys1)));
    QVERIFY2(dSys2 > 300 && dSys2 < 1200, qPrintable(QString::number(dSys2)));
    QVERIFY(sys2->startOffsetMs + dSys2 <= m.durationMs + 300);

    // Downstream: the chain is one logical track per device.
    const QVector<library::tracknames::SegmentInfo> seg = library::tracknames::segments(m.tracks);
    for (int i = 0; i < m.tracks.size(); ++i)
        QCOMPARE(seg.at(i).count, 2);
}

// Without a device lister the session neither cuts nor re-opens: the sleep is only recorded.
void RecordingResumeTest::sleepWithoutListerKeepsTracks()
{
    if (!haveFfmpeg()) QSKIP("ffmpeg/ffprobe nincs a PATH-on");
    Rig rig(false);
    QSignalSpy slept(rig.rec.get(), &recording::RecordingSession::systemSlept);
    QSignalSpy closed(rig.rec.get(), &recording::RecordingSession::trackClosed);
    rig.rec->start({dev(QStringLiteral("Mic A"), TrackKind::Mic)});
    rig.feeder.start();
    QTest::qWait(300);
    rig.skew->store(30000);
    QVERIFY(slept.wait(2000));
    QTest::qWait(300);
    QVERIFY(closed.isEmpty());
    QVERIFY(rig.rec->trackOpen(0));
    const Meeting m = rig.stopAndWait();
    QCOMPARE(m.tracks.size(), 1);
    QCOMPARE(m.gaps.size(), 1);
    QCOMPARE(m.gaps.at(0).reason, QStringLiteral("sleep"));
}

// A detached device is looked for every poll; it comes back under another name with the same id:
// a new segment that continues the lost one, and a device-lost gap naming it.
void RecordingResumeTest::detachedDeviceReopensById()
{
    if (!haveFfmpeg()) QSKIP("ffmpeg/ffprobe nincs a PATH-on");
    Rig rig;
    const AudioDeviceInfo mic = dev(QStringLiteral("Mic A"), TrackKind::Mic);
    const AudioDeviceInfo headset = dev(QStringLiteral("Sennheiser headset"), TrackKind::Mic, QStringLiteral("usb-1234"));
    const AudioDeviceInfo headsetBack = dev(QStringLiteral("Sennheiser headset (2)"), TrackKind::Mic, QStringLiteral("usb-1234"));
    rig.present = {mic, headset};
    QSignalSpy reopening(rig.rec.get(), &recording::RecordingSession::reopenStarted);
    QSignalSpy reopened(rig.rec.get(), &recording::RecordingSession::trackReopened);
    QSignalSpy resumed(rig.rec.get(), &recording::RecordingSession::trackResumed);

    rig.rec->start({mic, headset});
    rig.feeder.start();
    QTest::qWait(400);
    rig.engine->vanish(headset.name);
    rig.present = {mic};
    rig.rec->closeTrack(headset.name);            // the hot-plug watch closes it (detached)
    QCOMPARE(reopening.size(), 1);
    QCOMPARE(reopening.at(0).at(0).toString(), headset.name);
    QCOMPARE(reopening.at(0).at(1).toString(), QStringLiteral("device-lost"));
    QCOMPARE(rig.rec->reopeningDevices(), QStringList{headset.name});
    QTest::qWait(400);                            // polls: not there yet
    QCOMPARE(reopened.size(), 0);
    QVERIFY(rig.listed >= 2);

    rig.present = {mic, headsetBack};
    QVERIFY(reopened.wait(2000));
    QCOMPARE(reopened.at(0).at(0).toString(), headset.name);
    QCOMPARE(reopened.at(0).at(1).toString(), headsetBack.name);
    QCOMPARE(reopened.at(0).at(2).toInt(), 2);
    QVERIFY(rig.rec->trackOpen(2));
    QTest::qWait(300);
    const Meeting m = rig.stopAndWait();
    QCOMPARE(m.tracks.size(), 3);
    const Track& lost = m.tracks.at(1);
    const Track& back = m.tracks.at(2);
    QCOMPARE(back.deviceName, headsetBack.name);
    QCOMPARE(back.continuesTrackId, lost.id);
    QVERIFY(back.startOffsetMs >= 800);
    QCOMPARE(m.gaps.size(), 1);
    const RecordingGap& g = m.gaps.at(0);
    QCOMPARE(g.reason, QStringLiteral("device-lost"));
    QCOMPARE(g.trackId, back.id);
    QCOMPARE(g.toMs, back.startOffsetMs);
    QVERIFY2(g.fromMs >= 350 && g.fromMs < 700, qPrintable(QString::number(g.fromMs)));
    QVERIFY(g.fromWall.isValid() && g.toWall > g.fromWall);

    // Another name, one logical track; the Overview finds the interruption before the segment.
    const QVector<library::tracknames::SegmentInfo> seg = library::tracknames::segments(m.tracks);
    QCOMPARE(seg.at(2).leader, 1);
    QCOMPARE(seg.at(2).segment, 2);
    QCOMPARE(seg.at(0).segment, 0);
    const std::optional<RecordingGap> before = library::tracknames::gapBefore(m, 2);
    QVERIFY(before.has_value());
    QCOMPARE(before->reason, QStringLiteral("device-lost"));
    QVERIFY(!library::tracknames::gapBefore(m, 1).has_value());
}

// The backend stopped a device's capture (WASAPI device invalidated, a killed stream) and it delivers
// nothing: the session closes it as detached and re-opens it.
void RecordingResumeTest::backendStoppedDeviceIsReopened()
{
    if (!haveFfmpeg()) QSKIP("ffmpeg/ffprobe nincs a PATH-on");
    Rig rig;
    rig.rec->setStallWarnMs(200);
    const AudioDeviceInfo mic = dev(QStringLiteral("Mic A"), TrackKind::Mic);
    const AudioDeviceInfo usb = dev(QStringLiteral("USB mic"), TrackKind::Mic);
    rig.present = {mic, usb};
    QSignalSpy reopening(rig.rec.get(), &recording::RecordingSession::reopenStarted);
    QSignalSpy reopened(rig.rec.get(), &recording::RecordingSession::trackReopened);
    QSignalSpy closed(rig.rec.get(), &recording::RecordingSession::trackClosed);
    rig.rec->start({mic, usb});
    rig.feeder.start();
    QTest::qWait(300);
    rig.engine->loseByBackend(usb.name);
    QVERIFY(reopening.wait(3000));
    QCOMPARE(reopening.at(0).at(0).toString(), usb.name);
    QCOMPARE(closed.size(), 1);
    QVERIFY(reopened.wait(2000));
    QTest::qWait(200);
    const Meeting m = rig.stopAndWait();
    QCOMPARE(m.tracks.size(), 3);
    QCOMPARE(m.tracks.at(2).continuesTrackId, m.tracks.at(1).id);
    QCOMPARE(m.tracks.at(0).startOffsetMs, qint64(0));
}

// The device never comes back: after the window the session gives up, the recording goes on.
void RecordingResumeTest::reopenGivesUpAfterWindow()
{
    if (!haveFfmpeg()) QSKIP("ffmpeg/ffprobe nincs a PATH-on");
    Rig rig;
    rig.rec->setReopenTiming(50, 400);
    const AudioDeviceInfo mic = dev(QStringLiteral("Mic A"), TrackKind::Mic);
    const AudioDeviceInfo usb = dev(QStringLiteral("USB mic"), TrackKind::Mic);
    rig.present = {mic, usb};
    QSignalSpy abandoned(rig.rec.get(), &recording::RecordingSession::reopenAbandoned);
    QSignalSpy reopened(rig.rec.get(), &recording::RecordingSession::trackReopened);
    rig.rec->start({mic, usb});
    rig.feeder.start();
    QTest::qWait(200);
    // Listed, but it cannot be opened (not ready after the wake-up), then gone for good.
    rig.engine->unavailableNames << usb.name;
    rig.rec->closeTrack(usb.name);
    QVERIFY(abandoned.wait(3000));
    QCOMPARE(abandoned.at(0).at(0).toString(), usb.name);
    QCOMPARE(abandoned.at(0).at(1).toString(), QStringLiteral("device-lost"));
    QVERIFY(reopened.isEmpty());
    QVERIFY(rig.rec->reopeningDevices().isEmpty());
    QCOMPARE(rig.rec->state(), RecordingState::Recording);
    const Meeting m = rig.stopAndWait();
    QCOMPARE(m.tracks.size(), 2);
    QVERIFY(m.gaps.isEmpty());
}

QTEST_GUILESS_MAIN(RecordingResumeTest)
#include "test_recording_resume.moc"
