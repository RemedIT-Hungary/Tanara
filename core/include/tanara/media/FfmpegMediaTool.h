#pragma once
//
// FfmpegMediaTool: the MediaTool adapter that runs ffmpeg / ffprobe as child processes (QProcess).
// The command lines are built by the pure functions in `ffmpegargs` (unit-tested); they are the
// app's former, scattered ffmpeg commands, unchanged.
#include "tanara/media/MediaSettings.h"
#include "tanara/media/MediaTool.h"
#include "tanara/media/MediaToolLocator.h"

#include <QMutex>

namespace tanara::media {

namespace ffmpegargs {
// ffprobe: the JSON description of the format and the streams.
QStringList probe(const QString& path);
// ffmpeg: decode (a segment) to raw PCM on stdout.
QStringList decode(const DecodeSpec& spec);
// ffmpeg: raw PCM on stdin -> Opus file.
QStringList encode(const EncodeSpec& spec);
// ffmpeg: the first audio stream -> Opus/ogg track(s), with `-progress pipe:1`.
FilterGraphSpec transcode(const TranscodeSpec& spec);
// Parses the `-progress` key=value lines of a stdout chunk: the latest out_time_us / out_time_ms in
// ms (both are microseconds), or -1. `carry` holds an incomplete last line between chunks.
qint64 parseProgress(QByteArray& carry, const QByteArray& chunk);
// Parses ffprobe's JSON output.
std::optional<MediaInfo> parseProbeJson(const QByteArray& json);
} // namespace ffmpegargs

class FfmpegMediaTool final : public MediaTool {
public:
    FfmpegMediaTool(ToolResolution ffmpeg, ToolResolution ffprobe);

    // Resolves the tools (MediaToolLocator, the running process's environment) and logs the result.
    static std::shared_ptr<FfmpegMediaTool> fromSettings(const MediaSettings& settings);
    static std::shared_ptr<FfmpegMediaTool> fromSettings(const MediaSettings& settings, const LocatorEnv& env);

    const ToolResolution& ffmpeg() const { return m_ffmpeg; }
    const ToolResolution& ffprobe() const { return m_ffprobe; }

    bool available() const override;
    QString version() const override;
    QString ffmpegPath() const override;
    QString ffprobePath() const override;

    MediaResult probe(const QString& path, int timeoutMs = 30000) const override;
    MediaJob* startProbe(const QString& path, QObject* parent) const override;
    MediaResult decodePcm(const DecodeSpec& spec, const RawPcmSink& sink,
                          const CancelToken& cancel = {}) const override;
    MediaJob* startDecode(const DecodeSpec& spec, QObject* parent) const override;
    std::unique_ptr<EncoderHandle> startEncoder(const EncodeSpec& spec) const override;
    MediaJob* runFilterGraph(const FilterGraphSpec& spec, QObject* parent) const override;
    MediaJob* startTranscode(const TranscodeSpec& spec, QObject* parent) const override;

private:
    ToolResolution m_ffmpeg;
    ToolResolution m_ffprobe;
    mutable QMutex m_versionLock;
    mutable bool m_versionRead = false;
    mutable QString m_version;
};

} // namespace tanara::media
