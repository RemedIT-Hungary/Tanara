//
// Sávok meghallgatása az AppController-ben (G6): a beszéd-csík aktivitása MINDEN sávra (a
// lekeverésből kimaradtra is) háttérben számolva és gyorsítótárazva (trackActivityReady), és a
// beszélő → sáv kötés (setParticipantTracks / participantTracks): átirat előtt a
// Participant::tracks, átirat után a szerkesztő kézi sáv-beosztása (ugyanaz, amit a „Miért ő?"
// chipek írnak), a következtetett oldal kézi kötés nélkül, és az átirat érkezésekor a
// résztvevő kötése átkerül a beszélő beosztásába.
// Szintetikus sávok (ffmpeg lavfi), IZOLÁLT TANARA_HOME, Soniox / LLM nélkül. ffmpeg nélkül QSKIP.
//
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/AppController.h"
#include "tanara/edit/ParticipantAnalysis.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/edit/TrackActivity.h"
#include "tanara/store/JsonSerialization.h"
#include "tanara/store/MeetingStore.h"

#include <memory>

using namespace tanara;

namespace {

// 24 mp-es sáv: freq Hz-es szinusz, ahol `on` igaz, máshol digitális csend.
bool makeTrack(const QString& path, int freq, const QString& onExpr)
{
    QProcess p;
    const QString filter = QStringLiteral("sine=frequency=%1:duration=24,volume='if(%2,1,0)':eval=frame")
                               .arg(freq).arg(onExpr);
    p.start(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i", filter, "-ar", "16000", "-ac", "1",
                                       "-y", path});
    return p.waitForFinished(30000) && p.exitCode() == 0 && QFileInfo::exists(path);
}

} // namespace

class TrackListeningAppTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();

    void activityCoversExcludedTrackAndIsCached();
    void participantTracksSerialize();
    void bindingBeforeTranscript();
    void bindingAfterTranscriptIsTheEditorAssignment();
    void bindingCopiedToOverlayOnTranscript();
    void manualTracksSteerClusterMatchAndEvidence();

private:
    Meeting makeMeeting();
    void writeTranscript(Meeting& m);

    bool m_haveFfmpeg = false;
    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<AppController> m_app;
};

void TrackListeningAppTest::initTestCase()
{
    m_haveFfmpeg = !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();
    qputenv("TANARA_CLOUD", "off");
}

void TrackListeningAppTest::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    qputenv("TANARA_HOME", m_home->path().toUtf8());
    m_app = std::make_unique<AppController>();
    QVERIFY(m_app->store()->metadataDir().startsWith(m_home->path()));
    m_app->setUserSpeakerName(QStringLiteral("Ádám"));
}

void TrackListeningAppTest::cleanup()
{
    m_app.reset();
    m_home.reset();
}

// Mikrofon: 220 Hz 0–8 és 16–24 mp; loopback: 440 Hz 8–16 mp, KÉZZEL kivéve a lekeverésből.
Meeting TrackListeningAppTest::makeMeeting()
{
    Meeting m = m_app->store()->createMeeting(QStringLiteral("Meghallgatás"));
    const QDir dir(m.folder);
    if (m_haveFfmpeg) {
        [&] { QVERIFY(makeTrack(dir.filePath("track_mic.wav"), 220, "not(between(t,8,16))")); }();
        [&] { QVERIFY(makeTrack(dir.filePath("track_loop.wav"), 440, "between(t,8,16)")); }();
    }
    auto add = [&m](const QString& id, TrackKind kind, const QString& file) {
        Track t;
        t.id = id;
        t.deviceName = id;
        t.kind = kind;
        t.file = file;
        m.tracks.append(t);
    };
    add("mic", TrackKind::Mic, "track_mic.wav");
    add("loop", TrackKind::Loopback, "track_loop.wav");
    m.durationMs = 24000;
    Participant me;
    me.id = "v1";
    me.personName = "Ádám";
    me.sides = {"mic"};
    me.rawSpeakerIds = {"Beszélő 1"};
    Participant far;
    far.id = "v2";
    far.personName = "Béla";
    far.sides = {"loopback"};
    far.rawSpeakerIds = {"Beszélő 2@loopback"};
    m.participants = {me, far};
    m_app->store()->saveMeeting(m);
    return m;
}

