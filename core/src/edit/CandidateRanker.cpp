#include "tanara/edit/CandidateRanker.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace tanara {
namespace ranking {

namespace {

QString tr(const char* s, int n = -1) { return QCoreApplication::translate("CandidateRanker", s, nullptr, n); }

bool decided(Side s) { return s == Side::Local || s == Side::Remote; }

bool opposite(Side a, Side b)
{
    return (a == Side::Local && b == Side::Remote) || (a == Side::Remote && b == Side::Local);
}

QString percent(double v) { return QStringLiteral("%1%").arg(qRound(std::clamp(v, 0.0, 1.0) * 100.0)); }

// A leave-one-out centroid (a próba hangja nélkül), ha marad elég sor.
QVector<float> centroidWithout(const SpeakerProfile& p, const QVector<float>& probe, bool exclude)
{
    if (p.sum.isEmpty()) return {};
    const int n = p.sumCount - (exclude ? 1 : 0);
    if (n < 1) return {};
    if (!exclude) return normalized(p.sum);
    QVector<double> s = p.sum;
    for (int i = 0; i < s.size() && i < probe.size(); ++i) s[i] -= double(probe[i]);
    return normalized(s);
}

Evidence voiceEvidence(double v, const QString& basis)
{
    Evidence e;
    e.kind = EvidenceKind::Voice;
    e.fixTarget = QStringLiteral("samples");
    if (std::isnan(v)) {
        e.polarity = Polarity::Neutral;
        e.value = 0.0;
        e.text = tr("nincs hangadat");
        return e;
    }
    e.value = v;
    e.polarity = v >= kVoiceGood ? Polarity::Support : v < kVoiceBad ? Polarity::Contradict : Polarity::Neutral;
    e.text = tr("hang %1").arg(percent(v));
    e.detail = basis == QLatin1String("core")   ? tr("a megerősített soraihoz mérve")
             : basis == QLatin1String("prints") ? tr("a hanglenyomatához mérve")
                                                : tr("a többi sorához mérve");
    return e;
}

// Sáv-bizonyíték a jelölt oldala és a próba oldala között. Nincs, ha valamelyik nem dönthető.
bool sideEvidence(const SpeakerProfile& p, Side probeSide, Evidence* out)
{
    if (!decided(p.side) || !decided(probeSide)) return false;
    const bool manual = p.sideBasis == QLatin1String("manual");
    Evidence e;
    e.kind = manual ? EvidenceKind::Manual : EvidenceKind::Side;
    e.fixTarget = QStringLiteral("tracks");
    if (opposite(p.side, probeSide)) {
        e.polarity = Polarity::Contradict;
        e.value = -1.0;
        e.text = tr("másik sávon beszél");
        e.detail = tr("%1: %2; ez: %3").arg(p.displayName, sideLabel(p.side), sideLabel(probeSide));
    } else {
        e.polarity = Polarity::Support;
        e.value = 1.0;
        e.text = p.side == Side::Remote ? tr("a hívás hangján") : tr("a mikrofonon");
        e.detail = manual ? tr("kézi sáv-beosztás") : QString();
    }
    *out = e;
    return true;
}

bool tagEvidence(const SpeakerProfile& p, const QStringList& meetingTags, Evidence* out, int* shared)
{
    *shared = 0;
    if (meetingTags.isEmpty() || p.tags.isEmpty()) return false;
    QStringList common;
    for (const QString& t : p.tags)
        if (meetingTags.contains(t, Qt::CaseInsensitive) && !common.contains(t, Qt::CaseInsensitive)) common << t;
    *shared = int(common.size());
    Evidence e;
    e.kind = EvidenceKind::Tag;
    e.fixTarget = QStringLiteral("tags");
    e.value = common.size();
    if (common.isEmpty()) {
        e.polarity = Polarity::Contradict;
        e.text = tr("nincs közös címke");
    } else {
        e.polarity = Polarity::Support;
        e.text = tr("%n közös címke", int(common.size()));
        e.detail = common.join(QStringLiteral(", "));
    }
    *out = e;
    return true;
}

Candidate score(const RankContext& ctx, const SpeakerProfile& p, const Probe& probe)
{
    Candidate c;
    c.speakerKey = p.key;
    c.personName = p.personName;
    c.linesHere = p.lines;

    QString basis;
    const double v = voiceScore(p, probe, &basis);
    if (!probe.embedding.isEmpty()) c.evidence.append(voiceEvidence(v, basis));
    double s = std::isnan(v) ? 0.0 : kVoiceWeight * std::clamp(v, 0.0, 1.0);

    Evidence e;
    if (ctx.sidesActive && sideEvidence(p, probe.side, &e)) {
        if (e.polarity == Polarity::Contradict) {
            s -= kSidePenalty;
            c.otherSide = true;
        } else {
            s += kSideSupport;
        }
        c.evidence.append(e);
    }
    int shared = 0;
    if (tagEvidence(p, ctx.meetingTags, &e, &shared)) {
        s += shared > 0 ? kTagSupport * std::min(shared, kTagMaxShared) : -kTagMismatch;
        c.evidence.append(e);
    }
    if (p.lines > 0) {
        s += kLineWeight * std::min(1.0, double(p.lines) / kLineSaturation);
        Evidence l;
        l.kind = EvidenceKind::LineCount;
        l.polarity = Polarity::Support;
        l.value = p.lines;
        l.text = tr("%n sor itt", p.lines);
        c.evidence.append(l);
    }
    if (!p.key.isEmpty()) c.evidence += similarityWarnings(ctx, p.key);
    c.score = s;
    return c;
}

void sortCandidates(QVector<Candidate>& v)
{
    std::stable_sort(v.begin(), v.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
}

} // namespace

const SpeakerProfile* RankContext::profile(const QString& key) const
{
    const auto it = index.constFind(key);
    return it == index.constEnd() ? nullptr : &speakers.at(it.value());
}

double cosine(const QVector<float>& a, const QVector<float>& b)
{
    if (a.isEmpty() || a.size() != b.size()) return qQNaN();
    double d = 0.0;
    for (int i = 0; i < a.size(); ++i) d += double(a[i]) * double(b[i]);
    return d;
}

QVector<float> normalized(const QVector<double>& sum)
{
    double n = 0.0;
    for (double x : sum) n += x * x;
    if (n <= 0.0) return {};
    n = std::sqrt(n);
    QVector<float> out(sum.size());
    for (int i = 0; i < sum.size(); ++i) out[i] = float(sum[i] / n);
    return out;
}

double voiceScore(const SpeakerProfile& p, const Probe& probe, QString* basis)
{
    if (probe.embedding.isEmpty()) return qQNaN();
    double best = qQNaN();
    QString kind;
    auto consider = [&](double v, const char* k) {
        if (std::isnan(v)) return;
        if (std::isnan(best) || v > best) { best = v; kind = QString::fromLatin1(k); }
    };
    if (!p.core.isEmpty()) {
        consider(cosine(p.core, probe.embedding), "core");
    } else {
        const bool self = probe.inCurrentSum && p.key == probe.currentKey && !p.key.isEmpty();
        const int n = p.sumCount - (self ? 1 : 0);
        if (n >= kMinReferenceLines) consider(cosine(centroidWithout(p, probe.embedding, self), probe.embedding), "lines");
    }
    for (const QVector<float>& pr : p.prints) consider(cosine(pr, probe.embedding), "prints");
    if (basis) *basis = kind;
    return best;
}

QVector<Candidate> rank(const RankContext& ctx, const Probe& probe)
{
    QVector<Candidate> out;
    out.reserve(ctx.speakers.size());
    for (const SpeakerProfile& p : ctx.speakers) out.append(score(ctx, p, probe));
    sortCandidates(out);
    return out;
}

QVector<Candidate> rankForLine(const RankContext& ctx, const QVector<float>& embedding, Side lineSide,
                               const QString& currentKey, bool inCurrentSum)
{
    Probe p;
    p.embedding = embedding;
    p.side = lineSide;
    p.currentKey = currentKey;
    p.inCurrentSum = inCurrentSum;
    return rank(ctx, p);
}

QVector<Candidate> rankForSpeaker(const RankContext& ctx, const QString& speakerKey)
{
    const SpeakerProfile* self = ctx.profile(speakerKey);
    if (!self) return {};
    Probe p;
    p.embedding = self->core.isEmpty() ? normalized(self->sum) : self->core;
    p.side = self->lineSide;
    p.currentKey = speakerKey;
    QVector<Candidate> out;
    for (const SpeakerProfile& sp : ctx.speakers) {
        if (sp.key == speakerKey) continue;
        // Ugyanaz a személy más kulccsal (pl. két nyers címke) nem „valójában más".
        if (!sp.personName.isEmpty() && sp.personName.compare(self->personName, Qt::CaseInsensitive) == 0) continue;
        out.append(score(ctx, sp, p));
    }
    sortCandidates(out);
    return out;
}

QVector<Evidence> selfEvidence(const RankContext& ctx, const QString& speakerKey)
{
    QVector<Evidence> out;
    const SpeakerProfile* p = ctx.profile(speakerKey);
    if (!p) return out;
    // Hang: a beszélő itteni hangja a személy lenyomataihoz.
    const QVector<float> own = p->core.isEmpty() ? normalized(p->sum) : p->core;
    if (!p->personName.isEmpty()) {
        double best = qQNaN();
        for (const QVector<float>& pr : p->prints) {
            const double v = cosine(pr, own);
            if (!std::isnan(v) && (std::isnan(best) || v > best)) best = v;
        }
        if (p->prints.isEmpty() || own.isEmpty()) {
            Evidence e;
            e.kind = EvidenceKind::Voice;
            e.polarity = Polarity::Neutral;
            e.text = p->prints.isEmpty() ? tr("nincs hanglenyomata") : tr("nincs hangadat");
            e.fixTarget = QStringLiteral("samples");
            out.append(e);
        } else {
            out.append(voiceEvidence(best, QStringLiteral("prints")));
        }
    }
    Evidence e;
    if (ctx.sidesActive && sideEvidence(*p, p->lineSide, &e)) {
        if (e.polarity == Polarity::Contradict) e.text = tr("a sorai a másik sávon szólnak");
        out.append(e);
    }
    int shared = 0;
    if (tagEvidence(*p, ctx.meetingTags, &e, &shared)) out.append(e);
    if (p->lines > 0) {
        Evidence l;
        l.kind = EvidenceKind::LineCount;
        l.polarity = Polarity::Support;
        l.value = p->lines;
        l.text = tr("%n sor itt", p->lines);
        out.append(l);
    }
    out += similarityWarnings(ctx, speakerKey);
    return out;
}

QVector<Evidence> whyNot(const Candidate& c)
{
    QVector<Evidence> out;
    for (const Evidence& e : c.evidence)
        if (e.polarity == Polarity::Contradict) out.append(e);
    return out;
}

QVector<Evidence> similarityWarnings(const RankContext& ctx, const QString& speakerKey)
{
    QVector<Evidence> out;
    const SpeakerProfile* p = ctx.profile(speakerKey);
    if (!p || p->core.isEmpty()) return out;
    for (const SpeakerProfile& o : ctx.speakers) {
        if (o.key.isEmpty() || o.key == speakerKey || o.core.isEmpty()) continue;
        const double v = cosine(p->core, o.core);
        if (std::isnan(v) || v < kSimilarWarn) continue;
        Evidence e;
        e.kind = EvidenceKind::Similarity;
        e.polarity = Polarity::Neutral;
        e.value = v;
        e.text = tr("hasonló hang: %1").arg(o.displayName);
        e.detail = tr("a két megerősített hang %1-ban egyezik").arg(percent(v));
        e.fixTarget = QStringLiteral("pair:") + o.key;
        out.append(e);
    }
    return out;
}

QString sideLabel(Side s)
{
    switch (s) {
    case Side::Local:   return tr("mikrofon");
    case Side::Remote:  return tr("hívás hangja");
    case Side::Mixed:   return tr("vegyes");
    case Side::Unknown: break;
    }
    return tr("ismeretlen");
}

QString evidenceKindName(EvidenceKind k)
{
    switch (k) {
    case EvidenceKind::Voice:      return QStringLiteral("voice");
    case EvidenceKind::Side:       return QStringLiteral("side");
    case EvidenceKind::Tag:        return QStringLiteral("tag");
    case EvidenceKind::LineCount:  return QStringLiteral("lineCount");
    case EvidenceKind::Similarity: return QStringLiteral("similarity");
    case EvidenceKind::Calendar:   return QStringLiteral("calendar");
    case EvidenceKind::Manual:     return QStringLiteral("manual");
    }
    return QStringLiteral("voice");
}

QString polarityName(Polarity p)
{
    switch (p) {
    case Polarity::Support:    return QStringLiteral("support");
    case Polarity::Contradict: return QStringLiteral("contradict");
    case Polarity::Neutral:    break;
    }
    return QStringLiteral("neutral");
}

} // namespace ranking
} // namespace tanara
