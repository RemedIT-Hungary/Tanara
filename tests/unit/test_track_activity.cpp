// TrackActivity: the per-track energy time series: frame dB, noise floor, window energy,
// placing the segments in meeting time, the cache. With a fake decoder (synthetic PCM); one
// probe with the real ffmpeg (lavfi-generated audio), QSKIP if there is no ffmpeg.
#include <QtTest>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QProcess>

#include <cmath>

#include "tanara/processing/analysis/TrackActivity.h"

using namespace tanara;
using namespace tanara::processing::trackactivity;

namespace {

constexpr int kSr = processing::trackactivity::kSampleRate;

// A sine of ms length (amplitude 0..1) or silence (amp == 0).
QVector<qint16> tone(int ms, double amp, double hz = 440.0)
{
    QVector<qint16> v(ms * kSr / 1000);
    for (int i = 0; i < v.size(); ++i)
        v[i] = qint16(std::lround(amp * 32767.0 * std::sin(2.0 * M_PI * hz * i / kSr)));
    return v;
}

QVector<qint16> cat(std::initializer_list<QVector<qint16>> parts)
{
    QVector<qint16> out;
    for (const auto& p : parts) out += p;
    return out;
}

// Fake decoder: file name to PCM; it gives it in odd chunks (the frame boundary is independent of that).
PcmDecoder fakeDecoder(const QHash<QString, QVector<qint16>>& files, QStringList* calls = nullptr)
{
    return [files, calls](const QString& path, const PcmSink& sink, QString* error) -> bool {
        const QString name = QFileInfo(path).fileName();
        if (calls) *calls << name;
        if (!files.contains(name)) { if (error) *error = "nincs"; return false; }
        const QVector<qint16>& pcm = files.value(name);
        qsizetype at = 0, chunk = 997;
        while (at < pcm.size()) {
            const qsizetype n = std::min(chunk, pcm.size() - at);
            sink(pcm.constData() + at, n);
            at += n;
            chunk = chunk == 997 ? 3 : 997;
        }
        return true;
    };
}

Track mk(const QString& id, const QString& device, TrackKind kind, qint64 offset = 0, bool active = true)
{
    Track t;
    t.id = id;
    t.deviceName = device;
    t.kind = kind;
    t.active = active;
    t.startOffsetMs = offset;
    t.file = QStringLiteral("track_%1.wav").arg(id);
    return t;
}

void touch(const QString& path, const QByteArray& data = "x")
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

TrackActivity flat(QVector<float> db, qint64 origin = 0, int frameMs = 50)
{
    TrackActivity t;
    t.trackId = "t";
    t.kind = TrackKind::Mic;
    t.frameMs = frameMs;
    t.originMs = origin;
    t.dbAboveFloor = std::move(db);
    return t;
}

} // namespace

class TrackActivityTest : public QObject {
    Q_OBJECT
private slots:
    void frameDbLevels()
    {
        // The RMS of a 0.5-amplitude sine is 0.5/sqrt(2), about -9.03 dBFS; silence is clipped to kMinDb.
        const QVector<float> db = processing::trackactivity::frameDb(cat({tone(100, 0.5), tone(100, 0.0), tone(20, 0.5)}), 50);
        QCOMPARE(db.size(), 5);   // 2 + 2 + a short (20 ms) last frame
        QVERIFY(std::abs(db[0] - (-9.03f)) < 0.1f);
        QVERIFY(std::abs(db[1] - (-9.03f)) < 0.1f);
        QCOMPARE(db[2], processing::trackactivity::kMinDb);
        QCOMPARE(db[3], processing::trackactivity::kMinDb);
        QVERIFY(db[4] > -12.0f);
        QVERIFY(processing::trackactivity::frameDb({}, 50).isEmpty());
    }

    void floorIsLowPercentile()
    {
        QVector<float> db;
        for (int i = 0; i < 100; ++i) db << float(-60 + (i % 10 == 0 ? 0 : 40));   // 10% quiet, 90% loud
        db << std::numeric_limits<float>::quiet_NaN();
        QCOMPARE(processing::trackactivity::estimateFloor(db), -60.0f);
        QCOMPARE(processing::trackactivity::estimateFloor({}), processing::trackactivity::kMinDb);
    }

