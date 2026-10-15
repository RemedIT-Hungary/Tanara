// processing::SummarizationService + TopicQueue on their own (without AppController): the quick summary,
// the complex flow (topic extraction -> per-topic analyses -> reduce), one topic's failure not stopping the
// queue, cancelling (the quick call, one topic, the whole batch), and the Tanara Cloud run bookkeeping
// through the app port (one run for the whole complex summary, closed once). A scripted fake chat provider
// (registry) and a fake SummaryLlmPort; made-up content, ISOLATED TANARA_HOME, no LLM / network.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>

#include "tanara/library/jobs/JobStats.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/summary/SummaryRepository.h"
#include "tanara/processing/summary/SummarizationService.h"
#include "tanara/processing/summary/TopicQueue.h"
#include "tanara/providers/chat/IChatProvider.h"
#include "tanara/providers/chat/ILocalModelServer.h"
#include "tanara/providers/chat/LlmServer.h"
#include "tanara/providers/core/ProviderRegistry.h"

#include <functional>
#include <memory>

using namespace tanara;
using processing::SummarizationService;
using processing::SummaryLlmPort;
using processing::TopicQueue;

namespace {

const QString kChatId = QStringLiteral("fake-chat");

class ScriptedJob : public providers::LlmJob {
    Q_OBJECT
public:
    explicit ScriptedJob(QObject* parent) : providers::LlmJob(parent) {}
    void cancel() override { cancelled = true; }
    bool cancelled = false;
};

// What the fake model answers to one request: a reply, an error, or nothing (hang).
struct Answer {
    enum Kind { Reply, Fail, Hang } kind = Reply;
    QString text;
};

// The script shared by the chat providers the registry creates (one per LLM call).
struct ChatScript {
    std::function<Answer(const QString& system, const QString& user)> answer;
    int calls = 0;
};

class ScriptedChat : public QObject, public providers::IChatProvider {
    Q_OBJECT
public:
    ScriptedChat(ChatScript* script, QObject* parent) : QObject(parent), m_script(script) {}
    QString name() const override { return kChatId; }
    bool supportsStreaming() const override { return false; }
    providers::LlmJob* chat(const providers::LlmRequest& req) override
    {
        ++m_script->calls;
        QString system, user;
        for (const providers::ChatMessage& msg : req.messages)
            (msg.role == QLatin1String("system") ? system : user) += msg.content;
        const Answer a = m_script->answer ? m_script->answer(system, user) : Answer{};
        auto* job = new ScriptedJob(this);
        if (a.kind == Answer::Reply)
            QTimer::singleShot(0, job, [job, t = a.text]() { emit job->finished(t); });
        else if (a.kind == Answer::Fail)
            QTimer::singleShot(0, job, [job, t = a.text]() { emit job->failed(t); });
        return job;
    }

private:
    ChatScript* m_script;
};

// The app side: local by default; in cloud mode it hands out counted runs.
class FakePort : public SummaryLlmPort {
public:
    struct FakeRun : RunHandle { QString kind; int id = 0; };

    QString baseUrl;              // the local config's address (the dead-server test points it at a closed port)
    bool cloud = false;
    bool cloudError = false;      // the run's last gateway error (cloud mode)
    int runsCreated = 0;
    QStringList finished;         // kind of each finished run
    int failed = 0;

