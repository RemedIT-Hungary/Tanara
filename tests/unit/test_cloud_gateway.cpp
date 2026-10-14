// Tanara Cloud: integration test against mock-gateway 1.2.0 (tests/mock-gateway/mock-gateway.mjs).
//
// The test starts the mock (node, no dependencies) on a free port, with canned STT/LLM, and
// walks through the client's gateway paths: waitlist, device flow, account, catalog, estimate,
// terms acceptance, top-up link, error states (402/403/426/429/503), and the existing
// SonioxProvider / OpenAiCompatibleProvider with the gateway config (headers, charge, write-back),
// and the P10 capability adapters on the draft endpoints (embeddings, voice, identify, summaries,
// tag suggestions, the filtered catalog): success, 402, 401, 422.
// Without node the test is skipped (QSKIP).
#include <QtTest>
#include "tanara/providers/core/ProviderConfig.h"
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

#include "tanara/domain/Tag.h"
#include "tanara/domain/Transcript.h"
#include "tanara/foundation/Version.h"
#include "tanara/domain/Voice.h"
#include "tanara/cloud/CloudSession.h"
#include "tanara/cloud/CloudRunLedger.h"
#include "tanara/cloud/CloudCapabilities.h"
#include "tanara/cloud/CloudEstimator.h"
#include "fake_media_tool.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/providers/chat/OpenAiCompatibleProvider.h"
#include "tanara/foundation/secrets/SecretStore.h"
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
    void capabilities_registered();
    void capabilities_textEmbedding();
    void capabilities_voiceEmbedding_and_identify();
    void capabilities_summaries();
    void capabilities_tags();
    void capabilities_modelCatalog();
    void capabilities_estimator();
    void capabilities_errors_402_401_422();
    void logout_removesKey();

private:
    void mockConfig(const QJsonObject& cfg);
    QJsonObject mockState();
    providers::ProviderConfig gatewayConfig(const QString& model) const;
    providers::ProviderContext gatewayContext(QVector<net::HttpExchange>* log);

    QProcess m_mock;
    int m_port = 0;
    QString m_base;
    QTemporaryDir m_dir;
    std::unique_ptr<FileSecretStore> m_keys;
    std::unique_ptr<cloud::CloudSession> m_acc;
    std::unique_ptr<cloud::CloudRunLedger> m_ledger;
    cloud::CloudRunPtr m_run;   // the run of the last gatewayContext()
    providers::ProviderRegistry m_reg;   // the cloud adapters (cloud::registerInto)
    std::shared_ptr<testing::FakeMediaTool> m_media = std::make_shared<testing::FakeMediaTool>();
    template <class Port> std::unique_ptr<Port> make(const QString& id, const providers::ProviderContext& ctx,
                                                     providers::ProviderConfig cfg = {});
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

    m_keys = std::make_unique<FileSecretStore>(m_dir.filePath("secrets.json"));
    m_acc = std::make_unique<cloud::CloudSession>(m_keys.get(), m_dir.path());
    m_ledger = std::make_unique<cloud::CloudRunLedger>(m_acc.get());
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

// The same gateway config + context that AppController gets from the CloudRunLedger for the
// "tanara-cloud" provider (one run, job id "job-test-1", summary mode "quick").
providers::ProviderConfig TestCloudGateway::gatewayConfig(const QString& model) const
{
    return m_ledger->configFor(model, 4000);
}

providers::ProviderContext TestCloudGateway::gatewayContext(QVector<net::HttpExchange>* log)
{
    m_run = m_ledger->newRun(QStringLiteral("m1"), QStringLiteral("test"));
    m_run->jobId = QStringLiteral("job-test-1");
    providers::ProviderContext ctx = m_ledger->contextFor(m_run, QStringLiteral("quick"), nullptr);
    const net::ExchangeObserver ledgerObserver = ctx.observe;
    ctx.observe = [ledgerObserver, log](const net::HttpExchange& ex) { ledgerObserver(ex); log->append(ex); };
    return ctx;
}

