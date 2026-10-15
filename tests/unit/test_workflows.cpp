// app::Workflows on its own (without AppController): the transcribe -> (mixdown) -> STT -> identify chain as a
// state machine over the meeting's Transcribe job, with a fake media tool (the mix), a fake STT provider and
// fake voice steps. The chain with and without a mix, cancel at every step, a failure in the middle, a
// restart, the "Who was there?" decision and the identification toggle, the readiness block, the
// re-transcription, the standalone identification, the post-recording / import chains and the speech check.
// The local diarization step (D4): after a provider without speakers (FakeDiarizer) the transcript gets the
// speakers before the identification; a diarizing provider skips it; cancel in the diarize stage; the setting
// (and the per-run override) off; a failing diarizer does not stop the chain.
// Isolated TANARA_HOME; no network, no ffmpeg.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "fake_diarizer.h"
#include "fake_media_tool.h"
#include "fake_stt.h"

#include "tanara/app/Workflows.h"
#include "tanara/library/jobs/JobStats.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/processing/diarization/DiarizationMerge.h"
#include "tanara/processing/diarization/DiarizationService.h"
#include "tanara/processing/stt/TranscriptionService.h"
#include "tanara/recording/MixdownService.h"

#include <memory>

using namespace tanara;
using app::Workflows;

class WorkflowsTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void chainWithMixdownToIdentify();
    void chainWithoutMixdown();
    void cancelDuringMixdown();
    void cancelDuringStt();
    void cancelDuringIdentify();
    void mixdownFailureStopsChain();
    void sttFailureThenRestart();
    void decisionAndToggleSkipIdentify();
    void readinessBlocksStart();
    void retranscribeClearsNamesOnlyOnSuccess();
    void standaloneIdentify();
    void cancelOtherKindsAndAll();
    void afterRecordingAndImport();
    void speechCheckOncePerProcess();
    void localDiarizationAfterWhisper();
    void providerDiarizedSkipsLocal();
    void cancelDuringDiarize();
    void diarizationOffOrFailing();
    void manualDiarizeSpeakers();
    void manualDiarizeFailureAndGates();
    void askRouteResolution();

private:
    Meeting recorded(const QString& title, bool freshMix = false, bool unmeasured = false);
    void runToStt(const QString& id);
    JobOutcome outcome(const QString& id) const { return m_outcomes.value(id, JobOutcome::Done); }
    void makeWorkflows();

    std::unique_ptr<QTemporaryDir> m_home;
    std::shared_ptr<testing::FakeMediaTool> m_media;
    std::unique_ptr<library::MeetingRepository> m_store;
    std::unique_ptr<library::MeetingJobTracker> m_jobs;
    std::unique_ptr<library::JobStats> m_stats;
    std::unique_ptr<recording::MixdownService> m_mix;
    providers::ProviderRegistry m_registry;
    std::shared_ptr<testing::FakeSttLog> m_log;
    std::unique_ptr<testing::FakeTranscriptionPort> m_port;
    std::unique_ptr<processing::TranscriptionService> m_stt;
    std::unique_ptr<processing::DiarizationService> m_diar;
    std::unique_ptr<Workflows> m_wf;

    // The fake routes (S3): the stored stt / llm route, the cloud, the Ask answerer.
    providers::Route m_sttRoute = providers::Route::External;
    bool m_cloudLive = false;
    int m_asked = 0;
    std::optional<providers::Route> m_answer;

    // The fake local diarization (the diarization route Local = on).
    bool m_diarEnabled = false;
    bool m_diarUsable = true;
    bool m_diarManual = false;
    int m_diarCreated = 0;
    QPointer<testing::FakeDiarizer> m_fakeDiar;
    std::function<providers::DiarizeResult(const providers::DiarizeRequest&)> m_diarScript;

    // The fake voice side.
    bool m_usable = true;
    bool m_decided = false;
    bool m_identifyStarts = true;
    providers::ReadinessResult m_ready;
    QString m_mixdownMode = QStringLiteral("auto");
    QStringList m_calls;   // "identify:<id>:<asStage>", "cancelIdentify:<kind>", "apply", "analyze", "measure"
    std::function<void(const MeetingActivity&)> m_measureDone;
    QStringList m_errors;
    QHash<QString, JobOutcome> m_outcomes;
    int n = 0;
};

void WorkflowsTest::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    qputenv("TANARA_HOME", m_home->path().toUtf8());
    m_media = std::make_shared<testing::FakeMediaTool>();
    m_store = std::make_unique<library::MeetingRepository>(m_home->filePath(QStringLiteral("rec")),
                                                           m_home->filePath(QStringLiteral("meta")));
    m_jobs = std::make_unique<library::MeetingJobTracker>(m_store.get());
    m_stats = std::make_unique<library::JobStats>();
    m_mix = std::make_unique<recording::MixdownService>(m_store.get(), m_jobs.get(), m_media);
    m_log = std::make_shared<testing::FakeSttLog>();
    testing::registerFakeStt(m_registry, m_log);
    m_port = std::make_unique<testing::FakeTranscriptionPort>();
    m_stt = std::make_unique<processing::TranscriptionService>(processing::TranscriptionService::Deps{
        m_store.get(), m_jobs.get(), m_stats.get(), &m_registry, m_port.get()});
    m_diarEnabled = false;
    m_sttRoute = providers::Route::External;
    m_cloudLive = false;
    m_asked = 0;
    m_answer.reset();
    m_diarUsable = true;
    m_diarManual = false;
    m_diarCreated = 0;
    m_diarScript = {};
    processing::DiarizationService::Deps dd;
    dd.store = m_store.get();
    dd.jobs = m_jobs.get();
    dd.diarizer = [this](QObject* parent) -> providers::IDiarizer* {
        auto* f = new testing::FakeDiarizer(parent);
        f->manual = m_diarManual;
        f->script = m_diarScript;
        m_fakeDiar = f;
        ++m_diarCreated;
        return f;
    };
    dd.transcript = [this](const QString& id) { return m_stt->currentTranscript(id); };
    dd.saveTranscript = [this](const QString& id, const MergedTranscript& t) { m_stt->replaceTranscript(id, t); };
    m_diar = std::make_unique<processing::DiarizationService>(dd);
    m_usable = true;
    m_decided = false;
    m_identifyStarts = true;
    m_ready = providers::ReadinessResult();
    m_ready.runnable = true;
    m_mixdownMode = QStringLiteral("auto");
    m_calls.clear();
    m_errors.clear();
    m_outcomes.clear();
    m_measureDone = {};
    connect(m_jobs.get(), &library::MeetingJobTracker::jobFinished, this,
            [this](const QString& id, JobKind kind, JobOutcome o) {
                if (kind == JobKind::Transcribe) m_outcomes.insert(id, o);
            });
    makeWorkflows();
}

