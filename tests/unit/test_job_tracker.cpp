// MeetingJobTracker + JobErrors + JobStats: the per-meeting processing state: derived icon
// states, the running job's stages, persistent (restart-surviving) errors.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/jobs/JobErrors.h"
#include "tanara/library/jobs/JobStats.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/cloud/CloudTypes.h"

using namespace tanara;

class JobTrackerTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void emptyMeetingIsNone();
    void flagsDeriveDone();
    void runningJobAndStages();
    void failurePersistsAcrossRestart();
    void failedRetryKeepsDoneButShowsError();
    void successAndClearRemoveError();
    void cancelLeavesNoError();
    void summaryKindsShareSlot();
    void topicErrorsPersist();
    void identifyState();
    void mixdownState();
    void removedMeetingForgotten();

    void describeHttp401();
    void describeNetworkAndServerErrors();
    void describeWithoutExchange();
    void describeCloud();

    void statsEstimateOnlyFromSamples();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<library::MeetingRepository> m_store;
    Meeting newMeeting(const QString& title = QStringLiteral("Teszt")) {
        return m_store->createMeeting(title);
    }
};

#ifndef Q_MOC_RUN   // (moc would get lost on raw string literals)

void JobTrackerTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_store = std::make_unique<library::MeetingRepository>(m_dir->filePath("rec"), m_dir->filePath("meta"));
}

void JobTrackerTest::cleanup()
{
    m_store.reset();
    m_dir.reset();
}

void JobTrackerTest::emptyMeetingIsNone()
{
    library::MeetingJobTracker t(m_store.get());
    const Meeting m = newMeeting();
    const MeetingProcessingState s = t.state(m.id);
    QCOMPARE(s.meetingId, m.id);
    QCOMPARE(s.transcriptState, StepState::None);
    QCOMPARE(s.summaryState, StepState::None);
    QCOMPARE(s.identifyState, StepState::None);
    QVERIFY(!s.mixdownRunning);
    QVERIFY(!s.busy());
    QVERIFY(!s.transcriptError.isValid());
    // A state file is created only if there is something to keep.
    QVERIFY(!QFile::exists(QDir(m.folder).filePath(library::MeetingJobTracker::stateFileName())));
}

void JobTrackerTest::flagsDeriveDone()
{
    library::MeetingJobTracker t(m_store.get());
    Meeting m = newMeeting();
    m.hasTranscript = true;
    m.hasSummary = true;
    m.speakerMap.insert(QStringLiteral("Beszélő 1"), QStringLiteral("Ödön"));
    m_store->saveMeeting(m);
    const MeetingProcessingState s = t.state(m.id);
    QCOMPARE(s.transcriptState, StepState::Done);
    QCOMPARE(s.summaryState, StepState::Done);
    QCOMPARE(s.identifyState, StepState::Done);   // old meeting: a named speaker is the signal
}

