//
// TrackCatalog + WaveformService — „Sávok” fül (M09): barátságos nevek az eszköz szerepe
// alapján, átnevezés, hiányzó fájl megkeresése, eldobott sávok törlése, hullámforma-csúcsok
// (aszinkron ffmpeg + gyorsítótár a meeting mappájában).
//
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QtEndian>
#include <cmath>

#include "tanara/audio/TrackCatalog.h"
#include "tanara/audio/WaveformService.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/JsonSerialization.h"

using namespace tanara;

namespace {

Track mk(const QString& id, const QString& device, TrackKind kind, bool active = true, float peak = 0.5f)
{
    Track t;
    t.id = id;
    t.deviceName = device;
    t.kind = kind;
    t.active = active;
    t.peakLevel = peak;
    t.file = QStringLiteral("track_%1.wav").arg(id);
    return t;
}

// 16 bites mono WAV: az első fele csend, a második fele szinusz (amplitúdó: amp).
void writeWav(const QString& path, int seconds, double amp = 0.5, int rate = 8000)
{
    const int n = seconds * rate;
    QByteArray pcm(n * 2, '\0');
    auto* s = reinterpret_cast<qint16*>(pcm.data());
    for (int i = n / 2; i < n; ++i)
        s[i] = qToLittleEndian<qint16>(qint16(amp * 32767.0 * std::sin(2.0 * M_PI * 440.0 * i / rate)));
    QByteArray h;
    auto le32 = [&h](quint32 v) { char b[4]; qToLittleEndian(v, b); h.append(b, 4); };
    auto le16 = [&h](quint16 v) { char b[2]; qToLittleEndian(v, b); h.append(b, 2); };
    h.append("RIFF"); le32(quint32(36 + pcm.size()));
    h.append("WAVEfmt "); le32(16); le16(1); le16(1);
    le32(quint32(rate)); le32(quint32(rate * 2)); le16(2); le16(16);
    h.append("data"); le32(quint32(pcm.size()));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(h);
    f.write(pcm);
}

bool haveFfmpeg()
{
    return !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();
}

} // namespace

class TracksTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void friendlyNamesByRole();
    void callDeviceByLoudnessWhenNameSilent();
    void duplicateRolesDisambiguated();
    void shortDeviceNames();

    void viewsAndMissingFile();
    void renamePersists();
    void relocateCopiesIntoFolder();
    void relocateRejectsBadFiles();
    void deleteDroppedTracksAtOnce();

    void peaksComputedCachedAndReused();
    void peaksInvalidatedWhenFileChanges();
    void peaksFailForBrokenFile();
    void resample();

private:
    Meeting makeMeeting(const QVector<Track>& tracks, bool createFiles = true);
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<MeetingStore> m_store;
};

void TracksTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_store = std::make_unique<MeetingStore>(m_dir->filePath("rec"), m_dir->filePath("meta"));
}

void TracksTest::cleanup()
{
    m_store.reset();
    m_dir.reset();
}

Meeting TracksTest::makeMeeting(const QVector<Track>& tracks, bool createFiles)
{
    Meeting m = m_store->createMeeting("Sávok");
    m.tracks = tracks;
    m.durationMs = 2000;
    m.mixdownFile = "mixdown.mp3";
    if (createFiles)
        for (const Track& t : tracks) writeWav(QDir(m.folder).filePath(t.file), 2);
    m_store->saveMeeting(m);
    return m;
}

// ---- barátságos nevek -------------------------------------------------------------------

