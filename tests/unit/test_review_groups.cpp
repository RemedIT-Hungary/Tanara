//
// Jelölt-rangsor (CandidateRanker) és Átnézendő csoportok (ReviewGroups) KITALÁLT adaton: a
// „hangok" kis dimenziós egységvektorok, a sáv-aktivitás szintetikus (nincs hang, nincs I/O).
//
#include <QtTest>

#include <cmath>

#include "tanara/edit/CandidateRanker.h"
#include "tanara/edit/ReviewGroups.h"

using namespace tanara;
using namespace tanara::ranking;

namespace {

constexpr int kDim = 6;

// Egységvektor a k. tengely felé, opcionális keveréssel a j. tengely felé (cos = c a k-hoz).
QVector<float> voice(int k, int j = -1, float c = 1.0f)
{
    QVector<float> v(kDim, 0.0f);
    v[k] = c;
    if (j >= 0) v[j] = std::sqrt(std::max(0.0f, 1.0f - c * c));
    return v;
}

QVector<double> sumOf(const QVector<QVector<float>>& vs)
{
    QVector<double> s(kDim, 0.0);
    for (const auto& v : vs)
        for (int i = 0; i < kDim; ++i) s[i] += v[i];
    return s;
}

SpeakerProfile profile(const QString& key, const QString& person, Side side, int lines,
                       const QVector<QVector<float>>& voices, bool core)
{
    SpeakerProfile p;
    p.key = key;
    p.personName = person;
    p.displayName = person.isEmpty() ? key : person;
    p.side = side;
    p.sideBasis = QStringLiteral("lines");
    p.lines = lines;
    p.sum = sumOf(voices);
    p.sumCount = int(voices.size());
    if (core) {
        p.core = normalized(p.sum);
        p.confirmedLines = int(voices.size());
    }
    return p;
}

RankContext context(const QVector<SpeakerProfile>& ps, bool sidesActive = true)
{
    RankContext ctx;
    ctx.sidesActive = sidesActive;
    for (const SpeakerProfile& p : ps) {
        if (!p.key.isEmpty()) ctx.index.insert(p.key, int(ctx.speakers.size()));
        ctx.speakers.append(p);
    }
    return ctx;
}

const Candidate* find(const QVector<Candidate>& cs, const QString& key)
{
    for (const Candidate& c : cs)
        if (c.speakerKey == key) return &c;
    return nullptr;
}

bool hasEvidence(const QVector<Evidence>& ev, EvidenceKind k, Polarity p, const QString& fix = {})
{
    for (const Evidence& e : ev)
        if (e.kind == k && e.polarity == p && (fix.isEmpty() || e.fixTarget == fix)) return true;
    return false;
}

// ---- meeting-szintű forgatókönyv -------------------------------------------------

struct Span { qint64 s, e; float db; };

TrackActivity activity(const QString& id, TrackKind kind, qint64 totalMs, const QVector<Span>& spans)
{
    TrackActivity t;
    t.trackId = id;
    t.kind = kind;
    t.frameMs = 50;
    t.dbAboveFloor = QVector<float>(int(totalMs / 50), 0.0f);
    for (const Span& sp : spans)
        for (qint64 ms = sp.s; ms < sp.e; ms += 50)
            if (ms / 50 < t.dbAboveFloor.size()) t.dbAboveFloor[ms / 50] = sp.db;
    return t;
}

// Sorok: 4 mp-esek 5 mp-enként. side: 'L' mic, 'R' loopback, '-' rövid (1 mp, nincs hang).
struct LineSpec { QString raw; char side; int voiceAxis; bool confirmed = false; };

struct Scenario {
    ReviewInput in;
    MeetingActivity act;
};

Scenario build(const QVector<LineSpec>& specs)
{
    Scenario sc;
    QVector<Span> mic, loop;
    Track tm; tm.id = "mic"; tm.kind = TrackKind::Mic;
    Track tl; tl.id = "loop"; tl.kind = TrackKind::Loopback;
    sc.in.meeting.tracks = {tm, tl};
    for (int i = 0; i < specs.size(); ++i) {
        const LineSpec& s = specs[i];
        const qint64 st = i * 5000, en = st + (s.side == '-' ? 1000 : 4000);
        TranscriptLine l{QStringLiteral("u%1").arg(st), st, en, s.raw, QStringLiteral("x")};
        sc.in.lines << l;
        if (s.side == 'L' || s.side == '-') mic << Span{st, en, 30.0f};
        else loop << Span{st, en, 30.0f};
        if (s.side != '-' && s.voiceAxis >= 0) sc.in.embeddings.insert(l.id, voice(s.voiceAxis));
        if (s.confirmed) sc.in.overlay.utterances[l.id].confirmed = true;
    }
    const qint64 total = specs.size() * 5000 + 1000;
    sc.act.tracks = {activity("mic", TrackKind::Mic, total, mic), activity("loop", TrackKind::Loopback, total, loop)};
    return sc;
}

const ReviewGroup* group(const QVector<ReviewGroup>& gs, ReviewKind k)
{
    for (const ReviewGroup& g : gs)
        if (g.kind == k) return &g;
    return nullptr;
}

} // namespace

