#include "tanara/edit/ReviewGroups.h"

#include "tanara/edit/SpeakerAnalysis.h"

#include <QCoreApplication>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace tanara {

using ranking::RankContext;
using ranking::SpeakerProfile;

namespace {

QString tr(const char* s, int n = -1) { return QCoreApplication::translate("ReviewGroups", s, nullptr, n); }

bool decided(Side s) { return s == Side::Local || s == Side::Remote; }

QString percent(double v) { return QStringLiteral("%1%").arg(qRound(std::clamp(v, 0.0, 1.0) * 100.0)); }

// Soronkénti nézet az elemzéshez.
struct Row {
    int index = 0;
    QString id;
    QString key;
    qint64 durationMs = 0;
    bool locked = false;
    bool noisy = false;
    Side side = Side::Unknown;
    const QVector<float>* emb = nullptr;
    bool clean() const { return emb && !noisy; }
};

QVector<Row> rowsOf(const ReviewInput& in, const SideReport& sides)
{
    QVector<Row> rows;
    rows.reserve(sides.lines.size());
    for (int i = 0; i < sides.lines.size(); ++i) {
        const LineSide& ls = sides.lines[i];
        Row r;
        r.index = i;
        r.id = ls.utteranceId;
        r.key = ls.speakerKey;
        r.durationMs = ls.endMs - ls.startMs;
        r.locked = ls.locked;
        r.noisy = ls.noisy;
        r.side = ls.side;
        const auto it = in.embeddings.constFind(ls.utteranceId);
        if (it != in.embeddings.constEnd() && !it->isEmpty()) r.emb = &it.value();
        rows.append(r);
    }
    return rows;
}

void addTo(QVector<double>& sum, const QVector<float>& v)
{
    if (sum.isEmpty()) sum = QVector<double>(v.size(), 0.0);
    if (sum.size() != v.size()) return;
    for (int i = 0; i < v.size(); ++i) sum[i] += double(v[i]);
}

// A sorok többsége szerinti oldal (Local / Remote), ha egyértelmű.
Side majoritySide(int local, int remote)
{
    const int lr = local + remote;
    if (lr < sides::kPersonMinLines) return Side::Unknown;
    const float maj = float(std::max(local, remote)) / float(lr);
    if (maj < sides::kPersonMajority) return Side::Unknown;
    return local >= remote ? Side::Local : Side::Remote;
}

const SpeakerProfile* findProfile(const RankContext& ctx, const Candidate& c)
{
    if (!c.speakerKey.isEmpty()) return ctx.profile(c.speakerKey);
    for (const SpeakerProfile& p : ctx.speakers)
        if (p.key.isEmpty() && p.personName.compare(c.personName, Qt::CaseInsensitive) == 0) return &p;
    return nullptr;
}

QString nameOf(const RankContext& ctx, const QString& key, const QString& person)
{
    if (!key.isEmpty())
        if (const SpeakerProfile* p = ctx.profile(key)) return p->displayName;
    return person.isEmpty() ? tr("új résztvevő") : person;
}

bool samePerson(const RankContext& ctx, const QString& keyA, const Candidate& c)
{
    if (c.speakerKey == keyA) return true;
    const SpeakerProfile* a = ctx.profile(keyA);
    return a && !a->personName.isEmpty() && a->personName.compare(c.personName, Qt::CaseInsensitive) == 0;
}

QString groupId(ReviewKind k, const QString& current, const QString& proposedKey, const QString& proposedPerson)
{
    const QString target = !proposedKey.isEmpty() ? proposedKey
                         : !proposedPerson.isEmpty() ? QStringLiteral("person:") + proposedPerson
                                                     : QString();
    return reviewKindName(k) + QLatin1Char(':') + current + QStringLiteral("->") + target;
}

Evidence makeEvidence(EvidenceKind k, Polarity p, double v, const QString& text, const QString& detail,
                      const QString& fix)
{
    Evidence e;
    e.kind = k;
    e.polarity = p;
    e.value = v;
    e.text = text;
    e.detail = detail;
    e.fixTarget = fix;
    return e;
}

Evidence voiceSupport(double avg, const QString& who)
{
    return makeEvidence(EvidenceKind::Voice, avg >= ranking::kVoiceGood ? Polarity::Support : Polarity::Neutral,
                        avg, tr("hang %1").arg(percent(avg)), tr("átlag %1 hangjához").arg(who),
                        QStringLiteral("samples"));
}

} // namespace

