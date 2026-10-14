// processing::VoiceAnalysisService on its own (without AppController): the speaker models in use, the
// pre-transcript participant analysis on the analysis pool, the synchronous participant guesses and the
// asynchronous identification of a transcript's speakers (matching through the IParticipantIdentifier
// port's local rule), cancelling, the track activity of the speech strip, the voiceprint backfill's skip
// report and enrollment from a sample. Synthetic tracks (ffmpeg lavfi) and a fake embedder (the audio's
// frequency is the "voice"). Made-up names, ISOLATED TANARA_HOME; no Soniox / LLM / network. Without
// ffmpeg QSKIP.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/media/FfmpegMediaTool.h"
#include "tanara/processing/analysis/LocalParticipantIdentifier.h"
#include "tanara/processing/analysis/VoiceAnalysisService.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <QRandomGenerator>

#include <memory>

using namespace tanara;
using processing::VoiceAnalysisService;

namespace {

const QString kCamp = QStringLiteral("campplus");

// Fake "model": the frequency estimated from zero crossings decides (220 Hz is dim 0, 440 Hz is dim 1).
processing::PcmEmbedderLoader fakeLoader()
{
    return [](const processing::VoiceModelSpec& spec, const QString&, QString*) -> processing::PcmEmbedder {
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

// A 24 s track: a sine of freq Hz where `on` is true (an ffmpeg volume expression), digital silence elsewhere.
bool makeTrack(const QString& path, int freq, const QString& onExpr)
{
    QProcess p;
    const QString filter =
        QStringLiteral("sine=frequency=%1:duration=24,volume='if(%2,1,0)':eval=frame").arg(freq).arg(onExpr);
    p.start(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i", filter, "-ar", "16000", "-ac", "1",
                                       "-y", path});
    return p.waitForFinished(30000) && p.exitCode() == 0 && QFileInfo::exists(path);
}

Voiceprint print(int hot, const QString& model = kCamp)
{
    Voiceprint vp;
    vp.model = model;
    vp.embedding = QVector<float>(processing::VoiceModelRegistry::spec(kCamp)->dim, 0.0f);
    vp.embedding[hot] = 1.0f;
    vp.dim = vp.embedding.size();
    vp.sampleRef = QStringLiteral("x.wav#0-1000");
    return vp;
}

} // namespace

class VoiceAnalysisServiceTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();

    void modelsAndErrorPaths();
    void analysisAndGuesses();
    void identifyAsyncAndCancel();
    void trackActivityComputedOnce();
    void backfillReportsSkips();
    void enrollFromSample();
    void matchingRuleEqualsStore();

private:
    Meeting makeMeeting();
    void writeTranscript(Meeting& m, const QVector<std::tuple<qint64, qint64, QString, QString>>& rows);
    std::unique_ptr<VoiceAnalysisService> makeService(bool withModel = true);

    bool m_haveFfmpeg = false;
    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<library::MeetingRepository> m_store;
    std::unique_ptr<library::PeopleStore> m_people;
    std::unique_ptr<library::VoiceprintStore> m_prints;
    std::unique_ptr<library::MeetingJobTracker> m_jobs;
    QString m_stamp = QStringLiteral("t1");
};

void VoiceAnalysisServiceTest::initTestCase()
{
    m_haveFfmpeg = !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();
}

void VoiceAnalysisServiceTest::init()
{
    if (!m_haveFfmpeg) QSKIP("nincs ffmpeg");
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    qputenv("TANARA_HOME", m_home->path().toUtf8());
    const QDir home(m_home->path());
    const QString f = home.filePath("models/" + processing::VoiceModelRegistry::spec(kCamp)->fileName);
    QDir().mkpath(QFileInfo(f).absolutePath());
    QFile file(f);
    QVERIFY(file.open(QIODevice::WriteOnly));   // placeholder: the fake loader does not read it
    file.close();
    QDir().mkpath(home.filePath("rec"));
    QDir().mkpath(home.filePath("meta"));
    m_store = std::make_unique<library::MeetingRepository>(home.filePath("rec"), home.filePath("meta"));
    m_people = std::make_unique<library::PeopleStore>(home.filePath("meta/people.json"));
    m_prints = std::make_unique<library::VoiceprintStore>(home.filePath("meta/voiceprints.json"));
    m_jobs = std::make_unique<library::MeetingJobTracker>(m_store.get());
    m_stamp = QStringLiteral("t1");
}

