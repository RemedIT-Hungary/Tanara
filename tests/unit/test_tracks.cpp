// TrackCatalog + WaveformService: the "Tracks" tab: friendly names from the device's role,
// renaming, locating a missing file, deleting dropped tracks, waveform peaks (asynchronous
// ffmpeg + a cache in the meeting's folder).
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QtEndian>
#include <cmath>

#include "fake_media_tool.h"
#include "tanara/library/tracks/TrackCatalog.h"
#include "tanara/media/FfmpegMediaTool.h"
#include "tanara/library/tracks/WaveformService.h"
#include "tanara/library/store/MeetingStore.h"
#include "tanara/library/store/JsonSerialization.h"

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

// 16-bit mono WAV: the first half is silence, the second half a sine (amplitude: amp).
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
    void segmentsOfOneDevice();
    void segmentsRenameTogether();

    void viewsAndMissingFile();
    void renamePersists();
    void relocateCopiesIntoFolder();
    void relocateRejectsBadFiles();
    void deleteDroppedTracksAtOnce();

    void peaksComputedCachedAndReused();
    void peaksInvalidatedWhenFileChanges();
    void peaksFailForBrokenFile();
    void resample();
    void peaksWithFakeMediaTool();

private:
    Meeting makeMeeting(const QVector<Track>& tracks, bool createFiles = true);
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<library::MeetingStore> m_store;
};

void TracksTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_store = std::make_unique<library::MeetingStore>(m_dir->filePath("rec"), m_dir->filePath("meta"));
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

// ---- friendly names ----

void TracksTest::friendlyNamesByRole()
{
    // The real setup: the headset's "Communication" monitor + another output + a USB microphone.
    const QVector<Track> tracks{
        mk("comm", "Monitor of Sennheiser - Communication", TrackKind::Loopback, true, 0.5f),
        mk("mic", "Trust USB mikrofon", TrackKind::Mic, true, 0.7f),
        mk("kanto", "Monitor of Kanto YU4 - Optikai Digitális sztereó (IEC958)", TrackKind::Loopback, false, 0.0f),
    };
    QCOMPARE(library::tracknames::classify(tracks[0], tracks), library::TrackRole::CallAudio);
    QCOMPARE(library::tracknames::classify(tracks[1], tracks), library::TrackRole::OwnMic);
    QCOMPARE(library::tracknames::classify(tracks[2], tracks), library::TrackRole::SystemAudio);
    QCOMPARE(library::tracknames::friendlyNames(tracks),
             QStringList({"Hívás hangja", "Saját mikrofon", "Rendszerhang"}));
    QVERIFY(library::tracknames::looksLikeCallDevice("Headset Earphone (Jabra) Hands-Free AG Audio"));
    QVERIFY(!library::tracknames::looksLikeCallDevice("Monitor of Kanto YU4 - Optikai Digitális sztereó (IEC958)"));
}

void TracksTest::callDeviceByLoudnessWhenNameSilent()
{
    // No name suggests a call device: the single active monitor is the call audio ...
    QVector<Track> one{
        mk("a", "Monitor of Beépített hang - Analóg sztereó", TrackKind::Loopback, true, 0.3f),
        mk("b", "Monitor of HDMI kimenet", TrackKind::Loopback, false, 0.0f),
    };
    QCOMPARE(library::tracknames::classify(one[0], one), library::TrackRole::CallAudio);
    QCOMPARE(library::tracknames::classify(one[1], one), library::TrackRole::SystemAudio);
    // ... among several active ones the loudest.
    QVector<Track> two{
        mk("a", "Monitor of Beépített hang", TrackKind::Loopback, true, 0.1f),
        mk("b", "Monitor of USB DAC", TrackKind::Loopback, true, 0.6f),
    };
    QCOMPARE(library::tracknames::classify(two[0], two), library::TrackRole::SystemAudio);
    QCOMPARE(library::tracknames::classify(two[1], two), library::TrackRole::CallAudio);
    // Line input / unknown: the device name stays.
    QVector<Track> other{mk("aux", "Line In (Scarlett 2i2)", TrackKind::Other)};
    QCOMPARE(library::tracknames::classify(other[0], other), library::TrackRole::Other);
    QCOMPARE(library::tracknames::friendlyNames(other), QStringList({"Scarlett 2i2"}));
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
    const QStringList names = library::tracknames::friendlyNames(tracks);
    QCOMPARE(names.at(0), QStringLiteral("Saját mikrofon (Trust USB mikrofon)"));
    QCOMPARE(names.at(1), QStringLiteral("Saját mikrofon (Webcam C920)"));
    QCOMPARE(names.at(2), QStringLiteral("Hívás hangja"));
    QCOMPARE(names.at(3), QStringLiteral("Rendszerhang (Kanto YU4)"));
    QCOMPARE(names.at(4), QStringLiteral("Rendszerhang (HDMI)"));
    QCOMPARE(QSet<QString>(names.begin(), names.end()).size(), 5);   // all different
    // The same device name twice: an ordinal tells them apart.
    const QVector<Track> same{mk("a", "USB Mic", TrackKind::Mic), mk("b", "USB Mic", TrackKind::Mic)};
    const QStringList n2 = library::tracknames::friendlyNames(same);
    QVERIFY(n2.at(0) != n2.at(1));
}