void TrackListeningAppTest::writeTranscript(Meeting& m)
{
    const QVector<std::tuple<qint64, qint64, QString>> rows = {
        {500, 7500, "Beszélő 1"}, {8500, 15500, "Beszélő 2"}, {16500, 23500, "Beszélő 1"}};
    QJsonArray toks, segs;
    for (const auto& [s, e, spk] : rows) {
        toks.append(QJsonObject{{"text", "x"}, {"speaker", spk}, {"startMs", double(s)}, {"endMs", double(e)},
                                {"confidence", 1.0}, {"trackId", "mixdown"}});
        segs.append(QJsonObject{{"startMs", double(s)}, {"endMs", double(e)}, {"speaker", spk}, {"text", "x"}});
    }
    QFile tf(QDir(m.folder).filePath("transcript.tokens.json"));
    QVERIFY(tf.open(QIODevice::WriteOnly));
    tf.write(QJsonDocument(QJsonObject{{"language", "hu"}, {"tokens", toks}}).toJson());
    tf.close();
    QFile sf(QDir(m.folder).filePath("transcript.segments.json"));
    QVERIFY(sf.open(QIODevice::WriteOnly));
    sf.write(QJsonDocument(segs).toJson());
    sf.close();
    m = m_app->store()->load(m.id);
    m.hasTranscript = true;
    m_app->store()->saveMeeting(m);
}

void TrackListeningAppTest::activityCoversExcludedTrackAndIsCached()
{
    if (!m_haveFfmpeg) QSKIP("nincs ffmpeg");
    const Meeting m0 = makeMeeting();
    // A loopback kézzel kivéve (a lekeverés-újrakeverés mellékhatás, itt nem várjuk meg).
    Meeting m = m_app->store()->load(m0.id);
    m.tracks[1].active = false;
    m.tracks[1].excludedReason = QStringLiteral("manual");
    m_app->store()->saveMeeting(m);

    QSignalSpy ready(m_app.get(), &AppController::trackActivityReady);
    MeetingActivity a = m_app->trackActivity(m0.id);
    QVERIFY(a.isEmpty());                       // még nincs: háttérben számol
    QVERIFY(m_app->trackActivityComputing(m0.id));
    QVERIFY(m_app->trackActivity(m0.id).isEmpty());   // nem indul második számolás
    QVERIFY(ready.wait(30000));
    QCOMPARE(ready.count(), 1);
    QCOMPARE(ready.first().at(0).toString(), m0.id);
    QVERIFY(!m_app->trackActivityComputing(m0.id));

    a = m_app->trackActivity(m0.id);
    QCOMPARE(a.tracks.size(), 2);               // a kimaradt sáv is
    QCOMPARE(a.tracks[0].trackId, QStringLiteral("mic"));
    QCOMPARE(a.tracks[1].trackId, QStringLiteral("loop"));
    // A loopback 8–16 mp között szól: a 10. mp-nél magasan a padló felett, a 2. mp-nél nem.
    const TrackActivity& loop = a.tracks[1];
    QVERIFY(loop.dbAboveFloor.at(10000 / loop.frameMs) > 20.0f);
    QVERIFY(loop.dbAboveFloor.at(2000 / loop.frameMs) < 3.0f);
    QVERIFY(QFileInfo::exists(MeetingActivity::viewFilePath(m.folder)));

    // Új AppController (új folyamat): a lemezről jön, számolás nélkül.
    m_app = std::make_unique<AppController>();
    QSignalSpy ready2(m_app.get(), &AppController::trackActivityReady);
    QCOMPARE(m_app->trackActivity(m0.id).tracks.size(), 2);
    QVERIFY(!m_app->trackActivityComputing(m0.id));
    QCOMPARE(ready2.count(), 0);

    // Ismeretlen meeting: üres, jel nélkül.
    QVERIFY(m_app->trackActivity(QStringLiteral("nincs-ilyen")).isEmpty());
    QVERIFY(!m_app->trackActivityComputing(QStringLiteral("nincs-ilyen")));
}

