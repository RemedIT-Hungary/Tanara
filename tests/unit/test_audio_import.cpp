//
// AudioImporter — hangfájlok importálása új meetingbe: fájl-adatok (ffprobe), sáv-terv,
// alapértelmezett cím / dátum, kódolás a felvett sávok formájára, csatorna-bontás, több
// fájl, videó hangja, nem-hang fájl elutasítása, megszakítás (semmi nem marad utána), és az
// AppController-réteg (JobKind::Import a trackerben, lekeverés a mixdownMode szerint).
// A teszt-hangokat az ffmpeg gyártja ideiglenes mappába; ffmpeg nélkül a tesztek kimaradnak.
//
#include <QtTest>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtEndian>

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/audio/TrackCatalog.h"
#include "tanara/import/AudioImporter.h"
#include "tanara/jobs/MeetingJobTracker.h"
#include "tanara/store/MeetingStore.h"

using namespace tanara;

namespace {

bool run(const QString& program, const QStringList& args, QByteArray* out = nullptr)
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(60000)) { p.kill(); return false; }
    if (out) *out = p.readAllStandardOutput();
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

bool ffmpeg(const QStringList& args)
{
    return run(QStringLiteral("ffmpeg"),
               QStringList{"-hide_banner", "-loglevel", "error", "-y"} + args);
}

// A hangfájl csúcsa (0..1) mono 16 bitre dekódolva.
double peakOf(const QString& path)
{
    QByteArray pcm;
    if (!run(QStringLiteral("ffmpeg"), {"-hide_banner", "-loglevel", "error", "-i", path,
                                        "-ac", "1", "-f", "s16le", "-"}, &pcm))
        return -1.0;
    int peak = 0;
    const auto* s = reinterpret_cast<const qint16*>(pcm.constData());
    for (qsizetype i = 0; i < pcm.size() / 2; ++i)
        peak = qMax(peak, qAbs(int(qFromLittleEndian(s[i]))));
    return peak / 32768.0;
}

// Minden bejegyzés a mappában (a rejtetteket is) — „nem maradt semmi” ellenőrzéséhez.
QStringList everythingIn(const QString& dir)
{
    return QDir(dir).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
}

} // namespace

class AudioImportTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();

    void probeReadsAudioFiles();
    void probeRejectsNonAudio();
    void planAndDefaults();

    void importsMonoFile();
    void stereoKeptOrSplit();
    void multipleFilesWithOwnTrack();
    void videoContainerUsesEmbeddedDate();
    void nonAudioFileFailsCleanly();
    void cancelLeavesNothing();

    void controllerRunsImportAsJob();
    void controllerCancelAndManualMixdown();

private:
    QString src(const QString& name) const { return m_dir->filePath("src/" + name); }
    QString mono(const QString& name, int seconds, int freq = 440);
    QString stereoLeftOnly(const QString& name, int seconds);
    // Importálás a végéig; visszaadja a kész meetinget (üres id = nem sikerült).
    Meeting importAndWait(AudioImporter& imp, const ImportRequest& req);

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<MeetingStore> m_store;
    bool m_ffmpeg = false;
};

void AudioImportTest::initTestCase()
{
    m_ffmpeg = !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()
            && !QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty();
    qputenv("TANARA_CLOUD", "off");
}

void AudioImportTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    QVERIFY(QDir().mkpath(m_dir->filePath("src")));
    m_store = std::make_unique<MeetingStore>(m_dir->filePath("rec"), m_dir->filePath("meta"));
}

void AudioImportTest::cleanup()
{
    m_store.reset();
    m_dir.reset();
}

QString AudioImportTest::mono(const QString& name, int seconds, int freq)
{
    const QString path = src(name);
    if (!ffmpeg({"-f", "lavfi", "-i", QStringLiteral("sine=frequency=%1:duration=%2:sample_rate=16000")
                                          .arg(freq).arg(seconds), "-ac", "1", path}))
        return {};
    return path;
}

