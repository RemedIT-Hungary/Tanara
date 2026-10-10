//
// SideAnalysis — sáv-oldal következtetés szintetikus aktivitáson (nincs hang, nincs I/O):
// localShare, az adaptív (2-közép) küszöb, a személy-oldal (saját név / sávhoz kötött beszélő
// alapértelmezése és annak felülírása a megerősített sorokkal), az ellentmondások, a nyers
// címkénkénti bontás, és az egysávos meeting (minden Unknown).
//
#include <QtTest>

#include <cmath>

#include "tanara/edit/SideAnalysis.h"

using namespace tanara;

namespace {

struct Span { qint64 s, e; float db; };

// Egy sáv aktivitása: 0..totalMs, a megadott szakaszokon az adott dB a padló felett, máshol 0.
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

// Szintetikus megbeszélés: 2 mp-es sorok 2.5 mp-enként. kinds[i] == 'L' → a sor alatt csak
// a mikrofon szól (localDb), 'R' → csak a loopback (remoteDb; a mic-en bleedDb áthallás).
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

const PersonSide* person(const SideReport& r, const QString& key)
{
    for (const PersonSide& p : r.persons)
        if (p.speakerKey == key) return &p;
    return nullptr;
}

} // namespace

class SideAnalysisTest : public QObject {
    Q_OBJECT
private slots:
    void localShareFromDb()
    {
        QCOMPARE(sides::localShareFromDb(20.0f, 0.0f), 1.0f);
        QCOMPARE(sides::localShareFromDb(0.0f, 20.0f), 0.0f);
        QVERIFY(std::abs(sides::localShareFromDb(20.0f, 20.0f) - 0.5f) < 1e-6f);
        // A padló feletti TÖBBLET-teljesítmény aránya: 20 dB (99) vs 10 dB (9) → 0.917.
        QVERIFY(std::abs(sides::localShareFromDb(20.0f, 10.0f) - 99.0f / 108.0f) < 1e-4f);
        QVERIFY(std::isnan(sides::localShareFromDb(0.0f, 0.0f)));
    }

    void classifyWithDefaults()
    {
        const sides::Thresholds th;
        QCOMPARE(sides::classifyShare(0.95f, th), Side::Local);
        QCOMPARE(sides::classifyShare(0.70f, th), Side::Local);
        QCOMPARE(sides::classifyShare(0.50f, th), Side::Mixed);
        QCOMPARE(sides::classifyShare(0.30f, th), Side::Remote);
        QCOMPARE(sides::classifyShare(0.02f, th), Side::Remote);
        QCOMPARE(sides::classifyShare(qQNaN(), th), Side::Unknown);
    }

    void adaptiveThresholdBimodal()
    {
        // Tiszta kétcsúcsú eloszlás 0.05 és 0.95 körül → a Mixed-sáv a két közép közti középső fele.
        QVector<float> v;
        for (int i = 0; i < 10; ++i) v << 0.04f + 0.002f * i << 0.94f + 0.002f * i;
        const sides::Thresholds th = sides::adaptiveThresholds(v);
        QVERIFY(th.adaptive);
        QVERIFY(std::abs(th.centerRemote - 0.049f) < 0.01f);
        QVERIFY(std::abs(th.centerLocal - 0.949f) < 0.01f);
        QVERIFY(std::abs(th.loRemote - 0.274f) < 0.01f);
        QVERIFY(std::abs(th.hiLocal - 0.724f) < 0.01f);
    }

    void adaptiveThresholdFollowsBleed()
    {
        // Mic-áthallás: a távoli sorok 0.35 körül, a helyiek 0.97 körül. Az alap 0.3-as küszöb a
        // távoli sorokat Mixed-nek látná; az adaptív a két csúcs közé teszi a határt.
        QVector<float> v;
        for (int i = 0; i < 12; ++i) v << 0.33f + 0.004f * i;
        for (int i = 0; i < 8; ++i) v << 0.96f + 0.003f * i;
        const sides::Thresholds th = sides::adaptiveThresholds(v);
        QVERIFY(th.adaptive);
        QVERIFY(th.loRemote > 0.45f && th.loRemote < 0.55f);
        QVERIFY(th.hiLocal > 0.75f && th.hiLocal < 0.85f);
        QCOMPARE(sides::classifyShare(0.35f, th), Side::Remote);
        QCOMPARE(sides::classifyShare(0.35f, sides::Thresholds{}), Side::Mixed);
        QCOMPARE(sides::classifyShare(0.97f, th), Side::Local);
        QCOMPARE(sides::classifyShare(0.65f, th), Side::Mixed);
    }

