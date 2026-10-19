#include "tanara/processing/analysis/TrackActivity.h"
#include "tanara/library/MeetingLayout.h"

#include "tanara/foundation/log/Logging.h"
#include "tanara/library/tracks/TrackCatalog.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <limits>

namespace tanara::processing {

namespace {

const float kNaN = std::numeric_limits<float>::quiet_NaN();

// Per-frame RMS collector: the PCM comes in chunks, the frame boundary is independent of the chunking.
class FrameAccumulator {
public:
    explicit FrameAccumulator(int frameMs) : m_frameSamples(std::max(1, frameMs) * trackactivity::kSampleRate / 1000) {}

    void add(const qint16* s, qsizetype n)
    {
        for (qsizetype i = 0; i < n; ++i) {
            const double v = double(s[i]) / 32768.0;
            m_sumSq += v * v;
            if (++m_count == m_frameSamples) flush();
        }
    }
    // The remaining (shorter) last frame counts too.
    QVector<float> finish()
    {
        if (m_count > 0) flush();
        return std::move(m_db);
    }

private:
    void flush()
    {
        const double rms = std::sqrt(m_sumSq / double(m_count));
        const double db = rms > 0.0 ? 20.0 * std::log10(rms) : double(trackactivity::kMinDb);
        m_db.append(float(std::max(db, double(trackactivity::kMinDb))));
        m_sumSq = 0.0;
        m_count = 0;
    }

