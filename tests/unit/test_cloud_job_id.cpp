// Tanara Cloud: the lifecycle of X-Tanara-Job-Id in AppController (one processing run = one
// id; resuming = a new run).
//
// The test starts the mock-gateway (node, canned LLM), builds the AppController with an
// isolated HOME (TANARA_CLOUD=live), signs in with the device flow, and checks the per-run job
// ids from the mock's usage log (GET /__mock/state):
//   - complex summary: topic gathering + topic analyses + final summary = ONE job id, the
//     charge notice (cloudCharged) once, with all the calls;
//   - resuming after a cancel (error): a new job id, only the remaining calls;
//   - quick summary: for a short meeting one call, its own job id; for a long one the part
//     notes + the final summary are ONE run (one job id, one charge notice); resuming after an error is a new run.
// Without node: QSKIP.
#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>
#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "tanara/app/AppController.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/cloud/CloudAccount.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/library/store/MeetingStore.h"

#include <memory>

#ifndef TANARA_MOCK_GATEWAY
#error "TANARA_MOCK_GATEWAY (a mock-gateway.mjs útja) nincs megadva"
#endif

using namespace tanara;

class TestCloudJobId : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void complex_oneRunOneJobId();
    void complex_continuationIsNewRun();
    void quick_ownJobId();
    void quick_multiPartOneRun();

private:
    void mockConfig(const QJsonObject& cfg);
    QJsonArray llmUsage();
    QString newMeetingWithTranscript(const QString& title);
    QString newLongMeeting(const QString& title, int minutes);

    QProcess m_mock;
    QString m_base;
    QTemporaryDir m_home;
    QNetworkAccessManager m_nam;
    std::unique_ptr<app::AppController> m_app;
};

#ifndef Q_MOC_RUN

void TestCloudJobId::initTestCase()
{
    if (!cloud::clientCompiled())
        QSKIP("a cloud-kliens nincs befordítva (TANARA_BUILD_CLOUD=OFF)");
    const QString node = QStandardPaths::findExecutable(QStringLiteral("node"));
    if (node.isEmpty())
        QSKIP("node nem található — a mock-gateway integrációs teszt kihagyva");
    QVERIFY(m_home.isValid());

    const int port = 19300 + QRandomGenerator::global()->bounded(1000);
    m_base = QStringLiteral("http://127.0.0.1:%1").arg(port);
    m_mock.setProgram(node);
    m_mock.setArguments({ QStringLiteral(TANARA_MOCK_GATEWAY), "--port", QString::number(port),
                          "--llm", "off", "--balance", "10" });
    m_mock.setProcessChannelMode(QProcess::MergedChannels);
    m_mock.start();
    QVERIFY(m_mock.waitForStarted(5000));
    QByteArray out;
    QDeadlineTimer dl(8000);
    while (!out.contains("mock-gateway 1.2.0") && !dl.hasExpired()) {
        m_mock.waitForReadyRead(200);
        out += m_mock.readAll();
    }
    QVERIFY2(out.contains("mock-gateway 1.2.0"), out.constData());

    // Isolated HOME (~/.tanara under temp) + live cloud mode against the mock.
    qputenv("HOME", m_home.path().toUtf8());
    // On Windows HOME does not change QDir::homePath(), so TANARA_HOME points under temp too.
    qputenv("TANARA_HOME", m_home.filePath(".tanara").toUtf8());
    qputenv("TANARA_CLOUD", "live");
    qputenv("TANARA_CLOUD_URL", m_base.toUtf8());
    QDir().mkpath(m_home.filePath(".tanara"));

    m_app = std::make_unique<app::AppController>();
    QVERIFY(m_app->cloudLive());
    connect(m_app.get(), &app::AppController::errorOccurred, this, [](const QString& e) { qWarning("errorOccurred: %s", qPrintable(e)); });

    AppSettings s = m_app->settings()->settings();
    s.llmProviderId = cloud::ProviderId;
    s.llmConfigs[cloud::ProviderId].type = cloud::ProviderId;
    m_app->settings()->setSettings(s);
    QVERIFY(m_app->usesCloud(providers::WorkflowStep::Summarize));

    mockConfig({ { "auto_approve", true } });
    QSignalSpy ok(m_app->cloud(), &cloud::CloudAccount::deviceFlowSucceeded);
    m_app->cloud()->startDeviceFlow();
    QVERIFY(ok.wait(10000));
    QVERIFY(m_app->cloud()->isLoggedIn());
}