class ReviewGroupsTest : public QObject {
    Q_OBJECT
private slots:
    void sideConflictDemotesButKeeps()
    {
        // A próba hangja Annához áll közelebb (0.8 vs 0.6), de a loopbackon szólt, Anna pedig
        // mikrofonon beszél → Anna lejjebb kerül (otherSide), de a listán marad.
        const RankContext ctx = context({profile("A", "Anna", Side::Local, 10, {voice(0), voice(0), voice(0)}, true),
                                         profile("B", "Béla", Side::Remote, 10, {voice(1), voice(1), voice(1)}, true)});
        const QVector<float> probe = voice(0, 1, 0.8f);
        QVector<Candidate> cs = rankForLine(ctx, probe, Side::Remote, "A", false);
        QCOMPARE(cs.size(), 2);
        QCOMPARE(cs[0].speakerKey, QStringLiteral("B"));
        const Candidate* a = find(cs, "A");
        QVERIFY(a);
        QVERIFY(a->otherSide);
        QVERIFY(std::abs(a->score - (0.8 - kSidePenalty + kLineWeight * 0.5)) < 1e-6);
        const QVector<Evidence> why = whyNot(*a);
        QVERIFY(hasEvidence(why, EvidenceKind::Side, Polarity::Contradict, "tracks"));
        QVERIFY(hasEvidence(find(cs, "B")->evidence, EvidenceKind::Side, Polarity::Support));

        // Sáv-adat nélkül a hang dönt.
        cs = rankForLine(context(ctx.speakers, /*sidesActive*/ false), probe, Side::Remote, "A", false);
        QCOMPARE(cs[0].speakerKey, QStringLiteral("A"));
        QVERIFY(!cs[0].otherSide);
    }

    void weightsTagsAndLines()
    {
        // Azonos hang: a címke és a sorok száma dönt.
        SpeakerProfile a = profile("A", "Anna", Side::Unknown, 20, {voice(0), voice(0)}, false);
        SpeakerProfile b = profile("B", "Béla", Side::Unknown, 2, {voice(0), voice(0)}, false);
        a.tags = {"projekt-x"};
        b.tags = {"marketing"};
        RankContext ctx = context({a, b});
        ctx.meetingTags = {"projekt-x", "heti"};
        const QVector<Candidate> cs = rankForLine(ctx, voice(0), Side::Unknown, "", false);
        QCOMPARE(cs[0].speakerKey, QStringLiteral("A"));
        const Candidate* ca = find(cs, "A");
        const Candidate* cb = find(cs, "B");
        QVERIFY(std::abs(ca->score - (1.0 + kTagSupport + kLineWeight)) < 1e-6);
        QVERIFY(std::abs(cb->score - (1.0 - kTagMismatch + kLineWeight * 0.1)) < 1e-6);
        QVERIFY(hasEvidence(ca->evidence, EvidenceKind::Tag, Polarity::Support, "tags"));
        QVERIFY(hasEvidence(cb->evidence, EvidenceKind::Tag, Polarity::Contradict, "tags"));
        QVERIFY(hasEvidence(ca->evidence, EvidenceKind::LineCount, Polarity::Support));
        QCOMPARE(ca->linesHere, 20);
    }

