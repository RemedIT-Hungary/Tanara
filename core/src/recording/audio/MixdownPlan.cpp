#include "tanara/recording/audio/MixdownPlan.h"

#include "tanara/library/tracks/TrackCatalog.h"

#include <QDir>
#include <QFileInfo>

namespace tanara::recording {

namespace {

// Loudness normalization (EBU R128, -16 LUFS, true-peak -1.5 dBTP): brings quiet speech up to a
// comfortable playback volume. It affects only the mix meant for listening, not the STT.
const QString kLoudnorm = QStringLiteral(
    "loudnorm=I=-16,acompressor=threshold=-24dB:ratio=4:makeup=10,alimiter=limit=0.97");

} // namespace

MixdownPlan MixdownPlan::fromMeeting(const Meeting& m)
{
    MixdownPlan plan;
    QVector<library::TrackView> views;   // for the names of missing tracks, only if needed
    for (const Track& t : m.tracks) {
        if (!t.included()) continue;   // dropped / speechless / manually removed track
        const QString path = QDir(m.folder).filePath(t.file);
        if (t.file.isEmpty() || !QFileInfo(path).isFile()) {
            if (views.isEmpty()) views = library::TrackCatalog::tracks(m);
            QString name = t.id;
            for (const library::TrackView& v : std::as_const(views))
                if (v.track.id == t.id) { name = v.displayName; break; }
            plan.missing << name;
            continue;
        }
        plan.inputs.append({t.id, path, qMax<qint64>(0, t.startOffsetMs)});
    }
    return plan;
}

QStringList MixdownPlan::filterArgs(const QVector<qint64>& offsets)
{
    const int n = int(offsets.size());
    if (n == 0) return {};
    QString pre, mixIn;
    for (int i = 0; i < n; ++i) {
        if (offsets.at(i) <= 0) { mixIn += QStringLiteral("[%1]").arg(i); continue; }
        // "[1]adelay=57200:all=1[d1];": shifts every channel of the input by this much.
        pre += QStringLiteral("[%1]adelay=%2:all=1[d%1];").arg(i).arg(offsets.at(i));
        mixIn += QStringLiteral("[d%1]").arg(i);
    }
    if (n > 1)
        return {QStringLiteral("-filter_complex"),
                QStringLiteral("%1%2amix=inputs=%3:duration=longest:normalize=0,%4")
                    .arg(pre, mixIn).arg(n).arg(kLoudnorm)};
    if (!pre.isEmpty())
        return {QStringLiteral("-filter_complex"), pre + mixIn + kLoudnorm};   // 1 shifted track
    return {QStringLiteral("-af"), kLoudnorm};                                // 1 track -> normalization
}

QStringList MixdownPlan::ffmpegArgs(const QString& outPath) const
{
    if (inputs.isEmpty()) return {};
    QStringList args{QStringLiteral("-hide_banner"),
                     QStringLiteral("-loglevel"), QStringLiteral("error")};
    QVector<qint64> offsets;
    for (const MixdownInput& in : inputs) {
        args << QStringLiteral("-i") << in.path;
        offsets << in.offsetMs;
    }
    args += filterArgs(offsets);
    // STEREO output (dual-mono): Qt6/PipeWire may play a mono stream quietly / on one channel; with
    // stereo both speakers sound.
    args << QStringLiteral("-ac") << QStringLiteral("2")
         << QStringLiteral("-c:a") << QStringLiteral("libmp3lame")
         << QStringLiteral("-q:a") << QStringLiteral("4");
    // Real progress: ffmpeg writes key=value lines to stdout (out_time_us=...).
    args << QStringLiteral("-progress") << QStringLiteral("pipe:1") << QStringLiteral("-nostats")
         << QStringLiteral("-y") << outPath;
    return args;
}

} // namespace tanara::recording