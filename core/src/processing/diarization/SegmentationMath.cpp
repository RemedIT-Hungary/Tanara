#include "tanara/processing/diarization/SegmentationMath.h"

#include <QHash>
#include <QVarLengthArray>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace tanara::processing::segmentation {

int ModelInfo::computedFramesPerWindow() const
{
    if (receptiveFieldShift <= 0 || windowSize < receptiveFieldSize) return 0;
    return (windowSize - receptiveFieldSize) / receptiveFieldShift + 1;
}

bool ModelInfo::isValid() const
{
    return sampleRate > 0 && windowSize > 0 && receptiveFieldSize > 0 && receptiveFieldShift > 0
        && numSpeakers > 0 && powersetMaxClasses > 0 && powersetMaxClasses <= numSpeakers
        && numClasses == powersetClasses(numSpeakers, powersetMaxClasses).size() && framesPerWindow > 0;
}

QVector<QVector<int>> powersetClasses(int numSpeakers, int maxSetSize)
{
    QVector<QVector<int>> out;
    if (numSpeakers <= 0 || maxSetSize < 0) return out;
    // Every subset of {0..n-1} of size 0..maxSetSize, by size, then lexicographic (itertools.combinations).
    for (int size = 0; size <= std::min(maxSetSize, numSpeakers); ++size) {
        QVector<int> idx(size);
        for (int i = 0; i < size; ++i) idx[i] = i;
        while (true) {
            out.append(idx);
            int i = size - 1;
            while (i >= 0 && idx[i] == numSpeakers - size + i) --i;
            if (i < 0) break;
            ++idx[i];
            for (int j = i + 1; j < size; ++j) idx[j] = idx[j - 1] + 1;
        }
    }
    return out;
}

int Activity::activeFrames(int s) const
{
    int n = 0;
    for (int f = 0; f < frames; ++f)
        if (at(f, s) > 0.5f) ++n;
    return n;
}

int Activity::countAt(int f) const
{
    int n = 0;
    for (int s = 0; s < speakers; ++s)
        if (at(f, s) > 0.5f) ++n;
    return n;
}

QVector<quint8> argmaxClasses(const float* logits, int frames, int classes)
{
    QVector<quint8> out(frames, 0);
    if (!logits || classes <= 0) return out;
    for (int f = 0; f < frames; ++f) {
        const float* row = logits + qsizetype(f) * classes;
        int best = 0;
        for (int c = 1; c < classes; ++c)
            if (row[c] > row[best]) best = c;   // first maximum wins (stable on ties)
        out[f] = quint8(best);
    }
    return out;
}

Activity decodePowerset(const QVector<quint8>& classes, const QVector<QVector<int>>& mapping, int numSpeakers)
{
    Activity a(int(classes.size()), numSpeakers);
    for (int f = 0; f < classes.size(); ++f) {
        const int c = classes[f];
        if (c >= mapping.size()) continue;
        for (int s : mapping[c])
            if (s >= 0 && s < numSpeakers) a.at(f, s) = 1.0f;
    }
    return a;
}

// ---- windowing ------------------------------------------------------------------------------------

int windowShiftSamples(const ModelInfo& info, double windowShiftRatio)
{
    const int step = std::max(1, info.receptiveFieldShift);
    const double wanted = std::clamp(windowShiftRatio, 0.0, 1.0) * info.windowSize;
    int frames = int(std::lround(wanted / step));
    frames = std::clamp(frames, 1, std::max(1, info.windowSize / step));
    return frames * step;
}

QVector<qint64> windowStarts(qint64 numSamples, int windowSize, int windowShift)
{
    QVector<qint64> out;
    if (numSamples <= 0 || windowSize <= 0 || windowShift <= 0) return out;
    if (numSamples <= windowSize) return {0};
    qint64 s = 0;
    for (; s + windowSize <= numSamples; s += windowShift) out.append(s);
    if (out.last() + windowSize < numSamples) out.append(out.last() + windowShift);   // zero-padded tail
    return out;
}

