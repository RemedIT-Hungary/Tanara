#pragma once
//
// Value types of the media port (MediaTool.h): PCM formats, decode / encode / transcode / filter-graph
// specs, probe results and the outcome of one external-tool run. Pure data, no process handling.
#include <QByteArray>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace tanara::media {

enum class SampleFormat { S16, F32 };   // signed 16-bit / 32-bit float, little endian, interleaved

struct PcmFormat {
    int sampleRate = 16000;
    int channels = 1;
    SampleFormat sampleFormat = SampleFormat::S16;

    int bytesPerSample() const { return sampleFormat == SampleFormat::F32 ? 4 : 2; }
    int bytesPerFrame() const { return bytesPerSample() * (channels > 0 ? channels : 1); }
    bool operator==(const PcmFormat&) const = default;
};

// A time window of the source file, in the FILE's own time. endMs <= startMs = until the end;
// startMs <= 0 = from the beginning.
struct TimeRange {
    qint64 startMs = 0;
    qint64 endMs = 0;
    bool operator==(const TimeRange&) const = default;
};

// Decoding a (segment of a) file into raw PCM on stdout.
struct DecodeSpec {
    QString path;
    TimeRange range;
    PcmFormat format;
    // Synchronous decode only: overall time limit (0 = none); exceeded -> killed, MediaResult::timedOut.
    int timeoutMs = 0;
};

// Encoding raw PCM written to the encoder's stdin into an Opus (.ogg) file (the recorder).
struct EncodeSpec {
    PcmFormat input{48000, 1, SampleFormat::S16};
    QString outputPath;
    int bitrateKbps = 64;
};

// One output of a transcode: a mono channel of the source (channel >= 0) or the whole first audio
// stream down-mixed to `channels` (channel < 0).
struct TranscodeOutput {
    QString path;
    int channel = -1;
    int channels = 1;
};

// Transcoding the first audio stream of one source into Opus/ogg track files (the importer). Several
// outputs = one read of the source, split per channel.
struct TranscodeSpec {
    QString inputPath;
    QVector<TranscodeOutput> outputs;
    int bitrateKbps = 64;
    int sampleRate = 48000;
};

// A complete ffmpeg run described by its argument list (without the program name). The planners
// produce it (recording::MixdownPlan::ffmpegArgs, media::ffmpegargs::transcode); the port runs it
// asynchronously and reports the progress (`-progress pipe:1` lines on stdout).
struct FilterGraphSpec {
    QStringList arguments;
};

// One stream of a probed file (ffprobe -show_streams).
struct MediaStream {
    int index = -1;
    QString codecType;      // "audio" | "video" | ...
    QString codecName;
    int channels = 0;
    int sampleRate = 0;
    double durationSec = 0.0;
    bool attachedPic = false;   // cover art (disposition.attached_pic), not real video
    QJsonObject tags;
};

// The probed file (ffprobe -show_format -show_streams).
struct MediaInfo {
    double durationSec = 0.0;   // format.duration (0 = unknown)
    QJsonObject formatTags;
    QVector<MediaStream> streams;

    // format.duration in ms, truncated (0 = unknown).
    qint64 durationMs() const { return qint64(durationSec * 1000.0); }
    // The first audio stream, or nullptr.
    const MediaStream* firstAudio() const;
    // Whether there is a real video stream (cover art does not count).
    bool hasVideo() const;
};

// The outcome of one external-tool run (synchronous or asynchronous).
struct MediaResult {
    bool started = false;      // false: the tool could not be started (missing / not executable)
    bool timedOut = false;     // killed after the time limit
    bool cancelled = false;    // stopped by cancel() / the CancelToken
    bool normalExit = false;   // the process exited by itself (not killed / crashed)
    int exitCode = -1;
    qint64 bytes = 0;          // decode: PCM bytes delivered
    QByteArray stdErr;
    std::optional<MediaInfo> info;   // probe: the parsed output (only for a clean exit)

    bool ok() const { return started && !timedOut && !cancelled && normalExit && exitCode == 0; }
    QString errorText() const { return QString::fromUtf8(stdErr).trimmed(); }
};

// Raw PCM chunks of a synchronous decode, always whole frames (format.bytesPerFrame()).
using RawPcmSink = std::function<void(const char* data, qsizetype bytes)>;

// The decoded PCM arrives continuously (mono s16): the decoder calls this piece by piece.
using PcmSink = std::function<void(const qint16* samples, qsizetype count)>;
// Decoding one file into the sink. false = failed (*error filled, if not null).
using PcmDecoder = std::function<bool(const QString& absPath, const PcmSink& sink, QString* error)>;

// Cooperative cancel of a synchronous operation (copyable; the copies share the flag).
class CancelToken {
public:
    CancelToken() : m_flag(std::make_shared<std::atomic<bool>>(false)) {}
    void cancel() const { m_flag->store(true); }
    bool isCancelled() const { return m_flag->load(); }

private:
    std::shared_ptr<std::atomic<bool>> m_flag;
};

} // namespace tanara::media

Q_DECLARE_METATYPE(tanara::media::MediaResult)