    void leaveOneOutAndPrints()
    {
        // A mostani beszélő 3 sorból álló összegéből a próba kimarad (nem önmagához mér).
        SpeakerProfile a = profile("A", "Anna", Side::Unknown, 3, {voice(0), voice(1), voice(1)}, false);
        const RankContext ctx = context({a});
        Probe p;
        p.embedding = voice(0);
        p.currentKey = "A";
        p.inCurrentSum = true;
        QString basis;
        QVERIFY(std::abs(voiceScore(ctx.speakers[0], p, &basis)) < 1e-6);   // csak a két voice(1) marad
        QCOMPARE(basis, QStringLiteral("lines"));
        p.inCurrentSum = false;
        QVERIFY(voiceScore(ctx.speakers[0], p) > 0.4);
        // A lenyomat is referencia; a jobbik számít.
        SpeakerProfile c;
        c.personName = "Cecil";
        c.prints = {voice(2)};
        p.embedding = voice(2);
        QVERIFY(std::abs(voiceScore(c, p, &basis) - 1.0) < 1e-6);
        QCOMPARE(basis, QStringLiteral("prints"));
    }

    void similarityWarning()
    {
        const RankContext ctx = context({profile("A", "Anna", Side::Unknown, 5, {voice(0), voice(0), voice(0)}, true),
                                         profile("B", "Béla", Side::Unknown, 5,
                                                 {voice(0, 1, 0.95f), voice(0, 1, 0.95f), voice(0, 1, 0.95f)}, true),
                                         profile("C", "Cecil", Side::Unknown, 5, {voice(2), voice(2), voice(2)}, true)});
        const QVector<Evidence> w = similarityWarnings(ctx, "A");
        QCOMPARE(w.size(), 1);
        QCOMPARE(w[0].fixTarget, QStringLiteral("pair:B"));
        QCOMPARE(w[0].kind, EvidenceKind::Similarity);
        QVERIFY(similarityWarnings(ctx, "C").isEmpty());
        // A „Miért ő?" is tartalmazza.
        QVERIFY(hasEvidence(selfEvidence(ctx, "A"), EvidenceKind::Similarity, Polarity::Neutral, "pair:B"));
        // „Nem ő? Valójában…": a beszélő maga nincs a listán, a hasonló hang elöl.
        const QVector<Candidate> cs = rankForSpeaker(ctx, "A");
        QCOMPARE(cs.size(), 2);
        QCOMPARE(cs[0].speakerKey, QStringLiteral("B"));
    }

    void sideConflictGroup()
    {
        // Ádám (mic) sorai közé 3 olyan került, ami a loopbackon szólt Béla hangján; Béla (B2)
        // megerősített sorai a loopbackon → egy csoport: B1 → B2, sáv-ellentmondás bizonyítékkal.
        Scenario sc = build({{"B1", 'L', 0}, {"B2", 'R', 1, true}, {"B1", 'L', 0}, {"B1", 'R', 1},
                             {"B2", 'R', 1, true}, {"B1", 'R', 1}, {"B1", 'L', 0}, {"B2", 'R', 1, true},
                             {"B1", 'R', 1}, {"B1", 'L', 0}});
        sc.in.meeting.speakerMap = {{"B1", "Ádám"}, {"B2", "Béla"}};
        sc.in.userName = "Ádám";
        const ReviewResult r = analyzeReview(sc.in, sc.act);
        QVERIFY(r.sides.active);
        const ReviewGroup* g = group(r.groups, ReviewKind::SideConflict);
        QVERIFY(g);
        QCOMPARE(g->currentSpeakerKey, QStringLiteral("B1"));
        QCOMPARE(g->proposedSpeakerKey, QStringLiteral("B2"));
        QCOMPARE(g->utteranceIds, (QStringList{"u15000", "u25000", "u40000"}));
        QVERIFY(hasEvidence(g->evidence, EvidenceKind::Side, Polarity::Contradict, "tracks"));
        QVERIFY(hasEvidence(g->evidence, EvidenceKind::Voice, Polarity::Support));
        QVERIFY(g->confirmedBasis >= 3);
        QCOMPARE(r.sideConflicts.size(), 3);
        QCOMPARE(r.sideConflicts.value("u15000"), QStringLiteral("B1"));
        QVERIFY(!g->id.isEmpty());
        // Nincs megfelelő azonos oldali jelölt → a javaslat üres (új résztvevő).
        Scenario solo = build({{"B1", 'L', 0}, {"B1", 'L', 0}, {"B1", 'R', 1}, {"B1", 'L', 0}});
        solo.in.meeting.speakerMap = {{"B1", "Ádám"}};
        solo.in.userName = "Ádám";
        const ReviewResult rs = analyzeReview(solo.in, solo.act);
        const ReviewGroup* gs = group(rs.groups, ReviewKind::SideConflict);
        QVERIFY(gs);
        QVERIFY(gs->proposedSpeakerKey.isEmpty() && gs->proposedPersonName.isEmpty());
    }

