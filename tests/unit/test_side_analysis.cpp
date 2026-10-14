// SideAnalysis: track-side inference on synthetic activity (no audio, no I/O): localShare,
// the adaptive (2-means) threshold, the person's side (default from own name / track-bound
// speaker and its override by confirmed lines), the contradictions, the per-raw-label
// breakdown, and the single-track meeting (everything Unknown).
#include <QtTest>

#include <cmath>
#include <limits>

#include "tanara/editing/review/SideAnalysis.h"

using namespace tanara;

namespace {

// The tests' people: the case-folded name stands in as the PersonId (so "ádám" is the same person).
PersonId P(const QString& name) { return PersonId::fromString(name.toCaseFolded()); }
SelfPerson me(const QString& name) { return SelfPerson{P(name), name}; }

}

namespace {

struct Span { qint64 s, e; float db; };

// One track's activity: 0..totalMs, the given dB above the floor on the given segments, 0 elsewhere.
TrackActivity activity(const QString& id, TrackKind kind, qint64 totalMs, const QVector<Span>& spans,
                       qint64 originMs = 0)
{
    TrackActivity t;
    t.trackId = id;
    t.kind = kind;
    t.frameMs = 50;
    t.originMs = originMs;
    t.dbAboveFloor = QVector<float>(int((totalMs - originMs) / 50), 0.0f);
    for (const Span& sp : spans)
        for (qint64 ms = sp.s; ms < sp.e; ms += 50) {
            const qint64 i = (ms - originMs) / 50;
            if (i >= 0 && i < t.dbAboveFloor.size()) t.dbAboveFloor[i] = sp.db;
        }
    return t;
}

TranscriptLine line(const QString& id, qint64 s, qint64 e, const QString& raw)
{
    return TranscriptLine{id, s, e, raw, QStringLiteral("x")};
}

// Synthetic meeting: 2 s lines every 2.5 s. kinds[i] == 'L': under the line only the
// microphone plays (localDb), 'R': only the loopback (remoteDb; bleedDb crosstalk on the mic).
struct Synth {
    QVector<TranscriptLine> lines;
    MeetingActivity act;
};

Synth synth(const QString& kinds, const QStringList& labels, float localDb = 30.0f, float remoteDb = 30.0f,
            float bleedDb = 0.0f)
{
    Synth s;
    QVector<Span> mic, loop;
    for (int i = 0; i < kinds.size(); ++i) {
        const qint64 st = i * 2500, en = st + 2000;
        s.lines.append(line(QStringLiteral("u%1").arg(st), st, en, labels.value(i, labels.last())));
        if (kinds[i] == 'L') mic.append({st, en, localDb});
        else { loop.append({st, en, remoteDb}); if (bleedDb > 0) mic.append({st, en, bleedDb}); }
    }
    const qint64 total = kinds.size() * 2500 + 1000;
    s.act.tracks = {activity("mic", TrackKind::Mic, total, mic), activity("loop", TrackKind::Loopback, total, loop)};
    return s;
}

void confirm(SpeakerOverlay& ov, const QString& id, const QString& speaker = {})
{
    ov.utterances[id].confirmed = true;
    if (!speaker.isEmpty()) { ov.utterances[id].speaker = speaker; ov.utterances[id].corrected = true; }
}

const editing::PersonSide* person(const editing::SideReport& r, const QString& key)
{
    for (const editing::PersonSide& p : r.persons)
        if (p.speakerKey == key) return &p;
    return nullptr;
}

} // namespace

class SideAnalysisTest : public QObject {
    Q_OBJECT
private slots:
    void localShareFromDb()
    {
        QCOMPARE(editing::sides::localShareFromDb(20.0f, 0.0f), 1.0f);
        QCOMPARE(editing::sides::localShareFromDb(0.0f, 20.0f), 0.0f);
        QVERIFY(std::abs(editing::sides::localShareFromDb(20.0f, 20.0f) - 0.5f) < 1e-6f);
        // The share of EXCESS power above the floor: 20 dB (99) vs 10 dB (9) -> 0.917.
        QVERIFY(std::abs(editing::sides::localShareFromDb(20.0f, 10.0f) - 99.0f / 108.0f) < 1e-4f);
        QVERIFY(std::isnan(editing::sides::localShareFromDb(0.0f, 0.0f)));
    }