void WorkflowsTest::makeWorkflows()
{
    Workflows::Deps d;
    d.store = m_store.get();
    d.jobs = m_jobs.get();
    d.mixdown = m_mix.get();
    d.transcription = m_stt.get();
    d.voice.usable = [this]() { return m_usable; };
    d.voice.startIdentify = [this](const QString& id, bool asStage) {
        m_calls << QStringLiteral("identify:%1:%2").arg(id).arg(int(asStage));
        return m_identifyStarts;
    };
    d.voice.cancelIdentify = [this](const QString&, JobKind kind) {
        m_calls << QStringLiteral("cancelIdentify:%1").arg(int(kind));
        return kind == JobKind::Transcribe && m_identifyStarts;
    };
    d.voice.applyParticipants = [this](const QString&) { m_calls << QStringLiteral("apply"); return m_decided; };
    d.voice.analyzeParticipants = [this](const QString&) { m_calls << QStringLiteral("analyze"); return false; };
    d.voice.measureActivity = [this](const Meeting&, std::function<void(const MeetingActivity&)> done) {
        m_calls << QStringLiteral("measure");
        m_measureDone = std::move(done);
    };
    d.readiness = [this](providers::WorkflowStep, const QString&) { return m_ready; };
    d.mixdownMode = [this]() { return m_mixdownMode; };
    d.diarization = m_diar.get();
    d.storedRoute = [this](providers::Capability cap) {
        if (cap == providers::Capability::Diarization)
            return m_diarEnabled ? providers::Route::Local : providers::Route::External;
        return cap == providers::Capability::Stt ? m_sttRoute : providers::Route::Local;
    };
    d.fallbackRoute = [](providers::Capability cap) {
        return cap == providers::Capability::Stt ? providers::Route::External : providers::Route::Local;
    };
    d.cloudLive = [this]() { return m_cloudLive; };
    d.askRoute = [this](providers::Capability cap, const QString&) -> std::optional<providers::Route> {
        if (cap != providers::Capability::Stt) return std::nullopt;
        ++m_asked;
        return m_answer;
    };
    d.localDiarizationUsable = [this]() { return m_diarUsable; };
    d.voice.useDiarization = [this](const QString&, const providers::DiarizeResult& r) {
        m_calls << QStringLiteral("useDiarization:%1").arg(r.speakers.size());
    };
    m_wf = std::make_unique<Workflows>(d);
    // As the facade does: the mix's signals reach the chains.
    connect(m_mix.get(), &recording::MixdownService::progress, m_wf.get(), &Workflows::mixdownProgress);
    connect(m_mix.get(), &recording::MixdownService::runEnded, m_wf.get(), &Workflows::mixdownRunEnded);
    connect(m_wf.get(), &Workflows::errorOccurred, this, [this](const QString& e) { m_errors << e; });
    connect(m_stt.get(), &processing::TranscriptionService::errorOccurred, this, [this](const QString& e) { m_errors << e; });
}

void WorkflowsTest::cleanup()
{
    m_wf.reset();
    m_diar.reset();
    m_stt.reset();
    m_mix.reset();
    m_jobs.reset();
    m_store.reset();
    m_home.reset();
}

Meeting WorkflowsTest::recorded(const QString& title, bool freshMix, bool unmeasured)
{
    Meeting m = m_store->createMeeting(title);
    m.durationMs = 90000;
    for (const QString& id : {QStringLiteral("mic"), QStringLiteral("sys")}) {
        Track t;
        t.id = id;
        t.deviceName = id;
        t.file = QStringLiteral("track_%1.ogg").arg(id);
        t.active = true;
        t.speechRatio = unmeasured ? -1.0 : 0.5;
        m.tracks << t;
        QFile f(QDir(m.folder).filePath(t.file));
        if (f.open(QIODevice::WriteOnly)) f.write("x");
    }
    if (freshMix) {
        QFile f(QDir(m.folder).filePath(QStringLiteral("mixdown.mp3")));
        if (f.open(QIODevice::WriteOnly)) f.write("mix");
        m.mixdownFile = QStringLiteral("mixdown.mp3");
        m.mixdownDirty = false;
    } else {
        m.mixdownDirty = true;
    }
    m_store->saveMeeting(m);
    return m;
}

// Starts the chain and waits until the STT step runs (the mix done, if any).
void WorkflowsTest::runToStt(const QString& id)
{
    const int before = m_log->jobs.size();
    m_wf->transcribe(id);
    QTRY_COMPARE_WITH_TIMEOUT(m_log->jobs.size(), before + 1, 5000);
    QVERIFY(m_stt->running(id));
    QVERIFY(!m_wf->transcriptionInMixdown(id));
}

