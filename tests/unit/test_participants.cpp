//
// Résztvevők („Ki volt ott?") — a tiszta logika: sáv-beszédarány és kizárás a lekeverésből,
// hang-ablakok, klaszterezés hamis embedderrel és hamis hanggal (két klaszter → két jelölt, egyik
// lenyomattal párosítva), bizonyítékok és csoportok, kötés a nyers beszélőkhöz időátfedéssel
// (kétoldalú nyers beszélő kettéosztva), a jóváhagyás kötései, és a meeting.json oda-vissza.
// I/O és ffmpeg nélkül.
//
#include <QtTest>
#include <QTemporaryDir>

#include "tanara/audio/MixdownPlan.h"
#include "tanara/edit/ParticipantAnalysis.h"
#include "tanara/edit/TrackSpeech.h"
#include "tanara/store/JsonSerialization.h"
#include "tanara/store/VoiceprintStore.h"
#include "tanara/voiceid/VoiceEmbedderSet.h"
#include "tanara/voiceid/VoiceModelRegistry.h"

#include <cmath>

using namespace tanara;

namespace {

// Keretenként: beszéd (40 dB a padló felett, padló -80 dBFS) a megadott [s,e) mp-szakaszokban.
TrackActivity activity(const QString& id, TrackKind kind, int seconds, const QVector<QPair<int, int>>& speech)
{
    TrackActivity t;
    t.trackId = id;
    t.kind = kind;
    t.frameMs = 50;
    t.floorDb = -80.0f;
    t.dbAboveFloor.fill(0.0f, seconds * 20);
    for (const auto& s : speech)
        for (int f = s.first * 20; f < s.second * 20 && f < t.dbAboveFloor.size(); ++f) t.dbAboveFloor[f] = 40.0f;
    return t;
}

Track track(const QString& id, TrackKind kind, const QString& file)
{
    Track t;
    t.id = id;
    t.deviceName = id;
    t.kind = kind;
    t.file = file;
    return t;
}

// Hamis „modell": a PCM első mintája választja ki a forró dimenziót (a hamis hang ezt kódolja).
PcmEmbedderLoader fakeLoader()
{
    return [](const VoiceModelSpec& spec, const QString&, QString*) -> PcmEmbedder {
        const int dim = spec.dim > 0 ? spec.dim : 8;
        return [dim](const QVector<float>& pcm) {
            if (pcm.isEmpty()) return QVector<float>();
            QVector<float> v(dim, 0.0f);
            v[std::clamp(int(pcm.first()), 0, dim - 1)] = 1.0f;
            return v;
        };
    };
}

QVector<float> oneHot(int dim, int hot)
{
    QVector<float> v(dim, 0.0f);
    v[hot] = 1.0f;
    return v;
}

TranscriptLine line(const QString& raw, qint64 s, qint64 e)
{
    TranscriptLine l;
    l.id = QStringLiteral("u%1").arg(s);
    l.rawLabel = raw;
    l.startMs = s;
    l.endMs = e;
    return l;
}

} // namespace

class ParticipantsTest : public QObject {
    Q_OBJECT
private slots:
    void speechRatioAndExclusion();
    void allSilentKeepsEverything();
    void speechWindowsSplitAndMerge();
    void analysisTwoClustersOneMatched();
    void manualParticipantBoostAndEvidence();
    void bindRawSpeakersByOverlap();
    void twoSidedRawSpeakerIsSplit();
    void meetingJsonRoundTrip();
};

