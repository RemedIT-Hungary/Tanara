// processing::TranscriptionService on its own (without AppController): one STT step over a meeting's mixdown
// with a fake STT provider and a fake TranscriptionPort. The save (transcript files, hasTranscript, the
// speaker map's reset), the stages it reports into the caller's Transcribe job, cancel, failure (with and
// without a gateway report), an empty result, an unknown provider, a tier without diarization and a meeting
// deleted meanwhile. Isolated TANARA_HOME; no network.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "fake_stt.h"

#include "tanara/library/jobs/JobStats.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/processing/stt/TranscriptionService.h"

#include <memory>

using namespace tanara;
using processing::TranscriptionService;
using library::TranscriptRepository;

class TranscriptionServiceTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void savesTranscriptAndReportsStages();
    void speakerMapClearedWithNewTranscript();
    void cancelAtProvider();
    void failureClosesJobWithError();
    void emptyResultKeepsOldTranscript();
    void unknownProviderFailsAtOnce();
    void tierWithoutDiarization();
    void meetingDeletedMeanwhile();

private:
    Meeting meeting(const QString& title);
    void beginJob(const QString& id);
    JobOutcome lastOutcome(const QString& id) const;

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<library::MeetingRepository> m_store;
    std::unique_ptr<library::MeetingJobTracker> m_jobs;
    std::unique_ptr<library::JobStats> m_stats;
    providers::ProviderRegistry m_registry;
    std::shared_ptr<testing::FakeSttLog> m_log;
    std::unique_ptr<testing::FakeTranscriptionPort> m_port;
    std::unique_ptr<TranscriptionService> m_svc;
    QStringList m_errors;
    QHash<QString, JobOutcome> m_outcomes;
};

void TranscriptionServiceTest::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    qputenv("TANARA_HOME", m_home->path().toUtf8());
    m_store = std::make_unique<library::MeetingRepository>(m_home->filePath(QStringLiteral("rec")),
                                                           m_home->filePath(QStringLiteral("meta")));
    m_jobs = std::make_unique<library::MeetingJobTracker>(m_store.get());
    m_stats = std::make_unique<library::JobStats>();
    m_log = std::make_shared<testing::FakeSttLog>();
    testing::registerFakeStt(m_registry, m_log);
    m_port = std::make_unique<testing::FakeTranscriptionPort>();
    m_svc = std::make_unique<TranscriptionService>(
        TranscriptionService::Deps{m_store.get(), m_jobs.get(), m_stats.get(), &m_registry, m_port.get()});
    m_errors.clear();
    m_outcomes.clear();
    connect(m_svc.get(), &TranscriptionService::errorOccurred, this, [this](const QString& e) { m_errors << e; });
    connect(m_jobs.get(), &library::MeetingJobTracker::jobFinished, this,
            [this](const QString& id, JobKind kind, JobOutcome o) {
                if (kind == JobKind::Transcribe) m_outcomes.insert(id, o);
            });
}

void TranscriptionServiceTest::cleanup()
{
    m_svc.reset();
    m_port.reset();
    m_jobs.reset();
    m_store.reset();
    m_home.reset();
}

Meeting TranscriptionServiceTest::meeting(const QString& title)
{
    Meeting m = m_store->createMeeting(title);
    m.durationMs = 60000;
    m.mixdownFile = QStringLiteral("mixdown.mp3");
    QFile f(QDir(m.folder).filePath(m.mixdownFile));
    if (f.open(QIODevice::WriteOnly)) f.write("mix");
    f.close();
    m_store->saveMeeting(m);
    return m;
}

void TranscriptionServiceTest::beginJob(const QString& id)
{
    QVector<JobStage> stages;
    for (const char* s : {"upload", "transcribe", "diarize"})
        stages.append({QString::fromLatin1(s), QString::fromLatin1(s), StageState::Waiting, -1, QString()});
    m_jobs->begin(id, JobKind::Transcribe, QStringLiteral("Átírás"), stages);
}

JobOutcome TranscriptionServiceTest::lastOutcome(const QString& id) const { return m_outcomes.value(id, JobOutcome::Done); }