void TracksTest::friendlyNamesByRole()
{
    // A valós felállás: headset „Communication” monitorja + egy másik kimenet + USB mikrofon.
    const QVector<Track> tracks{
        mk("comm", "Monitor of Sennheiser - Communication", TrackKind::Loopback, true, 0.5f),
        mk("mic", "Trust USB mikrofon", TrackKind::Mic, true, 0.7f),
        mk("kanto", "Monitor of Kanto YU4 - Optikai Digitális sztereó (IEC958)", TrackKind::Loopback, false, 0.0f),
    };
    QCOMPARE(tracknames::classify(tracks[0], tracks), TrackRole::CallAudio);
    QCOMPARE(tracknames::classify(tracks[1], tracks), TrackRole::OwnMic);
    QCOMPARE(tracknames::classify(tracks[2], tracks), TrackRole::SystemAudio);
    QCOMPARE(tracknames::friendlyNames(tracks),
             QStringList({"Hívás hangja", "Saját mikrofon", "Rendszerhang"}));
    QVERIFY(tracknames::looksLikeCallDevice("Headset Earphone (Jabra) Hands-Free AG Audio"));
    QVERIFY(!tracknames::looksLikeCallDevice("Monitor of Kanto YU4 - Optikai Digitális sztereó (IEC958)"));
}

void TracksTest::callDeviceByLoudnessWhenNameSilent()
{
    // Egyik név sem utal hívás-eszközre: az egyetlen aktív monitor a hívás hangja …
    QVector<Track> one{
        mk("a", "Monitor of Beépített hang - Analóg sztereó", TrackKind::Loopback, true, 0.3f),
        mk("b", "Monitor of HDMI kimenet", TrackKind::Loopback, false, 0.0f),
    };
    QCOMPARE(tracknames::classify(one[0], one), TrackRole::CallAudio);
    QCOMPARE(tracknames::classify(one[1], one), TrackRole::SystemAudio);
    // … több aktív közül a leghangosabb.
    QVector<Track> two{
        mk("a", "Monitor of Beépített hang", TrackKind::Loopback, true, 0.1f),
        mk("b", "Monitor of USB DAC", TrackKind::Loopback, true, 0.6f),
    };
    QCOMPARE(tracknames::classify(two[0], two), TrackRole::SystemAudio);
    QCOMPARE(tracknames::classify(two[1], two), TrackRole::CallAudio);
    // Vonalbemenet / ismeretlen: az eszköz neve marad.
    QVector<Track> other{mk("aux", "Line In (Scarlett 2i2)", TrackKind::Other)};
    QCOMPARE(tracknames::classify(other[0], other), TrackRole::Other);
    QCOMPARE(tracknames::friendlyNames(other), QStringList({"Scarlett 2i2"}));
}

void TracksTest::duplicateRolesDisambiguated()
{
    const QVector<Track> tracks{
        mk("m1", "Trust USB mikrofon", TrackKind::Mic),
        mk("m2", "Webcam C920 - Analóg sztereó", TrackKind::Mic),
        mk("l1", "Monitor of Sennheiser - Communication", TrackKind::Loopback),
        mk("l2", "Monitor of Kanto YU4 - Optikai", TrackKind::Loopback),
        mk("l3", "Monitor of HDMI", TrackKind::Loopback),
    };
    const QStringList names = tracknames::friendlyNames(tracks);
    QCOMPARE(names.at(0), QStringLiteral("Saját mikrofon (Trust USB mikrofon)"));
    QCOMPARE(names.at(1), QStringLiteral("Saját mikrofon (Webcam C920)"));
    QCOMPARE(names.at(2), QStringLiteral("Hívás hangja"));
    QCOMPARE(names.at(3), QStringLiteral("Rendszerhang (Kanto YU4)"));
    QCOMPARE(names.at(4), QStringLiteral("Rendszerhang (HDMI)"));
    QCOMPARE(QSet<QString>(names.begin(), names.end()).size(), 5);   // mind különböző
    // Azonos eszköznév kétszer: sorszám különböztet.
    const QVector<Track> same{mk("a", "USB Mic", TrackKind::Mic), mk("b", "USB Mic", TrackKind::Mic)};
    const QStringList n2 = tracknames::friendlyNames(same);
    QVERIFY(n2.at(0) != n2.at(1));
}