void TracksTest::shortDeviceNames()
{
    QCOMPARE(library::tracknames::shortDeviceName("Monitor of Kanto YU4 - Optikai Digitális sztereó (IEC958)"),
             QStringLiteral("Kanto YU4"));
    QCOMPARE(library::tracknames::shortDeviceName("Hangszórók (Realtek(R) Audio)"), QStringLiteral("Hangszórók (Realtek(R) Audio)"));
    QCOMPARE(library::tracknames::shortDeviceName("Hangszórók (Realtek Audio)"), QStringLiteral("Realtek Audio"));
    QCOMPARE(library::tracknames::shortDeviceName("Trust USB mikrofon"), QStringLiteral("Trust USB mikrofon"));
}

// ---- catalog operations ----

// ---- one device's segments ----

void TracksTest::segmentsOfOneDevice()
{
    // The system audio on at 57.2 s, off later, on again at 300 s: two files. Meanwhile another
    // loopback (HDMI) plays too, more quietly. The second segment is at the END of meeting.json.
    Track sys1 = mk("sys", "Monitor of Speakers", TrackKind::Loopback, true, 0.3f);
    sys1.startOffsetMs = 57200;
    Track sys2 = mk("sys-2", "Monitor of Speakers", TrackKind::Loopback, false, 0.0f);   // quiet, dropped
    sys2.startOffsetMs = 300000;
    Track sys3 = mk("sys-3", "Monitor of Speakers", TrackKind::Loopback, true, 0.6f);
    sys3.startOffsetMs = 400000;
    const QVector<Track> tracks{
        mk("mic", "Trust USB mikrofon", TrackKind::Mic),
        sys3,
        sys1,
        mk("hdmi", "Monitor of HDMI", TrackKind::Loopback, true, 0.1f),
        sys2,
    };
    const QVector<library::tracknames::SegmentInfo> seg = library::tracknames::segments(tracks);
    QCOMPARE(seg.at(0).segment, 0);
    QCOMPARE(seg.at(0).count, 1);
    QCOMPARE(seg.at(2).leader, 2);                     // the earliest segment leads
    QCOMPARE(seg.at(2).segment, 1);
    QCOMPARE(seg.at(4).leader, 2);
    QCOMPARE(seg.at(4).segment, 2);
    QCOMPARE(seg.at(1).leader, 2);
    QCOMPARE(seg.at(1).segment, 3);
    QCOMPARE(seg.at(1).count, 3);
    QCOMPARE(seg.at(3).segment, 0);

    // Shared role and name: the segments together are the loudest loopback, so the call audio;
    // the dropped segment too (not "System audio" in the middle of a call).
    for (int i : {1, 2, 4})
        QCOMPARE(library::tracknames::classify(tracks[i], tracks), library::TrackRole::CallAudio);
    QCOMPARE(library::tracknames::classify(tracks[3], tracks), library::TrackRole::SystemAudio);
    const QStringList names = library::tracknames::friendlyNames(tracks);
    QCOMPARE(names.at(1), QStringLiteral("Hívás hangja"));
    QCOMPARE(names.at(2), QStringLiteral("Hívás hangja"));
    QCOMPARE(names.at(4), QStringLiteral("Hívás hangja"));
    QCOMPARE(names.at(3), QStringLiteral("Rendszerhang"));

    // Order on the Tracks tab: per logical track, within that by offset.
    Meeting m;
    m.folder = m_dir->path();
    m.tracks = tracks;
    const QVector<library::TrackView> views = library::TrackCatalog::tracks(m);
    QStringList ids;
    for (const library::TrackView& v : views) ids << v.track.id;
    QCOMPARE(ids, QStringList({"mic", "sys", "sys-2", "sys-3", "hdmi"}));
    QCOMPARE(views.at(1).segment, 1);
    QCOMPARE(views.at(3).segment, 3);
    QCOMPARE(views.at(3).segmentCount, 3);
    QCOMPARE(views.at(0).segment, 0);

    // Same-named devices at the same time (two identical microphones): separate tracks, not segments.
    QVector<Track> twins{mk("a", "USB Mic", TrackKind::Mic), mk("b", "USB Mic", TrackKind::Mic)};
    QCOMPARE(library::tracknames::segments(twins).at(1).leader, 1);
    QCOMPARE(library::tracknames::segments(twins).at(1).segment, 0);
    // ... but if one started later, two segments of one device.
    twins[1].startOffsetMs = 5000;
    QCOMPARE(library::tracknames::segments(twins).at(1).leader, 0);
    const QStringList tn = library::tracknames::friendlyNames(twins);
    QCOMPARE(tn.at(0), tn.at(1));
}