    void adaptiveThresholdFallsBack()
    {
        // Kevés sor.
        QVERIFY(!sides::adaptiveThresholds({0.0f, 0.0f, 1.0f, 1.0f, 1.0f}).adaptive);
        // Egycsúcsú (mindenki távoli).
        QVector<float> uni;
        for (int i = 0; i < 30; ++i) uni << 0.01f * (i % 7);
        const sides::Thresholds u = sides::adaptiveThresholds(uni);
        QVERIFY(!u.adaptive);
        QCOMPARE(u.loRemote, sides::kDefaultLoRemote);
        QCOMPARE(u.hiLocal, sides::kDefaultHiLocal);
        // Egy csoport túl kicsi (2 kiugró 40 közül).
        QVector<float> small;
        for (int i = 0; i < 38; ++i) small << 0.02f;
        small << 0.98f << 0.99f;
        QVERIFY(!sides::adaptiveThresholds(small).adaptive);
        // Két csúcs, de mindkettő a helyi oldalon (0.75 / 0.99) — a 0.5 nincs köztük.
        QVector<float> local;
        for (int i = 0; i < 10; ++i) local << 0.75f << 0.99f;
        QVERIFY(!sides::adaptiveThresholds(local).adaptive);
        // NaN-ok nem számítanak.
        QVERIFY(!sides::adaptiveThresholds(QVector<float>(20, qQNaN())).adaptive);
    }

    void singleTrackMeetingIsInactive()
    {
        Synth s = synth("LLRRLR", {"Beszélő 1"});
        Meeting m;
        m.speakerMap["Beszélő 1"] = "Ádám";
        SpeakerOverlay ov;
        for (const auto& l : s.lines) confirm(ov, l.id);
        for (TrackKind keep : {TrackKind::Mic, TrackKind::Loopback}) {
            MeetingActivity only;
            for (const auto& t : s.act.tracks)
                if (t.kind == keep) only.tracks << t;
            const SideReport r = analyzeSides(m, s.lines, ov, only, "Ádám");
            QVERIFY(!r.active);
            QCOMPARE(r.totals.unknown, 6);
            QVERIFY(r.conflicts.isEmpty());
            QCOMPARE(r.persons.size(), 1);
            QCOMPARE(r.persons[0].side, Side::Unknown);
            for (int h : r.histogram) QCOMPARE(h, 0);
        }
        // Üres aktivitás (nincs sáv) ugyanígy.
        const SideReport r = analyzeSides(m, s.lines, ov, MeetingActivity{}, "Ádám");
        QVERIFY(!r.active);
        QCOMPARE(r.totals.unknown, 6);
    }

    void mixedRawLabelAgainstUserDefault()
    {
        // A „Beszélő 1” a felhasználó (speakerMap → Ádám), de alatta távoli sorok is vannak — a
        // hosszú tesztmeeting esete. Nincs megerősített sor: az alapértelmezés (Local) dönt.
        Synth s = synth("LLRLRRLL", {"Beszélő 1", "Beszélő 1", "Beszélő 1", "Beszélő 1",
                                      "Beszélő 1", "Beszélő 1", "Beszélő 1", "Beszélő 2"});
        Meeting m;
        m.speakerMap["Beszélő 1"] = "Ádám";
        const SideReport r = analyzeSides(m, s.lines, SpeakerOverlay{}, s.act, "ádám");
        QVERIFY(r.active);
        QCOMPARE(r.totals.local, 5);
        QCOMPARE(r.totals.remote, 3);
        const SideCounts b1 = r.rawLabels.value("Beszélő 1");
        QCOMPARE(b1.local, 4);
        QCOMPARE(b1.remote, 3);
        const PersonSide* p = person(r, "Beszélő 1");
        QVERIFY(p);
        QCOMPARE(p->side, Side::Local);
        QCOMPARE(p->basis, QStringLiteral("user-name"));
        QCOMPARE(p->confidence, sides::kDefaultConfidence);
        QCOMPARE(p->allLines.remote, 3);
        QCOMPARE(r.conflicts.size(), 3);
        for (const SideConflict& c : r.conflicts) {
            QCOMPARE(c.speakerKey, QStringLiteral("Beszélő 1"));
            QCOMPARE(c.lineSide, Side::Remote);
            QCOMPARE(c.personSide, Side::Local);
            QVERIFY(c.localShare < 0.01f);
        }
        QCOMPARE(r.conflicts[0].utteranceId, QStringLiteral("u5000"));   // időrendben
        // A névtelen „Beszélő 2”-nek nincs se alapértelmezése, se megerősített sora.
        QCOMPARE(person(r, "Beszélő 2")->side, Side::Unknown);
        QCOMPARE(r.histogram[0], 3);
        QCOMPARE(r.histogram[9], 5);
    }

