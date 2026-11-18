// Side split (editing/review/SideSplit.h) and local-side ownership (processing ParticipantAnalysis
// approvalBindings), on synthetic activity + tokens (no audio, no network):
//   * the merger breaks a line before a side-cut token; tokens.json keeps the cut list;
//   * one cut, two cuts (local, remote, local), real word spans (Whisper);
//   * no cut: talking over each other (Mixed), the echo of the local voice on the loopback, a remote
//     run too short, a single-track meeting;
//   * the CLI path (reslice::planSideSplit / applySideSplit): ids, overlay inheritance (a contradicting
//     side inherits nothing, confirmed is not inherited), summary spans, embedding entries;
//   * local ownership with and without manual tracks; a manual correction / confirmed line wins.
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/editing/review/SideSplit.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/editing/session/TranscriptReslice.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/library/store/UtteranceEmbeddingCache.h"
#include "tanara/processing/analysis/ParticipantAnalysis.h"

using namespace tanara;
using editing::Side;

namespace {

PersonId P(const QString& name) { return PersonId::fromString(name.toCaseFolded()); }

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
            if (ms / 50 < t.dbAboveFloor.size()) t.dbAboveFloor[int(ms / 50)] = sp.db;
    return t;
}

MeetingActivity twoTracks(qint64 totalMs, const QVector<Span>& mic, const QVector<Span>& loop)
{
    MeetingActivity a;
    a.tracks = {activity("mic", TrackKind::Mic, totalMs, mic), activity("loop", TrackKind::Loopback, totalMs, loop)};
    return a;
}

// Soniox-style tokens of one speaker: a word every 300 ms from fromMs to toMs, 60 ms wide (emission time).
MergedTranscript soniox(qint64 fromMs, qint64 toMs, const QString& speaker = QStringLiteral("speaker:1"))
{
    MergedTranscript mt;
    mt.emissionTiming = true;
    int i = 0;
    for (qint64 t = fromMs; t <= toMs; t += 300, ++i)
        mt.tokens.append(TranscriptToken{QStringLiteral(" w%1").arg(i), speaker, t, t + 60, 0.9, QStringLiteral("mix")});
    return mt;
}

QVector<TranscriptLine> linesOf(const MergedTranscript& mt)
{
    QVector<TranscriptLine> out;
    for (const Utterance& u : mt.segments())
        out.append(TranscriptLine{QStringLiteral("u%1").arg(u.startMs), u.startMs, u.endMs, u.speaker, u.text});
    return out;
}

Track track(const QString& id, TrackKind kind, bool fixedSpeaker = false)
{
    Track t;
    t.id = id;
    t.deviceName = id;
    t.kind = kind;
    t.file = QStringLiteral("track_%1.ogg").arg(id);
    t.fixedSpeaker = fixedSpeaker;
    return t;
}

void writeBytes(const QString& path, const QByteArray& b)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(b);
}

} // namespace

class SideSplitTest : public QObject {
    Q_OBJECT
private slots:
    void mergerBreaksBeforeCutToken();
    void oneCut();
    void twoCuts();
    void wordSpans();
    void talkingOverIsNotCut();
    void echoIsNotCut();
    void shortRunIsNotCut();
    void singleTrackIsInactive();
    void cliPathCarriesIdsOverlayAndSummary();
    void localOwnershipWithManualTracks();
    void localOwnershipFromFixedMicWithoutManualTracks();
    void localOwnershipKeepsManualCorrections();
};

void SideSplitTest::mergerBreaksBeforeCutToken()
{
    MergedTranscript mt = soniox(1000, 2800);   // 7 tokens, one line
    QCOMPARE(mt.segments().size(), 1);
    mt.sideCuts = {1900};
    const QVector<Utterance> segs = mt.segments();
    QCOMPARE(segs.size(), 2);
    QCOMPARE(segs.at(0).speaker, segs.at(1).speaker);   // the parts keep the raw label
    QCOMPARE(segs.at(0).text, QStringLiteral("w0 w1 w2"));
    QCOMPARE(segs.at(1).text, QStringLiteral("w3 w4 w5 w6"));
    QCOMPARE(segs.at(0).startMs, 700);                  // the padding applies to the parts
    QCOMPARE(segs.at(1).startMs, 1660);                 // up to the previous part's end
    QCOMPARE(mt.lineTokenGroups(), (QVector<QVector<int>>{{0, 1, 2}, {3, 4, 5, 6}}));

    // tokens.json keeps the cut list.
    QTemporaryDir dir;
    QVERIFY(library::TranscriptRepository::writeTokens(dir.path(), mt));
    const MergedTranscript back = library::TranscriptRepository::readTokens(dir.path());
    QCOMPARE(back.sideCuts, QVector<qint64>{1900});
    QCOMPARE(back.segments().size(), 2);
}