void ParticipantsTest::speechRatioAndExclusion()
{
    // 20 mp: a mikrofonon 10 mp beszéd, a loopbackon semmi, a harmadikon 0,2 mp (1%) kattanás.
    const TrackActivity mic = activity("mic", TrackKind::Mic, 20, {{0, 10}});
    const TrackActivity loop = activity("loop", TrackKind::Loopback, 20, {});
    TrackActivity other = activity("other", TrackKind::Loopback, 20, {});
    for (int f = 0; f < 4; ++f) other.dbAboveFloor[f] = 40.0f;
    QCOMPARE(trackspeech::speechRatio(mic), 0.5);
    QCOMPARE(trackspeech::speechRatio(loop), 0.0);
    QCOMPARE(trackspeech::speechRatio(other), 0.01);
    // Az abszolút szint-kapu: a digitális csend (-90) feletti 40 dB is csak -50 dBFS → beszéd;
    // de -100 dBFS-es padló felett 40 dB = -60 dBFS → nem.
    TrackActivity quiet = mic;
    quiet.floorDb = -100.0f;
    QCOMPARE(trackspeech::speechRatio(quiet), 0.0);

    Meeting m;
    m.folder = QStringLiteral("/nonexistent");
    m.tracks = {track("mic", TrackKind::Mic, "a.ogg"), track("loop", TrackKind::Loopback, "b.ogg"),
                track("other", TrackKind::Loopback, "c.ogg")};
    QVERIFY(trackspeech::needsSpeechCheck(m));
    MeetingActivity act;
    act.tracks = {mic, loop, other};
    const trackspeech::SpeechCheckResult r = trackspeech::applySpeechRatios(m, act);
    QCOMPARE(r.measured, QStringList({"mic", "loop", "other"}));
    QCOMPARE(r.excluded, QStringList({"loop", "other"}));
    QCOMPARE(m.tracks[0].speechRatio, 0.5);
    QVERIFY(m.tracks[0].included());
    QCOMPARE(m.tracks[1].excludedReason, trackspeech::kNoSpeech);
    QVERIFY(!m.tracks[1].active);
    QVERIFY(!trackspeech::needsSpeechCheck(m));

    // A lekeverés-terv a kimaradt sávot nem veszi fel (a fájlok nem léteznek → mind „hiányzik",
    // de csak a bevont sáv neve kerül a listára).
    const MixdownPlan plan = MixdownPlan::fromMeeting(m);
    QCOMPARE(plan.missing.size(), 1);

    // A kézzel visszavett (mért) sávot egy újabb mérés nem bántja.
    m.tracks[1].active = true;
    m.tracks[1].excludedReason.clear();
    const trackspeech::SpeechCheckResult again = trackspeech::applySpeechRatios(m, act);
    QVERIFY(!again.changed());
    QVERIFY(m.tracks[1].included());
}

void ParticipantsTest::allSilentKeepsEverything()
{
    Meeting m;
    m.tracks = {track("mic", TrackKind::Mic, "a.ogg"), track("loop", TrackKind::Loopback, "b.ogg")};
    MeetingActivity act;
    act.tracks = {activity("mic", TrackKind::Mic, 10, {}), activity("loop", TrackKind::Loopback, 10, {})};
    const trackspeech::SpeechCheckResult r = trackspeech::applySpeechRatios(m, act);
    QCOMPARE(r.measured.size(), 2);
    QVERIFY(r.excluded.isEmpty());
    QVERIFY(m.tracks[0].included() && m.tracks[1].included());
}

void ParticipantsTest::speechWindowsSplitAndMerge()
{
    // 0–2 mp (rövid, kimarad); 5–9,1 mp 0,3 mp-es szünettel (egy szakasz, egy ablak);
    // 12–35 mp (23 mp → 10 + 10 + 3 mp-es ablakok).
    TrackActivity t = activity("mic", TrackKind::Mic, 40, {{0, 2}, {5, 7}, {12, 35}});
    for (int f = 146; f < 182; ++f) t.dbAboveFloor[f] = 40.0f;   // 7,3–9,1 mp
    const QVector<SpeechWindow> w = participants::speechWindows(t, participants::kSideMic);
    QCOMPARE(w.size(), 4);
    QCOMPARE(w[0].startMs, qint64(5000));
    QCOMPARE(w[0].endMs, qint64(9100));
    QCOMPARE(w[1].startMs, qint64(12000));
    QCOMPARE(w[1].endMs, qint64(22000));
    QCOMPARE(w[3].startMs, qint64(32000));
    QCOMPARE(w[3].endMs, qint64(35000));
    QCOMPARE(w[0].side, participants::kSideMic);
}

