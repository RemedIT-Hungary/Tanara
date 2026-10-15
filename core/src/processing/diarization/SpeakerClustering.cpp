#include "tanara/processing/diarization/SpeakerClustering.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace tanara::processing::clustering {

float cosineDistance(const QVector<float>& a, const QVector<float>& b)
{
    if (a.size() != b.size() || a.isEmpty()) return 2.0f;
    double dot = 0.0;
    for (qsizetype i = 0; i < a.size(); ++i) dot += double(a[i]) * double(b[i]);
    const double d = 1.0 - dot;
    return std::isfinite(d) ? float(std::clamp(d, 0.0, 2.0)) : 2.0f;
}

namespace {

// The condensed upper triangle (i < j) of an n x n matrix.
class Condensed {
public:
    explicit Condensed(int n) : m_n(n), m_v(size_t(n) * size_t(n > 0 ? n - 1 : 0) / 2, 0.0f) {}
    float& at(int i, int j)
    {
        if (i > j) std::swap(i, j);
        return m_v[index(i, j)];
    }

private:
    size_t index(int i, int j) const
    {
        return size_t(i) * size_t(m_n) - size_t(i) * size_t(i + 1) / 2 + size_t(j - i - 1);
    }
    int m_n;
    std::vector<float> m_v;
};

int find(QVector<int>& parent, int x)
{
    while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

QVector<int> denseLabels(QVector<int> raw)
{
    QHash<int, int> map;
    for (int& l : raw) {
        if (l < 0) continue;
        auto it = map.find(l);
        if (it == map.end()) it = map.insert(l, int(map.size()));
        l = it.value();
    }
    return raw;
}

} // namespace

QVector<Merge> completeLinkage(const QVector<QVector<float>>& vectors)
{
    const int n = int(vectors.size());
    QVector<Merge> merges;
    if (n < 2) return merges;
    merges.reserve(n - 1);

    Condensed d(n);
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) d.at(i, j) = cosineDistance(vectors[i], vectors[j]);

    // NN-chain (Murtagh 1983; valid for the reducible complete linkage): follow nearest neighbours until
    // two clusters are each other's nearest, merge them, keep the rest of the chain.
    std::vector<char> active(size_t(n), 1);
    std::vector<int> chain;
    chain.reserve(size_t(n));
    int remaining = n;
    while (remaining > 1) {
        if (chain.empty()) {
            for (int i = 0; i < n; ++i)
                if (active[size_t(i)]) { chain.push_back(i); break; }
        }
        int a = -1, b = -1;
        float h = 0.0f;
        for (;;) {
            a = chain.back();
            const int prev = chain.size() >= 2 ? chain[chain.size() - 2] : -1;
            // The previous chain element wins ties, so the chain always ends.
            int best = prev;
            float bestD = prev >= 0 ? d.at(a, prev) : std::numeric_limits<float>::infinity();
            for (int x = 0; x < n; ++x) {
                if (x == a || !active[size_t(x)]) continue;
                const float dx = d.at(a, x);
                if (dx < bestD) { bestD = dx; best = x; }
            }
            if (best == prev) { b = prev; h = bestD; break; }
            chain.push_back(best);
        }
        chain.pop_back();
        chain.pop_back();
        const int r = std::min(a, b), s = std::max(a, b);
        merges.append({r, s, h});
        for (int x = 0; x < n; ++x) {
            if (x == r || x == s || !active[size_t(x)]) continue;
            float& rx = d.at(r, x);
            rx = std::max(rx, d.at(s, x));
        }
        active[size_t(s)] = 0;
        --remaining;
    }
    std::stable_sort(merges.begin(), merges.end(), [](const Merge& x, const Merge& y) { return x.height < y.height; });
    return merges;
}

