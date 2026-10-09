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

// A sor súlya a centroidban: a hossza (mp), a felhasznált ablakra vágva.
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

// cos(e, sum), ahol e egységvektor.
double cosTo(const Vec& sum, double sumNorm2, const QVector<float>& e)
{
    if (sumNorm2 <= 1e-12) return qQNaN();
    return dot(sum, e) / std::sqrt(sumNorm2);
}

// cos(e, sum − w·e): a centroid a sor NÉLKÜL (leave-one-out), e egységvektor.
double cosToWithout(const Vec& sum, double sumNorm2, const QVector<float>& e, double w)
{
    const double d = dot(sum, e);
    const double n2 = sumNorm2 - 2.0 * w * d + w * w;
    if (n2 <= 1e-9) return qQNaN();
    return (d - w) / std::sqrt(n2);
}

} // namespace

namespace {

// A centroid-építés közös része: beszélőnként a súlyozott összeg és a benne lévő sorok száma.
struct Centroids {
    QVector<Vec> sums;
    QVector<double> norms;
    QVector<int> counts;
    QVector<bool> member;           // soronként: benne van-e a saját beszélője centroidjában
};

bool validLine(const AnalysisLine& l, int speakerCount)
{
    return l.hasEmbedding() && l.speaker >= 0 && l.speaker < speakerCount;
}

// A rendes szabály: minden sor, de a zajosak csak akkor, ha nincs elég tiszta sor.
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

// A sor illeszkedése a centroidokhoz (a sajátjához leave-one-out, ha benne van).
LineFit fitOf(const Centroids& c, const QVector<AnalysisLine>& lines, int i, int speakerCount)
{
    LineFit f;
    const AnalysisLine& l = lines[i];
    if (c.member[i]) {
        if (c.counts[l.speaker] - 1 >= kMinSpeakerLines)
            f.own = cosToWithout(c.sums[l.speaker], c.norms[l.speaker], *l.embedding, weightOf(l));
    } else if (c.counts[l.speaker] >= kMinSpeakerLines) {
        f.own = cosTo(c.sums[l.speaker], c.norms[l.speaker], *l.embedding);
    }
    for (int s = 0; s < speakerCount; ++s) {
        if (s == l.speaker || c.counts[s] < kMinSpeakerLines) continue;
        const double v = cosTo(c.sums[s], c.norms[s], *l.embedding);
        if (!std::isnan(v) && (std::isnan(f.other) || v > f.other)) {
            f.other = v;
            f.otherSpeaker = s;
        }
    }
    return f;
}

// A küszöbök: egy másik beszélő jobban illik, vagy (megbízható hosszú sornál) a sajátjához sem.
bool judgeUncertain(const AnalysisLine& l, const LineFit& f)
{
    if (std::isnan(f.own)) return false;    // a saját beszélőről nincs elég minta → nem ítélünk
    const bool reliable = l.durationMs >= kReliableMs;
    const double margin = reliable ? kUncertainMargin : kUncertainMarginShort;
    if (!std::isnan(f.other) && f.other - f.own >= margin)
        return true;                        // egy másik beszélőre jobban hasonlít
    return reliable && f.own < kUncertainMinFit;    // a sajátjára sem hasonlít (új, el nem különített hang)
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

// Beszélőnként a zárolt mag: 2 = csak a tiszta zárolt sorok, 1 = minden zárolt sor
// (a zajosakkal együtt, mert tisztából nincs elég), 0 = nincs mag.
QVector<int> coreModes(const QVector<AnalysisLine>& lines, int speakerCount)
{
    QVector<int> lockedClean(speakerCount, 0), lockedAll(speakerCount, 0);
    for (const AnalysisLine& l : lines) {
        if (!validLine(l, speakerCount) || !l.locked) continue;
        ++lockedAll[l.speaker];
        if (!l.noisy) ++lockedClean[l.speaker];
    }
    QVector<int> mode(speakerCount, 0);
    for (int s = 0; s < speakerCount; ++s)
        mode[s] = lockedClean[s] >= kMinSpeakerLines ? 2 : lockedAll[s] >= kMinSpeakerLines ? 1 : 0;
    return mode;
}

} // namespace

bool hasTrustedCore(const QVector<AnalysisLine>& lines, int speakerCount)
{
    if (speakerCount <= 0) return false;
    const QVector<int> mode = coreModes(lines, speakerCount);
    return std::any_of(mode.cbegin(), mode.cend(), [](int m) { return m > 0; });
}

RecheckAnalysis computeUncertainRechecked(const QVector<AnalysisLine>& lines, int speakerCount)
{
    RecheckAnalysis out;
    out.lines.resize(lines.size());
    if (speakerCount <= 0) return out;
    const QVector<int> mode = coreModes(lines, speakerCount);
    const QVector<bool> clean = cleanEnough(lines, speakerCount);
    const Centroids c = buildCentroids(lines, speakerCount, [&](const AnalysisLine& l) {
        switch (mode[l.speaker]) {
        case 2:  return l.locked && !l.noisy;
        case 1:  return l.locked;
        default: return !l.noisy || !clean[l.speaker];     // a rendes szabály
        }
    });
    out.trustedCore.resize(speakerCount);
    out.coreLines.fill(0, speakerCount);
    for (int s = 0; s < speakerCount; ++s) {
        out.trustedCore[s] = mode[s] > 0;
        if (mode[s] > 0) out.coreLines[s] = c.counts[s];
    }

    for (int i = 0; i < lines.size(); ++i) {
        const AnalysisLine& l = lines[i];
        // A zárolt sor a felhasználó döntése; a zajosat ő sem tudná eldönteni → nem jelöljük.
        if (l.locked || l.noisy || !validLine(l, speakerCount) || l.durationMs < kMinEmbedMs)
            continue;
        const LineFit f = fitOf(c, lines, i, speakerCount);
        RecheckVerdict& v = out.lines[i];
        v.own = f.own;
        v.other = f.other;
        v.uncertain = judgeUncertain(l, f);
        // Javaslat csak akkor, ha a másik beszélő TÉNYLEG jobban illik (nem csak a saját gyenge).
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
        // Az embedding-ablak: hosszú sornál csak a közepe számít (lásd kMaxEmbedMs).
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
            if (o.startMs >= we) break;     // időrend: a továbbiak már az ablak után kezdődnek
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
    // A cél „magja": a jelenlegi sorai (köztük a most kézzel átrakottak) — ez végig rögzített.
    Vec seed;
    int anchors = 0;
    QVector<int> members;       // a forrás embeddelt sorai
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

    // Magozott 2-közép a forrás sorain. A „marad" klaszter kezdete a maghoz LEGKEVÉSBÉ
    // hasonló sorok (alsó ~30%) + a zárolt sorok — így akkor is működik, ha a kiemelt hang
    // a forrás többsége (a teljes, kevert forrás-centroidhoz mérve ilyenkor semmi sem válna le).
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

    QSet<int> moved;            // a forrás sorai, amelyek a célhoz húznak
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
            if (l.locked) continue;     // a megerősített / kézzel javított sor a forrásnál marad
            const double w = weightOf(l);
            const bool in = moved.contains(i);
            // Mindkét oldalon a sor NÉLKÜLI centroidhoz mérünk (különben önmagát húzná).
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

    // Ha a két klaszter hangja nem különül el (ugyanannak az embernek két „hangulata"), a
    // szétválás nem valódi második beszélő → nincs javaslat.
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

    // A javaslatba csak a HATÁROZOTTAN a célhoz húzó, elég hosszú sorok kerülnek.
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
                                       int speakerB)
{
    PairRecheckAnalysis out;
    out.lines.resize(lines.size());
    if (speakerA < 0 || speakerB < 0 || speakerA == speakerB) return out;

    auto usable = [](const AnalysisLine& l) { return l.hasEmbedding() && !l.noisy; };
    // Referenciánként: a zárolt tiszta sorok, ha elég van; különben az összes tiszta sor.
    struct Ref { Vec sum; double norm = 0.0; int count = 0; bool fallback = false; QVector<bool> member; };
    auto buildRef = [&](int speaker) {
        Ref r;
        r.member.fill(false, lines.size());
        int locked = 0;
        for (const AnalysisLine& l : lines)
            if (l.speaker == speaker && usable(l) && l.locked) ++locked;
        r.fallback = locked < kMinSpeakerLines;
        for (int i = 0; i < lines.size(); ++i) {
            const AnalysisLine& l = lines[i];
            if (l.speaker != speaker || !usable(l) || (!r.fallback && !l.locked)) continue;
            addScaled(r.sum, *l.embedding, weightOf(l));
            r.member[i] = true;
            ++r.count;
        }
        r.norm = norm2(r.sum);
        return r;
    };
    const Ref a = buildRef(speakerA);
    const Ref b = buildRef(speakerB);
    out.refLinesA = a.count;
    out.refLinesB = b.count;
    out.fallbackA = a.fallback;
    out.fallbackB = b.fallback;
    out.valid = a.count >= kMinSpeakerLines && b.count >= kMinSpeakerLines;
    if (a.norm > 1e-12 && b.norm > 1e-12) {
        double d = 0.0;
        const int n = std::min<int>(a.sum.size(), b.sum.size());
        for (int k = 0; k < n; ++k) d += a.sum[k] * b.sum[k];
        out.centroidSimilarity = d / std::sqrt(a.norm * b.norm);
    }
    if (!out.valid) return out;

    // A sor illeszkedése egy referenciához: ha benne van, önmaga nélkül (különben önmagát húzná).
    auto fit = [&](const Ref& r, int i) {
        const AnalysisLine& l = lines[i];
        if (!r.member[i]) return cosTo(r.sum, r.norm, *l.embedding);
        if (r.count - 1 < kMinSpeakerLines) return qQNaN();
        return cosToWithout(r.sum, r.norm, *l.embedding, weightOf(l));
    };
    for (int i = 0; i < lines.size(); ++i) {
        const AnalysisLine& l = lines[i];
        if (l.speaker != speakerA && l.speaker != speakerB) continue;
        // A zárolt sor a felhasználó döntése; a zajosat ő sem tudná eldönteni; a rövid megbízhatatlan.
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