void ParticipantsTest::analysisTwoClustersOneMatched()
{
    Meeting m;
    m.tracks = {track("mic", TrackKind::Mic, "track_mic.ogg"), track("loop", TrackKind::Loopback, "track_loop.ogg")};
    MeetingActivity act;
    act.tracks = {activity("mic", TrackKind::Mic, 30, {{0, 8}, {20, 28}}),
                  activity("loop", TrackKind::Loopback, 30, {{9, 18}})};
    // Hamis hang: a mikrofon „0-s", a loopback „1-es" hang.
    int reads = 0;
    const participants::PcmReader pcm = [&reads](const QString& id, qint64 s, qint64 e) {
        ++reads;
        return QVector<float>(int((e - s) * 16), id == QStringLiteral("mic") ? 0.0f : 1.0f);
    };
    const VoiceModelSpec spec = *VoiceModelRegistry::spec(QStringLiteral("campplus"));
    const VoiceEmbedderSet set({{spec, QStringLiteral("/fake")}}, fakeLoader());
    ClusterSet cs = participants::computeVoiceClusters(m, act, set, pcm);
    QVERIFY(cs.error.isEmpty());
    QCOMPARE(reads, 3);
    QCOMPARE(cs.windows, 3);
    QCOMPARE(cs.clusters.size(), 2);
    QCOMPARE(cs.clusters[0].side, participants::kSideMic);
    QCOMPARE(cs.clusters[0].windows.size(), 2);
    QVERIFY(cs.clusters[0].sampleRef.startsWith("track_mic.ogg#"));
    QCOMPARE(cs.clusters[1].side, participants::kSideLoopback);

    // Lenyomat csak a loopback hangjára („Béla"); a saját név „Ádám" (nincs lenyomata).
    const int dim = spec.dim;
    participants::CandidateContext ctx;
    ctx.selfName = QStringLiteral("Ádám");
    ctx.rank = [dim](const EmbeddingSet& q) {
        const double s = VoiceprintStore::cosineSimilarity(q.value("campplus"), oneHot(dim, 1));
        return QVector<VoiceMatch>{{QStringLiteral("Béla"), s}};
    };
    ctx.hasVoiceprint = [](const QString& n) { return n == QStringLiteral("Béla"); };
    const QVector<Participant> ps = participants::buildParticipants(cs, m, ctx);
    QCOMPARE(ps.size(), 2);
    const Participant* bela = nullptr;
    const Participant* unknown = nullptr;
    for (const Participant& p : ps) (p.personName == QStringLiteral("Béla") ? bela : unknown) = &p;
    QVERIFY(bela && unknown);
    QCOMPARE(bela->source, ParticipantSource::Voice);
    QCOMPARE(bela->sides, QStringList({participants::kSideLoopback}));
    QVERIFY(unknown->personName.isEmpty());
    QCOMPARE(unknown->sides, QStringList({participants::kSideMic}));
    QCOMPARE(cs.clusters[1].participantId, bela->id);
    QCOMPARE(cs.clusters[1].matchName, QStringLiteral("Béla"));
    QVERIFY(qAbs(bela->talkShare + unknown->talkShare - 1.0) < 1e-9);
    // Béla: csak hang → Kétséges, de bejelölt; az ismeretlen hang nincs bejelölve.
    QCOMPARE(participants::participantGroup(*bela), ParticipantGroup::Doubt);
    QVERIFY(participants::defaultChecked(*bela));
    QVERIFY(!participants::defaultChecked(*unknown));
    QCOMPARE(bela->evidence.first().text, QStringLiteral("hang 100%"));

    // Közös címke → Biztos.
    Meeting tagged = m;
    tagged.tagIds = {QStringLiteral("t1")};
    ctx.personTags = [](const QString& n) { return n == QStringLiteral("Béla") ? QStringList{"t1"} : QStringList{}; };
    ctx.tagName = [](const QString&) { return QStringLiteral("Projekt"); };
    ClusterSet cs2 = cs;
    const QVector<Participant> ps2 = participants::buildParticipants(cs2, tagged, ctx);
    QCOMPARE(ps2.first().personName, QStringLiteral("Béla"));   // a Biztos csoport elöl
    QCOMPARE(participants::participantGroup(ps2.first()), ParticipantGroup::Sure);
    bool tagEv = false;
    for (const Evidence& e : ps2.first().evidence)
        if (e.kind == EvidenceKind::Tag && e.text == QStringLiteral("címke: Projekt")) tagEv = true;
    QVERIFY(tagEv);
}