void TracksTest::shortDeviceNames()
{
    QCOMPARE(tracknames::shortDeviceName("Monitor of Kanto YU4 - Optikai Digitális sztereó (IEC958)"),
             QStringLiteral("Kanto YU4"));
    QCOMPARE(tracknames::shortDeviceName("Hangszórók (Realtek(R) Audio)"), QStringLiteral("Hangszórók (Realtek(R) Audio)"));
    QCOMPARE(tracknames::shortDeviceName("Hangszórók (Realtek Audio)"), QStringLiteral("Realtek Audio"));
    QCOMPARE(tracknames::shortDeviceName("Trust USB mikrofon"), QStringLiteral("Trust USB mikrofon"));
}

// ---- katalógus-műveletek ----------------------------------------------------------------

void TracksTest::viewsAndMissingFile()
{
    Meeting m = makeMeeting({mk("comm", "Monitor of Sennheiser - Communication", TrackKind::Loopback),
                             mk("mic", "Trust USB mikrofon", TrackKind::Mic)});
    QFile::remove(QDir(m.folder).filePath("track_mic.wav"));
    TrackCatalog cat(m_store.get());
    const QVector<TrackView> v = cat.tracks(m.id);
    QCOMPARE(v.size(), 2);
    QCOMPARE(v[0].displayName, QStringLiteral("Hívás hangja"));
    QCOMPARE(v[0].rawDeviceName, QStringLiteral("Monitor of Sennheiser - Communication"));
    QCOMPARE(v[0].role, TrackRole::CallAudio);
    QVERIFY(!v[0].fileMissing);
    QVERIFY(v[0].fileSize > 0);
    QCOMPARE(v[0].durationMs, qint64(-1));              // még nincs hullámforma-cache
    QVERIFY(v[0].absolutePath.startsWith(m.folder));
    QVERIFY(v[1].fileMissing);                          // „hiányzik a fájl”
    QCOMPARE(v[1].displayName, QStringLiteral("Saját mikrofon"));
    QVERIFY(cat.tracks(QStringLiteral("nincs-ilyen")).isEmpty());
}

void TracksTest::renamePersists()
{
    const Meeting m = makeMeeting({mk("mic", "Trust USB mikrofon", TrackKind::Mic)});
    TrackCatalog cat(m_store.get());
    QSignalSpy changed(&cat, &TrackCatalog::tracksChanged);
    QVERIFY(cat.renameTrack(m.id, "mic", "  Tárgyaló   asztali mikrofon "));
    QCOMPARE(changed.count(), 1);
    QVERIFY(!cat.renameTrack(m.id, "mic", "Tárgyaló asztali mikrofon"));   // nincs változás
    QVERIFY(!cat.renameTrack(m.id, "nincs", "x"));

    // Új store + katalógus (újraindítás): a név a meeting.json-ból jön.
    MeetingStore store2(m_dir->filePath("rec"), m_dir->filePath("meta"));
    TrackCatalog cat2(&store2);
    TrackView v = cat2.tracks(m.id).first();
    QCOMPARE(v.displayName, QStringLiteral("Tárgyaló asztali mikrofon"));
    QCOMPARE(v.friendlyName, QStringLiteral("Saját mikrofon"));
    QCOMPARE(v.rawDeviceName, QStringLiteral("Trust USB mikrofon"));     // a nyers név megmarad
    QVERIFY(v.renamed);
    QCOMPARE(store2.load(m.id).tracks.first().customName, QStringLiteral("Tárgyaló asztali mikrofon"));

    // Üres név (vagy maga a barátságos név) → vissza az alapértelmezettre.
    QVERIFY(cat2.renameTrack(m.id, "mic", "Saját mikrofon"));
    v = cat2.tracks(m.id).first();
    QVERIFY(!v.renamed);
    QCOMPARE(v.displayName, QStringLiteral("Saját mikrofon"));
    // Régi (customName nélküli) JSON olvasása: üres marad, és üresen nem is íródik ki.
    QVERIFY(!toJson(store2.load(m.id).tracks.first()).contains("customName"));
}