void TracksTest::segmentsRenameTogether()
{
    Track a = mk("sys", "Monitor of Speakers", TrackKind::Loopback);
    Track b = mk("sys-2", "Monitor of Speakers", TrackKind::Loopback);
    b.startOffsetMs = 12000;
    const Meeting m = makeMeeting({mk("mic", "Trust USB mikrofon", TrackKind::Mic), a, b});
    library::TrackCatalog cat(m_store.get());
    QVERIFY(cat.renameTrack(m.id, "sys-2", "Teams"));
    const Meeting back = m_store->load(m.id);
    QCOMPARE(back.tracks.at(1).customName, QStringLiteral("Teams"));
    QCOMPARE(back.tracks.at(2).customName, QStringLiteral("Teams"));
    QCOMPARE(back.tracks.at(0).customName, QString());
    // Back to the friendly name: on both segments.
    QVERIFY(cat.renameTrack(m.id, "sys", QString()));
    for (const library::TrackView& v : cat.tracks(m.id)) QVERIFY(!v.renamed);
}

void TracksTest::viewsAndMissingFile()
{
    Meeting m = makeMeeting({mk("comm", "Monitor of Sennheiser - Communication", TrackKind::Loopback),
                             mk("mic", "Trust USB mikrofon", TrackKind::Mic)});
    QFile::remove(QDir(m.folder).filePath("track_mic.wav"));
    library::TrackCatalog cat(m_store.get());
    const QVector<library::TrackView> v = cat.tracks(m.id);
    QCOMPARE(v.size(), 2);
    QCOMPARE(v[0].displayName, QStringLiteral("Hívás hangja"));
    QCOMPARE(v[0].rawDeviceName, QStringLiteral("Monitor of Sennheiser - Communication"));
    QCOMPARE(v[0].role, library::TrackRole::CallAudio);
    QVERIFY(!v[0].fileMissing);
    QVERIFY(v[0].fileSize > 0);
    QCOMPARE(v[0].durationMs, qint64(-1));              // no waveform cache yet
    QVERIFY(v[0].absolutePath.startsWith(m.folder));
    QVERIFY(v[1].fileMissing);                          // "file missing"
    QCOMPARE(v[1].displayName, QStringLiteral("Saját mikrofon"));
    QVERIFY(cat.tracks(QStringLiteral("nincs-ilyen")).isEmpty());
}

void TracksTest::renamePersists()
{
    const Meeting m = makeMeeting({mk("mic", "Trust USB mikrofon", TrackKind::Mic)});
    library::TrackCatalog cat(m_store.get());
    QSignalSpy changed(&cat, &library::TrackCatalog::tracksChanged);
    QVERIFY(cat.renameTrack(m.id, "mic", "  Tárgyaló   asztali mikrofon "));
    QCOMPARE(changed.count(), 1);
    QVERIFY(!cat.renameTrack(m.id, "mic", "Tárgyaló asztali mikrofon"));   // no change
    QVERIFY(!cat.renameTrack(m.id, "nincs", "x"));

    // A new store + catalog (a restart): the name comes from meeting.json.
    library::MeetingStore store2(m_dir->filePath("rec"), m_dir->filePath("meta"));
    library::TrackCatalog cat2(&store2);
    library::TrackView v = cat2.tracks(m.id).first();
    QCOMPARE(v.displayName, QStringLiteral("Tárgyaló asztali mikrofon"));
    QCOMPARE(v.friendlyName, QStringLiteral("Saját mikrofon"));
    QCOMPARE(v.rawDeviceName, QStringLiteral("Trust USB mikrofon"));     // the raw name stays
    QVERIFY(v.renamed);
    QCOMPARE(store2.load(m.id).tracks.first().customName, QStringLiteral("Tárgyaló asztali mikrofon"));

    // An empty name (or the friendly name itself) goes back to the default.
    QVERIFY(cat2.renameTrack(m.id, "mic", "Saját mikrofon"));
    v = cat2.tracks(m.id).first();
    QVERIFY(!v.renamed);
    QCOMPARE(v.displayName, QStringLiteral("Saját mikrofon"));
    // Reading an old JSON (without customName): stays empty, and is not written out when empty.
    QVERIFY(!library::toJson(store2.load(m.id).tracks.first()).contains("customName"));
}