void TestCloudGateway::waitlist_acceptedAndErrors()
{
    QSignalSpy ok(m_acc.get(), &cloud::CloudSession::waitlistJoined);
    QSignalSpy bad(m_acc.get(), &cloud::CloudSession::waitlistFailed);
    cloud::WaitlistSignup s;
    s.email = "anna@example.com"; s.consent = true; s.uiLanguage = "hu"; s.useCase = "meetings"; s.meetingLanguages = { "hu" };
    m_acc->joinWaitlist(s);
    QVERIFY(ok.wait(5000));
    QCOMPARE(ok.first().first().toString(), QStringLiteral("anna@example.com"));
    const QJsonArray wl = mockState().value("waitlist").toArray();
    QCOMPARE(wl.size(), 1);
    QCOMPARE(wl[0].toObject().value("source").toString(), QStringLiteral("client"));
    QCOMPARE(wl[0].toObject().value("platform").toString(), cloud::clientPlatform());
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
    QSignalSpy code(m_acc.get(), &cloud::CloudSession::deviceCodeReady);
    QSignalSpy success(m_acc.get(), &cloud::CloudSession::deviceFlowSucceeded);
    QSignalSpy account(m_acc.get(), &cloud::CloudSession::accountUpdated);
    QSignalSpy models(m_acc.get(), &cloud::CloudSession::modelsUpdated);
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
    QCOMPARE(info.value("platform").toString(), cloud::clientPlatform());
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
    cloud::CloudSession again(m_keys.get(), m_dir.path());
    QCOMPARE(again.account().balance.micros, 12700000);
    QCOMPARE(again.email(), QStringLiteral("anna@example.com"));
    QVERIFY(!again.models().isEmpty());
    mockConfig({ { "auto_approve", false } });
}

void TestCloudGateway::deviceFlow_denied_and_cancel()
{
    FileSecretStore keys(m_dir.filePath("secrets-2.json"));
    QTemporaryDir d2;
    cloud::CloudSession acc(&keys, d2.path());
    acc.setBaseUrl(m_base);
    QSignalSpy code(&acc, &cloud::CloudSession::deviceCodeReady);
    QSignalSpy failed(&acc, &cloud::CloudSession::deviceFlowFailed);

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
    QSignalSpy account(m_acc.get(), &cloud::CloudSession::accountUpdated);
    m_acc->refreshAccount();
    QVERIFY(account.wait(5000));
    cloud::TermsStatus t = m_acc->account().terms;
    QVERIFY(t.needsEarlyAcceptance());
    QVERIFY(!t.needsAcceptance());
    QVERIFY(t.upcomingEffectiveFrom > QDateTime::currentDateTimeUtc());

    QSignalSpy accepted(m_acc.get(), &cloud::CloudSession::termsAccepted);
    m_acc->acceptTerms(t.upcomingVersion);
    QVERIFY(accepted.wait(5000));
    t = accepted.first().first().value<cloud::TermsStatus>();
    QCOMPARE(t.acceptedVersion, QStringLiteral("2026-11-01"));
    QVERIFY(!t.needsEarlyAcceptance());

    // Unknown version: 400 validation_error (fields.version).
    QSignalSpy failed(m_acc.get(), &cloud::CloudSession::termsFailed);
    m_acc->acceptTerms("1999-01-01");
    QVERIFY(failed.wait(5000));
    QVERIFY(failed.first().first().value<cloud::CloudError>().fieldErrors.contains("version"));
    mockConfig({ { "terms_pending", false }, { "terms_accepted", "2026-06-01" } });
}

void TestCloudGateway::topup_unavailable()
{
    QSignalSpy failed(m_acc.get(), &cloud::CloudSession::topupFailed);
    m_acc->createTopupLink();
    QVERIFY(failed.wait(5000));
    const cloud::CloudError e = failed.first().first().value<cloud::CloudError>();
    QCOMPARE(e.kind, cloud::CloudErrorKind::TopupUnavailable);
    QVERIFY(e.contactUrl.endsWith("/contact"));

    mockConfig({ { "topup_available", true } });
    QSignalSpy ok(m_acc.get(), &cloud::CloudSession::topupLinkReady);
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
    QSignalSpy topup(m_acc.get(), &cloud::CloudSession::topupFailed);
    m_acc->createTopupLink();
    QVERIFY(topup.wait(5000));
    QCOMPARE(topup.first().first().value<cloud::CloudError>().kind, cloud::CloudErrorKind::TopupUnavailable);
    mockConfig({ { "suspended", QJsonValue() } });
}