void JobTrackerTest::runningJobAndStages()
{
    library::MeetingJobTracker t(m_store.get());
    const Meeting m = newMeeting();
    QSignalSpy started(&t, &library::MeetingJobTracker::jobStarted);
    QSignalSpy progress(&t, &library::MeetingJobTracker::jobProgressChanged);
    QSignalSpy changed(&t, &library::MeetingJobTracker::stateChanged);

    t.begin(m.id, JobKind::Transcribe, QStringLiteral("Átírás folyamatban"),
            {{QStringLiteral("upload"), QStringLiteral("Feltöltés"), StageState::Waiting, -1, QString()},
             {QStringLiteral("transcribe"), QStringLiteral("Átírás"), StageState::Waiting, -1, QString()}});
    QCOMPARE(started.count(), 1);
    QVERIFY(t.isRunning(m.id, JobKind::Transcribe));
    QVERIFY(t.isBusy(m.id));
    QCOMPARE(t.state(m.id).transcriptState, StepState::Running);
    QCOMPARE(t.activeJobs().size(), 1);

    t.setStage(m.id, JobKind::Transcribe, QStringLiteral("upload"), StageState::Running, 0);
    t.setStagePercent(m.id, JobKind::Transcribe, QStringLiteral("upload"), 42);
    JobProgress jp = t.job(m.id, JobKind::Transcribe);
    QVERIFY(jp.isValid());
    QCOMPARE(jp.stages.size(), 2);
    QCOMPARE(jp.stage(QStringLiteral("upload"))->state, StageState::Running);
    QCOMPARE(jp.stage(QStringLiteral("upload"))->percent, 42);
    QCOMPARE(jp.stage(QStringLiteral("transcribe"))->state, StageState::Waiting);
    QCOMPARE(jp.stage(QStringLiteral("transcribe"))->percent, -1);   // no invented percentage

    // The same value: no redundant signal.
    const int n = progress.count();
    t.setStagePercent(m.id, JobKind::Transcribe, QStringLiteral("upload"), 42);
    QCOMPARE(progress.count(), n);

    t.setStage(m.id, JobKind::Transcribe, QStringLiteral("upload"), StageState::Done);
    QCOMPARE(t.job(m.id, JobKind::Transcribe).stage(QStringLiteral("upload"))->percent, -1);

    // Estimate: only if given; the remaining time counts from the start.
    QCOMPARE(t.job(m.id, JobKind::Transcribe).etaSeconds(), -1);
    t.setEstimate(m.id, JobKind::Transcribe, 120);
    jp = t.job(m.id, JobKind::Transcribe);
    QCOMPARE(jp.etaSeconds(jp.startedAt.addSecs(20)), 100);
    QCOMPARE(jp.etaSeconds(jp.startedAt.addSecs(500)), 0);

    QSignalSpy finished(&t, &library::MeetingJobTracker::jobFinished);
    t.finish(m.id, JobKind::Transcribe);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Done);
    QVERIFY(!t.isBusy(m.id));
    QVERIFY(!t.job(m.id, JobKind::Transcribe).isValid());
    QVERIFY(changed.count() >= 2);
}

void JobTrackerTest::failurePersistsAcrossRestart()
{
    const Meeting m = newMeeting();
    {
        library::MeetingJobTracker t(m_store.get());
        QSignalSpy errs(&t, &library::MeetingJobTracker::errorChanged);
        t.begin(m.id, JobKind::Transcribe, QStringLiteral("Átírás"));
        JobError e;
        e.message = QStringLiteral("A szolgáltató nem fogadta el az API-kulcsot.");
        e.detail = QStringLiteral("HTTP 401 · invalid_api_key");
        e.fixActionHint = QStringLiteral("settings:stt");
        t.fail(m.id, JobKind::Transcribe, e);
        QCOMPARE(errs.count(), 1);
        const MeetingProcessingState s = t.state(m.id);
        QCOMPARE(s.transcriptState, StepState::Failed);
        QCOMPARE(s.transcriptError.detail, QStringLiteral("HTTP 401 · invalid_api_key"));
        QVERIFY(s.transcriptError.when.isValid());
        QVERIFY(!s.busy());
    }
    QVERIFY(QFile::exists(QDir(m.folder).filePath(library::MeetingJobTracker::stateFileName())));
    // "Restart": a new tracker (and a new store) on the same disk.
    library::MeetingRepository store2(m_dir->filePath("rec"), m_dir->filePath("meta"));
    library::MeetingJobTracker t2(&store2);
    const MeetingProcessingState s = t2.state(m.id);
    QCOMPARE(s.transcriptState, StepState::Failed);
    QCOMPARE(s.transcriptError.message, QStringLiteral("A szolgáltató nem fogadta el az API-kulcsot."));
    QCOMPARE(s.transcriptError.detail, QStringLiteral("HTTP 401 · invalid_api_key"));
    QCOMPARE(s.transcriptError.fixActionHint, QStringLiteral("settings:stt"));
    QCOMPARE(s.transcriptError.kind, JobKind::Transcribe);
    QCOMPARE(t2.lastError(m.id, JobKind::Transcribe).detail, QStringLiteral("HTTP 401 · invalid_api_key"));
    QCOMPARE(s.summaryState, StepState::None);
}

void JobTrackerTest::failedRetryKeepsDoneButShowsError()
{
    library::MeetingJobTracker t(m_store.get());
    Meeting m = newMeeting();
    m.hasTranscript = true;
    m_store->saveMeeting(m);
    t.begin(m.id, JobKind::Transcribe, QStringLiteral("Újra-átírás"));
    JobError e;
    e.message = QStringLiteral("hiba");
    t.fail(m.id, JobKind::Transcribe, e);
    const MeetingProcessingState s = t.state(m.id);
    QCOMPARE(s.transcriptState, StepState::Done);       // the earlier transcript exists
    QVERIFY(s.transcriptError.isValid());               // but the failed rerun shows
}