    void classifyWithDefaults()
    {
        const editing::sides::Thresholds th;
        QCOMPARE(editing::sides::classifyShare(0.95f, th), editing::Side::Local);
        QCOMPARE(editing::sides::classifyShare(0.70f, th), editing::Side::Local);
        QCOMPARE(editing::sides::classifyShare(0.50f, th), editing::Side::Mixed);
        QCOMPARE(editing::sides::classifyShare(0.30f, th), editing::Side::Remote);
        QCOMPARE(editing::sides::classifyShare(0.02f, th), editing::Side::Remote);
        QCOMPARE(editing::sides::classifyShare(qQNaN(), th), editing::Side::Unknown);
    }

    void adaptiveThresholdBimodal()
    {
        // A clean bimodal distribution around 0.05 and 0.95: the Mixed band is the middle half between the two centers.
        QVector<float> v;
        for (int i = 0; i < 10; ++i) v << 0.04f + 0.002f * i << 0.94f + 0.002f * i;
        const editing::sides::Thresholds th = editing::sides::adaptiveThresholds(v);
        QVERIFY(th.adaptive);
        QVERIFY(std::abs(th.centerRemote - 0.049f) < 0.01f);
        QVERIFY(std::abs(th.centerLocal - 0.949f) < 0.01f);
        QVERIFY(std::abs(th.loRemote - 0.274f) < 0.01f);
        QVERIFY(std::abs(th.hiLocal - 0.724f) < 0.01f);
    }

    void adaptiveThresholdFollowsBleed()
    {
        // Mic crosstalk: the remote lines around 0.35, the local ones around 0.97. The default 0.3
        // threshold would see the remote lines as Mixed; the adaptive one puts the boundary between the two peaks.
        QVector<float> v;
        for (int i = 0; i < 12; ++i) v << 0.33f + 0.004f * i;
        for (int i = 0; i < 8; ++i) v << 0.96f + 0.003f * i;
        const editing::sides::Thresholds th = editing::sides::adaptiveThresholds(v);
        QVERIFY(th.adaptive);
        QVERIFY(th.loRemote > 0.45f && th.loRemote < 0.55f);
        QVERIFY(th.hiLocal > 0.75f && th.hiLocal < 0.85f);
        QCOMPARE(editing::sides::classifyShare(0.35f, th), editing::Side::Remote);
        QCOMPARE(editing::sides::classifyShare(0.35f, editing::sides::Thresholds{}), editing::Side::Mixed);
        QCOMPARE(editing::sides::classifyShare(0.97f, th), editing::Side::Local);
        QCOMPARE(editing::sides::classifyShare(0.65f, th), editing::Side::Mixed);
    }

    void adaptiveThresholdFallsBack()
    {
        // Few lines.
        QVERIFY(!editing::sides::adaptiveThresholds({0.0f, 0.0f, 1.0f, 1.0f, 1.0f}).adaptive);
        // Unimodal (everyone remote).
        QVector<float> uni;
        for (int i = 0; i < 30; ++i) uni << 0.01f * (i % 7);
        const editing::sides::Thresholds u = editing::sides::adaptiveThresholds(uni);
        QVERIFY(!u.adaptive);
        QCOMPARE(u.loRemote, editing::sides::kDefaultLoRemote);
        QCOMPARE(u.hiLocal, editing::sides::kDefaultHiLocal);
        // One group is too small (2 outliers out of 40).
        QVector<float> small;
        for (int i = 0; i < 38; ++i) small << 0.02f;
        small << 0.98f << 0.99f;
        QVERIFY(!editing::sides::adaptiveThresholds(small).adaptive);
        // Two peaks, but both on the local side (0.75 / 0.99): 0.5 is not between them.
        QVector<float> local;
        for (int i = 0; i < 10; ++i) local << 0.75f << 0.99f;
        QVERIFY(!editing::sides::adaptiveThresholds(local).adaptive);
        // NaNs do not count.
        QVERIFY(!editing::sides::adaptiveThresholds(QVector<float>(20, qQNaN())).adaptive);
    }