// Sztereó: a bal csatornán szinusz, a jobb néma — a bontás ellenőrzéséhez.
QString AudioImportTest::stereoLeftOnly(const QString& name, int seconds)
{
    const QString path = src(name);
    if (!ffmpeg({"-f", "lavfi", "-i", QStringLiteral("aevalsrc=0.5*sin(440*2*PI*t)|0:d=%1:s=16000").arg(seconds),
                 path}))
        return {};
    return path;
}

Meeting AudioImportTest::importAndWait(AudioImporter& imp, const ImportRequest& req)
{
    QSignalSpy finished(&imp, &AudioImporter::finished);
    QSignalSpy failed(&imp, &AudioImporter::failed);
    const QString id = imp.start(req);
    if (id.isEmpty()) return {};
    for (int i = 0; i < 600 && finished.isEmpty() && failed.isEmpty(); ++i)
        QTest::qWait(50);
    if (finished.isEmpty()) return {};
    return finished.first().at(1).value<Meeting>();
}

// ---- fájl-adatok, terv, alapértelmezések ---------------------------------------------

void AudioImportTest::probeReadsAudioFiles()
{
    if (!m_ffmpeg) QSKIP("ffmpeg / ffprobe nincs telepítve");
    const QString m = mono("egy.wav", 2);
    const QString s = stereoLeftOnly("ketto.flac", 1);
    QVERIFY(!m.isEmpty() && !s.isEmpty());

    const ImportFileInfo mi = audioimport::probe(m);
    QVERIFY2(mi.ok, qPrintable(mi.error));
    QCOMPARE(mi.channels, 1);
    QCOMPARE(mi.sampleRate, 16000);
    QVERIFY(qAbs(mi.durationMs - 2000) < 100);
    QVERIFY(mi.sizeBytes > 0);
    QVERIFY(!mi.hasVideo);
    QVERIFY(!mi.createdAt.isValid());
    QVERIFY(mi.modifiedAt.isValid());
    QCOMPARE(mi.bestDate(), mi.modifiedAt);
    QVERIFY(!audioimport::splitByDefault(mi));

    const ImportFileInfo si = audioimport::probe(s);
    QVERIFY(si.ok);
    QCOMPARE(si.channels, 2);
    QCOMPARE(si.codec, QStringLiteral("flac"));
    QVERIFY(!audioimport::splitByDefault(si));   // sima sztereó: a felhasználó dönt

    // Ugyanez a háttérben (a UI útja).
    AudioImporter imp(m_store.get());
    QSignalSpy probed(&imp, &AudioImporter::probed);
    imp.probeAsync(s);
    imp.probeAsync(src("nincs-ilyen.wav"));
    QTRY_COMPARE_WITH_TIMEOUT(probed.size(), 2, 10000);
    int okCount = 0;
    for (const auto& args : probed) {
        const ImportFileInfo info = args.at(0).value<ImportFileInfo>();
        if (info.ok) { ++okCount; QCOMPARE(info.channels, 2); }
        else QVERIFY(info.error.contains("nincs-ilyen.wav"));
    }
    QCOMPARE(okCount, 1);
}

void AudioImportTest::probeRejectsNonAudio()
{
    if (!m_ffmpeg) QSKIP("ffmpeg / ffprobe nincs telepítve");
    QFile text(src("jegyzet.txt"));
    QVERIFY(text.open(QIODevice::WriteOnly));
    text.write("ez nem hang\n");
    text.close();
    const ImportFileInfo ti = audioimport::probe(text.fileName());
    QVERIFY(!ti.ok);
    QVERIFY(ti.error.contains("jegyzet.txt"));

    // Videó hang nélkül: felismerhető fájl, de nincs benne hang.
    const QString silentVideo = src("nema.mkv");
    QVERIFY(ffmpeg({"-f", "lavfi", "-i", "testsrc=duration=1:size=64x64:rate=5", "-c:v", "mjpeg", silentVideo}));
    const ImportFileInfo vi = audioimport::probe(silentVideo);
    QVERIFY(!vi.ok);
    QVERIFY2(vi.error.contains("nincs hang"), qPrintable(vi.error));

    const ImportFileInfo missing = audioimport::probe(src("nincs.wav"));
    QVERIFY(!missing.ok);
    QVERIFY(missing.error.contains("nem található"));
}

