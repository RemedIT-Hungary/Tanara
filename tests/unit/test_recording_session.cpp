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

// Felvétel közben kihúzott eszköz: csak az ő sávja zárul le (a fájlja a lezárásig tart), a
// többi sáv megszakítás nélkül megy tovább, és egyik sáv eltolása sem változik.
void RecordingSessionTest::unpluggedDeviceClosesOnlyItsTrack()
{
    if (!haveFfmpeg()) QSKIP("ffmpeg/ffprobe nincs a PATH-on");
    QTemporaryDir dir;
    FakeEngine* engine = nullptr;
    RecordingSession rec(dir.path(), QStringLiteral("Kihúzás"));
    rec.setEngineFactory([&engine] { auto e = std::make_unique<FakeEngine>(); engine = e.get(); return e; });
    QSignalSpy finished(&rec, &RecordingSession::finished);
    QSignalSpy closed(&rec, &RecordingSession::trackClosed);
    QSignalSpy elapsed(&rec, &RecordingSession::elapsedChanged);
    QTimer feeder;
    feeder.setInterval(5);
    QObject::connect(&feeder, &QTimer::timeout, [&engine] { if (engine) engine->feed(); });

    const QString headset = QStringLiteral("Sennheiser headset");
    rec.start({dev(QStringLiteral("Mic A"), TrackKind::Mic), dev(headset, TrackKind::Mic),
               dev(QStringLiteral("Monitor of Speakers"), TrackKind::Loopback)});
    QCOMPARE(rec.state(), RecordingState::Recording);
    feeder.start();

    QTest::qWait(800);
    engine->vanish(headset);                     // kihúzták: nem jön több adat
    QTest::qWait(300);
    rec.closeTrack(headset);                     // a hot-plug figyelő zárja le
    QCOMPARE(closed.size(), 1);
    QCOMPARE(closed.at(0).at(0).toInt(), 1);
    QCOMPARE(closed.at(0).at(1).toString(), headset);
    QCOMPARE(engine->closeCalls, 1);
    QVERIFY(!rec.trackOpen(1));
    QVERIFY(rec.trackOpen(0));
    QVERIFY(rec.trackOpen(2));
    rec.closeTrack(headset);                     // ismételt jelzés: nincs második lezárás
    QCOMPARE(closed.size(), 1);

    elapsed.clear();
    QTest::qWait(900);
    QCOMPARE(rec.state(), RecordingState::Recording);   // a felvétel megy tovább
    QVERIFY(elapsed.size() > 5);                        // a drain-szál dolgozik
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
    // A kihúzott eszköz sávja a kihúzásig tartó hangot hordozza, lezárt, olvasható fájlként.
    QVERIFY2(dHs > 600 && dHs < 1100, qPrintable(QString::number(dHs)));
}

QTEST_GUILESS_MAIN(RecordingSessionTest)
#include "test_recording_session.moc"