    void confirmedLinesDecidePersonSide()
    {
        // „Beszélő 2” névtelen: 3 megerősített távoli sor → Remote ("lines"); a 4. (nem megerősített)
        // helyi sora ellentmondás.
        Synth s = synth("RRRLL", {"Beszélő 2", "Beszélő 2", "Beszélő 2", "Beszélő 2", "Beszélő 1"});
        SpeakerOverlay ov;
        confirm(ov, "u0");
        confirm(ov, "u2500");
        confirm(ov, "u5000");
        const SideReport r = analyzeSides(Meeting{}, s.lines, ov, s.act, "Ádám");
        const PersonSide* p = person(r, "Beszélő 2");
        QCOMPARE(p->side, Side::Remote);
        QCOMPARE(p->basis, QStringLiteral("lines"));
        QCOMPARE(p->remoteLines, 3);
        QCOMPARE(p->confidence, 1.0f);
        QCOMPARE(r.conflicts.size(), 1);
        QCOMPARE(r.conflicts[0].utteranceId, QStringLiteral("u7500"));
    }

    void confirmedLinesOverrideUserNameDefault()
    {
        // A „Beszélő 2”-t (tévesen) a felhasználóhoz kötötték, de 4 megerősített sora a loopbackon
        // szólt → az adat felülírja a saját-név alapértelmezést (Remote), a helyi sora ellentmondás.
        Synth s = synth("RRRRL", {"Beszélő 2"});
        Meeting m;
        m.speakerMap["Beszélő 2"] = "Ádám";
        SpeakerOverlay ov;
        for (const char* id : {"u0", "u2500", "u5000", "u7500"}) confirm(ov, id);
        SideReport r = analyzeSides(m, s.lines, ov, s.act, "Ádám");
        const PersonSide* p = person(r, "Beszélő 2");
        QCOMPARE(p->defaultSide, Side::Local);
        QCOMPARE(p->side, Side::Remote);
        QCOMPARE(p->basis, QStringLiteral("override"));
        QCOMPARE(p->confidence, 1.0f);
        QCOMPARE(r.conflicts.size(), 1);
        QCOMPARE(r.conflicts[0].lineSide, Side::Local);

        // Csak 2 ellentmondó megerősített sor: kevés a felülíráshoz → marad az alapértelmezés.
        SpeakerOverlay weak;
        confirm(weak, "u0");
        confirm(weak, "u2500");
        r = analyzeSides(m, s.lines, weak, s.act, "Ádám");
        p = person(r, "Beszélő 2");
        QCOMPARE(p->side, Side::Local);
        QCOMPARE(p->basis, QStringLiteral("user-name"));
        QCOMPARE(r.conflicts.size(), 4);

        // Zajos (egymásra beszélős) megerősített sor nem számít bizonyítéknak.
        SpeakerOverlay noisy = ov;
        noisy.utterances["u0"].noisy = true;
        noisy.utterances["u2500"].noisy = true;
        r = analyzeSides(m, s.lines, noisy, s.act, "Ádám");
        QCOMPARE(person(r, "Beszélő 2")->side, Side::Local);
    }

    void fixedTrackSpeakerDefault()
    {
        // A loopback-sávhoz kötött beszélő („Béla”) alapból távoli; a felhasználó neve más.
        Synth s = synth("RRL", {"Távoli 1"});
        Meeting m;
        Track loop;
        loop.id = "loop";
        loop.kind = TrackKind::Loopback;
        loop.fixedSpeaker = true;
        loop.speakerLabel = "Béla";
        m.tracks = {loop};
        m.speakerMap["Távoli 1"] = "Béla";
        const SideReport r = analyzeSides(m, s.lines, SpeakerOverlay{}, s.act, "Ádám");
        const PersonSide* p = person(r, "Távoli 1");
        QCOMPARE(p->side, Side::Remote);
        QCOMPARE(p->basis, QStringLiteral("fixed-track"));
        QCOMPARE(r.conflicts.size(), 1);
    }