    void singleTrackMeetingIsInactive()
    {
        Synth s = synth("LLRRLR", {"Beszélő 1"});
        Meeting m;
        m.speakerMap["Beszélő 1"] = P("Ádám");
        SpeakerOverlay ov;
        for (const auto& l : s.lines) confirm(ov, l.id);
        for (TrackKind keep : {TrackKind::Mic, TrackKind::Loopback}) {
            MeetingActivity only;
            for (const auto& t : s.act.tracks)
                if (t.kind == keep) only.tracks << t;
            const editing::SideReport r = editing::analyzeSides(m, s.lines, ov, only, me("Ádám"));
            QVERIFY(!r.active);
            QCOMPARE(r.totals.unknown, 6);
            QVERIFY(r.conflicts.isEmpty());
            QCOMPARE(r.persons.size(), 1);
            QCOMPARE(r.persons[0].side, editing::Side::Unknown);
            for (int h : r.histogram) QCOMPARE(h, 0);
        }
        // Empty activity (no track) likewise.
        const editing::SideReport r = editing::analyzeSides(m, s.lines, ov, MeetingActivity{}, me("Ádám"));
        QVERIFY(!r.active);
        QCOMPARE(r.totals.unknown, 6);
    }

    void mixedRawLabelAgainstUserDefault()
    {
        // "Speaker 1" is the user (speakerMap to the own person), but there are remote lines under it
        // too: the case of the long test meeting. No confirmed line: the default (Local) decides.
        Synth s = synth("LLRLRRLL", {"Beszélő 1", "Beszélő 1", "Beszélő 1", "Beszélő 1",
                                      "Beszélő 1", "Beszélő 1", "Beszélő 1", "Beszélő 2"});
        Meeting m;
        m.speakerMap["Beszélő 1"] = P("Ádám");
        const editing::SideReport r = editing::analyzeSides(m, s.lines, SpeakerOverlay{}, s.act, me("ádám"));
        QVERIFY(r.active);
        QCOMPARE(r.totals.local, 5);
        QCOMPARE(r.totals.remote, 3);
        const editing::SideCounts b1 = r.rawLabels.value("Beszélő 1");
        QCOMPARE(b1.local, 4);
        QCOMPARE(b1.remote, 3);
        const editing::PersonSide* p = person(r, "Beszélő 1");
        QVERIFY(p);
        QCOMPARE(p->side, editing::Side::Local);
        QCOMPARE(p->basis, QStringLiteral("user-name"));
        QCOMPARE(p->confidence, editing::sides::kDefaultConfidence);
        QCOMPARE(p->allLines.remote, 3);
        QCOMPARE(r.conflicts.size(), 3);
        for (const editing::SideConflict& c : r.conflicts) {
            QCOMPARE(c.speakerKey, QStringLiteral("Beszélő 1"));
            QCOMPARE(c.lineSide, editing::Side::Remote);
            QCOMPARE(c.personSide, editing::Side::Local);
            QVERIFY(c.localShare < 0.01f);
        }
        QCOMPARE(r.conflicts[0].utteranceId, QStringLiteral("u5000"));   // in chronological order
        // The anonymous "Speaker 2" has neither a default nor a confirmed line.
        QCOMPARE(person(r, "Beszélő 2")->side, editing::Side::Unknown);
        QCOMPARE(r.histogram[0], 3);
        QCOMPARE(r.histogram[9], 5);
    }

