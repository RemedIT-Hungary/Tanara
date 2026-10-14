#pragma once
// FakeMediaTool: a MediaTool for the tests, without ffmpeg. Decoding gives preset mono s16 PCM per
// file name (converted to the requested sample format, sliced by the requested range at the requested
// rate); probing gives preset MediaInfo; the encoder / filter graph record their specs and write a
// small output file. Every call is recorded (thread-safe), so a test can check what was asked.
#include "tanara/media/MediaTool.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>

#include <cstring>

namespace tanara::testing {

class FakeMediaTool final : public media::MediaTool {
public:
    // ---- configuration (set before use) -----------------------------------------------------
    QHash<QString, QVector<qint16>> pcm;          // file NAME -> mono s16 samples
    QHash<QString, media::MediaInfo> infos;       // file NAME -> probe result
    bool graphOk = true;                          // runFilterGraph: exit code 0?
    qint64 graphProgressMs = 500;                 // runFilterGraph: one progress report (ms); <0 = none
    bool encoderStarts = true;                    // startEncoder: false = cannot be started
    qsizetype chunkBytes = 998;                   // decode chunk size (whole frames are guaranteed by the sink)

    // ---- recorded calls ----------------------------------------------------------------------
    QStringList probedPaths() const { QMutexLocker l(&m_lock); return m_probed; }
    QVector<media::DecodeSpec> decodes() const { QMutexLocker l(&m_lock); return m_decodes; }
    QVector<media::EncodeSpec> encodes() const { QMutexLocker l(&m_lock); return m_encodes; }
    QVector<media::FilterGraphSpec> graphs() const { QMutexLocker l(&m_lock); return m_graphs; }

    bool available() const override { return true; }
    QString version() const override { return QStringLiteral("fake"); }
    QString ffmpegPath() const override { return QStringLiteral("fake-ffmpeg"); }
    QString ffprobePath() const override { return QStringLiteral("fake-ffprobe"); }

    media::MediaResult probe(const QString& path, int /*timeoutMs*/ = 30000) const override
    {
        { QMutexLocker l(&m_lock); m_probed << path; }
        media::MediaResult r;
        r.started = true;
        r.normalExit = true;
        const QString name = QFileInfo(path).fileName();
        r.exitCode = infos.contains(name) ? 0 : 1;
        if (r.exitCode == 0) r.info = infos.value(name);
        return r;
    }

    media::MediaJob* startProbe(const QString& path, QObject* parent) const override
    {
        const media::MediaResult r = probe(path);
        return new Job(parent, {}, -1, r);
    }

    media::MediaResult decodePcm(const media::DecodeSpec& spec, const media::RawPcmSink& sink,
                                 const media::CancelToken& cancel = {}) const override
    {
        { QMutexLocker l(&m_lock); m_decodes << spec; }
        media::MediaResult r;
        r.started = true;
        const QByteArray bytes = render(spec);
        if (bytes.isNull()) {
            r.normalExit = true;
            r.exitCode = 1;
            r.stdErr = "no such fake file";
            return r;
        }
        const qsizetype frame = spec.format.bytesPerFrame();
        const qsizetype step = std::max<qsizetype>(frame, chunkBytes - chunkBytes % frame);
        for (qsizetype at = 0; at < bytes.size(); at += step) {
            if (cancel.isCancelled()) { r.cancelled = true; return r; }
            const qsizetype n = std::min(step, bytes.size() - at);
            if (sink) sink(bytes.constData() + at, n);
            r.bytes += n;
        }
        r.normalExit = true;
        r.exitCode = 0;
        return r;
    }

    media::MediaJob* startDecode(const media::DecodeSpec& spec, QObject* parent) const override
    {
        { QMutexLocker l(&m_lock); m_decodes << spec; }
        const QByteArray bytes = render(spec);
        media::MediaResult r;
        r.started = true;
        r.normalExit = true;
        r.exitCode = bytes.isNull() ? 1 : 0;
        r.bytes = bytes.size();
        return new Job(parent, bytes, -1, r);
    }

    std::unique_ptr<media::EncoderHandle> startEncoder(const media::EncodeSpec& spec) const override
    {
        { QMutexLocker l(&m_lock); m_encodes << spec; }
        if (!encoderStarts) return nullptr;
        return std::make_unique<Encoder>(spec.outputPath);
    }

    media::MediaJob* runFilterGraph(const media::FilterGraphSpec& spec, QObject* parent) const override
    {
        { QMutexLocker l(&m_lock); m_graphs << spec; }
        media::MediaResult r;
        r.started = true;
        r.normalExit = true;
        r.exitCode = graphOk ? 0 : 1;
        if (graphOk && !spec.arguments.isEmpty()) {
            QFile f(spec.arguments.last());   // the output is the last argument
            if (f.open(QIODevice::WriteOnly)) f.write("fake media output");
        }
        return new Job(parent, {}, graphProgressMs, r);
    }