QString reviewKindName(ReviewKind k)
{
    switch (k) {
    case ReviewKind::SideConflict:       return QStringLiteral("sideConflict");
    case ReviewKind::CoreMismatch:       return QStringLiteral("coreMismatch");
    case ReviewKind::ShortLines:         return QStringLiteral("shortLines");
    case ReviewKind::ContaminatedCore:   return QStringLiteral("contaminatedCore");
    case ReviewKind::SimilarToNewPerson: return QStringLiteral("similarToNewPerson");
    }
    return QStringLiteral("shortLines");
}

// ---- 2-közép ------------------------------------------------------------------

namespace review {

QVector<int> twoMeans(const QVector<QVector<float>>& v, int iterations)
{
    const int n = int(v.size());
    if (n < 2) return {};
    // Kezdés: a legkevésbé hasonló pár.
    int a = 0, b = 1;
    double worst = 2.0;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            const double c = ranking::cosine(v[i], v[j]);
            if (!std::isnan(c) && c < worst) { worst = c; a = i; b = j; }
        }
    QVector<float> ca = v[a], cb = v[b];
    QVector<int> label(n, -1);
    for (int it = 0; it < std::max(1, iterations); ++it) {
        bool changed = false;
        QVector<double> sa, sb;
        for (int i = 0; i < n; ++i) {
            const int l = ranking::cosine(v[i], ca) >= ranking::cosine(v[i], cb) ? 0 : 1;
            if (l != label[i]) { label[i] = l; changed = true; }
            addTo(l == 0 ? sa : sb, v[i]);
        }
        if (sa.isEmpty() || sb.isEmpty()) break;
        ca = ranking::normalized(sa);
        cb = ranking::normalized(sb);
        if (!changed) break;
    }
    return label;
}

CoreSplit splitConfirmedCore(const ReviewInput& in, const SideReport& sides, const QString& speakerKey)
{
    CoreSplit out;
    QVector<QVector<float>> vecs;
    QStringList ids;
    for (const Row& r : rowsOf(in, sides)) {
        if (r.key != speakerKey || !r.locked || !r.clean()) continue;
        vecs.append(*r.emb);
        ids << r.id;
    }
    if (vecs.size() < kContamMinLines) return out;
    const QVector<int> label = twoMeans(vecs);
    if (label.isEmpty()) return out;
    QVector<double> s0, s1;
    QStringList g0, g1;
    for (int i = 0; i < vecs.size(); ++i) {
        addTo(label[i] == 0 ? s0 : s1, vecs[i]);
        (label[i] == 0 ? g0 : g1) << ids[i];
    }
    if (g0.isEmpty() || g1.isEmpty()) return out;
    const QVector<float> c0 = ranking::normalized(s0), c1 = ranking::normalized(s1);
    out.similarity = ranking::cosine(c0, c1);
    const bool zeroSmaller = g0.size() < g1.size() || (g0.size() == g1.size() && ids.indexOf(g0.first()) > ids.indexOf(g1.first()));
    out.minority = zeroSmaller ? g0 : g1;
    out.majority = zeroSmaller ? g1 : g0;
    out.minorityCentroid = zeroSmaller ? c0 : c1;
    const double share = double(out.minority.size()) / double(vecs.size());
    out.contaminated = !std::isnan(out.similarity) && out.similarity < kContamMaxSimilarity && share >= kContamMinShare;
    return out;
}

} // namespace review

// ---- kontextus ------------------------------------------------------------------