int globalFrameCount(const ModelInfo& info, qint64 numSamples)
{
    if (numSamples <= 0 || info.receptiveFieldShift <= 0) return 0;
    // Frames g with g * rfShift + rfSize / 2 < numSamples.
    const double lim = (double(numSamples) - 0.5 * info.receptiveFieldSize) / info.receptiveFieldShift;
    if (lim <= 0) return 1;   // shorter than half a receptive field: still one frame
    return int(std::ceil(lim));
}

// ---- Hungarian ------------------------------------------------------------------------------------

namespace {

// rows <= cols. Returns the column of every row. (Kuhn-Munkres with row/column potentials, 1-based.)
QVector<int> hungarianWide(const QVector<double>& a, int n, int m)
{
    const double inf = std::numeric_limits<double>::infinity();
    QVector<double> u(n + 1, 0.0), v(m + 1, 0.0);
    QVector<int> p(m + 1, 0), way(m + 1, 0);
    for (int i = 1; i <= n; ++i) {
        p[0] = i;
        int j0 = 0;
        QVector<double> minv(m + 1, inf);
        QVector<bool> used(m + 1, false);
        do {
            used[j0] = true;
            const int i0 = p[j0];
            double delta = inf;
            int j1 = 0;
            for (int j = 1; j <= m; ++j) {
                if (used[j]) continue;
                const double cur = a[(i0 - 1) * m + (j - 1)] - u[i0] - v[j];
                if (cur < minv[j]) { minv[j] = cur; way[j] = j0; }
                if (minv[j] < delta) { delta = minv[j]; j1 = j; }
            }
            for (int j = 0; j <= m; ++j) {
                if (used[j]) { u[p[j]] += delta; v[j] -= delta; }
                else minv[j] -= delta;
            }
            j0 = j1;
        } while (p[j0] != 0);
        do {
            const int j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0 != 0);
    }
    QVector<int> rowToCol(n, -1);
    for (int j = 1; j <= m; ++j)
        if (p[j] > 0) rowToCol[p[j] - 1] = j - 1;
    return rowToCol;
}

} // namespace

QVector<int> hungarianAssign(const QVector<double>& cost, int rows, int cols)
{
    if (rows <= 0) return {};
    if (cols <= 0 || cost.size() < qsizetype(rows) * cols) return QVector<int>(rows, -1);
    if (rows <= cols) return hungarianWide(cost, rows, cols);
    // Tall: solve the transpose (every column gets a row), then invert.
    QVector<double> t(qsizetype(rows) * cols);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) t[c * rows + r] = cost[r * cols + c];
    const QVector<int> colToRow = hungarianWide(t, cols, rows);
    QVector<int> out(rows, -1);
    for (int c = 0; c < cols; ++c)
        if (colToRow[c] >= 0) out[colToRow[c]] = c;
    return out;
}

// ---- stitching ------------------------------------------------------------------------------------