    void confirmedLinesDecidePersonSide()
    {
        // "Speaker 2" is anonymous: 3 confirmed remote lines give Remote ("lines"); its 4th
        // (unconfirmed) local line is a contradiction.
        Synth s = synth("RRRLL", {"Beszélő 2", "Beszélő 2", "Beszélő 2", "Beszélő 2", "Beszélő 1"});
        SpeakerOverlay ov;
        confirm(ov, "u0");
        confirm(ov, "u2500");
        confirm(ov, "u5000");
        const editing::SideReport r = editing::analyzeSides(Meeting{}, s.lines, ov, s.act, me("Ádám"));
        const editing::PersonSide* p = person(r, "Beszélő 2");
        QCOMPARE(p->side, editing::Side::Remote);
        QCOMPARE(p->basis, QStringLiteral("lines"));
        QCOMPARE(p->remoteLines, 3);
        QCOMPARE(p->confidence, 1.0f);
        QCOMPARE(r.conflicts.size(), 1);
        QCOMPARE(r.conflicts[0].utteranceId, QStringLiteral("u7500"));
    }

    void confirmedLinesOverrideUserNameDefault()
    {
        // "Speaker 2" was (wrongly) bound to the user, but its 4 confirmed lines played on the
        // loopback, so the data overrides the own-name default (Remote), its local line is a contradiction.
        Synth s = synth("RRRRL", {"Beszélő 2"});
        Meeting m;
        m.speakerMap["Beszélő 2"] = P("Ádám");
        SpeakerOverlay ov;
        for (const char* id : {"u0", "u2500", "u5000", "u7500"}) confirm(ov, id);
        editing::SideReport r = editing::analyzeSides(m, s.lines, ov, s.act, me("Ádám"));
        const editing::PersonSide* p = person(r, "Beszélő 2");
        QCOMPARE(p->defaultSide, editing::Side::Local);
        QCOMPARE(p->side, editing::Side::Remote);
        QCOMPARE(p->basis, QStringLiteral("override"));
        QCOMPARE(p->confidence, 1.0f);
        QCOMPARE(r.conflicts.size(), 1);
        QCOMPARE(r.conflicts[0].lineSide, editing::Side::Local);

        // Only 2 contradicting confirmed lines: too few to override, so the default stays.
        SpeakerOverlay weak;
        confirm(weak, "u0");
        confirm(weak, "u2500");
        r = editing::analyzeSides(m, s.lines, weak, s.act, me("Ádám"));
        p = person(r, "Beszélő 2");
        QCOMPARE(p->side, editing::Side::Local);
        QCOMPARE(p->basis, QStringLiteral("user-name"));
        QCOMPARE(r.conflicts.size(), 4);

        // A noisy (overlapping speech) confirmed line does not count as evidence.
        SpeakerOverlay noisy = ov;
        noisy.utterances["u0"].noisy = true;
        noisy.utterances["u2500"].noisy = true;
        r = editing::analyzeSides(m, s.lines, noisy, s.act, me("Ádám"));
        QCOMPARE(person(r, "Beszélő 2")->side, editing::Side::Local);
    }

    void fixedTrackSpeakerDefault()
    {
        // A speaker bound to the loopback track ("Béla") is remote by default; the user's name is another.
        Synth s = synth("RRL", {"Távoli 1"});
        Meeting m;
        Track loop;
        loop.id = "loop";
        loop.kind = TrackKind::Loopback;
        loop.fixedSpeaker = true;
        loop.speakerLabel = "Béla";
        m.tracks = {loop};
        m.speakerMap["Távoli 1"] = P("Béla");
        editing::SideHints h;
        h.names.insert(P("Béla"), QStringLiteral("Béla"));   // the person's name matches the track's label
        const editing::SideReport r = editing::analyzeSides(m, s.lines, SpeakerOverlay{}, s.act, me("Ádám"), h);
        const editing::PersonSide* p = person(r, "Távoli 1");
        QCOMPARE(p->side, editing::Side::Remote);
        QCOMPARE(p->basis, QStringLiteral("fixed-track"));
        QCOMPARE(r.conflicts.size(), 1);
    }