void SideSplitTest::oneCut()
{
    // Local speech, then remote speech under one raw speaker.
    const MergedTranscript mt = soniox(1000, 7000);
    const MeetingActivity act = twoTracks(9000, {{600, 4000, 30}}, {{4000, 7100, 30}});
    const editing::sides::SplitResult r = editing::sides::splitLinesBySide(mt, act);
    QVERIFY(r.active);
    QCOMPARE(r.cuts.size(), 1);
    QVERIFY2(r.cuts.first() >= 3700 && r.cuts.first() <= 4600, qPrintable(QString::number(r.cuts.first())));
    QCOMPARE(r.lines.size(), 1);
    QCOMPARE(r.lines.first().partSides, (QVector<Side>{Side::Local, Side::Remote}));
    QCOMPARE(r.parts(), 2);

    MergedTranscript cut = mt;
    cut.sideCuts = r.cuts;
    QCOMPARE(cut.segments().size(), 2);
    // Every part is on one side now.
    const QVector<Side> sides = editing::sides::classifyLineSides(act, linesOf(cut));
    QCOMPARE(sides, (QVector<Side>{Side::Local, Side::Remote}));
}

void SideSplitTest::twoCuts()
{
    const MergedTranscript mt = soniox(1000, 9100);
    const MeetingActivity act = twoTracks(11000, {{600, 3700, 30}, {6400, 9200, 30}}, {{3700, 6400, 30}});
    const editing::sides::SplitResult r = editing::sides::splitLinesBySide(mt, act);
    QCOMPARE(r.cuts.size(), 2);
    QCOMPARE(r.lines.first().partSides, (QVector<Side>{Side::Local, Side::Remote, Side::Local}));
    QCOMPARE(r.parts(), 3);
}

void SideSplitTest::wordSpans()
{
    // Whisper-style real word spans (250 ms wide, no emission flag).
    MergedTranscript mt;
    for (qint64 t = 0; t < 6000; t += 300)
        mt.tokens.append(TranscriptToken{QStringLiteral(" w"), QStringLiteral("speaker:1"), t, t + 250, 1.0, {}});
    const MeetingActivity act = twoTracks(8000, {{0, 3000, 30}}, {{3000, 6000, 30}});
    const editing::sides::SplitResult r = editing::sides::splitLinesBySide(mt, act);
    QCOMPARE(r.cuts.size(), 1);
    QVERIFY2(r.cuts.first() >= 2700 && r.cuts.first() <= 3300, qPrintable(QString::number(r.cuts.first())));
    QCOMPARE(r.lines.first().partSides, (QVector<Side>{Side::Local, Side::Remote}));
}

void SideSplitTest::talkingOverIsNotCut()
{
    // The second half: both sides at speech level (Mixed) -> neutral, no cut.
    const MergedTranscript mt = soniox(1000, 7000);
    const MeetingActivity act = twoTracks(12000, {{600, 7100, 30}}, {{4000, 7100, 30}, {9000, 11000, 30}});
    const editing::sides::SplitResult r = editing::sides::splitLinesBySide(mt, act);
    QVERIFY(r.active);
    QVERIFY(r.cuts.isEmpty());
}

void SideSplitTest::echoIsNotCut()
{
    // The local voice returns on the loopback 8 dB below real remote speech (which sets the loopback's
    // speech level elsewhere): classifyLevels says Local (echo), so there is no Remote run.
    const MergedTranscript mt = soniox(1000, 7000);
    const MeetingActivity act = twoTracks(14000, {{600, 7100, 30}}, {{4000, 7100, 22}, {9000, 13000, 30}});
    QCOMPARE(editing::sides::classifyWindow(act, editing::sides::speechLevels(act), 4500, 6500), Side::Local);
    QVERIFY(editing::sides::splitLinesBySide(mt, act).cuts.isEmpty());
}