void TracksTest::relocateCopiesIntoFolder()
{
    Meeting m = makeMeeting({mk("mic", "Trust USB mikrofon", TrackKind::Mic)});
    m.mixdownDirty = false;
    m_store->saveMeeting(m);
    const QString orig = QDir(m.folder).filePath("track_mic.wav");
    // A felvétel „elkóborolt”: a fájl egy külső mappában van más néven.
    const QString outside = m_dir->filePath("valahol/felvetel-masolat.wav");
    QDir().mkpath(m_dir->filePath("valahol"));
    QVERIFY(QFile::rename(orig, outside));

    TrackCatalog cat(m_store.get());
    QVERIFY(cat.tracks(m.id).first().fileMissing);
    QSignalSpy changed(&cat, &TrackCatalog::tracksChanged);
    QString err;
    QVERIFY2(cat.relocateTrack(m.id, "mic", outside, &err), qPrintable(err));
    QCOMPARE(changed.count(), 1);

    const TrackView v = cat.tracks(m.id).first();
    QVERIFY(!v.fileMissing);
    QCOMPARE(v.track.file, QStringLiteral("track_mic.wav"));     // az eredeti név, a mappában
    QVERIFY(QFile::exists(orig));
    QVERIFY(QFile::exists(outside));                             // az eredeti érintetlen
    QVERIFY(m_store->load(m.id).mixdownDirty);                   // aktív sáv → a keverék elavult

    // A mappában lévő (más nevű) fájl: másolás nélkül hivatkozunk rá.
    const QString inFolder = QDir(m.folder).filePath("track_mic_v2.wav");
    QVERIFY(QFile::copy(outside, inFolder));
    QVERIFY(cat.relocateTrack(m.id, "mic", inFolder, &err));
    QCOMPARE(cat.tracks(m.id).first().track.file, QStringLiteral("track_mic_v2.wav"));
}

void TracksTest::relocateRejectsBadFiles()
{
    const Meeting m = makeMeeting({mk("mic", "Mic", TrackKind::Mic), mk("sys", "Monitor of X", TrackKind::Loopback)});
    TrackCatalog cat(m_store.get());
    QSignalSpy changed(&cat, &TrackCatalog::tracksChanged);
    QString err;
    QVERIFY(!cat.relocateTrack(m.id, "mic", m_dir->filePath("nincs.wav"), &err));
    QVERIFY(!err.isEmpty());
    QFile empty(m_dir->filePath("ures.wav"));
    QVERIFY(empty.open(QIODevice::WriteOnly));
    empty.close();
    QVERIFY(!cat.relocateTrack(m.id, "mic", empty.fileName(), &err));
    // Másik sáv fájlját nem lehet „megtalálni” ehhez a sávhoz.
    QVERIFY(!cat.relocateTrack(m.id, "mic", QDir(m.folder).filePath("track_sys.wav"), &err));
    QVERIFY(!cat.relocateTrack(m.id, "nincs-sav", QDir(m.folder).filePath("track_mic.wav"), &err));
    QCOMPARE(changed.count(), 0);
    QCOMPARE(m_store->load(m.id).tracks.first().file, QStringLiteral("track_mic.wav"));   // nincs változás
}

