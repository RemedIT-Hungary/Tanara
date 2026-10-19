// Tanara: RecordingSession with a fake capture engine (no audio device): a track switched on /
// off and on during a recording gets its opening time as the offset (startOffsetMs), its file is
// only the actually recorded segment (no leading silence), and the gaps of a silent loopback,
// where gap filling is on, are filled with silence by the recorder. The encoder is the real
// ffmpeg (without it QSKIP). The pure functions of "tracktiming" are here too.
#include <QtTest>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QTimer>

#include <array>
#include <cmath>
#include <memory>

#include "tanara/media/FfmpegMediaTool.h"
#include "tanara/recording/audio/AudioEngine.h"
#include "tanara/recording/audio/RecordingSession.h"
#include "tanara/recording/audio/SystemAudioSource.h"
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

AudioDeviceInfo dev(const QString& name, TrackKind kind)
{
    AudioDeviceInfo d;
    d.id = name;
    d.name = name;
    d.kind = kind;
    return d;
}

} // namespace

class RecordingSessionTest : public QObject {
    Q_OBJECT
private slots:
    void fileRangeMapping();
    void framesOwedHelper();
    void lateAndToggledTracksGetOffsets();
    void silentLoopbackGapsAreFilled();
    void unpluggedDeviceClosesOnlyItsTrack();
    void stallRule();
    void stalledTrackWarnsAndResumes();
    void detachedTrackStallsOnce();
};

void RecordingSessionTest::fileRangeMapping()
{
    using tanara::tracktiming::meetingToFileRange;
    // A track starting together: unchanged.
    auto r = meetingToFileRange(0, 1000, 4000);
    QVERIFY(r.valid());
    QCOMPARE(r.startMs, qint64(1000)); QCOMPARE(r.endMs, qint64(4000));
    // A track starting 57.2 s later: the meeting's 60-63 s is the file's 2.8-5.8 s.
    r = meetingToFileRange(57200, 60000, 63000);
    QCOMPARE(r.startMs, qint64(2800)); QCOMPARE(r.endMs, qint64(5800));
    // A range before the track's start: empty; an overhang is clipped.
    QVERIFY(!meetingToFileRange(57200, 10000, 13000).valid());
    r = meetingToFileRange(57200, 56000, 59000);
    QCOMPARE(r.startMs, qint64(0)); QCOMPARE(r.endMs, qint64(1800));
    // After the file's end: empty; an overhang is clipped.
    QVERIFY(!meetingToFileRange(1000, 20000, 23000, 10000).valid());
    r = meetingToFileRange(1000, 10000, 13000, 10000);
    QCOMPARE(r.startMs, qint64(9000)); QCOMPARE(r.endMs, qint64(10000));
    // Empty / reversed request.
    QVERIFY(!meetingToFileRange(0, 5000, 5000).valid());
    Track t; t.startOffsetMs = 2000;
    QCOMPARE(tanara::tracktiming::fileRange(t, 2500, 3000).startMs, qint64(500));
}

void RecordingSessionTest::framesOwedHelper()
{
    using namespace tracktiming;
    QCOMPARE(recording::tracktiming::expectedFrames(1000, 48000), qint64(48000));
    QCOMPARE(recording::tracktiming::expectedFrames(-5, 48000), qint64(0));
    QCOMPARE(recording::tracktiming::framesOwed(48000, 48000, 960), qint64(0));
    QCOMPARE(recording::tracktiming::framesOwed(48000, 47500, 960), qint64(0));        // within the tolerance: nothing
    QCOMPARE(recording::tracktiming::framesOwed(48000, 40000, 960), qint64(8000));     // gap: all to be filled
    QCOMPARE(recording::tracktiming::framesOwed(48000, 50000, 960), qint64(0));        // a device running ahead: never negative
    // Filling only if the device has been silent since kGapIdleMs.
    QCOMPARE(recording::tracktiming::silenceToInsert(2000, recording::tracktiming::kGapIdleMs - 1, 0, 48000), qint64(0));
    QCOMPARE(recording::tracktiming::silenceToInsert(2000, recording::tracktiming::kGapIdleMs, 0, 48000), qint64(96000));
    QCOMPARE(recording::tracktiming::silenceToInsert(2000, 500, 96000 - 100, 48000), qint64(0));   // a 2 ms gap < tolerance
    // Loopback fills on every platform (because of the interrupted monitor stream), the microphone does not.
    QVERIFY(recording::tracktiming::fillsCaptureGaps(TrackKind::Loopback));
    QVERIFY(!recording::tracktiming::fillsCaptureGaps(TrackKind::Mic));
}