    void micBleedStillSeparates()
    {
        // A távoli sorok alatt a mikrofonon is van jel (hangszóró-áthallás): 26 dB a loopback 28 dB-je
        // mellett → localShare ≈ 0.39. Az alap küszöbbel Mixed lenne; az adaptív Remote-nak látja.
        Synth s = synth("LRLRRLRRLRLR", {"Beszélő 1"}, 30.0f, 28.0f, 26.0f);
        const SideReport r = analyzeSides(Meeting{}, s.lines, SpeakerOverlay{}, s.act, "Ádám");
        QVERIFY(r.thresholds.adaptive);
        QCOMPARE(r.totals.local, 5);
        QCOMPARE(r.totals.remote, 7);
        QCOMPARE(r.totals.mixed, 0);
        for (const LineSide& l : r.lines)
            if (l.side == Side::Remote) {
                QVERIFY(l.localShare > 0.35f && l.localShare < 0.45f);
                QVERIFY(std::abs(l.micDb - 26.0f) < 0.1f);
                QVERIFY(std::abs(l.loopDb - 28.0f) < 0.1f);
            }
    }

    void silenceAndGapsAreUnknown()
    {
        // 1. sor: mindkét oldal csendes; 2. sor: a mic-sáv lyukas (NaN) → Unknown; 3.: helyi.
        QVector<TranscriptLine> lines{line("a", 0, 2000, "B1"), line("b", 2500, 4500, "B1"),
                                      line("c", 5000, 7000, "B1")};
        TrackActivity mic = activity("mic", TrackKind::Mic, 8000, {{5000, 7000, 30.0f}});
        for (qint64 ms = 2500; ms < 4500; ms += 50) mic.dbAboveFloor[ms / 50] = qQNaN();
        MeetingActivity act;
        act.tracks = {mic, activity("loop", TrackKind::Loopback, 8000, {{2500, 4500, 30.0f}})};
        const SideReport r = analyzeSides(Meeting{}, lines, SpeakerOverlay{}, act, {});
        QCOMPARE(r.lines[0].side, Side::Unknown);
        QCOMPARE(r.lines[1].side, Side::Unknown);
        QVERIFY(std::isnan(r.lines[1].micDb));
        QCOMPARE(r.lines[2].side, Side::Local);
        QCOMPARE(r.totals.unknown, 2);
    }

    void severalTracksPerKindAreSummed()
    {
        // Két loopback-sáv (hívás + rendszerhang): együtt 3 dB-lel több a többlet-teljesítmény.
        QVector<TranscriptLine> lines{line("a", 0, 2000, "B1")};
        MeetingActivity act;
        act.tracks = {activity("mic", TrackKind::Mic, 3000, {{0, 2000, 20.0f}}),
                      activity("l1", TrackKind::Loopback, 3000, {{0, 2000, 20.0f}}),
                      activity("l2", TrackKind::Loopback, 3000, {{0, 2000, 20.0f}})};
        const SideReport r = analyzeSides(Meeting{}, lines, SpeakerOverlay{}, act, {});
        QVERIFY(std::abs(r.lines[0].localShare - 1.0f / 3.0f) < 1e-3f);
        QCOMPARE(r.lines[0].side, Side::Mixed);
    }

    void correctedSpeakerIsUsed()
    {
        // A felülírt (kézzel átsorolt) sor a cél-beszélőhöz számít, a nyers címkéje marad.
        Synth s = synth("LLLR", {"Beszélő 1"});
        SpeakerOverlay ov;
        confirm(ov, "u7500", "Beszélő 9");
        const SideReport r = analyzeSides(Meeting{}, s.lines, ov, s.act, {});
        QCOMPARE(r.lines[3].speakerKey, QStringLiteral("Beszélő 9"));
        QCOMPARE(r.lines[3].rawLabel, QStringLiteral("Beszélő 1"));
        QVERIFY(r.lines[3].locked);
        QCOMPARE(r.rawLabels.value("Beszélő 1").remote, 1);
        QCOMPARE(person(r, "Beszélő 9")->allLines.remote, 1);
    }
};

QTEST_GUILESS_MAIN(SideAnalysisTest)
#include "test_side_analysis.moc"