void TracksTest::deleteDroppedTracksAtOnce()
{
    Meeting m = makeMeeting({mk("comm", "Monitor of Sennheiser - Communication", TrackKind::Loopback),
                             mk("d1", "Monitor of Kanto", TrackKind::Loopback, false, 0.0f),
                             mk("mic", "Mic", TrackKind::Mic),
                             mk("d2", "Monitor of HDMI", TrackKind::Loopback, false, 0.0f)});
    m.mixdownDirty = false;
    m_store->saveMeeting(m);
    const QDir folder(m.folder);
    // Hullámforma-cache az egyik eldobott sávhoz (annak is el kell tűnnie).
    QFile cache(WaveformService::cachePath(folder.filePath("track_d1.wav")));
    QVERIFY(cache.open(QIODevice::WriteOnly));
    cache.write("{}");
    cache.close();

    TrackCatalog cat(m_store.get());
    QCOMPARE(cat.droppedTrackCount(m.id), 2);
    QSignalSpy changed(&cat, &TrackCatalog::tracksChanged);
    QCOMPARE(cat.deleteDroppedTracks(m.id), 2);
    QCOMPARE(changed.count(), 1);                              // egy művelet → egy jel

    const Meeting after = m_store->load(m.id);
    QCOMPARE(after.tracks.size(), 2);
    QCOMPARE(after.tracks[0].id, QStringLiteral("comm"));
    QCOMPARE(after.tracks[1].id, QStringLiteral("mic"));
    QVERIFY(!folder.exists("track_d1.wav"));
    QVERIFY(!folder.exists("track_d2.wav"));
    QVERIFY(!QFile::exists(cache.fileName()));
    QVERIFY(folder.exists("track_comm.wav"));                  // az aktív felvételek megmaradnak
    QVERIFY(folder.exists("track_mic.wav"));
    QVERIFY(!after.mixdownDirty);                              // eldobott sáv nem volt a keverékben
    QCOMPARE(cat.deleteDroppedTracks(m.id), 0);
    QCOMPARE(changed.count(), 1);
}

// ---- hullámforma ------------------------------------------------------------------------

void TracksTest::peaksComputedCachedAndReused()
{
    if (!haveFfmpeg()) QSKIP("ffmpeg nem található");
    const Meeting m = makeMeeting({mk("mic", "Mic", TrackKind::Mic)}, false);
    const QString path = QDir(m.folder).filePath("track_mic.wav");
    writeWav(path, 30, 0.5);

    QVERIFY(!WaveformService::loadCached(path).isValid());
    WaveformService svc;
    QSignalSpy ready(&svc, &WaveformService::peaksReady);
    QSignalSpy failed(&svc, &WaveformService::peaksFailed);
    QElapsedTimer clock;
    clock.start();
    svc.request(m.id, "mic", path);
    QVERIFY2(clock.elapsed() < 100, "a request() blokkolt");   // aszinkron
    QVERIFY(svc.isPending(path));
    svc.request(m.id, "mic", path);                            // dupla kérés → egy számítás
    QVERIFY(ready.wait(20000));
    QCOMPARE(ready.count(), 1);
    QCOMPARE(failed.count(), 0);

    QCOMPARE(ready.at(0).at(0).toString(), m.id);
    QCOMPARE(ready.at(0).at(1).toString(), QStringLiteral("mic"));
    const TrackPeaks p = ready.at(0).at(2).value<TrackPeaks>();
    QCOMPARE(p.trackId, QStringLiteral("mic"));
    QCOMPARE(p.peaks.size(), WaveformService::kBuckets);       // „pár száz vödör”
    QVERIFY(qAbs(p.durationMs - 30000) < 100);                 // a dekódolt, pontos hossz
    // Az első fele csend, a második fele ~0.5 amplitúdójú szinusz.
    QVERIFY(p.peaks.at(50) < 0.02f);
    QVERIFY(p.peaks.at(150) < 0.02f);
    QVERIFY(qAbs(p.peaks.at(250) - 0.5f) < 0.05f);
    QVERIFY(qAbs(p.peaks.at(390) - 0.5f) < 0.05f);

    // Gyorsítótár a meeting mappájában; a következő kérés számítás nélkül, ugyanazt adja.
    QVERIFY(QFile::exists(WaveformService::cachePath(path)));
    QVERIFY(WaveformService::cachePath(path).startsWith(m.folder));
    const TrackPeaks cached = WaveformService::loadCached(path);
    QVERIFY(cached.isValid());
    QCOMPARE(cached.peaks, p.peaks);
    QCOMPARE(cached.durationMs, p.durationMs);

    svc.request(m.id, "mic", path);
    QVERIFY(!svc.isPending(path));                             // nem indult ffmpeg
    QVERIFY(ready.wait(2000));
    QCOMPARE(ready.count(), 2);
    QCOMPARE(ready.at(1).at(2).value<TrackPeaks>().peaks, p.peaks);

    // A katalógus a cache-ből tudja a sáv hosszát.
    TrackCatalog cat(m_store.get());
    QCOMPARE(cat.tracks(m.id).first().durationMs, p.durationMs);
}

