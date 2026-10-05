//
// Tanara Cloud — a gateway-szerződés (docs/cloud-gateway-api.yaml 1.2.0) kliensoldali
// feldolgozó / formázó függvényeinek unit-tesztjei (hálózat nélkül).
//
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/Types.h"
#include "tanara/cloud/CloudTypes.h"

using namespace tanara;

namespace {
QJsonObject obj(const char* json) { return QJsonDocument::fromJson(QByteArray(json)).object(); }
Money usd(qint64 micros) { return Money{ micros, QStringLiteral("USD") }; }
const QString NBSP = QString(QChar(0x00A0));
const QString MINUS = QString(QChar(0x2212));
}

class TestCloudTypes : public QObject {
    Q_OBJECT
private slots:
    void money_balance_floorsAndLocalizes();
    void money_charge_precisionTiers();
    void hours_rules();
    void semver_and_client_header();
    void vat_labels();
    void error_402_hold();
    void error_kinds();
    void account_parse();
    void models_parse_and_tiers();
    void estimate_request_and_levels();
    void charge_and_job_info();
    void device_code_parse();
    void waitlist_body();
    void waitlist_validation();
};

// A slot-törzsek a moc elől rejtve (a moc nem bírja a JSON raw-string literálokat).
#ifndef Q_MOC_RUN
void TestCloudTypes::money_balance_floorsAndLocalizes()
{
    QCOMPARE(formatMoney(usd(12409999), MoneyStyle::Balance, "hu"), QStringLiteral("$12,40"));
    QCOMPARE(formatMoney(usd(12409999), MoneyStyle::Balance, "en"), QStringLiteral("$12.40"));
    QCOMPARE(formatMoney(usd(1284500000), MoneyStyle::Balance, "hu"), QStringLiteral("$1") + NBSP + QStringLiteral("284,50"));
    QCOMPARE(formatMoney(usd(1284500000), MoneyStyle::Balance, "en"), QStringLiteral("$1,284.50"));
    QCOMPARE(formatMoney(usd(0), MoneyStyle::Balance, "hu"), QStringLiteral("$0,00"));
    QCOMPARE(formatMoney(usd(-25000000), MoneyStyle::Balance, "hu"), MINUS + QStringLiteral("$25,00"));
    QCOMPARE(formatMoney(Money{}, MoneyStyle::Balance, "hu"), QStringLiteral("—"));
}

void TestCloudTypes::money_charge_precisionTiers()
{
    QCOMPARE(formatMoney(usd(415053), MoneyStyle::Charge, "hu"), QStringLiteral("$0,42"));
    QCOMPARE(formatMoney(usd(4200), MoneyStyle::Charge, "en"), QStringLiteral("$0.0042"));
    QCOMPARE(formatMoney(usd(555), MoneyStyle::Charge, "hu"), QStringLiteral("$0,0006"));
    QCOMPARE(formatMoney(usd(42), MoneyStyle::Charge, "en"), QStringLiteral("< $0.0001"));
    QCOMPARE(formatMoney(usd(0), MoneyStyle::Charge, "en"), QStringLiteral("$0.00"));
    QCOMPARE(formatMoney(usd(-420000), MoneyStyle::Charge, "en"), MINUS + QStringLiteral("$0.42"));
    QCOMPARE(formatMoney(Money{ 12400000, "EUR" }, MoneyStyle::Balance, "hu"), QStringLiteral("12,40") + NBSP + QStringLiteral("EUR"));
}

void TestCloudTypes::hours_rules()
{
    QCOMPARE(formatHours(38.9, "hu"), QStringLiteral("38 óra"));
    QCOMPARE(formatHours(2.79, "hu"), QStringLiteral("2,7 óra"));
    QCOMPARE(formatHours(2.0, "hu"), QStringLiteral("2 óra"));
    QCOMPARE(formatHours(0.75, "hu"), QStringLiteral("45 perc"));
    QCOMPARE(formatHours(0.0, "hu"), QStringLiteral("0 perc"));
    QCOMPARE(formatDuration(3735000), QStringLiteral("1:02:15"));
    QCOMPARE(formatDuration(65000), QStringLiteral("1:05"));
}