    void micBleedStillSeparates()
    {
        // Under the remote lines there is a signal on the microphone too (speaker crosstalk): 26 dB
        // next to the loopback's 28 dB, so localShare is about 0.39. The OLD classification: with the
        // default threshold it would be Mixed; the adaptive one sees Remote. The NEW (speech-level
        // normalized): the mic's 26 dB is only 4 dB below the mic's speech level (30), so the mic
        // "speaks" and it is never Remote; the loopback is at speech level too, so Mixed (crosstalk
        // this strong looks like overlapping speech).
        Synth s = synth("LRLRRLRRLRLR", {"Beszélő 1"}, 30.0f, 28.0f, 26.0f);
        const editing::SideReport r = editing::analyzeSides(Meeting{}, s.lines, SpeakerOverlay{}, s.act, me("Ádám"));
        QVERIFY(r.thresholds.adaptive);
        QCOMPARE(r.legacyTotals.local, 5);
        QCOMPARE(r.legacyTotals.remote, 7);
        QCOMPARE(r.legacyTotals.mixed, 0);
        QCOMPARE(r.totals.local, 5);
        QCOMPARE(r.totals.remote, 0);
        QCOMPARE(r.totals.mixed, 7);
        for (const editing::LineSide& l : r.lines)
            if (l.legacySide == editing::Side::Remote) {
                QVERIFY(l.localShare > 0.35f && l.localShare < 0.45f);
                QVERIFY(std::abs(l.micDb - 26.0f) < 0.1f);
                QVERIFY(std::abs(l.loopDb - 28.0f) < 0.1f);
                QVERIFY(std::abs(l.micRelDb - (-4.0f)) < 0.1f);
                QCOMPARE(l.side, editing::Side::Mixed);
            }
    }

    void classifyLevelsRules()
    {
        const float inf = std::numeric_limits<float>::infinity();
        QCOMPARE(editing::sides::classifyLevels(0.0f, -inf), editing::Side::Local);          // purely local
        QCOMPARE(editing::sides::classifyLevels(-inf, 0.0f), editing::Side::Remote);         // clean remote
        QCOMPARE(editing::sides::classifyLevels(-25.0f, 0.0f), editing::Side::Remote);       // quiet mic noise
        QCOMPARE(editing::sides::classifyLevels(0.0f, -8.0f), editing::Side::Local);         // echo
        QCOMPARE(editing::sides::classifyLevels(0.0f, -2.0f), editing::Side::Mixed);         // overlapping speech
        QCOMPARE(editing::sides::classifyLevels(-9.0f, 0.0f), editing::Side::Mixed);         // the mic speaks: never Remote
        QCOMPARE(editing::sides::classifyLevels(-15.0f, 0.0f), editing::Side::Remote);       // an intermediate mic (crosstalk) + the loopback speaks: remote
        QCOMPARE(editing::sides::classifyLevels(-inf, -inf), editing::Side::Unknown);        // silence
        QCOMPARE(editing::sides::classifyLevels(qQNaN(), 0.0f), editing::Side::Unknown);     // no coverage
        QCOMPARE(editing::sides::classifyLevels(0.0f, -15.0f), editing::Side::Local);        // the loopback is quiet
    }

    void speechLevelIsActivePercentile()
    {
        // 100 active frames 1..100 dB, plus silence and a hole: the 90th percentile is about 90.
        TrackActivity t;
        t.trackId = "mic";
        t.kind = TrackKind::Mic;
        for (int i = 1; i <= 100; ++i) t.dbAboveFloor << float(i);
        t.dbAboveFloor << 0.0f << 0.0f << qQNaN();
        const float lvl = editing::sides::speechLevelDb(t);
        QVERIFY(lvl >= 88.0f && lvl <= 92.0f);
        TrackActivity silent;
        silent.dbAboveFloor = QVector<float>(20, 1.0f);
        QVERIFY(std::isnan(editing::sides::speechLevelDb(silent)));
    }