StitchResult stitchWindows(const QVector<WindowActivity>& windows, const StitchOptions& opts)
{
    StitchResult r;
    r.localToStitched.resize(windows.size());
    int coveredEnd = std::numeric_limits<int>::min();   // end (exclusive) of the frames seen so far
    for (int k = 0; k < windows.size(); ++k) {
        const WindowActivity& w = windows[k];
        const Activity& act = w.activity;
        QVector<int> map(act.speakers, -1);

        QVector<int> locals;   // the local speakers with any activity in this window
        for (int l = 0; l < act.speakers; ++l)
            if (act.activeFrames(l) > 0) locals.append(l);

        const int rs = w.startFrame;
        const int re = std::min(w.startFrame + act.frames, coveredEnd);
        if (k > 0 && re > rs && !locals.isEmpty()) {
            const int R = re - rs;
            // Mean activity of every stitched speaker on the shared frames, over the earlier windows
            // that cover each frame.
            QVector<int> cover(R, 0);
            QHash<int, QVector<float>> agg;
            for (int j = k - 1; j >= 0; --j) {
                const WindowActivity& pw = windows[j];
                const int ps = pw.startFrame, pe = pw.startFrame + pw.activity.frames;
                if (pe <= rs) continue;   // starts are increasing, ends need not be strictly: keep scanning
                const int a = std::max(ps, rs), b = std::min(pe, re);
                for (int f = a; f < b; ++f) ++cover[f - rs];
                const QVector<int>& pmap = r.localToStitched[j];
                for (int s = 0; s < pmap.size(); ++s) {
                    const int g = pmap[s];
                    if (g < 0) continue;
                    QVector<float>& row = agg[g];
                    if (row.isEmpty()) row.fill(0.0f, R);
                    for (int f = a; f < b; ++f) row[f - rs] += pw.activity.at(f - ps, s);
                }
            }
            QVector<int> cands;
            for (auto it = agg.begin(); it != agg.end(); ++it) {
                QVector<float>& row = it.value();
                double sum = 0;
                for (int f = 0; f < R; ++f) {
                    if (cover[f] > 0) row[f] /= float(cover[f]);
                    sum += row[f];
                }
                if (sum > 0) cands.append(it.key());
            }
            std::sort(cands.begin(), cands.end());
            if (!cands.isEmpty()) {
                const int nr = int(cands.size()), nc = int(locals.size());
                QVector<double> inter(nr * nc, 0.0), uni(nr * nc, 0.0), cost(nr * nc, 0.0);
                for (int i = 0; i < nr; ++i) {
                    const QVector<float>& row = agg[cands[i]];
                    for (int c = 0; c < nc; ++c) {
                        double in = 0, un = 0;
                        for (int f = 0; f < R; ++f) {
                            const float x = row[f], y = act.at(f + rs - w.startFrame, locals[c]);
                            in += std::min(x, y);
                            un += std::max(x, y);
                        }
                        inter[i * nc + c] = in;
                        uni[i * nc + c] = un;
                        cost[i * nc + c] = -in;
                    }
                }
                const QVector<int> assign = hungarianAssign(cost, nr, nc);
                for (int i = 0; i < nr; ++i) {
                    const int c = assign[i];
                    if (c < 0) continue;
                    const double in = inter[i * nc + c], un = uni[i * nc + c];
                    if (in >= opts.minSharedFrames && un > 0 && in / un >= opts.minIoU) map[locals[c]] = cands[i];
                }
            }
        }
        for (int l : locals)
            if (map[l] < 0) map[l] = r.numStitched++;
        r.localToStitched[k] = map;
        coveredEnd = std::max(coveredEnd, w.startFrame + act.frames);
    }
    return r;
}

// ---- aggregation + turns --------------------------------------------------------------------------

bool FrameLabels::has(int f, int id) const
{
    for (int k = 0; k < maxActive; ++k)
        if (at(f, k) == id) return true;
    return false;
}

int FrameLabels::count(int f) const
{
    int n = 0;
    for (int k = 0; k < maxActive; ++k)
        if (at(f, k) >= 0) ++n;
    return n;
}

FrameLabels aggregate(const QVector<WindowActivity>& windows, const StitchResult& stitch, int totalFrames,
                      int maxActive)
{
    FrameLabels out;
    out.frames = std::max(0, totalFrames);
    out.maxActive = std::max(1, maxActive);
    out.ids.fill(-1, qsizetype(out.frames) * out.maxActive);
    if (out.frames == 0) return out;

    using Scores = QVarLengthArray<std::pair<int, float>, 4>;
    QVector<Scores> scores(out.frames);
    QVector<float> countSum(out.frames, 0.0f);
    QVector<int> cover(out.frames, 0);
    for (int k = 0; k < windows.size(); ++k) {
        const WindowActivity& w = windows[k];
        const QVector<int>& map = stitch.localToStitched.value(k);
        for (int f = 0; f < w.activity.frames; ++f) {
            const int g = w.startFrame + f;
            if (g < 0) continue;
            if (g >= out.frames) break;
            ++cover[g];
            countSum[g] += float(w.activity.countAt(f));
            for (int s = 0; s < w.activity.speakers; ++s) {
                const int id = s < map.size() ? map[s] : -1;
                if (id < 0 || w.activity.at(f, s) <= 0.5f) continue;
                Scores& sc = scores[g];
                auto it = std::find_if(sc.begin(), sc.end(), [id](const auto& p) { return p.first == id; });
                if (it == sc.end()) sc.append({id, 1.0f});
                else it->second += 1.0f;
            }
        }
    }
    for (int g = 0; g < out.frames; ++g) {
        if (cover[g] == 0) continue;
        int c = int(std::lround(double(countSum[g]) / cover[g]));
        c = std::min(c, out.maxActive);
        Scores& sc = scores[g];
        std::sort(sc.begin(), sc.end(), [](const auto& a, const auto& b) {
            return a.second != b.second ? a.second > b.second : a.first < b.first;
        });
        for (int k = 0, n = 0; k < sc.size() && n < c; ++k) {
            if (sc[k].second <= 0) break;
            out.ids[g * out.maxActive + n++] = sc[k].first;
        }
    }
    return out;
}