void WorkflowsTest::chainWithMixdownToIdentify()
{
    const Meeting m = recorded(QStringLiteral("Lánc"));
    m_wf->transcribe(m.id);
    JobProgress jp = m_jobs->job(m.id, JobKind::Transcribe);
    QStringList ids;
    for (const JobStage& st : jp.stages) ids << st.id;
    QCOMPARE(ids, QStringList({"mixdown", "upload", "transcribe", "diarize", "identify"}));
    QVERIFY(jp.stage("upload")->detail.contains(QStringLiteral("2 sáv")));
    QCOMPARE(jp.stage("mixdown")->state, StageState::Running);
    QVERIFY(m_wf->transcriptionInMixdown(m.id));
    QVERIFY(!m_jobs->isRunning(m.id, JobKind::Mixdown));   // the mix is a stage, not its own task
    QVERIFY(m_calls.contains(QStringLiteral("analyze")));   // the voice analysis runs in parallel
    QVERIFY(m_calls.contains(QStringLiteral("cancelIdentify:%1").arg(int(JobKind::Identify))));
    m_wf->transcribe(m.id);                                 // double start: no-op
    QCOMPARE(m_media->graphs().size(), 1);

    QTRY_COMPARE_WITH_TIMEOUT(m_log->jobs.size(), 1, 5000);
    jp = m_jobs->job(m.id, JobKind::Transcribe);
    QCOMPARE(jp.stage("mixdown")->state, StageState::Done);
    QCOMPARE(jp.stage("upload")->state, StageState::Running);

    emit m_log->jobs.first()->finished(testing::sttResult({{"1", "Szia"}}));
    QVERIFY(m_calls.contains(QStringLiteral("apply")));
    QVERIFY(m_calls.contains(QStringLiteral("identify:%1:1").arg(m.id)));
    // The identification closes the job (its own finish in the voice service).
    QVERIFY(m_jobs->isRunning(m.id, JobKind::Transcribe));
    m_jobs->finish(m.id, JobKind::Transcribe);
    QCOMPARE(outcome(m.id), JobOutcome::Done);
    QVERIFY(m_store->load(m.id).hasTranscript);
    QVERIFY(m_errors.isEmpty());

    // Without a usable voice model: no identify stage, and the job finishes right after the STT.
    m_usable = false;
    m_identifyStarts = false;
    const Meeting k = recorded(QStringLiteral("Modell nélkül"), /*freshMix*/ true);
    runToStt(k.id);
    ids.clear();
    for (const JobStage& st : m_jobs->job(k.id, JobKind::Transcribe).stages) ids << st.id;
    QCOMPARE(ids, QStringList({"upload", "transcribe", "diarize"}));
    emit m_log->jobs.last()->finished(testing::sttResult({{"1", "Szia"}}));
    QVERIFY(!m_jobs->isRunning(k.id, JobKind::Transcribe));
    QCOMPARE(outcome(k.id), JobOutcome::Done);
}

void WorkflowsTest::chainWithoutMixdown()
{
    const Meeting m = recorded(QStringLiteral("Kész keverék"), /*freshMix*/ true);
    m_wf->transcribe(m.id);
    QCOMPARE(m_log->jobs.size(), 1);   // the STT starts at once
    QVERIFY(!m_jobs->job(m.id, JobKind::Transcribe).stage("mixdown"));
    QCOMPARE(m_media->graphs().size(), 0);
}

void WorkflowsTest::cancelDuringMixdown()
{
    const Meeting m = recorded(QStringLiteral("Megszakítás a keverésben"));
    m_wf->transcribe(m.id);
    QVERIFY(m_mix->running(m.id));
    QVERIFY(m_wf->cancel(m.id, JobKind::Transcribe));
    QCOMPARE(outcome(m.id), JobOutcome::Cancelled);
    QVERIFY(!m_wf->transcriptionInMixdown(m.id));
    QTRY_VERIFY_WITH_TIMEOUT(!m_mix->running(m.id), 5000);   // our own ffmpeg stopped
    QTest::qWait(50);
    QCOMPARE(m_log->jobs.size(), 0);                           // the chain is broken: no STT
    QVERIFY(m_errors.isEmpty());
    QVERIFY(!m_jobs->lastError(m.id, JobKind::Transcribe).isValid());

    // A standalone mix that was already running is not stopped by the transcription's cancel.
    const Meeting k = recorded(QStringLiteral("Önálló keverés"));
    m_mix->regenerate(k.id);
    m_wf->transcribe(k.id);
    QVERIFY(m_wf->cancel(k.id, JobKind::Transcribe));
    QVERIFY(m_mix->running(k.id));
    QTRY_VERIFY_WITH_TIMEOUT(!m_mix->running(k.id), 5000);
    QCOMPARE(m_log->jobs.size(), 0);
}

void WorkflowsTest::cancelDuringStt()
{
    m_port->cloud = true;
    const Meeting m = recorded(QStringLiteral("Megszakítás az átírásban"));
    runToStt(m.id);
    QVERIFY(m_wf->cancel(m.id, JobKind::Transcribe));
    QVERIFY(m_log->jobs.first()->cancelled);
    QCOMPARE(outcome(m.id), JobOutcome::Cancelled);
    QCOMPARE(m_port->cancelled, 1);
    QVERIFY(!m_calls.contains(QStringLiteral("apply")));
    QVERIFY(m_errors.isEmpty());
    QVERIFY(!m_store->load(m.id).hasTranscript);
}

void WorkflowsTest::cancelDuringIdentify()
{
    const Meeting m = recorded(QStringLiteral("Megszakítás az azonosításban"), true);
    runToStt(m.id);
    emit m_log->jobs.first()->finished(testing::sttResult({{"1", "Szia"}}));
    QVERIFY(m_jobs->isRunning(m.id, JobKind::Transcribe));   // the identify stage runs
    m_calls.clear();
    QVERIFY(m_wf->cancel(m.id, JobKind::Transcribe));
    QCOMPARE(m_calls, QStringList({QStringLiteral("cancelIdentify:%1").arg(int(JobKind::Transcribe))}));
    QVERIFY(m_store->load(m.id).hasTranscript);   // the transcript stays
}

void WorkflowsTest::mixdownFailureStopsChain()
{
    m_media->graphOk = false;
    const Meeting m = recorded(QStringLiteral("Hibás keverés"));
    m_wf->transcribe(m.id);
    QTRY_COMPARE_WITH_TIMEOUT(outcome(m.id), JobOutcome::Failed, 5000);
    QVERIFY(m_jobs->lastError(m.id, JobKind::Transcribe).isValid());
    QVERIFY(!m_errors.isEmpty());
    QVERIFY(!m_wf->transcriptionInMixdown(m.id));
    QCOMPARE(m_log->jobs.size(), 0);

    // Restart after the fix: the chain runs again from the top.
    m_media->graphOk = true;
    m_outcomes.clear();
    runToStt(m.id);
    emit m_log->jobs.last()->finished(testing::sttResult({{"1", "Szia"}}));
    m_jobs->finish(m.id, JobKind::Transcribe);
    QCOMPARE(outcome(m.id), JobOutcome::Done);
    QVERIFY(!m_jobs->lastError(m.id, JobKind::Transcribe).isValid());
}

