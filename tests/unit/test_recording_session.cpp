//
// Tanara — RecordingSession hamis capture-motorral (hangeszköz nélkül): a felvétel közben
// bekapcsolt / ki-be kapcsolt sáv a megnyitás idejét kapja eltolásnak (startOffsetMs), a fájlja
// csak a ténylegesen felvett szakasz (nincs vezető csend), és a néma loopback réseit — ahol a
// pótlás be van kapcsolva — a felvevő csenddel tölti ki. Az encoder a valódi ffmpeg (nélküle
// QSKIP). A „tracktiming” tiszta függvényei is itt.
//
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

#include "tanara/audio/AudioEngine.h"
#include "tanara/audio/RecordingSession.h"
#include "tanara/audio/TrackTiming.h"

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

// Hamis motor: eszközönként egy körpuffer. A „feed()” a fali óra szerint esedékes mintákat
// írja a NYITOTT, nem néma eszközök pufferébe (440 Hz-es szinusz) — mintha valódi capture
// futna. A `silentNames`-beli eszközök sosem adnak adatot (mint a WASAPI loopback csendben).
class FakeEngine : public AudioEngine {
public:
    struct Slot {
        std::unique_ptr<RingBuffer> ring;
        AudioDeviceInfo info;
        bool open = true;
        QElapsedTimer since;
        qint64 framesFed = 0;
    };
    QStringList silentNames;
    std::array<std::unique_ptr<Slot>, kMaxDevices> devs;
    int n = 0;

    bool start(const QVector<AudioDeviceInfo>& devices) override {
        for (const AudioDeviceInfo& d : devices) addDevice(d);
        return n > 0;
    }
    void stop() override { for (int i = 0; i < n; ++i) devs[i]->open = false; }
    int addDevice(const AudioDeviceInfo& d) override {
        auto s = std::make_unique<Slot>();
        s->ring = std::make_unique<RingBuffer>(48000 * 2);
        s->info = d;
        s->since.start();
        devs[n] = std::move(s);
        return n++;
    }
    void closeDevice(int i) override { if (i >= 0 && i < n) devs[i]->open = false; }
    bool isOpen(int i) const override { return i >= 0 && i < n && devs[i]->open; }
    int count() const override { return n; }
    RingBuffer& buffer(int i) override { return *devs[i]->ring; }
    float rms(int i) const override { return silentNames.contains(devs[i]->info.name) ? 0.0f : 0.3f; }
    float peak(int i) const override { return rms(i); }
    float takePeak(int i) override { return rms(i); }
    int channels(int) const override { return 1; }
    AudioDeviceInfo deviceInfo(int i) const override { return devs[i]->info; }

    void feed() {
        for (int i = 0; i < n; ++i) {
            Slot& s = *devs[i];
            if (!s.open) continue;
            if (silentNames.contains(s.info.name)) { s.framesFed = s.since.elapsed() * 48; continue; }
            const qint64 due = s.since.elapsed() * 48 - s.framesFed;
            if (due <= 0) continue;
            std::vector<int16_t> buf(static_cast<size_t>(due));
            for (qint64 k = 0; k < due; ++k)
                buf[size_t(k)] = int16_t(8000 * std::sin(2.0 * M_PI * 440.0 * double(s.framesFed + k) / 48000.0));
            s.ring->write(buf.data(), buf.size());
            s.framesFed += due;
        }
    }
};

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
};

void RecordingSessionTest::fileRangeMapping()
{
    using tracktiming::meetingToFileRange;
    // Együtt induló sáv: változatlan.
    auto r = meetingToFileRange(0, 1000, 4000);
    QVERIFY(r.valid());
    QCOMPARE(r.startMs, qint64(1000)); QCOMPARE(r.endMs, qint64(4000));
    // 57,2 s-mal később induló sáv: a megbeszélés 60–63 s-a a fájl 2,8–5,8 s-a.
    r = meetingToFileRange(57200, 60000, 63000);
    QCOMPARE(r.startMs, qint64(2800)); QCOMPARE(r.endMs, qint64(5800));
    // A sáv kezdete előtti tartomány: üres; az átlógó levágódik.
    QVERIFY(!meetingToFileRange(57200, 10000, 13000).valid());
    r = meetingToFileRange(57200, 56000, 59000);
    QCOMPARE(r.startMs, qint64(0)); QCOMPARE(r.endMs, qint64(1800));
    // A fájl vége után: üres; az átlógó levágódik.
    QVERIFY(!meetingToFileRange(1000, 20000, 23000, 10000).valid());
    r = meetingToFileRange(1000, 10000, 13000, 10000);
    QCOMPARE(r.startMs, qint64(9000)); QCOMPARE(r.endMs, qint64(10000));
    // Üres / fordított kérés.
    QVERIFY(!meetingToFileRange(0, 5000, 5000).valid());
    Track t; t.startOffsetMs = 2000;
    QCOMPARE(tracktiming::fileRange(t, 2500, 3000).startMs, qint64(500));
}

