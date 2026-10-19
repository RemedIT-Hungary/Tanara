#include "tanara/media/FfmpegMediaTool.h"

#include "tanara/foundation/log/Logging.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QPointer>
#include <QProcess>

namespace tanara::media {

// ---- command lines -----------------------------------------------------------------------------

namespace ffmpegargs {

QStringList probe(const QString& path)
{
    return {QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-print_format"), QStringLiteral("json"),
            QStringLiteral("-show_format"), QStringLiteral("-show_streams"), path};
}

QStringList decode(const DecodeSpec& spec)
{
    QStringList args{QStringLiteral("-v"), QStringLiteral("error")};
    // INPUT seek: -ss BEFORE -i, so decoding starts at the sought position (fast, constant even for a
    // long file), not from 0. The length is limited with -t.
    if (spec.range.startMs > 0)
        args << QStringLiteral("-ss") << QString::number(spec.range.startMs / 1000.0, 'f', 3);
    args << QStringLiteral("-i") << spec.path;
    if (spec.range.endMs > spec.range.startMs)
        args << QStringLiteral("-t")
             << QString::number((spec.range.endMs - spec.range.startMs) / 1000.0, 'f', 3);
    args << QStringLiteral("-vn")
         << QStringLiteral("-ac") << QString::number(spec.format.channels)
         << QStringLiteral("-ar") << QString::number(spec.format.sampleRate)
         << QStringLiteral("-f")
         << (spec.format.sampleFormat == SampleFormat::F32 ? QStringLiteral("f32le") : QStringLiteral("s16le"))
         << QStringLiteral("-");
    return args;
}

QStringList encode(const EncodeSpec& spec)
{
    return {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-f"),
            spec.input.sampleFormat == SampleFormat::F32 ? QStringLiteral("f32le") : QStringLiteral("s16le"),
            QStringLiteral("-ar"), QString::number(spec.input.sampleRate),
            QStringLiteral("-ac"), QString::number(spec.input.channels),
            QStringLiteral("-i"), QStringLiteral("pipe:0"),
            QStringLiteral("-c:a"), QStringLiteral("libopus"),
            QStringLiteral("-b:a"), QString::number(spec.bitrateKbps) + QStringLiteral("k"),
            QStringLiteral("-y"), spec.outputPath};
}

FilterGraphSpec transcode(const TranscodeSpec& spec)
{
    const QString bitrate = QString::number(spec.bitrateKbps) + QLatin1Char('k');
    QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                     QStringLiteral("-nostdin"), QStringLiteral("-y"),
                     QStringLiteral("-progress"), QStringLiteral("pipe:1"), QStringLiteral("-nostats"),
                     QStringLiteral("-i"), spec.inputPath};
    const auto output = [&](const TranscodeOutput& o) {
        args << QStringLiteral("-map_metadata") << QStringLiteral("-1")
             << QStringLiteral("-ar") << QString::number(spec.sampleRate)
             << QStringLiteral("-c:a") << QStringLiteral("libopus")
             << QStringLiteral("-b:a") << bitrate
             << o.path;
    };
    if (spec.outputs.size() == 1 && spec.outputs.first().channel < 0) {
        args << QStringLiteral("-map") << QStringLiteral("0:a:0")
             << QStringLiteral("-ac") << QString::number(spec.outputs.first().channels);
        output(spec.outputs.first());
        return {args};
    }
    // A single read of the source, one mono output per channel.
    QString graph = QStringLiteral("[0:a:0]asplit=%1").arg(spec.outputs.size());
    for (int k = 0; k < spec.outputs.size(); ++k) graph += QStringLiteral("[s%1]").arg(k);
    for (int k = 0; k < spec.outputs.size(); ++k)
        graph += QStringLiteral(";[s%1]pan=mono|c0=c%2[o%1]").arg(k).arg(spec.outputs.at(k).channel);
    args << QStringLiteral("-filter_complex") << graph;
    for (int k = 0; k < spec.outputs.size(); ++k) {
        args << QStringLiteral("-map") << QStringLiteral("[o%1]").arg(k);
        output(spec.outputs.at(k));
    }
    return {args};
}

qint64 parseProgress(QByteArray& carry, const QByteArray& chunk)
{
    carry += chunk;
    qint64 outMs = -1;
    int nl;
    while ((nl = carry.indexOf('\n')) >= 0) {
        const QByteArray line = carry.left(nl).trimmed();
        carry.remove(0, nl + 1);
        const int eq = line.indexOf('=');
        if (eq < 0) continue;
        const QByteArray key = line.left(eq).trimmed();
        if (key != "out_time_us" && key != "out_time_ms") continue;   // (out_time_ms is microseconds too)
        bool ok = false;
        const qint64 us = line.mid(eq + 1).trimmed().toLongLong(&ok);
        if (ok && us >= 0) outMs = us / 1000;
    }
    return outMs;
}

std::optional<MediaInfo> parseProbeJson(const QByteArray& json)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
    const QJsonObject root = doc.object();
    MediaInfo info;
    const QJsonObject format = root.value(QStringLiteral("format")).toObject();
    info.durationSec = format.value(QStringLiteral("duration")).toString().toDouble();
    info.formatTags = format.value(QStringLiteral("tags")).toObject();
    for (const QJsonValue& v : root.value(QStringLiteral("streams")).toArray()) {
        const QJsonObject s = v.toObject();
        MediaStream st;
        st.index = s.value(QStringLiteral("index")).toInt(-1);
        st.codecType = s.value(QStringLiteral("codec_type")).toString();
        st.codecName = s.value(QStringLiteral("codec_name")).toString();
        st.channels = s.value(QStringLiteral("channels")).toInt();
        st.sampleRate = s.value(QStringLiteral("sample_rate")).toString().toInt();
        st.durationSec = s.value(QStringLiteral("duration")).toString().toDouble();
        st.attachedPic = s.value(QStringLiteral("disposition")).toObject()
                             .value(QStringLiteral("attached_pic")).toInt() != 0;
        st.tags = s.value(QStringLiteral("tags")).toObject();
        info.streams.append(st);
    }
    return info;
}

} // namespace ffmpegargs