void ParticipantsTest::manualParticipantBoostAndEvidence()
{
    Meeting m;
    Participant manual;
    manual.id = QStringLiteral("m-1");
    manual.personName = QStringLiteral("Ádám");
    manual.source = ParticipantSource::Manual;
    Participant ghost = manual;
    ghost.id = QStringLiteral("m-2");
    ghost.personName = QStringLiteral("Cecília");
    m.participants = {manual, ghost};

    ClusterSet cs;
    VoiceCluster c;
    c.trackId = QStringLiteral("mic");
    c.side = participants::kSideMic;
    c.windows = {{0, 5000}};
    cs.clusters = {c};
    participants::CandidateContext ctx;
    ctx.selfName = QStringLiteral("Ádám");
    // 0,45: magában küszöb alatt, de a kézi előnnyel (+0,1) elfogadva.
    ctx.rank = [](const EmbeddingSet&) {
        return QVector<VoiceMatch>{{QStringLiteral("Ádám"), 0.45}, {QStringLiteral("Cecília"), 0.2}};
    };
    ctx.hasVoiceprint = [](const QString& n) { return n == QStringLiteral("Cecília"); };
    const QVector<Participant> ps = participants::buildParticipants(cs, m, ctx);
    QCOMPARE(ps.size(), 2);
    const Participant& adam = ps[0];
    QCOMPARE(adam.id, QStringLiteral("m-1"));
    QCOMPARE(adam.source, ParticipantSource::Manual);
    QCOMPARE(adam.sides, QStringList({participants::kSideMic}));
    // Kézi + hang + saját hang a mikrofonon → Biztos.
    QCOMPARE(participants::participantGroup(adam), ParticipantGroup::Sure);
    // Cecília: kézzel felvéve, van lenyomata, de nem hallottuk → ellentmondás → Kétséges, nincs bejelölve.
    const Participant& cili = ps[1];
    QCOMPARE(cili.id, QStringLiteral("m-2"));
    QCOMPARE(participants::participantGroup(cili), ParticipantGroup::Doubt);
    QVERIFY(!participants::defaultChecked(cili));

    // Lenyomat nélküli kézi résztvevő: „nincs lenyomata" (semleges) → bejelölhető.
    ctx.hasVoiceprint = [](const QString&) { return false; };
    const QVector<Evidence> ev = participants::participantEvidence(ghost, false, -1.0, m, ctx);
    bool noPrint = false;
    for (const Evidence& e : ev)
        if (e.kind == EvidenceKind::Voice && e.polarity == Polarity::Neutral && e.text == QStringLiteral("nincs lenyomata")
            && e.fixTarget == QStringLiteral("samples"))
            noPrint = true;
    QVERIFY(noPrint);

    // Naptár-forrás hang nélkül → „Meghívott, de nem hallottuk".
    Participant invited;
    invited.personName = QStringLiteral("Dóra");
    invited.source = ParticipantSource::Calendar;
    QCOMPARE(participants::participantGroup(invited), ParticipantGroup::InvitedNotHeard);
}