void TestCloudTypes::semver_and_client_header()
{
    QVERIFY(semverLess("0.1.4", "0.2.0"));
    QVERIFY(!semverLess("0.2.0", "0.2.0"));
    QVERIFY(!semverLess("1.0", "0.9.9"));
    QVERIFY(semverLess("0.1", "0.1.1"));
    QVERIFY(clientHeaderValue().startsWith(libraryVersion() + "/"));
#if defined(Q_OS_LINUX)
    QCOMPARE(clientPlatform(), QStringLiteral("linux"));
#elif defined(Q_OS_WIN)
    QCOMPARE(clientPlatform(), QStringLiteral("windows"));
#endif
}

void TestCloudTypes::vat_labels()
{
    QCOMPARE(vatLabel("gross"), QStringLiteral("ÁFA-t tartalmaz"));
    QCOMPARE(vatLabel("reverse_charge"), QStringLiteral("ÁFA nélkül, fordított adózás"));
}

void TestCloudTypes::error_402_hold()
{
    QHash<QByteArray, QByteArray> h{ { "x-tanara-request-id", "req_HDR" } };
    const CloudError e = parseCloudError(402, h, R"({"error":{"code":"insufficient_balance","message":"Nincs elég egyenleg.", "request_id":"req_01","support_url":"https://s/x","balance":{"amount_micros":200000,"currency":"USD"}, "needed":{"amount_micros":620000,"currency":"USD"},"needed_basis":"hold","topup_url":null,"contact_url":"https://c"}})");
    QCOMPARE(e.kind, CloudErrorKind::InsufficientBalance);
    QCOMPARE(e.requestId, QStringLiteral("req_01"));       // a törzs request_id-je az elsődleges
    QCOMPARE(e.neededBasis, QStringLiteral("hold"));
    QCOMPARE(e.needed.micros, 620000);
    QCOMPARE(e.balance.micros, 200000);
    QVERIFY(e.topupUrl.isEmpty());
    QCOMPARE(e.contactUrl, QStringLiteral("https://c"));
}

void TestCloudTypes::error_kinds()
{
    const QHash<QByteArray, QByteArray> h{ { "x-tanara-request-id", "req_H" }, { "retry-after", "30" } };
    QCOMPARE(parseCloudError(403, h, R"({"error":{"code":"account_suspended","message":"m","request_id":"r","suspension_reason":"payment_dispute"}})").suspensionReason,
             QStringLiteral("payment_dispute"));
    const CloudError t = parseCloudError(403, h, R"({"error":{"code":"terms_acceptance_required","message":"m","request_id":"r","terms_version":"2026-11-01","terms_url":"u"}})");
    QCOMPARE(t.kind, CloudErrorKind::TermsRequired);
    QCOMPARE(t.termsVersion, QStringLiteral("2026-11-01"));
    const CloudError rl = parseCloudError(429, h, R"({"error":{"code":"rate_limited","message":"m","request_id":"r"}})");
    QCOMPARE(rl.kind, CloudErrorKind::RateLimited);
    QCOMPARE(rl.retryAfterSec, 30);                     // a Retry-After fejlécből
    const CloudError sl = parseCloudError(429, h, R"({"error":{"code":"spend_limit_reached","message":"m","request_id":"r", "limit":{"period":"daily","amount":{"amount_micros":5000000,"currency":"USD"},"resets_at":"2026-10-02T00:00:00Z"},"settings_url":"https://w/keys"}})");
    QCOMPARE(sl.kind, CloudErrorKind::SpendLimit);
    QCOMPARE(sl.limitPeriod, QStringLiteral("daily"));
    QCOMPARE(sl.settingsUrl, QStringLiteral("https://w/keys"));
    const CloudError mt = parseCloudError(503, h, R"({"error":{"code":"maintenance","message":"m","request_id":"r","retry_after":1500, "window":{"starts_at":"2026-10-01T20:00:00Z","ends_at":"2026-10-01T20:30:00Z"}}})");
    QCOMPARE(mt.kind, CloudErrorKind::Maintenance);
    QCOMPARE(mt.retryAfterSec, 1500);
    QVERIFY(mt.windowEnd.isValid());
    QCOMPARE(parseCloudError(503, {}, R"({"error":{"code":"upstream_unavailable","message":"m","request_id":"r"}})").kind, CloudErrorKind::Upstream);
    QCOMPARE(parseCloudError(500, {}, R"({"error":{"code":"internal_error","message":"m","request_id":"r"}})").kind, CloudErrorKind::Internal);
    const CloudError old = parseCloudError(426, { { "x-tanara-min-client", "0.2.0" } }, R"({"error":{"code":"client_too_old","message":"m","request_id":"r"}})");
    QCOMPARE(old.kind, CloudErrorKind::ClientTooOld);
    QCOMPARE(old.minClient, QStringLiteral("0.2.0"));   // a fejlécből, ha a törzsben nincs
    QCOMPARE(parseCloudError(401, h, "{}").kind, CloudErrorKind::Unauthorized);
    QCOMPARE(parseCloudError(401, h, "{}").requestId, QStringLiteral("req_H"));
    const CloudError de = parseCloudError(400, {}, R"({"error":{"code":"disposable_email","message":"m","request_id":"r","fields":{"email":{"code":"disposable_email","message":"x"}}}})");
    QCOMPARE(de.kind, CloudErrorKind::DisposableEmail);
    QCOMPARE(de.fieldErrors.value("email"), QStringLiteral("disposable_email"));
    QCOMPARE(parseCloudError(400, {}, R"({"error":{"code":"access_denied","message":"m","request_id":"r"}})").kind, CloudErrorKind::DeviceFlow);
    QCOMPARE(parseCloudError(409, {}, R"({"error":{"code":"topup_unavailable","message":"m","request_id":"r","contact_url":"c"}})").kind, CloudErrorKind::TopupUnavailable);
    const CloudError net = parseCloudError(0, {}, {}, "Connection refused");
    QCOMPARE(net.kind, CloudErrorKind::Network);
    QVERIFY(net.requestId.isEmpty());
    QVERIFY(!parseCloudError(200, {}, "{}").isError());
}

void TestCloudTypes::account_parse()
{
    const AccountInfo a = accountFromJson(obj(R"({"email":"anna@example.com","balance":{"amount_micros":1100000,"currency":"USD"}, "vat_mode":"reverse_charge","low_balance_threshold":{"amount_micros":2000000,"currency":"USD"},"low_balance":true, "low_balance_since":"2026-09-30T10:00:00Z","balance_empty":false,"hours_left":{"fast":11.2,"accurate":2.79}, "topup_available":false,"contact_url":"c","dashboard_url":"d","usage_url":"u","trial":"granted", "terms":{"accepted_version":"2026-06-01","current_version":"2026-06-01","current_url":"t","upcoming": {"version":"2026-11-01","effective_from":"2026-11-01T00:00:00Z","url":"t2","summary":"s"}}, "notices":[{"id":"n1","level":"warning","message":"Karbantartás","url":null}]})"));
    QVERIFY(a.valid);
    QCOMPARE(a.balance.micros, 1100000);
    QCOMPARE(a.vatMode, QStringLiteral("reverse_charge"));
    QVERIFY(a.lowBalance);
    QVERIFY(a.lowBalanceSince.isValid());
    QCOMPARE(a.hoursAccurate, 2.79);
    QVERIFY(a.terms.needsEarlyAcceptance());
    QVERIFY(!a.terms.needsAcceptance());
    QCOMPARE(a.notices.size(), 1);
    QCOMPARE(a.notices[0].level, QStringLiteral("warning"));
    QVERIFY(a.notices[0].url.isEmpty());

    TermsStatus t;
    t.acceptedVersion = "2026-06-01"; t.currentVersion = "2026-11-01";
    QVERIFY(t.needsAcceptance());
}

void TestCloudTypes::models_parse_and_tiers()
{
    const QVector<CloudModel> ms = modelsFromJson(obj(R"({"object":"list","data":[ {"id":"tanara/stt-fast","object":"model","owned_by":"tanara","tanara":{"kind":"stt","tier":"fast","virtual":true,"expert":false, "display_name":"Gyors","diarization":false,"languages":null,"hidden_for_languages":["hu"],"price":{"per_hour":{"amount_micros":80000,"currency":"USD"}}}}, {"id":"tanara/stt-accurate","object":"model","owned_by":"tanara","tanara":{"kind":"stt","tier":"accurate","virtual":true, "diarization":true,"languages":null,"price":{"per_hour":{"amount_micros":400000,"currency":"USD"}}}}, {"id":"x/llm","object":"model","owned_by":"x","tanara":{"kind":"llm","tier":null,"virtual":false,"expert":true, "languages":["en"],"price":{"per_1m_input_tokens":{"amount_micros":3000000,"currency":"USD"},"per_1m_output_tokens":{"amount_micros":15000000,"currency":"USD"}}}}, {"id":"no-meta","object":"model","owned_by":"x"}]})"));
    QCOMPARE(ms.size(), 3);   // a tanara-metaadat nélküli kimarad
    const auto fast = findTierModel(ms, "stt", "fast");
    QVERIFY(fast.has_value());
    QVERIFY(!fast->diarization);
    QVERIFY(fast->notRecommendedFor("hu"));
    QVERIFY(!fast->notRecommendedFor("en"));
    QVERIFY(!fast->notRecommendedFor("auto"));
    QCOMPARE(findTierModel(ms, "stt", "accurate")->perHour.micros, 400000);
    const auto llm = findModel(ms, "x/llm");
    QVERIFY(llm->expert && llm->tier.isEmpty());
    QVERIFY(llm->notRecommendedFor("hu"));             // languages: [en]
    QCOMPARE(llm->perMOutput.micros, 15000000);
    QCOMPARE(virtualModelId("stt", "fast"), QStringLiteral("tanara/stt-fast"));
    QCOMPARE(virtualModelId("llm", "accurate"), QStringLiteral("tanara/summary-accurate"));
}

void TestCloudTypes::estimate_request_and_levels()
{
    EstimateRequest r;
    r.task = "summarize"; r.durationMs = 3735000; r.llmModel = "tanara/summary-accurate"; r.summaryMode = "complex";
    r.llmCalls = { { 1, 48000, 4000 }, { -1, 48000, 4000 }, { 1, 12000, 2000 } };
    const QJsonObject o = estimateRequestToJson(r);
    QCOMPARE(o.value("task").toString(), QStringLiteral("summarize"));
    QVERIFY(!o.contains("stt_model"));
    QVERIFY(!o.contains("transcript_chars"));
    const QJsonArray calls = o.value("llm_calls").toArray();
    QCOMPARE(calls.size(), 3);
    QVERIFY(calls[1].toObject().value("count").isNull());   // ismeretlen témaszám → null
    QCOMPARE(calls[0].toObject().value("count").toInt(), 1);

    EstimateResult e = estimateFromJson(obj(R"({"estimate":{"amount_micros":460000,"currency":"USD"},"low":{"amount_micros":380000,"currency":"USD"}, "high":{"amount_micros":610000,"currency":"USD"},"balance":{"amount_micros":12400000,"currency":"USD"},"required":{"amount_micros":700000,"currency":"USD"}, "enough":true,"balance_after":{"amount_micros":11940000,"currency":"USD"},"low_balance_after":false,"vat_mode":"gross", "breakdown":[{"item":"stt","model":"tanara/stt-accurate","tier":"accurate","summary_mode":null,"amount":{"amount_micros":410000,"currency":"USD"},"exact":true}, {"item":"llm","model":"tanara/summary-accurate","tier":"accurate","summary_mode":"complex","amount":{"amount_micros":50000,"currency":"USD"},"exact":false}]})"));
    QVERIFY(e.valid);
    QCOMPARE(e.breakdown.size(), 2);
    QVERIFY(e.breakdown[0].exact);
    QVERIFY(e.breakdown[0].summaryMode.isEmpty());
    QCOMPARE(estimateLevel(e), EstimateLevel::Enough);
    e.enough = false; e.balance = usd(500000);            // estimate ≤ balance < required
    QCOMPARE(estimateLevel(e), EstimateLevel::LittleReserve);
    e.balance = usd(200000);                              // balance < estimate
    QCOMPARE(estimateLevel(e), EstimateLevel::NotEnough);
}

void TestCloudTypes::charge_and_job_info()
{
    const ChargeInfo c = chargeInfoFromJson(obj(R"({"charge_id":"ch_1","charge":{"amount_micros":410000,"currency":"USD"}, "balance":{"amount_micros":11990000,"currency":"USD"},"low_balance":false,"balance_empty":false,"vat_mode":"gross","job_id":null})"));
    QVERIFY(c.valid);
    QCOMPARE(c.charge.micros, 410000);
    QVERIFY(c.jobId.isEmpty());
    const ChargeInfo h = chargeInfoFromHeaders({ { "x-tanara-charge-micros", "555" }, { "x-tanara-balance-micros", "12699445" }, { "x-tanara-currency", "USD" } });
    QVERIFY(h.valid);
    QCOMPARE(h.balance.micros, 12699445);
    QVERIFY(!chargeInfoFromHeaders({}).valid);

    const TranscriptionJobInfo j = transcriptionJobInfoFromJson(obj(R"({"id":"tr_1","status":"error","error_message":"x", "tanara":{"refunded":true,"charge":{"amount_micros":320000,"currency":"USD"},"refund":{"amount_micros":320000,"currency":"USD"}, "balance":{"amount_micros":12400000,"currency":"USD"},"error_code":"upstream_error","content_deleted":false,"job_id":"j"}})"));
    QVERIFY(j.valid && j.refunded);
    QCOMPARE(j.status, QStringLiteral("error"));
    QCOMPARE(j.refund.micros, 320000);
    QCOMPARE(j.balance.micros, 12400000);
}

void TestCloudTypes::device_code_parse()
{
    const DeviceCode d = deviceCodeFromJson(obj(R"({"device_code":"dc","user_code":"WDJB-MJHT","verification_uri":"https://a/device", "interval":0,"expires_in":600})"));
    QCOMPARE(d.userCode, QStringLiteral("WDJB-MJHT"));
    QCOMPARE(d.verificationUriComplete, QStringLiteral("https://a/device"));   // hiányzó complete → uri
    QCOMPARE(d.interval, 1);                                                   // min. 1 mp
}

// MKT-02: a várólista-kérés összeállítása (csak a kitöltött opcionális mezők) + előellenőrzés.
void TestCloudTypes::waitlist_body()
{
    WaitlistSignup s;
    s.email = "  anna@example.com ";
    s.consent = true;
    s.uiLanguage = "hu";
    QJsonObject o = waitlistToJson(s);
    QCOMPARE(o.value("email").toString(), QStringLiteral("anna@example.com"));
    QCOMPARE(o.value("consent").toBool(), true);
    QCOMPARE(o.value("source").toString(), QStringLiteral("client"));
    QCOMPARE(o.value("ui_language").toString(), QStringLiteral("hu"));
    QCOMPARE(o.value("platform").toString(), clientPlatform());
    QCOMPARE(o.value("client_version").toString(), libraryVersion());
    QVERIFY(!o.contains("use_case"));              // opcionális, üres → nincs a törzsben
    QVERIFY(!o.contains("meeting_languages"));
    QVERIFY(!o.contains("attribution"));           // csak a webről

    s.useCase = "interviews";
    s.meetingLanguages = { "hu", "other" };
    s.uiLanguage = "de";                           // nem hu/en → kimarad
    o = waitlistToJson(s);
    QCOMPARE(o.value("use_case").toString(), QStringLiteral("interviews"));
    QCOMPARE(o.value("meeting_languages").toArray().size(), 2);
    QVERIFY(!o.contains("ui_language"));
}

void TestCloudTypes::waitlist_validation()
{
    WaitlistSignup s;
    s.email = "anna@example.com";
    QCOMPARE(validateWaitlist(s), QStringLiteral("consent"));   // hozzájárulás kötelező
    s.consent = true;
    QVERIFY(validateWaitlist(s).isEmpty());
    for (const char* bad : { "", "anna", "@example.com", "anna@", "anna@example", "a b@example.com", "a@b@c.hu", "anna@example." }) {
        s.email = QString::fromUtf8(bad);
        QCOMPARE(validateWaitlist(s), QStringLiteral("email"));
    }
}

#endif

QTEST_GUILESS_MAIN(TestCloudTypes)
#include "test_cloud_types.moc"