// ---- asynchronous job --------------------------------------------------------------------------

namespace {

class FfmpegJob final : public MediaJob {
public:
    enum class Mode { Probe, Decode, Graph };

    FfmpegJob(Mode mode, QString program, QStringList args, QObject* parent)
        : MediaJob(parent), m_mode(mode), m_program(std::move(program)), m_args(std::move(args)) {}

    ~FfmpegJob() override { abort(3000); }

    void start() override
    {
        if (m_proc || m_done) return;
        m_proc = new QProcess(this);
        connect(m_proc, &QProcess::readyReadStandardOutput, this, [this] { consume(m_proc->readAllStandardOutput()); });
        connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            consume(m_proc->readAllStandardOutput());
            MediaResult r;
            r.started = true;
            r.normalExit = status == QProcess::NormalExit;
            r.exitCode = code;
            r.cancelled = m_cancelled;
            r.bytes = m_bytes;
            r.stdErr = m_proc->readAllStandardError();
            if (m_mode == Mode::Probe && r.normalExit && code == 0) r.info = ffmpegargs::parseProbeJson(m_out);
            finish(r);
        });
        connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart) return;   // otherwise finished follows
            MediaResult r;
            r.cancelled = m_cancelled;
            finish(r);
        });
        m_proc->start(m_program, m_args);
    }

    void cancel() override
    {
        m_cancelled = true;
        if (m_proc && m_proc->state() != QProcess::NotRunning) m_proc->kill();   // finished follows
    }

    void abort(int waitMs) override
    {
        m_done = true;
        if (!m_proc) return;
        m_proc->disconnect(this);
        if (m_proc->state() != QProcess::NotRunning) {
            m_proc->kill();
            m_proc->waitForFinished(waitMs);
        }
    }

    bool isRunning() const override { return m_proc && !m_done && m_proc->state() != QProcess::NotRunning; }