void RecordingSessionTest::lateAndToggledTracksGetOffsets()
{
    if (!haveFfmpeg()) QSKIP("ffmpeg/ffprobe nincs a PATH-on");
    QTemporaryDir dir;
    FakeEngine* engine = nullptr;
    std::shared_ptr<std::atomic<bool>> alive;
    recording::RecordingSession rec(media::FfmpegMediaTool::fromSettings({}), dir.path(), QStringLiteral("Szakaszok"));
    rec.setEngineFactory([&engine, &alive] { auto e = std::make_unique<FakeEngine>(); engine = e.get();
        alive = engine->alive; return e; });
    QSignalSpy finished(&rec, &recording::RecordingSession::finished);
    QTimer feeder;
    feeder.setInterval(5);
    QObject::connect(&feeder, &QTimer::timeout, [&engine, &alive] { if (engine && alive && *alive) engine->feed(); });

    QElapsedTimer clock;
    clock.start();
    rec.start({dev(QStringLiteral("Mic A"), TrackKind::Mic)});
    QCOMPARE(rec.state(), RecordingState::Recording);
    feeder.start();
    const AudioDeviceInfo sys = dev(QStringLiteral("Monitor of Speakers"), TrackKind::Loopback);

    QTest::qWait(1000);
    const qint64 addAt1 = clock.elapsed();
    QVERIFY(rec.addDevice(sys));                 // t ≈ 1 s
    QTest::qWait(500);
    rec.closeTrack(sys.name);                    // t ≈ 1,5 s
    QVERIFY(!rec.trackOpen(1));
    QTest::qWait(500);
    const qint64 addAt2 = clock.elapsed();
    QVERIFY(rec.addDevice(sys));                 // t about 2 s: the second segment
    QVERIFY(!rec.addDevice(sys));                // the open track already has it
    QTest::qWait(500);
    rec.stop();
    QVERIFY(finished.wait(20000));
    feeder.stop();

    const Meeting m = finished.at(0).at(0).value<Meeting>();
    QCOMPARE(m.tracks.size(), 3);
    const Track& mic = m.tracks.at(0);
    const Track& s1 = m.tracks.at(1);
    const Track& s2 = m.tracks.at(2);
    QCOMPARE(mic.startOffsetMs, qint64(0));
    QVERIFY2(qAbs(s1.startOffsetMs - addAt1) < 150, qPrintable(QString::number(s1.startOffsetMs)));
    QVERIFY2(qAbs(s2.startOffsetMs - addAt2) < 150, qPrintable(QString::number(s2.startOffsetMs)));
    QCOMPARE(s1.file, QStringLiteral("track_monitor-of-speakers.ogg"));
    QCOMPARE(s2.file, QStringLiteral("track_monitor-of-speakers-2.ogg"));
    QCOMPARE(s1.deviceName, s2.deviceName);
    QVERIFY(m.durationMs >= 2400);

    // The files contain the actually recorded segment: no leading silence.
    const qint64 dMic = probeMs(QDir(m.folder).filePath(mic.file));
    const qint64 d1 = probeMs(QDir(m.folder).filePath(s1.file));
    const qint64 d2 = probeMs(QDir(m.folder).filePath(s2.file));
    QVERIFY2(qAbs(dMic - m.durationMs) < 250, qPrintable(QStringLiteral("%1 vs %2").arg(dMic).arg(m.durationMs)));
    QVERIFY2(d1 > 300 && d1 < 800, qPrintable(QString::number(d1)));      // about 0.5 s, not about 1.5 s
    QVERIFY2(d2 > 300 && d2 < 800, qPrintable(QString::number(d2)));
    // Offset + length stay within the end of the meeting.
    QVERIFY(s2.startOffsetMs + d2 <= m.durationMs + 250);
}