    void windowEnergyAveragesLinearPower()
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const TrackActivity t = flat({0.0f, 20.0f, nan, 10.0f}, 1000);
        // One frame: exactly its value.
        QCOMPARE(*windowEnergyDb(t, 1050, 1100), 20.0f);
        // The linear mean of two frames (0 and 20 dB): 10*log10((1+100)/2) about 17.03 dB, NOT 10.
        QVERIFY(std::abs(*windowEnergyDb(t, 1000, 1100) - 17.03f) < 0.01f);
        // A NaN frame is left out; a partial overlap counts too.
        QVERIFY(std::abs(*windowEnergyDb(t, 1120, 1170) - 10.0f) < 0.01f);
        QVERIFY(!windowEnergyDb(t, 1100, 1150));     // only the hole
        QVERIFY(!windowEnergyDb(t, 0, 1000));        // before the track
        QVERIFY(!windowEnergyDb(t, 1200, 2000));     // after the track
        QVERIFY(windowEnergyDb(t, 0, 1001));         // reaches into the first frame
        QVERIFY(!windowEnergyDb(t, 1100, 1100));     // empty window
    }

    void computeMergesSegmentsAndSkipsBadTracks()
    {
        QTemporaryDir dir;
        // "mic": two segments (the same device, 0 and 2000 ms offset), so one logical track, with a
        // hole between. "loop": started 500 ms later. "off": inactive. "gone": missing file. "bad": cannot be decoded.
        Meeting m;
        m.tracks = {mk("mic", "Mic A", TrackKind::Mic, 0), mk("loop", "Out", TrackKind::Loopback, 500),
                    mk("mic-2", "Mic A", TrackKind::Mic, 2000), mk("off", "Other", TrackKind::Mic, 0, false),
                    mk("gone", "Gone", TrackKind::Loopback), mk("bad", "Bad", TrackKind::Loopback)};
        for (const char* id : {"mic", "loop", "mic-2", "off", "bad"})
            touch(QDir(dir.path()).filePath(QStringLiteral("track_%1.wav").arg(id)));
        QHash<QString, QVector<qint16>> files;
        files["track_mic.wav"] = cat({tone(500, 0.0), tone(500, 0.5)});   // 1000 ms
        files["track_mic-2.wav"] = cat({tone(500, 0.5), tone(500, 0.0)});
        files["track_loop.wav"] = cat({tone(1000, 0.0), tone(1000, 0.2)});
        files["track_off.wav"] = tone(1000, 0.5);
        QStringList calls;
        const MeetingActivity a = processing::computeMeetingActivity(m, dir.path(), "ffmpeg", 50, fakeDecoder(files, &calls));

        QVERIFY(!calls.contains("track_off.wav"));
        QVERIFY(!calls.contains("track_gone.wav"));
        QVERIFY(calls.contains("track_bad.wav"));
        QCOMPARE(a.tracks.size(), 2);
        const TrackActivity& mic = a.tracks[0];
        QCOMPARE(mic.trackId, QStringLiteral("mic"));
        QCOMPARE(mic.kind, TrackKind::Mic);
        QCOMPARE(mic.originMs, qint64(0));
        QCOMPARE(mic.dbAboveFloor.size(), 60);              // 0..3000 ms
        QCOMPARE(mic.coveredFrames(), 40);
        QVERIFY(std::isnan(mic.dbAboveFloor[30]));          // 1500 ms: hole
        QCOMPARE(mic.dbAboveFloor[0], 0.0f);                // silence = the floor
        QVERIFY(mic.dbAboveFloor[15] > 70.0f);              // -9 dBFS above the -90 floor
        QVERIFY(mic.dbAboveFloor[45] > 70.0f);              // the start of the 2nd segment (2250 ms)
        QCOMPARE(mic.floorDb, processing::trackactivity::kMinDb);
        QVERIFY(!windowEnergyDb(mic, 1100, 1900));
        QVERIFY(*windowEnergyDb(mic, 2000, 2400) > 70.0f);

        const TrackActivity& loop = a.tracks[1];
        QCOMPARE(loop.originMs, qint64(500));
        QCOMPARE(loop.kind, TrackKind::Loopback);
        QVERIFY(!windowEnergyDb(loop, 0, 400));
        QVERIFY(*windowEnergyDb(loop, 500, 1400) < 0.01f);       // file time 0..900: silence
        QVERIFY(*windowEnergyDb(loop, 1600, 2400) > 60.0f);      // file time 1100..1900: audio
        QCOMPARE(a.fingerprint, processing::trackactivity::activityFingerprint(m, dir.path(), 50));
    }

    void cacheRoundTripAndFingerprint()
    {
        QTemporaryDir dir;
        Meeting m;
        m.tracks = {mk("mic", "Mic", TrackKind::Mic), mk("loop", "Out", TrackKind::Loopback, 250)};
        touch(QDir(dir.path()).filePath("track_mic.wav"));
        touch(QDir(dir.path()).filePath("track_loop.wav"));
        QHash<QString, QVector<qint16>> files{{"track_mic.wav", tone(300, 0.3)}, {"track_loop.wav", tone(300, 0.0)}};
        MeetingActivity a = processing::computeMeetingActivity(m, dir.path(), "ffmpeg", 50, fakeDecoder(files));
        a.tracks[0].dbAboveFloor[2] = std::numeric_limits<float>::quiet_NaN();
        QVERIFY(a.save(dir.path()));
        QVERIFY(QFileInfo::exists(MeetingActivity::filePath(dir.path())));

        const MeetingActivity b = MeetingActivity::load(dir.path(), a.fingerprint);
        QCOMPARE(b.tracks.size(), 2);
        QCOMPARE(b.tracks[1].originMs, qint64(250));
        QCOMPARE(b.tracks[1].kind, TrackKind::Loopback);
        QCOMPARE(b.tracks[0].floorDb, a.tracks[0].floorDb);
        QCOMPARE(b.tracks[0].dbAboveFloor.size(), a.tracks[0].dbAboveFloor.size());
        QVERIFY(std::isnan(b.tracks[0].dbAboveFloor[2]));
        QCOMPARE(b.tracks[0].dbAboveFloor[1], a.tracks[0].dbAboveFloor[1]);

        // Another fingerprint gives empty; a changed file / another frameMs / another offset gives another fingerprint.
        QVERIFY(MeetingActivity::load(dir.path(), "masik").isEmpty());
        const QString fp = processing::trackactivity::activityFingerprint(m, dir.path(), 50);
        QVERIFY(fp != processing::trackactivity::activityFingerprint(m, dir.path(), 40));
        Meeting shifted = m;
        shifted.tracks[1].startOffsetMs = 300;
        QVERIFY(fp != processing::trackactivity::activityFingerprint(shifted, dir.path(), 50));
        touch(QDir(dir.path()).filePath("track_mic.wav"), "longer content");
        QVERIFY(fp != processing::trackactivity::activityFingerprint(m, dir.path(), 50));

        // A corrupt file gives empty (recompute), not a crash.
        touch(MeetingActivity::filePath(dir.path()), "TACT garbage");
        QVERIFY(MeetingActivity::load(dir.path(), a.fingerprint).isEmpty());
    }

    void realFfmpegDecode()
    {
        if (QStandardPaths::findExecutable("ffmpeg").isEmpty()) QSKIP("nincs ffmpeg");
        QTemporaryDir dir;
        const QString file = QDir(dir.path()).filePath("track_mic.ogg");
        // 1 s of silence, 1 s of sine, 1 s of silence: 48 kHz Opus (like the recorder).
        QProcess ff;
        ff.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i",
                            "aevalsrc=if(between(t\\,1\\,2)\\,0.5*sin(2*PI*300*t)\\,0):s=48000:d=3",
                            "-c:a", "libopus", "-y", file});
        QVERIFY(ff.waitForFinished(30000));
        if (!QFileInfo::exists(file)) QSKIP("az ffmpeg nem tud Opus-t kódolni");
        Meeting m;
        m.tracks = {mk("mic", "Mic", TrackKind::Mic)};
        m.tracks[0].file = "track_mic.ogg";
        const MeetingActivity a = processing::computeMeetingActivity(m, dir.path());
        QCOMPARE(a.tracks.size(), 1);
        const TrackActivity& t = a.tracks[0];
        QVERIFY(std::abs(t.dbAboveFloor.size() - 60) <= 2);
        QVERIFY(*windowEnergyDb(t, 1200, 1800) > 40.0f);
        QVERIFY(*windowEnergyDb(t, 200, 800) < 6.0f);
        QVERIFY(*windowEnergyDb(t, 2200, 2800) < 6.0f);
    }
};

QTEST_GUILESS_MAIN(TrackActivityTest)
#include "test_track_activity.moc"