void TrackListeningAppTest::participantTracksSerialize()
{
    Participant p;
    p.id = "v1";
    p.personName = "Ádám";
    p.tracks = {"mic", "loop"};
    const Participant back = participantFromJson(toJson(p));
    QCOMPARE(back.tracks, p.tracks);
    Participant none;
    none.id = "v2";
    QVERIFY(!toJson(none).contains(QStringLiteral("tracks")));   // üresen nem írjuk ki
}

void TrackListeningAppTest::bindingBeforeTranscript()
{
    const Meeting m0 = makeMeeting();
    // Következtetett: a hangelemzés oldala (sides) → a bevont sávok.
    ParticipantTrackBinding b = m_app->participantTracks(m0.id, "v1");
    QVERIFY(!b.manual);
    QCOMPARE(b.trackIds, QStringList{"mic"});
    b = m_app->participantTracks(m0.id, "v2");
    QCOMPARE(b.trackIds, QStringList{"loop"});

    QSignalSpy changed(m_app.get(), &AppController::participantsChanged);
    // Ismeretlen sáv-id kimarad; a kötés a Participant::tracks-ba megy.
    QVERIFY(m_app->setParticipantTracks(m0.id, "v2", {"mic", "nincs", "loop"}));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(m_app->participants(m0.id).at(1).tracks, (QStringList{"mic", "loop"}));
    b = m_app->participantTracks(m0.id, "v2");
    QVERIFY(b.manual);
    QCOMPARE(b.trackIds, (QStringList{"mic", "loop"}));
    QVERIFY(!m_app->setParticipantTracks(m0.id, "v2", {"mic", "loop"}));   // nincs változás
    QCOMPARE(changed.count(), 1);

    // Üres = vissza a következtetésre.
    QVERIFY(m_app->setParticipantTracks(m0.id, "v2", {}));
    b = m_app->participantTracks(m0.id, "v2");
    QVERIFY(!b.manual);
    QCOMPARE(b.trackIds, QStringList{"loop"});

    // Ismeretlen résztvevő / beszélő: nincs mit kötni.
    QVERIFY(!m_app->setParticipantTracks(m0.id, "nincs-ilyen", {"mic"}));
}

void TrackListeningAppTest::bindingAfterTranscriptIsTheEditorAssignment()
{
    Meeting m = makeMeeting();
    writeTranscript(m);
    SpeakerEditor* ed = m_app->speakerEditor(m.id);
    QVERIFY(ed && ed->hasTranscript());

    // Résztvevő-id → a nyers beszélője (a "@loopback" utótag nélkül) a szerkesztőben.
    QVERIFY(m_app->setParticipantTracks(m.id, "v2", {"loop"}));
    QCOMPARE(ed->trackAssignment(QStringLiteral("Beszélő 2")), QStringList{"loop"});
    QCOMPARE(m_app->participants(m.id).at(1).tracks, QStringList{"loop"});
    ParticipantTrackBinding b = m_app->participantTracks(m.id, "v2");
    QVERIFY(b.manual);
    QCOMPARE(b.trackIds, QStringList{"loop"});

    // A szerkesztő („Miért ő?") felől módosítva ugyanazt látjuk — egy adat.
    QVERIFY(ed->setTrackAssignment(QStringLiteral("Beszélő 2"), {"mic"}));
    b = m_app->participantTracks(m.id, "v2");
    QCOMPARE(b.trackIds, QStringList{"mic"});
    // Visszavonás → a korábbi kötés.
    ed->undo();
    QCOMPARE(m_app->participantTracks(m.id, "v2").trackIds, QStringList{"loop"});

    // Beszélő-kulccsal is köthető (átirat utáni sor, résztvevő nélkül is); a hozzá tartozó
    // résztvevő is megkapja.
    QVERIFY(m_app->setParticipantTracks(m.id, QStringLiteral("Beszélő 1"), {"mic", "loop"}));
    QCOMPARE(ed->trackAssignment(QStringLiteral("Beszélő 1")), (QStringList{"mic", "loop"}));
    QCOMPARE(m_app->participants(m.id).at(0).tracks, (QStringList{"mic", "loop"}));
}