void SideSplitTest::shortRunIsNotCut()
{
    // A 600 ms remote burst inside a local line: shorter than kSplitMinRunMs.
    const MergedTranscript mt = soniox(1000, 7000);
    const MeetingActivity act =
        twoTracks(12000, {{600, 4000, 30}, {4600, 7100, 30}}, {{4000, 4600, 30}, {9000, 11000, 30}});
    static_assert(editing::sides::kSplitMinRunMs > 600);
    QVERIFY(editing::sides::splitLinesBySide(mt, act).cuts.isEmpty());

    // The same burst long enough is cut twice (local, remote, local).
    const MeetingActivity longer =
        twoTracks(12000, {{600, 3000, 30}, {4600, 7100, 30}}, {{3000, 4600, 30}, {9000, 11000, 30}});
    QCOMPARE(editing::sides::splitLinesBySide(mt, longer).cuts.size(), 2);
}

void SideSplitTest::singleTrackIsInactive()
{
    const MergedTranscript mt = soniox(1000, 7000);
    MeetingActivity act;
    act.tracks = {activity("mic", TrackKind::Mic, 9000, {{600, 4000, 30}})};
    const editing::sides::SplitResult r = editing::sides::splitLinesBySide(mt, act);
    QVERIFY(!r.active);
    QVERIFY(r.cuts.isEmpty());
    QVERIFY(!editing::sides::splitLinesBySide(mt, MeetingActivity()).active);
}

