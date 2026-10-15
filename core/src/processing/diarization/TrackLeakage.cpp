#include "tanara/processing/diarization/TrackLeakage.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace tanara::processing::leakage {

namespace {

constexpr float kFloorDb = -100.0f;

std::pair<int, int> frames(double start, double end, int frameMs, int n)
{
    const double f = frameMs / 1000.0;
    const int a = std::clamp(int(std::floor(start / f)), 0, n);
    const int b = std::clamp(int(std::ceil(end / f)), 0, n);
    return {a, std::max(a, b)};
}

// The time of [start, end) covered by the (meeting-time) intervals.
double covered(double start, double end, const QVector<Turn>& other)
{
    double c = 0;
    for (const Turn& o : other) c += std::max(0.0, std::min(end, o.end) - std::max(start, o.start));
    return c;
}

} // namespace

QVector<float> frameDb(const QVector<float>& pcm16k, int frameMs, int sampleRate)
{
    const qsizetype len = std::max<qsizetype>(1, qsizetype(sampleRate) * frameMs / 1000);
    QVector<float> out;
    out.reserve(pcm16k.size() / len + 1);
    for (qsizetype a = 0; a < pcm16k.size(); a += len) {
        const qsizetype b = std::min(pcm16k.size(), a + len);
        double e = 0;
        for (qsizetype i = a; i < b; ++i) e += double(pcm16k[i]) * pcm16k[i];
        const double rms = std::sqrt(e / double(b - a));
        out.append(rms > 1e-5 ? float(20.0 * std::log10(rms)) : kFloorDb);
    }
    return out;
}

float speechLevel(const QVector<float>& db, const QVector<Turn>& speech, int frameMs, double percentile)
{
    std::vector<float> v;
    for (const Turn& t : speech) {
        const auto [a, b] = frames(t.start, t.end, frameMs, int(db.size()));
        for (int i = a; i < b; ++i) v.push_back(db[i]);
    }
    if (v.empty()) return kFloorDb;
    const size_t k = std::min(v.size() - 1, size_t(std::clamp(percentile, 0.0, 1.0) * double(v.size() - 1) + 0.5));
    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(k), v.end());
    return v[k];
}

float meanLevel(const QVector<float>& db, double start, double end, int frameMs)
{
    const auto [a, b] = frames(start, end, frameMs, int(db.size()));
    if (b <= a) return kFloorDb;
    double p = 0;
    for (int i = a; i < b; ++i) p += std::pow(10.0, db[i] / 10.0);
    p /= double(b - a);
    return p > 0 ? std::max(kFloorDb, float(10.0 * std::log10(p))) : kFloorDb;
}

QVector<QVector<Leak>> detect(const QVector<TrackInput>& tracks, const Options& opts)
{
    const int n = int(tracks.size());
    QVector<QVector<Leak>> out(n);
    if (n < 2) return out;
    // Per track: its speech level and its speech turns in meeting time.
    QVector<float> level(n);
    QVector<QVector<Turn>> meeting(n);
    for (int i = 0; i < n; ++i) {
        const TrackInput& t = tracks[i];
        level[i] = speechLevel(t.frameDb, t.speech, opts.frameMs, opts.speechPercentile);
        const double off = t.startOffsetMs / 1000.0;
        for (const Turn& s : t.speech) meeting[i].append({s.start + off, s.end + off});
    }
    for (int a = 0; a < n; ++a) {
        const TrackInput& A = tracks[a];
        const double offA = A.startOffsetMs / 1000.0;
        for (int k = 0; k < A.speech.size(); ++k) {
            const Turn& t = A.speech[k];
            const double len = t.end - t.start;
            if (len <= 0) continue;
            const float relA = meanLevel(A.frameDb, t.start, t.end, opts.frameMs) - level[a];
            if (relA > opts.leakDb) continue;
            // The best source: the loudest (relative) track that covers the turn enough.
            int best = -1;
            float bestRel = -1e9f;
            for (int b = 0; b < n; ++b) {
                if (b == a || tracks[b].speech.isEmpty()) continue;
                const double ms = t.start + offA, me = t.end + offA;
                if (covered(ms, me, meeting[b]) < opts.minCoverage * len) continue;
                const double offB = tracks[b].startOffsetMs / 1000.0;
                const float relB = meanLevel(tracks[b].frameDb, ms - offB, me - offB, opts.frameMs) - level[b];
                if (relB < opts.sourceDb || relB - relA < opts.marginDb) continue;
                if (relB > bestRel) { bestRel = relB; best = b; }
            }
            if (best < 0) continue;
            out[a].append({k, t, tracks[best].trackId, relA, bestRel});
        }
    }
    return out;
}

} // namespace tanara::processing::leakage
