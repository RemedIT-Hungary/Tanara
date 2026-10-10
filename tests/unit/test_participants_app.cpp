//
// Résztvevők az AppController-ben: sáv-beszédarány szintetikus sávokkal (ffmpeg lavfi) és kizárás
// a lekeverésből, „Beemelem" / kézi kivétel; átirat előtti hangelemzés háttérszálon hamis
// embedderrel (a hang frekvenciája a „hang"), párosítás a lenyomatokkal; kötés a nyers
// beszélőkhöz; jóváhagyás → átnevezés egy visszavonási lépésben; „Ő nem volt ott"; „Csak én".
// IZOLÁLT TANARA_HOME, Soniox / LLM nélkül. ffmpeg nélkül QSKIP.
//
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/audio/MixdownPlan.h"
#include "tanara/edit/ParticipantAnalysis.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/edit/TrackSpeech.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/VoiceprintStore.h"
#include "tanara/voiceid/VoiceModelRegistry.h"

#include <memory>

using namespace tanara;

namespace {

const QString kCamp = QStringLiteral("campplus");

// Hamis „modell": a nullátmenetekből becsült frekvencia dönt (220 Hz → 0. dim, 440 Hz → 1. dim).
PcmEmbedderLoader fakeLoader()
{
    return [](const VoiceModelSpec& spec, const QString&, QString*) -> PcmEmbedder {
        const int dim = spec.dim;
        return [dim](const QVector<float>& pcm) {
            if (pcm.size() < 8000) return QVector<float>();
            int zc = 0;
            for (int i = 1; i < pcm.size(); ++i)
                if ((pcm[i - 1] < 0.0f) != (pcm[i] < 0.0f)) ++zc;
            const double hz = zc / 2.0 / (pcm.size() / 16000.0);
            QVector<float> v(dim, 0.0f);
            v[hz < 330.0 ? 0 : 1] = 1.0f;
            return v;
        };
    };
}

// 24 mp-es sáv: freq Hz-es szinusz, ahol `on` igaz (ffmpeg volume-kifejezés), máshol digitális csend.
bool makeTrack(const QString& path, int freq, const QString& onExpr)
{
    QProcess p;
    const QString filter = freq > 0
        ? QStringLiteral("sine=frequency=%1:duration=24,volume='if(%2,1,0)':eval=frame").arg(freq).arg(onExpr)
        : QStringLiteral("anullsrc=r=16000:cl=mono,atrim=duration=24");
    p.start(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i", filter, "-ar", "16000", "-ac", "1",
                                       "-y", path});
    return p.waitForFinished(30000) && p.exitCode() == 0 && QFileInfo::exists(path);
}

Voiceprint print(int hot)
{
    Voiceprint vp;
    vp.model = kCamp;
    vp.embedding = QVector<float>(VoiceModelRegistry::spec(kCamp)->dim, 0.0f);
    vp.embedding[hot] = 1.0f;
    vp.sampleRef = QStringLiteral("x.wav#0-1000");
    return vp;
}

const Participant* byName(const QVector<Participant>& ps, const QString& name)
{
    for (const Participant& p : ps)
        if (p.personName == name) return &p;
    return nullptr;
}

} // namespace

class ParticipantsAppTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();

    void speechCheckExcludesSilentTrack();
    void analysisApproveUndoUnbindSolo();
    void manualParticipantKeptThroughAnalysis();

private:
    Meeting makeMeeting(bool withSilent);
    void writeTranscript(Meeting& m, const QVector<std::tuple<qint64, qint64, QString>>& rows);

    bool m_haveFfmpeg = false;
    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<AppController> m_app;
};

void ParticipantsAppTest::initTestCase()
{
    m_haveFfmpeg = !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();
    qputenv("TANARA_CLOUD", "off");
}

void ParticipantsAppTest::init()
{
    if (!m_haveFfmpeg) QSKIP("nincs ffmpeg");
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    qputenv("TANARA_HOME", m_home->path().toUtf8());
    const QString f = QDir(m_home->path()).filePath("models/" + VoiceModelRegistry::spec(kCamp)->fileName);
    QDir().mkpath(QFileInfo(f).absolutePath());
    QFile file(f);
    QVERIFY(file.open(QIODevice::WriteOnly));   // helyőrző: a hamis betöltő nem olvassa
    file.close();
    m_app = std::make_unique<AppController>();
    QVERIFY(m_app->store()->metadataDir().startsWith(m_home->path()));
    m_app->setVoiceEmbedderLoader(fakeLoader());
    m_app->settings()->setEnabledVoiceModels({kCamp});
    QVERIFY(m_app->voiceIdentificationAvailable());
    m_app->setUserSpeakerName(QStringLiteral("Ádám"));
}

void ParticipantsAppTest::cleanup()
{
    m_app.reset();
    m_home.reset();
}

