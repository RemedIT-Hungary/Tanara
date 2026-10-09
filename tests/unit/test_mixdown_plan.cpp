//
// Tanara — MixdownPlan: a lekeverés bemenetei és szűrőgráfja. A később kezdődő sáv / egy eszköz
// második szakasza adelay-jel kerül a helyére; az eldobott sáv kimarad; a hiányzó fájlú aktív
// sáv neve a `missing`-be kerül. A végén valódi ffmpeg-keverés szintetikus (lavfi sine) sávokkal.
//
#include <QtTest>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "tanara/audio/MixdownPlan.h"

using namespace tanara;

namespace {

Track mk(const QString& id, const QString& device, TrackKind kind, qint64 offset = 0, bool active = true)
{
    Track t;
    t.id = id;
    t.deviceName = device;
    t.kind = kind;
    t.file = QStringLiteral("track_%1.ogg").arg(id);
    t.startOffsetMs = offset;
    t.active = active;
    return t;
}

bool haveFfmpeg()
{
    return !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()
        && !QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty();
}

bool sine(const QString& path, double seconds, int freq)
{
    QProcess p;
    p.start(QStringLiteral("ffmpeg"), {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
        QStringLiteral("error"), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
        QStringLiteral("sine=frequency=%1:sample_rate=48000:duration=%2").arg(freq).arg(seconds),
        QStringLiteral("-c:a"), QStringLiteral("libopus"), QStringLiteral("-y"), path});
    return p.waitForFinished(20000) && p.exitCode() == 0;
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

void touch(const QString& path)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
}

} // namespace

class MixdownPlanTest : public QObject {
    Q_OBJECT
private slots:
    void filterNoOffsets();
    void filterOffsetsGetAdelay();
    void filterSingleInput();
    void planSkipsDroppedAndReportsMissing();
    void planSegmentsOfOneDevice();
    void ffmpegMixPlacesLateTrack();
};

void MixdownPlanTest::filterNoOffsets()
{
    const QStringList f = MixdownPlan::filterArgs({0, 0});
    QCOMPARE(f.size(), 2);
    QCOMPARE(f.at(0), QStringLiteral("-filter_complex"));
    QVERIFY(f.at(1).startsWith(QStringLiteral("[0][1]amix=inputs=2:duration=longest:normalize=0,loudnorm")));
    QVERIFY(!f.at(1).contains(QStringLiteral("adelay")));
    QVERIFY(MixdownPlan::filterArgs({}).isEmpty());
}

void MixdownPlanTest::filterOffsetsGetAdelay()
{
    const QStringList f = MixdownPlan::filterArgs({0, 57200, 0, 1500});
    QCOMPARE(f.at(0), QStringLiteral("-filter_complex"));
    QVERIFY2(f.at(1).startsWith(QStringLiteral(
        "[1]adelay=57200:all=1[d1];[3]adelay=1500:all=1[d3];[0][d1][2][d3]amix=inputs=4:")),
        qPrintable(f.at(1)));
}

void MixdownPlanTest::filterSingleInput()
{
    // Egy eltolás nélküli bemenet: csak normalizálás.
    QStringList f = MixdownPlan::filterArgs({0});
    QCOMPARE(f.at(0), QStringLiteral("-af"));
    QVERIFY(f.at(1).startsWith(QStringLiteral("loudnorm")));
    // Egy eltolt bemenet: adelay, amix nélkül.
    f = MixdownPlan::filterArgs({2000});
    QCOMPARE(f.at(0), QStringLiteral("-filter_complex"));
    QVERIFY2(f.at(1).startsWith(QStringLiteral("[0]adelay=2000:all=1[d0];[d0]loudnorm")), qPrintable(f.at(1)));
    QVERIFY(!f.at(1).contains(QStringLiteral("amix")));
}