void WorkflowsTest::sttFailureThenRestart()
{
    const Meeting m = recorded(QStringLiteral("Hibás átírás"), true);
    runToStt(m.id);
    emit m_log->jobs.first()->failed(QStringLiteral("HTTP 500"));
    QCOMPARE(outcome(m.id), JobOutcome::Failed);
    QVERIFY(!m_jobs->isRunning(m.id, JobKind::Transcribe));
    QVERIFY(!m_calls.contains(QStringLiteral("apply")));
    QCOMPARE(m_errors.size(), 1);

    // Restart (also after a cancel).
    runToStt(m.id);
    QVERIFY(m_wf->cancel(m.id, JobKind::Transcribe));
    QCOMPARE(outcome(m.id), JobOutcome::Cancelled);
    runToStt(m.id);
    emit m_log->jobs.last()->finished(testing::sttResult({{"1", "Újra"}}));
    QVERIFY(m_store->load(m.id).hasTranscript);
    QVERIFY(m_calls.contains(QStringLiteral("identify:%1:1").arg(m.id)));
}

void WorkflowsTest::decisionAndToggleSkipIdentify()
{
    // The "Who was there?" decision covered the speakers: no automatic identification.
    m_decided = true;
    const Meeting m = recorded(QStringLiteral("Döntés"), true);
    runToStt(m.id);
    emit m_log->jobs.first()->finished(testing::sttResult({{"1", "Szia"}}));
    QCOMPARE(outcome(m.id), JobOutcome::Done);
    QVERIFY(!m_calls.contains(QStringLiteral("identify:%1:1").arg(m.id)));

    // The toggle: the stage is skipped from the start, the job finishes after the STT.
    m_decided = false;
    const Meeting k = recorded(QStringLiteral("Kapcsoló"), true);
    QVERIFY(m_wf->identifyAfterTranscription(k.id));
    m_wf->setIdentifyAfterTranscription(k.id, false);
    QVERIFY(!m_wf->identifyAfterTranscription(k.id));
    runToStt(k.id);
    QCOMPARE(m_jobs->job(k.id, JobKind::Transcribe).stage("identify")->state, StageState::Skipped);
    emit m_log->jobs.last()->finished(testing::sttResult({{"1", "Szia"}}));
    QCOMPARE(outcome(k.id), JobOutcome::Done);
    QVERIFY(!m_calls.contains(QStringLiteral("identify:%1:1").arg(k.id)));
    m_wf->setIdentifyAfterTranscription(k.id, true);
    QVERIFY(m_wf->identifyAfterTranscription(k.id));
}

void WorkflowsTest::readinessBlocksStart()
{
    m_ready.runnable = false;
    m_ready.detail = QStringLiteral("nincs kulcs");
    const Meeting m = recorded(QStringLiteral("Nem futtatható"), true);
    m_wf->transcribe(m.id);
    QVERIFY(!m_jobs->job(m.id, JobKind::Transcribe).isValid());
    QCOMPARE(m_errors, QStringList({QStringLiteral("nincs kulcs")}));
    m_wf->transcribe(QStringLiteral("ismeretlen"));
    QCOMPARE(m_errors.size(), 2);
}

void WorkflowsTest::retranscribeClearsNamesOnlyOnSuccess()
{
    Meeting m = recorded(QStringLiteral("Újraátírás"), true);
    m.speakerMap.insert(QStringLiteral("Beszélő 1"), PersonId::generate());
    m_store->saveMeeting(m);

    // Cancelled: the names stay.
    m_wf->retranscribe(m.id, false);
    QCOMPARE(m_log->jobs.size(), 1);
    QVERIFY(m_wf->cancel(m.id, JobKind::Transcribe));
    QCOMPARE(m_store->load(m.id).speakerMap.size(), 1);

    // Success (a first transcript, so only the flag clears the names).
    m_wf->retranscribe(m.id, false);
    emit m_log->jobs.last()->finished(testing::sttResult({{"1", "Szia"}}));
    QVERIFY(m_store->load(m.id).speakerMap.isEmpty());

    // A plain transcription of a meeting without a transcript keeps a pre-set map.
    Meeting k = recorded(QStringLiteral("Sima"), true);
    k.speakerMap.insert(QStringLiteral("Beszélő 1"), PersonId::generate());
    m_store->saveMeeting(k);
    runToStt(k.id);
    emit m_log->jobs.last()->finished(testing::sttResult({{"1", "Szia"}}));
    QCOMPARE(m_store->load(k.id).speakerMap.size(), 1);
}

void WorkflowsTest::standaloneIdentify()
{
    const Meeting m = recorded(QStringLiteral("Azonosítás"), true);
    QVERIFY(m_wf->identify(m.id));
    QVERIFY(m_calls.contains(QStringLiteral("identify:%1:0").arg(m.id)));
    runToStt(m.id);
    m_calls.clear();
    QVERIFY(!m_wf->identify(m.id));   // the transcription identifies at its end
    QVERIFY(m_calls.isEmpty());
}

void WorkflowsTest::cancelOtherKindsAndAll()
{
    const Meeting m = recorded(QStringLiteral("Minden"));
    // Nothing runs: false everywhere (the services that are not wired answer false too).
    QVERIFY(!m_wf->cancel(m.id, JobKind::Summarize));
    QVERIFY(!m_wf->cancel(m.id, JobKind::Import));
    QVERIFY(!m_wf->cancel(m.id, JobKind::Export));
    QVERIFY(!m_wf->cancel(m.id, JobKind::Mixdown));
    m_mix->regenerate(m.id);
    QVERIFY(m_wf->cancel(m.id, JobKind::Mixdown));
    QTRY_VERIFY_WITH_TIMEOUT(!m_mix->running(m.id), 5000);

    m_wf->transcribe(m.id);
    m_calls.clear();
    m_wf->cancelAll(m.id);
    QCOMPARE(outcome(m.id), JobOutcome::Cancelled);
    QVERIFY(m_calls.contains(QStringLiteral("cancelIdentify:%1").arg(int(JobKind::Identify))));
}

