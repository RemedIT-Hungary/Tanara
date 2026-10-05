//
// Tanara Cloud — az X-Tanara-Job-Id életciklusa az AppController-ben (grill-döntés 29:
// egy feldolgozás-futás = egy azonosító; folytatás = új futás).
//
// A teszt a mock-gatewayt indítja (node, kannázott LLM), izolált HOME-mal építi fel az
// AppControllert (TANARA_CLOUD=live), device flow-val bejelentkezik, és a mock
// usage-naplójából (GET /__mock/state) ellenőrzi a futásonkénti job id-ket:
//   - komplex összefoglaló: témagyűjtés + téma-elemzések + összegzés = EGY job id,
//     a K-07 (cloudCharged) egyszer, az összes hívással;
//   - megszakítás (hiba) utáni folytatás: új job id, csak a hátralévő hívásokkal;
//   - gyors összefoglaló: rövid megbeszélésnél egy hívás, saját job id; hosszúnál a részenkénti
//     jegyzetek + az összegzés EGY futás (egy job id, egy K-07); hiba utáni folytatás új futás.
// Node nélkül QSKIP.
//
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

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/cloud/CloudAccount.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/store/MeetingStore.h"

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
    std::unique_ptr<AppController> m_app;
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

    // Izolált HOME (~/.tanara a temp alatt) + élő cloud-mód a mock ellen.
    qputenv("HOME", m_home.path().toUtf8());
    qputenv("USERPROFILE", m_home.path().toUtf8());   // Windowson a QDir::homePath() ezt olvassa
    qputenv("TANARA_CLOUD", "live");
    qputenv("TANARA_CLOUD_URL", m_base.toUtf8());
    QDir().mkpath(m_home.filePath(".tanara"));

    m_app = std::make_unique<AppController>();
    QVERIFY(m_app->cloudLive());
    connect(m_app.get(), &AppController::errorOccurred, this, [](const QString& e) { qWarning("errorOccurred: %s", qPrintable(e)); });

    AppSettings s = m_app->settings()->settings();
    s.llmProviderId = cloud::ProviderId;
    s.llmConfigs[cloud::ProviderId].type = cloud::ProviderId;
    m_app->settings()->setSettings(s);
    QVERIFY(m_app->usesCloud(WorkflowStep::Summarize));

    mockConfig({ { "auto_approve", true } });
    QSignalSpy ok(m_app->cloud(), &CloudAccount::deviceFlowSucceeded);
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