QVector<int> cutTree(int n, const QVector<Merge>& merges, double threshold, int numClusters)
{
    if (n <= 0) return {};
    QVector<int> parent(n);
    std::iota(parent.begin(), parent.end(), 0);
    const int apply = numClusters > 0 ? std::max(0, n - std::min(numClusters, n)) : int(merges.size());
    for (int k = 0; k < merges.size() && k < apply; ++k) {
        const Merge& m = merges[k];
        if (numClusters <= 0 && !(double(m.height) < threshold)) break;   // sorted: nothing lower follows
        const int ra = find(parent, m.a), rb = find(parent, m.b);
        if (ra != rb) parent[rb] = ra;
    }
    QVector<int> labels(n);
    for (int i = 0; i < n; ++i) labels[i] = find(parent, i);
    return denseLabels(labels);
}

QVector<int> clusterCompleteLinkage(const QVector<QVector<float>>& vectors, double threshold, int numClusters)
{
    return cutTree(int(vectors.size()), completeLinkage(vectors), threshold, numClusters);
}

QVector<float> centroid(const QVector<QVector<float>>& vectors, const QVector<int>& members)
{
    QVector<float> acc;
    for (int i : members) {
        if (i < 0 || i >= vectors.size()) continue;
        const QVector<float>& v = vectors[i];
        if (v.isEmpty()) continue;
        if (acc.isEmpty()) acc.fill(0.0f, v.size());
        if (v.size() != acc.size()) continue;
        for (qsizetype k = 0; k < v.size(); ++k) acc[k] += v[k];
    }
    double norm = 0.0;
    for (float x : acc) norm += double(x) * x;
    norm = std::sqrt(norm);
    if (norm <= 1e-12) return {};
    for (float& x : acc) x = float(x / norm);
    return acc;
}

int clusterCount(const QVector<int>& labels)
{
    int mx = -1;
    for (int l : labels) mx = std::max(mx, l);
    return mx + 1;
}

QVector<int> reassignSmallClusters(const QVector<int>& labels, const QVector<QVector<float>>& vectors,
                                   int minClusterSize)
{
    const int k = clusterCount(labels);
    if (minClusterSize <= 1 || k <= 1) return labels;
    QVector<QVector<int>> members(k);
    for (int i = 0; i < labels.size(); ++i)
        if (labels[i] >= 0) members[labels[i]].append(i);
    QVector<int> large;
    for (int c = 0; c < k; ++c)
        if (members[c].size() >= minClusterSize) large.append(c);
    if (large.isEmpty() || large.size() == k) return labels;

    QVector<QVector<float>> cents(k);
    for (int c = 0; c < k; ++c) cents[c] = centroid(vectors, members[c]);
    QVector<int> target(k);
    std::iota(target.begin(), target.end(), 0);
    for (int c = 0; c < k; ++c) {
        if (members[c].size() >= minClusterSize) continue;
        int best = large.first();
        float bestD = std::numeric_limits<float>::infinity();
        for (int l : large) {
            const float dl = cosineDistance(cents[c], cents[l]);
            if (dl < bestD) { bestD = dl; best = l; }
        }
        target[c] = best;
    }
    QVector<int> out = labels;
    for (int& l : out)
        if (l >= 0) l = target[l];
    return denseLabels(out);
}

QVector<int> subsample(int n, int cap, const QVector<int>& groups)
{
    QVector<int> out;
    if (n <= 0) return out;
    if (cap <= 0 || n <= cap) {
        out.resize(n);
        std::iota(out.begin(), out.end(), 0);
        return out;
    }
    const int stride = (n + cap - 1) / cap;
    QSet<int> chosen;
    QSet<int> covered;
    for (int i = 0; i < n; i += stride) {
        chosen.insert(i);
        if (i < groups.size() && groups[i] >= 0) covered.insert(groups[i]);
    }
    for (int i = 0; i < n && i < groups.size(); ++i) {
        const int g = groups[i];
        if (g < 0 || covered.contains(g)) continue;
        covered.insert(g);
        chosen.insert(i);
    }
    out = QVector<int>(chosen.cbegin(), chosen.cend());
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace tanara::processing::clustering