void JobTrackerTest::successAndClearRemoveError()
{
    library::MeetingJobTracker t(m_store.get());
    const Meeting m = newMeeting();
    JobError e;
    e.kind = JobKind::Transcribe;
    e.message = QStringLiteral("hiba");
    t.recordError(m.id, e);
    QCOMPARE(t.state(m.id).transcriptState, StepState::Failed);

    t.clearError(m.id, JobKind::Transcribe);
    QCOMPARE(t.state(m.id).transcriptState, StepState::None);
    QVERIFY(!QFile::exists(QDir(m.folder).filePath(library::MeetingJobTracker::stateFileName())));

    t.recordError(m.id, e);
    t.begin(m.id, JobKind::Transcribe, QStringLiteral("Átírás"));
    QCOMPARE(t.state(m.id).transcriptState, StepState::Running);
    t.finish(m.id, JobKind::Transcribe);   // success: the error loses its validity
    QVERIFY(!t.lastError(m.id, JobKind::Transcribe).isValid());
}

void JobTrackerTest::cancelLeavesNoError()
{
    library::MeetingJobTracker t(m_store.get());
    const Meeting m = newMeeting();
    QSignalSpy finished(&t, &library::MeetingJobTracker::jobFinished);
    t.begin(m.id, JobKind::Transcribe, QStringLiteral("Átírás"));
    t.setCancelling(m.id, JobKind::Transcribe);
    QVERIFY(t.job(m.id, JobKind::Transcribe).cancelling);
    t.cancelled(m.id, JobKind::Transcribe);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Cancelled);
    const MeetingProcessingState s = t.state(m.id);
    QCOMPARE(s.transcriptState, StepState::None);
    QVERIFY(!s.transcriptError.isValid());
}

void JobTrackerTest::summaryKindsShareSlot()
{
    library::MeetingJobTracker t(m_store.get());
    Meeting m = newMeeting();
    m.hasTranscript = true;
    m_store->saveMeeting(m);
    for (JobKind k : {JobKind::Summarize, JobKind::ExtractTopics, JobKind::AnalyzeTopics}) {
        t.begin(m.id, k, QStringLiteral("x"));
        QCOMPARE(t.state(m.id).summaryState, StepState::Running);
        JobError e;
        e.message = QStringLiteral("LLM hiba");
        t.fail(m.id, k, e);
        QCOMPARE(t.state(m.id).summaryState, StepState::Failed);
        QCOMPARE(t.state(m.id).summaryError.kind, k);
        QVERIFY(t.lastError(m.id, JobKind::Summarize).isValid());
        t.clearError(m.id, JobKind::Summarize);
        QCOMPARE(t.state(m.id).summaryState, StepState::None);
    }
    QCOMPARE(t.state(m.id).transcriptState, StepState::Done);   // does not affect the other step
}

void JobTrackerTest::topicErrorsPersist()
{
    const Meeting m = newMeeting();
    {
        library::MeetingJobTracker t(m_store.get());
        JobError e;
        e.message = QStringLiteral("időtúllépés");
        e.detail = QStringLiteral("HTTP 504");
        t.setTopicError(m.id, QStringLiteral("t1"), e);
        t.setTopicError(m.id, QStringLiteral("t2"), e);
        t.clearTopicError(m.id, QStringLiteral("t2"));
    }
    library::MeetingJobTracker t2(m_store.get());
    const QHash<QString, JobError> errs = t2.topicErrors(m.id);
    QCOMPARE(errs.size(), 1);
    QCOMPARE(errs.value(QStringLiteral("t1")).message, QStringLiteral("időtúllépés"));
    QCOMPARE(errs.value(QStringLiteral("t1")).detail, QStringLiteral("HTTP 504"));
}

