// media::MediaTool: the ffmpeg command lines (spec -> argv, the former call sites' commands
// verbatim), the -progress / ffprobe-JSON parsers, the tool locator's order (temporary folders and
// PATH, the Windows ".exe" rule), the settings section, the FakeMediaTool, and a real ffmpeg round
// (decode / probe / encode / filter graph; QSKIP without ffmpeg).
#include <QtTest>
#include <QTemporaryDir>

#include "fake_media_tool.h"
#include "tanara/media/FfmpegMediaTool.h"
#include "tanara/media/MediaSettings.h"
#include "tanara/media/MediaToolLocator.h"

using namespace tanara;
using namespace tanara::media;

namespace {

#if defined(Q_OS_WIN)
constexpr bool kWindows = true;
const QString kExe = QStringLiteral(".exe");
#else
constexpr bool kWindows = false;
const QString kExe;
#endif

QString makeExe(const QString& dir, const QString& name)
{
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(name);
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write("#!/bin/sh\nexit 0\n");
    f.close();
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return path;
}

} // namespace

class MediaToolTest : public QObject {
    Q_OBJECT
private slots:
    void decodeArgs()
    {
        DecodeSpec s;
        s.path = "/a/b.ogg";
        s.format = PcmFormat{16000, 1, SampleFormat::S16};
        QCOMPARE(ffmpegargs::decode(s),
                 QStringList({"-v", "error", "-i", "/a/b.ogg", "-vn", "-ac", "1", "-ar", "16000", "-f", "s16le", "-"}));
        s.range = TimeRange{1500, 4250};
        s.format = PcmFormat{16000, 1, SampleFormat::F32};
        QCOMPARE(ffmpegargs::decode(s),
                 QStringList({"-v", "error", "-ss", "1.500", "-i", "/a/b.ogg", "-t", "2.750", "-vn",
                              "-ac", "1", "-ar", "16000", "-f", "f32le", "-"}));
        s.range = TimeRange{0, 800};   // from the start: no -ss
        s.format = PcmFormat{8000, 1, SampleFormat::S16};
        QCOMPARE(ffmpegargs::decode(s),
                 QStringList({"-v", "error", "-i", "/a/b.ogg", "-t", "0.800", "-vn", "-ac", "1", "-ar", "8000",
                              "-f", "s16le", "-"}));
    }

    void encodeArgs()
    {
        EncodeSpec e;
        e.input = PcmFormat{48000, 2, SampleFormat::S16};
        e.outputPath = "/m/track_mic.ogg";
        e.bitrateKbps = 96;
        // The recorder's former command (RecordingSession), verbatim.
        QCOMPARE(ffmpegargs::encode(e),
                 QStringList({"-hide_banner", "-loglevel", "error", "-f", "s16le", "-ar", "48000", "-ac", "2",
                              "-i", "pipe:0", "-c:a", "libopus", "-b:a", "96k", "-y", "/m/track_mic.ogg"}));
    }

    void transcodeArgs()
    {
        // The importer's former command (AudioImporter::encodeArgs), verbatim: whole file -> stereo.
        TranscodeSpec t;
        t.inputPath = "/src/a.mp3";
        t.bitrateKbps = 64;
        t.outputs = {TranscodeOutput{"/st/track_a.ogg", -1, 2}};
        const QStringList head{"-hide_banner", "-loglevel", "error", "-nostdin", "-y", "-progress", "pipe:1",
                               "-nostats", "-i", "/src/a.mp3"};
        QCOMPARE(ffmpegargs::transcode(t).arguments,
                 head + QStringList({"-map", "0:a:0", "-ac", "2", "-map_metadata", "-1", "-ar", "48000",
                                     "-c:a", "libopus", "-b:a", "64k", "/st/track_a.ogg"}));
        // Split per channel: one read, asplit + pan.
        t.outputs = {TranscodeOutput{"/st/l.ogg", 0, 1}, TranscodeOutput{"/st/r.ogg", 1, 1}};
        QCOMPARE(ffmpegargs::transcode(t).arguments,
                 head + QStringList({"-filter_complex",
                                     "[0:a:0]asplit=2[s0][s1];[s0]pan=mono|c0=c0[o0];[s1]pan=mono|c0=c1[o1]",
                                     "-map", "[o0]", "-map_metadata", "-1", "-ar", "48000", "-c:a", "libopus",
                                     "-b:a", "64k", "/st/l.ogg",
                                     "-map", "[o1]", "-map_metadata", "-1", "-ar", "48000", "-c:a", "libopus",
                                     "-b:a", "64k", "/st/r.ogg"}));
    }