void TestCloudJobId::cleanupTestCase()
{
    m_app.reset();
    if (m_mock.state() != QProcess::NotRunning) {
        m_mock.kill();
        m_mock.waitForFinished(3000);
    }
}

void TestCloudJobId::mockConfig(const QJsonObject& cfg)
{
    QNetworkRequest r(QUrl(m_base + "/__mock/config"));
    r.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* rep = m_nam.post(r, QJsonDocument(cfg).toJson(QJsonDocument::Compact));
    QSignalSpy fin(rep, &QNetworkReply::finished);
    QVERIFY(fin.wait(5000));
    rep->deleteLater();
}

QJsonArray TestCloudJobId::llmUsage()
{
    QNetworkReply* rep = m_nam.get(QNetworkRequest(QUrl(m_base + "/__mock/state")));
    QSignalSpy fin(rep, &QNetworkReply::finished);
    fin.wait(5000);
    rep->deleteLater();
    QJsonArray llm;
    for (const QJsonValue& v : QJsonDocument::fromJson(rep->readAll()).object().value("usage").toArray())
        if (v.toObject().value("kind").toString() == QLatin1String("llm")) llm.append(v);
    return llm;
}

QString TestCloudJobId::newMeetingWithTranscript(const QString& title)
{
    Meeting m = m_app->store()->createMeeting(title);
    m.hasTranscript = true;
    m_app->store()->saveMeeting(m);
    QFile f(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
    if (!f.open(QIODevice::WriteOnly)) return {};
    f.write(QByteArrayLiteral(
        "{\"language\":\"hu\",\"tokens\":["
        "{\"text\":\"Sziasztok, a MuseumPlus migráció a téma.\",\"speaker\":\"Ádám\",\"startMs\":0,\"endMs\":2500,"
        "\"confidence\":0.99,\"trackId\":\"mic\"},"
        "{\"text\":\" Dompa megírja a tesztet.\",\"speaker\":\"Béla\",\"startMs\":2600,\"endMs\":4200,"
        "\"confidence\":0.99,\"trackId\":\"mic\"}]}"));
    return m.id;
}

// The whole complex path (topic gathering, analysis without editing, summary) is one run.
void TestCloudJobId::complex_oneRunOneJobId()
{
    const QString id = newMeetingWithTranscript(QStringLiteral("Komplex — egy futás"));
    QVERIFY(!id.isEmpty());
    const int before = llmUsage().size();

    QSignalSpy charged(m_app.get(), &app::AppController::cloudCharged);
    QSignalSpy errors(m_app.get(), &app::AppController::cloudError);
    QSignalSpy done(m_app.get(), &app::AppController::summaryReady);
    auto conn = connect(m_app.get(), &app::AppController::topicsReady, this,
                        [this](const QString& mid, const QVector<SummaryTopic>& topics) {
        m_app->generateComplexSummary(mid, topics);
    });
    m_app->extractMeetingTopics(id);
    QVERIFY(done.wait(20000));
    disconnect(conn);
    QCOMPARE(errors.size(), 0);

    const QJsonArray all = llmUsage();
    QSet<QString> jobs;
    int calls = 0;
    for (int i = before; i < all.size(); ++i) {
        const QJsonObject u = all[i].toObject();
        QCOMPARE(u.value("summary_mode").toString(), QStringLiteral("complex"));
        jobs.insert(u.value("job_id").toString());
        ++calls;
    }
    // Canned: 2 topics: topic gathering + 2 analyses + summary.
    QCOMPARE(calls, 4);
    QCOMPARE(jobs.size(), 1);
    QVERIFY(!jobs.values().first().isEmpty());

    // Charge notice: once, with all the run's calls.
    QCOMPARE(charged.size(), 1);
    QCOMPARE(charged.first().at(1).toString(), QStringLiteral("complex"));
    QCOMPARE(charged.first().at(3).toInt(), 4);
}

// An error during analysis ends the run; "Resume" is a new run with a new job id, and it
// calls only the remaining parts.
void TestCloudJobId::complex_continuationIsNewRun()
{
    const QString id = newMeetingWithTranscript(QStringLiteral("Komplex — folytatás"));
    QVERIFY(!id.isEmpty());
    const int before = llmUsage().size();

    // Topic gathering + the first analysis succeed, the second call gets a 500.
    mockConfig({ { "fail_chat_after", 2 } });
    QSignalSpy errors(m_app.get(), &app::AppController::cloudError);
    QVector<SummaryTopic> topics;
    auto conn = connect(m_app.get(), &app::AppController::topicsReady, this,
                        [this, &topics](const QString& mid, const QVector<SummaryTopic>& t) {
        topics = t;
        m_app->generateComplexSummary(mid, t);
    });
    m_app->extractMeetingTopics(id);
    QVERIFY(errors.wait(20000));
    disconnect(conn);
    QCOMPARE(errors.first().at(1).toString(), QStringLiteral("complex"));
    QCOMPARE(topics.size(), 2);

    QJsonArray all = llmUsage();
    QCOMPARE(all.size() - before, 2);
    const QString firstJob = all[before].toObject().value("job_id").toString();
    QVERIFY(!firstJob.isEmpty());
    QCOMPARE(all[before + 1].toObject().value("job_id").toString(), firstJob);

    // Resume: the same call as in the GUI.
    mockConfig({ { "fail_chat_after", QJsonValue() } });
    QSignalSpy charged(m_app.get(), &app::AppController::cloudCharged);
    QSignalSpy done(m_app.get(), &app::AppController::summaryReady);
    m_app->generateComplexSummary(id, topics);
    QVERIFY(done.wait(20000));

    all = llmUsage();
    QCOMPARE(all.size() - before, 4);   // + the remaining analysis + summary
    const QString secondJob = all[before + 2].toObject().value("job_id").toString();
    QVERIFY(!secondJob.isEmpty());
    QVERIFY(secondJob != firstJob);
    QCOMPARE(all[before + 3].toObject().value("job_id").toString(), secondJob);
    QCOMPARE(charged.size(), 1);
    QCOMPARE(charged.first().at(3).toInt(), 2);
}

// The quick summary is unchanged: one call, its own (new) job id, quick mode.
void TestCloudJobId::quick_ownJobId()
{
    const QString id = newMeetingWithTranscript(QStringLiteral("Gyors"));
    QVERIFY(!id.isEmpty());
    const QJsonArray prev = llmUsage();
    QSet<QString> earlier;
    for (const QJsonValue& v : prev) earlier.insert(v.toObject().value("job_id").toString());

    QSignalSpy charged(m_app.get(), &app::AppController::cloudCharged);
    QSignalSpy done(m_app.get(), &app::AppController::summaryReady);
    m_app->summarizeMeeting(id);
    QVERIFY(done.wait(20000));

    const QJsonArray all = llmUsage();
    QCOMPARE(all.size() - prev.size(), 1);
    const QJsonObject u = all.last().toObject();
    QCOMPARE(u.value("summary_mode").toString(), QStringLiteral("quick"));
    QVERIFY(!u.value("job_id").toString().isEmpty());
    QVERIFY(!earlier.contains(u.value("job_id").toString()));
    QCOMPARE(charged.size(), 1);
    QCOMPARE(charged.first().at(1).toString(), QStringLiteral("summary"));
}

// Long (multi-part) transcript: one paragraph every half minute.
QString TestCloudJobId::newLongMeeting(const QString& title, int minutes)
{
    Meeting m = m_app->store()->createMeeting(title);
    m.hasTranscript = true;
    m.durationMs = qint64(minutes) * 60000;
    m_app->store()->saveMeeting(m);
    QJsonArray toks;
    for (int i = 0; i < minutes * 2; ++i)
        toks.append(QJsonObject{{"text", QStringLiteral(" A MuseumPlus migráció %1. bekezdése: a csapat a "
                                                        "szállítás ütemezését, a tesztelést és a felelősöket "
                                                        "egyezteti, hosszan és részletesen.").arg(i)},
                                {"speaker", i % 2 ? "Béla" : "Ádám"}, {"startMs", i * 30000},
                                {"endMs", i * 30000 + 20000}, {"confidence", 0.99}, {"trackId", "mic"}});
    QFile f(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
    if (!f.open(QIODevice::WriteOnly)) return {};
    f.write(QJsonDocument(QJsonObject{{"language", "hu"}, {"tokens", toks}}).toJson());
    return m.id;
}

// Quick summary of a long meeting: 3 note calls + 1 summary = ONE run (one job id), the
// charge notice once, with all four calls. On an error: the run ends (with a partial charge),
// the resume is a new run, and it starts only the remaining calls.
void TestCloudJobId::quick_multiPartOneRun()
{
    const QString id = newLongMeeting(QStringLiteral("Hosszú gyors"), 40);
    QVERIFY(!id.isEmpty());
    const cloud::EstimateRequest est = m_app->makeEstimateRequest(id, QStringLiteral("summarize"), QStringLiteral("quick"));
    QCOMPARE(est.llmCalls.size(), 2);
    QCOMPARE(est.llmCalls[0].count, 3);
    QCOMPARE(est.llmCalls[1].count, 1);

    int before = llmUsage().size();
    QSignalSpy charged(m_app.get(), &app::AppController::cloudCharged);
    QSignalSpy done(m_app.get(), &app::AppController::summaryReady);
    m_app->summarizeMeeting(id);
    QVERIFY(done.wait(20000));
    QJsonArray all = llmUsage();
    QCOMPARE(all.size() - before, 4);
    QSet<QString> jobs;
    for (int i = before; i < all.size(); ++i) {
        QCOMPARE(all[i].toObject().value("summary_mode").toString(), QStringLiteral("quick"));
        jobs.insert(all[i].toObject().value("job_id").toString());
    }
    QCOMPARE(jobs.size(), 1);
    QCOMPARE(charged.size(), 1);
    QCOMPARE(charged.first().at(1).toString(), QStringLiteral("summary"));
    QCOMPARE(charged.first().at(3).toInt(), 4);
    const library::SummaryDocument doc = m_app->summaryDocument(id);
    QCOMPARE(doc.summary.execSummary,
             QStringLiteral("A csapat a MuseumPlus migrációt egyeztette; a szállítás jövő hétre került."));
    // The canned note writes the whole part as one (similarly titled) subject in every part, so
    // the merge stops at 20 minutes: one section per part.
    QCOMPARE(doc.summary.memo.size(), 3);
    QCOMPARE(doc.summary.memo[0].startMs, 0);
    QCOMPARE(doc.summary.memo[2].startMs, 30 * 60000);

    // An error at the 2nd note call (the first succeeded, charged).
    const QString id2 = newLongMeeting(QStringLiteral("Hosszú gyors — hiba"), 40);
    before = llmUsage().size();
    mockConfig({ { "fail_chat_after", 1 } });
    QSignalSpy errors(m_app.get(), &app::AppController::cloudError);
    m_app->summarizeMeeting(id2);
    QVERIFY(errors.wait(20000));
    QCOMPARE(errors.first().at(1).toString(), QStringLiteral("summary"));
    QVERIFY(errors.first().at(3).value<cloud::Money>().micros > 0);    // partial charge: not "nothing"
    all = llmUsage();
    QCOMPARE(all.size() - before, 1);
    const QString firstJob = all[before].toObject().value("job_id").toString();

    // Resume: a new run, a new job id; only parts 2-3 + the summary.
    mockConfig({ { "fail_chat_after", QJsonValue() } });
    charged.clear();
    m_app->summarizeMeeting(id2);
    QVERIFY(done.wait(20000));
    all = llmUsage();
    QCOMPARE(all.size() - before, 4);
    const QString secondJob = all[before + 1].toObject().value("job_id").toString();
    QVERIFY(!secondJob.isEmpty());
    QVERIFY(secondJob != firstJob);
    for (int i = before + 1; i < all.size(); ++i)
        QCOMPARE(all[i].toObject().value("job_id").toString(), secondJob);
    QCOMPARE(charged.size(), 1);
    QCOMPARE(charged.first().at(3).toInt(), 3);
}

#endif // Q_MOC_RUN

QTEST_GUILESS_MAIN(TestCloudJobId)
#include "test_cloud_job_id.moc"