void ParticipantsTest::bindRawSpeakersByOverlap()
{
    QVector<Participant> ps(2);
    ps[0].id = QStringLiteral("v1"); ps[0].personName = QStringLiteral("Ádám");
    ps[1].id = QStringLiteral("v2"); ps[1].personName = QStringLiteral("Béla");
    VoiceCluster a; a.participantId = "v1"; a.side = participants::kSideMic; a.windows = {{0, 8000}, {20000, 28000}};
    VoiceCluster b; b.participantId = "v2"; b.side = participants::kSideLoopback; b.windows = {{9000, 18000}};
    const QVector<TranscriptLine> lines = {line("Beszélő 1", 1000, 7000), line("Beszélő 2", 9500, 15500),
                                           line("Beszélő 1", 21000, 27000), line("Beszélő 3", 40000, 41000)};
    QVERIFY(participants::bindRawSpeakers(ps, {a, b}, lines));
    QCOMPARE(ps[0].rawSpeakerIds, QStringList({"Beszélő 1"}));
    QCOMPARE(ps[1].rawSpeakerIds, QStringList({"Beszélő 2"}));   // a 3. beszélőt nem fedi semmi
    QVERIFY(qAbs(ps[0].talkShare - 12.0 / 19.0) < 1e-9);
    QVERIFY(!participants::bindRawSpeakers(ps, {a, b}, lines));   // változatlan

    // Jóváhagyás: csak Ádám bejelölve → Beszélő 1 Ádám; Béla nyers beszélője névtelen marad.
    const QVector<SpeakerBinding> bind = participants::approvalBindings(ps, {"v1"}, lines, {a, b}, nullptr);
    QCOMPARE(bind.size(), 2);
    QCOMPARE(bind[0].rawLabel, QStringLiteral("Beszélő 1"));
    QCOMPARE(bind[0].personName, QStringLiteral("Ádám"));
    QVERIFY(bind[0].utteranceIds.isEmpty());
    QCOMPARE(bind[1].rawLabel, QStringLiteral("Beszélő 2"));
    QVERIFY(bind[1].personName.isEmpty());
}

void ParticipantsTest::twoSidedRawSpeakerIsSplit()
{
    QVector<Participant> ps(2);
    ps[0].id = QStringLiteral("v1"); ps[0].personName = QStringLiteral("Ádám");
    ps[1].id = QStringLiteral("v2"); ps[1].personName = QStringLiteral("Béla");
    VoiceCluster a; a.participantId = "v1"; a.side = participants::kSideMic; a.windows = {{0, 8000}, {20000, 28000}};
    VoiceCluster b; b.participantId = "v2"; b.side = participants::kSideLoopback; b.windows = {{9000, 18000}};
    // A diarizáció Ádámot és Bélát egy beszélőnek vette („Beszélő 1"), a 2. beszélő Ádám.
    const QVector<TranscriptLine> lines = {line("Beszélő 1", 1000, 7000), line("Beszélő 1", 9500, 15500),
                                           line("Beszélő 2", 21000, 27000), line("Beszélő 1", 30000, 31000)};
    QVERIFY(participants::bindRawSpeakers(ps, {a, b}, lines));
    QCOMPARE(ps[0].rawSpeakerIds, QStringList({"Beszélő 1@mic", "Beszélő 2"}));
    QCOMPARE(ps[1].rawSpeakerIds, QStringList({"Beszélő 1@loopback"}));
    QCOMPARE(participants::rawDisplay(ps[1].rawSpeakerIds.first()), QStringLiteral("Beszélő 1 · hívás"));
    QCOMPARE(participants::rawDisplay(ps[0].rawSpeakerIds.first()), QStringLiteral("Beszélő 1 · mikrofon"));
    QCOMPARE(participants::rawDisplay(QStringLiteral("Beszélő 2")), QStringLiteral("Beszélő 2"));

    // Jóváhagyás mindkettőre: Beszélő 1 sorai oldal szerint; az eldönthetetlen sor (30 mp) a
    // nagyobbik (itt: egyenlő → mikrofon) oldalhoz.
    const QVector<SpeakerBinding> bind = participants::approvalBindings(ps, {"v1", "v2"}, lines, {a, b}, nullptr);
    QCOMPARE(bind.size(), 3);
    QCOMPARE(bind[0].personName, QStringLiteral("Ádám"));
    QCOMPARE(bind[0].utteranceIds, QStringList({"u1000", "u30000"}));
    QCOMPARE(bind[1].rawLabel, QStringLiteral("Beszélő 2"));
    QVERIFY(bind[1].utteranceIds.isEmpty());
    QCOMPARE(bind[2].personName, QStringLiteral("Béla"));
    QCOMPARE(bind[2].utteranceIds, QStringList({"u9500"}));

    // Oldal sáv-energiából, ha a klaszterek nem fedik a sort.
    MeetingActivity act;
    act.tracks = {activity("mic", TrackKind::Mic, 40, {}), activity("loop", TrackKind::Loopback, 40, {{30, 31}})};
    QCOMPARE(participants::lineSide(lines[3], {a, b}, &act), participants::kSideLoopback);
    QCOMPARE(participants::lineSide(lines[3], {a, b}, nullptr), QString());
}