void WorkflowsTest::afterRecordingAndImport()
{
    // Measured tracks, automatic mode: the mix starts, then the voice analysis.
    const Meeting m = recorded(QStringLiteral("Felvétel"));
    m_wf->afterRecording(m);
    QVERIFY(m_mix->running(m.id));
    QVERIFY(m_jobs->isRunning(m.id, JobKind::Mixdown));   // a standalone mix is its own task
    QVERIFY(m_calls.contains(QStringLiteral("analyze")));
    QTRY_VERIFY_WITH_TIMEOUT(!m_mix->running(m.id), 5000);

    // Manual mode: no mix after an import.
    m_mixdownMode = QStringLiteral("manual");
    const Meeting k = recorded(QStringLiteral("Import"));
    m_wf->afterImport(k);
    QVERIFY(!m_mix->running(k.id));

    // The standalone recorder process: nothing runs.
    m_mixdownMode = QStringLiteral("auto");
    m_wf->setAutoMixdown(false);
    QVERIFY(!m_wf->autoMixdown());
    m_calls.clear();
    const Meeting r = recorded(QStringLiteral("Önálló felvevő"));
    m_wf->afterRecording(r);
    QVERIFY(!m_mix->running(r.id));
    QVERIFY(m_calls.isEmpty());
    m_wf->setAutoMixdown(true);

    // Unmeasured tracks: the speech check first; the mix starts at its end.
    const Meeting u = recorded(QStringLiteral("Mérés előbb"), false, /*unmeasured*/ true);
    QVERIFY(m_wf->speechCheckPending(u));
    m_wf->afterImport(u);
    QVERIFY(m_calls.contains(QStringLiteral("measure")));
    QVERIFY(m_wf->speechCheckRunning(u.id));
    QVERIFY(!m_mix->running(u.id));
    QSignalSpy tracks(m_wf.get(), &Workflows::tracksChanged);
    m_measureDone(MeetingActivity());
    QCOMPARE(tracks.size(), 1);
    QVERIFY(!m_wf->speechCheckRunning(u.id));
    QVERIFY(!m_wf->speechCheckPending(u));
    QVERIFY(m_mix->running(u.id));
    QTRY_VERIFY_WITH_TIMEOUT(!m_mix->running(u.id), 5000);
}

void WorkflowsTest::speechCheckOncePerProcess()
{
    const Meeting measured = recorded(QStringLiteral("Mért"));
    QVERIFY(!m_wf->checkTrackSpeech(measured.id));   // not needed
    const Meeting u = recorded(QStringLiteral("Méretlen"), false, true);
    QVERIFY(m_wf->checkTrackSpeech(u.id));
    QVERIFY(!m_wf->checkTrackSpeech(u.id));          // running
    m_measureDone(MeetingActivity());
    QVERIFY(!m_mix->running(u.id));                   // a plain check does not remix
    QVERIFY(!m_wf->checkTrackSpeech(u.id));          // measured in this process
}