void VoiceAnalysisServiceTest::cleanup()
{
    m_jobs.reset();
    m_prints.reset();
    m_people.reset();
    m_store.reset();
    m_home.reset();
}

std::unique_ptr<VoiceAnalysisService> VoiceAnalysisServiceTest::makeService(bool withModel)
{
    VoiceAnalysisService::Deps deps;
    deps.store = m_store.get();
    deps.voiceprints = m_prints.get();
    deps.people = m_people.get();
    deps.jobs = m_jobs.get();
    deps.media = media::FfmpegMediaTool::fromSettings({});
    // Without a model: an empty models folder (the enabled list falls back to the base model otherwise).
    deps.modelsDir = QDir(m_home->path()).filePath(withModel ? "models" : "nomodels");
    deps.enabledModels = []() { return QStringList{kCamp}; };
    deps.self = [this]() { SelfPerson me; me.id = m_people->selfId(); me.name = QStringLiteral("Ádám"); return me; };
    deps.transcriptLines = [](const QString& folder) { return editing::speakeredit::loadTranscriptLines(folder); };
    deps.transcriptStamp = [this](const QString&, const QString&) { return m_stamp; };
    deps.speechCheckPending = [](const Meeting&) { return false; };
    auto svc = std::make_unique<VoiceAnalysisService>(deps);
    svc->setLoader(fakeLoader());
    svc->applyModels();
    return svc;
}

// Microphone: 220 Hz at 0-8 and 16-24 s; loopback: 440 Hz at 8-16 s.
Meeting VoiceAnalysisServiceTest::makeMeeting()
{
    Meeting m = m_store->createMeeting(QStringLiteral("Hangelemzés"));
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
    m.durationMs = 24000;
    m_store->saveMeeting(m);
    return m;
}

// rows: start, end, raw speaker, the token's track.
void VoiceAnalysisServiceTest::writeTranscript(Meeting& m,
                                               const QVector<std::tuple<qint64, qint64, QString, QString>>& rows)
{
    QJsonArray toks, segs;
    for (const auto& [s, e, spk, track] : rows) {
        toks.append(QJsonObject{{"text", "x"}, {"speaker", spk}, {"startMs", double(s)}, {"endMs", double(e)},
                                {"confidence", 1.0}, {"trackId", track}});
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
    m = m_store->load(m.id);
    m.hasTranscript = true;
    m_store->saveMeeting(m);
}

// Without a usable model nothing starts (and nothing hangs); the model set is applied once.
void VoiceAnalysisServiceTest::modelsAndErrorPaths()
{
    auto none = makeService(/*withModel*/ false);
    QVERIFY(!none->usable());
    QVERIFY(none->modelIds().isEmpty());
    QVERIFY(!none->utteranceEmbedderFactory());
    const Meeting m = makeMeeting();
    QVERIFY(!none->analyzeParticipants(m.id));
    QVERIFY(!none->maybeAnalyzeParticipants(m.id));
    QVERIFY(!none->startIdentify(m.id, false));
    QVERIFY(none->identifyParticipants(m.id).isEmpty());
    QVERIFY(!none->enrollSpeaker(m.id, QStringLiteral("S1"), m_people->ensure(QStringLiteral("Béla"))));
    QCOMPARE(none->testSpeakerMatch(m.id, QStringLiteral("S1")).score, -1.0);
    QSignalSpy finished(none.get(), &VoiceAnalysisService::backfillFinished);
    none->backfill();
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.first().at(0).toInt(), 0);

    auto svc = makeService();
    QVERIFY(svc->usable());
    QCOMPARE(svc->modelIds(), QStringList{kCamp});
    QCOMPARE(svc->editorModelIds(), QStringList{kCamp});
    QVERIFY(svc->utteranceEmbedderFactory());
    QVERIFY(!svc->applyModels().changed);   // the same set: no change
    // Unknown meeting / nothing to cancel.
    QVERIFY(!svc->analyzeParticipants(QStringLiteral("nope")));
    QVERIFY(!svc->startIdentify(QStringLiteral("nope"), false));
    QVERIFY(!svc->cancel(m.id, JobKind::Identify));
    QVERIFY(!svc->cancel(m.id, JobKind::Mixdown));
    QVERIFY(!svc->startIdentify(m.id, false));   // no transcript
}