void TranscriptionServiceTest::savesTranscriptAndReportsStages()
{
    const Meeting m = meeting(QStringLiteral("Heti egyeztetés"));
    beginJob(m.id);
    QSignalSpy ready(m_svc.get(), &TranscriptionService::transcriptReady);
    QSignalSpy transcribed(m_svc.get(), &TranscriptionService::transcribed);
    QSignalSpy progress(m_svc.get(), &TranscriptionService::jobProgress);
    const QString stampBefore = m_svc->transcriptStamp(m.id, m.folder);

    QVERIFY(m_svc->start(m.id));
    QVERIFY(m_svc->running(m.id));
    QCOMPARE(m_log->jobs.size(), 1);
    const providers::SttRequest req = m_log->requests.first();
    QCOMPARE(req.audioFilePath, QDir(m.folder).filePath(QStringLiteral("mixdown.mp3")));
    QCOMPARE(req.trackId, QStringLiteral("mixdown"));
    QVERIFY(req.diarization);
    QCOMPARE(req.contextGeneral.value(QStringLiteral("Megbeszélés")), QStringLiteral("Heti egyeztetés"));
    QCOMPARE(m_jobs->job(m.id, JobKind::Transcribe).stage(QStringLiteral("upload"))->state, StageState::Running);

    testing::FakeSttJob* job = m_log->jobs.first();
    emit job->stateChanged(SttJobState::Queued);
    JobProgress jp = m_jobs->job(m.id, JobKind::Transcribe);
    QCOMPARE(jp.stage(QStringLiteral("upload"))->state, StageState::Done);
    QCOMPARE(jp.stage(QStringLiteral("transcribe"))->state, StageState::Running);
    QCOMPARE(jp.stage(QStringLiteral("diarize"))->state, StageState::Running);
    QVERIFY(!progress.isEmpty());

    emit job->finished(testing::sttResult({{"1", "Sziasztok,"}, {"2", " jó"}, {"2", " reggelt."}}));
    QVERIFY(!m_svc->running(m.id));
    QCOMPARE(ready.size(), 1);
    QCOMPARE(transcribed.size(), 1);
    QCOMPARE(ready.first().at(1).toString(), TranscriptRepository::markdownPath(m.folder));
    const Meeting after = m_store->load(m.id);
    QVERIFY(after.hasTranscript);
    QVERIFY(TranscriptRepository::hasTokens(m.folder));
    const MergedTranscript merged = TranscriptRepository::readTokens(m.folder);
    QCOMPARE(merged.tokens.size(), 3);
    QCOMPARE(merged.tokens.first().speaker, QStringLiteral("speaker:1"));
    QCOMPARE(merged.tokens.last().speaker, QStringLiteral("speaker:2"));
    QCOMPARE(m_svc->cachedTranscript(m.id).tokens.size(), 3);
    QVERIFY(m_svc->transcriptStamp(m.id, m.folder) != stampBefore);
    // The step does not close the job: the caller continues (identification).
    jp = m_jobs->job(m.id, JobKind::Transcribe);
    QVERIFY(jp.isValid());
    QCOMPARE(jp.stage(QStringLiteral("transcribe"))->state, StageState::Done);
    QCOMPARE(jp.stage(QStringLiteral("diarize"))->state, StageState::Done);
    QVERIFY(m_jobs->isRunning(m.id, JobKind::Transcribe));
    QVERIFY(m_errors.isEmpty());

    m_svc->forget(m.id);
    QVERIFY(m_svc->cachedTranscript(m.id).tokens.isEmpty());
}

void TranscriptionServiceTest::speakerMapClearedWithNewTranscript()
{
    Meeting m = meeting(QStringLiteral("Nevek"));
    m.speakerMap.insert(QStringLiteral("speaker:1"), PersonId::generate());
    m_store->saveMeeting(m);

    // First transcript, no flag: the (pre-set) map stays.
    beginJob(m.id);
    QVERIFY(m_svc->start(m.id, /*clearSpeakers*/ false));
    emit m_log->jobs.last()->finished(testing::sttResult({{"1", "Egy"}}));
    QCOMPARE(m_store->load(m.id).speakerMap.size(), 1);
    m_jobs->finish(m.id, JobKind::Transcribe);

    // A transcript existed: the new one clears the map even without the flag.
    beginJob(m.id);
    QVERIFY(m_svc->start(m.id, false));
    emit m_log->jobs.last()->finished(testing::sttResult({{"1", "Kettő"}}));
    QVERIFY(m_store->load(m.id).speakerMap.isEmpty());
    m_jobs->finish(m.id, JobKind::Transcribe);

    // The flag clears on a first transcript too.
    Meeting n = meeting(QStringLiteral("Új"));
    n.speakerMap.insert(QStringLiteral("speaker:1"), PersonId::generate());
    m_store->saveMeeting(n);
    beginJob(n.id);
    QVERIFY(m_svc->start(n.id, true));
    emit m_log->jobs.last()->finished(testing::sttResult({{"1", "Három"}}));
    QVERIFY(m_store->load(n.id).speakerMap.isEmpty());
}

void TranscriptionServiceTest::cancelAtProvider()
{
    m_port->cloud = true;
    const Meeting m = meeting(QStringLiteral("Megszakítás"));
    beginJob(m.id);
    QVERIFY(!m_svc->cancel(m.id));   // nothing runs yet
    QVERIFY(m_svc->start(m.id));
    QVERIFY(m_svc->cancel(m.id));
    QVERIFY(m_log->jobs.first()->cancelled);
    QVERIFY(!m_svc->running(m.id));
    QCOMPARE(lastOutcome(m.id), JobOutcome::Cancelled);
    QVERIFY(!m_jobs->lastError(m.id, JobKind::Transcribe).isValid());
    QVERIFY(m_errors.isEmpty());
    QCOMPARE(m_port->cancelled, 1);
    QCOMPARE(m_port->failed, 0);
    QVERIFY(!m_store->load(m.id).hasTranscript);
}