void RecordingSessionTest::framesOwedHelper()
{
    using namespace tracktiming;
    QCOMPARE(expectedFrames(1000, 48000), qint64(48000));
    QCOMPARE(expectedFrames(-5, 48000), qint64(0));
    QCOMPARE(framesOwed(48000, 48000, 960), qint64(0));
    QCOMPARE(framesOwed(48000, 47500, 960), qint64(0));        // a tűrésen belül: semmi
    QCOMPARE(framesOwed(48000, 40000, 960), qint64(8000));     // hiány: mind pótolandó
    QCOMPARE(framesOwed(48000, 50000, 960), qint64(0));        // előre futó eszköz: sosem negatív
    // Pótlás csak akkor, ha az eszköz kGapIdleMs óta hallgat.
    QCOMPARE(silenceToInsert(2000, kGapIdleMs - 1, 0, 48000), qint64(0));
    QCOMPARE(silenceToInsert(2000, kGapIdleMs, 0, 48000), qint64(96000));
    QCOMPARE(silenceToInsert(2000, 500, 96000 - 100, 48000), qint64(0));   // 2 ms hiány < tűrés
    // Csak Windows-loopback pótol alapból.
#if defined(_WIN32)
    QVERIFY(fillsCaptureGaps(TrackKind::Loopback));
#else
    QVERIFY(!fillsCaptureGaps(TrackKind::Loopback));
#endif
    QVERIFY(!fillsCaptureGaps(TrackKind::Mic));
}

void RecordingSessionTest::lateAndToggledTracksGetOffsets()
{
    if (!haveFfmpeg()) QSKIP("ffmpeg/ffprobe nincs a PATH-on");
    QTemporaryDir dir;
    FakeEngine* engine = nullptr;
    RecordingSession rec(dir.path(), QStringLiteral("Szakaszok"));
    rec.setEngineFactory([&engine] { auto e = std::make_unique<FakeEngine>(); engine = e.get(); return e; });
    QSignalSpy finished(&rec, &RecordingSession::finished);
    QTimer feeder;
    feeder.setInterval(5);
    QObject::connect(&feeder, &QTimer::timeout, [&engine] { if (engine) engine->feed(); });

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
    QVERIFY(rec.addDevice(sys));                 // t ≈ 2 s: második szakasz
    QVERIFY(!rec.addDevice(sys));                // nyitott sávon már van
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

    // A fájlok a ténylegesen felvett szakaszt tartalmazzák: nincs vezető csend.
    const qint64 dMic = probeMs(QDir(m.folder).filePath(mic.file));
    const qint64 d1 = probeMs(QDir(m.folder).filePath(s1.file));
    const qint64 d2 = probeMs(QDir(m.folder).filePath(s2.file));
    QVERIFY2(qAbs(dMic - m.durationMs) < 250, qPrintable(QStringLiteral("%1 vs %2").arg(dMic).arg(m.durationMs)));
    QVERIFY2(d1 > 300 && d1 < 800, qPrintable(QString::number(d1)));      // ~0,5 s, nem ~1,5 s
    QVERIFY2(d2 > 300 && d2 < 800, qPrintable(QString::number(d2)));
    // Eltolás + hossz a megbeszélés végén belül marad.
    QVERIFY(s2.startOffsetMs + d2 <= m.durationMs + 250);
}

void RecordingSessionTest::silentLoopbackGapsAreFilled()
{
    if (!haveFfmpeg()) QSKIP("ffmpeg/ffprobe nincs a PATH-on");
    QTemporaryDir dir;
    FakeEngine* engine = nullptr;
    RecordingSession rec(dir.path(), QStringLiteral("Néma loopback"));
    rec.setFillCaptureGaps(true);   // Linuxon alapból ki — itt a Windows-ág logikáját mérjük
    rec.setEngineFactory([&engine] {
        auto e = std::make_unique<FakeEngine>();
        e->silentNames << QStringLiteral("Speakers (loopback)");
        engine = e.get();
        return e;
    });
    QSignalSpy finished(&rec, &RecordingSession::finished);
    QTimer feeder;
    feeder.setInterval(5);
    QObject::connect(&feeder, &QTimer::timeout, [&engine] { if (engine) engine->feed(); });

    rec.start({dev(QStringLiteral("Mic A"), TrackKind::Mic),
               dev(QStringLiteral("Speakers (loopback)"), TrackKind::Loopback)});
    feeder.start();
    QTest::qWait(700);
    // Félúton „megszólal” a loopback egy rövid időre, aztán megint néma.
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
    // A néma szakaszok csenddel pótolva: a loopback fájlja ugyanolyan hosszú, mint a mic-é.
    QVERIFY2(qAbs(dLb - dMic) < 200, qPrintable(QStringLiteral("loopback %1 ms, mic %2 ms").arg(dLb).arg(dMic)));
}

QTEST_GUILESS_MAIN(RecordingSessionTest)
#include "test_recording_session.moc"