// A teljes komplex út (témagyűjtés → szerkesztés nélkül elemzés → összegzés) egy futás.
void TestCloudJobId::complex_oneRunOneJobId()
{
    const QString id = newMeetingWithTranscript(QStringLiteral("Komplex — egy futás"));
    QVERIFY(!id.isEmpty());
    const int before = llmUsage().size();

    QSignalSpy charged(m_app.get(), &AppController::cloudCharged);
    QSignalSpy errors(m_app.get(), &AppController::cloudError);
    QSignalSpy done(m_app.get(), &AppController::summaryReady);
    auto conn = connect(m_app.get(), &AppController::topicsReady, this,
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
    // Kannázott: 2 téma → témagyűjtés + 2 elemzés + összegzés.
    QCOMPARE(calls, 4);
    QCOMPARE(jobs.size(), 1);
    QVERIFY(!jobs.values().first().isEmpty());

    // K-07: egyszer, a futás összes hívásával.
    QCOMPARE(charged.size(), 1);
    QCOMPARE(charged.first().at(1).toString(), QStringLiteral("complex"));
    QCOMPARE(charged.first().at(3).toInt(), 4);
}

// Hiba az elemzés közben → a futás lezárul; a „Folytatás” új futás, új job id-vel, és csak
// a hátralévő részeket hívja.
void TestCloudJobId::complex_continuationIsNewRun()
{
    const QString id = newMeetingWithTranscript(QStringLiteral("Komplex — folytatás"));
    QVERIFY(!id.isEmpty());
    const int before = llmUsage().size();

    // A témagyűjtés + az első elemzés sikerül, a második hívás 500-at kap.
    mockConfig({ { "fail_chat_after", 2 } });
    QSignalSpy errors(m_app.get(), &AppController::cloudError);
    QVector<SummaryTopic> topics;
    auto conn = connect(m_app.get(), &AppController::topicsReady, this,
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

    // Folytatás (K-12 „Folytatás”): ugyanaz a hívás, mint a GUI-ban.
    mockConfig({ { "fail_chat_after", QJsonValue() } });
    QSignalSpy charged(m_app.get(), &AppController::cloudCharged);
    QSignalSpy done(m_app.get(), &AppController::summaryReady);
    m_app->generateComplexSummary(id, topics);
    QVERIFY(done.wait(20000));

    all = llmUsage();
    QCOMPARE(all.size() - before, 4);   // + a hátralévő elemzés + összegzés
    const QString secondJob = all[before + 2].toObject().value("job_id").toString();
    QVERIFY(!secondJob.isEmpty());
    QVERIFY(secondJob != firstJob);
    QCOMPARE(all[before + 3].toObject().value("job_id").toString(), secondJob);
    QCOMPARE(charged.size(), 1);
    QCOMPARE(charged.first().at(3).toInt(), 2);
}

// A gyors összefoglaló változatlan: egy hívás, saját (új) job id, quick mód.
void TestCloudJobId::quick_ownJobId()
{
    const QString id = newMeetingWithTranscript(QStringLiteral("Gyors"));
    QVERIFY(!id.isEmpty());
    const QJsonArray prev = llmUsage();
    QSet<QString> earlier;
    for (const QJsonValue& v : prev) earlier.insert(v.toObject().value("job_id").toString());

    QSignalSpy charged(m_app.get(), &AppController::cloudCharged);
    QSignalSpy done(m_app.get(), &AppController::summaryReady);
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

// Hosszú (több részes) átirat: félpercenként egy bekezdés.
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

// Hosszú megbeszélés gyors összefoglalója: 3 jegyzet-hívás + 1 összegzés = EGY futás (egy job
// id), a K-07 egyszer, mind a négy hívással. Hiba közben: a futás lezárul (részleges terheléssel),
// a folytatás új futás, és csak a hátralévő hívásokat indítja.
void TestCloudJobId::quick_multiPartOneRun()
{
    const QString id = newLongMeeting(QStringLiteral("Hosszú gyors"), 40);
    QVERIFY(!id.isEmpty());
    const EstimateRequest est = m_app->makeEstimateRequest(id, QStringLiteral("summarize"), QStringLiteral("quick"));
    QCOMPARE(est.llmCalls.size(), 2);
    QCOMPARE(est.llmCalls[0].count, 3);
    QCOMPARE(est.llmCalls[1].count, 1);

    int before = llmUsage().size();
    QSignalSpy charged(m_app.get(), &AppController::cloudCharged);
    QSignalSpy done(m_app.get(), &AppController::summaryReady);
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
    const SummaryDocument doc = m_app->summaryDocument(id);
    QCOMPARE(doc.summary.execSummary,
             QStringLiteral("A csapat a MuseumPlus migrációt egyeztette; a szállítás jövő hétre került."));
    // A kannázott jegyzet minden részben az egész részt egy (hasonló című) tárgynak írja → az
    // összevonás 20 percnél megáll: részenként egy szakasz.
    QCOMPARE(doc.summary.memo.size(), 3);
    QCOMPARE(doc.summary.memo[0].startMs, 0);
    QCOMPARE(doc.summary.memo[2].startMs, 30 * 60000);

    // Hiba a 2. jegyzet-hívásnál (az első sikeres, terhelt).
    const QString id2 = newLongMeeting(QStringLiteral("Hosszú gyors — hiba"), 40);
    before = llmUsage().size();
    mockConfig({ { "fail_chat_after", 1 } });
    QSignalSpy errors(m_app.get(), &AppController::cloudError);
    m_app->summarizeMeeting(id2);
    QVERIFY(errors.wait(20000));
    QCOMPARE(errors.first().at(1).toString(), QStringLiteral("summary"));
    QVERIFY(errors.first().at(3).value<Money>().micros > 0);    // részleges terhelés: nem „semmi”
    all = llmUsage();
    QCOMPARE(all.size() - before, 1);
    const QString firstJob = all[before].toObject().value("job_id").toString();

    // Folytatás: új futás, új job id; csak a 2–3. rész + az összegzés.
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