void AudioImportTest::planAndDefaults()
{
    ImportFileInfo monoInfo;   monoInfo.ok = true;   monoInfo.channels = 1;
    ImportFileInfo stereoInfo; stereoInfo.ok = true; stereoInfo.channels = 2;
    ImportFileInfo quadInfo;   quadInfo.ok = true;   quadInfo.channels = 4;

    // Egy sztereó fájl bontás nélkül: egy sztereó sáv a fájl nevével.
    auto plan = audioimport::planTracks({{"/x/Interjú Kovács.wav", false}}, {stereoInfo});
    QCOMPARE(plan.size(), 1);
    QCOMPARE(plan[0].name, QStringLiteral("Interjú Kovács"));
    QCOMPARE(plan[0].channels, 2);
    QCOMPARE(plan[0].channel, -1);

    // Bontva: bal / jobb, mono sávok.
    plan = audioimport::planTracks({{"/x/Interjú Kovács.wav", true}}, {stereoInfo});
    QCOMPARE(plan.size(), 2);
    QCOMPARE(plan[0].name, QStringLiteral("Bal csatorna"));
    QCOMPARE(plan[1].name, QStringLiteral("Jobb csatorna"));
    QCOMPARE(plan[1].channel, 1);
    QCOMPARE(plan[1].channels, 1);

    // Mono fájlon a bontás nem hat; négy csatorna sorszámot kap, több fájlnál a fájlnévvel.
    plan = audioimport::planTracks({{"/x/a.wav", true}, {"/x/zoom h6.wav", true}}, {monoInfo, quadInfo});
    QCOMPARE(plan.size(), 5);
    QCOMPARE(plan[0].name, QStringLiteral("a"));
    QCOMPARE(plan[1].name, QStringLiteral("zoom h6 – 1. csatorna"));
    QCOMPARE(plan[4].name, QStringLiteral("zoom h6 – 4. csatorna"));
    QVERIFY(audioimport::splitByDefault(quadInfo));
    quadInfo.hasVideo = true;
    QVERIFY(!audioimport::splitByDefault(quadInfo));   // film 5.1 hangja: nem bontjuk magától

    // Négy csatorna bontás nélkül sztereóba keverve kerül be.
    plan = audioimport::planTracks({{"/x/zoom h6.wav", false}}, {quadInfo});
    QCOMPARE(plan[0].channels, 2);

    // A sávnév a TrackCatalog rövidítése után is a teljes fájlnév marad.
    for (const QString& file : {QStringLiteral("/x/Kovács - 2. rész.mp3"), QStringLiteral("/x/felvétel (1).wav"),
                                QStringLiteral("/x/Monitor of valami.wav")}) {
        const QString name = audioimport::planTracks({{file, false}}, {monoInfo}).first().name;
        QCOMPARE(tracknames::shortDeviceName(name), name);
        QVERIFY(name.size() >= QFileInfo(file).completeBaseName().size());
    }

    // Cím: egy fájl → a neve; több → a közös eleje (ha értelmes), különben az első.
    QCOMPARE(audioimport::defaultTitle({"/x/Interjú Kovács.wav"}), QStringLiteral("Interjú Kovács"));
    QCOMPARE(audioimport::defaultTitle({"/x/Kutatás 12 - mikrofon.wav", "/y/Kutatás 12 - vonal.wav"}),
             QStringLiteral("Kutatás 12"));
    QCOMPARE(audioimport::defaultTitle({"/x/alfa.wav", "/x/beta.wav"}), QStringLiteral("alfa"));
    QVERIFY(!audioimport::defaultTitle({}).isEmpty());

    // Dátum: a beágyazott idő erősebb a módosítási időnél; több fájlnál a legkorábbi.
    ImportFileInfo a, b;
    a.modifiedAt = QDateTime(QDate(2025, 3, 2), QTime(10, 0));
    b.modifiedAt = QDateTime(QDate(2025, 5, 1), QTime(9, 0));
    b.createdAt  = QDateTime(QDate(2025, 1, 15), QTime(8, 30));
    QCOMPARE(audioimport::defaultStart({a, b}), b.createdAt);
    QCOMPARE(audioimport::defaultStart({a}), a.modifiedAt);
}