void TracksTest::relocateCopiesIntoFolder()
{
    Meeting m = makeMeeting({mk("mic", "Trust USB mikrofon", TrackKind::Mic)});
    m.mixdownDirty = false;
    m_store->saveMeeting(m);
    const QString orig = QDir(m.folder).filePath("track_mic.wav");
    // The recording "wandered off": the file is in an outside folder under another name.
    const QString outside = m_dir->filePath("valahol/felvetel-masolat.wav");
    QDir().mkpath(m_dir->filePath("valahol"));
    QVERIFY(QFile::rename(orig, outside));

    library::TrackCatalog cat(m_store.get());
    QVERIFY(cat.tracks(m.id).first().fileMissing);
    QSignalSpy changed(&cat, &library::TrackCatalog::tracksChanged);
    QString err;
    QVERIFY2(cat.relocateTrack(m.id, "mic", outside, &err), qPrintable(err));
    QCOMPARE(changed.count(), 1);

    const library::TrackView v = cat.tracks(m.id).first();
    QVERIFY(!v.fileMissing);
    QCOMPARE(v.track.file, QStringLiteral("track_mic.wav"));     // the original name, in the folder
    QVERIFY(QFile::exists(orig));
    QVERIFY(QFile::exists(outside));                             // the original is untouched
    QVERIFY(m_store->load(m.id).mixdownDirty);                   // an active track, so the mix is stale

    // A file in the folder (with another name): we reference it without copying.
    const QString inFolder = QDir(m.folder).filePath("track_mic_v2.wav");
    QVERIFY(QFile::copy(outside, inFolder));
    QVERIFY(cat.relocateTrack(m.id, "mic", inFolder, &err));
    QCOMPARE(cat.tracks(m.id).first().track.file, QStringLiteral("track_mic_v2.wav"));
}

void TracksTest::relocateRejectsBadFiles()
{
    const Meeting m = makeMeeting({mk("mic", "Mic", TrackKind::Mic), mk("sys", "Monitor of X", TrackKind::Loopback)});
    library::TrackCatalog cat(m_store.get());
    QSignalSpy changed(&cat, &library::TrackCatalog::tracksChanged);
    QString err;
    QVERIFY(!cat.relocateTrack(m.id, "mic", m_dir->filePath("nincs.wav"), &err));
    QVERIFY(!err.isEmpty());
    QFile empty(m_dir->filePath("ures.wav"));
    QVERIFY(empty.open(QIODevice::WriteOnly));
    empty.close();
    QVERIFY(!cat.relocateTrack(m.id, "mic", empty.fileName(), &err));
    // Another track's file cannot be "found" for this track.
    QVERIFY(!cat.relocateTrack(m.id, "mic", QDir(m.folder).filePath("track_sys.wav"), &err));
    QVERIFY(!cat.relocateTrack(m.id, "nincs-sav", QDir(m.folder).filePath("track_mic.wav"), &err));
    QCOMPARE(changed.count(), 0);
    QCOMPARE(m_store->load(m.id).tracks.first().file, QStringLiteral("track_mic.wav"));   // no change
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
    // The waveform cache for one dropped track (it must disappear too).
    QFile cache(library::WaveformService::cachePath(folder.filePath("track_d1.wav")));
    QVERIFY(cache.open(QIODevice::WriteOnly));
    cache.write("{}");
    cache.close();

    library::TrackCatalog cat(m_store.get());
    QCOMPARE(cat.droppedTrackCount(m.id), 2);
    QSignalSpy changed(&cat, &library::TrackCatalog::tracksChanged);
    QCOMPARE(cat.deleteDroppedTracks(m.id), 2);
    QCOMPARE(changed.count(), 1);                              // one operation, one signal

    const Meeting after = m_store->load(m.id);
    QCOMPARE(after.tracks.size(), 2);
    QCOMPARE(after.tracks[0].id, QStringLiteral("comm"));
    QCOMPARE(after.tracks[1].id, QStringLiteral("mic"));
    QVERIFY(!folder.exists("track_d1.wav"));
    QVERIFY(!folder.exists("track_d2.wav"));
    QVERIFY(!QFile::exists(cache.fileName()));
    QVERIFY(folder.exists("track_comm.wav"));                  // the active recordings stay
    QVERIFY(folder.exists("track_mic.wav"));
    QVERIFY(!after.mixdownDirty);                              // the dropped track was not in the mix
    QCOMPARE(cat.deleteDroppedTracks(m.id), 0);
    QCOMPARE(changed.count(), 1);
}