    void probeArgsAndJson()
    {
        QCOMPARE(ffmpegargs::probe("/x.wav"),
                 QStringList({"-v", "error", "-print_format", "json", "-show_format", "-show_streams", "/x.wav"}));
        const QByteArray json = R"({"streams":[
            {"index":0,"codec_type":"video","codec_name":"mjpeg","disposition":{"attached_pic":1}},
            {"index":1,"codec_type":"audio","codec_name":"mp3","channels":2,"sample_rate":"44100",
             "duration":"3.5","tags":{"creation_time":"2024-01-02T03:04:05Z"}}],
            "format":{"duration":"3.512","tags":{"title":"x"}}})";
        const std::optional<MediaInfo> info = ffmpegargs::parseProbeJson(json);
        QVERIFY(info);
        QCOMPARE(info->streams.size(), 2);
        QCOMPARE(info->durationMs(), qint64(3512));
        QVERIFY(!info->hasVideo());                 // cover art only
        QVERIFY(info->firstAudio());
        QCOMPARE(info->firstAudio()->channels, 2);
        QCOMPARE(info->firstAudio()->sampleRate, 44100);
        QCOMPARE(info->firstAudio()->codecName, QStringLiteral("mp3"));
        QCOMPARE(info->formatTags.value("title").toString(), QStringLiteral("x"));
        QVERIFY(!ffmpegargs::parseProbeJson("not json"));
    }

    void progressParser()
    {
        QByteArray carry;
        QCOMPARE(ffmpegargs::parseProgress(carry, "frame=1\nout_time_us=1500000\nout_time_ms=2000"), qint64(1500));
        QCOMPARE(carry, QByteArray("out_time_ms=2000"));   // incomplete line kept
        QCOMPARE(ffmpegargs::parseProgress(carry, "000\nprogress=continue\n"), qint64(2000));
        QCOMPARE(ffmpegargs::parseProgress(carry, "out_time_us=N/A\n"), qint64(-1));
        QVERIFY(carry.isEmpty());
    }

    void locatorOrder()
    {
        QTemporaryDir tmp;
        const QString app = tmp.filePath("app/bin");
        const QString appimage = tmp.filePath("appimage");
        const QString pathDir = tmp.filePath("path");
        const QString ffmpegName = QStringLiteral("ffmpeg") + kExe;
        const QString configured = makeExe(tmp.filePath("custom"), QStringLiteral("my-ffmpeg") + kExe);
        makeExe(app, ffmpegName);
        makeExe(QDir(appimage).filePath("usr/bin"), ffmpegName);
        makeExe(pathDir, ffmpegName);
        LocatorEnv env;
        env.windows = kWindows;   // the Windows rule: only <tool>.exe counts as executable
        env.appDir = app;
        env.appImageDir = appimage;
        env.pathDirs = {pathDir};

        // 1. settings
        ToolResolution r = MediaToolLocator::locate("ffmpeg", configured, env);
        QCOMPARE(r.source, ToolSource::Settings);
        QCOMPARE(r.path, configured);
        // a configured path that does not exist: falls through to 2. the app folder
        r = MediaToolLocator::locate("ffmpeg", tmp.filePath("nope/ffmpeg") + kExe, env);
        QCOMPARE(r.source, ToolSource::AppDir);
        QCOMPARE(r.path, QDir(app).filePath(ffmpegName));
        // 3. bundle (AppImage)
        QFile::remove(QDir(app).filePath(ffmpegName));
        r = MediaToolLocator::locate("ffmpeg", {}, env);
        QCOMPARE(r.source, ToolSource::Bundle);
        QCOMPARE(r.path, QDir(appimage).filePath(QStringLiteral("usr/bin/") + ffmpegName));
        // 4. PATH
        env.appImageDir.clear();
        r = MediaToolLocator::locate("ffmpeg", {}, env);
        QCOMPARE(r.source, ToolSource::Path);
        QCOMPARE(QFileInfo(r.path).canonicalFilePath(), QFileInfo(QDir(pathDir).filePath(ffmpegName)).canonicalFilePath());
        // not found anywhere
        QFile::remove(QDir(pathDir).filePath(ffmpegName));
        env.pathDirs = {tmp.filePath("empty")};
        r = MediaToolLocator::locate("ffmpeg", {}, env);
        QVERIFY(!r.found());
        // a non-executable file does not count (on Windows executability is the extension, not a mode bit)
#if !defined(Q_OS_WIN)
        QFile plain(QDir(app).filePath(ffmpegName));
        QVERIFY(plain.open(QIODevice::WriteOnly));
        plain.close();
        plain.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        QVERIFY(!MediaToolLocator::locate("ffmpeg", {}, env).found());
#endif
    }

    void locatorBundlesAndWindows()
    {
        LocatorEnv env;
        env.appDir = "/Apps/Tanara.app/Contents/MacOS";
        env.macos = true;
        QCOMPARE(MediaToolLocator::candidates("ffprobe", "~/bin/ffprobe", env),
                 QStringList({QDir::homePath() + "/bin/ffprobe", "/Apps/Tanara.app/Contents/MacOS/ffprobe",
                              "/Apps/Tanara.app/Contents/Resources/bin/ffprobe",
                              "/Apps/Tanara.app/Contents/MacOS/ffprobe"}));
        LocatorEnv win;
        win.appDir = "C:/Program Files/Tanara";
        win.windows = true;
        QCOMPARE(MediaToolLocator::executableName("ffmpeg", true), QStringLiteral("ffmpeg.exe"));
        QCOMPARE(MediaToolLocator::executableName("ffmpeg.exe", true), QStringLiteral("ffmpeg.exe"));
        QCOMPARE(MediaToolLocator::executableName("ffmpeg", false), QStringLiteral("ffmpeg"));
        QCOMPARE(MediaToolLocator::candidates("ffmpeg", {}, win),
                 QStringList({"C:/Program Files/Tanara/ffmpeg.exe"}));
        // Windows rule with real files: only the .exe is found in the app folder.
        QTemporaryDir tmp;
        makeExe(tmp.path(), "ffmpeg");
        LocatorEnv w2;
        w2.appDir = tmp.path();
        w2.windows = true;
        w2.pathDirs = {tmp.filePath("none")};
        QVERIFY(!MediaToolLocator::locate("ffmpeg", {}, w2).found());
        makeExe(tmp.path(), "ffmpeg.exe");
        QCOMPARE(MediaToolLocator::locate("ffmpeg", {}, w2).path, QDir(tmp.path()).filePath("ffmpeg.exe"));
    }

    void settingsSection()
    {
        MediaSettings s;
        QVERIFY(s.ffmpegPath.isEmpty() && s.ffprobePath.isEmpty());   // automatic by default
        s.ffmpegPath = "/opt/ff/ffmpeg";
        const MediaSettings back = MediaSettings::fromJson(s.toJson());
        QCOMPARE(back, s);
        QCOMPARE(MediaSettings::fromJson({}), MediaSettings());
    }

    void missingToolFailsToStart()
    {
        FfmpegMediaTool tool(ToolResolution{"/nonexistent/ffmpeg-x", ToolSource::Settings},
                             ToolResolution{"/nonexistent/ffprobe-x", ToolSource::Settings});
        DecodeSpec spec;
        spec.path = "/whatever.wav";
        const MediaResult r = tool.decodePcm(spec, {});
        QVERIFY(!r.started);
        QVERIFY(!tool.probe("/whatever.wav").started);
        QVERIFY(!tool.startEncoder(EncodeSpec{}));
        QObject owner;
        MediaJob* job = tool.runFilterGraph(FilterGraphSpec{{"-version"}}, &owner);
        QSignalSpy finished(job, &MediaJob::finished);
        job->start();
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(!finished.at(0).at(0).value<MediaResult>().started);
    }

    void fakeTool()
    {
        testing::FakeMediaTool fake;
        QVector<qint16> pcm(16000);
        for (int i = 0; i < pcm.size(); ++i) pcm[i] = qint16(i % 100);
        fake.pcm["a.ogg"] = pcm;
        // The s16 decoder helper: whole file, odd chunks reassembled.
        QVector<qint16> got;
        QString error;
        QVERIFY(fake.monoS16Decoder(16000)("/x/a.ogg", [&got](const qint16* s, qsizetype n) {
            for (qsizetype i = 0; i < n; ++i) got.append(s[i]);
        }, &error));
        QCOMPARE(got, pcm);
        QVERIFY(!fake.monoS16Decoder(16000)("/x/none.ogg", [](const qint16*, qsizetype) {}, &error));
        // The float helper with a range.
        const QVector<float> f = fake.decodeMonoF32("/x/a.ogg", 500, 750);
        QCOMPARE(f.size(), 4000);
        QCOMPARE(f.first(), float(8000 % 100) / 32768.0f);
        QCOMPARE(fake.decodes().size(), 3);
        QCOMPARE(fake.decodes().last().format.sampleFormat, SampleFormat::F32);
    }

    // The real adapter end-to-end (QSKIP without ffmpeg / ffprobe).
    void realFfmpegRoundTrip()
    {
        const auto tool = FfmpegMediaTool::fromSettings(MediaSettings{});
        if (!tool->available()) QSKIP("nincs ffmpeg/ffprobe");
        QVERIFY(tool->version().startsWith("ffmpeg"));
        QTemporaryDir dir;
        const QString ogg = dir.filePath("enc.ogg");
        // Encoder: 1 s of 48 kHz mono silence -> Opus.
        {
            auto enc = tool->startEncoder(EncodeSpec{PcmFormat{48000, 1, SampleFormat::S16}, ogg, 32});
            QVERIFY(enc);
            QVERIFY(enc->isRunning());
            const QByteArray silence(48000 * 2, '\0');
            enc->write(silence.constData(), silence.size());
            QVERIFY(enc->finish(30000));
        }
        QVERIFY(QFileInfo(ogg).size() > 0);
        // Probe (sync + async).
        const MediaResult p = tool->probe(ogg);
        QVERIFY(p.ok() && p.info);
        QVERIFY(p.info->firstAudio());
        QVERIFY(std::abs(p.info->durationMs() - 1000) < 100);
        QObject owner;
        MediaJob* pj = tool->startProbe(ogg, &owner);
        QSignalSpy probed(pj, &MediaJob::finished);
        pj->start();
        QTRY_COMPARE(probed.size(), 1);
        QVERIFY(probed.at(0).at(0).value<MediaResult>().info.has_value());
        // Sync decode (16 kHz s16, ~1 s) and a cancelled one.
        DecodeSpec d;
        d.path = ogg;
        qint64 bytes = 0;
        const MediaResult r = tool->decodePcm(d, [&bytes](const char*, qsizetype n) { QVERIFY(n % 2 == 0); bytes += n; });
        QVERIFY(r.ok());
        QVERIFY(std::abs(bytes - 32000) < 3200);
        QCOMPARE(r.bytes, bytes);
        CancelToken token;
        token.cancel();
        QVERIFY(tool->decodePcm(d, {}, token).cancelled);
        // Async decode.
        MediaJob* dj = tool->startDecode(d, &owner);
        qint64 asyncBytes = 0;
        connect(dj, &MediaJob::output, this, [&asyncBytes](const QByteArray& b) { asyncBytes += b.size(); });
        QSignalSpy decoded(dj, &MediaJob::finished);
        dj->start();
        QTRY_COMPARE(decoded.size(), 1);
        QVERIFY(decoded.at(0).at(0).value<MediaResult>().ok());
        QCOMPARE(asyncBytes, bytes);
        // Filter graph with progress (transcode).
        TranscodeSpec t;
        t.inputPath = ogg;
        t.outputs = {TranscodeOutput{dir.filePath("out.ogg"), -1, 1}};
        MediaJob* gj = tool->runFilterGraph(ffmpegargs::transcode(t), &owner);
        QSignalSpy progress(gj, &MediaJob::progress);
        QSignalSpy graphDone(gj, &MediaJob::finished);
        gj->start();
        QTRY_COMPARE(graphDone.size(), 1);
        QVERIFY(graphDone.at(0).at(0).value<MediaResult>().ok());
        QVERIFY(!progress.isEmpty());
        QVERIFY(QFileInfo(dir.filePath("out.ogg")).size() > 0);
    }
};

QTEST_GUILESS_MAIN(MediaToolTest)
#include "test_media_tool.moc"