// ---- importálás ----------------------------------------------------------------------

void AudioImportTest::importsMonoFile()
{
    if (!m_ffmpeg) QSKIP("ffmpeg / ffprobe nincs telepítve");
    const QString file = mono("Interjú Kovács.mp3", 2);
    QVERIFY(!file.isEmpty());
    const QDateTime stamp(QDate(2024, 11, 5), QTime(14, 30, 0));
    {
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(stamp, QFileDevice::FileModificationTime));
    }
    const qint64 sizeBefore = QFileInfo(file).size();

    AudioImporter imp(m_store.get());
    imp.setOpusBitrateKbps(32);
    QSignalSpy started(&imp, &AudioImporter::started);
    QSignalSpy progress(&imp, &AudioImporter::progress);
    QSignalSpy updated(m_store.get(), &MeetingStore::meetingUpdated);
    ImportRequest req;
    req.sources = {{file, false}};
    const Meeting m = importAndWait(imp, req);
    QVERIFY(!m.id.isEmpty());
    QVERIFY(!imp.busy());
    QCOMPARE(started.size(), 1);
    QCOMPARE(started.first().at(0).toString(), m.id);
    QCOMPARE(updated.size(), 1);

    // A meeting olyan, mint egy felvett: tárban van, cím a fájlnévből, dátum a fájl idejéből.
    const Meeting stored = m_store->load(m.id);
    QCOMPARE(stored.id, m.id);
    QCOMPARE(stored.title, QStringLiteral("Interjú Kovács"));
    QCOMPARE(stored.startedAt, stamp);
    QVERIFY2(qAbs(stored.durationMs - 2000) < 200, qPrintable(QString::number(stored.durationMs)));
    QVERIFY(stored.mixdownFile.isEmpty());
    QVERIFY(!stored.hasTranscript);
    QVERIFY(QFileInfo(stored.folder).fileName().startsWith("2024-11-05_1430_interju-kovacs"));
    QCOMPARE(QFileInfo(stored.folder).absolutePath(), QFileInfo(m_store->audioDir()).absoluteFilePath());

    QCOMPARE(stored.tracks.size(), 1);
    const Track t = stored.tracks.first();
    QCOMPARE(t.kind, TrackKind::Other);
    QVERIFY(!t.fixedSpeaker);
    QVERIFY(t.speakerLabel.isEmpty());
    QVERIFY(t.active);
    QCOMPARE(t.deviceName, QStringLiteral("Interjú Kovács"));
    QCOMPARE(t.file, QStringLiteral("track_interju-kovacs.ogg"));
    QCOMPARE(t.channels, 1);
    QCOMPARE(t.sampleRate, 48000);

    // A sáv Opus/ogg, 48 kHz — ugyanaz a forma, mint a felvett sávoké.
    const ImportFileInfo out = audioimport::probe(QDir(stored.folder).filePath(t.file));
    QVERIFY(out.ok);
    QCOMPARE(out.codec, QStringLiteral("opus"));
    QCOMPARE(out.sampleRate, 48000);
    QCOMPARE(out.channels, 1);
    QVERIFY(peakOf(QDir(stored.folder).filePath(t.file)) > 0.05);

    // A Sávok fül nézete: a fájl neve a sáv neve, a fájl megvan.
    const QVector<TrackView> views = TrackCatalog::tracks(stored);
    QCOMPARE(views.first().displayName, QStringLiteral("Interjú Kovács"));
    QCOMPARE(views.first().role, TrackRole::Other);
    QVERIFY(!views.first().fileMissing);

    // Valós haladás: nem csökken, a vége 100.
    QVERIFY(!progress.isEmpty());
    int last = -1;
    for (const auto& args : progress) {
        QVERIFY(args.at(1).toInt() >= last);
        last = args.at(1).toInt();
    }
    QCOMPARE(last, 100);

    // Az eredeti érintetlen, és nem maradt rejtett munkamappa.
    QCOMPARE(QFileInfo(file).size(), sizeBefore);
    QCOMPARE(QFileInfo(file).lastModified(), stamp);
    QCOMPARE(everythingIn(m_store->audioDir()), QStringList{QFileInfo(stored.folder).fileName()});

    // Index-újraépítés (árva-helyreállítással) után is pontosan egy meeting van.
    m_store->rebuildIndexFromDisk();
    QCOMPARE(m_store->loadAll().size(), 1);
    QCOMPARE(m_store->loadAll().first().id, m.id);
}