RankContext buildRankContext(const ReviewInput& in, const SideReport& sides)
{
    RankContext ctx;
    ctx.sidesActive = sides.active;
    ctx.meetingTags = in.meeting.tagIds;

    QHash<QString, QVector<QVector<float>>> prints;   // case-folded név → lenyomatok
    QHash<QString, QString> printNames;
    for (auto it = in.personPrints.cbegin(); it != in.personPrints.cend(); ++it) {
        prints[it.key().toCaseFolded()] += it.value();
        printNames.insert(it.key().toCaseFolded(), it.key());
    }
    auto tagsOf = [&](const QString& person) {
        return (in.personTags && !person.isEmpty()) ? in.personTags(person) : QStringList();
    };

    for (const PersonSide& ps : sides.persons) {
        SpeakerProfile p;
        p.key = ps.speakerKey;
        p.personName = ps.personName;
        p.displayName = ps.displayName;
        p.side = ps.side;
        p.sideBasis = ps.basis;
        p.lineSide = majoritySide(ps.allLines.local, ps.allLines.remote);
        if (!p.personName.isEmpty()) p.prints = prints.value(p.personName.toCaseFolded());
        p.tags = tagsOf(p.personName);
        ctx.index.insert(p.key, int(ctx.speakers.size()));
        ctx.speakers.append(p);
    }
    // Sor nélküli, kézzel felvett résztvevők.
    for (const OverlayParticipant& op : in.overlay.participants) {
        if (ctx.index.contains(op.key)) continue;
        SpeakerProfile p;
        p.key = op.key;
        p.personName = op.person;
        p.displayName = speakeredit::speakerDisplayName(in.overlay, in.meeting.speakerMap, op.key);
        p.side = op.person.isEmpty() ? Side::Unknown
                                     : in.sideHints.learnedSides.value(op.person.toCaseFolded(), Side::Unknown);
        p.sideBasis = decided(p.side) ? QStringLiteral("learned") : QStringLiteral("none");
        if (!p.personName.isEmpty()) p.prints = prints.value(p.personName.toCaseFolded());
        p.tags = tagsOf(p.personName);
        ctx.index.insert(p.key, int(ctx.speakers.size()));
        ctx.speakers.append(p);
    }

    // Sor-összegek és megerősített magok.
    QHash<QString, QVector<double>> coreSum;
    QHash<QString, int> coreCount;
    for (const Row& r : rowsOf(in, sides)) {
        const auto it = ctx.index.constFind(r.key);
        if (it == ctx.index.constEnd()) continue;
        SpeakerProfile& p = ctx.speakers[it.value()];
        ++p.lines;
        if (!r.clean()) continue;
        addTo(p.sum, *r.emb);
        ++p.sumCount;
        if (r.locked) {
            addTo(coreSum[r.key], *r.emb);
            ++coreCount[r.key];
        }
    }
    for (SpeakerProfile& p : ctx.speakers) {
        const int n = coreCount.value(p.key);
        if (n >= speakeredit::kMinSpeakerLines) {
            p.core = ranking::normalized(coreSum.value(p.key));
            p.confirmedLines = n;
        }
    }

    // A meetingen kívüli, lenyomattal bíró személyek.
    QSet<QString> inMeeting;
    for (const SpeakerProfile& p : std::as_const(ctx.speakers))
        if (!p.personName.isEmpty()) inMeeting.insert(p.personName.toCaseFolded());
    QStringList outside;
    for (auto it = prints.cbegin(); it != prints.cend(); ++it)
        if (!inMeeting.contains(it.key()) && !it.value().isEmpty()) outside << it.key();
    std::sort(outside.begin(), outside.end());
    for (const QString& folded : std::as_const(outside)) {
        SpeakerProfile p;
        p.personName = printNames.value(folded);
        p.displayName = p.personName;
        p.prints = prints.value(folded);
        p.side = in.sideHints.learnedSides.value(folded, Side::Unknown);
        p.sideBasis = decided(p.side) ? QStringLiteral("learned") : QStringLiteral("none");
        p.tags = tagsOf(p.personName);
        ctx.speakers.append(p);
    }
    return ctx;
}

