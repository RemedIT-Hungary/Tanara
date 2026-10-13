#include "tanara/edit/SpeakerAnalysis.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>

namespace tanara {
namespace speakeredit {

namespace {

using Vec = QVector<double>;

// A line's weight in the centroid: its length (s), clipped to the window used.
double weightOf(const AnalysisLine& l)
{
    return double(std::clamp<qint64>(l.durationMs, 500, kMaxEmbedMs)) / 1000.0;
}

void addScaled(Vec& acc, const QVector<float>& e, double w)
{
    if (acc.size() < e.size()) acc.resize(e.size());
    for (int k = 0; k < e.size(); ++k) acc[k] += w * double(e[k]);
}

double dot(const Vec& a, const QVector<float>& e)
{
    const int n = std::min<int>(a.size(), e.size());
    double s = 0.0;
    for (int k = 0; k < n; ++k) s += a[k] * double(e[k]);
    return s;
}

double norm2(const Vec& a)
{
    double s = 0.0;
    for (double v : a) s += v * v;
    return s;
}

// cos(e, sum), where e is a unit vector.
double cosTo(const Vec& sum, double sumNorm2, const QVector<float>& e)
{
    if (sumNorm2 <= 1e-12) return qQNaN();
    return dot(sum, e) / std::sqrt(sumNorm2);
}

// cos(e, sum - w*e): the centroid WITHOUT the line (leave-one-out), e a unit vector.
double cosToWithout(const Vec& sum, double sumNorm2, const QVector<float>& e, double w)
{
    const double d = dot(sum, e);
    const double n2 = sumNorm2 - 2.0 * w * d + w * w;
    if (n2 <= 1e-9) return qQNaN();
    return (d - w) / std::sqrt(n2);
}

} // namespace

namespace {

// The shared part of building centroids: per speaker the weighted sum and the number of lines in it.
struct Centroids {
    QVector<Vec> sums;
    QVector<double> norms;
    QVector<int> counts;
    QVector<bool> member;           // per line: whether it is in its own speaker's centroid
    QVector<bool> printReady;       // per speaker: a voiceprint is in it too, so it is ready regardless of the line count
};

bool readyWith(const Centroids& c, int s, int minus)
{
    return (!c.printReady.isEmpty() && c.printReady[s]) || c.counts[s] - minus >= kMinSpeakerLines;
}

bool validLine(const AnalysisLine& l, int speakerCount)
{
    return l.hasEmbedding() && l.speaker >= 0 && l.speaker < speakerCount;
}

// The normal rule: every line, but noisy ones only if there are not enough clean lines.
QVector<bool> cleanEnough(const QVector<AnalysisLine>& lines, int speakerCount)
{
    QVector<int> clean(speakerCount, 0);
    for (const AnalysisLine& l : lines)
        if (validLine(l, speakerCount) && !l.noisy) ++clean[l.speaker];
    QVector<bool> out(speakerCount);
    for (int s = 0; s < speakerCount; ++s) out[s] = clean[s] >= kMinSpeakerLines;
    return out;
}

Centroids buildCentroids(const QVector<AnalysisLine>& lines, int speakerCount,
                         const std::function<bool(const AnalysisLine&)>& include)
{
    Centroids c;
    c.sums.resize(speakerCount);
    c.norms.resize(speakerCount);
    c.counts.fill(0, speakerCount);
    c.member.fill(false, lines.size());
    c.printReady.fill(false, speakerCount);
    for (int i = 0; i < lines.size(); ++i) {
        const AnalysisLine& l = lines[i];
        if (!validLine(l, speakerCount) || !include(l)) continue;
        addScaled(c.sums[l.speaker], *l.embedding, weightOf(l));
        ++c.counts[l.speaker];
        c.member[i] = true;
    }
    for (int s = 0; s < speakerCount; ++s) c.norms[s] = norm2(c.sums[s]);
    return c;
}

// A line's fit to the centroids (to its own with leave-one-out, if it is in it).
LineFit fitOf(const Centroids& c, const QVector<AnalysisLine>& lines, int i, int speakerCount)
{
    LineFit f;
    const AnalysisLine& l = lines[i];
    if (c.member[i]) {
        if (readyWith(c, l.speaker, 1))
            f.own = cosToWithout(c.sums[l.speaker], c.norms[l.speaker], *l.embedding, weightOf(l));
    } else if (readyWith(c, l.speaker, 0)) {
        f.own = cosTo(c.sums[l.speaker], c.norms[l.speaker], *l.embedding);
    }
    for (int s = 0; s < speakerCount; ++s) {
        if (s == l.speaker || !readyWith(c, s, 0)) continue;
        const double v = cosTo(c.sums[s], c.norms[s], *l.embedding);
        if (!std::isnan(v) && (std::isnan(f.other) || v > f.other)) {
            f.other = v;
            f.otherSpeaker = s;
        }
    }
    return f;
}

// The thresholds: another speaker fits better, or (for a reliable long line) not even its own.
bool judgeUncertain(const AnalysisLine& l, const LineFit& f)
{
    if (std::isnan(f.own)) return false;    // not enough samples of its own speaker: we do not judge
    const bool reliable = l.durationMs >= kReliableMs;
    const double margin = reliable ? kUncertainMargin : kUncertainMarginShort;
    if (!std::isnan(f.other) && f.other - f.own >= margin)
        return true;                        // it resembles another speaker better
    return reliable && f.own < kUncertainMinFit;    // it does not even resemble its own (a new, unseparated voice)
}

} // namespace

QVector<LineFit> computeFits(const QVector<AnalysisLine>& lines, int speakerCount)
{
    QVector<LineFit> fits(lines.size());
    if (speakerCount <= 0) return fits;
    const QVector<bool> clean = cleanEnough(lines, speakerCount);
    const Centroids c = buildCentroids(lines, speakerCount, [&](const AnalysisLine& l) {
        return !l.noisy || !clean[l.speaker];
    });
    for (int i = 0; i < lines.size(); ++i)
        if (validLine(lines[i], speakerCount)) fits[i] = fitOf(c, lines, i, speakerCount);
    return fits;
}

QVector<bool> computeUncertain(const QVector<AnalysisLine>& lines, int speakerCount)
{
    QVector<bool> out(lines.size(), false);
    const QVector<LineFit> fits = computeFits(lines, speakerCount);
    for (int i = 0; i < lines.size(); ++i) {
        const AnalysisLine& l = lines[i];
        if (l.locked || !l.hasEmbedding() || l.durationMs < kMinEmbedMs) continue;
        out[i] = judgeUncertain(l, fits[i]);
    }
    return out;
}

namespace {

const SpeakerPrior* priorOf(const QVector<SpeakerPrior>& priors, int speaker)
{
    if (speaker < 0 || speaker >= priors.size() || priors[speaker].isEmpty()) return nullptr;
    return &priors[speaker];
}

// Per line: its voice is already carried by a local voiceprint (it does not count separately in the reference).
QVector<bool> replacedMask(const QVector<AnalysisLine>& lines, const QVector<SpeakerPrior>& priors)
{
    QVector<bool> out(lines.size(), false);
    for (const SpeakerPrior& p : priors)
        for (int i : p.replacedLines)
            if (i >= 0 && i < out.size()) out[i] = true;
    return out;
}

// Adding the voiceprints to a speaker's reference sum with the weight rule (see SpeakerPrior in the
// header). localLineWeight: the weight (s) of the lines that went into the reference.
// localCore: whether there is a local core; if not, only the earlier voiceprints go in (at full weight).
void addPrints(Vec& sum, const SpeakerPrior* p, double localLineWeight, bool localCore,
               ReferenceInfo& info)
{
    if (!p) return;
    double localWeight = localLineWeight;
    if (localCore) {
        const double w = double(kLocalPrintMs) / 1000.0;
        for (const QVector<float>& v : p->localVectors) {
            if (v.isEmpty()) continue;
            addScaled(sum, v, w);
            localWeight += w;
            ++info.localPrints;
        }
    }
    int n = 0;
    for (const QVector<float>& v : p->vectors) n += v.isEmpty() ? 0 : 1;
    if (n == 0) return;
    double per = double(kPriorPrintMs) / 1000.0;
    if (localCore) {
        const double cap = kPriorMaxShare * localWeight;
        if (per * n > cap) {
            per = cap / n;
            info.priorCapped = true;
        }
    }
    if (per <= 0.0) return;
    for (const QVector<float>& v : p->vectors) {
        if (v.isEmpty()) continue;
        addScaled(sum, v, per);
        ++info.priorPrints;
    }
    const double priorWeight = per * info.priorPrints;
    info.priorShare = priorWeight / (priorWeight + localWeight);
}

// Per speaker the reference mode: 2 = only the clean locked lines, 1 = every locked line (including
// noisy, since there are not enough clean ones), 3 = a local voiceprint + the (few) clean locked lines,
// 4 = the earlier voiceprints + the (few) clean locked lines, 0 = no core (the normal rule).
// Lines folded into a local voiceprint (replaced) do not count.
enum { kModeNone = 0, kModeLockedAll = 1, kModeLockedClean = 2, kModeLocalPrint = 3, kModePrior = 4 };

QVector<int> coreModes(const QVector<AnalysisLine>& lines, int speakerCount,
                       const QVector<SpeakerPrior>& priors, const QVector<bool>& replaced)
{
    QVector<int> lockedClean(speakerCount, 0), lockedAll(speakerCount, 0);
    for (int i = 0; i < lines.size(); ++i) {
        const AnalysisLine& l = lines[i];
        if (!validLine(l, speakerCount) || !l.locked || replaced.value(i)) continue;
        ++lockedAll[l.speaker];
        if (!l.noisy) ++lockedClean[l.speaker];
    }
    QVector<int> mode(speakerCount, kModeNone);
    for (int s = 0; s < speakerCount; ++s) {
        const SpeakerPrior* p = priorOf(priors, s);
        if (lockedClean[s] >= kMinSpeakerLines) mode[s] = kModeLockedClean;
        else if (lockedAll[s] >= kMinSpeakerLines) mode[s] = kModeLockedAll;
        else if (p && !p->localVectors.isEmpty()) mode[s] = kModeLocalPrint;
        else if (p && !p->vectors.isEmpty()) mode[s] = kModePrior;
    }
    return mode;
}

bool isLocalMode(int m) { return m == kModeLockedAll || m == kModeLockedClean || m == kModeLocalPrint; }

} // namespace

bool hasTrustedCore(const QVector<AnalysisLine>& lines, int speakerCount,
                    const QVector<SpeakerPrior>& priors)
{
    if (speakerCount <= 0) return false;
    const QVector<int> mode = coreModes(lines, speakerCount, priors, replacedMask(lines, priors));
    return std::any_of(mode.cbegin(), mode.cend(), isLocalMode);
}

RecheckAnalysis computeUncertainRechecked(const QVector<AnalysisLine>& lines, int speakerCount,
                                          const QVector<SpeakerPrior>& priors)
{
    RecheckAnalysis out;
    out.lines.resize(lines.size());
    if (speakerCount <= 0) return out;
    const QVector<bool> replaced = replacedMask(lines, priors);
    const QVector<int> mode = coreModes(lines, speakerCount, priors, replaced);
    const QVector<bool> clean = cleanEnough(lines, speakerCount);
    // Lines folded into a local voiceprint are left out (the voiceprint counts instead).
    Centroids c = buildCentroids(lines, speakerCount, [&](const AnalysisLine& l) {
        if (replaced[int(&l - lines.constData())]) return false;
        switch (mode[l.speaker]) {
        case kModeLockedClean:
        case kModeLocalPrint:  return l.locked && !l.noisy;
        case kModeLockedAll:   return l.locked;
        // Only an earlier voiceprint gives a core: the (few) clean confirmed lines join it so the local
        // acoustics count too; the voiceprint's weight is not capped then.
        case kModePrior:       return l.locked && !l.noisy;
        default:               return !l.noisy || !clean[l.speaker];     // the normal rule
        }
    });
    out.trustedCore.resize(speakerCount);
    out.coreLines.fill(0, speakerCount);
    out.refs.resize(speakerCount);
    for (int s = 0; s < speakerCount; ++s) {
        ReferenceInfo& info = out.refs[s];
        const bool local = isLocalMode(mode[s]);
        double lineWeight = 0.0;
        for (int i = 0; i < lines.size(); ++i)
            if (c.member[i] && lines[i].speaker == s) lineWeight += weightOf(lines[i]);
        info.lines = c.counts[s];
        if (local || mode[s] == kModePrior) {
            addPrints(c.sums[s], priorOf(priors, s), lineWeight, local, info);
            c.printReady[s] = info.localPrints + info.priorPrints > 0;
        }
        info.kind = local ? ReferenceKind::Local
                  : mode[s] == kModePrior ? ReferenceKind::Prior
                  : c.counts[s] > 0 ? ReferenceKind::Fallback : ReferenceKind::None;
        c.norms[s] = norm2(c.sums[s]);
        out.trustedCore[s] = local;
        if (local) out.coreLines[s] = c.counts[s];
    }

    for (int i = 0; i < lines.size(); ++i) {
        const AnalysisLine& l = lines[i];
        // A locked line is the user's decision; they could not decide a noisy one either, so we do not flag it.
        if (l.locked || l.noisy || !validLine(l, speakerCount) || l.durationMs < kMinEmbedMs)
            continue;
        const LineFit f = fitOf(c, lines, i, speakerCount);
        RecheckVerdict& v = out.lines[i];
        v.own = f.own;
        v.other = f.other;
        v.uncertain = judgeUncertain(l, f);
        // A suggestion only if the other speaker REALLY fits better (not just its own weak fit).
        const double margin = l.durationMs >= kReliableMs ? kUncertainMargin : kUncertainMarginShort;
        if (v.uncertain && !std::isnan(f.other) && f.other - f.own >= margin)
            v.otherSpeaker = f.otherSpeaker;
    }
    return out;
}

QVector<bool> computeOverlapNoisy(const QVector<TimedLine>& lines)
{
    QVector<bool> out(lines.size(), false);
    for (int i = 0; i < lines.size(); ++i) {
        const TimedLine& l = lines[i];
        // The embedding window: for a long line only its middle counts (see kMaxEmbedMs).
        qint64 ws = l.startMs, we = l.endMs;
        if (we - ws > kMaxEmbedMs) {
            ws = (l.startMs + l.endMs) / 2 - kMaxEmbedMs / 2;
            we = ws + kMaxEmbedMs;
        }
        const qint64 window = we - ws;
        if (window <= 0) continue;
        qint64 overlap = 0;
        for (int j = 0; j < lines.size(); ++j) {
            const TimedLine& o = lines[j];
            if (j == i || o.speaker == l.speaker) continue;
            if (o.startMs >= we) break;     // time order: the rest start after the window
            overlap += std::max<qint64>(0, std::min(we, o.endMs) - std::max(ws, o.startMs));
        }
        overlap = std::min(overlap, window);
        out[i] = overlap >= kNoisyOverlapMs || double(overlap) >= kNoisyOverlapRatio * double(window);
    }
    return out;
}

QVector<int> suggestSimilar(const QVector<AnalysisLine>& lines, int sourceSpeaker,
                            int targetSpeaker)
{
    return suggestSimilarDetailed(lines, sourceSpeaker, targetSpeaker).lines;
}

SuggestOutcome suggestSimilarDetailed(const QVector<AnalysisLine>& lines, int sourceSpeaker,
                                      int targetSpeaker)
{
    SuggestOutcome outcome;
    if (sourceSpeaker == targetSpeaker) return outcome;
    // The target's "core": its current lines (including those just moved by hand); fixed throughout.
    Vec seed;
    int anchors = 0;
    QVector<int> members;       // the source's embedded lines
    for (int i = 0; i < lines.size(); ++i) {
        const AnalysisLine& l = lines[i];
        if (!l.hasEmbedding()) continue;
        if (l.speaker == targetSpeaker) {
            addScaled(seed, *l.embedding, weightOf(l));
            ++anchors;
        } else if (l.speaker == sourceSpeaker) {
            members.append(i);
        }
    }
    if (anchors == 0 || members.size() < 2) return outcome;
    const double seedNorm = norm2(seed);

    // Core-seeded 2-means on the source's lines. The "stays" cluster starts from the lines LEAST similar to
    // the core (bottom ~30%) + the locked lines, so it works even if the highlighted voice is the majority of
    // the source (measured against the full, mixed source centroid nothing would split off then).
    QVector<int> bySim = members;
    QHash<int, double> simToSeed;
    for (int i : std::as_const(members))
        simToSeed.insert(i, cosTo(seed, seedNorm, *lines[i].embedding));
    std::sort(bySim.begin(), bySim.end(),
              [&](int a, int b) { return simToSeed.value(a) < simToSeed.value(b); });
    const int initCount = std::max<int>(1, int(bySim.size() * 3 / 10));
    Vec stay;
    for (int k = 0; k < bySim.size(); ++k) {
        const int i = bySim[k];
        if (k < initCount || lines[i].locked)
            addScaled(stay, *lines[i].embedding, weightOf(lines[i]));
    }
    double stayNorm = norm2(stay);

    QSet<int> moved;            // the source's lines that pull toward the target
    for (int i : std::as_const(members)) {
        if (lines[i].locked) continue;
        const double toStay = cosTo(stay, stayNorm, *lines[i].embedding);
        if (simToSeed.value(i) > (std::isnan(toStay) ? -1.0 : toStay)) moved.insert(i);
    }

    Vec target = seed;
    double targetNorm = seedNorm;
    for (int iter = 0; iter < 8; ++iter) {
        target = seed;
        stay.fill(0.0);
        if (stay.size() < seed.size()) stay.resize(seed.size());
        for (int i : std::as_const(members)) {
            const double w = weightOf(lines[i]);
            addScaled(moved.contains(i) ? target : stay, *lines[i].embedding, w);
        }
        targetNorm = norm2(target);
        stayNorm = norm2(stay);

        QSet<int> next;
        for (int i : std::as_const(members)) {
            const AnalysisLine& l = lines[i];
            if (l.locked) continue;     // a confirmed / manually corrected line stays at the source
            const double w = weightOf(l);
            const bool in = moved.contains(i);
            // On both sides we measure against the centroid WITHOUT the line (otherwise it would pull itself).
            const double toTarget = in ? cosToWithout(target, targetNorm, *l.embedding, w)
                                       : cosTo(target, targetNorm, *l.embedding);
            const double toStay = in ? cosTo(stay, stayNorm, *l.embedding)
                                     : cosToWithout(stay, stayNorm, *l.embedding, w);
            if (std::isnan(toTarget)) continue;
            if (std::isnan(toStay) || toTarget > toStay) next.insert(i);
        }
        if (next == moved) break;
        moved = next;
    }

    // If the two clusters' voices do not separate (two "moods" of the same person), the split is not a real
    // second speaker, so no suggestion.
    if (targetNorm > 1e-12 && stayNorm > 1e-12) {
        double d = 0.0;
        const int n = std::min<int>(target.size(), stay.size());
        for (int k = 0; k < n; ++k) d += target[k] * stay[k];
        outcome.centroidSimilarity = d / std::sqrt(targetNorm * stayNorm);
        if (outcome.centroidSimilarity > kSuggestMaxCentroidSimilarity) {
            outcome.blockedBySimilarity = true;
            return outcome;
        }
    }

    // Only lines that DECISIVELY pull toward the target and are long enough go into the suggestion.
    QVector<int> out;
    for (int i : std::as_const(members)) {
        if (!moved.contains(i)) continue;
        const AnalysisLine& l = lines[i];
        if (l.durationMs < kMinEmbedMs) continue;
        const double w = weightOf(l);
        const double toTarget = cosToWithout(target, targetNorm, *l.embedding, w);
        const double toStay = cosTo(stay, stayNorm, *l.embedding);
        if (std::isnan(toTarget) || toTarget < kSuggestMinSimilarity) continue;
        const double margin = l.durationMs >= kReliableMs ? kSuggestMargin : kSuggestMarginShort;
        if (std::isnan(toStay) || toTarget - toStay >= margin) out.append(i);
    }
    std::sort(out.begin(), out.end());
    outcome.lines = out;
    return outcome;
}

PairRecheckAnalysis computePairRecheck(const QVector<AnalysisLine>& lines, int speakerA,
                                       int speakerB, const QVector<SpeakerPrior>& priors)
{
    PairRecheckAnalysis out;
    out.lines.resize(lines.size());
    if (speakerA < 0 || speakerB < 0 || speakerA == speakerB) return out;

    auto usable = [](const AnalysisLine& l) { return l.hasEmbedding() && !l.noisy; };
    const QVector<bool> replaced = replacedMask(lines, priors);
    // Per reference (see SpeakerPrior): local core = the locked clean lines (if there are enough) and/or the
    // local voiceprints, plus the earlier voiceprints with capped weight; without a local core the earlier
    // voiceprints; if there are none either, all clean lines (fallback).
    struct Ref { Vec sum; double norm = 0.0; int count = 0; bool hasPrints = false;
                 ReferenceInfo info; QVector<bool> member; };
    auto buildRef = [&](int speaker) {
        Ref r;
        r.member.fill(false, lines.size());
        const SpeakerPrior* p = priorOf(priors, speaker);
        int locked = 0;
        for (int i = 0; i < lines.size(); ++i) {
            const AnalysisLine& l = lines[i];
            if (l.speaker == speaker && usable(l) && l.locked && !replaced[i]) ++locked;
        }
        const bool local = locked >= kMinSpeakerLines || (p && !p->localVectors.isEmpty());
        const bool priorOnly = !local && p && !p->vectors.isEmpty();
        r.info.kind = local ? ReferenceKind::Local : priorOnly ? ReferenceKind::Prior
                                                              : ReferenceKind::Fallback;
        double lineWeight = 0.0;
        if (!priorOnly) {
            for (int i = 0; i < lines.size(); ++i) {
                const AnalysisLine& l = lines[i];
                if (l.speaker != speaker || !usable(l)) continue;
                if (local && (!l.locked || replaced[i])) continue;
                addScaled(r.sum, *l.embedding, weightOf(l));
                lineWeight += weightOf(l);
                r.member[i] = true;
                ++r.count;
            }
        }
        if (local || priorOnly) addPrints(r.sum, p, lineWeight, local, r.info);
        r.info.lines = r.count;
        r.hasPrints = r.info.localPrints + r.info.priorPrints > 0;
        r.norm = norm2(r.sum);
        return r;
    };
    const Ref a = buildRef(speakerA);
    const Ref b = buildRef(speakerB);
    out.refLinesA = a.count;
    out.refLinesB = b.count;
    out.fallbackA = a.info.fallback();
    out.fallbackB = b.info.fallback();
    out.refA = a.info;
    out.refB = b.info;
    auto ready = [](const Ref& r) { return r.hasPrints || r.count >= kMinSpeakerLines; };
    out.valid = ready(a) && ready(b);
    if (a.norm > 1e-12 && b.norm > 1e-12) {
        double d = 0.0;
        const int n = std::min<int>(a.sum.size(), b.sum.size());
        for (int k = 0; k < n; ++k) d += a.sum[k] * b.sum[k];
        out.centroidSimilarity = d / std::sqrt(a.norm * b.norm);
    }
    if (!out.valid) return out;

    // A line's fit to a reference: if it is in it, without itself (otherwise it would pull itself).
    auto fit = [&](const Ref& r, int i) {
        const AnalysisLine& l = lines[i];
        if (!r.member[i]) return cosTo(r.sum, r.norm, *l.embedding);
        if (!r.hasPrints && r.count - 1 < kMinSpeakerLines) return qQNaN();
        return cosToWithout(r.sum, r.norm, *l.embedding, weightOf(l));
    };
    for (int i = 0; i < lines.size(); ++i) {
        const AnalysisLine& l = lines[i];
        if (l.speaker != speakerA && l.speaker != speakerB) continue;
        // A locked line is the user's decision; they could not decide a noisy one; a short one is unreliable.
        if (l.locked || l.noisy || !l.hasEmbedding() || l.durationMs < kMinEmbedMs) continue;
        PairVerdict& v = out.lines[i];
        v.toA = fit(a, i);
        v.toB = fit(b, i);
        if (std::isnan(v.toA) || std::isnan(v.toB)) continue;
        const bool onA = l.speaker == speakerA;
        const double own = onA ? v.toA : v.toB;
        const double other = onA ? v.toB : v.toA;
        const double margin = l.durationMs >= kReliableMs ? kPairMargin : kPairMarginShort;
        if (other - own >= margin) {
            v.flagged = true;
            v.hintedSpeaker = onA ? speakerB : speakerA;
        }
    }
    return out;
}

} // namespace speakeredit
} // namespace tanara