void SideSplitTest::cliPathCarriesIdsOverlayAndSummary()
{
    QTemporaryDir dir;
    const QString folder = dir.path();
    Meeting m;
    m.id = QStringLiteral("side-split-1");
    m.title = QStringLiteral("Side split");
    m.folder = folder;
    m.tracks = {track("mic", TrackKind::Mic), track("loop", TrackKind::Loopback)};
    QVERIFY(library::MeetingRepository::writeMeetingFile(m));

    // Line A (speaker:1, 1000-7000): local then remote. Line B (speaker:2, 20000-25000): remote only.
    // Line C (speaker:1, 30000-36000): local then remote. Line D (speaker:1, 40000-46000): remote then local.
    MergedTranscript mt = soniox(1000, 7000);
    mt.tokens << soniox(20000, 25000, QStringLiteral("speaker:2")).tokens << soniox(30000, 36000).tokens
              << soniox(40000, 46000).tokens;
    std::stable_sort(mt.tokens.begin(), mt.tokens.end(),
                     [](const TranscriptToken& a, const TranscriptToken& b) { return a.startMs < b.startMs; });
    QVERIFY(library::TranscriptRepository::writeTokens(folder, mt));
    QVERIFY(library::TranscriptRepository::writeSegments(folder, mt.segments()));
    const MeetingActivity act = twoTracks(48000, {{600, 4000, 30}, {29600, 33000, 30}, {43000, 46100, 30}},
                                          {{4000, 7100, 30}, {19600, 25100, 30}, {33000, 36100, 30}, {39600, 43000, 30}});

    const QVector<TranscriptLine> old = editing::speakeredit::loadTranscriptLines(folder);
    QCOMPARE(old.size(), 4);
    const QString a = old.at(0).id, b = old.at(1).id, c = old.at(2).id, d = old.at(3).id;
    QCOMPARE(a, QStringLiteral("u700"));

    // Overlay: A was corrected to the local participant and confirmed; C was corrected to the remote
    // participant (contradicts its first, local part); D only confirmed (raw speaker, unknown side).
    SpeakerOverlay ov;
    ov.transcriptFingerprint = editing::speakeredit::transcriptFingerprint(old);
    ov.participants = {OverlayParticipant{QStringLiteral("participant:1"), P("Ádám"), {}, 0},
                       OverlayParticipant{QStringLiteral("participant:2"), P("Béla"), {}, 1}};
    ov.nextParticipant = 3;
    ov.speakerTracks.insert(QStringLiteral("participant:1"), {QStringLiteral("mic")});
    ov.speakerTracks.insert(QStringLiteral("participant:2"), {QStringLiteral("loop")});
    ov.utterances[a].speaker = QStringLiteral("participant:1");
    ov.utterances[a].corrected = true;
    ov.utterances[a].confirmed = true;
    ov.utterances[c].speaker = QStringLiteral("participant:2");
    ov.utterances[c].corrected = true;
    ov.utterances[d].confirmed = true;
    QVERIFY(editing::speakeredit::saveOverlay(folder, ov));

    writeBytes(QDir(folder).filePath("summary.json"),
               QJsonDocument(QJsonObject{{"summary", QJsonObject{{"statements", QJsonArray{QJsonObject{
                   {"id", "s1"}, {"sourceSpans", QJsonArray{QJsonObject{{"startMs", 700}, {"endMs", 7000},
                                                                        {"utteranceIds", QJsonArray{a, b}}}}}}}}}}})
                   .toJson());
    library::UtteranceEmbeddingCache cache;
    cache.fingerprint = ov.transcriptFingerprint;
    for (const TranscriptLine& l : old) cache.set(QStringLiteral("m1"), l.id, {1.0f, 0.0f});
    QVERIFY(cache.save(folder));

    // Plan: nothing written.
    const QByteArray segBefore = [&] { QFile f(library::TranscriptRepository::segmentsPath(folder)); f.open(QIODevice::ReadOnly); return f.readAll(); }();
    const editing::reslice::SplitPlan p = editing::reslice::planSideSplit(folder, act, SelfPerson{P("Ádám"), {}});
    QVERIFY2(p.ok, qPrintable(p.error));
    QVERIFY(p.hasWork());
    QCOMPARE(p.cutLines, 3);
    QCOMPARE(p.parts, 6);
    QCOMPARE(p.newLines.size(), 7);
    QCOMPARE(p.partsOf.value(b), QStringList{b});
    QCOMPARE(p.partsOf.value(a).size(), 2);
    QCOMPARE(p.partsOf.value(a).first(), a);   // the first part keeps the id
    QCOMPARE(p.confirmedNotInherited, 1);       // A's second part
    {
        QFile f(library::TranscriptRepository::segmentsPath(folder));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), segBefore);
    }

    const editing::reslice::ApplyResult res = editing::reslice::applySideSplit(folder, p);
    QVERIFY2(res.ok, qPrintable(res.error));
    QVERIFY(QFile::exists(library::TranscriptRepository::tokensPath(folder) + QStringLiteral(".side-split.bak")));

    const QVector<TranscriptLine> now = editing::speakeredit::loadTranscriptLines(folder);
    QCOMPARE(now.size(), 7);
    const QString a2 = p.partsOf.value(a).at(1), c1 = p.partsOf.value(c).at(0), c2 = p.partsOf.value(c).at(1);
    const QString d1 = p.partsOf.value(d).at(0), d2 = p.partsOf.value(d).at(1);
    const SpeakerOverlay nov = editing::speakeredit::loadOverlayFor(folder, now);
    QCOMPARE(nov.participants.size(), 2);   // the overlay still belongs to the (new) transcript
    // A: the first part keeps everything; the (remote) second part contradicts the local person: nothing.
    QCOMPARE(nov.utterances.value(a).speaker, QStringLiteral("participant:1"));
    QVERIFY(nov.utterances.value(a).confirmed);
    QVERIFY(!nov.utterances.contains(a2));
    // C: the local first part contradicts the remote person: its speaker goes; the remote part inherits it.
    QVERIFY(!nov.utterances.contains(c1));
    QCOMPARE(nov.utterances.value(c2).speaker, QStringLiteral("participant:2"));
    QVERIFY(nov.utterances.value(c2).corrected);
    QVERIFY(!nov.utterances.value(c2).confirmed);
    // D: a confirmed line of a raw speaker without a side: the first part keeps the flag, the new one does not.
    QVERIFY(nov.utterances.value(d1).confirmed);
    QVERIFY(!nov.utterances.contains(d2));

    // The summary span lists every part of A, and its times are refreshed.
    QFile sf(QDir(folder).filePath("summary.json"));
    QVERIFY(sf.open(QIODevice::ReadOnly));
    const QJsonObject span = QJsonDocument::fromJson(sf.readAll()).object().value("summary").toObject()
                                 .value("statements").toArray().at(0).toObject()
                                 .value("sourceSpans").toArray().at(0).toObject();
    QStringList ids;
    for (const QJsonValue& v : span.value("utteranceIds").toArray()) ids << v.toString();
    QCOMPARE(ids, (QStringList{a, a2, b}));
    QCOMPARE(qint64(span.value("startMs").toDouble()), now.at(0).startMs);

    // Embeddings: the changed lines' entries are gone, B's stays under the new fingerprint.
    QCOMPARE(res.embeddingsDropped, 3);
    const library::UtteranceEmbeddingCache after =
        library::UtteranceEmbeddingCache::load(folder, editing::speakeredit::transcriptFingerprint(now));
    QVERIFY(after.has(QStringLiteral("m1"), b));
    QVERIFY(!after.has(QStringLiteral("m1"), a));

    // A second run has nothing to do.
    const editing::reslice::SplitPlan again = editing::reslice::planSideSplit(folder, act, SelfPerson{P("Ádám"), {}});
    QVERIFY2(again.ok, qPrintable(again.error));
    QVERIFY(!again.hasWork());
}