private:
    void consume(const QByteArray& data)
    {
        if (data.isEmpty() || m_done) return;
        switch (m_mode) {
        case Mode::Probe:
            m_out += data;
            break;
        case Mode::Decode:
            m_bytes += data.size();
            emit output(data);
            break;
        case Mode::Graph: {
            const qint64 ms = ffmpegargs::parseProgress(m_carry, data);
            if (ms >= 0) emit progress(ms);
            break;
        }
        }
    }

    void finish(const MediaResult& r)
    {
        if (m_done) return;
        m_done = true;
        emit finished(r);
    }

    Mode m_mode;
    QString m_program;
    QStringList m_args;
    QProcess* m_proc = nullptr;
    QByteArray m_out;       // probe: the JSON
    QByteArray m_carry;     // graph: incomplete progress line
    qint64 m_bytes = 0;
    bool m_cancelled = false;
    bool m_done = false;
};

class FfmpegEncoder final : public EncoderHandle {
public:
    explicit FfmpegEncoder(std::unique_ptr<QProcess> proc) : m_proc(std::move(proc)) {}
    ~FfmpegEncoder() override = default;   // ~QProcess kills and reaps a still running encoder

    bool isRunning() const override { return m_proc->state() == QProcess::Running; }
    void write(const char* data, qint64 bytes) override
    {
        if (m_proc->state() == QProcess::Running) m_proc->write(data, bytes);
    }
    bool finish(int timeoutMs) override
    {
        m_proc->closeWriteChannel();
        if (!m_proc->waitForFinished(timeoutMs)) {
            m_proc->kill();
            m_proc->waitForFinished(2000);
        }
        return m_proc->exitStatus() == QProcess::NormalExit && m_proc->exitCode() == 0;
    }
    void kill() override { m_proc->kill(); }

private:
    std::unique_ptr<QProcess> m_proc;
};

} // namespace

// ---- FfmpegMediaTool ---------------------------------------------------------------------------

FfmpegMediaTool::FfmpegMediaTool(ToolResolution ffmpeg, ToolResolution ffprobe)
    : m_ffmpeg(std::move(ffmpeg)), m_ffprobe(std::move(ffprobe))
{
    qRegisterMetaType<tanara::media::MediaResult>();
}

std::shared_ptr<FfmpegMediaTool> FfmpegMediaTool::fromSettings(const MediaSettings& settings)
{
    return fromSettings(settings, LocatorEnv::systemEnv());
}

std::shared_ptr<FfmpegMediaTool> FfmpegMediaTool::fromSettings(const MediaSettings& settings, const LocatorEnv& env)
{
    auto tool = std::make_shared<FfmpegMediaTool>(
        MediaToolLocator::locate(QStringLiteral("ffmpeg"), settings.ffmpegPath, env),
        MediaToolLocator::locate(QStringLiteral("ffprobe"), settings.ffprobePath, env));
    const auto describe = [](const char* name, const ToolResolution& r) {
        return r.found() ? QStringLiteral("%1=%2 (%3)").arg(QLatin1String(name), r.path, MediaToolLocator::sourceName(r.source))
                         : QStringLiteral("%1=NINCS MEG").arg(QLatin1String(name));
    };
    // The startup diagnostics (app::logStartupDiagnostics) logs the result with the version at INFO level.
    if (tool->available())
        qCDebug(lcAudio).noquote() << "Media tools:" << describe("ffmpeg", tool->m_ffmpeg) << describe("ffprobe", tool->m_ffprobe);
    else
        qCWarning(lcAudio).noquote() << "Media tools:" << describe("ffmpeg", tool->m_ffmpeg) << describe("ffprobe", tool->m_ffprobe);
    return tool;
}

bool FfmpegMediaTool::available() const { return m_ffmpeg.found() && m_ffprobe.found(); }

QString FfmpegMediaTool::ffmpegPath() const
{
    return m_ffmpeg.found() ? m_ffmpeg.path : QStringLiteral("ffmpeg");
}

QString FfmpegMediaTool::ffprobePath() const
{
    return m_ffprobe.found() ? m_ffprobe.path : QStringLiteral("ffprobe");
}