void TrackListeningAppTest::bindingCopiedToOverlayOnTranscript()
{
    Meeting m = makeMeeting();
    QVERIFY(m_app->setParticipantTracks(m.id, "v1", {"mic", "loop"}));
    // Jóváhagyás átirat előtt; az átirat érkezése (applyParticipantsToTranscript) hozza át.
    QVERIFY(m_app->approveParticipants(m.id, m_app->participants(m.id)));
    writeTranscript(m);
    // Az átirat-érkezés horga privát: a jóváhagyás újra-alkalmazása ugyanazt az utat járja.
    QVERIFY(m_app->approveParticipants(m.id, m_app->participants(m.id)));
    SpeakerEditor* ed = m_app->speakerEditor(m.id);
    QVERIFY(ed);
    QCOMPARE(ed->trackAssignment(QStringLiteral("Beszélő 1")), (QStringList{"mic", "loop"}));
    QCOMPARE(m_app->participantTracks(m.id, "v1").trackIds, (QStringList{"mic", "loop"}));
    QVERIFY(m_app->participantTracks(m.id, "v1").manual);
}

// Átirat előtti elemzés: a kézi sáv-kötés a párosítást (más sávon szóló klaszter nem lehet ő)
// és a bizonyítékot („kézzel: hívás") is meghatározza; az újraelemzés megtartja a kötést.
void TrackListeningAppTest::manualTracksSteerClusterMatchAndEvidence()
{
    Meeting m = makeMeeting();
    m.participants[1].tracks = {"loop"};            // Béla kézzel a hívás sávjára kötve
    m.participants[1].source = ParticipantSource::Voice;
    ClusterSet cs;
    VoiceCluster onMic;
    onMic.trackId = "mic";
    onMic.side = "mic";
    onMic.windows = {{0, 4000}};
    VoiceCluster onLoop = onMic;
    onLoop.trackId = "loop";
    onLoop.side = "loopback";
    onLoop.windows = {{8000, 12000}};
    cs.clusters = {onMic, onLoop};
    participants::CandidateContext ctx;
    // Mindkét klaszter Bélára hasonlít a legjobban (pl. visszhang a mikrofonon).
    ctx.rank = [](const EmbeddingSet&) { return QVector<VoiceMatch>{{QStringLiteral("Béla"), 0.8}}; };
    const QVector<Participant> out = participants::buildParticipants(cs, m, ctx);
    QCOMPARE(cs.clusters[0].matchName, QString());              // a mikrofon-klaszter nem ő
    QCOMPARE(cs.clusters[1].matchName, QStringLiteral("Béla"));
    const Participant* bela = nullptr;
    for (const Participant& p : out)
        if (p.personName == QStringLiteral("Béla")) bela = &p;
    QVERIFY(bela);
    QCOMPARE(bela->tracks, QStringList{"loop"});                 // az újraelemzés megtartja
    bool manualSide = false;
    for (const Evidence& e : bela->evidence)
        if (e.kind == EvidenceKind::Side && e.text == QStringLiteral("kézzel: hívás")
            && e.polarity == Polarity::Support)
            manualSide = true;
    QVERIFY(manualSide);

    // Az AppController a kötés váltásakor a bizonyítékot is frissíti.
    QVERIFY(m_app->setParticipantTracks(m.id, "v1", {"mic", "loop"}));
    bool both = false;
    for (const Evidence& e : m_app->participants(m.id).at(0).evidence)
        both = both || (e.kind == EvidenceKind::Side && e.text == QStringLiteral("kézzel: mikrofon és hívás"));
    QVERIFY(both);
}

QTEST_MAIN(TrackListeningAppTest)
#include "test_track_listening_app.moc"
