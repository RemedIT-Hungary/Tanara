// Candidate ranking (CandidateRanker) and review queue groups (ReviewGroups) on MADE-UP data:
// the "voices" are low-dimensional unit vectors, the track activity is synthetic (no audio, no I/O).
#include <QtTest>

#include <cmath>

#include "tanara/edit/CandidateRanker.h"
#include "tanara/edit/ReviewGroups.h"

using namespace tanara;
using namespace tanara::ranking;

namespace {

constexpr int kDim = 6;

// A unit vector toward axis k, optionally mixed toward axis j (cos = c to k).
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

// ---- meeting-level scenario ----

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

// Lines: 4 s each, every 5 s. side: 'L' mic, 'R' loopback, '-' short (1 s, no voice).
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
        // The probe's voice is closer to Anna (0.8 vs 0.6), but it played on the loopback and Anna
        // speaks on the microphone, so Anna drops lower (otherSide) but stays on the list.
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

        // Without track data the voice decides.
        cs = rankForLine(context(ctx.speakers, /*sidesActive*/ false), probe, Side::Remote, "A", false);
        QCOMPARE(cs[0].speakerKey, QStringLiteral("A"));
        QVERIFY(!cs[0].otherSide);
    }

    void weightsTagsAndLines()
    {
        // The same voice: the tag and the line count decide.
        SpeakerProfile a = profile("A", "Anna", Side::Unknown, 20, {voice(0), voice(0)}, false);
        SpeakerProfile b = profile("B", "Béla", Side::Unknown, 2, {voice(0), voice(0)}, false);
        // The tag evidence by PersonTagSnapshot's rule: Anna's manual tag is the meeting's; Béla has
        // a manual tag, the meeting's tags are mature (5 meetings), and he was at none of them.
        PersonTagSnapshot snap;
        QVector<PersonTagRow> rows;
        for (int i = 0; i < 5; ++i)
            rows << PersonTagRow{QStringLiteral("m%1").arg(i), QDateTime(), {"projekt-x", "heti"}, {"Cecil"}};
        snap.stats = std::make_shared<PersonTagStats>(PersonTagStats::compute(rows, QString()));
        snap.tagNames = {{"projekt-x", "Projekt X"}, {"heti", "Heti"}, {"marketing", "Marketing"}};
        snap.manual.insert(persontags::personKey("Anna"), {"projekt-x"});
        snap.manual.insert(persontags::personKey("Béla"), {"marketing"});
        a.tagEvidence = snap.evidence("Anna", {"projekt-x", "heti"});
        b.tagEvidence = snap.evidence("Béla", {"projekt-x", "heti"});
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
        // The chip and the explanation.
        for (const Evidence& e : ca->evidence)
            if (e.kind == EvidenceKind::Tag) {
                QCOMPARE(e.text, QStringLiteral("#Projekt X"));
                QVERIFY(e.detail.contains(QStringLiteral("0 / 5")));
            }
        for (const Evidence& e : cb->evidence)
            if (e.kind == EvidenceKind::Tag) QCOMPARE(e.text, QStringLiteral("más címkék"));
        // A young tag (< 5 meetings) does not penalize: no tag evidence.
        QVector<PersonTagRow> young = rows.mid(0, 2);
        snap.stats = std::make_shared<PersonTagStats>(PersonTagStats::compute(young, QString()));
        QVERIFY(snap.evidence("Béla", {"projekt-x", "heti"}).isEmpty());
    }

    void leaveOneOutAndPrints()
    {
        // The probe is left out of the current speaker's 3-line sum (it does not measure against itself).
        SpeakerProfile a = profile("A", "Anna", Side::Unknown, 3, {voice(0), voice(1), voice(1)}, false);
        const RankContext ctx = context({a});
        Probe p;
        p.embedding = voice(0);
        p.currentKey = "A";
        p.inCurrentSum = true;
        QString basis;
        QVERIFY(std::abs(voiceScore(ctx.speakers[0], p, &basis)) < 1e-6);   // only the two voice(1) remain
        QCOMPARE(basis, QStringLiteral("lines"));
        p.inCurrentSum = false;
        QVERIFY(voiceScore(ctx.speakers[0], p) > 0.4);
        // The voiceprint is a reference too; the better one counts.
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
        // "Why them?" contains it too.
        QVERIFY(hasEvidence(selfEvidence(ctx, "A"), EvidenceKind::Similarity, Polarity::Neutral, "pair:B"));
        // "Not them? Actually...": the speaker itself is not on the list, the similar voice first.
        const QVector<Candidate> cs = rankForSpeaker(ctx, "A");
        QCOMPARE(cs.size(), 2);
        QCOMPARE(cs[0].speakerKey, QStringLiteral("B"));
    }

    void sideConflictGroup()
    {
        // Among Ádám's (mic) lines 3 got in that played on the loopback in Béla's voice; Béla's (B2)
        // confirmed lines are on the loopback: one group: B1 to B2, with track-contradiction evidence.
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
        // No suitable same-side candidate: the suggestion is empty (a new participant).
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
        // Without track data (single track): the confirmed cores of A and B; A's two unconfirmed lines in B's voice.
        Scenario sc = build({{"A", 'L', 0, true}, {"A", 'L', 0, true}, {"A", 'L', 0, true}, {"B", 'L', 1, true},
                             {"B", 'L', 1, true}, {"B", 'L', 1, true}, {"A", 'L', 1}, {"A", 'L', 0}, {"A", 'L', 1}});
        sc.act.tracks.removeLast();   // mic only
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
        QCOMPARE(r.groups.last().kind, ReviewKind::ShortLines);   // at the end
    }

    void contaminatedCore()
    {
        // Its 8 confirmed lines: 5 on voice 0, 3 on voice 2 (cos = 0): two clusters (37.5%).
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

        // Without C: a new anonymous participant is the suggestion.
        specs.resize(8);
        Scenario noC = build(specs);
        r = analyzeReview(noC.in, MeetingActivity{});
        g = group(r.groups, ReviewKind::ContaminatedCore);
        QVERIFY(g);
        QVERIFY(g->proposedSpeakerKey.isEmpty() && g->proposedPersonName.isEmpty());

        // A uniform core (only voice 0, with a little noise) is not contaminated.
        QVector<LineSpec> clean;
        for (int i = 0; i < 8; ++i) clean << LineSpec{"A", 'L', 0, true};
        Scenario cl = build(clean);
        r = analyzeReview(cl.in, MeetingActivity{});
        QVERIFY(!group(r.groups, ReviewKind::ContaminatedCore));
        QVERIFY(!review::splitConfirmedCore(cl.in, r.sides, "A").contaminated);
    }

    void similarToNewPerson()
    {
        // The new participant's (participant:1) two lines are on voice 3; A still has 2 such lines
        // (and 2 on its own voice): a group with the 2 lines.
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
        // A person outside the meeting who has a voiceprint is a candidate too (without a key), with a learned side.
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

    // G10: a manual track assignment excludes only when all three conditions coincide; otherwise a deduction.
    void sideVeto_onlyWhenAllThreeHold()
    {
        auto manualProfile = [](const QString& key, const QString& person, Side side, const QVector<QVector<float>>& v) {
            SpeakerProfile p = profile(key, person, side, 10, v, true);
            p.sideBasis = QStringLiteral("manual");
            p.manualSide = side;
            return p;
        };
        RankContext ctx = context({manualProfile("A", "Anna", Side::Local, {voice(0), voice(0), voice(0)}),
                                   manualProfile("B", "Béla", Side::Remote, {voice(1), voice(1), voice(1)})});
        ctx.vetoActive = true;
        const QVector<float> probe = voice(0, 1, 0.9f);   // Anna's voice, on the call's voice

        // 1+2+3: Anna excluded, at the end of the list, without a score, with the exclusion's evidence.
        QVector<Candidate> cs = rankForLine(ctx, probe, Side::Remote, "", false);
        QCOMPARE(cs.size(), 2);
        QCOMPARE(cs[0].speakerKey, QStringLiteral("B"));
        QVERIFY(!cs[0].vetoed);
        QCOMPARE(cs[1].speakerKey, QStringLiteral("A"));
        QVERIFY(cs[1].vetoed);
        QVERIFY(cs[1].otherSide);
        QCOMPARE(cs[1].score, 0.0);
        bool vetoEv = false;
        for (const Evidence& e : cs[1].evidence)
            vetoEv |= e.kind == EvidenceKind::Manual && e.polarity == Polarity::Contradict
                   && e.text == QStringLiteral("kizárva: a sávja szerint nem ő") && e.fixTarget == QStringLiteral("tracks");
        QVERIFY(vetoEv);
        QVERIFY(!whyNot(cs[1]).isEmpty());

        // A missing assignment for one named person (vetoActive = false): no exclusion, only a deduction.
        ctx.vetoActive = false;
        cs = rankForLine(ctx, probe, Side::Remote, "", false);
        const Candidate* a = find(cs, "A");
        QVERIFY(a && !a->vetoed && a->otherSide);
        QVERIFY(a->score > 0.0);
        ctx.vetoActive = true;

        // A mixed / unknown line: no exclusion.
        for (Side s : {Side::Mixed, Side::Unknown}) {
            cs = rankForLine(ctx, probe, s, "", false);
            for (const Candidate& c : cs) QVERIFY(!c.vetoed);
            QCOMPARE(cs[0].speakerKey, QStringLiteral("A"));
        }

        // A multi-track person (microphone + call): never excluded.
        ctx.speakers[0].manualSide = Side::Mixed;
        ctx.speakers[0].side = Side::Mixed;
        cs = rankForLine(ctx, probe, Side::Remote, "", false);
        QVERIFY(!find(cs, "A")->vetoed);
        QCOMPARE(cs[0].speakerKey, QStringLiteral("A"));

        // A candidate without a manual assignment (learned side) is never excluded, only a deduction.
        ctx.speakers[0].manualSide = Side::Unknown;
        ctx.speakers[0].side = Side::Local;
        ctx.speakers[0].sideBasis = QStringLiteral("learned");
        cs = rankForLine(ctx, probe, Side::Remote, "", false);
        QVERIFY(!find(cs, "A")->vetoed);
        QVERIFY(find(cs, "A")->otherSide);
    }

    // G10: vetoActive comes from the meeting's named speakers (an anonymous one does not block);
    // the person's manual side is in the profile.
    void sideVeto_contextFromOverlay()
    {
        Scenario sc = build({{"B1", 'L', 0}, {"B2", 'R', 1}, {"B1", 'L', 0}, {"B3", 'R', 2}, {"B2", 'R', 1}});
        sc.in.meeting.speakerMap = {{"B1", "Ádám"}, {"B2", "Béla"}};
        sc.in.overlay.speakerTracks.insert("B1", {"mic"});
        ReviewResult r = analyzeReview(sc.in, sc.act);
        QVERIFY(!r.context.vetoActive);   // Béla has no assignment
        sc.in.overlay.speakerTracks.insert("B2", {"loop"});
        r = analyzeReview(sc.in, sc.act);
        QVERIFY(r.context.vetoActive);    // B3 is anonymous: does not count
        QCOMPARE(r.context.profile("B1")->manualSide, Side::Local);
        QCOMPARE(r.context.profile("B2")->manualSide, Side::Remote);
        QCOMPARE(r.context.profile("B3")->manualSide, Side::Unknown);
        const QVector<Candidate> cs = rankForLine(r.context, voice(0), Side::Remote, "B2", false);
        QVERIFY(find(cs, "B1")->vetoed);
        QCOMPARE(cs.last().speakerKey, QStringLiteral("B1"));
        // Single-track meeting: no exclusion.
        MeetingActivity micOnly = sc.act;
        micOnly.tracks.removeLast();
        QVERIFY(!analyzeReview(sc.in, micOnly).context.vetoActive);
    }

    // G10: the SideConflict suggestion is the other side's best non-excluded candidate; if there
    // is no known person there, a new speaker on that side.
    void sideVeto_sideConflictProposal()
    {
        Scenario sc = build({{"B1", 'L', 0}, {"B2", 'R', 1, true}, {"B1", 'L', 0}, {"B1", 'R', 1},
                             {"B2", 'R', 1, true}, {"B1", 'R', 1}, {"B1", 'L', 0}, {"B2", 'R', 1, true}});
        sc.in.meeting.speakerMap = {{"B1", "Ádám"}, {"B2", "Béla"}};
        sc.in.overlay.speakerTracks.insert("B1", {"mic"});
        sc.in.overlay.speakerTracks.insert("B2", {"loop"});
        ReviewResult r = analyzeReview(sc.in, sc.act);
        QVERIFY(r.context.vetoActive);
        const ReviewGroup* g = group(r.groups, ReviewKind::SideConflict);
        QVERIFY(g);
        QCOMPARE(g->proposedSpeakerKey, QStringLiteral("B2"));
        QVERIFY(!g->proposeNew);

        // Without Béla: no known person on the call voice, so "new remote speaker".
        Scenario solo = build({{"B1", 'L', 0}, {"B1", 'L', 0}, {"B1", 'R', 1}, {"B1", 'R', 1}, {"B1", 'L', 0}});
        solo.in.meeting.speakerMap = {{"B1", "Ádám"}};
        solo.in.overlay.speakerTracks.insert("B1", {"mic"});
        r = analyzeReview(solo.in, solo.act);
        g = group(r.groups, ReviewKind::SideConflict);
        QVERIFY(g);
        QVERIFY(g->proposedSpeakerKey.isEmpty() && g->proposedPersonName.isEmpty());
        QVERIFY(g->proposeNew);
        QCOMPARE(g->proposedSide, Side::Remote);
        QVERIFY(g->id.endsWith(QStringLiteral("new:remote")));
        QVERIFY(g->subtitle.contains(QStringLiteral("új távoli beszélő")));
        QCOMPARE(g->utteranceIds.size(), 2);

        // Without a manual assignment (only the own name) the old behavior stays: no suggestion.
        solo.in.overlay.speakerTracks.clear();
        solo.in.userName = "Ádám";
        r = analyzeReview(solo.in, solo.act);
        g = group(r.groups, ReviewKind::SideConflict);
        QVERIFY(g && !g->proposeNew);
    }

    // G10: the core mismatch does not suggest an excluded person either.
    void sideVeto_coreMismatchNeverProposesVetoed()
    {
        Scenario sc = build({{"A", 'L', 0, true}, {"A", 'L', 0, true}, {"A", 'L', 0, true}, {"B", 'R', 1, true},
                             {"B", 'R', 1, true}, {"B", 'R', 1, true}, {"A", 'L', 1}, {"A", 'L', 0}, {"A", 'L', 1}});
        sc.in.meeting.speakerMap = {{"A", "Anna"}, {"B", "Béla"}};
        ReviewResult r = analyzeReview(sc.in, sc.act);
        const ReviewGroup* g = group(r.groups, ReviewKind::CoreMismatch);
        QVERIFY(g && g->proposedSpeakerKey == QStringLiteral("B"));   // without an assignment: suggests it
        sc.in.overlay.speakerTracks.insert("A", {"mic"});
        sc.in.overlay.speakerTracks.insert("B", {"loop"});
        r = analyzeReview(sc.in, sc.act);
        QVERIFY(r.context.vetoActive);
        QVERIFY(!group(r.groups, ReviewKind::CoreMismatch));
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