void SideSplitTest::localOwnershipWithManualTracks()
{
    Meeting m;
    m.tracks = {track("mic", TrackKind::Mic), track("loop", TrackKind::Loopback)};
    QVector<Participant> ps(2);
    ps[0].id = QStringLiteral("v1"); ps[0].personId = P("Ádám"); ps[0].tracks = {QStringLiteral("mic")};
    ps[0].rawSpeakerIds = {QStringLiteral("speaker:1")};
    ps[1].id = QStringLiteral("v2"); ps[1].personId = P("Béla");   // no manual tracks
    ps[1].rawSpeakerIds = {QStringLiteral("speaker:2")};
    // speaker:1 (Ádám's raw speaker) has a remote line; speaker:2 (Béla's) a local line.
    const QVector<TranscriptLine> lines = {{"u1000", 1000, 3000, "speaker:1", {}}, {"u4000", 4000, 6000, "speaker:1", {}},
                                           {"u7000", 7000, 9000, "speaker:2", {}}, {"u10000", 10000, 12000, "speaker:2", {}}};
    const MeetingActivity act = twoTracks(14000, {{1000, 3000, 30}, {7000, 9000, 30}}, {{4000, 6000, 30}, {10000, 12000, 30}});
    QCOMPARE(processing::participants::singleLocalPerson(ps, {"v1", "v2"}, m, {}), P("Ádám"));

    const QVector<SpeakerBinding> bind =
        processing::participants::approvalBindings(ps, {"v1", "v2"}, lines, {}, &act, &m, {}, nullptr);
    // Ádám's raw speaker goes anonymous (its remote line is not his), his local lines go to him per line.
    QHash<QString, PersonId> owner, raw;
    for (const SpeakerBinding& b : bind) {
        if (b.utteranceIds.isEmpty()) raw.insert(b.rawLabel, b.personId);
        else for (const QString& id : b.utteranceIds) owner.insert(id, b.personId);
    }
    QVERIFY(raw.contains("speaker:1") && raw.value("speaker:1").isNull());
    QCOMPARE(owner.value("u1000"), P("Ádám"));
    QVERIFY(!owner.contains("u4000"));                 // stays anonymous
    QCOMPARE(owner.value("u7000"), P("Ádám"));         // a local line, whatever raw speaker
    QCOMPARE(raw.value("speaker:2"), P("Béla"));       // the rest of Béla's raw speaker is his
    QVERIFY(!owner.contains("u10000"));

    // Two local people: no ownership.
    ps[1].tracks = {QStringLiteral("mic")};
    QVERIFY(processing::participants::singleLocalPerson(ps, {"v1", "v2"}, m, {}).isNull());
    // Without activity (single track / no cache): nothing changes.
    ps[1].tracks.clear();
    const QVector<SpeakerBinding> plain =
        processing::participants::approvalBindings(ps, {"v1", "v2"}, lines, {}, nullptr, &m, {}, nullptr);
    QCOMPARE(plain.size(), 2);
    QVERIFY(plain[0].utteranceIds.isEmpty() && plain[1].utteranceIds.isEmpty());
}

