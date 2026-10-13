// Tanara Cloud: integration test against mock-gateway 1.2.0 (tests/mock-gateway/mock-gateway.mjs).
//
// The test starts the mock (node, no dependencies) on a free port, with canned STT/LLM, and
// walks through the client's gateway paths: waitlist, device flow, account, catalog, estimate,
// terms acceptance, top-up link, error states (402/403/426/429/503), and the existing
// SonioxProvider / OpenAiCompatibleProvider with the gateway config (headers, charge, write-back).
// Without node the test is skipped (QSKIP).
#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "tanara/domain/Types.h"
#include "tanara/cloud/CloudAccount.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/providers/chat/OpenAiCompatibleProvider.h"
#include "tanara/foundation/KeyStore.h"
#include "tanara/providers/stt/SonioxProvider.h"

#ifndef TANARA_MOCK_GATEWAY
#error "TANARA_MOCK_GATEWAY (a mock-gateway.mjs útja) nincs megadva"
#endif

using namespace tanara;

class TestCloudGateway : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void waitlist_acceptedAndErrors();
    void deviceFlow_login_account_models();
    void deviceFlow_denied_and_cancel();
    void estimate_levels();
    void terms_earlyAcceptance();
    void topup_unavailable();
    void errors_blockingStates();
    void minClient_detected();
    void stt_throughGateway_chargeAndRefund();
    void llm_throughGateway_charge_and402();
    void pending_refundAtStartup();
    void logout_removesKey();

private:
    void mockConfig(const QJsonObject& cfg);
    QJsonObject mockState();
    ProviderConfig gatewayConfig(const QString& model, QVector<cloud::HttpExchange>* log);

    QProcess m_mock;
    int m_port = 0;
    QString m_base;
    QTemporaryDir m_dir;
    std::unique_ptr<KeyStore> m_keys;
    std::unique_ptr<cloud::CloudAccount> m_acc;
    QNetworkAccessManager m_nam;
};

#ifndef Q_MOC_RUN