void JobTrackerTest::identifyState()
{
    Meeting m = newMeeting();
    m.hasTranscript = true;
    m_store->saveMeeting(m);
    {
        library::MeetingJobTracker t(m_store.get());
        QCOMPARE(t.state(m.id).identifyState, StepState::None);
        t.begin(m.id, JobKind::Identify, QStringLiteral("Résztvevők azonosítása"));
        t.setCounts(m.id, JobKind::Identify, 3, 5);
        QCOMPARE(t.state(m.id).identifyState, StepState::Running);
        QCOMPARE(t.job(m.id, JobKind::Identify).done, 3);
        QCOMPARE(t.job(m.id, JobKind::Identify).total, 5);
        t.finish(m.id, JobKind::Identify);
        QCOMPARE(t.state(m.id).identifyState, StepState::None);   // not marked yet
        t.markIdentified(m.id);
        QCOMPARE(t.state(m.id).identifyState, StepState::Done);
    }
    library::MeetingJobTracker t2(m_store.get());
    QCOMPARE(t2.state(m.id).identifyState, StepState::Done);       // after a restart too
    QVERIFY(t2.identifiedAt(m.id).isValid());
    t2.markIdentified(m.id, false);
    QCOMPARE(t2.state(m.id).identifyState, StepState::None);

    // The transcription job's "identify" stage counts as a running identification too.
    t2.begin(m.id, JobKind::Transcribe, QStringLiteral("Átírás"),
             {{QStringLiteral("identify"), QStringLiteral("Azonosítás"), StageState::Waiting, -1, QString()}});
    QCOMPARE(t2.state(m.id).identifyState, StepState::None);
    t2.setStage(m.id, JobKind::Transcribe, QStringLiteral("identify"), StageState::Running, 40);
    QCOMPARE(t2.state(m.id).identifyState, StepState::Running);
}

void JobTrackerTest::mixdownState()
{
    library::MeetingJobTracker t(m_store.get());
    const Meeting m = newMeeting();
    t.begin(m.id, JobKind::Mixdown, QStringLiteral("Lekeverés"));
    t.setPercent(m.id, JobKind::Mixdown, 37);
    MeetingProcessingState s = t.state(m.id);
    QVERIFY(s.mixdownRunning);
    QCOMPARE(s.mixdownPercent, 37);
    QCOMPARE(s.transcriptState, StepState::None);   // a standalone mix is not "transcription running"
    t.cancelled(m.id, JobKind::Mixdown);
    QVERIFY(!t.state(m.id).mixdownRunning);

    // A mix running as part of the transcription: the "mixdown" stage's percentage.
    t.begin(m.id, JobKind::Transcribe, QStringLiteral("Átírás"),
            {{QStringLiteral("mixdown"), QStringLiteral("Lekeverés"), StageState::Waiting, -1, QString()}});
    t.setStage(m.id, JobKind::Transcribe, QStringLiteral("mixdown"), StageState::Running, 12);
    s = t.state(m.id);
    QVERIFY(s.mixdownRunning);
    QCOMPARE(s.mixdownPercent, 12);
    QCOMPARE(s.transcriptState, StepState::Running);
}

void JobTrackerTest::removedMeetingForgotten()
{
    library::MeetingJobTracker t(m_store.get());
    const Meeting m = newMeeting();
    t.begin(m.id, JobKind::Summarize, QStringLiteral("x"));
    QVERIFY(m_store->deleteMeeting(m.id));
    QVERIFY(!t.isBusy(m.id));
    // Closing on an already deleted meeting: does not crash, creates no file.
    JobError e;
    e.message = QStringLiteral("hiba");
    t.fail(m.id, JobKind::Summarize, e);
    QVERIFY(!QDir(m.folder).exists());
}

// ---- JobErrors -------------------------------------------------------------------------

void JobTrackerTest::describeHttp401()
{
    net::HttpExchange ex;
    ex.method = "POST";
    ex.path = QStringLiteral("/v1/files");
    ex.status = 401;
    ex.body = R"({"status_code":401,"error_type":"invalid_api_key","message":"Invalid API key."})";
    const JobError e = library::describeJobFailure(JobKind::Transcribe,
                                          QStringLiteral("Feltöltés hiba: Host requires authentication"), &ex);
    QVERIFY(e.isValid());
    QCOMPARE(e.kind, JobKind::Transcribe);
    QCOMPARE(e.detail, QStringLiteral("HTTP 401 · invalid_api_key · Invalid API key."));
    QVERIFY(e.message.contains(QStringLiteral("API-kulcs")));
    QVERIFY(!e.message.contains(QStringLiteral("Host requires")));   // human text, not the raw one
    QCOMPARE(e.fixActionHint, QStringLiteral("settings:stt"));

    // OpenAI-compatible body.
    ex.body = R"({"error":{"message":"Incorrect API key provided","type":"invalid_request_error","code":"invalid_api_key"}})";
    const JobError e2 = library::describeJobFailure(JobKind::Summarize, QStringLiteral("LLM hiba"), &ex);
    QCOMPARE(e2.detail, QStringLiteral("HTTP 401 · invalid_api_key · Incorrect API key provided"));
    QCOMPARE(e2.fixActionHint, QStringLiteral("settings:llm"));
}