// ---- waveform ----

void TracksTest::peaksComputedCachedAndReused()
{
    if (!haveFfmpeg()) QSKIP("ffmpeg nem található");
    const Meeting m = makeMeeting({mk("mic", "Mic", TrackKind::Mic)}, false);
    const QString path = QDir(m.folder).filePath("track_mic.wav");
    writeWav(path, 30, 0.5);

    QVERIFY(!library::WaveformService::loadCached(path).isValid());
    library::WaveformService svc(media::FfmpegMediaTool::fromSettings({}));
    QSignalSpy ready(&svc, &library::WaveformService::peaksReady);
    QSignalSpy failed(&svc, &library::WaveformService::peaksFailed);
    QElapsedTimer clock;
    clock.start();
    svc.request(m.id, "mic", path);
    QVERIFY2(clock.elapsed() < 100, "a request() blokkolt");   // asynchronous
    QVERIFY(svc.isPending(path));
    svc.request(m.id, "mic", path);                            // double request: one computation
    QVERIFY(ready.wait(20000));
    QCOMPARE(ready.count(), 1);
    QCOMPARE(failed.count(), 0);

    QCOMPARE(ready.at(0).at(0).toString(), m.id);
    QCOMPARE(ready.at(0).at(1).toString(), QStringLiteral("mic"));
    const library::TrackPeaks p = ready.at(0).at(2).value<library::TrackPeaks>();
    QCOMPARE(p.trackId, QStringLiteral("mic"));
    QCOMPARE(p.peaks.size(), library::WaveformService::kBuckets);       // "a few hundred buckets"
    QVERIFY(qAbs(p.durationMs - 30000) < 100);                 // the decoded, exact length
    // The first half is silence, the second half a sine of ~0.5 amplitude.
    QVERIFY(p.peaks.at(50) < 0.02f);
    QVERIFY(p.peaks.at(150) < 0.02f);
    QVERIFY(qAbs(p.peaks.at(250) - 0.5f) < 0.05f);
    QVERIFY(qAbs(p.peaks.at(390) - 0.5f) < 0.05f);

    // Cache in the meeting's folder; the next request gives the same without computing.
    QVERIFY(QFile::exists(library::WaveformService::cachePath(path)));
    QVERIFY(library::WaveformService::cachePath(path).startsWith(m.folder));
    const library::TrackPeaks cached = library::WaveformService::loadCached(path);
    QVERIFY(cached.isValid());
    QCOMPARE(cached.peaks, p.peaks);
    QCOMPARE(cached.durationMs, p.durationMs);

    svc.request(m.id, "mic", path);
    QVERIFY(!svc.isPending(path));                             // no ffmpeg started
    QVERIFY(ready.wait(2000));
    QCOMPARE(ready.count(), 2);
    QCOMPARE(ready.at(1).at(2).value<library::TrackPeaks>().peaks, p.peaks);

    // The catalog knows the track's length from the cache.
    library::TrackCatalog cat(m_store.get());
    QCOMPARE(cat.tracks(m.id).first().durationMs, p.durationMs);
}