void TestCloudGateway::minClient_detected()
{
    QSignalSpy old(m_acc.get(), &cloud::CloudSession::clientTooOldDetected);
    mockConfig({ { "min_client", "9.9.9" } });
    QSignalSpy failed(m_acc.get(), &cloud::CloudSession::accountFailed);
    m_acc->refreshAccount();
    QVERIFY(failed.wait(5000));
    QCOMPARE(failed.first().first().value<cloud::CloudError>().kind, cloud::CloudErrorKind::ClientTooOld);
    QVERIFY(old.count() == 1);
    QVERIFY(m_acc->clientTooOld());
    QCOMPARE(m_acc->minClient(), QStringLiteral("9.9.9"));
    QVERIFY(m_acc->downloadUrl().endsWith("/download"));
    mockConfig({ { "min_client", "0.1.0" } });
    QSignalSpy ok(m_acc.get(), &cloud::CloudSession::accountUpdated);
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
        QVector<net::HttpExchange> log;
        providers::SonioxProvider p(gatewayContext(&log), gatewayConfig(QStringLiteral("tanara/stt-accurate")));
        providers::SttRequest req;
        req.audioFilePath = audio;
        req.trackId = "mixdown";
        req.diarization = true;
        providers::SttJob* job = p.transcribe(req);
        QSignalSpy fin(job, &providers::SttJob::finished);
        QSignalSpy fail(job, &providers::SttJob::failed);
        QTRY_VERIFY_WITH_TIMEOUT(fin.count() + fail.count() > 0, 15000);
        QTest::qWait(300);   // feedback of the cleanup DELETEs
        // The app closes the run through the ledger (AppController::finishCloudRun / failCloudRun):
        // charged on success, refunded on the written-back failure; the pending id goes away.
        QVERIFY(!m_run->transcriptionId.isEmpty());
        if (fin.count() > 0) {
            QSignalSpy charged(m_ledger.get(), &cloud::CloudRunLedger::charged);
            m_ledger->finish(m_run);
            QCOMPARE(charged.count(), 1);
            QVERIFY(m_run->chargedMicros > 0);
        } else {
            QSignalSpy refunded(m_ledger.get(), &cloud::CloudRunLedger::refunded);
            QVERIFY(m_ledger->fail(m_run));
            QCOMPARE(refunded.count(), 1);
            QVERIFY(m_run->refunded);
        }
        QVERIFY(!m_acc->pendingTranscriptions().contains(m_run->transcriptionId));

        // Every request went with the shared headers, and every response has a request id.
        QVERIFY(!log.isEmpty());
        for (const net::HttpExchange& ex : log)
            QVERIFY2(ex.headers.contains("x-tanara-request-id"), qPrintable(ex.path));
        const auto create = std::find_if(log.cbegin(), log.cend(), [](const net::HttpExchange& ex) {
            return ex.method == "POST" && ex.path.endsWith("/transcriptions"); });
        QVERIFY(create != log.cend());
        const cloud::ChargeInfo ch = cloud::chargeInfoFromJson(QJsonDocument::fromJson(create->body).object().value("tanara").toObject());
        QVERIFY(ch.valid);
        QVERIFY(ch.charge.micros > 0);
        QCOMPARE(cloud::chargeInfoFromHeaders(create->headers).charge.micros, ch.charge.micros);
        QCOMPARE(ch.jobId, QStringLiteral("job-test-1"));
        QVERIFY(std::any_of(log.cbegin(), log.cend(), [](const net::HttpExchange& ex) {
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
    QVector<net::HttpExchange> log;
    providers::OpenAiCompatibleProvider p(gatewayContext(&log), gatewayConfig(QStringLiteral("tanara/summary-fast")));
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
    // The ledger booked the charge into the run (through the context's cost sink).
    QCOMPARE(m_run->calls, 1);
    QCOMPARE(m_run->chargedMicros, c.charge.micros);
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
    QVector<net::HttpExchange> log;
    const QString audio = m_dir.filePath("mixdown.mp3");
    providers::SonioxProvider p(gatewayContext(&log), gatewayConfig(QStringLiteral("tanara/stt-accurate")));
    providers::SttRequest req; req.audioFilePath = audio; req.trackId = "mixdown";
    providers::SttJob* job = p.transcribe(req);
    QTRY_VERIFY_WITH_TIMEOUT(std::any_of(log.cbegin(), log.cend(), [](const net::HttpExchange& ex) {
        return ex.method == "POST" && ex.path.endsWith("/transcriptions"); }), 8000);
    const auto create = std::find_if(log.cbegin(), log.cend(), [](const net::HttpExchange& ex) {
        return ex.method == "POST" && ex.path.endsWith("/transcriptions"); });
    const QString trId = QJsonDocument::fromJson(create->body).object().value("id").toString();
    delete job;   // "quit": it no longer polled the status

    m_acc->addPendingTranscription(trId, QString());
    QTest::qWait(3000);   // the mock job ends with an error meanwhile
    QSignalSpy refunded(m_acc.get(), &cloud::CloudSession::previousTranscriptionRefunded);
    cloud::CloudSession restarted(m_keys.get(), m_dir.path());   // new start: the pending id from cloud-state.json
    restarted.setBaseUrl(m_base);
    QCOMPARE(restarted.pendingTranscriptions(), QStringList{ trId });
    QSignalSpy refunded2(&restarted, &cloud::CloudSession::previousTranscriptionRefunded);
    restarted.checkPendingTranscriptions();
    QVERIFY(refunded2.wait(5000));
    QVERIFY(refunded2.first().first().value<cloud::Money>().micros > 0);
    QTRY_VERIFY_WITH_TIMEOUT(restarted.pendingTranscriptions().isEmpty(), 3000);
    mockConfig({ { "refund", false } });
}

template <class Port>
std::unique_ptr<Port> TestCloudGateway::make(const QString& id, const providers::ProviderContext& ctx,
                                             providers::ProviderConfig cfg)
{
    if (m_reg.descriptors(providers::ProviderTraits<Port>::kind).isEmpty()) {
        providers::registerBuiltinProviders(m_reg);
        cloud::CloudCapabilityDeps deps;
        auto media = m_media;
        deps.media = [media]() { return media; };
        cloud::registerInto(m_reg, *m_acc, deps);
    }
    return std::unique_ptr<Port>(m_reg.create<Port>(id, ctx, cfg, nullptr));
}

namespace {
template <class Job>
bool waitJob(Job* job, int ms = 8000)
{
    QSignalSpy ok(job, &providers::CapabilityJob::finished);
    QSignalSpy bad(job, &providers::CapabilityJob::failed);
    QDeadlineTimer dl(ms);
    while (!job->isDone() && ok.isEmpty() && bad.isEmpty() && !dl.hasExpired()) QTest::qWait(10);
    return job->isDone() || !ok.isEmpty() || !bad.isEmpty();
}
}

void TestCloudGateway::capabilities_registered()
{
    QVERIFY(m_acc->isLoggedIn());
    providers::ProviderRegistry reg;
    providers::registerBuiltinProviders(reg);
    cloud::registerInto(reg, *m_acc);
    using K = providers::CapabilityKind;
    const QString id = cloud::capabilityProviderId();
    for (K k : { K::Transcription, K::Chat, K::VoiceEmbedding, K::ParticipantIdentification, K::Summarization,
                 K::TagSuggestion, K::ModelCatalog }) {
        const providers::ProviderDescriptor d = reg.descriptor(k, id);
        QVERIFY2(d.id == id, qPrintable(providers::capabilityName(k)));
        QCOMPARE(d.origin, providers::Origin::Cloud);
        QCOMPARE(d.authMode, providers::AuthMode::Login);
    }
    QCOMPARE(reg.descriptor(K::TextEmbedding, providers::embeddingproviders::CloudId).origin, providers::Origin::Cloud);
    QVERIFY(reg.descriptor(K::VoiceEmbedding, id).traits.inputs.contains(providers::InputKind::AudioClip));
    QVERIFY(reg.descriptor(K::VoiceEmbedding, id).traits.inputs.contains(providers::InputKind::Pcm));
    QCOMPARE(reg.descriptor(K::Summarization, id).catalogRef, id);
}

void TestCloudGateway::capabilities_textEmbedding()
{
    QVector<net::HttpExchange> log;
    auto emb = make<providers::ITextEmbedder>(providers::embeddingproviders::CloudId, gatewayContext(&log));
    QVERIFY(emb);
    providers::EmbeddingJob* job = emb->embed({ { "Nordvik raktár", "MuseumPlus bevezetés" }, QString() });
    QSignalSpy fin(job, &providers::EmbeddingJob::finished);
    QVERIFY(fin.wait(5000));
    const auto vecs = fin.first().first().value<QVector<QVector<float>>>();
    QCOMPARE(vecs.size(), 2);
    QCOMPARE(vecs[0].size(), 64);
    QVERIFY(vecs[0] != vecs[1]);
    QCOMPARE(log.size(), 1);
    QVERIFY(log[0].path.endsWith("/v1/embeddings"));
    QCOMPARE(QJsonDocument::fromJson(log[0].body).object().value("model").toString(), QStringLiteral("tanara/text-embedding"));
    QCOMPARE(m_run->calls, 1);   // charged through the ledger
    QVERIFY(m_run->chargedMicros > 0);
}

void TestCloudGateway::capabilities_voiceEmbedding_and_identify()
{
    QVector<net::HttpExchange> log;
    m_media->pcm.insert("track_mic.ogg", QVector<qint16>(16000 * 3, 2000));
    auto emb = make<providers::IVoiceEmbedder>(cloud::capabilityProviderId(), gatewayContext(&log));
    QVERIFY(emb);
    QCOMPARE(emb->defaultModelIds(), QStringList{ "tanara/voice-embedding" });
    providers::VoiceEmbeddingRequest req;
    req.clips = { { m_dir.filePath("track_mic.ogg"), 0, 1500 }, { m_dir.filePath("track_mic.ogg"), 0, 1500 } };
    req.pcm.clear();
    providers::VoiceEmbeddingJob* job = emb->embed(req);
    QVERIFY(waitJob(job));
    QVERIFY2(!job->error().isError(), qPrintable(job->error().message));
    const QVector<providers::VoiceVector> v = job->result();
    QCOMPARE(v.size(), 2);
    QCOMPARE(v[0].dim, 192);
    QCOMPARE(v[0].vector.size(), 192);
    QCOMPARE(v[0].vector, v[1].vector);              // the same clip -> the same (deterministic) vector
    QCOMPARE(v[1].input, 1);
    QCOMPARE(m_media->encodes().size(), 2);          // every clip re-encoded to Opus through the media tool
    QCOMPARE(m_media->encodes()[0].input.sampleRate, 16000);
    QCOMPARE(log.size(), 2);
    QVERIFY(log[0].path.endsWith("/v1/voice/embeddings"));
    QCOMPARE(m_run->calls, 2);
    const QJsonObject last = mockState().value("usage").toArray().last().toObject();
    QCOMPARE(last.value("kind").toString(), QStringLiteral("voice_embedding"));
    QCOMPARE(last.value("job_id").toString(), QStringLiteral("job-test-1"));

    // Identification: the probe is the clip's vector; Anna's print is the same vector, Béla's another one.
    auto idf = make<providers::IParticipantIdentifier>(cloud::capabilityProviderId(), gatewayContext(&log));
    providers::IdentifyRequest ir;
    providers::IdentifyProbe probe;
    probe.vectors = { v[0] };
    probe.trackId = "mic";
    probe.windows = 4;
    ir.probes = { probe };
    Voiceprint anna; anna.model = v[0].modelId; anna.embedding = v[0].vector;
    QVector<float> other(192, 0.0f); other[0] = 1.0f;
    Voiceprint bela; bela.model = v[0].modelId; bela.embedding = other;
    const PersonId annaId = PersonId::fromString(QStringLiteral("p-anna-1"));
    ir.candidates = { { "Béla", { bela }, PersonId::fromString(QStringLiteral("p-bela-1")) },
                      { "Kovács Anna", { anna }, annaId } };
    const QByteArray body = cloud::CloudParticipantIdentifier::requestBody(ir, QString());
    QVERIFY(!body.contains("Anna") && !body.contains("B\xc3\xa9la"));   // names never leave the device
    QVERIFY(!body.contains("p-anna-1") && !body.contains("p-bela-1"));     // nor do the person ids
    log.clear();
    providers::IdentifyJob* ij = idf->identify(ir);
    QVERIFY(waitJob(ij));
    QVERIFY2(!ij->error().isError(), qPrintable(ij->error().message));
    QCOMPARE(ij->result().size(), 1);
    QCOMPARE(ij->result()[0].personId, annaId);
    QCOMPARE(ij->result()[0].displayName, QStringLiteral("Kovács Anna"));
    QVERIFY(ij->result()[0].score > 0.99);
    QCOMPARE(ij->result()[0].trackId, QStringLiteral("mic"));
    QCOMPARE(ij->result()[0].windows, 4);
    QVERIFY(log[0].path.endsWith("/v1/voice/identify"));
    // Below the threshold: no name.
    ir.candidates = { { "Béla", { bela }, PersonId::fromString(QStringLiteral("p-bela-1")) } };
    ij = idf->identify(ir);
    QVERIFY(waitJob(ij));
    QVERIFY(ij->result()[0].personId.isNull());
}

void TestCloudGateway::capabilities_summaries()
{
    QVector<net::HttpExchange> log;
    providers::ProviderConfig cfg = gatewayConfig(QStringLiteral("tanara/summary-fast"));
    cfg.extra.insert(QStringLiteral("pollIntervalMs"), 20);
    auto sum = make<providers::ISummarizer>(cloud::capabilityProviderId(), gatewayContext(&log), cfg);
    QVERIFY(sum);
    providers::SummarizationRequest req;
    req.mode = providers::SummarizationMode::Complex;
    req.language = QStringLiteral("magyar");
    for (int i = 0; i < 4; ++i) {
        TranscriptToken t;
        t.text = QStringLiteral(" A migráció állapota, %1.").arg(i);
        t.speaker = i % 2 ? QStringLiteral("Béla") : QStringLiteral("Ádám");
        t.startMs = i * 10000;
        t.endMs = t.startMs + 5000;
        req.transcript.tokens.append(t);
    }
    providers::SummarizationJob* job = sum->summarize(req);
    QVERIFY(waitJob(job));
    QVERIFY2(!job->error().isError(), qPrintable(job->error().message));
    const providers::SummarizationResult r = job->result();
    QCOMPARE(r.mode, providers::SummarizationMode::Complex);
    QVERIFY(r.summary.execSummary.contains(QStringLiteral("MuseumPlus")));
    QCOMPARE(r.topics.size(), 2);
    QCOMPARE(r.analyses.size(), 2);
    QCOMPARE(r.summary.actionItems.value(0).owner, QStringLiteral("Dompa"));
    QCOMPARE(r.summary.participants, QStringList({ "Ádám", "Béla" }));
    // POST + 2 polls (processing, completed); the POST carried the lines and was charged once.
    QCOMPARE(log.size(), 3);
    QVERIFY(log[0].path.endsWith("/v1/summaries"));
    QCOMPARE(log[0].status, 202);
    const QJsonObject posted = QJsonDocument::fromJson(log[0].body).object();
    QVERIFY(!posted.value("id").toString().isEmpty());
    QVERIFY(log[2].path.endsWith("/v1/summaries/" + posted.value("id").toString()));
    QCOMPARE(m_run->calls, 1);
    const QJsonObject last = mockState().value("usage").toArray().last().toObject();
    QCOMPARE(last.value("kind").toString(), QStringLiteral("summary"));
    QCOMPARE(last.value("summary_mode").toString(), QStringLiteral("complex"));
}

void TestCloudGateway::capabilities_tags()
{
    QVector<net::HttpExchange> log;
    auto tags = make<providers::ITagSuggester>(cloud::capabilityProviderId(), gatewayContext(&log));
    QVERIFY(tags);
    providers::TagSuggestRequest req;
    req.title = QStringLiteral("Nordvik ütemterv");
    req.summaryText = QStringLiteral("A raktár költözése csúszik. #Raktárköltözés #Logisztika #Harmadik");
    req.candidates = { { "tag-1", "Nordvik", "Kovács Anna; raktár" }, { "tag-2", "MuseumPlus", QString() },
                       { "tag-3", "Belső", QString() } };
    req.maxNew = 2;
    providers::TagSuggestJob* job = tags->suggest(req);
    QVERIFY(waitJob(job));
    QVERIFY2(!job->error().isError(), qPrintable(job->error().message));
    QStringList names;
    for (const TagSuggestion& t : job->result()) names << (t.isNew ? "+" : t.tagId + ":") + t.name;
    QCOMPARE(names, QStringList({ "tag-1:Nordvik", "+Raktárköltözés", "+Logisztika" }));
    QVERIFY(log.last().path.endsWith("/v1/tags/suggest"));
    const QJsonObject sent = mockState().value("usage").toArray().last().toObject();
    QCOMPARE(sent.value("kind").toString(), QStringLiteral("tags"));
    QCOMPARE(m_run->calls, 1);
    // A tag already on the meeting is dropped.
    req.currentTagIds = { "tag-1" };
    job = tags->suggest(req);
    QVERIFY(waitJob(job));
    QCOMPARE(job->result().size(), 2);
}

void TestCloudGateway::capabilities_modelCatalog()
{
    QVector<net::HttpExchange> log;
    auto cat = make<providers::IModelCatalog>(cloud::capabilityProviderId(), gatewayContext(&log));
    QVERIFY(cat);
    providers::ModelListJob* job = cat->list(providers::CapabilityKind::VoiceEmbedding);
    QVERIFY(waitJob(job));
    QCOMPARE(job->result().size(), 1);
    QCOMPARE(job->result()[0].id, QStringLiteral("tanara/voice-embedding"));
    QCOMPARE(job->result()[0].dim, 192);
    QCOMPARE(job->result()[0].origin, providers::Origin::Cloud);
    QVERIFY(log.last().path.endsWith("/v1/models"));
    job = cat->list(providers::CapabilityKind::Transcription);
    QVERIFY(waitJob(job));
    QVERIFY(job->result().size() >= 2);
    for (const providers::ModelInfo& m : job->result()) QVERIFY(!m.id.contains("embedding"));

    providers::ModelReadyJob* ready = cat->ensureAvailable(QStringLiteral("tanara/stt-fast"));
    QVERIFY(waitJob(ready));
    QCOMPARE(ready->result().remoteId, QStringLiteral("tanara/stt-fast"));
    QVERIFY(!ready->result().isLocal());
    ready = cat->ensureAvailable(QStringLiteral("no/such-model"));
    QVERIFY(waitJob(ready));
    QCOMPARE(ready->error().kind, Error::Kind::Input);
    QCOMPARE(m_run->calls, 0);   // listing costs nothing
}

void TestCloudGateway::capabilities_estimator()
{
    cloud::CloudEstimator est(m_acc.get());
    providers::CostEstimateRequest req;
    req.task = QStringLiteral("transcribe");
    req.durationMs = 3600000;
    req.tracks = 2;
    req.sttModel = QStringLiteral("tanara/stt-accurate");
    providers::CostEstimateJob* job = est.estimateCost(req);
    QVERIFY(waitJob(job));
    QVERIFY2(!job->error().isError(), qPrintable(job->error().message));
    QVERIFY(job->result().estimateMicros > 0);
    QCOMPARE(job->result().currency, QStringLiteral("USD"));
    QVERIFY(job->result().enough);
    req.task = QStringLiteral("nonsense");
    job = est.estimateCost(req);
    QVERIFY(waitJob(job));
    QCOMPARE(job->error().kind, Error::Kind::Input);   // 400 validation_error
}

void TestCloudGateway::capabilities_errors_402_401_422()
{
    QVector<net::HttpExchange> log;
    m_media->pcm.insert("track_mic.ogg", QVector<qint16>(16000 * 3, 2000));
    auto voice = make<providers::IVoiceEmbedder>(cloud::capabilityProviderId(), gatewayContext(&log));
    auto idf = make<providers::IParticipantIdentifier>(cloud::capabilityProviderId(), gatewayContext(&log));
    providers::ProviderConfig cfg;
    cfg.extra.insert(QStringLiteral("pollIntervalMs"), 20);
    auto sum = make<providers::ISummarizer>(cloud::capabilityProviderId(), gatewayContext(&log), cfg);
    auto tags = make<providers::ITagSuggester>(cloud::capabilityProviderId(), gatewayContext(&log));
    auto text = make<providers::ITextEmbedder>(providers::embeddingproviders::CloudId, gatewayContext(&log));

    providers::VoiceEmbeddingRequest vr;
    vr.pcm = { QVector<float>(16000, 0.2f) };
    providers::IdentifyRequest ir;
    providers::IdentifyProbe probe;
    probe.vectors = { { QVector<float>(4, 0.5f), 4, QStringLiteral("tanara/voice-embedding"), 0 } };
    ir.probes = { probe };
    providers::SummarizationRequest sr;
    TranscriptToken tok; tok.text = "Szia."; tok.speaker = "Ádám"; tok.endMs = 1000;
    sr.transcript.tokens = { tok };
    providers::TagSuggestRequest tr;
    tr.summaryText = QStringLiteral("Nordvik.");

    const auto expectAll = [&](Error::Kind kind, int status) {
        QList<providers::CapabilityJob*> jobs{ voice->embed(vr), idf->identify(ir), sum->summarize(sr), tags->suggest(tr) };
        for (providers::CapabilityJob* j : jobs) {
            QVERIFY(waitJob(j));
            QCOMPARE(j->error().kind, kind);
            QCOMPARE(j->error().httpStatus, status);
        }
        providers::EmbeddingJob* ej = text->embed({ { "x" }, QString() });
        QSignalSpy failed(ej, &providers::EmbeddingJob::failed);
        QVERIFY(failed.wait(5000));
        QCOMPARE(ej->error().httpStatus, status);
    };

    // 402: out of balance; nothing is charged.
    mockConfig({ { "balance", 0 } });
    const int callsBefore = m_run->calls;
    expectAll(Error::Kind::Quota, 402);
    QCOMPARE(m_run->calls, callsBefore);
    mockConfig({ { "balance", 10 } });

    // 401: a revoked / wrong key (the context's auth source wins over the session's).
    providers::ProviderContext bad = gatewayContext(&log);
    bad.auth = providers::AuthSource::fixed(QStringLiteral("tk_revoked"));
    voice = make<providers::IVoiceEmbedder>(cloud::capabilityProviderId(), bad);
    idf = make<providers::IParticipantIdentifier>(cloud::capabilityProviderId(), bad);
    sum = make<providers::ISummarizer>(cloud::capabilityProviderId(), bad, cfg);
    tags = make<providers::ITagSuggester>(cloud::capabilityProviderId(), bad);
    text = make<providers::ITextEmbedder>(providers::embeddingproviders::CloudId, bad);
    expectAll(Error::Kind::Auth, 401);

    // 422: an unprocessable input (an empty summary / no transcript lines).
    tags = make<providers::ITagSuggester>(cloud::capabilityProviderId(), gatewayContext(&log));
    sum = make<providers::ISummarizer>(cloud::capabilityProviderId(), gatewayContext(&log), cfg);
    tr.summaryText.clear();
    providers::TagSuggestJob* tj = tags->suggest(tr);
    QVERIFY(waitJob(tj));
    QCOMPARE(tj->error().kind, Error::Kind::Input);
    QCOMPARE(tj->error().httpStatus, 422);
    sr.transcript.tokens.clear();
    providers::SummarizationJob* sj = sum->summarize(sr);
    QVERIFY(waitJob(sj));
    QCOMPARE(sj->error().kind, Error::Kind::Input);
    QCOMPARE(sj->error().code, QStringLiteral("invalid_input"));
}

void TestCloudGateway::logout_removesKey()
{
    QVERIFY(m_acc->isLoggedIn());
    const QString key = m_acc->apiKey();
    QSignalSpy out(m_acc.get(), &cloud::CloudSession::loggedOut);
    m_acc->logout();
    QVERIFY(out.wait(5000));
    QVERIFY(!m_acc->isLoggedIn());
    QVERIFY(m_keys->get(cloud::ApiKeySecret).isEmpty());
    QVERIFY(m_acc->email().isEmpty());
    // With the revoked key the gateway gives 401.
    m_keys->set(cloud::ApiKeySecret, key);
    QSignalSpy failed(m_acc.get(), &cloud::CloudSession::accountFailed);
    m_acc->refreshAccount();
    QVERIFY(failed.wait(5000));
    QCOMPARE(failed.first().first().value<cloud::CloudError>().kind, cloud::CloudErrorKind::Unauthorized);
    m_keys->remove(cloud::ApiKeySecret);
}

#endif // Q_MOC_RUN

QTEST_GUILESS_MAIN(TestCloudGateway)
#include "test_cloud_gateway.moc"