void TracksTest::peaksInvalidatedWhenFileChanges()
{
    if (!haveFfmpeg()) QSKIP("ffmpeg nem található");
    const Meeting m = makeMeeting({mk("mic", "Mic", TrackKind::Mic)}, false);
    const QString path = QDir(m.folder).filePath("track_mic.wav");
    writeWav(path, 4, 0.25);
    WaveformService svc;
    QSignalSpy ready(&svc, &WaveformService::peaksReady);
    svc.request(m.id, "mic", path);
    QVERIFY(ready.wait(20000));
    QVERIFY(WaveformService::loadCached(path).isValid());

    writeWav(path, 6, 0.8);                                    // a fájl lecserélődött
    QVERIFY(!WaveformService::loadCached(path).isValid());     // a cache elavult
    svc.request(m.id, "mic", path);
    QVERIFY(ready.wait(20000));
    const TrackPeaks p = ready.at(1).at(2).value<TrackPeaks>();
    QVERIFY(qAbs(p.durationMs - 6000) < 100);
    QVERIFY(qAbs(p.peaks.last() - 0.8f) < 0.05f);

    WaveformService::removeCache(path);
    QVERIFY(!QFile::exists(WaveformService::cachePath(path)));
}

void TracksTest::peaksFailForBrokenFile()
{
    if (!haveFfmpeg()) QSKIP("ffmpeg nem található");
    WaveformService svc;
    QSignalSpy ready(&svc, &WaveformService::peaksReady);
    QSignalSpy failed(&svc, &WaveformService::peaksFailed);
    // Hiányzó fájl.
    svc.request("m1", "t1", m_dir->filePath("nincs.ogg"));
    QVERIFY(failed.wait(2000));
    // Nem hangfájl.
    const QString junk = m_dir->filePath("szemet.ogg");
    QFile f(junk);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("ez nem hang");
    f.close();
    svc.request("m1", "t2", junk);
    QVERIFY(failed.wait(20000));
    QCOMPARE(failed.count(), 2);
    QCOMPARE(ready.count(), 0);
    QVERIFY(!QFile::exists(WaveformService::cachePath(junk)));

    // Megszakítás: nincs jel, nincs cache.
    const QString big = m_dir->filePath("nagy.wav");
    writeWav(big, 600);
    svc.request("m2", "t", big);
    svc.cancel("m2");
    QTest::qWait(500);
    QCOMPARE(failed.count(), 2);
    QCOMPARE(ready.count(), 0);
    QVERIFY(!svc.isPending(big));
}

void TracksTest::resample()
{
    const QVector<float> src{0.1f, 0.9f, 0.2f, 0.3f, 0.0f, 0.5f};
    QCOMPARE(WaveformService::resample(src, 3), QVector<float>({0.9f, 0.3f, 0.5f}));   // max-tartó
    QCOMPARE(WaveformService::resample(src, 6), src);
    QCOMPARE(WaveformService::resample(src, 12).size(), 12);
    QCOMPARE(WaveformService::resample(src, 12).at(2), 0.9f);
    QVERIFY(WaveformService::resample({}, 10).isEmpty());
}

QTEST_GUILESS_MAIN(TracksTest)
#include "test_tracks.moc"