void JobTrackerTest::describeNetworkAndServerErrors()
{
    net::HttpExchange net;
    net.method = "POST";
    net.status = 0;
    net.networkError = QStringLiteral("Connection refused");
    const JobError e = library::describeJobFailure(JobKind::Summarize, QStringLiteral("Hálózati hiba: Connection refused"), &net);
    QVERIFY(e.message.contains(QStringLiteral("elérni")));
    QVERIFY(e.detail.contains(QStringLiteral("Connection refused")));

    net::HttpExchange srv;
    srv.status = 503;
    srv.body = "upstream down";
    const JobError e5 = library::describeJobFailure(JobKind::Transcribe, QStringLiteral("x"), &srv);
    QCOMPARE(e5.detail, QStringLiteral("HTTP 503 · upstream down"));
    QVERIFY(e5.fixActionHint.isEmpty());

    net::HttpExchange rate;
    rate.status = 429;
    QCOMPARE(library::describeJobFailure(JobKind::Transcribe, QStringLiteral("x"), &rate).detail, QStringLiteral("HTTP 429"));
}

void JobTrackerTest::describeWithoutExchange()
{
    const JobError e = library::describeJobFailure(JobKind::Summarize,
                                          QStringLiteral("Nem sikerült JSON-ként értelmezni a választ"), nullptr);
    QCOMPARE(e.message, QStringLiteral("Nem sikerült JSON-ként értelmezni a választ"));
    QVERIFY(e.detail.isEmpty());
    QVERIFY(library::describeJobFailure(JobKind::Mixdown, QString(), nullptr).isValid());   // never empty
}

void JobTrackerTest::describeCloud()
{
    const cloud::CloudError ce = cloud::parseCloudError(402, {},
        R"({"error":{"code":"insufficient_balance","message":"Nincs elég egyenleg.","request_id":"req_1"}})");
    QVERIFY(ce.isError());
    const JobError e = library::describeCloudFailure(JobKind::Transcribe, cloud::toError(ce));
    QCOMPARE(e.message, QStringLiteral("Nincs elég egyenleg."));
    QCOMPARE(e.detail, QStringLiteral("HTTP 402 · insufficient_balance · req_1"));
}

// ---- JobStats --------------------------------------------------------------------------

void JobTrackerTest::statsEstimateOnlyFromSamples()
{
    const QString path = m_dir->filePath("meta/job-stats.json");
    {
        library::JobStats st(path);
        QCOMPARE(st.estimateSec(QStringLiteral("transcribe/soniox/x"), 3600), -1);   // no estimate without samples
        st.addSample(QStringLiteral("transcribe/soniox/x"), 60, 600);   // 0.1 ratio
        QCOMPARE(st.estimateSec(QStringLiteral("transcribe/soniox/x"), 3600), 360);
        QCOMPARE(st.estimateSec(QStringLiteral("transcribe/egyeb/x"), 3600), -1);    // does not carry over to another provider
        st.addSample(QStringLiteral("transcribe/soniox/x"), 5, 2);      // too short a sample: left out
        QCOMPARE(st.sampleCount(QStringLiteral("transcribe/soniox/x")), 1);
    }
    library::JobStats st2(path);   // persistent
    QCOMPARE(st2.estimateSec(QStringLiteral("transcribe/soniox/x"), 1200), 120);
    st2.addSample(QStringLiteral("transcribe/soniox/x"), 120, 600);     // 0.2 gives a moving average of 0.13
    QCOMPARE(st2.estimateSec(QStringLiteral("transcribe/soniox/x"), 1000), 130);
}

#endif // Q_MOC_RUN

QTEST_GUILESS_MAIN(JobTrackerTest)
#include "test_job_tracker.moc"