    void coreMismatchGroup()
    {
        // Sáv-adat nélkül (egysávos): A és B megerősített magja; A két nem megerősített sora B hangján.
        Scenario sc = build({{"A", 'L', 0, true}, {"A", 'L', 0, true}, {"A", 'L', 0, true}, {"B", 'L', 1, true},
                             {"B", 'L', 1, true}, {"B", 'L', 1, true}, {"A", 'L', 1}, {"A", 'L', 0}, {"A", 'L', 1}});
        sc.act.tracks.removeLast();   // csak mic
        const ReviewResult r = analyzeReview(sc.in, sc.act);
        QVERIFY(!r.sides.active);
        const ReviewGroup* g = group(r.groups, ReviewKind::CoreMismatch);
        QVERIFY(g);
        QCOMPARE(g->currentSpeakerKey, QStringLiteral("A"));
        QCOMPARE(g->proposedSpeakerKey, QStringLiteral("B"));
        QCOMPARE(g->utteranceIds, (QStringList{"u30000", "u40000"}));
        QCOMPARE(g->confirmedBasis, 6);
        QVERIFY(hasEvidence(g->evidence, EvidenceKind::Voice, Polarity::Contradict, "samples"));
        QVERIFY(!group(r.groups, ReviewKind::SideConflict));
    }

    void shortLinesGroup()
    {
        Scenario sc = build({{"A", 'L', 0}, {"A", '-', -1}, {"B", '-', -1}, {"A", 'L', 0}});
        const ReviewResult r = analyzeReview(sc.in, sc.act);
        const ReviewGroup* g = group(r.groups, ReviewKind::ShortLines);
        QVERIFY(g);
        QCOMPARE(g->utteranceIds.size(), 2);
        QVERIFY(g->proposedSpeakerKey.isEmpty());
        QCOMPARE(r.groups.last().kind, ReviewKind::ShortLines);   // a végén
    }

    void contaminatedCore()
    {
        // A 8 megerősített sora: 5 a 0. hangon, 3 a 2. hangon (cos = 0) → két klaszter (37.5%).
        QVector<LineSpec> specs;
        for (int i = 0; i < 5; ++i) specs << LineSpec{"A", 'L', 0, true};
        for (int i = 0; i < 3; ++i) specs << LineSpec{"A", 'L', 2, true};
        specs << LineSpec{"C", 'L', 2, true} << LineSpec{"C", 'L', 2, true} << LineSpec{"C", 'L', 2, true};
        Scenario sc = build(specs);
        sc.act.tracks.removeLast();
        ReviewResult r = analyzeReview(sc.in, sc.act);
        const review::CoreSplit split = review::splitConfirmedCore(sc.in, r.sides, "A");
        QVERIFY(split.contaminated);
        QCOMPARE(split.minority, (QStringList{"u25000", "u30000", "u35000"}));
        QVERIFY(split.similarity < review::kContamMaxSimilarity);
        const ReviewGroup* g = group(r.groups, ReviewKind::ContaminatedCore);
        QVERIFY(g);
        QCOMPARE(g->currentSpeakerKey, QStringLiteral("A"));
        QCOMPARE(g->proposedSpeakerKey, QStringLiteral("C"));
        QCOMPARE(g->utteranceIds, split.minority);
        QVERIFY(hasEvidence(g->evidence, EvidenceKind::Similarity, Polarity::Contradict));

        // C nélkül: új névtelen résztvevő a javaslat.
        specs.resize(8);
        Scenario noC = build(specs);
        r = analyzeReview(noC.in, MeetingActivity{});
        g = group(r.groups, ReviewKind::ContaminatedCore);
        QVERIFY(g);
        QVERIFY(g->proposedSpeakerKey.isEmpty() && g->proposedPersonName.isEmpty());

        // Egységes mag (csak a 0. hang, kis zajjal) → nem szennyezett.
        QVector<LineSpec> clean;
        for (int i = 0; i < 8; ++i) clean << LineSpec{"A", 'L', 0, true};
        Scenario cl = build(clean);
        r = analyzeReview(cl.in, MeetingActivity{});
        QVERIFY(!group(r.groups, ReviewKind::ContaminatedCore));
        QVERIFY(!review::splitConfirmedCore(cl.in, r.sides, "A").contaminated);
    }