    providers::ReadinessResult readiness(const QString&) const override
    {
        providers::ReadinessResult r;
        r.runnable = true;
        return r;
    }
    processing::ProcessingSettings processingSettings() const override { return {}; }
    QString chatProviderId() const override { return kChatId; }
    providers::ProviderConfig localConfig() const override { return config0(); }
    bool cloudSelected() const override { return cloud; }
    Run newRun(const QString&, const QString& kind) override
    {
        auto r = std::make_shared<FakeRun>();
        r->kind = kind;
        r->id = ++runsCreated;
        return r;
    }
    void setRunKind(const Run& run, const QString& kind) override
    {
        if (auto* r = static_cast<FakeRun*>(run.get())) r->kind = kind;
    }
    providers::ProviderConfig config(Run& run, const QString& meetingId, const QString& kind) override
    {
        if (cloud && !run) run = newRun(meetingId, kind);
        return config0();
    }
    providers::ProviderContext context(const Run&, const QString&) const override { return {}; }
    void finishRun(const Run& run) override
    {
        if (auto* r = static_cast<FakeRun*>(run.get())) finished << r->kind;
    }
    bool failRun(const Run& run) override
    {
        if (!run) return false;
        ++failed;
        return true;
    }
    bool runError(const Run& run, Error* out) const override
    {
        if (!run || !cloudError) return false;
        if (out) { out->kind = Error::Kind::Quota; out->message = QStringLiteral("Nincs egyenleg"); }
        return true;
    }
    QString runErrorMessage(const Run& run) const override
    {
        return run && cloudError ? QStringLiteral("Nincs egyenleg") : QString();
    }
    void refreshAccount(const Run&) override {}

private:
    providers::ProviderConfig config0() const
    {
        providers::ProviderConfig c;
        c.type = kChatId;
        c.baseUrl = baseUrl;
        c.model = QStringLiteral("teszt-modell");
        c.maxTokens = 2000;
        return c;
    }
};

const QString kQuickJson =
    QStringLiteral(R"({"execSummary":"Rövid összefoglaló.","decisions":["Döntés"],"actionItems":[],"participants":[]})");
const QString kAnalysis = QStringLiteral("Elemzés.\n\n## Döntések\n- d\n");
const QString kTopics = QStringLiteral("## Alfa téma\nAz első.\n\n## Béta téma\nA második.\n\n## Gamma téma\nA harmadik.\n");

// Collects the Qt log lines (still forwarding them to the test framework's handler).
QStringList g_log;
QtMessageHandler g_prevHandler = nullptr;
void catchLog(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    g_log << msg;
    if (g_prevHandler) g_prevHandler(type, ctx, msg);
}
struct LogCatcher {
    LogCatcher() { g_log.clear(); g_prevHandler = qInstallMessageHandler(catchLog); }
    ~LogCatcher() { qInstallMessageHandler(g_prevHandler); }
    QString joined() const { return g_log.join(QLatin1Char('\n')); }
};

bool waitFor(const std::function<bool()>& cond, int ms = 8000)
{
    QDeadlineTimer dl(ms);
    while (!cond() && !dl.hasExpired()) QTest::qWait(10);
    return cond();
}

} // namespace

