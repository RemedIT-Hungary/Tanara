#include "tanara/edit/SpeakerAnalysis.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>

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

QVector<LineFit> computeFits(const QVector<AnalysisLine>& lines, int speakerCount)
{
    QVector<LineFit> fits(lines.size());
    if (speakerCount <= 0) return fits;

    QVector<Vec> sums(speakerCount);
    QVector<int> counts(speakerCount, 0);
    for (const AnalysisLine& l : lines) {
        if (!l.hasEmbedding() || l.speaker < 0 || l.speaker >= speakerCount) continue;
        addScaled(sums[l.speaker], *l.embedding, weightOf(l));
        ++counts[l.speaker];
    }
    QVector<double> norms(speakerCount);
    for (int s = 0; s < speakerCount; ++s) norms[s] = norm2(sums[s]);

    for (int i = 0; i < lines.size(); ++i) {
        const AnalysisLine& l = lines[i];
        if (!l.hasEmbedding() || l.speaker < 0 || l.speaker >= speakerCount) continue;
        LineFit& f = fits[i];
        // Saját centroid a sor nélkül — csak ha a sor nélkül is marad elég minta.
        if (counts[l.speaker] - 1 >= kMinSpeakerLines)
            f.own = cosToWithout(sums[l.speaker], norms[l.speaker], *l.embedding, weightOf(l));
        for (int s = 0; s < speakerCount; ++s) {
            if (s == l.speaker || counts[s] < kMinSpeakerLines) continue;
            const double c = cosTo(sums[s], norms[s], *l.embedding);
            if (!std::isnan(c) && (std::isnan(f.other) || c > f.other)) {
                f.other = c;
                f.otherSpeaker = s;
            }
        }
    }
    return fits;
}

QVector<bool> computeUncertain(const QVector<AnalysisLine>& lines, int speakerCount)
{
    QVector<bool> out(lines.size(), false);
    const QVector<LineFit> fits = computeFits(lines, speakerCount);
    for (int i = 0; i < lines.size(); ++i) {
        const AnalysisLine& l = lines[i];
        if (l.locked || !l.hasEmbedding() || l.durationMs < kMinEmbedMs) continue;
        const LineFit& f = fits[i];
        if (std::isnan(f.own)) continue;    // a saját beszélőről nincs elég minta → nem ítélünk
        const bool reliable = l.durationMs >= kReliableMs;
        const double margin = reliable ? kUncertainMargin : kUncertainMarginShort;
        if (!std::isnan(f.other) && f.other - f.own >= margin)
            out[i] = true;                  // egy másik beszélőre jobban hasonlít
        else if (reliable && f.own < kUncertainMinFit)
            out[i] = true;                  // a sajátjára sem hasonlít (pl. új, el nem különített hang)
    }
    return out;
}

QVector<int> suggestSimilar(const QVector<AnalysisLine>& lines, int sourceSpeaker,
                            int targetSpeaker)
{
    if (sourceSpeaker == targetSpeaker) return {};
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
    if (anchors == 0 || members.size() < 2) return {};
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
        if (d / std::sqrt(targetNorm * stayNorm) > kSuggestMaxCentroidSimilarity) return {};
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
    return out;
}

} // namespace speakeredit
} // namespace tanara