void AudioImportTest::stereoKeptOrSplit()
{
    if (!m_ffmpeg) QSKIP("ffmpeg / ffprobe nincs telepítve");
    const QString file = stereoLeftOnly("ketmikrofon.wav", 2);
    QVERIFY(!file.isEmpty());
    AudioImporter imp(m_store.get());

    // Bontás nélkül: egy sztereó sáv; megadott cím és dátum.
    ImportRequest whole;
    whole.sources = {{file, false}};
    whole.title = QStringLiteral("  Páros interjú ");
    whole.startedAt = QDateTime(QDate(2023, 2, 1), QTime(9, 15));
    const Meeting a = importAndWait(imp, whole);
    QVERIFY(!a.id.isEmpty());
    QCOMPARE(a.title, QStringLiteral("Páros interjú"));
    QCOMPARE(a.startedAt, whole.startedAt);
    QCOMPARE(a.tracks.size(), 1);
    QCOMPARE(a.tracks[0].channels, 2);
    QCOMPARE(audioimport::probe(QDir(a.folder).filePath(a.tracks[0].file)).channels, 2);

    // Bontva: két mono sáv; a bal szól, a jobb néma (a csatornák nem keverednek).
    ImportRequest split = whole;
    split.sources = {{file, true}};
    const Meeting b = importAndWait(imp, split);
    QVERIFY(!b.id.isEmpty());
    QVERIFY(b.folder != a.folder);   // azonos cím + időpont: a mappa mégis egyedi
    QCOMPARE(b.tracks.size(), 2);
    QCOMPARE(b.tracks[0].deviceName, QStringLiteral("Bal csatorna"));
    QCOMPARE(b.tracks[1].deviceName, QStringLiteral("Jobb csatorna"));
    QCOMPARE(b.tracks[0].file, QStringLiteral("track_bal-csatorna.ogg"));
    for (const Track& t : b.tracks) {
        QCOMPARE(t.channels, 1);
        QCOMPARE(t.kind, TrackKind::Other);
        QCOMPARE(audioimport::probe(QDir(b.folder).filePath(t.file)).channels, 1);
    }
    QVERIFY(peakOf(QDir(b.folder).filePath(b.tracks[0].file)) > 0.2);
    QVERIFY(peakOf(QDir(b.folder).filePath(b.tracks[1].file)) < 0.01);
    QCOMPARE(tracknames::friendlyNames(b.tracks), QStringList({"Bal csatorna", "Jobb csatorna"}));
    QCOMPARE(m_store->loadAll().size(), 2);
}

