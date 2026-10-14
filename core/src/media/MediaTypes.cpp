#include "tanara/media/MediaSettings.h"
#include "tanara/media/MediaTool.h"
#include "tanara/media/MediaTypes.h"

#include <cstring>

namespace tanara::media {

// ---- MediaInfo ---------------------------------------------------------------------------------

const MediaStream* MediaInfo::firstAudio() const
{
    for (const MediaStream& s : streams)
        if (s.codecType == QLatin1String("audio")) return &s;
    return nullptr;
}

bool MediaInfo::hasVideo() const
{
    for (const MediaStream& s : streams)
        if (s.codecType == QLatin1String("video") && !s.attachedPic) return true;
    return false;
}

// ---- MediaSettings -----------------------------------------------------------------------------

QJsonObject MediaSettings::toJson() const
{
    return QJsonObject{{QStringLiteral("ffmpegPath"), ffmpegPath},
                       {QStringLiteral("ffprobePath"), ffprobePath}};
}

MediaSettings MediaSettings::fromJson(const QJsonObject& o)
{
    MediaSettings s;
    s.ffmpegPath = o.value(QStringLiteral("ffmpegPath")).toString().trimmed();
    s.ffprobePath = o.value(QStringLiteral("ffprobePath")).toString().trimmed();
    return s;
}

// ---- MediaTool helpers -------------------------------------------------------------------------

PcmDecoder MediaTool::monoS16Decoder(int sampleRate) const
{
    return [this, sampleRate](const QString& path, const PcmSink& sink, QString* error) -> bool {
        DecodeSpec spec;
        spec.path = path;
        spec.format = PcmFormat{sampleRate, 1, SampleFormat::S16};
        const MediaResult r = decodePcm(spec, [&sink](const char* data, qsizetype bytes) {
            sink(reinterpret_cast<const qint16*>(data), bytes / qsizetype(sizeof(qint16)));
        });
        if (!r.started) {
            if (error) *error = QStringLiteral("Az ffmpeg nem indítható.");
            return false;
        }
        if (!r.ok() || r.bytes < 2) {
            if (error)
                *error = QStringLiteral("ffmpeg dekódolás sikertelen: %1 (%2)").arg(path, r.errorText());
            return false;
        }
        return true;
    };
}

QVector<float> MediaTool::decodeMonoF32(const QString& path, qint64 startMs, qint64 endMs, int sampleRate) const
{
    DecodeSpec spec;
    spec.path = path;
    spec.range = TimeRange{startMs, endMs};
    spec.format = PcmFormat{sampleRate, 1, SampleFormat::F32};
    spec.timeoutMs = 120000;
    QVector<float> pcm;
    const MediaResult r = decodePcm(spec, [&pcm](const char* data, qsizetype bytes) {
        const qsizetype n = bytes / qsizetype(sizeof(float));
        const qsizetype at = pcm.size();
        pcm.resize(at + n);
        std::memcpy(pcm.data() + at, data, size_t(n) * sizeof(float));
    });
    if (!r.started || r.timedOut) return {};
    return pcm;
}

} // namespace tanara::media