namespace {

// Two speakers on the "sys" track: 0-0.9 s speaker 0, 0.9-2 s speaker 1 (meeting time).
providers::DiarizeResult twoOnSys(const providers::DiarizeRequest&)
{
    providers::DiarizeResult r;
    for (int s = 0; s < 2; ++s) {
        providers::SpeakerTurn t;
        t.trackId = QStringLiteral("sys");
        t.speakerIndex = s;
        t.startMs = s == 0 ? 0 : 900;
        t.endMs = s == 0 ? 900 : 2000;
        r.turns << t;
        providers::DiarizedSpeaker sp;
        sp.trackId = QStringLiteral("sys");
        sp.index = s;
        sp.totalMs = t.durationMs();
        providers::VoiceVector v;
        v.vector = {s == 0 ? 1.0f : 0.0f, s == 0 ? 0.0f : 1.0f};
        v.dim = 2;
        v.modelId = QStringLiteral("fake");
        sp.centroid.vectors << v;
        r.speakers << sp;
    }
    r.segmentationModelId = QStringLiteral("fake-seg");
    r.embeddingModelIds = {QStringLiteral("fake")};
    return r;
}

TrackTranscript sixWords()
{
    return testing::sttResult({{"1", " egy"}, {"1", " kettő"}, {"1", " három"},
                               {"1", " négy"}, {"1", " öt"}, {"1", " hat"}});
}

QString meetingJson(const Meeting& m)
{
    QFile f(QDir(m.folder).filePath(QStringLiteral("meeting.json")));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QStringList tokenSpeakers(const Meeting& m)
{
    QStringList out;
    for (const TranscriptToken& t : library::TranscriptRepository::readTokens(m.folder).tokens) out << t.speaker;
    return out;
}

} // namespace

void WorkflowsTest::localDiarizationAfterWhisper()
{
    m_diarEnabled = true;
    m_diarScript = twoOnSys;
    Meeting m = recorded(QStringLiteral("Whisper"), /*freshMix*/ true);
    // A hand-bound participant makes the mic track single-speaker; the sys track is clustered.
    Participant p;
    p.id = QStringLiteral("p1");
    p.personId = PersonId::generate();
    p.source = ParticipantSource::Manual;
    p.tracks = {QStringLiteral("mic")};
    m.participants << p;
    m_store->saveMeeting(m);

    runToStt(m.id);
    emit m_log->jobs.first()->finished(sixWords());
    QVERIFY(m_diar->running(m.id));
    QCOMPARE(m_jobs->job(m.id, JobKind::Transcribe).stage("diarize")->state, StageState::Running);
    QVERIFY(!m_calls.contains(QStringLiteral("apply")));   // the identification waits for the speakers
    QCOMPARE(m_diarCreated, 1);
    QCOMPARE(m_fakeDiar->requests.size(), 1);
    const providers::DiarizeRequest req = m_fakeDiar->requests.first();
    QCOMPARE(req.tracks.size(), 2);   // the tracks, not the mixdown
    for (const providers::DiarizeTrack& t : req.tracks) {
        QVERIFY(t.path.endsWith(QStringLiteral("track_%1.ogg").arg(t.trackId)));
        QCOMPARE(t.knownPerson.isNull(), t.trackId != QStringLiteral("mic"));
    }

    QTRY_VERIFY_WITH_TIMEOUT(m_calls.contains(QStringLiteral("identify:%1:1").arg(m.id)), 5000);
    QVERIFY(m_calls.indexOf(QStringLiteral("useDiarization:2")) >= 0);
    QVERIFY(m_calls.indexOf(QStringLiteral("useDiarization:2")) < m_calls.indexOf(QStringLiteral("apply")));
    QCOMPARE(tokenSpeakers(m_store->load(m.id)),
             QStringList({"Beszélő 1", "Beszélő 1", "Beszélő 1", "Beszélő 2", "Beszélő 2", "Beszélő 2"}));
    const MergedTranscript t = library::TranscriptRepository::readTokens(m.folder);
    QCOMPARE(t.speakerSource, processing::diarizationmerge::kSourceLocalDiarization);
    QCOMPARE(t.speakerModels, QStringList({"fake-seg", "fake"}));
    QCOMPARE(library::TranscriptRepository::readSegments(m.folder).size(), 2);
    QCOMPARE(m_stt->cachedTranscript(m.id).speakerSource, processing::diarizationmerge::kSourceLocalDiarization);
    QCOMPARE(m_jobs->job(m.id, JobKind::Transcribe).stage("diarize")->state, StageState::Done);
    m_jobs->finish(m.id, JobKind::Transcribe);
    QCOMPARE(outcome(m.id), JobOutcome::Done);
    QVERIFY(m_errors.isEmpty());
}

void WorkflowsTest::providerDiarizedSkipsLocal()
{
    const QString soniox = QStringLiteral("fake-soniox");
    if (!m_registry.has(providers::CapabilityKind::Transcription, soniox))
        testing::registerFakeStt(m_registry, m_log, soniox, /*diarizes*/ true);
    m_diarEnabled = true;
    m_diarScript = twoOnSys;
    m_port->providerId = soniox;
    const Meeting m = recorded(QStringLiteral("Soniox"), true);
    runToStt(m.id);
    emit m_log->jobs.last()->finished(sixWords());
    QVERIFY(m_calls.contains(QStringLiteral("identify:%1:1").arg(m.id)));   // straight on
    QCOMPARE(m_diarCreated, 0);
    QVERIFY(!m_stt->providerDiarized(QStringLiteral("unknown")));
    QVERIFY(m_stt->providerDiarized(m.id));

    // The same provider on a tier that does not diarize: the local diarization runs.
    m_port->diarization = false;
    const Meeting k = recorded(QStringLiteral("Gyors csomag"), true);
    runToStt(k.id);
    emit m_log->jobs.last()->finished(sixWords());
    QVERIFY(!m_stt->providerDiarized(k.id));
    QCOMPARE(m_diarCreated, 1);
    QTRY_VERIFY_WITH_TIMEOUT(m_calls.contains(QStringLiteral("identify:%1:1").arg(k.id)), 5000);
    QCOMPARE(tokenSpeakers(m_store->load(k.id)).last(), QStringLiteral("Beszélő 2"));
}

void WorkflowsTest::cancelDuringDiarize()
{
    m_diarEnabled = true;
    m_diarManual = true;
    const Meeting m = recorded(QStringLiteral("Megszakítás a szétválasztásban"), true);
    runToStt(m.id);
    emit m_log->jobs.first()->finished(sixWords());
    QVERIFY(m_diar->running(m.id));
    QPointer<testing::FakeDiarizeJob> job = m_fakeDiar->jobs.first();
    m_calls.clear();
    QVERIFY(m_wf->cancel(m.id, JobKind::Transcribe));
    QVERIFY(job && job->cancelRequested);
    QTRY_COMPARE_WITH_TIMEOUT(outcome(m.id), JobOutcome::Cancelled, 5000);
    QVERIFY(!m_diar->running(m.id));
    QVERIFY(!m_calls.contains(QStringLiteral("apply")));
    QVERIFY(m_calls.filter(QStringLiteral("identify:")).isEmpty());
    QVERIFY(m_calls.filter(QStringLiteral("useDiarization")).isEmpty());
    // The transcript stays as the STT gave it.
    QVERIFY(m_store->load(m.id).hasTranscript);
    QCOMPARE(tokenSpeakers(m_store->load(m.id)), QStringList(6, QStringLiteral("Beszélő 1")));
    QVERIFY(!m_jobs->lastError(m.id, JobKind::Transcribe).isValid());
    QVERIFY(m_errors.isEmpty());
}

void WorkflowsTest::diarizationOffOrFailing()
{
    m_diarScript = twoOnSys;
    // The setting off: no diarizer.
    const Meeting a = recorded(QStringLiteral("Kikapcsolva"), true);
    runToStt(a.id);
    emit m_log->jobs.last()->finished(sixWords());
    QCOMPARE(m_diarCreated, 0);
    QVERIFY(m_calls.contains(QStringLiteral("identify:%1:1").arg(a.id)));

    // On, but no usable model: no diarizer either.
    m_diarEnabled = true;
    m_diarUsable = false;
    const Meeting b = recorded(QStringLiteral("Modell nélkül"), true);
    runToStt(b.id);
    emit m_log->jobs.last()->finished(sixWords());
    QCOMPARE(m_diarCreated, 0);
    QVERIFY(!m_wf->wantsLocalDiarization(b.id));

    // The per-run override wins over the setting (off -> on).
    m_diarEnabled = false;
    m_diarUsable = true;
    m_wf->setRouteOverride(providers::Capability::Diarization, providers::Route::Local);
    const Meeting c = recorded(QStringLiteral("Felülbírálva"), true);
    runToStt(c.id);
    emit m_log->jobs.last()->finished(sixWords());
    QCOMPARE(m_diarCreated, 1);
    QTRY_VERIFY_WITH_TIMEOUT(m_calls.contains(QStringLiteral("identify:%1:1").arg(c.id)), 5000);
    m_wf->setRouteOverride(providers::Capability::Diarization, providers::Route::External);
    m_diarEnabled = true;
    QVERIFY(!m_wf->wantsLocalDiarization(c.id));
    m_wf->setRouteOverride(providers::Capability::Diarization, std::nullopt);

    // A failing diarizer: the stage fails, the chain goes on with the one-speaker transcript.
    m_diarManual = true;
    const Meeting e = recorded(QStringLiteral("Hibás szétválasztás"), true);
    runToStt(e.id);
    emit m_log->jobs.last()->finished(sixWords());
    QVERIFY(m_diar->running(e.id));
    m_fakeDiar->jobs.last()->failLater(providers::inputError(QStringLiteral("decode_failed"), QStringLiteral("x")));
    QTRY_VERIFY_WITH_TIMEOUT(m_calls.contains(QStringLiteral("identify:%1:1").arg(e.id)), 5000);
    QCOMPARE(m_jobs->job(e.id, JobKind::Transcribe).stage("diarize")->state, StageState::Failed);
    QCOMPARE(tokenSpeakers(m_store->load(e.id)), QStringList(6, QStringLiteral("Beszélő 1")));
    QVERIFY(m_errors.isEmpty());
}

void WorkflowsTest::manualDiarizeSpeakers()
{
    // The setting is off: the transcription gives one speaker; the manual action still works.
    m_diarEnabled = false;
    m_diarScript = twoOnSys;
    Meeting m = recorded(QStringLiteral("Kézi"), /*freshMix*/ true);
    runToStt(m.id);
    emit m_log->jobs.first()->finished(sixWords());
    m_jobs->finish(m.id, JobKind::Transcribe);
    QCOMPARE(m_diarCreated, 0);

    Workflows::SpeakerStatus st = m_wf->speakerStatus(m.id);
    QCOMPARE(st.source, QStringLiteral("none"));
    QCOMPARE(st.blocker, QStringLiteral("disabled"));
    QCOMPARE(st.speakers, 1);
    QVERIFY(st.canDiarize);
    QVERIFY(!st.hasNames);
    QVERIFY(st.problem.isEmpty());

    // "How many speakers?" = 2: remembered, passed to the diarizer; its own job with the diarize + identify stages.
    m_calls.clear();
    QString why;
    QVERIFY(m_wf->diarizeSpeakers(m.id, 2, &why));
    QCOMPARE(m_store->load(m.id).diarizeSpeakerCount, 2);
    QVERIFY(meetingJson(m).contains(QStringLiteral("\"diarizeSpeakers\": 2")));   // written only when set
    QVERIFY(m_diar->running(m.id));
    QCOMPARE(m_diarCreated, 1);
    QCOMPARE(m_fakeDiar->requests.first().numClusters, 2);
    QStringList ids;
    for (const JobStage& g : m_jobs->job(m.id, JobKind::Transcribe).stages) ids << g.id;
    QCOMPARE(ids, QStringList({"diarize", "identify"}));
    QVERIFY(!m_wf->speakerStatus(m.id).canDiarize);   // running
    QCOMPARE(m_wf->speakerStatus(m.id).cannotBecause, QStringLiteral("running"));
    QTRY_VERIFY_WITH_TIMEOUT(m_calls.contains(QStringLiteral("identify:%1:1").arg(m.id)), 5000);
    QVERIFY(m_calls.contains(QStringLiteral("useDiarization:2")));
    QCOMPARE(tokenSpeakers(m_store->load(m.id)),
             QStringList({"Beszélő 1", "Beszélő 1", "Beszélő 1", "Beszélő 2", "Beszélő 2", "Beszélő 2"}));
    m_jobs->finish(m.id, JobKind::Transcribe);
    st = m_wf->speakerStatus(m.id);
    QCOMPARE(st.source, QStringLiteral("local"));
    QCOMPARE(st.speakers, 2);
    QCOMPARE(st.speakerCount, 2);
    QVERIFY(st.canDiarize);   // a local result can be redone

    // Redo with automatic count: the names go with the new speakers, the remembered count is cleared.
    Meeting named = m_store->load(m.id);
    named.speakerMap.insert(QStringLiteral("Beszélő 2"), PersonId::generate());
    m_store->saveMeeting(named);
    QVERIFY(m_wf->speakerStatus(m.id).hasNames);
    m_calls.clear();
    QVERIFY(m_wf->diarizeSpeakers(m.id, 0));
    QCOMPARE(m_diarCreated, 2);
    QCOMPARE(m_store->load(m.id).diarizeSpeakerCount, 0);
    QVERIFY(!meetingJson(m).contains(QStringLiteral("diarizeSpeakers")));
    QTRY_VERIFY_WITH_TIMEOUT(m_calls.contains(QStringLiteral("identify:%1:1").arg(m.id)), 5000);
    QVERIFY(m_store->load(m.id).speakerMap.isEmpty());
    QCOMPARE(tokenSpeakers(m_store->load(m.id)).last(), QStringLiteral("Beszélő 2"));
    m_jobs->finish(m.id, JobKind::Transcribe);
}

void WorkflowsTest::manualDiarizeFailureAndGates()
{
    m_diarScript = twoOnSys;
    const Meeting m = recorded(QStringLiteral("Hibás kézi"), true);
    runToStt(m.id);
    emit m_log->jobs.first()->finished(sixWords());
    m_jobs->finish(m.id, JobKind::Transcribe);

    // A failing diarizer: the job ends without identification, the problem is remembered, the transcript stays.
    m_diarManual = true;
    m_calls.clear();
    QVERIFY(m_wf->diarizeSpeakers(m.id, 0));
    m_fakeDiar->jobs.last()->failLater(providers::inputError(QStringLiteral("decode_failed"), QStringLiteral("x")));
    QTRY_VERIFY_WITH_TIMEOUT(!m_jobs->isRunning(m.id, JobKind::Transcribe), 5000);
    QCOMPARE(outcome(m.id), JobOutcome::Done);
    QVERIFY(m_calls.filter(QStringLiteral("identify:")).isEmpty());
    QVERIFY(!m_wf->speakerStatus(m.id).problem.isEmpty());
    QCOMPARE(tokenSpeakers(m_store->load(m.id)), QStringList(6, QStringLiteral("Beszélő 1")));
    QVERIFY(m_wf->speakerStatus(m.id).canDiarize);   // "Beszélők felismerése újra"

    // Cancel through the job tracker's usual path.
    QVERIFY(m_wf->diarizeSpeakers(m.id, 3));
    QVERIFY(m_wf->speakerStatus(m.id).problem.isEmpty());   // a new run clears the problem
    QVERIFY(m_wf->cancel(m.id, JobKind::Transcribe));
    QTRY_COMPARE_WITH_TIMEOUT(outcome(m.id), JobOutcome::Cancelled, 5000);
    QCOMPARE(tokenSpeakers(m_store->load(m.id)), QStringList(6, QStringLiteral("Beszélő 1")));

    // Gates: no usable model, no transcript, a provider-separated transcript.
    m_diarUsable = false;
    QString why;
    QVERIFY(!m_wf->diarizeSpeakers(m.id, 0, &why));
    QCOMPARE(why, QStringLiteral("no_model"));
    QCOMPARE(m_wf->speakerStatus(m.id).blocker, QStringLiteral("model"));
    m_diarUsable = true;
    const Meeting empty = recorded(QStringLiteral("Átirat nélkül"), true);
    QVERIFY(!m_wf->diarizeSpeakers(empty.id, 0, &why));
    QCOMPARE(why, QStringLiteral("no_transcript"));
    QVERIFY(!m_wf->diarizeSpeakers(QStringLiteral("nincs-ilyen"), 0, &why));

    // (A provider id of its own: the registry outlives the tests, a registered fake keeps the log it got.)
    const QString soniox = QStringLiteral("fake-soniox-manual");
    if (!m_registry.has(providers::CapabilityKind::Transcription, soniox))
        testing::registerFakeStt(m_registry, m_log, soniox, /*diarizes*/ true);
    m_port->providerId = soniox;
    const Meeting p = recorded(QStringLiteral("Szolgáltatótól"), true);
    runToStt(p.id);
    emit m_log->jobs.last()->finished(testing::sttResult({{"1", " egy"}, {"2", " kettő"}, {"1", " három"}}));
    m_jobs->finish(p.id, JobKind::Transcribe);
    const Workflows::SpeakerStatus ps = m_wf->speakerStatus(p.id);
    QCOMPARE(ps.source, QStringLiteral("provider"));
    QVERIFY(!ps.canDiarize);
    QCOMPARE(ps.cannotBecause, QStringLiteral("provider"));
}

// S3: the route of a run. Ask is answered once per run (transcribe), the answer stays for the meeting while the
// stored route is unchanged; no answer / a dead cloud -> the non-cloud route; an override and a per-run choice.
void WorkflowsTest::askRouteResolution()
{
    using providers::Capability;
    using providers::Route;
    m_sttRoute = Route::Ask;
    m_cloudLive = true;

    // No answer: the non-cloud route (External: the fallback of this fixture).
    QCOMPARE(m_wf->resolveRoute(Capability::Stt, QStringLiteral("m1")), Route::External);
    QCOMPARE(m_asked, 1);
    QCOMPARE(m_wf->route(Capability::Stt, QStringLiteral("m1")), Route::External);

    // The answer Cloud: remembered for that meeting only; route() never asks.
    m_answer = Route::Cloud;
    QCOMPARE(m_wf->resolveRoute(Capability::Stt, QStringLiteral("m1")), Route::Cloud);
    QCOMPARE(m_wf->route(Capability::Stt, QStringLiteral("m1")), Route::Cloud);
    QCOMPARE(m_wf->route(Capability::Stt, QStringLiteral("m2")), Route::External);
    QCOMPARE(m_wf->route(Capability::Stt), Route::External);
    QCOMPARE(m_asked, 2);

    // An answer of Ask counts as none.
    m_answer = Route::Ask;
    QCOMPARE(m_wf->resolveRoute(Capability::Stt, QStringLiteral("m4")), Route::External);

    // The cloud is not live: a Cloud answer runs on the non-cloud route.
    m_answer = Route::Cloud;
    m_cloudLive = false;
    QCOMPARE(m_wf->resolveRoute(Capability::Stt, QStringLiteral("m2")), Route::External);
    QCOMPARE(m_wf->route(Capability::Stt, QStringLiteral("m1")), Route::External);   // the remembered Cloud is dead too
    m_cloudLive = true;

    // The stored route changed: the remembered answer no longer applies.
    m_sttRoute = Route::Local;
    QCOMPARE(m_wf->route(Capability::Stt, QStringLiteral("m1")), Route::Local);

    // A per-run choice wins over the setting once.
    m_wf->chooseRoute(Capability::Stt, QStringLiteral("m3"), Route::Cloud);
    QCOMPARE(m_wf->resolveRoute(Capability::Stt, QStringLiteral("m3")), Route::Cloud);
    QCOMPARE(m_wf->resolveRoute(Capability::Stt, QStringLiteral("m3")), Route::Local);

    // The per-process override (the CLI's --run) wins over the setting; Cloud only while live.
    m_wf->setRouteOverride(Capability::Stt, Route::Cloud);
    QCOMPARE(m_wf->route(Capability::Stt), Route::Cloud);
    m_cloudLive = false;
    QCOMPARE(m_wf->route(Capability::Stt), Route::External);
    m_wf->setRouteOverride(Capability::Stt, std::nullopt);
    QCOMPARE(m_wf->route(Capability::Stt), Route::Local);

    // transcribe() asks once, before the provider starts.
    m_sttRoute = Route::Ask;
    m_cloudLive = true;
    m_answer.reset();
    const int asked = m_asked;
    const Meeting m = recorded(QStringLiteral("Kérdezzen"), true);
    runToStt(m.id);
    QCOMPARE(m_asked, asked + 1);
    QCOMPARE(m_wf->route(Capability::Stt, m.id), Route::External);
}

QTEST_GUILESS_MAIN(WorkflowsTest)
#include "test_workflows.moc"