    void echoOnLoopbackIsLocal()
    {
        // The mic is quiet (15 dB), the user's voice comes back on the loopback as an echo (22 dB),
        // the real remote speech is 30 dB. The old ratio saw the local lines as Remote (0.16); the
        // new one sees the line at the mic's speech level (0 dB) as Local, and the echo (-8 dB) as that too.
        QVector<TranscriptLine> lines;
        QVector<Span> mic, loop;
        const QString kinds = "LLRLRRLRLL";
        for (int i = 0; i < kinds.size(); ++i) {
            const qint64 st = i * 2500, en = st + 2000;
            lines << line(QStringLiteral("u%1").arg(st), st, en, "B1");
            if (kinds[i] == 'L') { mic << Span{st, en, 15.0f}; loop << Span{st, en, 22.0f}; }
            else loop << Span{st, en, 30.0f};
        }
        MeetingActivity act;
        act.tracks = {activity("mic", TrackKind::Mic, 26000, mic), activity("loop", TrackKind::Loopback, 26000, loop)};
        const editing::SideReport r = editing::analyzeSides(Meeting{}, lines, SpeakerOverlay{}, act, {});
        QVERIFY(r.active);
        QCOMPARE(r.totals.local, 6);
        QCOMPARE(r.totals.remote, 4);
        QCOMPARE(r.legacyTotals.local, 0);   // the old metric's fault
        for (const editing::LineSide& l : r.lines)
            if (l.side == editing::Side::Local) QVERIFY(std::abs(l.loopRelDb - (-8.0f)) < 0.1f);
    }

    void overlapIsMixed()
    {
        // Both sides at speech level (the loopback at its own level): overlapping speech.
        QVector<TranscriptLine> lines{line("a", 0, 2000, "B1"), line("b", 2500, 4500, "B1"),
                                      line("c", 5000, 7000, "B1")};
        MeetingActivity act;
        act.tracks = {activity("mic", TrackKind::Mic, 8000, {{0, 2000, 30.0f}, {5000, 7000, 30.0f}}),
                      activity("loop", TrackKind::Loopback, 8000, {{2500, 4500, 30.0f}, {5000, 7000, 30.0f}})};
        const editing::SideReport r = editing::analyzeSides(Meeting{}, lines, SpeakerOverlay{}, act, {});
        QCOMPARE(r.lines[0].side, editing::Side::Local);
        QCOMPARE(r.lines[1].side, editing::Side::Remote);
        QCOMPARE(r.lines[2].side, editing::Side::Mixed);
    }

    void manualTracksDecidePersonSide()
    {
        // The user (Local by default) manually assigned to the loopback track gives Remote
        // ("manual"); their local lines are contradictions. The manual assignment can come from the
        // overlay and from the hints too.
        Synth s = synth("LLLR", {"Beszélő 1"});
        Meeting m;
        Track mic; mic.id = "mic"; mic.kind = TrackKind::Mic;
        Track loop; loop.id = "loop"; loop.kind = TrackKind::Loopback;
        m.tracks = {mic, loop};
        m.speakerMap["Beszélő 1"] = P("Ádám");
        SpeakerOverlay ov;
        ov.speakerTracks["Beszélő 1"] = QStringList{"loop"};
        editing::SideReport r = editing::analyzeSides(m, s.lines, ov, s.act, me("Ádám"));
        const editing::PersonSide* p = person(r, "Beszélő 1");
        QCOMPARE(p->side, editing::Side::Remote);
        QCOMPARE(p->basis, QStringLiteral("manual"));
        QCOMPARE(p->manualTracks, QStringList{"loop"});
        QCOMPARE(r.conflicts.size(), 3);
        QCOMPARE(editing::sides::learnedDefaults(r).value(P("Ádám")), editing::Side::Remote);

        editing::SideHints h;
        h.speakerTracks["Beszélő 1"] = QStringList{"mic", "loop"};
        r = analyzeSides(m, s.lines, ov, s.act, me("Ádám"), h);
        QCOMPARE(person(r, "Beszélő 1")->side, editing::Side::Mixed);
        QVERIFY(r.conflicts.isEmpty());
        QCOMPARE(editing::sides::sideOfTracks(m, {"nincs"}), editing::Side::Unknown);
    }