void AudioImportTest::multipleFilesWithOwnTrack()
{
    if (!m_ffmpeg) QSKIP("ffmpeg / ffprobe nincs telepítve");
    const QString mine = mono("Kutatás 7 - saját.wav", 1);
    const QString theirs = stereoLeftOnly("Kutatás 7 - terem.ogg", 3);
    QVERIFY(!mine.isEmpty() && !theirs.isEmpty());

    AudioImporter imp(m_store.get());
    imp.setUserSpeakerName(QStringLiteral("Minta Márta"));
    QSignalSpy progress(&imp, &AudioImporter::progress);
    ImportRequest req;
    req.sources = {{mine, false}, {theirs, true}};
    req.ownTrack = 0;
    const Meeting m = importAndWait(imp, req);
    QVERIFY(!m.id.isEmpty());
    QCOMPARE(m.title, QStringLiteral("Kutatás 7"));
    QCOMPARE(m.tracks.size(), 3);
    QVERIFY2(qAbs(m.durationMs - 3000) < 250, qPrintable(QString::number(m.durationMs)));   // a leghosszabb sáv

    // A megjelölt sáv úgy viselkedik, mint egy felvett saját mikrofon.
    QCOMPARE(m.tracks[0].kind, TrackKind::Mic);
    QVERIFY(m.tracks[0].fixedSpeaker);
    QCOMPARE(m.tracks[0].speakerLabel, QStringLiteral("Minta Márta"));
    QCOMPARE(m.tracks[1].kind, TrackKind::Other);
    QVERIFY(!m.tracks[1].fixedSpeaker);
    QCOMPARE(tracknames::friendlyNames(m.tracks),
             QStringList({"Saját mikrofon", "Kutatás 7 – terem – Bal csatorna",
                          "Kutatás 7 – terem – Jobb csatorna"}));
    // Egyedi sáv-azonosítók és fájlok.
    QSet<QString> ids, files;
    for (const Track& t : m.tracks) {
        ids << t.id; files << t.file;
        QVERIFY(QFileInfo(QDir(m.folder).filePath(t.file)).size() > 0);
    }
    QCOMPARE(ids.size(), 3);
    QCOMPARE(files.size(), 3);
    // A haladás mindkét fájlt jelzi.
    QCOMPARE(progress.last().at(3).toInt(), 2);
    QCOMPARE(progress.last().at(1).toInt(), 100);
}

void AudioImportTest::videoContainerUsesEmbeddedDate()
{
    if (!m_ffmpeg) QSKIP("ffmpeg / ffprobe nincs telepítve");
    const QString video = src("előadás.mov");
    QVERIFY(ffmpeg({"-f", "lavfi", "-i", "testsrc=duration=2:size=64x64:rate=5",
                    "-f", "lavfi", "-i", "sine=frequency=330:duration=2",
                    "-c:v", "mjpeg", "-c:a", "pcm_s16le", "-shortest",
                    "-metadata", "creation_time=2024-03-05T10:20:30Z", video}));
    const ImportFileInfo info = audioimport::probe(video);
    QVERIFY2(info.ok, qPrintable(info.error));
    QVERIFY(info.hasVideo);
    QVERIFY(info.createdAt.isValid());
    QCOMPARE(info.createdAt.toUTC(), QDateTime(QDate(2024, 3, 5), QTime(10, 20, 30), QTimeZone::UTC));

    AudioImporter imp(m_store.get());
    ImportRequest req;
    req.sources = {{video, false}};
    const Meeting m = importAndWait(imp, req);
    QVERIFY(!m.id.isEmpty());
    QCOMPARE(m.startedAt.toUTC(), info.createdAt.toUTC());   // a beágyazott idő, nem a fájlé
    QCOMPARE(m.tracks.size(), 1);
    // Csak a hang került be.
    const ImportFileInfo out = audioimport::probe(QDir(m.folder).filePath(m.tracks[0].file));
    QVERIFY(out.ok);
    QVERIFY(!out.hasVideo);
    QCOMPARE(out.codec, QStringLiteral("opus"));
}

void AudioImportTest::nonAudioFileFailsCleanly()
{
    if (!m_ffmpeg) QSKIP("ffmpeg / ffprobe nincs telepítve");
    const QString good = mono("jo.wav", 1);
    QFile text(src("jegyzet.txt"));
    QVERIFY(text.open(QIODevice::WriteOnly));
    text.write("ez nem hang\n");
    text.close();

    AudioImporter imp(m_store.get());
    QSignalSpy failed(&imp, &AudioImporter::failed);
    QSignalSpy finished(&imp, &AudioImporter::finished);
    ImportRequest req;
    req.sources = {{good, false}, {text.fileName(), false}};
    const QString id = imp.start(req);
    QVERIFY(!id.isEmpty());
    QVERIFY(imp.busy());
    QVERIFY(imp.start(req).isEmpty());   // egyszerre egy importálás
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 15000);
    QCOMPARE(failed.first().at(0).toString(), id);
    QVERIFY(failed.first().at(1).toString().contains("jegyzet.txt"));
    QVERIFY(finished.isEmpty());
    QVERIFY(!imp.busy());
    QVERIFY(everythingIn(m_store->audioDir()).isEmpty());
    QVERIFY(m_store->loadAll().isEmpty());

    // Üres kérés: szintén jelben érkező hiba.
    QVERIFY(!imp.start(ImportRequest{}).isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 2, 5000);
    QVERIFY(!imp.busy());
}