    void similarToNewPerson()
    {
        // Az új résztvevő (participant:1) két sora a 3. hangon; A-nál még 2 ilyen sor van
        // (és 2 a saját hangján) → csoport a 2 sorral.
        Scenario sc = build({{"A", 'L', 0}, {"A", 'L', 3}, {"A", 'L', 0}, {"A", 'L', 3}, {"A", 'L', 3}, {"A", 'L', 3}});
        OverlayParticipant p;
        p.key = "participant:1";
        p.label = "Új beszélő 1";
        sc.in.overlay.participants << p;
        sc.in.overlay.utterances["u20000"].speaker = "participant:1";
        sc.in.overlay.utterances["u20000"].corrected = true;
        sc.in.overlay.utterances["u25000"].speaker = "participant:1";
        sc.in.overlay.utterances["u25000"].corrected = true;
        sc.in.newPersonKey = "participant:1";
        sc.act.tracks.removeLast();
        const ReviewResult r = analyzeReview(sc.in, sc.act);
        const ReviewGroup* g = group(r.groups, ReviewKind::SimilarToNewPerson);
        QVERIFY(g);
        QCOMPARE(g->proposedSpeakerKey, QStringLiteral("participant:1"));
        QCOMPARE(g->currentSpeakerKey, QStringLiteral("A"));
        QCOMPARE(g->utteranceIds, (QStringList{"u5000", "u15000"}));
    }

    void outsidePersonCandidate()
    {
        // A meetingen kívüli, lenyomattal bíró személy is jelölt (kulcs nélkül), tanult oldallal.
        Scenario sc = build({{"A", 'L', 0}, {"A", 'L', 0}, {"A", 'R', 4}});
        sc.in.meeting.speakerMap = {{"A", "Anna"}};
        sc.in.personPrints.insert("Dénes", {voice(4)});
        sc.in.sideHints.learnedSides.insert("dénes", Side::Remote);
        const ReviewResult r = analyzeReview(sc.in, sc.act);
        bool found = false;
        for (const SpeakerProfile& p : r.context.speakers)
            if (p.personName == "Dénes") { found = true; QVERIFY(p.key.isEmpty()); QCOMPARE(p.side, Side::Remote); }
        QVERIFY(found);
        const QVector<Candidate> cs = rankForLine(r.context, voice(4), Side::Remote, "A", false);
        QCOMPARE(cs[0].personName, QStringLiteral("Dénes"));
        QVERIFY(cs[0].speakerKey.isEmpty());
    }

    void twoMeansDeterministic()
    {
        QVector<QVector<float>> v{voice(0), voice(1), voice(0, 5, 0.9f), voice(1, 5, 0.9f)};
        const QVector<int> l = review::twoMeans(v);
        QCOMPARE(l.size(), 4);
        QCOMPARE(l[0], l[2]);
        QCOMPARE(l[1], l[3]);
        QVERIFY(l[0] != l[1]);
        QVERIFY(review::twoMeans({voice(0)}).isEmpty());
    }
};

QTEST_GUILESS_MAIN(ReviewGroupsTest)
#include "test_review_groups.moc"