QString FfmpegMediaTool::version() const
{
    QMutexLocker lock(&m_versionLock);
    if (m_versionRead) return m_version;
    m_versionRead = true;
    QProcess p;
    p.start(ffmpegPath(), {QStringLiteral("-hide_banner"), QStringLiteral("-version")});
    if (p.waitForStarted(5000) && p.waitForFinished(5000))
        m_version = QString::fromUtf8(p.readAllStandardOutput()).section(QLatin1Char('\n'), 0, 0).trimmed();
    else
        p.kill();
    return m_version;
}

MediaResult FfmpegMediaTool::probe(const QString& path, int timeoutMs) const
{
    MediaResult r;
    QProcess p;
    p.start(ffprobePath(), ffmpegargs::probe(path));
    if (!p.waitForStarted(5000)) return r;
    r.started = true;
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(2000);
        r.timedOut = true;
        return r;
    }
    r.normalExit = p.exitStatus() == QProcess::NormalExit;
    r.exitCode = p.exitCode();
    r.stdErr = p.readAllStandardError();
    if (r.normalExit && r.exitCode == 0) r.info = ffmpegargs::parseProbeJson(p.readAllStandardOutput());
    return r;
}

MediaJob* FfmpegMediaTool::startProbe(const QString& path, QObject* parent) const
{
    return new FfmpegJob(FfmpegJob::Mode::Probe, ffprobePath(), ffmpegargs::probe(path), parent);
}

MediaResult FfmpegMediaTool::decodePcm(const DecodeSpec& spec, const RawPcmSink& sink, const CancelToken& cancel) const
{
    MediaResult r;
    QProcess proc;
    proc.start(ffmpegPath(), ffmpegargs::decode(spec));
    if (!proc.waitForStarted(5000)) return r;
    r.started = true;
    QElapsedTimer clock;
    clock.start();
    // Streaming read: the PCM does not accumulate here, only an incomplete frame carries over.
    const qsizetype frame = spec.format.bytesPerFrame();
    QByteArray carry;
    auto pump = [&]() {
        QByteArray chunk = proc.readAllStandardOutput();
        if (chunk.isEmpty()) return;
        if (!carry.isEmpty()) { chunk.prepend(carry); carry.clear(); }
        const qsizetype usable = chunk.size() - chunk.size() % frame;
        if (usable < chunk.size()) carry = chunk.mid(usable);
        if (usable <= 0) return;
        if (sink) sink(chunk.constData(), usable);
        r.bytes += usable;
    };
    bool killed = false;
    while (proc.state() != QProcess::NotRunning) {
        proc.waitForReadyRead(200);
        pump();
        if (cancel.isCancelled()) { r.cancelled = true; killed = true; }
        else if (spec.timeoutMs > 0 && clock.elapsed() > spec.timeoutMs) { r.timedOut = true; killed = true; }
        if (killed) {
            proc.kill();
            proc.waitForFinished(2000);
            break;
        }
    }
    if (!killed) pump();
    r.normalExit = !killed && proc.exitStatus() == QProcess::NormalExit;
    r.exitCode = killed ? -1 : proc.exitCode();
    r.stdErr = proc.readAllStandardError();
    return r;
}

MediaJob* FfmpegMediaTool::startDecode(const DecodeSpec& spec, QObject* parent) const
{
    return new FfmpegJob(FfmpegJob::Mode::Decode, ffmpegPath(), ffmpegargs::decode(spec), parent);
}

std::unique_ptr<EncoderHandle> FfmpegMediaTool::startEncoder(const EncodeSpec& spec) const
{
    auto proc = std::make_unique<QProcess>();   // lives on the calling thread
    proc->start(ffmpegPath(), ffmpegargs::encode(spec));
    if (!proc->waitForStarted(5000)) return nullptr;
    return std::make_unique<FfmpegEncoder>(std::move(proc));
}

MediaJob* FfmpegMediaTool::runFilterGraph(const FilterGraphSpec& spec, QObject* parent) const
{
    return new FfmpegJob(FfmpegJob::Mode::Graph, ffmpegPath(), spec.arguments, parent);
}

MediaJob* FfmpegMediaTool::startTranscode(const TranscodeSpec& spec, QObject* parent) const
{
    return runFilterGraph(ffmpegargs::transcode(spec), parent);
}

} // namespace tanara::media