class TestSummarizationService : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;
    std::unique_ptr<library::MeetingRepository> m_store;
    std::unique_ptr<library::MeetingJobTracker> m_jobs;
    std::unique_ptr<library::JobStats> m_stats;
    providers::ProviderRegistry m_registry;
    bool m_realPreparer = false;   // the registry hands out the real LM Studio preparer (else: nothing to prepare)
    ChatScript m_script;
    FakePort m_port;
    std::unique_ptr<SummarizationService> m_svc;
    QStringList m_errors;
    int m_n = 0;

    // A meeting with a short two-speaker transcript on disk.
    Meeting transcribed(const QString& title)
    {
        Meeting m = m_store->createMeeting(title);
        QJsonArray toks;
        const QStringList words{"Sziasztok,", " az", " árajánlatot", " Ödön", " küldi."};
        for (int i = 0; i < words.size(); ++i)
            toks.append(QJsonObject{{"text", words[i]}, {"speaker", i < 3 ? "Beszélő 1" : "Beszélő 2"},
                                    {"startMs", i * 300}, {"endMs", i * 300 + 250}, {"confidence", 0.9},
                                    {"trackId", "mixdown"}});
        QFile f(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(QJsonObject{{"language", "hu"}, {"tokens", toks}}).toJson());
        f.close();
        m.hasTranscript = true;
        m_store->saveMeeting(m);
        return m_store->load(m.id);
    }

    QVector<SummaryTopic> threeTopics()
    {
        return { {QStringLiteral("t-a"), QStringLiteral("Alfa téma"), QStringLiteral("Az első.")},
                 {QStringLiteral("t-b"), QStringLiteral("Béta téma"), QStringLiteral("A második.")},
                 {QStringLiteral("t-c"), QStringLiteral("Gamma téma"), QStringLiteral("A harmadik.")} };
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        qputenv("TANARA_HOME", m_tmp.filePath(QStringLiteral("home")).toUtf8());
        m_registry.add<providers::IChatProvider>(providers::ProviderDescriptor{kChatId},
            [this](const providers::ProviderContext&, const providers::ProviderConfig&, QObject* p)
                -> providers::IChatProvider* { return new ScriptedChat(&m_script, p); });
        m_registry.add<providers::ILocalModelServer>(providers::ProviderDescriptor{kChatId},
            [this](const providers::ProviderContext& ctx, const providers::ProviderConfig& cfg, QObject* p)
                -> providers::ILocalModelServer* {
                return m_realPreparer ? new providers::LlmModelPreparer(ctx, cfg, p) : nullptr;
            });
    }

    void init()
    {
        const QString root = m_tmp.filePath(QStringLiteral("rec%1").arg(++m_n));
        QDir().mkpath(root);
        m_store = std::make_unique<library::MeetingRepository>(QStringList{root}, m_tmp.filePath(QStringLiteral("meta%1").arg(m_n)));
        m_jobs = std::make_unique<library::MeetingJobTracker>(m_store.get());
        m_stats = std::make_unique<library::JobStats>();
        m_port = FakePort{};
        m_script = ChatScript{};
        m_errors.clear();
        m_realPreparer = false;
        SummarizationService::Deps d;
        d.store = m_store.get();
        d.jobs = m_jobs.get();
        d.jobStats = m_stats.get();
        d.registry = &m_registry;
        d.llm = &m_port;
        d.notesDir = m_tmp.filePath(QStringLiteral("notes%1").arg(m_n));
        d.resolveSpeakers = [](MergedTranscript& mt, const Meeting&) {
            for (TranscriptToken& t : mt.tokens)
                if (t.speaker == QStringLiteral("Beszélő 1")) t.speaker = QStringLiteral("Zénó");
        };
        m_svc = std::make_unique<SummarizationService>(d);
        connect(m_svc.get(), &SummarizationService::errorOccurred, this, [this](const QString& e) { m_errors << e; });
        connect(&m_svc->topics(), &TopicQueue::errorOccurred, this, [this](const QString& e) { m_errors << e; });
    }

    void cleanup()
    {
        m_svc.reset();
        QTest::qWait(20);   // deleteLater of the finished calls
        m_jobs.reset();
        m_store.reset();
    }

    // Quick: one call for a short meeting; summary.md + summary.json + the notes copy, hasSummary, the job
    // closed; the LLM saw the resolved speaker name.
    void quickSummary()
    {
        QString seenUser;
        m_script.answer = [&seenUser](const QString&, const QString& user) {
            seenUser = user;
            return Answer{Answer::Reply, kQuickJson};
        };
        const Meeting m = transcribed(QStringLiteral("Gyors összefoglaló"));
        QSignalSpy ready(m_svc.get(), &SummarizationService::summaryReady);
        m_svc->summarize(m.id);
        QVERIFY(m_jobs->isRunning(m.id, JobKind::Summarize));
        QVERIFY(ready.wait(5000));
        QCOMPARE(ready.at(0).at(0).toString(), m.id);
        QCOMPARE(ready.at(0).at(1).toString(), library::SummaryRepository::markdownPath(m.folder));
        QVERIFY(!m_jobs->isRunning(m.id, JobKind::Summarize));
        QCOMPARE(m_script.calls, 1);
        QVERIFY2(seenUser.contains(QStringLiteral("Zénó")), qPrintable(seenUser.left(600)));

        const library::SummaryDocument doc = m_svc->document(m.id);
        QVERIFY(doc.exists);
        QCOMPARE(doc.meta.mode, library::SummaryMode::Quick);
        QCOMPARE(doc.meta.providerId, kChatId);
        QCOMPARE(doc.meta.model, QStringLiteral("teszt-modell"));
        QCOMPARE(doc.summary.execSummary, QStringLiteral("Rövid összefoglaló."));
        QVERIFY(m_store->load(m.id).hasSummary);
        QCOMPARE(QDir(m_tmp.filePath(QStringLiteral("notes%1").arg(m_n))).entryList(QDir::Files).size(), 1);
        QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));

        // A provider error: the task's error, the earlier summary stays, the fallback message goes out.
        m_script.answer = [](const QString&, const QString&) { return Answer{Answer::Fail, QStringLiteral("boom")}; };
        QSignalSpy finished(m_jobs.get(), &library::MeetingJobTracker::jobFinished);
        m_svc->summarize(m.id);
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Failed);
        QCOMPARE(m_errors.size(), 1);
        QVERIFY(m_svc->document(m.id).exists);
    }

    // The bug of 2026-10-15: the LLM provider is configured but LM Studio is not running. Nothing may be
    // sent to the dead address: the preparer's probe finds it unreachable, the task fails at once with the
    // address + the transport's reason, the failure is logged (provider, address) and persisted for the
    // Summary tab's error card; the chat provider is never called.
    void deadLocalServerFailsBeforeTheCall()
    {
        quint16 port = 0;
        {
            QTcpServer closed;   // a free port, closed again: nothing listens there
            QVERIFY(closed.listen(QHostAddress::LocalHost, 0));
            port = closed.serverPort();
        }
        m_port.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(port);
        m_realPreparer = true;
        providers::LlmServerProbe::invalidateCache();
        m_script.answer = [](const QString&, const QString&) { return Answer{Answer::Reply, kQuickJson}; };
        const Meeting m = transcribed(QStringLiteral("Halott szerver"));
        LogCatcher log;
        QSignalSpy finished(m_jobs.get(), &library::MeetingJobTracker::jobFinished);
        m_svc->summarize(m.id);
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Failed);
        QCOMPARE(m_script.calls, 0);

        const QString root = QStringLiteral("http://127.0.0.1:%1").arg(port);
        const JobError e = m_jobs->lastError(m.id, JobKind::Summarize);
        QVERIFY(e.isValid());
        QCOMPARE(e.kind, JobKind::Summarize);
        QVERIFY2(e.message.contains(QStringLiteral("A helyi LLM nem érhető el: ") + root), qPrintable(e.message));
        QVERIFY2(e.message.contains(QStringLiteral("fut az LM Studio?")), qPrintable(e.message));
        // detail: provider · address · the transport's reason
        const QStringList parts = e.detail.split(QStringLiteral(" · "));
        QCOMPARE(parts.size(), 4);
        QCOMPARE(parts.at(1), kChatId);
        QCOMPARE(parts.at(2), m_port.baseUrl);
        QVERIFY2(!parts.at(3).trimmed().isEmpty(), qPrintable(e.detail));
        QCOMPARE(e.fixActionHint, QStringLiteral("settings:llm-unreachable"));
        QCOMPARE(m_errors.size(), 1);
        QVERIFY2(m_errors.at(0).contains(root), qPrintable(m_errors.at(0)));
        QVERIFY(!m_store->load(m.id).hasSummary);
        const QString logged = log.joined();
        QVERIFY2(logged.contains(QStringLiteral("provider=") + kChatId), qPrintable(logged));
        QVERIFY2(logged.contains(QStringLiteral("url=") + m_port.baseUrl), qPrintable(logged));
    }

    // A live server that is NOT LM Studio (it answers the native probe with 404) must keep working: no
    // short-circuit, the chat call goes out.
    void liveNonLmStudioServerIsNotShortCircuited()
    {
        QTcpServer live;
        QVERIFY(live.listen(QHostAddress::LocalHost, 0));
        connect(&live, &QTcpServer::newConnection, &live, [&live] {
            while (QTcpSocket* c = live.nextPendingConnection()) {
                connect(c, &QTcpSocket::readyRead, c, [c] {
                    c->readAll();
                    c->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    c->disconnectFromHost();
                });
            }
        });
        m_port.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(live.serverPort());
        m_realPreparer = true;
        providers::LlmServerProbe::invalidateCache();
        m_script.answer = [](const QString&, const QString&) { return Answer{Answer::Reply, kQuickJson}; };
        const Meeting m = transcribed(QStringLiteral("Élő, nem LM Studio"));
        QSignalSpy ready(m_svc.get(), &SummarizationService::summaryReady);
        m_svc->summarize(m.id);
        QVERIFY(ready.wait(10000));
        QCOMPARE(m_script.calls, 1);
        QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));
    }

    // A call that fails mid-way (the server died after the probe): the failure is logged with the provider,
    // the address and the raw reason, and kept for the error card.
    void chatFailureIsLogged()
    {
        m_port.baseUrl = QStringLiteral("http://localhost:1234/v1");
        m_script.answer = [](const QString&, const QString&) {
            return Answer{Answer::Fail, QStringLiteral("Hálózati hiba: Connection refused")};
        };
        const Meeting m = transcribed(QStringLiteral("Napló"));
        LogCatcher log;
        QSignalSpy finished(m_jobs.get(), &library::MeetingJobTracker::jobFinished);
        m_svc->summarize(m.id);
        QVERIFY(finished.wait(5000));
        QVERIFY(m_jobs->lastError(m.id, JobKind::Summarize).isValid());
        const QString logged = log.joined();
        QVERIFY2(logged.contains(QStringLiteral("provider=") + kChatId), qPrintable(logged));
        QVERIFY2(logged.contains(QStringLiteral("url=http://localhost:1234/v1")), qPrintable(logged));
        QVERIFY2(logged.contains(QStringLiteral("hiba=Hálózati hiba: Connection refused")), qPrintable(logged));
    }

    // Complex: topic extraction (persisted) -> analyses of every topic -> reduce, the merged document.
    void complexSummary()
    {
        const Meeting m = transcribed(QStringLiteral("Komplex összefoglaló"));
        m_script.answer = [](const QString&, const QString&) { return Answer{Answer::Reply, kTopics}; };
        QSignalSpy topicsReady(&m_svc->topics(), &TopicQueue::topicsReady);
        m_svc->topics().extractTopics(m.id);
        QVERIFY(topicsReady.wait(5000));
        const QVector<SummaryTopic> topics = m_svc->topics().topics(m.id);
        QCOMPARE(topics.size(), 3);
        QCOMPARE(topics[1].title, QStringLiteral("Béta téma"));
        QVERIFY(!m_jobs->isRunning(m.id, JobKind::ExtractTopics));
        // An existing list is returned without a new call.
        const int calls = m_script.calls;
        m_svc->topics().extractTopics(m.id);
        QCOMPARE(topicsReady.count(), 2);
        QCOMPARE(m_script.calls, calls);

        m_script.answer = [](const QString&, const QString& user) {
            // The reduce gets the analyses (no transcript); everything else is a topic analysis.
            if (!user.contains(QStringLiteral("Sziasztok")))
                return Answer{Answer::Reply, QStringLiteral("A megbeszélés röviden: rendben.")};
            return Answer{Answer::Reply, kAnalysis};
        };
        QSignalSpy analysisReady(&m_svc->topics(), &TopicQueue::topicAnalysisReady);
        QSignalSpy queueFinished(&m_svc->topics(), &TopicQueue::topicAnalysisQueueFinished);
        QSignalSpy summaryReady(m_svc.get(), &SummarizationService::summaryReady);
        m_svc->topics().generate(m.id, topics);
        QVERIFY(m_jobs->isRunning(m.id, JobKind::AnalyzeTopics));
        QVERIFY(summaryReady.wait(8000));
        QCOMPARE(analysisReady.count(), 3);
        QCOMPARE(queueFinished.count(), 1);
        QCOMPARE(queueFinished.at(0).at(1).toInt(), 3);
        QCOMPARE(queueFinished.at(0).at(2).toInt(), 0);
        for (const TopicStatus& st : m_svc->topics().statuses(m.id)) QCOMPARE(st.state, TopicState::Done);
        const library::SummaryDocument doc = m_svc->document(m.id);
        QCOMPARE(doc.meta.mode, library::SummaryMode::Topics);
        QCOMPARE(doc.topics.size(), 3);
        QCOMPARE(doc.topics[0].topicId, topics[0].id);
        QCOMPARE(doc.summary.execSummary, QStringLiteral("A megbeszélés röviden: rendben."));
        QVERIFY(!m_jobs->isBusy(m.id));
        QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));
    }

    // One topic's failure: the other topics go on, the batch closes with 2 ok / 1 failed, no automatic
    // reduce; the failed topic shows its error; rerunning it alone works.
    void failingTopicDoesNotStopQueue()
    {
        const Meeting m = transcribed(QStringLiteral("Hibás téma"));
        m_script.answer = [](const QString&, const QString& user) {
            if (user.contains(QStringLiteral("Béta téma"))) return Answer{Answer::Fail, QStringLiteral("overloaded")};
            return Answer{Answer::Reply, kAnalysis};
        };
        QSignalSpy analysisReady(&m_svc->topics(), &TopicQueue::topicAnalysisReady);
        QSignalSpy analysisFailed(&m_svc->topics(), &TopicQueue::topicAnalysisFailed);
        QSignalSpy queueFinished(&m_svc->topics(), &TopicQueue::topicAnalysisQueueFinished);
        QSignalSpy summaryReady(m_svc.get(), &SummarizationService::summaryReady);
        QSignalSpy jobFinished(m_jobs.get(), &library::MeetingJobTracker::jobFinished);
        m_svc->topics().generate(m.id, threeTopics());
        QVERIFY(queueFinished.wait(8000));
        QCOMPARE(analysisReady.count(), 2);
        QCOMPARE(analysisFailed.count(), 1);
        QCOMPARE(analysisFailed.at(0).at(1).toString(), QStringLiteral("t-b"));
        QCOMPARE(queueFinished.at(0).at(1).toInt(), 2);
        QCOMPARE(queueFinished.at(0).at(2).toInt(), 1);
        QTest::qWait(100);
        QCOMPARE(summaryReady.count(), 0);
        QCOMPARE(jobFinished.last().at(2).value<JobOutcome>(), JobOutcome::Failed);
        const QVector<TopicStatus> sts = m_svc->topics().statuses(m.id);
        QCOMPARE(sts[0].state, TopicState::Done);
        QCOMPARE(sts[1].state, TopicState::Failed);
        QCOMPARE(sts[2].state, TopicState::Done);
        QCOMPARE(m_svc->topics().analyses(m.id).size(), 2);

        m_script.answer = [](const QString&, const QString&) { return Answer{Answer::Reply, kAnalysis}; };
        m_svc->topics().analyze(m.id, threeTopics()[1]);
        QVERIFY(analysisReady.wait(5000));
        QTRY_VERIFY(!m_jobs->isBusy(m.id));
        for (const TopicStatus& st : m_svc->topics().statuses(m.id)) QCOMPARE(st.state, TopicState::Done);
        QCOMPARE(summaryReady.count(), 0);   // a single topic's rerun does not reduce by itself
    }

    // Cancelling: the quick call (no result, the task cancelled), one queued topic, the whole batch.
    void cancel()
    {
        const Meeting m = transcribed(QStringLiteral("Megszakítás"));
        QVERIFY(!m_svc->cancel(m.id, JobKind::Summarize));   // nothing runs

        m_script.answer = [](const QString&, const QString&) { return Answer{Answer::Hang, QString()}; };
        QSignalSpy ready(m_svc.get(), &SummarizationService::summaryReady);
        QSignalSpy jobFinished(m_jobs.get(), &library::MeetingJobTracker::jobFinished);
        m_svc->summarize(m.id);
        QVERIFY(waitFor([this] { return m_script.calls == 1; }));
        QVERIFY(m_svc->cancel(m.id, JobKind::Summarize));
        QCOMPARE(jobFinished.count(), 1);
        QCOMPARE(jobFinished.at(0).at(2).value<JobOutcome>(), JobOutcome::Cancelled);
        QVERIFY(!m_svc->cancel(m.id, JobKind::Summarize));

        // The topic batch: the first analysis hangs, the others wait; one queued topic is removed, then the batch.
        QSignalSpy failed(&m_svc->topics(), &TopicQueue::topicAnalysisFailed);
        QSignalSpy queueFinished(&m_svc->topics(), &TopicQueue::topicAnalysisQueueFinished);
        m_svc->topics().generate(m.id, threeTopics());
        QVERIFY(waitFor([this] { return m_script.calls == 2; }));
        QVector<TopicStatus> sts = m_svc->topics().statuses(m.id);
        QCOMPARE(sts[0].state, TopicState::Running);
        QCOMPARE(sts[1].state, TopicState::Queued);
        QVERIFY(m_svc->topics().cancelTopic(m.id, QStringLiteral("t-c")));
        QCOMPARE(m_svc->topics().statuses(m.id)[2].state, TopicState::Waiting);
        QCOMPARE(m_jobs->job(m.id, JobKind::AnalyzeTopics).total, 2);

        QVERIFY(m_svc->cancel(m.id, JobKind::AnalyzeTopics));
        QCOMPARE(queueFinished.count(), 1);
        QCOMPARE(jobFinished.last().at(2).value<JobOutcome>(), JobOutcome::Cancelled);
        QCOMPARE(failed.count(), 3);   // t-c (removed), t-b (queued), t-a (running)
        QTest::qWait(100);
        sts = m_svc->topics().statuses(m.id);
        for (const TopicStatus& st : sts) QCOMPARE(st.state, TopicState::Waiting);
        QCOMPARE(m_script.calls, 2);   // nothing else started
        QCOMPARE(ready.count(), 0);
        QVERIFY(!m_jobs->isBusy(m.id));
        QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));
    }

    // Tanara Cloud: the complex summary is ONE run (topic gathering -> analyses -> reduce), closed once; a
    // gateway error drops the meeting's remaining topics and fails the run.
    void cloudRunBookkeeping()
    {
        m_port.cloud = true;
        const Meeting m = transcribed(QStringLiteral("Felhős komplex"));
        m_script.answer = [](const QString&, const QString&) { return Answer{Answer::Reply, kTopics}; };
        QSignalSpy topicsReady(&m_svc->topics(), &TopicQueue::topicsReady);
        m_svc->topics().extractTopics(m.id);
        QVERIFY(topicsReady.wait(5000));
        QCOMPARE(m_port.runsCreated, 1);
        QVERIFY(m_port.finished.isEmpty());   // the run stays open for the analyses

        m_script.answer = [](const QString&, const QString& user) {
            if (!user.contains(QStringLiteral("Sziasztok"))) return Answer{Answer::Reply, QStringLiteral("Röviden.")};
            return Answer{Answer::Reply, kAnalysis};
        };
        QSignalSpy summaryReady(m_svc.get(), &SummarizationService::summaryReady);
        m_svc->topics().generate(m.id, m_svc->topics().topics(m.id));
        QVERIFY(summaryReady.wait(8000));
        QCOMPARE(m_port.runsCreated, 1);
        QCOMPARE(m_port.finished, QStringList{QStringLiteral("complex")});

        // A new batch with a gateway error on the first topic: the rest is dropped, the run fails.
        const Meeting m2 = transcribed(QStringLiteral("Felhős hiba"));
        m_port.cloudError = true;
        m_script.answer = [](const QString&, const QString&) { return Answer{Answer::Fail, QStringLiteral("402")}; };
        QSignalSpy failed(&m_svc->topics(), &TopicQueue::topicAnalysisFailed);
        QSignalSpy queueFinished(&m_svc->topics(), &TopicQueue::topicAnalysisQueueFinished);
        m_svc->topics().generate(m2.id, threeTopics());
        QVERIFY(queueFinished.wait(5000));
        QCOMPARE(m_script.calls, 1 + 3 + 1 + 1);   // topics + 3 analyses + reduce, then ONE failing call
        QCOMPARE(failed.count(), 3);
        QCOMPARE(failed.at(0).at(2).toString(), QStringLiteral("Nincs egyenleg"));
        QCOMPARE(failed.at(1).at(2).toString(), QStringLiteral("Megszakítva"));   // the dropped ones
        QCOMPARE(m_port.failed, 1);
        QCOMPARE(m_port.runsCreated, 2);
        QCOMPARE(m_port.finished.size(), 1);
    }

    // The shared prompt / request builder (the facade's cost estimate uses the same instance).
    void requestBuilder()
    {
        processing::ProcessingSettings s;
        s.summaryPrompt = QStringLiteral("Saját prompt.");
        const Meeting m = transcribed(QStringLiteral("Kérés"));
        MergedTranscript mt;
        const processing::SummaryRequest req = SummarizationService::requestFor(s, mt, m, QStringLiteral("x"), 0);
        QVERIFY(req.singlePrompt.startsWith(QStringLiteral("Saját prompt.")));
        QCOMPARE(req.maxTokens, 8000);
        QCOMPARE(req.notesMaxTokens, 3000);
        QVERIFY(req.cachePath.startsWith(m.folder));
        QVERIFY(!SummarizationService::resolvedPrompt(s, QString(), "reduce").isEmpty());
    }
};

QTEST_MAIN(TestSummarizationService)
#include "test_summarization_service.moc"