// ---- csoportok ------------------------------------------------------------------

QVector<ReviewGroup> buildReviewGroups(const ReviewInput& in, const SideReport& sides, const RankContext& ctx)
{
    const QVector<Row> rows = rowsOf(in, sides);
    QHash<QString, int> rowOf;
    for (int i = 0; i < rows.size(); ++i) rowOf.insert(rows[i].id, i);
    QVector<bool> used(rows.size(), false);

    QVector<ReviewGroup> out;
    // Csoport-gyűjtő: id → index az out-ban.
    QHash<QString, int> byId;
    QHash<QString, QVector<double>> voiceSums;   // id → a javasolt hang-pontszámai
    auto groupFor = [&](ReviewKind kind, const QString& current, const QString& pKey, const QString& pPerson) -> ReviewGroup& {
        const QString id = groupId(kind, current, pKey, pPerson);
        auto it = byId.find(id);
        if (it == byId.end()) {
            ReviewGroup g;
            g.id = id;
            g.kind = kind;
            g.currentSpeakerKey = current;
            g.proposedSpeakerKey = pKey;
            g.proposedPersonName = pPerson;
            it = byId.insert(id, int(out.size()));
            out.append(g);
        }
        return out[it.value()];
    };

    // 1. Sáv-ellentmondás.
    if (sides.active) {
        for (const SideConflict& c : sides.conflicts) {
            const int ri = rowOf.value(c.utteranceId, -1);
            if (ri < 0 || rows[ri].locked || used[ri]) continue;
            const Row& r = rows[ri];
            QString pKey, pPerson;
            double voice = qQNaN();
            // Hang-beágyazás nélkül (rövid sor) nincs automatikus javaslat: a sáv csak azt mondja
            // meg, melyik OLDAL, azt nem, hogy ki — ezeket egyenként kell eldönteni.
            const QVector<Candidate> cands = r.emb
                ? ranking::rankForLine(ctx, *r.emb, c.lineSide, r.key, r.clean())
                : QVector<Candidate>();
            for (const Candidate& cand : cands) {
                if (samePerson(ctx, r.key, cand) || cand.otherSide) continue;
                const SpeakerProfile* p = findProfile(ctx, cand);
                if (!p || p->side != c.lineSide) continue;
                pKey = cand.speakerKey;
                pPerson = cand.personName;
                if (r.emb) {
                    ranking::Probe probe;
                    probe.embedding = *r.emb;
                    voice = ranking::voiceScore(*p, probe);
                }
                break;
            }
            ReviewGroup& g = groupFor(ReviewKind::SideConflict, r.key, pKey, pPerson);
            g.utteranceIds << r.id;
            if (!std::isnan(voice)) voiceSums[g.id] << voice;
            used[ri] = true;
        }
        for (ReviewGroup& g : out) {
            if (g.kind != ReviewKind::SideConflict) continue;
            const SpeakerProfile* cur = ctx.profile(g.currentSpeakerKey);
            const Side curSide = cur ? cur->side : Side::Unknown;
            const Side lineSide = curSide == Side::Local ? Side::Remote : Side::Local;
            const int n = int(g.utteranceIds.size());
            const QString curName = nameOf(ctx, g.currentSpeakerKey, QString());
            const bool hasProposal = !g.proposedSpeakerKey.isEmpty() || !g.proposedPersonName.isEmpty();
            const QString target = nameOf(ctx, g.proposedSpeakerKey, g.proposedPersonName);
            g.title = tr("%n sor a másik sávon", n);
            g.subtitle = hasProposal ? tr("Most: %1 · javaslat: %2").arg(curName, target)
                                     : tr("Most: %1 · hang nélkül nem javaslok nevet, egyenként dönthető").arg(curName);
            g.evidence << makeEvidence(cur && cur->sideBasis == QLatin1String("manual") ? EvidenceKind::Manual : EvidenceKind::Side,
                                       Polarity::Contradict, -1.0, tr("másik sávon szóltak"),
                                       tr("%1: %2; a sorok: %3").arg(curName, ranking::sideLabel(curSide),
                                                                     ranking::sideLabel(lineSide)),
                                       QStringLiteral("tracks"));
            const QVector<double> vs = voiceSums.value(g.id);
            if (!vs.isEmpty()) {
                double avg = 0.0;
                for (double v : vs) avg += v;
                g.evidence << voiceSupport(avg / vs.size(), target);
            }
            if (hasProposal)
                g.evidence << makeEvidence(EvidenceKind::Side, Polarity::Support, 1.0,
                                           lineSide == Side::Remote ? tr("%1 a hívás hangján beszél").arg(target)
                                                                    : tr("%1 a mikrofonon beszél").arg(target),
                                           QString(), QStringLiteral("tracks"));
            const SpeakerProfile* prop = g.proposedSpeakerKey.isEmpty() ? nullptr : ctx.profile(g.proposedSpeakerKey);
            const PersonSide* curSideInfo = nullptr;
            for (const PersonSide& ps : sides.persons)
                if (ps.speakerKey == g.currentSpeakerKey) curSideInfo = &ps;
            g.confirmedBasis = (prop ? prop->confirmedLines : 0)
                + (curSideInfo ? curSideInfo->localLines + curSideInfo->remoteLines : 0);
        }
    }

    // 2. Hasonló sorok az új személyhez.
    if (!in.newPersonKey.isEmpty()) {
        if (const SpeakerProfile* np = ctx.profile(in.newPersonKey)) {
            const QVector<float> ref = np->core.isEmpty() ? ranking::normalized(np->sum) : np->core;
            const Side newSide = decided(np->side) ? np->side : np->lineSide;
            if (!ref.isEmpty()) {
                QStringList ids;
                QMap<QString, int> fromKeys;
                double sumSim = 0.0;
                for (int i = 0; i < rows.size(); ++i) {
                    const Row& r = rows[i];
                    if (used[i] || r.locked || !r.clean() || r.key == in.newPersonKey) continue;
                    if (sides.active && decided(newSide) && r.side != newSide) continue;
                    const double sim = ranking::cosine(ref, *r.emb);
                    if (std::isnan(sim) || sim < review::kNewPersonSimilarity) continue;
                    double own = qQNaN();
                    if (const SpeakerProfile* op = ctx.profile(r.key)) {
                        ranking::Probe probe;
                        probe.embedding = *r.emb;
                        probe.currentKey = r.key;
                        probe.inCurrentSum = true;
                        own = ranking::voiceScore(*op, probe);
                    }
                    if (!std::isnan(own) && own >= sim) continue;
                    ids << r.id;
                    ++fromKeys[r.key];
                    sumSim += sim;
                    used[i] = true;
                }
                if (!ids.isEmpty()) {
                    ReviewGroup& g = groupFor(ReviewKind::SimilarToNewPerson, QString(), in.newPersonKey, np->personName);
                    g.utteranceIds = ids;
                    const int n = int(ids.size());
                    const double avg = sumSim / n;
                    g.title = tr("Még %n sor hangja hasonlít: %1", n).arg(np->displayName);
                    g.subtitle = sides.active && decided(newSide)
                        ? tr("hang %1, mind a(z) %2 oldalon").arg(percent(avg), ranking::sideLabel(newSide))
                        : tr("hang %1").arg(percent(avg));
                    g.evidence << voiceSupport(avg, np->displayName);
                    if (sides.active && decided(newSide))
                        g.evidence << makeEvidence(EvidenceKind::Side, Polarity::Support, 1.0,
                                                   newSide == Side::Remote ? tr("mind a hívás hangján") : tr("mind a mikrofonon"),
                                                   QString(), QStringLiteral("tracks"));
                    // A leggyakoribb forrás a „jelenlegi".
                    int best = 0;
                    for (auto it = fromKeys.cbegin(); it != fromKeys.cend(); ++it)
                        if (it.value() > best) { best = it.value(); g.currentSpeakerKey = it.key(); }
                    g.id = groupId(ReviewKind::SimilarToNewPerson, QString(), in.newPersonKey, np->personName);
                    g.confirmedBasis = np->confirmedLines;
                }
            }
        }
    }

    // 3. Szennyezett mag (zárolt sorok: más csoporttal nem fed át).
    for (const SpeakerProfile& p : ctx.speakers) {
        if (p.key.isEmpty() || p.confirmedLines < review::kContamMinLines) continue;
        const review::CoreSplit split = review::splitConfirmedCore(in, sides, p.key);
        if (!split.contaminated) continue;
        // Javaslat: a kisebb fél centroidjához legjobban illő más beszélő / személy, ha elég jól illik.
        int local = 0, remote = 0;
        for (const QString& id : split.minority) {
            const int ri = rowOf.value(id, -1);
            if (ri < 0) continue;
            local += rows[ri].side == Side::Local;
            remote += rows[ri].side == Side::Remote;
        }
        ranking::Probe probe;
        probe.embedding = split.minorityCentroid;
        probe.side = majoritySide(local, remote);
        probe.currentKey = p.key;
        QString pKey, pPerson;
        double voice = qQNaN();
        for (const Candidate& cand : ranking::rank(ctx, probe)) {
            if (samePerson(ctx, p.key, cand) || cand.otherSide) continue;
            const SpeakerProfile* cp = findProfile(ctx, cand);
            const double v = cp ? ranking::voiceScore(*cp, probe) : qQNaN();
            if (std::isnan(v) || v < ranking::kVoiceGood) continue;
            pKey = cand.speakerKey;
            pPerson = cand.personName;
            voice = v;
            break;
        }
        ReviewGroup& g = groupFor(ReviewKind::ContaminatedCore, p.key, pKey, pPerson);
        g.utteranceIds = split.minority;
        const int n = int(split.minority.size());
        g.title = tr("%1 megerősített sorai két hangra esnek").arg(p.displayName);
        g.subtitle = tr("Szétválasztás: %n sor → %1", n).arg(nameOf(ctx, pKey, pPerson));
        g.evidence << makeEvidence(EvidenceKind::Similarity, Polarity::Contradict, split.similarity,
                                   tr("két hang: %1 egyezés").arg(percent(split.similarity)),
                                   tr("%1 + %2 megerősített sor").arg(split.majority.size()).arg(split.minority.size()),
                                   QStringLiteral("samples"));
        if (!std::isnan(voice)) g.evidence << voiceSupport(voice, nameOf(ctx, pKey, pPerson));
        g.confirmedBasis = p.confirmedLines;
    }

    // 4. Mag-eltérés.
    for (int i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        if (used[i] || r.locked || !r.clean()) continue;
        const SpeakerProfile* own = ctx.profile(r.key);
        if (!own || own->core.isEmpty()) continue;
        const double ownFit = ranking::cosine(own->core, *r.emb);
        const double margin = r.durationMs < speakeredit::kReliableMs ? review::kCoreMargin * 2.0 : review::kCoreMargin;
        const SpeakerProfile* best = nullptr;
        double bestFit = qQNaN();
        for (const SpeakerProfile& o : ctx.speakers) {
            if (o.key.isEmpty() || o.key == r.key || o.core.isEmpty()) continue;
            if (!o.personName.isEmpty() && o.personName.compare(own->personName, Qt::CaseInsensitive) == 0) continue;
            const double f = ranking::cosine(o.core, *r.emb);
            if (!std::isnan(f) && (std::isnan(bestFit) || f > bestFit)) { bestFit = f; best = &o; }
        }
        if (!best || std::isnan(ownFit) || bestFit - ownFit < margin) continue;
        ReviewGroup& g = groupFor(ReviewKind::CoreMismatch, r.key, best->key, best->personName);
        g.utteranceIds << r.id;
        voiceSums[g.id] << bestFit;
        voiceSums[g.id + QStringLiteral("#own")] << ownFit;
        used[i] = true;
    }
    for (ReviewGroup& g : out) {
        if (g.kind != ReviewKind::CoreMismatch) continue;
        const int n = int(g.utteranceIds.size());
        const QString target = nameOf(ctx, g.proposedSpeakerKey, g.proposedPersonName);
        const QString curName = nameOf(ctx, g.currentSpeakerKey, QString());
        g.title = tr("%n sor hangja inkább: %1", n).arg(target);
        g.subtitle = tr("Most: %1").arg(curName);
        auto avgOf = [](const QVector<double>& v) {
            double s = 0.0;
            for (double x : v) s += x;
            return v.isEmpty() ? qQNaN() : s / v.size();
        };
        const double toTarget = avgOf(voiceSums.value(g.id));
        const double toOwn = avgOf(voiceSums.value(g.id + QStringLiteral("#own")));
        g.evidence << voiceSupport(toTarget, target);
        g.evidence << makeEvidence(EvidenceKind::Voice, Polarity::Contradict, toOwn,
                                   tr("%1 hangjához csak %2").arg(curName, percent(toOwn)),
                                   tr("a megerősített soraihoz mérve"), QStringLiteral("samples"));
        const SpeakerProfile* prop = ctx.profile(g.proposedSpeakerKey);
        const SpeakerProfile* cur = ctx.profile(g.currentSpeakerKey);
        g.confirmedBasis = (prop ? prop->confirmedLines : 0) + (cur ? cur->confirmedLines : 0);
        g.evidence += ranking::similarityWarnings(ctx, g.currentSpeakerKey);
    }

    // 5. Rövid sorok (csak tájékoztató).
    {
        QStringList ids;
        for (int i = 0; i < rows.size(); ++i) {
            const Row& r = rows[i];
            if (used[i] || r.locked || r.emb || r.durationMs >= speakeredit::kMinEmbedMs) continue;
            ids << r.id;
        }
        if (!ids.isEmpty()) {
            ReviewGroup& g = groupFor(ReviewKind::ShortLines, QString(), QString(), QString());
            g.utteranceIds = ids;
            g.title = tr("%n rövid sor", int(ids.size()));
            g.subtitle = tr("Túl rövidek a hang-ellenőrzéshez — csak tájékoztató");
        }
    }

    // Időrend a csoportokon belül; a csoportok fajta, majd méret szerint.
    for (ReviewGroup& g : out)
        std::sort(g.utteranceIds.begin(), g.utteranceIds.end(),
                  [&](const QString& a, const QString& b) { return rowOf.value(a) < rowOf.value(b); });
    auto order = [](ReviewKind k) {
        switch (k) {
        case ReviewKind::SideConflict:       return 0;
        case ReviewKind::SimilarToNewPerson: return 1;
        case ReviewKind::ContaminatedCore:   return 2;
        case ReviewKind::CoreMismatch:       return 3;
        case ReviewKind::ShortLines:         return 4;
        }
        return 5;
    };
    std::stable_sort(out.begin(), out.end(), [&](const ReviewGroup& a, const ReviewGroup& b) {
        if (order(a.kind) != order(b.kind)) return order(a.kind) < order(b.kind);
        return a.utteranceIds.size() > b.utteranceIds.size();
    });
    return out;
}

ReviewResult analyzeReview(const ReviewInput& in, const MeetingActivity& activity)
{
    ReviewResult r;
    r.sides = analyzeSides(in.meeting, in.lines, in.overlay, activity, in.userName, in.sideHints);
    r.context = buildRankContext(in, r.sides);
    r.groups = buildReviewGroups(in, r.sides, r.context);
    QSet<QString> locked;
    for (const LineSide& ls : r.sides.lines)
        if (ls.locked) locked.insert(ls.utteranceId);
    for (const SideConflict& c : r.sides.conflicts)
        if (!locked.contains(c.utteranceId)) r.sideConflicts.insert(c.utteranceId, c.speakerKey);
    return r;
}

} // namespace tanara