void RecordingSessionTest::silentLoopbackGapsAreFilled()
{
    if (!haveFfmpeg()) QSKIP("ffmpeg/ffprobe nincs a PATH-on");
    QTemporaryDir dir;
    FakeEngine* engine = nullptr;
    std::shared_ptr<std::atomic<bool>> alive;
    recording::RecordingSession rec(media::FfmpegMediaTool::fromSettings({}), dir.path(), QStringLiteral("Néma loopback"));
    rec.setFillCaptureGaps(true);   // explicitly on (it is the default for loopback anyway)
    rec.setEngineFactory([&engine, &alive] {
        auto e = std::make_unique<FakeEngine>();
        e->silentNames << QStringLiteral("Speakers (loopback)");
        engine = e.get();
        alive = engine->alive;
        return e;
    });
    QSignalSpy finished(&rec, &recording::RecordingSession::finished);
    QTimer feeder;
    feeder.setInterval(5);
    QObject::connect(&feeder, &QTimer::timeout, [&engine, &alive] { if (engine && alive && *alive) engine->feed(); });

    rec.start({dev(QStringLiteral("Mic A"), TrackKind::Mic),
               dev(QStringLiteral("Speakers (loopback)"), TrackKind::Loopback)});
    feeder.start();
    QTest::qWait(700);
    // Halfway the loopback "speaks" for a short time, then is silent again.
    engine->silentNames.clear();
    QTest::qWait(300);
    engine->silentNames << QStringLiteral("Speakers (loopback)");
    QTest::qWait(700);
    rec.stop();
    QVERIFY(finished.wait(20000));
    feeder.stop();

    const Meeting m = finished.at(0).at(0).value<Meeting>();
    QCOMPARE(m.tracks.size(), 2);
    const qint64 dMic = probeMs(QDir(m.folder).filePath(m.tracks.at(0).file));
    const qint64 dLb = probeMs(QDir(m.folder).filePath(m.tracks.at(1).file));
    // The silent stretches are filled with silence: the loopback's file is as long as the mic's.
    QVERIFY2(qAbs(dLb - dMic) < 200, qPrintable(QStringLiteral("loopback %1 ms, mic %2 ms").arg(dLb).arg(dMic)));
}

