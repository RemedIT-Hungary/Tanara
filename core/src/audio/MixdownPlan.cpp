#include "tanara/audio/MixdownPlan.h"

#include "tanara/audio/TrackCatalog.h"

#include <QDir>
#include <QFileInfo>

namespace tanara {

namespace {

// Loudness-normalizálás (EBU R128, -16 LUFS, true-peak -1.5 dBTP) — felhozza a halk beszédet
// kényelmes lejátszási hangerőre. Csak a hallgatásra szánt keveréket érinti, az STT-t nem.
const QString kLoudnorm = QStringLiteral(
    "loudnorm=I=-16,acompressor=threshold=-24dB:ratio=4:makeup=10,alimiter=limit=0.97");

} // namespace

MixdownPlan MixdownPlan::fromMeeting(const Meeting& m)
{
    MixdownPlan plan;
    QVector<TrackView> views;   // a hiányzó sávok nevéhez — csak ha kell
    for (const Track& t : m.tracks) {
        if (!t.active) continue;
        const QString path = QDir(m.folder).filePath(t.file);
        if (t.file.isEmpty() || !QFileInfo(path).isFile()) {
            if (views.isEmpty()) views = TrackCatalog::tracks(m);
            QString name = t.id;
            for (const TrackView& v : std::as_const(views))
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
        // "[1]adelay=57200:all=1[d1];" — a bemenet minden csatornáját ennyivel tolja.
        pre += QStringLiteral("[%1]adelay=%2:all=1[d%1];").arg(i).arg(offsets.at(i));
        mixIn += QStringLiteral("[d%1]").arg(i);
    }
    if (n > 1)
        return {QStringLiteral("-filter_complex"),
                QStringLiteral("%1%2amix=inputs=%3:duration=longest:normalize=0,%4")
                    .arg(pre, mixIn).arg(n).arg(kLoudnorm)};
    if (!pre.isEmpty())
        return {QStringLiteral("-filter_complex"), pre + mixIn + kLoudnorm};   // 1 eltolt sáv
    return {QStringLiteral("-af"), kLoudnorm};                                // 1 sáv → normalizálás
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
    // SZTEREÓ kimenet (dual-mono) — a Qt6/PipeWire mono streamet halkan/egy csatornára
    // játszhat; sztereóval mindkét hangszóró megszólal.
    args << QStringLiteral("-ac") << QStringLiteral("2")
         << QStringLiteral("-c:a") << QStringLiteral("libmp3lame")
         << QStringLiteral("-q:a") << QStringLiteral("4");
    // Valós haladás: az ffmpeg kulcs=érték sorokat ír a stdoutra (out_time_us=…).
    args << QStringLiteral("-progress") << QStringLiteral("pipe:1") << QStringLiteral("-nostats")
         << QStringLiteral("-y") << outPath;
    return args;
}

} // namespace tanara