void TranscriptionServiceTest::failureClosesJobWithError()
{
    const Meeting m = meeting(QStringLiteral("Hiba"));
    beginJob(m.id);
    QVERIFY(m_svc->start(m.id));
    emit m_log->jobs.first()->failed(QStringLiteral("HTTP 401: invalid_api_key"));
    QVERIFY(!m_svc->running(m.id));
    QCOMPARE(lastOutcome(m.id), JobOutcome::Failed);
    QVERIFY(m_jobs->lastError(m.id, JobKind::Transcribe).isValid());
    QCOMPARE(m_errors.size(), 1);   // no gateway report: the fallback message
    QVERIFY(!m_store->load(m.id).hasTranscript);

    // A cloud run whose gateway reported the error: no own message.
    m_port->cloud = true;
    m_port->reportFailure = true;
    beginJob(m.id);
    QVERIFY(m_svc->start(m.id));
    emit m_log->jobs.last()->failed(QStringLiteral("boom"));
    QCOMPARE(m_port->failed, 1);
    QCOMPARE(m_errors.size(), 1);
    QCOMPARE(lastOutcome(m.id), JobOutcome::Failed);
}

void TranscriptionServiceTest::emptyResultKeepsOldTranscript()
{
    const Meeting m = meeting(QStringLiteral("Üres"));
    beginJob(m.id);
    QVERIFY(m_svc->start(m.id));
    emit m_log->jobs.last()->finished(testing::sttResult({{"1", "Megvan"}}));
    m_jobs->finish(m.id, JobKind::Transcribe);
    const QByteArray before = [&] {
        QFile f(TranscriptRepository::tokensPath(m.folder));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }();

    beginJob(m.id);
    QSignalSpy transcribed(m_svc.get(), &TranscriptionService::transcribed);
    QVERIFY(m_svc->start(m.id));
    emit m_log->jobs.last()->finished(TrackTranscript{});
    QCOMPARE(transcribed.size(), 0);
    QCOMPARE(lastOutcome(m.id), JobOutcome::Failed);
    QCOMPARE(m_errors.size(), 1);
    QFile f(TranscriptRepository::tokensPath(m.folder));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), before);
}

void TranscriptionServiceTest::unknownProviderFailsAtOnce()
{
    m_port->providerId = QStringLiteral("nincs-ilyen");
    const Meeting m = meeting(QStringLiteral("Ismeretlen"));
    beginJob(m.id);
    QVERIFY(!m_svc->start(m.id));
    QVERIFY(!m_svc->running(m.id));
    QCOMPARE(lastOutcome(m.id), JobOutcome::Failed);
    QCOMPARE(m_errors.size(), 1);
    QVERIFY(m_errors.first().contains(QStringLiteral("nincs-ilyen")));

    // An unknown meeting: the job closes as cancelled.
    beginJob(QStringLiteral("nincs"));
    QVERIFY(!m_svc->start(QStringLiteral("nincs")));
    QCOMPARE(lastOutcome(QStringLiteral("nincs")), JobOutcome::Cancelled);
}

void TranscriptionServiceTest::tierWithoutDiarization()
{
    m_port->diarization = false;
    const Meeting m = meeting(QStringLiteral("Gyors"));
    beginJob(m.id);
    QVERIFY(m_svc->start(m.id));
    const providers::SttRequest req = m_log->requests.first();
    QVERIFY(!req.diarization);
    QCOMPARE(req.speakerLabel, QStringLiteral("1"));
    QCOMPARE(m_jobs->job(m.id, JobKind::Transcribe).stage(QStringLiteral("diarize"))->state, StageState::Skipped);
    emit m_log->jobs.first()->stateChanged(SttJobState::Processing);
    QCOMPARE(m_jobs->job(m.id, JobKind::Transcribe).stage(QStringLiteral("diarize"))->state, StageState::Skipped);
}

void TranscriptionServiceTest::meetingDeletedMeanwhile()
{
    m_port->cloud = true;
    const Meeting m = meeting(QStringLiteral("Törölt"));
    beginJob(m.id);
    QVERIFY(m_svc->start(m.id));
    QVERIFY(m_store->deleteMeeting(m.id));
    QSignalSpy transcribed(m_svc.get(), &TranscriptionService::transcribed);
    emit m_log->jobs.first()->finished(testing::sttResult({{"1", "Késő"}}));
    QCOMPARE(transcribed.size(), 0);
    QVERIFY(!m_svc->running(m.id));
    QVERIFY(!m_jobs->isRunning(m.id, JobKind::Transcribe));
    QCOMPARE(m_port->finished, 1);   // the run still closes (any charge shows up)
    QVERIFY(m_errors.isEmpty());
}

QTEST_GUILESS_MAIN(TranscriptionServiceTest)
#include "test_transcription_service.moc"