// A device unplugged during a recording: only its track closes (its file lasts until the
// close), the other tracks go on without interruption, and no track's offset changes.
void RecordingSessionTest::unpluggedDeviceClosesOnlyItsTrack()
{
    if (!haveFfmpeg()) QSKIP("ffmpeg/ffprobe nincs a PATH-on");
    QTemporaryDir dir;
    FakeEngine* engine = nullptr;
    std::shared_ptr<std::atomic<bool>> alive;
    recording::RecordingSession rec(media::FfmpegMediaTool::fromSettings({}), dir.path(), QStringLiteral("Kihúzás"));
    rec.setEngineFactory([&engine, &alive] { auto e = std::make_unique<FakeEngine>(); engine = e.get();
        alive = engine->alive; return e; });
    QSignalSpy finished(&rec, &recording::RecordingSession::finished);
    QSignalSpy closed(&rec, &recording::RecordingSession::trackClosed);
    QSignalSpy elapsed(&rec, &recording::RecordingSession::elapsedChanged);
    QTimer feeder;
    feeder.setInterval(5);
    QObject::connect(&feeder, &QTimer::timeout, [&engine, &alive] { if (engine && alive && *alive) engine->feed(); });

    const QString headset = QStringLiteral("Sennheiser headset");
    rec.start({dev(QStringLiteral("Mic A"), TrackKind::Mic), dev(headset, TrackKind::Mic),
               dev(QStringLiteral("Monitor of Speakers"), TrackKind::Loopback)});
    QCOMPARE(rec.state(), RecordingState::Recording);
    feeder.start();

    QTest::qWait(800);
    engine->vanish(headset);                     // unplugged: no more data comes
    QTest::qWait(300);
    rec.closeTrack(headset);                     // the hot-plug watcher closes it
    QCOMPARE(closed.size(), 1);
    QCOMPARE(closed.at(0).at(0).toInt(), 1);
    QCOMPARE(closed.at(0).at(1).toString(), headset);
    QCOMPARE(engine->closeCalls, 1);
    QVERIFY(!rec.trackOpen(1));
    QVERIFY(rec.trackOpen(0));
    QVERIFY(rec.trackOpen(2));
    rec.closeTrack(headset);                     // repeated signal: no second close
    QCOMPARE(closed.size(), 1);

    elapsed.clear();
    QTest::qWait(900);
    QCOMPARE(rec.state(), RecordingState::Recording);   // the recording goes on
    QVERIFY(elapsed.size() > 5);                        // the drain thread is working
    rec.stop();
    QVERIFY(finished.wait(20000));
    feeder.stop();

    const Meeting m = finished.at(0).at(0).value<Meeting>();
    QCOMPARE(m.tracks.size(), 3);
    for (const Track& t : m.tracks) QCOMPARE(t.startOffsetMs, qint64(0));
    const qint64 dMic = probeMs(QDir(m.folder).filePath(m.tracks.at(0).file));
    const qint64 dHs  = probeMs(QDir(m.folder).filePath(m.tracks.at(1).file));
    const qint64 dMon = probeMs(QDir(m.folder).filePath(m.tracks.at(2).file));
    QVERIFY2(qAbs(dMic - m.durationMs) < 250, qPrintable(QStringLiteral("%1 vs %2").arg(dMic).arg(m.durationMs)));
    QVERIFY2(qAbs(dMon - m.durationMs) < 250, qPrintable(QStringLiteral("%1 vs %2").arg(dMon).arg(m.durationMs)));
    // The unplugged device's track carries the audio up to the unplug, as a closed, readable file.
    QVERIFY2(dHs > 600 && dHs < 1100, qPrintable(QString::number(dHs)));
}

// "No data" vs "silence with data": the 5 s threshold and the per-kind watch.
void RecordingSessionTest::stallRule()
{
    using namespace recording::tracktiming;
    QCOMPARE(kStallWarnMs, qint64(5000));
    QVERIFY(!isStalled(kStallWarnMs - 1));
    QVERIFY(isStalled(kStallWarnMs));
    QVERIFY(isStalled(250, 200));
    // Microphones and line-ins always deliver samples (silence included), so they are watched everywhere.
    QVERIFY(!recording::systemAudioSource().deliversNothingWhenIdle(TrackKind::Mic));
    QVERIFY(!recording::systemAudioSource().deliversNothingWhenIdle(TrackKind::Other));
}