    void learnedSideBeatsUserName()
    {
        // The learned default (Remote) takes precedence over the own-name default; weak data does not override it.
        Synth s = synth("RRL", {"Beszélő 1"});
        Meeting m;
        m.speakerMap["Beszélő 1"] = P("Béla");
        editing::SideHints h;
        h.learnedSides.insert(P(QStringLiteral("béla")), editing::Side::Remote);
        const editing::SideReport r = analyzeSides(m, s.lines, SpeakerOverlay{}, s.act, me("Béla"), h);
        const editing::PersonSide* p = person(r, "Beszélő 1");
        QCOMPARE(p->side, editing::Side::Remote);
        QCOMPARE(p->basis, QStringLiteral("learned"));
        QCOMPARE(p->learnedSide, editing::Side::Remote);
        QCOMPARE(r.conflicts.size(), 1);
        // The learned default itself does not learn further (only from data / by hand).
        QVERIFY(editing::sides::learnedDefaults(r).isEmpty());
    }

    void silenceAndGapsAreUnknown()
    {
        // Line 1: both sides quiet; line 2: the mic track has a hole (NaN), so Unknown; line 3: local.
        QVector<TranscriptLine> lines{line("a", 0, 2000, "B1"), line("b", 2500, 4500, "B1"),
                                      line("c", 5000, 7000, "B1")};
        TrackActivity mic = activity("mic", TrackKind::Mic, 8000, {{5000, 7000, 30.0f}});
        for (qint64 ms = 2500; ms < 4500; ms += 50) mic.dbAboveFloor[ms / 50] = qQNaN();
        MeetingActivity act;
        act.tracks = {mic, activity("loop", TrackKind::Loopback, 8000, {{2500, 4500, 30.0f}})};
        const editing::SideReport r = editing::analyzeSides(Meeting{}, lines, SpeakerOverlay{}, act, {});
        QCOMPARE(r.lines[0].side, editing::Side::Unknown);
        QCOMPARE(r.lines[1].side, editing::Side::Unknown);
        QVERIFY(std::isnan(r.lines[1].micDb));
        QCOMPARE(r.lines[2].side, editing::Side::Local);
        QCOMPARE(r.totals.unknown, 2);
    }

    void severalTracksPerKindAreSummed()
    {
        // Two loopback tracks (call + system audio): together 3 dB more excess power.
        QVector<TranscriptLine> lines{line("a", 0, 2000, "B1")};
        MeetingActivity act;
        act.tracks = {activity("mic", TrackKind::Mic, 3000, {{0, 2000, 20.0f}}),
                      activity("l1", TrackKind::Loopback, 3000, {{0, 2000, 20.0f}}),
                      activity("l2", TrackKind::Loopback, 3000, {{0, 2000, 20.0f}})};
        const editing::SideReport r = editing::analyzeSides(Meeting{}, lines, SpeakerOverlay{}, act, {});
        QVERIFY(std::abs(r.lines[0].localShare - 1.0f / 3.0f) < 1e-3f);
        QCOMPARE(r.lines[0].side, editing::Side::Mixed);
    }

    void correctedSpeakerIsUsed()
    {
        // An overridden (manually reassigned) line counts for the target speaker, its raw label stays.
        Synth s = synth("LLLR", {"Beszélő 1"});
        SpeakerOverlay ov;
        confirm(ov, "u7500", "Beszélő 9");
        const editing::SideReport r = editing::analyzeSides(Meeting{}, s.lines, ov, s.act, {});
        QCOMPARE(r.lines[3].speakerKey, QStringLiteral("Beszélő 9"));
        QCOMPARE(r.lines[3].rawLabel, QStringLiteral("Beszélő 1"));
        QVERIFY(r.lines[3].locked);
        QCOMPARE(r.rawLabels.value("Beszélő 1").remote, 1);
        QCOMPARE(person(r, "Beszélő 9")->allLines.remote, 1);
    }
};

QTEST_GUILESS_MAIN(SideAnalysisTest)
#include "test_side_analysis.moc"