// The main path: the analysis on the pool (candidates with the matched people, saved to the meeting and the
// analysis file; not repeated on the same tracks), and the synchronous guesses through the identifier port.
void VoiceAnalysisServiceTest::analysisAndGuesses()
{
    auto svc = makeService();
    const Meeting m = makeMeeting();
    const PersonId adam = m_people->ensure(QStringLiteral("Ádám"));
    const PersonId bela = m_people->ensure(QStringLiteral("Béla"));
    m_prints->addPrint(adam, print(0));
    m_prints->addPrint(bela, print(1));

    QSignalSpy started(svc.get(), &VoiceAnalysisService::participantAnalysisStarted);
    QSignalSpy done(svc.get(), &VoiceAnalysisService::participantAnalysisFinished);
    QSignalSpy changed(svc.get(), &VoiceAnalysisService::participantsChanged);
    QVERIFY(svc->analyzeParticipants(m.id));
    QCOMPARE(started.count(), 1);
    QVERIFY(svc->analysisRunning(m.id));
    QVERIFY(!svc->analyzeParticipants(m.id));   // already running
    QVERIFY(done.wait(60000));
    QVERIFY(!svc->analysisRunning(m.id));
    QCOMPARE(changed.count(), 1);
    const QVector<Participant> ps = m_store->load(m.id).participants;
    QCOMPARE(ps.size(), 2);
    QSet<PersonId> who;
    for (const Participant& p : ps) who.insert(p.personId);
    QCOMPARE(who, (QSet<PersonId>{adam, bela}));
    QVERIFY(QFile::exists(processing::ParticipantAnalysisFile::filePath(m.folder)));
    QVERIFY(!svc->maybeAnalyzeParticipants(m.id));   // it already ran on these same tracks

    const QVector<ParticipantGuess> guesses = svc->identifyParticipants(m.id);
    QVERIFY(guesses.size() >= 2);
    for (int i = 1; i < guesses.size(); ++i)
        QVERIFY(guesses.at(i - 1).windows >= guesses.at(i).windows);   // the most windows first
    QCOMPARE(guesses.at(0).trackId, QStringLiteral("mic"));
    QCOMPARE(guesses.at(0).personId, adam);
    QCOMPARE(guesses.at(0).displayName, QStringLiteral("Ádám"));
    QVERIFY(guesses.at(0).score > 0.9);
    bool belaOnLoop = false;
    for (const ParticipantGuess& g : guesses)
        if (g.trackId == QStringLiteral("loop") && g.personId == bela) {
            belaOnLoop = g.sampleRef.startsWith(QStringLiteral("track_loop.wav#")) && g.displayName == QStringLiteral("Béla");
        }
    QVERIFY(belaOnLoop);

    // Cancelled at the first window: nothing yet.
    QVERIFY(svc->identifyParticipants(m.id, [](int, int) { return false; }).isEmpty());
}

// The asynchronous identification of a transcript's anonymous speakers: the matches go out on
// speakersIdentified and the Identify job closes; a cancelled run closes as cancelled (no error); a run whose
// transcript changed meanwhile hands over nothing.
void VoiceAnalysisServiceTest::identifyAsyncAndCancel()
{
    auto svc = makeService();
    Meeting m = makeMeeting();
    const PersonId adam = m_people->ensure(QStringLiteral("Ádám"));
    const PersonId bela = m_people->ensure(QStringLiteral("Béla"));
    m_prints->addPrint(adam, print(0));
    m_prints->addPrint(bela, print(1));
    writeTranscript(m, {{1000, 7000, "S1", "mic"}, {9000, 15000, "S2", "loop"}, {17000, 23000, "S1", "mic"}});

    // The synchronous counterpart and the single-speaker probe.
    QCOMPARE(svc->testSpeakerMatch(m.id, QStringLiteral("S2")).personId, bela);
    const QVector<SpeakerVoiceMatch> sync = svc->identifyTranscriptSpeakers(m.id);
    QCOMPARE(sync.size(), 2);
    QCOMPARE(sync.at(0).rawLabel, QStringLiteral("S1"));
    QCOMPARE(sync.at(0).personId, adam);
    QCOMPARE(sync.at(1).personId, bela);

    QSignalSpy identified(svc.get(), &VoiceAnalysisService::speakersIdentified);
    QVERIFY(svc->startIdentify(m.id, /*asStage*/ false));
    QVERIFY(svc->identifyRunning(m.id));
    QVERIFY(m_jobs->isRunning(m.id, JobKind::Identify));
    QVERIFY(!svc->startIdentify(m.id, false));   // already running
    QTRY_VERIFY_WITH_TIMEOUT(!svc->identifyRunning(m.id), 30000);
    QCOMPARE(identified.count(), 1);
    const auto matches = identified.first().at(1).value<QVector<SpeakerVoiceMatch>>();
    QCOMPARE(matches.size(), 2);
    QVERIFY(!m_jobs->isRunning(m.id, JobKind::Identify));
    QVERIFY(!m_jobs->lastError(m.id, JobKind::Identify).isValid());

    // Cancel: the stage form is not this run's kind; the own job is cancelled.
    identified.clear();
    QVERIFY(svc->startIdentify(m.id, false));
    QVERIFY(!svc->cancel(m.id, JobKind::Transcribe));
    QVERIFY(svc->cancel(m.id, JobKind::Identify));
    QTRY_VERIFY_WITH_TIMEOUT(!svc->identifyRunning(m.id), 30000);
    QVERIFY(!m_jobs->isRunning(m.id, JobKind::Identify));
    QVERIFY(!m_jobs->lastError(m.id, JobKind::Identify).isValid());   // a cancel leaves no error

    // The transcript changed during the run: the result belongs to the old labels and is dropped.
    identified.clear();
    QVERIFY(svc->startIdentify(m.id, false));
    m_stamp = QStringLiteral("t2");
    QTRY_VERIFY_WITH_TIMEOUT(!svc->identifyRunning(m.id), 30000);
    QCOMPARE(identified.count(), 0);
}