void SideSplitTest::localOwnershipFromFixedMicWithoutManualTracks()
{
    Meeting m;
    m.tracks = {track("mic", TrackKind::Mic, /*fixedSpeaker*/ true), track("loop", TrackKind::Loopback, true)};
    QVector<Participant> ps(2);
    ps[0].id = QStringLiteral("v1"); ps[0].personId = P("Ádám"); ps[0].rawSpeakerIds = {QStringLiteral("speaker:1")};
    ps[1].id = QStringLiteral("v2"); ps[1].personId = P("Béla"); ps[1].rawSpeakerIds = {QStringLiteral("speaker:2")};
    ps[1].sides = {QStringLiteral("loopback")};
    const QVector<TranscriptLine> lines = {{"u1000", 1000, 3000, "speaker:2", {}}, {"u4000", 4000, 6000, "speaker:2", {}}};
    const MeetingActivity act = twoTracks(8000, {{1000, 3000, 30}}, {{4000, 6000, 30}});

    // Without the own person nobody is local.
    QVERIFY(processing::participants::singleLocalPerson(ps, {"v1", "v2"}, m, {}).isNull());
    QCOMPARE(processing::participants::singleLocalPerson(ps, {"v1", "v2"}, m, P("Ádám")), P("Ádám"));
    const QVector<SpeakerBinding> bind =
        processing::participants::approvalBindings(ps, {"v1", "v2"}, lines, {}, &act, &m, P("Ádám"), nullptr);
    QCOMPARE(bind.last().personId, P("Ádám"));
    QCOMPARE(bind.last().utteranceIds, QStringList{"u1000"});

    // Someone else heard only on the mic (no manual tracks): ambiguous, no ownership.
    ps[1].sides = {QStringLiteral("mic")};
    QVERIFY(processing::participants::singleLocalPerson(ps, {"v1", "v2"}, m, P("Ádám")).isNull());
    // No fixed-speaker mic track: the own person is not taken as local.
    ps[1].sides = {QStringLiteral("loopback")};
    m.tracks[0].fixedSpeaker = false;
    QVERIFY(processing::participants::singleLocalPerson(ps, {"v1", "v2"}, m, P("Ádám")).isNull());
}

void SideSplitTest::localOwnershipKeepsManualCorrections()
{
    Meeting m;
    m.tracks = {track("mic", TrackKind::Mic), track("loop", TrackKind::Loopback)};
    QVector<Participant> ps(2);
    ps[0].id = QStringLiteral("v1"); ps[0].personId = P("Ádám"); ps[0].tracks = {QStringLiteral("mic")};
    ps[0].rawSpeakerIds = {QStringLiteral("speaker:1")};
    // (Béla has no manual tracks: the track veto is off, only the local-side ownership acts.)
    ps[1].id = QStringLiteral("v2"); ps[1].personId = P("Béla");
    ps[1].rawSpeakerIds = {QStringLiteral("speaker:2")};
    const QVector<TranscriptLine> lines = {{"u1000", 1000, 3000, "speaker:1", {}}, {"u4000", 4000, 6000, "speaker:1", {}},
                                           {"u7000", 7000, 9000, "speaker:2", {}}, {"u10000", 10000, 12000, "speaker:1", {}}};
    const MeetingActivity act =
        twoTracks(14000, {{1000, 3000, 30}, {7000, 9000, 30}}, {{4000, 6000, 30}, {10000, 12000, 30}});
    // u7000 (local side) was corrected by hand to Béla; u4000 (remote side, Ádám's raw speaker) confirmed.
    SpeakerOverlay ov;
    ov.utterances["u7000"].speaker = QStringLiteral("participant:2");
    ov.utterances["u7000"].corrected = true;
    ov.utterances["u4000"].confirmed = true;

    const QVector<SpeakerBinding> bind =
        processing::participants::approvalBindings(ps, {"v1", "v2"}, lines, {}, &act, &m, {}, &ov);
    QHash<QString, PersonId> owner;
    for (const SpeakerBinding& b : bind)
        for (const QString& id : b.utteranceIds) owner.insert(id, b.personId);
    QVERIFY(!owner.contains("u7000"));                 // the manual correction wins
    QCOMPARE(owner.value("u4000"), P("Ádám"));         // the confirmed line keeps its owner
    QCOMPARE(owner.value("u1000"), P("Ádám"));
    QVERIFY(owner.value("u10000") != P("Ádám"));       // an unlocked remote line is never Ádám's
}

QTEST_GUILESS_MAIN(SideSplitTest)
#include "test_side_split.moc"
