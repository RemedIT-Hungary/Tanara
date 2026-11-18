#include "tanara/editing/review/RemoteLineClusters.h"

#include <QHash>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace tanara::editing::remotelines {

namespace {

using Windows = QVector<QPair<qint64, qint64>>;

constexpr float kCannotLink = -3.0f;

double dot(const QVector<float>& a, const QVector<float>& b)
{
    const int n = std::min(a.size(), b.size());
    double s = 0.0;
    for (int k = 0; k < n; ++k) s += double(a[k]) * double(b[k]);
    return s;
}

// Cosine of two L2-normalized vectors (different sizes: no similarity).
double cosine(const QVector<float>& a, const QVector<float>& b)
{
    if (a.isEmpty() || a.size() != b.size()) return -1.0;
    return dot(a, b);
}

QVector<float> normalized(const QVector<double>& v)
{
    double n = 0.0;
    for (double x : v) n += x * x;
    n = std::sqrt(n);
    QVector<float> out(v.size(), 0.0f);
    if (n <= 0.0) return out;
    for (int k = 0; k < v.size(); ++k) out[k] = float(v[k] / n);
    return out;
}

qint64 overlapWith(const Line& l, const Windows& ws)
{
    qint64 sum = 0;
    for (const auto& w : ws) sum += std::max<qint64>(0, std::min(l.endMs, w.second) - std::max(l.startMs, w.first));
    return sum;
}

// A clustering unit: a weighted vector sum (the duration-weighted centroid unnormalized).
struct Unit {
    QVector<double> sum;
    double norm = 0.0;
    double weight = 0.0;    // total duration weight
    QVector<int> members;
    QString locked;     // the locked owner of its locked lines (at most one: cannot-link)
    bool alive = true;
};

double unitCos(const Unit& a, const Unit& b)
{
    if (a.norm <= 0.0 || b.norm <= 0.0 || a.sum.size() != b.sum.size()) return -1.0;
    double s = 0.0;
    for (int k = 0; k < a.sum.size(); ++k) s += a.sum[k] * b.sum[k];
    return s / (a.norm * b.norm);
}

void addVector(Unit& u, const QVector<float>& v, double w)
{
    u.weight += w;
    if (u.sum.isEmpty()) u.sum.fill(0.0, v.size());
    for (int k = 0; k < v.size() && k < u.sum.size(); ++k) u.sum[k] += w * double(v[k]);
    double n = 0.0;
    for (double x : std::as_const(u.sum)) n += x * x;
    u.norm = std::sqrt(n);
}

// Best target per similarity (one value per distinct target: the max over its clusters).
struct Ranked {
    QString best, second;
    double bestFit = -2.0, secondFit = -2.0;
};

Ranked rankTargets(const QVector<float>& e, const QVector<Cluster>& clusters)
{
    QHash<QString, double> per;
    for (const Cluster& c : clusters) {
        const double s = cosine(e, c.centroid);
        auto it = per.find(c.target);
        if (it == per.end()) per.insert(c.target, s);
        else if (s > it.value()) it.value() = s;
    }
    Ranked r;
    // Deterministic order: by target id on ties.
    QStringList ids = per.keys();
    std::sort(ids.begin(), ids.end());
    for (const QString& id : std::as_const(ids)) {
        const double s = per.value(id);
        if (s > r.bestFit) {
            r.second = r.best; r.secondFit = r.bestFit;
            r.best = id; r.bestFit = s;
        } else if (s > r.secondFit) {
            r.second = id; r.secondFit = s;
        }
    }
    return r;
}

} // namespace

QString basisName(Basis b)
{
    switch (b) {
    case Basis::Unchanged: return QStringLiteral("unchanged");
    case Basis::Cluster:   return QStringLiteral("cluster");
    case Basis::Nearest:   return QStringLiteral("nearest");
    case Basis::Turn:      return QStringLiteral("turn");
    case Basis::Raw:       return QStringLiteral("raw");
    }
    return {};
}