// The speech strip: the first request starts the computation (pending result), the ready signal follows, then
// the cached activity comes back at once.
void VoiceAnalysisServiceTest::trackActivityComputedOnce()
{
    auto svc = makeService();
    const Meeting m = makeMeeting();
    QSignalSpy ready(svc.get(), &VoiceAnalysisService::trackActivityReady);
    const MeetingActivity pending = svc->trackActivity(m.id);
    QVERIFY(pending.tracks.isEmpty());
    QVERIFY(!pending.fingerprint.isEmpty());
    QVERIFY(svc->trackActivityComputing(m.id));
    QVERIFY(ready.wait(30000));
    QVERIFY(!svc->trackActivityComputing(m.id));
    QCOMPARE(svc->trackActivity(m.id).tracks.size(), 2);
    QCOMPARE(ready.count(), 1);
    QVERIFY(svc->trackActivity(QStringLiteral("nope")).tracks.isEmpty());

    // The speech check's measurement on the same pool.
    MeetingActivity measured;
    bool called = false;
    svc->measureActivity(m, [&](const MeetingActivity& a) { measured = a; called = true; });
    QTRY_VERIFY_WITH_TIMEOUT(called, 30000);
    QCOMPARE(measured.tracks.size(), 2);
}

// The backfill's error paths: a sample whose meeting is gone and one whose audio is missing are skipped and
// reported (sorted by person); nothing is added.
void VoiceAnalysisServiceTest::backfillReportsSkips()
{
    auto svc = makeService();
    const Meeting m = makeMeeting();
    const PersonId bela = m_people->ensure(QStringLiteral("Béla"));
    const PersonId anna = m_people->ensure(QStringLiteral("Anna"));
    Voiceprint gone = print(1, QStringLiteral("other"));
    gone.sourceMeetingId = QStringLiteral("gone");
    m_prints->addPrint(bela, gone);
    Voiceprint missing = print(0, QStringLiteral("other"));
    missing.sourceMeetingId = m.id;
    missing.sampleRef = QStringLiteral("nincs.wav#0-3000");
    m_prints->addPrint(anna, missing);

    QSignalSpy started(svc.get(), &VoiceAnalysisService::backfillStarted);
    QSignalSpy finished(svc.get(), &VoiceAnalysisService::backfillFinished);
    svc->backfill();
    QVERIFY(svc->backfillRunning());
    QVERIFY(finished.wait(30000));
    QVERIFY(!svc->backfillRunning());
    QCOMPARE(started.first().at(0).toInt(), 1);   // the missing meeting is not even queued
    QCOMPARE(finished.first().at(0).toInt(), 0);
    QCOMPARE(finished.first().at(1).toInt(), 2);
    const QVector<processing::VoiceprintBackfillSkip> skips = svc->backfillSkipped();
    QCOMPARE(skips.size(), 2);
    QCOMPARE(skips.at(0).person, QStringLiteral("Anna"));
    QCOMPARE(skips.at(0).reason, processing::VoiceprintBackfillSkip::Reason::MissingAudio);
    QCOMPARE(skips.at(1).reason, processing::VoiceprintBackfillSkip::Reason::MissingMeeting);
}