// A microphone stops delivering data (the stream died, e.g. after sleep): trackStalled after the
// threshold, with the time of its last data; data again -> trackResumed. The threshold is lowered
// for the test (the same rule, 5 s in production).
void RecordingSessionTest::stalledTrackWarnsAndResumes()
{
    if (!haveFfmpeg()) QSKIP("ffmpeg/ffprobe nincs a PATH-on");
    QTemporaryDir dir;
    FakeEngine* engine = nullptr;
    std::shared_ptr<std::atomic<bool>> alive;
    recording::RecordingSession rec(media::FfmpegMediaTool::fromSettings({}), dir.path(), QStringLiteral("Akadás"));
    rec.setStallWarnMs(400);
    rec.setEngineFactory([&engine, &alive] { auto e = std::make_unique<FakeEngine>(); engine = e.get();
        alive = engine->alive; return e; });
    QSignalSpy stalled(&rec, &recording::RecordingSession::trackStalled);
    QSignalSpy resumed(&rec, &recording::RecordingSession::trackResumed);
    QSignalSpy finished(&rec, &recording::RecordingSession::finished);
    QTimer feeder;
    feeder.setInterval(5);
    QObject::connect(&feeder, &QTimer::timeout, [&engine, &alive] { if (engine && alive && *alive) engine->feed(); });

    const QString mic = QStringLiteral("Mic A");
    QElapsedTimer clock;
    clock.start();
    rec.start({dev(mic, TrackKind::Mic), dev(QStringLiteral("Monitor of Speakers"), TrackKind::Loopback)});
    QCOMPARE(rec.state(), RecordingState::Recording);
    feeder.start();
    QTest::qWait(500);
    QCOMPARE(stalled.size(), 0);                 // both deliver: no warning

    const qint64 stopAt = clock.elapsed();
    engine->vanish(mic);                         // no more samples from the microphone
    QTest::qWait(250);
    QCOMPARE(stalled.size(), 0);                 // below the threshold
    QVERIFY(stalled.wait(2000));
    const qint64 warnedAt = clock.elapsed();
    QCOMPARE(stalled.size(), 1);
    QCOMPARE(stalled.at(0).at(0).toString(), mic);
    const qint64 since = stalled.at(0).at(1).toLongLong();
    QVERIFY2(qAbs(since - stopAt) < 150, qPrintable(QStringLiteral("%1 vs %2").arg(since).arg(stopAt)));
    QVERIFY2(warnedAt - stopAt >= 400 - 50, qPrintable(QString::number(warnedAt - stopAt)));
    QCOMPARE(rec.stalledTracks().value(mic, -1), since);
    QTest::qWait(300);
    QCOMPARE(stalled.size(), 1);                 // once per stall, not every tick

    engine->revive(mic);
    QVERIFY(resumed.wait(2000));
    QCOMPARE(resumed.at(0).at(0).toString(), mic);
    QVERIFY(rec.stalledTracks().isEmpty());
    QCOMPARE(stalled.size(), 1);

    rec.stop();
    QVERIFY(finished.wait(20000));
    feeder.stop();
}

// A detached device's closed track is reported stalled exactly once (it will not resume); a track the
// user switched off is not a stall.
void RecordingSessionTest::detachedTrackStallsOnce()
{
    if (!haveFfmpeg()) QSKIP("ffmpeg/ffprobe nincs a PATH-on");
    QTemporaryDir dir;
    FakeEngine* engine = nullptr;
    std::shared_ptr<std::atomic<bool>> alive;
    recording::RecordingSession rec(media::FfmpegMediaTool::fromSettings({}), dir.path(), QStringLiteral("Leválasztás"));
    rec.setStallWarnMs(60000);                   // the tick alone never warns here
    rec.setEngineFactory([&engine, &alive] { auto e = std::make_unique<FakeEngine>(); engine = e.get();
        alive = engine->alive; return e; });
    QSignalSpy stalled(&rec, &recording::RecordingSession::trackStalled);
    QSignalSpy resumed(&rec, &recording::RecordingSession::trackResumed);
    QSignalSpy finished(&rec, &recording::RecordingSession::finished);
    QTimer feeder;
    feeder.setInterval(5);
    QObject::connect(&feeder, &QTimer::timeout, [&engine, &alive] { if (engine && alive && *alive) engine->feed(); });

    const QString headset = QStringLiteral("Sennheiser headset");
    const QString usb = QStringLiteral("USB mic");
    rec.start({dev(QStringLiteral("Mic A"), TrackKind::Mic), dev(headset, TrackKind::Mic), dev(usb, TrackKind::Mic)});
    feeder.start();
    QTest::qWait(300);
    engine->vanish(headset);
    rec.closeTrack(headset);                     // detached (the default)
    QVERIFY(stalled.wait(2000));
    QCOMPARE(stalled.at(0).at(0).toString(), headset);
    rec.closeTrack(usb, /*detached*/ false);     // switched off by the user
    QTest::qWait(400);
    QCOMPARE(stalled.size(), 1);
    QCOMPARE(resumed.size(), 0);
    QCOMPARE(rec.stalledTracks().keys(), QStringList{headset});

    rec.stop();
    QVERIFY(finished.wait(20000));
    feeder.stop();
    QCOMPARE(stalled.size(), 1);
    QVERIFY(rec.stalledTracks().isEmpty());
}

QTEST_GUILESS_MAIN(RecordingSessionTest)
#include "test_recording_session.moc"