void AudioImportTest::cancelLeavesNothing()
{
    if (!m_ffmpeg) QSKIP("ffmpeg / ffprobe nincs telepítve");
    // 40 perc hang: a kódolás elég hosszú ahhoz, hogy közben megszakítsuk.
    const QString big = mono("hosszu.wav", 2400);
    QVERIFY(!big.isEmpty());

    AudioImporter imp(m_store.get());
    QSignalSpy progress(&imp, &AudioImporter::progress);
    QSignalSpy cancelled(&imp, &AudioImporter::cancelled);
    QSignalSpy finished(&imp, &AudioImporter::finished);
    QSignalSpy failed(&imp, &AudioImporter::failed);
    ImportRequest req;
    req.sources = {{big, false}};
    const QString id = imp.start(req);
    // Megvárjuk, amíg tényleg kódol (valós százalék érkezik), és ott szakítjuk meg.
    QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty() && progress.last().at(1).toInt() > 0, 20000);
    QVERIFY(finished.isEmpty());
    QVERIFY(QDir(m_store->audioDir()).exists(".import-" + id));
    imp.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(cancelled.size(), 1, 10000);
    QCOMPARE(cancelled.first().at(0).toString(), id);
    QVERIFY(finished.isEmpty());
    QVERIFY(failed.isEmpty());
    QVERIFY(!imp.busy());
    QVERIFY2(everythingIn(m_store->audioDir()).isEmpty(),
             qPrintable(everythingIn(m_store->audioDir()).join(", ")));
    QVERIFY(m_store->loadAll().isEmpty());

    // Megszakítás után azonnal indítható új.
    const QString small = mono("rovid.wav", 1);
    ImportRequest again;
    again.sources = {{small, false}};
    QVERIFY(!importAndWait(imp, again).id.isEmpty());
    QCOMPARE(m_store->loadAll().size(), 1);
}

// ---- AppController-réteg --------------------------------------------------------------

void AudioImportTest::controllerRunsImportAsJob()
{
    if (!m_ffmpeg) QSKIP("ffmpeg / ffprobe nincs telepítve");
    qputenv("TANARA_HOME", m_dir->filePath("home").toUtf8());
    AppController app;
    QVERIFY(app.settings()->settings().audioDir.startsWith(m_dir->path()));
    const QString file = stereoLeftOnly("megbeszeles.wav", 2);

    QSignalSpy imported(&app, &AppController::importFinished);
    QSignalSpy jobFinished(app.jobs(), &MeetingJobTracker::jobFinished);
    QSignalSpy mixed(&app, &AppController::mixdownUpdated);
    QSignalSpy added(app.store(), &MeetingStore::meetingUpdated);
    ImportRequest req;
    req.sources = {{file, true}};
    const QString id = app.importAudio(req);
    QVERIFY(!id.isEmpty());
    // A feladat a leendő meeting azonosítóján fut, megszakítható; a meeting még nincs meg.
    QVERIFY(app.jobs()->isRunning(id, JobKind::Import));
    QVERIFY(app.jobs()->job(id, JobKind::Import).cancellable);
    QCOMPARE(app.jobs()->activeJobs().size(), 1);
    QVERIFY(app.store()->load(id).id.isEmpty());
    QVERIFY(app.importAudio(req).isEmpty());   // közben nem indul második

    QTRY_COMPARE_WITH_TIMEOUT(imported.size(), 1, 20000);
    const Meeting m = imported.first().at(0).value<Meeting>();
    QCOMPARE(m.id, id);
    QVERIFY(!app.jobs()->isRunning(id, JobKind::Import));
    QCOMPARE(jobFinished.first().at(1).value<JobKind>(), JobKind::Import);
    QCOMPARE(jobFinished.first().at(2).value<JobOutcome>(), JobOutcome::Done);
    QCOMPARE(app.store()->load(id).tracks.size(), 2);
    QVERIFY(!added.isEmpty());

    // mixdownMode "auto" (alapértelmezés): a lekeverés magától indul, mint felvétel után.
    QTRY_COMPARE_WITH_TIMEOUT(mixed.size(), 1, 30000);
    QVERIFY(mixed.first().at(1).toBool());
    const Meeting after = app.store()->load(id);
    QCOMPARE(after.mixdownFile, QStringLiteral("mixdown.mp3"));
    QVERIFY(QFileInfo(QDir(after.folder).filePath(after.mixdownFile)).size() > 0);
    QVERIFY(!after.hasTranscript);   // átírás nem indul magától
    QCOMPARE(app.processingState(id).transcriptState, StepState::None);
    qunsetenv("TANARA_HOME");
}