QVector<Span> applyMinDuration(QVector<Span> spans, double minOn, double minOff)
{
    std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.start < b.start; });
    QVector<Span> merged;
    for (const Span& s : spans) {
        if (!merged.isEmpty() && minOff > 0 && s.start - merged.last().end < minOff)
            merged.last().end = std::max(merged.last().end, s.end);
        else
            merged.append(s);
    }
    QVector<Span> out;
    for (const Span& s : merged)
        if (minOn <= 0 || s.duration() >= minOn) out.append(s);
    return out;
}

void flagOverlaps(QVector<Turn>& turns)
{
    std::sort(turns.begin(), turns.end(), [](const Turn& a, const Turn& b) {
        return a.start != b.start ? a.start < b.start : a.speaker < b.speaker;
    });
    for (Turn& t : turns) t.overlap = false;
    for (int i = 0; i < turns.size(); ++i)
        for (int j = i + 1; j < turns.size() && turns[j].start < turns[i].end; ++j)
            if (turns[j].speaker != turns[i].speaker) turns[i].overlap = turns[j].overlap = true;
}

QVector<Turn> labelsToTurns(const FrameLabels& labels, const ModelInfo& info, double minOn, double minOff)
{
    QHash<int, QVector<Span>> spans;
    QHash<int, int> open;   // id -> first frame of its running run
    auto close = [&](int id, int start, int endExcl) {
        spans[id].append({info.frameTimeSec(start), info.frameTimeSec(endExcl)});
    };
    for (int f = 0; f <= labels.frames; ++f) {
        // Close the runs that stop at f.
        for (auto it = open.begin(); it != open.end();) {
            if (f == labels.frames || !labels.has(f, it.key())) {
                close(it.key(), it.value(), f);
                it = open.erase(it);
            } else {
                ++it;
            }
        }
        if (f == labels.frames) break;
        for (int k = 0; k < labels.maxActive; ++k) {
            const int id = labels.at(f, k);
            if (id >= 0 && !open.contains(id)) open.insert(id, f);
        }
    }
    QVector<Turn> turns;
    for (auto it = spans.begin(); it != spans.end(); ++it)
        for (const Span& s : applyMinDuration(it.value(), minOn, minOff))
            turns.append({s.start, s.end, it.key(), false});
    flagOverlaps(turns);
    return turns;
}

QVector<LocalTurn> windowTurns(int windowIndex, const WindowActivity& w, const ModelInfo& info)
{
    QVector<LocalTurn> out;
    const Activity& a = w.activity;
    for (int s = 0; s < a.speakers; ++s) {
        int f = 0;
        while (f < a.frames) {
            if (a.at(f, s) <= 0.5f) { ++f; continue; }
            LocalTurn t;
            t.window = windowIndex;
            t.localSpeaker = s;
            const int start = f;
            while (f < a.frames && a.at(f, s) > 0.5f) {
                if (a.countAt(f) >= 2) t.overlap = true;
                ++f;
            }
            t.startFrame = w.startFrame + start;
            t.endFrame = w.startFrame + f;
            t.start = info.frameTimeSec(t.startFrame);
            t.end = info.frameTimeSec(t.endFrame);
            out.append(t);
        }
    }
    std::sort(out.begin(), out.end(), [](const LocalTurn& x, const LocalTurn& y) {
        return x.startFrame != y.startFrame ? x.startFrame < y.startFrame : x.localSpeaker < y.localSpeaker;
    });
    return out;
}

} // namespace tanara::processing::segmentation