// A voiceprint from an arbitrary span of a track, for a person created on the spot.
void VoiceAnalysisServiceTest::enrollFromSample()
{
    auto svc = makeService();
    const Meeting m = makeMeeting();
    QSignalSpy prints(svc.get(), &VoiceAnalysisService::voiceprintsChanged);
    QSignalSpy people(svc.get(), &VoiceAnalysisService::peopleChanged);
    QVERIFY(svc->enrollFromSample(PersonChoice::create(QStringLiteral("Cecília")), m.id, QStringLiteral("nope"),
                                  0, 3000).isNull());   // unknown track
    const PersonId cili = svc->enrollFromSample(PersonChoice::create(QStringLiteral("Cecília")), m.id,
                                                QStringLiteral("loop"), 9000, 13000);
    QVERIFY(!cili.isNull());
    QCOMPARE(prints.count(), 1);
    QCOMPARE(people.count(), 1);
    const QVector<Voiceprint> got = m_prints->printsFor(cili);
    QCOMPARE(got.size(), 1);
    QCOMPARE(got.first().sampleRef, QStringLiteral("track_loop.wav#9000-13000"));
    QCOMPARE(got.first().embedding.value(1), 1.0f);   // 440 Hz
}

// The identification moved from VoiceprintStore::bestMatch to the IParticipantIdentifier port's local rule
// (LocalParticipantIdentifier): with the store's people as the candidates and the probe's vectors in the
// normalized model order, it picks the same person with the same score (random stores, two models, people
// missing a model, below and above the threshold).
void VoiceAnalysisServiceTest::matchingRuleEqualsStore()
{
    QRandomGenerator rng(20261014);
    const QStringList models{QStringLiteral("campplus"), QStringLiteral("eres2net")};
    auto randomVec = [&rng]() {
        QVector<float> v(8);
        for (float& x : v) x = float(rng.generateDouble() * 2.0 - 1.0);
        return library::VoiceprintStore::l2normalize(v);
    };
    for (int trial = 0; trial < 200; ++trial) {
        library::VoiceprintStore store(QDir(m_home->path()).filePath(QStringLiteral("meta/rule-%1.json").arg(trial)));
        for (int p = 0; p < 5; ++p) {
            const PersonId person = PersonId::generate();
            for (const QString& model : models) {
                if (rng.bounded(4) == 0) continue;   // this person has no voiceprint of this model
                for (int k = 0; k < 1 + int(rng.bounded(3)); ++k) {
                    Voiceprint vp;
                    vp.model = model;
                    vp.embedding = randomVec();
                    vp.dim = vp.embedding.size();
                    vp.sampleRef = QStringLiteral("x.wav#%1-%2").arg(k * 1000).arg(k * 1000 + 900);
                    store.addPrint(person, vp);
                }
            }
        }
        EmbeddingSet query;
        for (const QString& model : models)
            if (rng.bounded(5) != 0) query.insert(model, randomVec());
        const VoiceMatch expected = store.bestMatch(query, query.keys());

        providers::IdentifyRequest req;
        providers::IdentifyProbe probe;
        for (const QString& id : voicemodels::normalizeIds(query.keys()))
            if (!query.value(id).isEmpty()) probe.vectors.append({query.value(id), 8, id, 0});
        req.probes = {probe};
        for (const PersonId& person : store.people()) req.candidates.append({QString(), store.printsFor(person), person});
        req.threshold = -1.0;   // the default (VoiceAnalysisService::kVoiceMatchThreshold)
        const ParticipantGuess g = processing::LocalParticipantIdentifier::identifyNow(req).value(0);
        QCOMPARE(processing::LocalParticipantIdentifier::kDefaultThreshold, VoiceAnalysisService::kVoiceMatchThreshold);
        QVERIFY2(qFuzzyCompare(1.0 + g.score, 1.0 + expected.score), qPrintable(QString::number(trial)));
        const PersonId expectedPerson = expected.score >= VoiceAnalysisService::kVoiceMatchThreshold
            ? expected.personId : PersonId();
        QCOMPARE(g.personId, expectedPerson);
    }
}

QTEST_MAIN(VoiceAnalysisServiceTest)
#include "test_voice_analysis_service.moc"