void TracksTest::peaksInvalidatedWhenFileChanges()
{
    if (!haveFfmpeg()) QSKIP("ffmpeg nem található");
    const Meeting m = makeMeeting({mk("mic", "Mic", TrackKind::Mic)}, false);
    const QString path = QDir(m.folder).filePath("track_mic.wav");
    writeWav(path, 4, 0.25);
    library::WaveformService svc(media::FfmpegMediaTool::fromSettings({}));
    QSignalSpy ready(&svc, &library::WaveformService::peaksReady);
    svc.request(m.id, "mic", path);
    QVERIFY(ready.wait(20000));
    QVERIFY(library::WaveformService::loadCached(path).isValid());

    writeWav(path, 6, 0.8);                                    // the file was replaced
    QVERIFY(!library::WaveformService::loadCached(path).isValid());     // the cache is stale
    svc.request(m.id, "mic", path);
    QVERIFY(ready.wait(20000));
    const library::TrackPeaks p = ready.at(1).at(2).value<library::TrackPeaks>();
    QVERIFY(qAbs(p.durationMs - 6000) < 100);
    QVERIFY(qAbs(p.peaks.last() - 0.8f) < 0.05f);

    library::WaveformService::removeCache(path);
    QVERIFY(!QFile::exists(library::WaveformService::cachePath(path)));
}

void TracksTest::peaksFailForBrokenFile()
{
    if (!haveFfmpeg()) QSKIP("ffmpeg nem található");
    library::WaveformService svc(media::FfmpegMediaTool::fromSettings({}));
    QSignalSpy ready(&svc, &library::WaveformService::peaksReady);
    QSignalSpy failed(&svc, &library::WaveformService::peaksFailed);
    // Missing file.
    svc.request("m1", "t1", m_dir->filePath("nincs.ogg"));
    QVERIFY(failed.wait(2000));
    // Not an audio file.
    const QString junk = m_dir->filePath("szemet.ogg");
    QFile f(junk);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("ez nem hang");
    f.close();
    svc.request("m1", "t2", junk);
    QVERIFY(failed.wait(20000));
    QCOMPARE(failed.count(), 2);
    QCOMPARE(ready.count(), 0);
    QVERIFY(!QFile::exists(library::WaveformService::cachePath(junk)));

    // Cancel: no signal, no cache.
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
    QCOMPARE(library::WaveformService::resample(src, 3), QVector<float>({0.9f, 0.3f, 0.5f}));   // max-hold
    QCOMPARE(library::WaveformService::resample(src, 6), src);
    QCOMPARE(library::WaveformService::resample(src, 12).size(), 12);
    QCOMPARE(library::WaveformService::resample(src, 12).at(2), 0.9f);
    QVERIFY(library::WaveformService::resample({}, 10).isEmpty());
}

// The waveform without ffmpeg: the media port is a fake (preset 8 kHz PCM).
void TracksTest::peaksWithFakeMediaTool()
{
    auto tool = std::make_shared<testing::FakeMediaTool>();
    QVector<qint16> pcm(8000 * 4, 0);                 // 4 s: 2 s silence, 2 s at half scale
    for (int i = 8000 * 2; i < pcm.size(); ++i) pcm[i] = (i % 2) ? 16384 : -16384;
    tool->pcm["track_mic.ogg"] = pcm;
    const Meeting m = makeMeeting({mk("mic", "Mic", TrackKind::Mic)}, false);
    const QString path = QDir(m.folder).filePath("track_mic.ogg");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();
    library::WaveformService svc(tool);
    QSignalSpy ready(&svc, &library::WaveformService::peaksReady);
    QSignalSpy failed(&svc, &library::WaveformService::peaksFailed);
    svc.request(m.id, "mic", path);
    QVERIFY(ready.wait(5000));
    QCOMPARE(failed.count(), 0);
    const library::TrackPeaks p = ready.at(0).at(2).value<library::TrackPeaks>();
    QCOMPARE(p.durationMs, qint64(4000));
    QCOMPARE(p.peaks.size(), 80);                     // 4 s of 50 ms windows (fewer than kBuckets)
    QVERIFY(p.peaks.first() < 0.01f);
    QVERIFY(qAbs(p.peaks.last() - 0.5f) < 0.01f);
    QCOMPARE(tool->decodes().size(), 1);
    QCOMPARE(tool->decodes().first().format.sampleRate, 8000);
    // An undecodable file fails (the fake knows no such PCM).
    const QString other = QDir(m.folder).filePath("track_x.ogg");
    QFile g(other);
    QVERIFY(g.open(QIODevice::WriteOnly));
    g.write("x");
    g.close();
    svc.request(m.id, "x", other);
    QVERIFY(failed.wait(5000));
}

QTEST_GUILESS_MAIN(TracksTest)
#include "test_tracks.moc"