    // Transcode: recorded as a filter graph (its command line), every output file written.
    media::MediaJob* startTranscode(const media::TranscodeSpec& spec, QObject* parent) const override
    {
        { QMutexLocker l(&m_lock); m_transcodes << spec; }
        if (graphOk)
            for (const media::TranscodeOutput& o : spec.outputs) {
                QFile f(o.path);
                if (f.open(QIODevice::WriteOnly)) f.write("fake media output");
            }
        media::MediaResult r;
        r.started = true;
        r.normalExit = true;
        r.exitCode = graphOk ? 0 : 1;
        return new Job(parent, {}, graphProgressMs, r);
    }
    QVector<media::TranscodeSpec> transcodes() const { QMutexLocker l(&m_lock); return m_transcodes; }

private:
    // The configured PCM in the spec's format and range (null = unknown file).
    QByteArray render(const media::DecodeSpec& spec) const
    {
        const QString name = QFileInfo(spec.path).fileName();
        if (!pcm.contains(name)) return {};
        const QVector<qint16>& all = pcm.value(name);
        const qint64 rate = spec.format.sampleRate;
        const qint64 from = std::clamp<qint64>(spec.range.startMs * rate / 1000, 0, all.size());
        const qint64 to = spec.range.endMs > spec.range.startMs
                              ? std::clamp<qint64>(spec.range.endMs * rate / 1000, from, all.size())
                              : all.size();
        QByteArray out;
        out.reserve(int((to - from) * spec.format.bytesPerFrame()));
        for (qint64 i = from; i < to; ++i) {
            for (int c = 0; c < std::max(1, spec.format.channels); ++c) {
                if (spec.format.sampleFormat == media::SampleFormat::F32) {
                    const float v = float(all.at(i)) / 32768.0f;
                    out.append(reinterpret_cast<const char*>(&v), sizeof v);
                } else {
                    const qint16 v = all.at(i);
                    out.append(reinterpret_cast<const char*>(&v), sizeof v);
                }
            }
        }
        if (out.isNull()) out = QByteArray("", 0);   // known but empty: not null
        return out;
    }

    // Asynchronous fake run: output / progress / finished in the next event-loop turns.
    class Job final : public media::MediaJob {
    public:
        Job(QObject* parent, QByteArray out, qint64 progressMs, media::MediaResult result)
            : MediaJob(parent), m_out(std::move(out)), m_progressMs(progressMs), m_result(std::move(result)) {}
        void start() override
        {
            m_running = true;
            QTimer::singleShot(0, this, [this] {
                if (!m_running) return;
                for (qsizetype at = 0; at < m_out.size(); at += 4001) emit output(m_out.mid(at, 4001));
                if (m_progressMs >= 0) emit progress(m_progressMs);
                QTimer::singleShot(0, this, [this] { if (m_running) done(); });
            });
        }
        void cancel() override
        {
            if (!m_running) return;
            m_result.cancelled = true;
            m_result.normalExit = false;
            QTimer::singleShot(0, this, [this] { if (m_running) done(); });
        }
        void abort(int) override { m_running = false; }
        bool isRunning() const override { return m_running; }

    private:
        void done() { m_running = false; emit finished(m_result); }
        QByteArray m_out;
        qint64 m_progressMs;
        media::MediaResult m_result;
        bool m_running = false;
    };

    class Encoder final : public media::EncoderHandle {
    public:
        explicit Encoder(QString path) : m_path(std::move(path)) {}
        bool isRunning() const override { return m_running; }
        void write(const char* data, qint64 bytes) override { if (m_running) m_data.append(data, bytes); }
        bool finish(int) override
        {
            if (!m_running) return false;
            m_running = false;
            QFile f(m_path);
            if (!f.open(QIODevice::WriteOnly)) return false;
            f.write(m_data.isEmpty() ? QByteArray("empty") : m_data);
            return true;
        }
        void kill() override { m_running = false; }

    private:
        QString m_path;
        QByteArray m_data;
        bool m_running = true;
    };

    mutable QMutex m_lock;
    mutable QStringList m_probed;
    mutable QVector<media::DecodeSpec> m_decodes;
    mutable QVector<media::EncodeSpec> m_encodes;
    mutable QVector<media::FilterGraphSpec> m_graphs;
    mutable QVector<media::TranscodeSpec> m_transcodes;
};

} // namespace tanara::testing