void AudioImportTest::controllerCancelAndManualMixdown()
{
    if (!m_ffmpeg) QSKIP("ffmpeg / ffprobe nincs telepítve");
    qputenv("TANARA_HOME", m_dir->filePath("home").toUtf8());
    AppController app;
    AppSettings s = app.settings()->settings();
    s.mixdownMode = QStringLiteral("manual");
    s.audioQuality = QStringLiteral("low");
    app.settings()->setSettings(s);
    const QString audioDir = app.store()->audioDir();

    // Megszakítás a feladat-rétegen át.
    const QString big = mono("hosszu.wav", 2400);
    QSignalSpy jobFinished(app.jobs(), &MeetingJobTracker::jobFinished);
    QSignalSpy cancelled(&app, &AppController::importCancelled);
    ImportRequest req;
    req.sources = {{big, false}};
    const QString id = app.importAudio(req);
    QTRY_VERIFY_WITH_TIMEOUT(app.jobs()->job(id, JobKind::Import).percent > 0, 20000);
    QVERIFY(!app.cancelJob(QStringLiteral("masik"), JobKind::Import));
    QVERIFY(app.cancelJob(id, JobKind::Import));
    QTRY_COMPARE_WITH_TIMEOUT(cancelled.size(), 1, 10000);
    QCOMPARE(jobFinished.last().at(2).value<JobOutcome>(), JobOutcome::Cancelled);
    QVERIFY(app.jobs()->activeJobs().isEmpty());
    QVERIFY(app.store()->loadAll().isEmpty());
    QVERIFY(everythingIn(audioDir).isEmpty());

    // Hibás fájl: importFailed, a feladat Failed-del zárul, nem marad meeting.
    QSignalSpy failed(&app, &AppController::importFailed);
    ImportRequest bad;
    bad.sources = {{src("nincs-ilyen.wav"), false}};
    const QString badId = app.importAudio(bad);
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 10000);
    QCOMPARE(failed.first().at(0).toString(), badId);
    QCOMPARE(jobFinished.last().at(2).value<JobOutcome>(), JobOutcome::Failed);
    QVERIFY(app.store()->loadAll().isEmpty());

    // Kézi lekeverés-mód: importálás után nem indul keverés; a sáv a beállított minőségű.
    QSignalSpy imported(&app, &AppController::importFinished);
    QSignalSpy mixed(&app, &AppController::mixdownUpdated);
    ImportRequest ok;
    ok.sources = {{mono("rovid.wav", 2), false}};
    const QString okId = app.importAudio(ok);
    QTRY_COMPARE_WITH_TIMEOUT(imported.size(), 1, 20000);
    QTest::qWait(300);
    QVERIFY(mixed.isEmpty());
    QVERIFY(!app.jobs()->isBusy(okId));
    const Meeting m = app.store()->load(okId);
    QVERIFY(m.mixdownFile.isEmpty());
    QVERIFY(!QFileInfo::exists(QDir(m.folder).filePath("mixdown.mp3")));
    qunsetenv("TANARA_HOME");
}

QTEST_GUILESS_MAIN(AudioImportTest)
#include "test_audio_import.moc"