Result assign(const QVector<Line>& lines, const QVector<Target>& targets, const Windows& otherWindows,
              const Options& opt)
{
    Result r;
    const int n = lines.size();
    r.lines.resize(n);

    QHash<QString, int> targetIdx;
    for (int t = 0; t < targets.size(); ++t) targetIdx.insert(targets[t].id, t);
    auto isLocal = [&](const QString& id) {
        const int t = targetIdx.value(id, -1);
        return t >= 0 && targets[t].local;
    };
    // A target that may own a remote cluster.
    auto eligible = [&](const Target& t) { return !opt.sideKnown || !t.local; };

    // ---- which lines are decided here --------------------------------------------------------
    QVector<bool> candidate(n, false);
    for (int i = 0; i < n; ++i) {
        const Line& l = lines[i];
        if (!l.lockedTarget.isEmpty() || l.ignored) continue;
        if (!opt.sideKnown) { candidate[i] = true; continue; }
        if (l.side == Side::Remote) candidate[i] = true;
        else if (l.side == Side::Mixed || l.side == Side::Unknown) candidate[i] = !isLocal(l.currentTarget);
    }

    // ---- 1. seeds + locked units ------------------------------------------------------------
    auto weightOf = [&](const Line& l) {
        return double(std::min(std::max<qint64>(0, l.durationMs()), opt.maxWeightMs)) / 1000.0;
    };
    QVector<Unit> units;
    QHash<QString, int> lockedUnit;
    for (int i = 0; i < n; ++i) {
        const Line& l = lines[i];
        if (l.embedding.isEmpty() || l.ignored) continue;
        if (!l.lockedTarget.isEmpty()) {
            const int t = targetIdx.value(l.lockedTarget, -1);
            if (t < 0 || !eligible(targets[t])) continue;
            if (opt.sideKnown && l.side == Side::Local) continue;
            auto it = lockedUnit.find(l.lockedTarget);
            if (it == lockedUnit.end()) {
                Unit u;
                u.locked = l.lockedTarget;
                units.append(u);
                it = lockedUnit.insert(l.lockedTarget, units.size() - 1);
            }
            Unit& u = units[it.value()];
            addVector(u, l.embedding, weightOf(l));
            u.members.append(i);
            continue;
        }
        if (!candidate[i] || l.durationMs() < opt.seedMinMs) continue;
        if (opt.sideKnown && l.side != Side::Remote) continue;   // Mixed / Unknown lines do not seed
        Unit u;
        addVector(u, l.embedding, weightOf(l));
        u.members.append(i);
        units.append(u);
        ++r.seeds;
    }
    if (units.isEmpty()) {
        r.skipped = QStringLiteral("no-seeds");
        return r;
    }

    // ---- 2. agglomerative clustering (centroid linkage) -------------------------------------
    const int m = units.size();
    QVector<float> sim(qsizetype(m) * m, kCannotLink);
    auto at = [&](int i, int j) -> float& { return sim[qsizetype(i) * m + j]; };
    // The initial similarity: average linkage = the weighted mean pairwise cosine of the members (a locked unit
    // has several), centroid linkage = the centroids' cosine.
    auto pairSim = [&](int i, int j) -> float {
        const Unit& a = units[i];
        const Unit& b = units[j];
        if (!a.locked.isEmpty() && !b.locked.isEmpty() && a.locked != b.locked) return kCannotLink;
        if (opt.linkage == Linkage::Centroid || (a.members.size() == 1 && b.members.size() == 1))
            return float(unitCos(a, b));
        double sum = 0.0, wsum = 0.0;
        for (int x : a.members)
            for (int y : b.members) {
                const double w = weightOf(lines[x]) * weightOf(lines[y]);
                sum += w * cosine(lines[x].embedding, lines[y].embedding);
                wsum += w;
            }
        return wsum > 0.0 ? float(sum / wsum) : -1.0f;
    };
    for (int i = 0; i < m; ++i)
        for (int j = i + 1; j < m; ++j) at(i, j) = at(j, i) = pairSim(i, j);
    for (;;) {
        float best = -2.0f;
        int a = -1, b = -1;
        for (int i = 0; i < m; ++i) {
            if (!units[i].alive) continue;
            const float* row = &sim[qsizetype(i) * m];
            for (int j = i + 1; j < m; ++j)
                if (row[j] > best && units[j].alive) { best = row[j]; a = i; b = j; }
        }
        if (a < 0 || best < float(opt.mergeThreshold)) break;
        Unit& ua = units[a];
        Unit& ub = units[b];
        const double wa = ua.weight, wb = ub.weight;
        for (int k = 0; k < ua.sum.size() && k < ub.sum.size(); ++k) ua.sum[k] += ub.sum[k];
        double nn = 0.0;
        for (double x : std::as_const(ua.sum)) nn += x * x;
        ua.norm = std::sqrt(nn);
        ua.weight = wa + wb;
        ua.members += ub.members;
        if (ua.locked.isEmpty()) ua.locked = ub.locked;
        ub.alive = false;
        ub.members.clear();
        for (int k = 0; k < m; ++k) {
            if (k == a || !units[k].alive) continue;
            float v;
            if (!ua.locked.isEmpty() && !units[k].locked.isEmpty() && ua.locked != units[k].locked) v = kCannotLink;
            else if (opt.linkage == Linkage::Average)   // Lance-Williams (weighted average linkage)
                v = float((wa * at(a, k) + wb * at(b, k)) / std::max(1e-9, wa + wb));
            else v = float(unitCos(ua, units[k]));
            at(a, k) = at(k, a) = v;
        }
    }

    // ---- 3. clusters, tiny ones dissolved ---------------------------------------------------
    QVector<Cluster> clusters;
    QVector<QString> lockedOf;   // per cluster: the locked owner (empty = none)
    for (const Unit& u : std::as_const(units)) {
        if (!u.alive || u.members.isEmpty()) continue;
        Cluster c;
        c.lines = u.members;
        std::sort(c.lines.begin(), c.lines.end(),
                  [&](int x, int y) { return lines[x].startMs < lines[y].startMs || (lines[x].startMs == lines[y].startMs && x < y); });
        for (int i : std::as_const(c.lines)) {
            c.seedMs += lines[i].durationMs();
            if (!lines[i].lockedTarget.isEmpty()) ++c.lockedLines;
        }
        if (u.locked.isEmpty() && (c.seedMs < opt.minClusterMs || c.lines.size() < opt.minClusterLines)) {
            ++r.dissolved;
            continue;
        }
        c.centroid = normalized(u.sum);
        clusters.append(c);
        lockedOf.append(u.locked);
    }
    // Deterministic order: by the first line's start.
    QVector<int> order(clusters.size());
    for (int k = 0; k < order.size(); ++k) order[k] = k;
    std::sort(order.begin(), order.end(), [&](int x, int y) {
        return lines[clusters[x].lines.first()].startMs < lines[clusters[y].lines.first()].startMs
            || (lines[clusters[x].lines.first()].startMs == lines[clusters[y].lines.first()].startMs && x < y);
    });
    {
        QVector<Cluster> sorted;
        QVector<QString> sortedLocked;
        for (int k : std::as_const(order)) { sorted.append(clusters[k]); sortedLocked.append(lockedOf[k]); }
        clusters = sorted;
        lockedOf = sortedLocked;
    }
    if (clusters.isEmpty()) {
        r.skipped = QStringLiteral("no-clusters");
        return r;
    }

    // ---- 4. binding clusters to targets -----------------------------------------------------
    for (int k = 0; k < clusters.size(); ++k) {
        Cluster& c = clusters[k];
        qint64 lockedMs = 0;
        for (int i : std::as_const(c.lines))
            if (!lines[i].lockedTarget.isEmpty()) lockedMs += lines[i].durationMs();
        if (!lockedOf[k].isEmpty() && c.seedMs > 0 && double(lockedMs) / double(c.seedMs) >= opt.lockedMinShare) {
            c.target = lockedOf[k];
            c.bindBasis = QStringLiteral("locked");
            continue;
        }
        // (b) pre-transcript windows.
        QVector<qint64> ov(targets.size(), 0);
        qint64 total = 0;
        for (int i : std::as_const(c.lines)) {
            const Line& l = lines[i];
            for (int t = 0; t < targets.size(); ++t) {
                if (!targets[t].named || !eligible(targets[t]) || targets[t].windows.isEmpty()) continue;
                const qint64 o = overlapWith(l, targets[t].windows);
                ov[t] += o;
                total += o;
            }
            total += overlapWith(l, otherWindows);
        }
        int bestW = -1;
        for (int t = 0; t < targets.size(); ++t)
            if (ov[t] > 0 && (bestW < 0 || ov[t] > ov[bestW])) bestW = t;
        // (c) voiceprints.
        QVector<double> print(targets.size(), qQNaN());
        int bestP = -1, secondP = -1;
        for (int t = 0; t < targets.size(); ++t) {
            if (!targets[t].named || !eligible(targets[t]) || targets[t].prints.isEmpty()) continue;
            double s = -2.0;
            for (const QVector<float>& p : targets[t].prints) s = std::max(s, cosine(c.centroid, p));
            print[t] = s;
            if (bestP < 0 || s > print[bestP]) { secondP = bestP; bestP = t; }
            else if (secondP < 0 || s > print[secondP]) secondP = t;
        }
        if (bestW >= 0 && ov[bestW] >= opt.bindMinOverlapMs && total > 0
            && double(ov[bestW]) / double(total) >= opt.bindMinShare) {
            c.target = targets[bestW].id;
            c.bindBasis = QStringLiteral("windows");
            c.windowMs = ov[bestW];
            c.windowShare = double(ov[bestW]) / double(total);
            c.printScore = print[bestW];
            continue;
        }
        if (bestP >= 0) {
            const double second = secondP >= 0 ? print[secondP] : -1.0;
            if (print[bestP] >= opt.printMatch && print[bestP] - second >= opt.printMargin) {
                c.target = targets[bestP].id;
                c.bindBasis = QStringLiteral("voiceprint");
                c.printScore = print[bestP];
                c.windowMs = ov[bestP];
                if (total > 0) c.windowShare = double(ov[bestP]) / double(total);
                continue;
            }
        }
        if (!lockedOf[k].isEmpty()) {   // a small locked share still beats an anonymous voice
            c.target = lockedOf[k];
            c.bindBasis = QStringLiteral("locked");
            continue;
        }
        c.target = QStringLiteral("anon:%1").arg(++r.anonymous);
        c.bindBasis = QStringLiteral("anonymous");
    }

    // ---- 5. line decisions ------------------------------------------------------------------
    QVector<QString> owner(n);   // the known remote owner of a line (decided here, or locked non-local)
    for (int i = 0; i < n; ++i) {
        const Line& l = lines[i];
        if (!l.lockedTarget.isEmpty() && !(opt.sideKnown && (l.side == Side::Local || isLocal(l.lockedTarget))))
            owner[i] = l.lockedTarget;
    }
    // Seed lines: their cluster's target; weak if another target fits nearly as well.
    for (int k = 0; k < clusters.size(); ++k) {
        const Cluster& c = clusters[k];
        for (int i : c.lines) {
            if (!candidate[i]) continue;
            LineDecision& d = r.lines[i];
            d.target = c.target;
            d.basis = Basis::Cluster;
            d.cluster = k;
            const double own = cosine(lines[i].embedding, c.centroid);
            double other = -2.0;
            QString otherTarget;
            for (const Cluster& o : std::as_const(clusters)) {
                if (o.target == c.target) continue;
                const double s = cosine(lines[i].embedding, o.centroid);
                if (s > other) { other = s; otherTarget = o.target; }
            }
            if (!otherTarget.isEmpty() && own - other < opt.weakMargin) {
                d.weak = true;
                d.hint = otherTarget;
            }
            owner[i] = c.target;
        }
    }
    // Lines with a vector outside the clusters: the nearest cluster if clearly closest.
    for (int i = 0; i < n; ++i) {
        if (!candidate[i] || r.lines[i].basis != Basis::Unchanged || lines[i].embedding.isEmpty()) continue;
        const Ranked rk = rankTargets(lines[i].embedding, clusters);
        if (rk.best.isEmpty()) continue;
        const double margin = lines[i].durationMs() >= opt.seedMinMs ? opt.nearestMargin : opt.nearestMarginShort;
        const double gap = rk.second.isEmpty() ? 2.0 : rk.bestFit - rk.secondFit;
        if (rk.bestFit < opt.nearestMinFit || gap < margin) continue;
        LineDecision& d = r.lines[i];
        d.target = rk.best;
        d.basis = Basis::Nearest;
        owner[i] = rk.best;
    }

    // The remaining candidates in time order: the turn, then the raw speaker's recent owner.
    QVector<int> byTime(n);
    for (int i = 0; i < n; ++i) byTime[i] = i;
    std::stable_sort(byTime.begin(), byTime.end(), [&](int x, int y) { return lines[x].startMs < lines[y].startMs; });
    constexpr int kScan = 12;   // neighbours looked at in each direction
    // The closest decided line before / after position p within the turn gap (-1 = none).
    auto neighbours = [&](int p, int* prev, int* next) {
        const Line& l = lines[byTime[p]];
        *prev = *next = -1;
        for (int q = p - 1; q >= 0 && q >= p - kScan; --q) {
            const int j = byTime[q];
            if (owner[j].isEmpty()) continue;
            if (l.startMs - lines[j].endMs <= opt.turnGapMs) *prev = j;
            break;
        }
        for (int q = p + 1; q < n && q <= p + kScan; ++q) {
            const int j = byTime[q];
            if (owner[j].isEmpty()) continue;
            if (lines[j].startMs - l.endMs <= opt.turnGapMs) *next = j;
            break;
        }
    };
    // A weak decision for a line WITHOUT a vector keeps its current (non-local) owner: the guess is only offered
    // as the suggestion of the uncertain line (measured: the turn / raw-speaker guess is not better than the
    // provider's mapping for such lines).
    auto keepIfWeak = [&](int i, LineDecision& d) {
        const Line& l = lines[i];
        if (!opt.keepWeakShort || !d.weak || !l.embedding.isEmpty() || l.currentTarget.isEmpty()
            || isLocal(l.currentTarget) || d.target == l.currentTarget)
            return;
        d.hint = d.target;
        d.target = l.currentTarget;
    };
    if (opt.smoothTurns) {
        // A weak cluster line between two lines of the same other target (its runner-up) joins their turn.
        for (int p = 0; p < n; ++p) {
            const int i = byTime[p];
            LineDecision& d = r.lines[i];
            if (!candidate[i] || d.basis != Basis::Cluster || !d.weak) continue;
            int prev = -1, next = -1;
            neighbours(p, &prev, &next);
            if (prev < 0 || next < 0 || owner[prev] != owner[next] || owner[prev] == d.target || owner[prev] != d.hint)
                continue;
            d.hint = d.target;
            d.target = owner[prev];
            d.basis = Basis::Turn;
            owner[i] = d.target;
        }
    }
    for (int p = 0; p < n; ++p) {
        const int i = byTime[p];
        if (!candidate[i] || r.lines[i].basis != Basis::Unchanged) continue;
        const Line& l = lines[i];
        int prev = -1, next = -1;
        neighbours(p, &prev, &next);
        if (prev < 0 && next < 0) continue;
        LineDecision& d = r.lines[i];
        d.basis = Basis::Turn;
        if (prev >= 0 && next >= 0 && owner[prev] == owner[next]) {
            d.target = owner[prev];
        } else if (prev >= 0 && next >= 0) {
            // The neighbours disagree: the one with the same raw speaker, else the previous (the turn goes on).
            const bool nextSame = lines[next].rawLabel == l.rawLabel && lines[prev].rawLabel != l.rawLabel;
            d.target = nextSame ? owner[next] : owner[prev];
            d.hint = nextSame ? owner[prev] : owner[next];
            d.weak = true;
        } else {
            const int j = prev >= 0 ? prev : next;
            d.target = owner[j];
            d.weak = lines[j].rawLabel != l.rawLabel;
        }
        keepIfWeak(i, d);
        owner[i] = d.target;
    }
    for (int p = 0; p < n; ++p) {
        const int i = byTime[p];
        if (!candidate[i] || r.lines[i].basis != Basis::Unchanged) continue;
        const Line& l = lines[i];
        // Owners of the same raw speaker's decided lines: around this time, else in the whole meeting; failing
        // that, of any decided remote line around this time / at all (a Remote line must not stay local).
        auto tally = [&](bool near, bool sameRaw) {
            QMap<QString, qint64> per;
            for (int j = 0; j < n; ++j) {
                if (owner[j].isEmpty() || j == i || (sameRaw && lines[j].rawLabel != l.rawLabel)) continue;
                if (near && std::llabs(lines[j].startMs - l.startMs) > opt.rawWindowMs) continue;
                per[owner[j]] += std::max<qint64>(1, lines[j].durationMs());
            }
            return per;
        };
        QMap<QString, qint64> per = tally(true, true);
        if (per.isEmpty()) per = tally(false, true);
        if (per.isEmpty()) per = tally(true, false);
        if (per.isEmpty()) per = tally(false, false);
        if (per.isEmpty()) continue;
        QString best, second;
        qint64 bestV = -1, secondV = -1;
        for (auto it = per.cbegin(); it != per.cend(); ++it) {
            if (it.value() > bestV) { second = best; secondV = bestV; best = it.key(); bestV = it.value(); }
            else if (it.value() > secondV) { second = it.key(); secondV = it.value(); }
        }
        LineDecision& d = r.lines[i];
        d.target = best;
        d.basis = Basis::Raw;
        d.weak = true;
        d.hint = second;
        keepIfWeak(i, d);
        owner[i] = d.target;
    }

    // Safety net: a Remote line never goes to a local target (none can own a cluster; checked anyway).
    for (int i = 0; i < n; ++i) {
        LineDecision& d = r.lines[i];
        if (opt.sideKnown && lines[i].side == Side::Remote && !d.target.isEmpty() && isLocal(d.target)) d = LineDecision();
    }
    r.clusters = clusters;
    r.ran = true;
    return r;
}

} // namespace tanara::editing::remotelines