void TestCloudGateway::initTestCase()
{
    const QString node = QStandardPaths::findExecutable(QStringLiteral("node"));
    if (node.isEmpty())
        QSKIP("node nem található — a mock-gateway integrációs teszt kihagyva");
    QVERIFY(m_dir.isValid());
    m_port = 18300 + QRandomGenerator::global()->bounded(1000);
    m_base = QStringLiteral("http://127.0.0.1:%1").arg(m_port);
    m_mock.setProgram(node);
    m_mock.setArguments({ QStringLiteral(TANARA_MOCK_GATEWAY), "--port", QString::number(m_port),
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

    m_keys = std::make_unique<KeyStore>(m_dir.filePath("secrets.json"));
    m_acc = std::make_unique<cloud::CloudAccount>(m_keys.get(), m_dir.path());
    m_acc->setBaseUrl(m_base + "/v1/");      // the /v1 and the trailing / are trimmed
    QCOMPARE(m_acc->apiBase(), m_base + "/v1");
    m_acc->setLanguage("hu");
}

void TestCloudGateway::cleanupTestCase()
{
    if (m_mock.state() != QProcess::NotRunning) {
        m_mock.kill();
        m_mock.waitForFinished(3000);
    }
}

void TestCloudGateway::mockConfig(const QJsonObject& cfg)
{
    QNetworkRequest r(QUrl(m_base + "/__mock/config"));
    r.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* rep = m_nam.post(r, QJsonDocument(cfg).toJson());
    QSignalSpy done(rep, &QNetworkReply::finished);
    QVERIFY(done.wait(5000));
    rep->deleteLater();
}

QJsonObject TestCloudGateway::mockState()
{
    QNetworkReply* rep = m_nam.get(QNetworkRequest(QUrl(m_base + "/__mock/state")));
    QSignalSpy done(rep, &QNetworkReply::finished);
    done.wait(5000);
    rep->deleteLater();
    return QJsonDocument::fromJson(rep->readAll()).object();
}

// The same gateway config that AppController builds for the "tanara-cloud" provider.
ProviderConfig TestCloudGateway::gatewayConfig(const QString& model, QVector<cloud::HttpExchange>* log)
{
    ProviderConfig cfg;
    cfg.type = cloud::ProviderId;
    cfg.baseUrl = m_acc->apiBase();
    cfg.apiKey = m_acc->apiKey();
    cfg.model = model;
    cfg.maxTokens = 4000;
    cfg.extraHeaders = m_acc->requestHeaders(QStringLiteral("job-test-1"), QStringLiteral("quick"));
    cloud::CloudAccount* acc = m_acc.get();
    cfg.onExchange = [acc, log](const cloud::HttpExchange& ex) { acc->observeExchange(ex); log->append(ex); };
    return cfg;
}

void TestCloudGateway::waitlist_acceptedAndErrors()
{
    QSignalSpy ok(m_acc.get(), &cloud::CloudAccount::waitlistJoined);
    QSignalSpy bad(m_acc.get(), &cloud::CloudAccount::waitlistFailed);
    cloud::WaitlistSignup s;
    s.email = "anna@example.com"; s.consent = true; s.uiLanguage = "hu"; s.useCase = "meetings"; s.meetingLanguages = { "hu" };
    m_acc->joinWaitlist(s);
    QVERIFY(ok.wait(5000));
    QCOMPARE(ok.first().first().toString(), QStringLiteral("anna@example.com"));
    const QJsonArray wl = mockState().value("waitlist").toArray();
    QCOMPARE(wl.size(), 1);
    QCOMPARE(wl[0].toObject().value("source").toString(), QStringLiteral("client"));
    QCOMPARE(wl[0].toObject().value("platform").toString(), QStringLiteral("linux"));
    QCOMPARE(wl[0].toObject().value("client_version").toString(), libraryVersion());

    s.email = "spam@mailinator.com";
    m_acc->joinWaitlist(s);
    QVERIFY(bad.wait(5000));
    cloud::CloudError e = bad.takeFirst().first().value<cloud::CloudError>();
    QCOMPARE(e.kind, cloud::CloudErrorKind::DisposableEmail);
    QVERIFY(!e.requestId.isEmpty());

    s.email = "anna@example.com"; s.consent = false;   // the server rejects it too (the UI never sends it anyway)
    m_acc->joinWaitlist(s);
    QVERIFY(bad.wait(5000));
    e = bad.takeFirst().first().value<cloud::CloudError>();
    QCOMPARE(e.kind, cloud::CloudErrorKind::Validation);
    QVERIFY(e.fieldErrors.contains("consent"));

    mockConfig({ { "rate_limited", true } });
    s.consent = true;
    m_acc->joinWaitlist(s);
    QVERIFY(bad.wait(5000));
    e = bad.takeFirst().first().value<cloud::CloudError>();
    QCOMPARE(e.kind, cloud::CloudErrorKind::RateLimited);
    QCOMPARE(e.retryAfterSec, 30);
    mockConfig({ { "rate_limited", false } });
}

void TestCloudGateway::deviceFlow_login_account_models()
{
    QVERIFY(!m_acc->isLoggedIn());
    mockConfig({ { "auto_approve", true } });
    QSignalSpy code(m_acc.get(), &cloud::CloudAccount::deviceCodeReady);
    QSignalSpy success(m_acc.get(), &cloud::CloudAccount::deviceFlowSucceeded);
    QSignalSpy account(m_acc.get(), &cloud::CloudAccount::accountUpdated);
    QSignalSpy models(m_acc.get(), &cloud::CloudAccount::modelsUpdated);
    m_acc->startDeviceFlow();
    QVERIFY(code.wait(5000));
    const cloud::DeviceCode dc = code.first().first().value<cloud::DeviceCode>();
    QVERIFY(dc.userCode.contains('-'));
    QVERIFY(dc.verificationUriComplete.contains(dc.userCode));
    QVERIFY(success.wait(8000));
    QCOMPARE(success.first().first().toString(), QStringLiteral("anna@example.com"));
    QVERIFY(m_acc->isLoggedIn());
    QVERIFY(m_keys->get(cloud::ApiKeySecret).startsWith("tk_"));
    // The mock received the device's data (the web approver shows it).
    const QJsonObject info = mockState().value("devices").toArray().last().toObject().value("info").toObject();
    QCOMPARE(info.value("platform").toString(), QStringLiteral("linux"));
    QCOMPARE(info.value("client_version").toString(), libraryVersion());

    QVERIFY(account.count() > 0 || account.wait(5000));
    const cloud::AccountInfo a = m_acc->account();
    QCOMPARE(a.email, QStringLiteral("anna@example.com"));
    QCOMPARE(a.balance.micros, 12700000);          // $10 net becomes $12.70 gross
    QCOMPARE(a.vatMode, QStringLiteral("gross"));
    QVERIFY(a.hoursAccurate > 31 && a.hoursAccurate < 32);
    QVERIFY(!a.topupAvailable);
    QCOMPARE(a.trial, QStringLiteral("granted"));
    QVERIFY(models.count() > 0 || models.wait(5000));
    const auto fast = findTierModel(m_acc->models(), "stt", "fast");
    QVERIFY(fast.has_value());
    QVERIFY(!fast->diarization);
    QVERIFY(fast->notRecommendedFor("hu"));
    QVERIFY(findTierModel(m_acc->models(), "llm", "accurate").has_value());

    // The account and catalog cache in cloud-state.json survives a restart (offline display).
    cloud::CloudAccount again(m_keys.get(), m_dir.path());
    QCOMPARE(again.account().balance.micros, 12700000);
    QCOMPARE(again.email(), QStringLiteral("anna@example.com"));
    QVERIFY(!again.models().isEmpty());
    mockConfig({ { "auto_approve", false } });
}

void TestCloudGateway::deviceFlow_denied_and_cancel()
{
    KeyStore keys(m_dir.filePath("secrets-2.json"));
    QTemporaryDir d2;
    cloud::CloudAccount acc(&keys, d2.path());
    acc.setBaseUrl(m_base);
    QSignalSpy code(&acc, &cloud::CloudAccount::deviceCodeReady);
    QSignalSpy failed(&acc, &cloud::CloudAccount::deviceFlowFailed);

    mockConfig({ { "deny", true } });
    acc.startDeviceFlow();
    QVERIFY(failed.wait(8000));
    QCOMPARE(failed.takeFirst().first().value<cloud::CloudError>().code, QStringLiteral("access_denied"));
    mockConfig({ { "deny", false } });

    // Cancel: the code cannot be approved, the mock sees it as "cancelled"; no orphan key.
    acc.startDeviceFlow();
    QVERIFY(code.wait(5000));
    acc.cancelDeviceFlow();
    QTest::qWait(1500);
    QVERIFY(failed.isEmpty());
    QVERIFY(!acc.isLoggedIn());
    QVERIFY(mockState().value("devices").toArray().last().toObject().value("cancelled").toBool());
}

void TestCloudGateway::estimate_levels()
{
    cloud::EstimateRequest r;
    r.task = "both"; r.durationMs = 3735000; r.sttModel = "tanara/stt-accurate"; r.llmModel = "tanara/summary-accurate";
    r.summaryMode = "complex"; r.language = "hu";
    r.llmCalls = { { 1, 48000, 4000 }, { -1, 48000, 4000 }, { 1, 6000, 4000 } };

    cloud::EstimateResult got; cloud::CloudError err; bool done = false;
    auto run = [&]() {
        done = false;
        m_acc->estimate(r, [&](const cloud::EstimateResult& e, const cloud::CloudError& ce) { got = e; err = ce; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    };
    run();
    QVERIFY(!err.isError());
    QVERIFY(got.valid);
    QCOMPARE(estimateLevel(got), cloud::EstimateLevel::Enough);
    QCOMPARE(got.breakdown.size(), 2);
    QVERIFY(got.breakdown[0].exact && !got.breakdown[1].exact);
    QCOMPARE(got.breakdown[0].model, QStringLiteral("tanara/stt-accurate"));   // the requested (virtual) name stays
    QVERIFY(got.required.micros >= got.estimate.micros);
    QCOMPARE(got.balanceAfter.micros, got.balance.micros - got.estimate.micros);

    // estimate <= balance < required: "Probably enough, but little reserve remains."
    const double between = (got.estimate.micros + got.required.micros) / 2.0 / 1.27 / 1e6;
    mockConfig({ { "balance", between } });
    run();
    QCOMPARE(estimateLevel(got), cloud::EstimateLevel::LittleReserve);
    mockConfig({ { "balance", 0.05 } });
    run();
    QCOMPARE(estimateLevel(got), cloud::EstimateLevel::NotEnough);
    mockConfig({ { "balance", 10 } });
}

void TestCloudGateway::terms_earlyAcceptance()
{
    mockConfig({ { "terms_pending", true } });
    QSignalSpy account(m_acc.get(), &cloud::CloudAccount::accountUpdated);
    m_acc->refreshAccount();
    QVERIFY(account.wait(5000));
    cloud::TermsStatus t = m_acc->account().terms;
    QVERIFY(t.needsEarlyAcceptance());
    QVERIFY(!t.needsAcceptance());
    QVERIFY(t.upcomingEffectiveFrom > QDateTime::currentDateTimeUtc());

    QSignalSpy accepted(m_acc.get(), &cloud::CloudAccount::termsAccepted);
    m_acc->acceptTerms(t.upcomingVersion);
    QVERIFY(accepted.wait(5000));
    t = accepted.first().first().value<cloud::TermsStatus>();
    QCOMPARE(t.acceptedVersion, QStringLiteral("2026-11-01"));
    QVERIFY(!t.needsEarlyAcceptance());

    // Unknown version: 400 validation_error (fields.version).
    QSignalSpy failed(m_acc.get(), &cloud::CloudAccount::termsFailed);
    m_acc->acceptTerms("1999-01-01");
    QVERIFY(failed.wait(5000));
    QVERIFY(failed.first().first().value<cloud::CloudError>().fieldErrors.contains("version"));
    mockConfig({ { "terms_pending", false }, { "terms_accepted", "2026-06-01" } });
}

void TestCloudGateway::topup_unavailable()
{
    QSignalSpy failed(m_acc.get(), &cloud::CloudAccount::topupFailed);
    m_acc->createTopupLink();
    QVERIFY(failed.wait(5000));
    const cloud::CloudError e = failed.first().first().value<cloud::CloudError>();
    QCOMPARE(e.kind, cloud::CloudErrorKind::TopupUnavailable);
    QVERIFY(e.contactUrl.endsWith("/contact"));

    mockConfig({ { "topup_available", true } });
    QSignalSpy ok(m_acc.get(), &cloud::CloudAccount::topupLinkReady);
    m_acc->createTopupLink();
    QVERIFY(ok.wait(5000));
    QVERIFY(ok.first().first().toString().contains("handoff="));
    mockConfig({ { "topup_available", false } });
}

void TestCloudGateway::errors_blockingStates()
{
    cloud::EstimateRequest r;
    r.task = "transcribe"; r.durationMs = 60000; r.sttModel = "tanara/stt-accurate";
    auto estimateError = [&]() {
        cloud::CloudError err; bool done = false;
        m_acc->estimate(r, [&](const cloud::EstimateResult&, const cloud::CloudError& ce) { err = ce; done = true; });
        QDeadlineTimer dl(5000);
        while (!done && !dl.hasExpired()) QTest::qWait(20);
        return err;
    };
    struct Case { QJsonObject cfg; cloud::CloudErrorKind kind; int status; };
    const QVector<Case> cases = {
        { { { "maintenance", true } }, cloud::CloudErrorKind::Maintenance, 503 },
        { { { "upstream_down", true } }, cloud::CloudErrorKind::Upstream, 503 },
        { { { "rate_limited", true } }, cloud::CloudErrorKind::RateLimited, 429 },
        { { { "spend_limit", true } }, cloud::CloudErrorKind::SpendLimit, 429 },
        { { { "internal_error", true } }, cloud::CloudErrorKind::Internal, 500 },
        { { { "terms_required", true } }, cloud::CloudErrorKind::TermsRequired, 403 },
        { { { "suspended", "payment_dispute" } }, cloud::CloudErrorKind::Suspended, 403 },
    };
    for (const Case& c : cases) {
        mockConfig(c.cfg);
        const cloud::CloudError e = estimateError();
        QCOMPARE(e.kind, c.kind);
        QCOMPARE(e.httpStatus, c.status);
        QVERIFY2(e.requestId.startsWith("req_"), qPrintable(e.code));   // the error id is always there
        QVERIFY(!e.message.isEmpty());
        if (c.kind == cloud::CloudErrorKind::Maintenance) QVERIFY(e.windowStart.isValid() && e.windowEnd.isValid());
        if (c.kind == cloud::CloudErrorKind::SpendLimit) { QCOMPARE(e.limitPeriod, QStringLiteral("daily")); QVERIFY(!e.settingsUrl.isEmpty()); }
        if (c.kind == cloud::CloudErrorKind::TermsRequired) QCOMPARE(e.termsVersion, QStringLiteral("2026-11-01"));
        if (c.kind == cloud::CloudErrorKind::Suspended) QCOMPARE(e.suspensionReason, QStringLiteral("payment_dispute"));
        QJsonObject off = c.cfg;
        for (auto it = off.begin(); it != off.end(); ++it) it.value() = it.value().isString() ? QJsonValue() : QJsonValue(false);
        mockConfig(off);
    }
    // A suspended account is 403 too, but for payment_dispute the top-up link is allowed (it reaches 409).
    mockConfig({ { "suspended", "payment_dispute" } });
    QSignalSpy topup(m_acc.get(), &cloud::CloudAccount::topupFailed);
    m_acc->createTopupLink();
    QVERIFY(topup.wait(5000));
    QCOMPARE(topup.first().first().value<cloud::CloudError>().kind, cloud::CloudErrorKind::TopupUnavailable);
    mockConfig({ { "suspended", QJsonValue() } });
}

void TestCloudGateway::minClient_detected()
{
    QSignalSpy old(m_acc.get(), &cloud::CloudAccount::clientTooOldDetected);
    mockConfig({ { "min_client", "9.9.9" } });
    QSignalSpy failed(m_acc.get(), &cloud::CloudAccount::accountFailed);
    m_acc->refreshAccount();
    QVERIFY(failed.wait(5000));
    QCOMPARE(failed.first().first().value<cloud::CloudError>().kind, cloud::CloudErrorKind::ClientTooOld);
    QVERIFY(old.count() == 1);
    QVERIFY(m_acc->clientTooOld());
    QCOMPARE(m_acc->minClient(), QStringLiteral("9.9.9"));
    QVERIFY(m_acc->downloadUrl().endsWith("/download"));
    mockConfig({ { "min_client", "0.1.0" } });
    QSignalSpy ok(m_acc.get(), &cloud::CloudAccount::accountUpdated);
    m_acc->refreshAccount();
    QVERIFY(ok.wait(5000));
    QVERIFY(!m_acc->clientTooOld());   // the gateway lowered the minimum
}

void TestCloudGateway::stt_throughGateway_chargeAndRefund()
{
    // A small "audio file" generated at run time (no binary fixture in the repo). Without ffprobe
    // or on non-audio bytes the mock estimates the length from the size.
    const QString audio = m_dir.filePath("mixdown.mp3");
    {
        QFile f(audio);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(160000, '\x55'));   // about 10 s by the size-based estimate
    }
    for (const bool refund : { false, true }) {
        mockConfig({ { "refund", refund } });
        QVector<cloud::HttpExchange> log;
        providers::SonioxProvider p(gatewayConfig(QStringLiteral("tanara/stt-accurate"), &log));
        providers::SttRequest req;
        req.audioFilePath = audio;
        req.trackId = "mixdown";
        req.diarization = true;
        providers::SttJob* job = p.transcribe(req);
        QSignalSpy fin(job, &providers::SttJob::finished);
        QSignalSpy fail(job, &providers::SttJob::failed);
        QTRY_VERIFY_WITH_TIMEOUT(fin.count() + fail.count() > 0, 15000);
        QTest::qWait(300);   // feedback of the cleanup DELETEs

        // Every request went with the shared headers, and every response has a request id.
        QVERIFY(!log.isEmpty());
        for (const cloud::HttpExchange& ex : log)
            QVERIFY2(ex.headers.contains("x-tanara-request-id"), qPrintable(ex.path));
        const auto create = std::find_if(log.cbegin(), log.cend(), [](const cloud::HttpExchange& ex) {
            return ex.method == "POST" && ex.path.endsWith("/transcriptions"); });
        QVERIFY(create != log.cend());
        const cloud::ChargeInfo ch = cloud::chargeInfoFromJson(QJsonDocument::fromJson(create->body).object().value("tanara").toObject());
        QVERIFY(ch.valid);
        QVERIFY(ch.charge.micros > 0);
        QCOMPARE(cloud::chargeInfoFromHeaders(create->headers).charge.micros, ch.charge.micros);
        QCOMPARE(ch.jobId, QStringLiteral("job-test-1"));
        QVERIFY(std::any_of(log.cbegin(), log.cend(), [](const cloud::HttpExchange& ex) {
            return ex.method == "DELETE" && ex.path.contains("/transcriptions/"); }));

        if (!refund) {
            QCOMPARE(fin.count(), 1);
            const TrackTranscript tt = fin.first().first().value<TrackTranscript>();
            QVERIFY(tt.tokens.size() > 5);
            QVERIFY(!tt.tokens.first().speaker.isEmpty());   // diarized
        } else {
            QCOMPARE(fail.count(), 1);
            // The write-back shows in the status reply that first gives an error, before the DELETE (the
            // client reads the tanara field first).
            int statusIdx = -1, deleteIdx = -1;
            for (int i = 0; i < log.size(); ++i) {
                const cloud::TranscriptionJobInfo j = cloud::transcriptionJobInfoFromJson(QJsonDocument::fromJson(log[i].body).object());
                if (log[i].method == "GET" && j.status == "error" && statusIdx < 0) {
                    QVERIFY(j.refunded);
                    QCOMPARE(j.refund.micros, ch.charge.micros);
                    statusIdx = i;
                }
                if (log[i].method == "DELETE" && log[i].path.contains("/transcriptions/") && deleteIdx < 0) deleteIdx = i;
            }
            QVERIFY(statusIdx >= 0 && deleteIdx > statusIdx);
        }
        delete job;
    }
    mockConfig({ { "refund", false } });
}

void TestCloudGateway::llm_throughGateway_charge_and402()
{
    QVector<cloud::HttpExchange> log;
    providers::OpenAiCompatibleProvider p(gatewayConfig(QStringLiteral("tanara/summary-fast"), &log));
    providers::LlmRequest req;
    req.messages = { { "system", "Összefoglaló." }, { "user", "Rövid átirat." } };
    req.maxTokens = 4000;
    providers::LlmJob* job = p.chat(req);
    QSignalSpy fin(job, &providers::LlmJob::finished);
    QVERIFY(fin.wait(5000));
    QCOMPARE(log.size(), 1);
    const QJsonObject usage = QJsonDocument::fromJson(log[0].body).object().value("usage").toObject();
    const cloud::ChargeInfo c = cloud::chargeInfoFromJson(usage.value("tanara_charge").toObject());
    QVERIFY(c.valid && c.charge.micros > 0);
    QCOMPARE(c.jobId, QStringLiteral("job-test-1"));
    // The mock's log: the summary mode and the job id in the usage record (grouping).
    const QJsonObject last = mockState().value("usage").toArray().last().toObject();
    QCOMPARE(last.value("summary_mode").toString(), QStringLiteral("quick"));
    QCOMPARE(last.value("job_id").toString(), QStringLiteral("job-test-1"));

    // Low balance: 402 hold (prompt + max_tokens cover), nothing is charged.
    mockConfig({ { "balance", 0.00001 } });
    log.clear();
    providers::LlmJob* job2 = p.chat(req);
    QSignalSpy failed(job2, &providers::LlmJob::failed);
    QVERIFY(failed.wait(5000));
    const cloud::CloudError e = cloud::parseCloudError(log[0].status, log[0].headers, log[0].body);
    QCOMPARE(e.kind, cloud::CloudErrorKind::InsufficientBalance);
    QCOMPARE(e.neededBasis, QStringLiteral("hold"));
    QVERIFY(e.needed.micros > e.balance.micros);
    QVERIFY(e.topupUrl.isEmpty());   // P0: no online top-up, so "Write to us"
    mockConfig({ { "balance", 10 } });
}

void TestCloudGateway::pending_refundAtStartup()
{
    // A "half-finished" transcription: the client saved the id, then quit.
    mockConfig({ { "refund", true } });
    QVector<cloud::HttpExchange> log;
    const QString audio = m_dir.filePath("mixdown.mp3");
    providers::SonioxProvider p(gatewayConfig(QStringLiteral("tanara/stt-accurate"), &log));
    providers::SttRequest req; req.audioFilePath = audio; req.trackId = "mixdown";
    providers::SttJob* job = p.transcribe(req);
    QTRY_VERIFY_WITH_TIMEOUT(std::any_of(log.cbegin(), log.cend(), [](const cloud::HttpExchange& ex) {
        return ex.method == "POST" && ex.path.endsWith("/transcriptions"); }), 8000);
    const auto create = std::find_if(log.cbegin(), log.cend(), [](const cloud::HttpExchange& ex) {
        return ex.method == "POST" && ex.path.endsWith("/transcriptions"); });
    const QString trId = QJsonDocument::fromJson(create->body).object().value("id").toString();
    delete job;   // "quit": it no longer polled the status

    m_acc->addPendingTranscription(trId, QString());
    QTest::qWait(3000);   // the mock job ends with an error meanwhile
    QSignalSpy refunded(m_acc.get(), &cloud::CloudAccount::previousTranscriptionRefunded);
    cloud::CloudAccount restarted(m_keys.get(), m_dir.path());   // new start: the pending id from cloud-state.json
    restarted.setBaseUrl(m_base);
    QCOMPARE(restarted.pendingTranscriptions(), QStringList{ trId });
    QSignalSpy refunded2(&restarted, &cloud::CloudAccount::previousTranscriptionRefunded);
    restarted.checkPendingTranscriptions();
    QVERIFY(refunded2.wait(5000));
    QVERIFY(refunded2.first().first().value<cloud::Money>().micros > 0);
    QTRY_VERIFY_WITH_TIMEOUT(restarted.pendingTranscriptions().isEmpty(), 3000);
    mockConfig({ { "refund", false } });
}

void TestCloudGateway::logout_removesKey()
{
    QVERIFY(m_acc->isLoggedIn());
    const QString key = m_acc->apiKey();
    QSignalSpy out(m_acc.get(), &cloud::CloudAccount::loggedOut);
    m_acc->logout();
    QVERIFY(out.wait(5000));
    QVERIFY(!m_acc->isLoggedIn());
    QVERIFY(m_keys->get(cloud::ApiKeySecret).isEmpty());
    QVERIFY(m_acc->email().isEmpty());
    // With the revoked key the gateway gives 401.
    m_keys->set(cloud::ApiKeySecret, key);
    QSignalSpy failed(m_acc.get(), &cloud::CloudAccount::accountFailed);
    m_acc->refreshAccount();
    QVERIFY(failed.wait(5000));
    QCOMPARE(failed.first().first().value<cloud::CloudError>().kind, cloud::CloudErrorKind::Unauthorized);
    m_keys->remove(cloud::ApiKeySecret);
}

#endif // Q_MOC_RUN

QTEST_GUILESS_MAIN(TestCloudGateway)
#include "test_cloud_gateway.moc"