    int m_frameSamples;
    double m_sumSq = 0.0;
    int m_count = 0;
    QVector<float> m_db;
};

} // namespace

namespace trackactivity {

QVector<float> frameDb(const QVector<qint16>& pcm, int frameMs)
{
    FrameAccumulator acc(frameMs);
    acc.add(pcm.constData(), pcm.size());
    return acc.finish();
}

float estimateFloor(const QVector<float>& db)
{
    QVector<float> v;
    v.reserve(db.size());
    for (float x : db)
        if (!std::isnan(x)) v.append(x);
    if (v.isEmpty()) return kMinDb;
    const qsizetype k = std::min<qsizetype>(v.size() - 1, qsizetype(double(v.size() - 1) * kFloorPercentile));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v.at(k);
}

QString activityFingerprint(const Meeting& m, const QString& folder, int frameMs)
{
    QStringList parts{QStringLiteral("frame=%1").arg(frameMs)};
    const QDir dir(folder);
    for (const Track& t : m.tracks) {
        if (!t.active) continue;
        const QFileInfo fi(dir.filePath(t.file));
        parts << QStringLiteral("%1|%2|%3|%4|%5|%6")
                     .arg(t.id, t.file)
                     .arg(fi.exists() ? fi.size() : -1)
                     .arg(fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : -1)
                     .arg(t.startOffsetMs)
                     .arg(int(t.kind));
    }
    return QString::fromLatin1(
        QCryptographicHash::hash(parts.join(QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha1).toHex());
}

std::optional<float> windowEnergyDb(const TrackActivity& t, qint64 startMs, qint64 endMs)
{
    if (endMs <= startMs || t.frameMs <= 0 || t.dbAboveFloor.isEmpty()) return std::nullopt;
    // Frames overlapping the window: [origin + i*frame, origin + (i+1)*frame) intersect [start, end) is non-empty.
    const qint64 rel0 = startMs - t.originMs;
    const qint64 rel1 = endMs - t.originMs;
    const qint64 first = std::max<qint64>(0, rel0 >= 0 ? rel0 / t.frameMs : -1);
    const qint64 last = std::min<qint64>(t.dbAboveFloor.size() - 1, (rel1 - 1) / t.frameMs);
    if (rel1 <= 0 || first > last) return std::nullopt;
    double sum = 0.0;
    int n = 0;
    for (qint64 i = first; i <= last; ++i) {
        const float d = t.dbAboveFloor.at(i);
        if (std::isnan(d)) continue;
        sum += std::pow(10.0, double(d) / 10.0);
        ++n;
    }
    if (n == 0) return std::nullopt;
    return float(10.0 * std::log10(sum / n));
}

} // namespace trackactivity

} // namespace tanara::processing

// MeetingActivity (a domain type, domain/Activity.h) keeps its cache I/O here, next to the
// activity computation; these members therefore live in namespace tanara, not tanara::processing.
namespace tanara {

namespace {

constexpr quint32 kCacheMagic = 0x54414354;   // "TACT"
constexpr quint32 kCacheVersion = 1;

void writeTrack(QDataStream& out, const TrackActivity& t)
{
    out << t.trackId << qint32(t.kind) << qint32(t.frameMs) << t.originMs << t.floorDb << t.dbAboveFloor;
}

bool readTrack(QDataStream& in, TrackActivity& t)
{
    qint32 kind = 0, frameMs = 0;
    in >> t.trackId >> kind >> frameMs >> t.originMs >> t.floorDb >> t.dbAboveFloor;
    t.kind = TrackKind(kind);
    t.frameMs = frameMs;
    return in.status() == QDataStream::Ok && frameMs > 0;
}

} // namespace

QString MeetingActivity::filePath(const QString& meetingFolder)
{
    return library::MeetingLayout::path(meetingFolder, library::MeetingLayout::trackActivity());
}

QString MeetingActivity::viewFilePath(const QString& meetingFolder)
{
    return library::MeetingLayout::path(meetingFolder, library::MeetingLayout::trackActivityView());
}

MeetingActivity MeetingActivity::load(const QString& meetingFolder, const QString& fingerprint,
                                      const QString& path)
{
    MeetingActivity a;
    a.fingerprint = fingerprint;
    QFile f(path.isEmpty() ? filePath(meetingFolder) : path);
    if (!f.open(QIODevice::ReadOnly)) return a;
    QDataStream in(&f);
    in.setVersion(QDataStream::Qt_6_0);
    in.setFloatingPointPrecision(QDataStream::SinglePrecision);
    quint32 magic = 0, version = 0;
    QString fp;
    qint32 count = 0;
    in >> magic >> version >> fp >> count;
    if (magic != kCacheMagic || version != kCacheVersion || fp != fingerprint || count < 0
        || in.status() != QDataStream::Ok)
        return a;
    QVector<TrackActivity> tracks;
    for (qint32 i = 0; i < count; ++i) {
        TrackActivity t;
        if (!readTrack(in, t)) return a;   // corrupt file -> empty (we recompute)
        tracks.append(t);
    }
    a.tracks = tracks;
    return a;
}

bool MeetingActivity::save(const QString& meetingFolder, const QString& path) const
{
    QSaveFile f(path.isEmpty() ? filePath(meetingFolder) : path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    QDataStream out(&f);
    out.setVersion(QDataStream::Qt_6_0);
    out.setFloatingPointPrecision(QDataStream::SinglePrecision);
    out << kCacheMagic << kCacheVersion << fingerprint << qint32(tracks.size());
    for (const TrackActivity& t : tracks) writeTrack(out, t);
    return f.commit();
}

} // namespace tanara

namespace tanara::processing {

MeetingActivity computeMeetingActivity(const Meeting& m, const QString& folder, const media::MediaTool& tool,
                                       int frameMs)
{
    return computeMeetingActivity(m, folder, tool.monoS16Decoder(trackactivity::kSampleRate), frameMs);
}

MeetingActivity loadOrComputeSideActivity(const Meeting& m, const media::MediaTool& tool)
{
    if (m.folder.isEmpty()) return {};
    bool mic = false, loop = false;
    for (const Track& t : m.tracks) {
        if (!t.active) continue;
        mic |= t.kind == TrackKind::Mic;
        loop |= t.kind == TrackKind::Loopback;
    }
    if (!mic || !loop) return {};
    const QString fp = trackactivity::activityFingerprint(m, m.folder);
    MeetingActivity act = MeetingActivity::load(m.folder, fp);
    if (act.isEmpty()) {
        act = computeMeetingActivity(m, m.folder, tool);
        if (!act.isEmpty()) act.save(m.folder);
    }
    return act;
}

MeetingActivity computeMeetingActivity(const Meeting& m, const QString& folder,
                                       const trackactivity::PcmDecoder& decode, int frameMs)
{
    PerfScope perf("computeMeetingActivity", 50);
    if (!decode) return {};
    frameMs = std::max(1, frameMs);
    MeetingActivity out;
    out.fingerprint = trackactivity::activityFingerprint(m, folder, frameMs);

    // Logical tracks: a device's spans under the leading (earliest) span, in order of first occurrence.
    const QVector<library::tracknames::SegmentInfo> seg = library::tracknames::segments(m.tracks);
    QVector<int> leaders;
    QHash<int, QVector<int>> members;
    for (int i = 0; i < m.tracks.size(); ++i) {
        if (!m.tracks.at(i).active) continue;
        const int lead = seg.value(i).leader >= 0 ? seg.value(i).leader : i;
        if (!members.contains(lead)) leaders.append(lead);
        members[lead].append(i);
    }

    const QDir dir(folder);
    for (int lead : leaders) {
        // Per span the frame dBs with their own offset.
        struct Part { qint64 offsetMs; QVector<float> db; };
        QVector<Part> parts;
        for (int idx : members.value(lead)) {
            const Track& t = m.tracks.at(idx);
            const QString path = dir.filePath(t.file);
            if (t.file.isEmpty() || !QFileInfo::exists(path)) {
                qCWarning(lcAudio) << "Track activity: track file missing, skipped:" << path;
                continue;
            }
            FrameAccumulator acc(frameMs);
            QString error;
            const bool ok = decode(path, [&acc](const qint16* s, qsizetype n) { acc.add(s, n); }, &error);
            QVector<float> db = acc.finish();
            if (!ok || db.isEmpty()) {
                qCWarning(lcAudio) << "Track activity: track cannot be decoded, skipped:" << path << error;
                continue;
            }
            parts.append({std::max<qint64>(0, t.startOffsetMs), std::move(db)});
        }
        if (parts.isEmpty()) continue;

        TrackActivity ta;
        ta.trackId = m.tracks.at(lead).id;
        ta.kind = m.tracks.at(lead).kind;
        ta.frameMs = frameMs;
        ta.originMs = std::min_element(parts.cbegin(), parts.cend(),
                                       [](const Part& a, const Part& b) { return a.offsetMs < b.offsetMs; })->offsetMs;
        // The spans onto their meeting time; the gaps are NaN.
        QVector<float> raw;
        for (const Part& p : parts) {
            const qint64 at = (p.offsetMs - ta.originMs + frameMs / 2) / frameMs;
            const qint64 need = at + p.db.size();
            if (raw.size() < need) raw.resize(need, kNaN);
            std::copy(p.db.cbegin(), p.db.cend(), raw.begin() + at);
        }
        ta.floorDb = trackactivity::estimateFloor(raw);
        ta.dbAboveFloor.resize(raw.size());
        for (qsizetype i = 0; i < raw.size(); ++i)
            ta.dbAboveFloor[i] = std::isnan(raw.at(i)) ? kNaN : std::max(0.0f, raw.at(i) - ta.floorDb);
        out.tracks.append(std::move(ta));
    }
    return out;
}

} // namespace tanara::processing