// Mikrofon: 220 Hz 0–8 és 16–24 mp; loopback: 440 Hz 8–16 mp; (opcionálisan) néma harmadik sáv.
Meeting ParticipantsAppTest::makeMeeting(bool withSilent)
{
    Meeting m = m_app->store()->createMeeting(QStringLiteral("Résztvevők"));
    const QDir dir(m.folder);
    [&] { QVERIFY(makeTrack(dir.filePath("track_mic.wav"), 220, "not(between(t,8,16))")); }();
    [&] { QVERIFY(makeTrack(dir.filePath("track_loop.wav"), 440, "between(t,8,16)")); }();
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
    if (withSilent) {
        [&] { QVERIFY(makeTrack(dir.filePath("track_quiet.wav"), 0, {})); }();
        add("quiet", TrackKind::Loopback, "track_quiet.wav");
    }
    m.durationMs = 24000;
    m_app->store()->saveMeeting(m);
    return m;
}

void ParticipantsAppTest::writeTranscript(Meeting& m, const QVector<std::tuple<qint64, qint64, QString>>& rows)
{
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

void ParticipantsAppTest::speechCheckExcludesSilentTrack()
{
    const Meeting m0 = makeMeeting(/*withSilent*/ true);
    QSignalSpy tracks(m_app.get(), &AppController::tracksChanged);
    QVERIFY(m_app->checkTrackSpeech(m0.id));
    QVERIFY(tracks.wait(30000));

    Meeting m = m_app->store()->load(m0.id);
    QCOMPARE(m.tracks.size(), 3);
    QVERIFY(m.tracks[0].included());
    QVERIFY(qAbs(m.tracks[0].speechRatio - 2.0 / 3.0) < 0.05);
    QVERIFY(m.tracks[1].included());
    QVERIFY(qAbs(m.tracks[1].speechRatio - 1.0 / 3.0) < 0.05);
    QCOMPARE(m.tracks[2].excludedReason, trackspeech::kNoSpeech);
    QVERIFY(!m.tracks[2].active);
    QCOMPARE(m.tracks[2].speechRatio, 0.0);
    QCOMPARE(MixdownPlan::fromMeeting(m).inputs.size(), 2);
    QVERIFY(!m_app->checkTrackSpeech(m0.id));   // már mérve

    // „Beemelem" → vissza a lekeverésbe (újrakeverés indul).
    QSignalSpy mixed(m_app.get(), &AppController::mixdownUpdated);
    m_app->includeTrack(m0.id, "quiet");
    m = m_app->store()->load(m0.id);
    QVERIFY(m.tracks[2].included());
    QCOMPARE(MixdownPlan::fromMeeting(m).inputs.size(), 3);
    QVERIFY(mixed.wait(30000));
    QVERIFY(mixed.last().at(1).toBool());

    // Kézi kivétel.
    m_app->excludeTrack(m0.id, "loop");
    m = m_app->store()->load(m0.id);
    QCOMPARE(m.tracks[1].excludedReason, trackspeech::kManual);
    QCOMPARE(MixdownPlan::fromMeeting(m).inputs.size(), 2);
    QVERIFY(mixed.wait(30000));
}

void ParticipantsAppTest::analysisApproveUndoUnbindSolo()
{
    Meeting m = makeMeeting(/*withSilent*/ false);
    m_app->voiceprints()->addPrint(QStringLiteral("Ádám"), print(0));
    m_app->voiceprints()->addPrint(QStringLiteral("Béla"), print(1));

    QSignalSpy done(m_app.get(), &AppController::participantAnalysisFinished);
    QVERIFY(m_app->analyzeParticipants(m.id));
    QVERIFY(m_app->participantAnalysisRunning(m.id));
    QVERIFY(!m_app->analyzeParticipants(m.id));   // már fut
    QVERIFY(done.wait(60000));
    QVERIFY(!m_app->participantAnalysisRunning(m.id));

    QVector<Participant> ps = m_app->participants(m.id);
    QCOMPARE(ps.size(), 2);
    const Participant* adam = byName(ps, QStringLiteral("Ádám"));
    const Participant* bela = byName(ps, QStringLiteral("Béla"));
    QVERIFY(adam && bela);
    QCOMPARE(adam->sides, QStringList({participants::kSideMic}));
    QCOMPARE(bela->sides, QStringList({participants::kSideLoopback}));
    QCOMPARE(participants::participantGroup(*adam), ParticipantGroup::Sure);    // hang + saját mikrofon
    QCOMPARE(participants::participantGroup(*bela), ParticipantGroup::Doubt);   // csak hang
    QVERIFY(QFile::exists(ParticipantAnalysisFile::filePath(m.folder)));
    QVERIFY(m_app->participantApprovalPending(m.id));
    const QString adamId = adam->id, belaId = bela->id;

    // Az átirat később jön: a kötés időátfedéssel.
    writeTranscript(m, {{1000, 7000, "Beszélő 1"}, {9000, 15000, "Beszélő 2"}, {17000, 23000, "Beszélő 1"}});
    QSignalSpy changed(m_app.get(), &AppController::participantsChanged);
    QVERIFY(m_app->bindRawSpeakers(m.id));
    QCOMPARE(changed.count(), 1);
    ps = m_app->participants(m.id);
    QCOMPARE(byName(ps, "Ádám")->rawSpeakerIds, QStringList({"Beszélő 1"}));
    QCOMPARE(byName(ps, "Béla")->rawSpeakerIds, QStringList({"Beszélő 2"}));

    // Jóváhagyás → a nyers beszélők a nevekre, egy visszavonási lépésben.
    QVERIFY(m_app->approveParticipants(m.id, ps));
    m = m_app->store()->load(m.id);
    QCOMPARE(m.speakerMap.value("Beszélő 1"), QStringLiteral("Ádám"));
    QCOMPARE(m.speakerMap.value("Beszélő 2"), QStringLiteral("Béla"));
    QVERIFY(m.approval.has_value());
    QCOMPARE(m.approval->candidates.size(), 2);
    QVERIFY(!m_app->participantApprovalPending(m.id));
    SpeakerEditor* ed = m_app->speakerEditor(m.id);
    QVERIFY(ed && ed->canUndo());
    ed->undo();
    m = m_app->store()->load(m.id);
    QVERIFY(m.speakerMap.isEmpty());
    QVERIFY(!ed->canUndo());   // egyetlen lépés volt
    ed->redo();
    QCOMPARE(m_app->store()->load(m.id).speakerMap.size(), 2);

    // „Ő nem volt ott": Béla sorai névtelenek, a jelölése törlődik.
    QVERIFY(m_app->unbindParticipant(m.id, belaId));
    m = m_app->store()->load(m.id);
    QVERIFY(!m.speakerMap.contains("Beszélő 2"));
    QCOMPARE(m.speakerMap.value("Beszélő 1"), QStringLiteral("Ádám"));
    QVERIFY(!byName(m.participants, "Béla")->approved);
    QVERIFY(byName(m.participants, "Ádám")->approved);

    // „Csak én beszéltem": minden nyers beszélő a saját névre (egy beszélővé vonva).
    QVERIFY(m_app->setSoloMeeting(m.id));
    m = m_app->store()->load(m.id);
    QVERIFY(m.approval && m.approval->solo);
    int named = 0;
    for (const EditorSpeaker& s : ed->speakers())
        if (s.utteranceCount > 0) {
            QCOMPARE(s.personName, QStringLiteral("Ádám"));
            ++named;
        }
    QCOMPARE(named, 1);
    QVERIFY(!m_app->analyzeParticipants(m.id));   // „csak én" után nincs azonosítás
    Q_UNUSED(adamId);
}

void ParticipantsAppTest::manualParticipantKeptThroughAnalysis()
{
    const Meeting m = makeMeeting(/*withSilent*/ false);
    m_app->voiceprints()->addPrint(QStringLiteral("Béla"), print(1));
    const QString id = m_app->addParticipant(m.id, QStringLiteral("Cecília"));
    QVERIFY(id.startsWith("m-"));
    QCOMPARE(m_app->addParticipant(m.id, QStringLiteral("cecília")), id);   // már megvan
    QSignalSpy done(m_app.get(), &AppController::participantAnalysisFinished);
    QVERIFY(m_app->analyzeParticipants(m.id));
    QVERIFY(done.wait(60000));
    const QVector<Participant> ps = m_app->participants(m.id);
    QCOMPARE(ps.size(), 3);   // Béla, Cecília (kézi), az ismeretlen mikrofon-hang
    const Participant* cili = byName(ps, QStringLiteral("Cecília"));
    QVERIFY(cili);
    QCOMPARE(cili->id, id);
    QCOMPARE(cili->source, ParticipantSource::Manual);
    bool noPrint = false;
    for (const Evidence& e : cili->evidence)
        if (e.kind == EvidenceKind::Voice && e.text == QStringLiteral("nincs lenyomata")) noPrint = true;
    QVERIFY(noPrint);
    QVERIFY(byName(ps, QString()));   // ismeretlen hang (az Ádám-lenyomat itt nincs meg)

    // „Kihagyás": döntés kötés nélkül.
    QVERIFY(m_app->skipApproval(m.id));
    QVERIFY(!m_app->participantApprovalPending(m.id));
    QVERIFY(m_app->store()->load(m.id).approval->skipped);
}

QTEST_GUILESS_MAIN(ParticipantsAppTest)
#include "test_participants_app.moc"