void MixdownPlanTest::planSkipsDroppedAndReportsMissing()
{
    QTemporaryDir dir;
    Meeting m;
    m.id = QStringLiteral("m1");
    m.folder = dir.path();
    m.tracks = {mk("mic", "USB Mic", TrackKind::Mic),
                mk("old", "Webcam", TrackKind::Mic, 0, false),                 // eldobott
                mk("sys", "Monitor of Kanto YU4", TrackKind::Loopback, 3000)};  // nincs fájl
    touch(QDir(m.folder).filePath(QStringLiteral("track_mic.ogg")));
    touch(QDir(m.folder).filePath(QStringLiteral("track_old.ogg")));
    const MixdownPlan plan = MixdownPlan::fromMeeting(m);
    QCOMPARE(plan.inputs.size(), 1);
    QCOMPARE(plan.inputs.at(0).trackId, QStringLiteral("mic"));
    QCOMPARE(plan.missing.size(), 1);
    QVERIFY(!plan.missing.at(0).isEmpty());
    QVERIFY(plan.missing.at(0) != QStringLiteral("sys"));   // a megjelenített név, nem az azonosító

    const QStringList args = plan.ffmpegArgs(QStringLiteral("/x/out.mp3"));
    QCOMPARE(args.count(QStringLiteral("-i")), 1);
    QVERIFY(args.contains(QStringLiteral("-af")));
    QCOMPARE(args.last(), QStringLiteral("/x/out.mp3"));
    QVERIFY(MixdownPlan{}.ffmpegArgs(QStringLiteral("/x/out.mp3")).isEmpty());
}

void MixdownPlanTest::planSegmentsOfOneDevice()
{
    QTemporaryDir dir;
    Meeting m;
    m.id = QStringLiteral("m2");
    m.folder = dir.path();
    // A mikrofon végig szól; a rendszerhang 57,2 s-nál bekapcsolva, kikapcsolva, majd 300 s-nál
    // újra: két szakasz, két fájl.
    m.tracks = {mk("mic", "USB Mic", TrackKind::Mic),
                mk("sys", "Monitor of Speakers", TrackKind::Loopback, 57200),
                mk("sys-2", "Monitor of Speakers", TrackKind::Loopback, 300000)};
    for (const Track& t : std::as_const(m.tracks)) touch(QDir(m.folder).filePath(t.file));
    const MixdownPlan plan = MixdownPlan::fromMeeting(m);
    QCOMPARE(plan.inputs.size(), 3);
    QCOMPARE(plan.inputs.at(1).offsetMs, qint64(57200));
    QCOMPARE(plan.inputs.at(2).offsetMs, qint64(300000));
    const QStringList args = plan.ffmpegArgs(QStringLiteral("out.mp3"));
    const int fc = int(args.indexOf(QStringLiteral("-filter_complex")));
    QVERIFY(fc > 0);
    QVERIFY2(args.at(fc + 1).startsWith(QStringLiteral(
        "[1]adelay=57200:all=1[d1];[2]adelay=300000:all=1[d2];[0][d1][d2]amix=inputs=3:")),
        qPrintable(args.at(fc + 1)));
}

void MixdownPlanTest::ffmpegMixPlacesLateTrack()
{
    if (!haveFfmpeg()) QSKIP("ffmpeg/ffprobe nincs a PATH-on");
    QTemporaryDir dir;
    Meeting m;
    m.id = QStringLiteral("m3");
    m.folder = dir.path();
    m.tracks = {mk("mic", "USB Mic", TrackKind::Mic),
                mk("sys", "Monitor of Speakers", TrackKind::Loopback, 2500)};
    QVERIFY(sine(QDir(m.folder).filePath(QStringLiteral("track_mic.ogg")), 3.0, 440));
    QVERIFY(sine(QDir(m.folder).filePath(QStringLiteral("track_sys.ogg")), 1.0, 660));
    const QString out = QDir(m.folder).filePath(QStringLiteral("mixdown.mp3"));
    QProcess p;
    p.start(QStringLiteral("ffmpeg"), MixdownPlan::fromMeeting(m).ffmpegArgs(out));
    QVERIFY(p.waitForFinished(60000));
    QCOMPARE(p.exitCode(), 0);
    // A 2,5 s-mal később kezdődő 1 s-os sáv a keverék végét 3,5 s-ra tolja.
    const qint64 d = probeMs(out);
    QVERIFY2(qAbs(d - 3500) < 150, qPrintable(QString::number(d)));
}

QTEST_GUILESS_MAIN(MixdownPlanTest)
#include "test_mixdown_plan.moc"