void ParticipantsTest::meetingJsonRoundTrip()
{
    Meeting m;
    m.id = QStringLiteral("x");
    Track t = track("mic", TrackKind::Mic, "a.ogg");
    t.speechRatio = 0.25;
    Track off = track("loop", TrackKind::Loopback, "b.ogg");
    off.active = false;
    off.excludedReason = trackspeech::kNoSpeech;
    m.tracks = {t, off};
    Participant p;
    p.id = QStringLiteral("v1");
    p.personName = QStringLiteral("Béla");
    p.rawSpeakerIds = {QStringLiteral("Beszélő 1@loopback")};
    p.source = ParticipantSource::Voice;
    p.approved = true;
    p.sides = {participants::kSideLoopback};
    p.evidence = {{EvidenceKind::Voice, Polarity::Support, 0.82, "hang 82%", "", "samples"}};
    p.talkShare = 0.4;
    m.participants = {p};
    ParticipantApproval ap;
    ap.at = QStringLiteral("2026-10-10T10:00:00");
    ap.modelIds = {QStringLiteral("campplus")};
    ap.candidates = {{p.id, p.personName, ParticipantGroup::Sure, true, p.rawSpeakerIds}};
    m.approval = ap;

    const Meeting r = meetingFromJson(toJson(m));
    QCOMPARE(r.tracks[0].speechRatio, 0.25);
    QVERIFY(r.tracks[0].included());
    QCOMPARE(r.tracks[1].excludedReason, trackspeech::kNoSpeech);
    QCOMPARE(r.participants.size(), 1);
    const Participant& q = r.participants.first();
    QCOMPARE(q.personName, p.personName);
    QCOMPARE(q.rawSpeakerIds, p.rawSpeakerIds);
    QCOMPARE(q.source, ParticipantSource::Voice);
    QVERIFY(q.approved);
    QCOMPARE(q.sides, p.sides);
    QCOMPARE(q.evidence.size(), 1);
    QCOMPARE(q.evidence[0].polarity, Polarity::Support);
    QCOMPARE(q.evidence[0].fixTarget, QStringLiteral("samples"));
    QCOMPARE(q.talkShare, 0.4);
    QVERIFY(r.approval.has_value());
    QCOMPARE(r.approval->candidates.size(), 1);
    QCOMPARE(r.approval->candidates[0].group, ParticipantGroup::Sure);
    QVERIFY(r.approval->candidates[0].checked);
    QVERIFY(!r.approval->solo);

    // Régi meeting: eldobott sáv ok nélkül → „nincs beszéd"; a mért arány hiányzik → -1.
    QJsonObject legacy = toJson(off);
    legacy.remove(QStringLiteral("excludedReason"));
    const Track lt = trackFromJson(legacy);
    QCOMPARE(lt.excludedReason, trackspeech::kNoSpeech);
    QCOMPARE(lt.speechRatio, -1.0);

    // A perzisztált elemzés oda-vissza.
    QTemporaryDir dir;
    ParticipantAnalysisFile f;
    f.at = ap.at;
    f.modelIds = ap.modelIds;
    f.tracksKey = QStringLiteral("abc");
    VoiceCluster c;
    c.trackId = "loop"; c.side = participants::kSideLoopback; c.participantId = "v1";
    c.matchName = "Béla"; c.matchScore = 0.82; c.windows = {{1000, 5000}}; c.sampleRef = "b.ogg#1000-5000";
    f.clusters = {c};
    QVERIFY(f.save(dir.path()));
    const ParticipantAnalysisFile g = ParticipantAnalysisFile::load(dir.path());
    QCOMPARE(g.tracksKey, f.tracksKey);
    QCOMPARE(g.clusters.size(), 1);
    QCOMPARE(g.clusters[0].windows, c.windows);
    QCOMPARE(g.clusters[0].matchName, c.matchName);
}

QTEST_GUILESS_MAIN(ParticipantsTest)
#include "test_participants.moc"
