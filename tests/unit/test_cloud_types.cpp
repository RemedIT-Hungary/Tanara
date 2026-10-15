// Tanara Cloud: unit tests of the client-side parsing / formatting functions of the gateway
// contract (docs/cloud-gateway-api.yaml 1.2.0) (no network).
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/foundation/Version.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/cloud/CloudCapabilities.h"
#include "tanara/cloud/CloudEstimator.h"
#include "tanara/cloud/CloudMode.h"

using namespace tanara;

namespace {
QJsonObject obj(const char* json) { return QJsonDocument::fromJson(QByteArray(json)).object(); }
cloud::Money usd(qint64 micros) { return cloud::Money{ micros, QStringLiteral("USD") }; }
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
    void error_422_input();
    void cloudMode_resolve();
    void cloudSettings_roundTrip();
    void estimator_plan();
    void gateway_kinds();
};

// The slot bodies are hidden from moc (moc cannot handle JSON raw-string literals).
#ifndef Q_MOC_RUN
void TestCloudTypes::money_balance_floorsAndLocalizes()
{
    QCOMPARE(formatMoney(usd(12409999), cloud::MoneyStyle::Balance, "hu"), QStringLiteral("$12,40"));
    QCOMPARE(formatMoney(usd(12409999), cloud::MoneyStyle::Balance, "en"), QStringLiteral("$12.40"));
    QCOMPARE(formatMoney(usd(1284500000), cloud::MoneyStyle::Balance, "hu"), QStringLiteral("$1") + NBSP + QStringLiteral("284,50"));
    QCOMPARE(formatMoney(usd(1284500000), cloud::MoneyStyle::Balance, "en"), QStringLiteral("$1,284.50"));
    QCOMPARE(formatMoney(usd(0), cloud::MoneyStyle::Balance, "hu"), QStringLiteral("$0,00"));
    QCOMPARE(formatMoney(usd(-25000000), cloud::MoneyStyle::Balance, "hu"), MINUS + QStringLiteral("$25,00"));
    QCOMPARE(formatMoney(cloud::Money{}, cloud::MoneyStyle::Balance, "hu"), QStringLiteral("—"));
}

void TestCloudTypes::money_charge_precisionTiers()
{
    QCOMPARE(formatMoney(usd(415053), cloud::MoneyStyle::Charge, "hu"), QStringLiteral("$0,42"));
    QCOMPARE(formatMoney(usd(4200), cloud::MoneyStyle::Charge, "en"), QStringLiteral("$0.0042"));
    QCOMPARE(formatMoney(usd(555), cloud::MoneyStyle::Charge, "hu"), QStringLiteral("$0,0006"));
    QCOMPARE(formatMoney(usd(42), cloud::MoneyStyle::Charge, "en"), QStringLiteral("< $0.0001"));
    QCOMPARE(formatMoney(usd(0), cloud::MoneyStyle::Charge, "en"), QStringLiteral("$0.00"));
    QCOMPARE(formatMoney(usd(-420000), cloud::MoneyStyle::Charge, "en"), MINUS + QStringLiteral("$0.42"));
    QCOMPARE(formatMoney(cloud::Money{ 12400000, "EUR" }, cloud::MoneyStyle::Balance, "hu"), QStringLiteral("12,40") + NBSP + QStringLiteral("EUR"));
}

void TestCloudTypes::hours_rules()
{
    QCOMPARE(cloud::formatHours(38.9, "hu"), QStringLiteral("38 óra"));
    QCOMPARE(cloud::formatHours(2.79, "hu"), QStringLiteral("2,7 óra"));
    QCOMPARE(cloud::formatHours(2.0, "hu"), QStringLiteral("2 óra"));
    QCOMPARE(cloud::formatHours(0.75, "hu"), QStringLiteral("45 perc"));
    QCOMPARE(cloud::formatHours(0.0, "hu"), QStringLiteral("0 perc"));
    QCOMPARE(cloud::formatDuration(3735000), QStringLiteral("1:02:15"));
    QCOMPARE(cloud::formatDuration(65000), QStringLiteral("1:05"));
}

void TestCloudTypes::semver_and_client_header()
{
    QVERIFY(cloud::semverLess("0.1.4", "0.2.0"));
    QVERIFY(!cloud::semverLess("0.2.0", "0.2.0"));
    QVERIFY(!cloud::semverLess("1.0", "0.9.9"));
    QVERIFY(cloud::semverLess("0.1", "0.1.1"));
    QVERIFY(cloud::clientHeaderValue().startsWith(libraryVersion() + "/"));
#if defined(Q_OS_LINUX)
    QCOMPARE(cloud::clientPlatform(), QStringLiteral("linux"));
#elif defined(Q_OS_WIN)
    QCOMPARE(cloud::clientPlatform(), QStringLiteral("windows"));
#endif
}

void TestCloudTypes::vat_labels()
{
    QCOMPARE(cloud::vatLabel("gross"), QStringLiteral("ÁFA-t tartalmaz"));
    QCOMPARE(cloud::vatLabel("reverse_charge"), QStringLiteral("ÁFA nélkül, fordított adózás"));
}

void TestCloudTypes::error_402_hold()
{
    QHash<QByteArray, QByteArray> h{ { "x-tanara-request-id", "req_HDR" } };
    const cloud::CloudError e = cloud::parseCloudError(402, h, R"({"error":{"code":"insufficient_balance","message":"Nincs elég egyenleg.", "request_id":"req_01","support_url":"https://s/x","balance":{"amount_micros":200000,"currency":"USD"}, "needed":{"amount_micros":620000,"currency":"USD"},"needed_basis":"hold","topup_url":null,"contact_url":"https://c"}})");
    QCOMPARE(e.kind, cloud::CloudErrorKind::InsufficientBalance);
    QCOMPARE(e.requestId, QStringLiteral("req_01"));       // the body's request_id is the primary
    QCOMPARE(e.neededBasis, QStringLiteral("hold"));
    QCOMPARE(e.needed.micros, 620000);
    QCOMPARE(e.balance.micros, 200000);
    QVERIFY(e.topupUrl.isEmpty());
    QCOMPARE(e.contactUrl, QStringLiteral("https://c"));
}

void TestCloudTypes::error_kinds()
{
    const QHash<QByteArray, QByteArray> h{ { "x-tanara-request-id", "req_H" }, { "retry-after", "30" } };
    QCOMPARE(cloud::parseCloudError(403, h, R"({"error":{"code":"account_suspended","message":"m","request_id":"r","suspension_reason":"payment_dispute"}})").suspensionReason,
             QStringLiteral("payment_dispute"));
    const cloud::CloudError t = cloud::parseCloudError(403, h, R"({"error":{"code":"terms_acceptance_required","message":"m","request_id":"r","terms_version":"2026-11-01","terms_url":"u"}})");
    QCOMPARE(t.kind, cloud::CloudErrorKind::TermsRequired);
    QCOMPARE(t.termsVersion, QStringLiteral("2026-11-01"));
    const cloud::CloudError rl = cloud::parseCloudError(429, h, R"({"error":{"code":"rate_limited","message":"m","request_id":"r"}})");
    QCOMPARE(rl.kind, cloud::CloudErrorKind::RateLimited);
    QCOMPARE(rl.retryAfterSec, 30);                     // from the Retry-After header
    const cloud::CloudError sl = cloud::parseCloudError(429, h, R"({"error":{"code":"spend_limit_reached","message":"m","request_id":"r", "limit":{"period":"daily","amount":{"amount_micros":5000000,"currency":"USD"},"resets_at":"2026-10-02T00:00:00Z"},"settings_url":"https://w/keys"}})");
    QCOMPARE(sl.kind, cloud::CloudErrorKind::SpendLimit);
    QCOMPARE(sl.limitPeriod, QStringLiteral("daily"));
    QCOMPARE(sl.settingsUrl, QStringLiteral("https://w/keys"));
    const cloud::CloudError mt = cloud::parseCloudError(503, h, R"({"error":{"code":"maintenance","message":"m","request_id":"r","retry_after":1500, "window":{"starts_at":"2026-10-01T20:00:00Z","ends_at":"2026-10-01T20:30:00Z"}}})");
    QCOMPARE(mt.kind, cloud::CloudErrorKind::Maintenance);
    QCOMPARE(mt.retryAfterSec, 1500);
    QVERIFY(mt.windowEnd.isValid());
    QCOMPARE(cloud::parseCloudError(503, {}, R"({"error":{"code":"upstream_unavailable","message":"m","request_id":"r"}})").kind, cloud::CloudErrorKind::Upstream);
    QCOMPARE(cloud::parseCloudError(500, {}, R"({"error":{"code":"internal_error","message":"m","request_id":"r"}})").kind, cloud::CloudErrorKind::Internal);
    const cloud::CloudError old = cloud::parseCloudError(426, { { "x-tanara-min-client", "0.2.0" } }, R"({"error":{"code":"client_too_old","message":"m","request_id":"r"}})");
    QCOMPARE(old.kind, cloud::CloudErrorKind::ClientTooOld);
    QCOMPARE(old.minClient, QStringLiteral("0.2.0"));   // from the header, if the body has none
    QCOMPARE(cloud::parseCloudError(401, h, "{}").kind, cloud::CloudErrorKind::Unauthorized);
    QCOMPARE(cloud::parseCloudError(401, h, "{}").requestId, QStringLiteral("req_H"));
    const cloud::CloudError de = cloud::parseCloudError(400, {}, R"({"error":{"code":"disposable_email","message":"m","request_id":"r","fields":{"email":{"code":"disposable_email","message":"x"}}}})");
    QCOMPARE(de.kind, cloud::CloudErrorKind::DisposableEmail);
    QCOMPARE(de.fieldErrors.value("email"), QStringLiteral("disposable_email"));
    QCOMPARE(cloud::parseCloudError(400, {}, R"({"error":{"code":"access_denied","message":"m","request_id":"r"}})").kind, cloud::CloudErrorKind::DeviceFlow);
    QCOMPARE(cloud::parseCloudError(409, {}, R"({"error":{"code":"topup_unavailable","message":"m","request_id":"r","contact_url":"c"}})").kind, cloud::CloudErrorKind::TopupUnavailable);
    const cloud::CloudError net = cloud::parseCloudError(0, {}, {}, "Connection refused");
    QCOMPARE(net.kind, cloud::CloudErrorKind::Network);
    QVERIFY(net.requestId.isEmpty());
    QVERIFY(!cloud::parseCloudError(200, {}, "{}").isError());
}

void TestCloudTypes::account_parse()
{
    const cloud::AccountInfo a = cloud::accountFromJson(obj(R"({"email":"anna@example.com","balance":{"amount_micros":1100000,"currency":"USD"}, "vat_mode":"reverse_charge","low_balance_threshold":{"amount_micros":2000000,"currency":"USD"},"low_balance":true, "low_balance_since":"2026-09-30T10:00:00Z","balance_empty":false,"hours_left":{"fast":11.2,"accurate":2.79}, "topup_available":false,"contact_url":"c","dashboard_url":"d","usage_url":"u","trial":"granted", "terms":{"accepted_version":"2026-06-01","current_version":"2026-06-01","current_url":"t","upcoming": {"version":"2026-11-01","effective_from":"2026-11-01T00:00:00Z","url":"t2","summary":"s"}}, "notices":[{"id":"n1","level":"warning","message":"Karbantartás","url":null}]})"));
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

    cloud::TermsStatus t;
    t.acceptedVersion = "2026-06-01"; t.currentVersion = "2026-11-01";
    QVERIFY(t.needsAcceptance());
}

void TestCloudTypes::models_parse_and_tiers()
{
    const QVector<cloud::CloudModel> ms = cloud::modelsFromJson(obj(R"({"object":"list","data":[ {"id":"tanara/stt-fast","object":"model","owned_by":"tanara","tanara":{"kind":"stt","tier":"fast","virtual":true,"expert":false, "display_name":"Gyors","diarization":false,"languages":null,"hidden_for_languages":["hu"],"price":{"per_hour":{"amount_micros":80000,"currency":"USD"}}}}, {"id":"tanara/stt-accurate","object":"model","owned_by":"tanara","tanara":{"kind":"stt","tier":"accurate","virtual":true, "diarization":true,"languages":null,"price":{"per_hour":{"amount_micros":400000,"currency":"USD"}}}}, {"id":"x/llm","object":"model","owned_by":"x","tanara":{"kind":"llm","tier":null,"virtual":false,"expert":true, "languages":["en"],"price":{"per_1m_input_tokens":{"amount_micros":3000000,"currency":"USD"},"per_1m_output_tokens":{"amount_micros":15000000,"currency":"USD"}}}}, {"id":"no-meta","object":"model","owned_by":"x"}]})"));
    QCOMPARE(ms.size(), 3);   // one without tanara metadata is left out
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
    QCOMPARE(cloud::virtualModelId("stt", "fast"), QStringLiteral("tanara/stt-fast"));
    QCOMPARE(cloud::virtualModelId("llm", "accurate"), QStringLiteral("tanara/summary-accurate"));
}

void TestCloudTypes::estimate_request_and_levels()
{
    cloud::EstimateRequest r;
    r.task = "summarize"; r.durationMs = 3735000; r.llmModel = "tanara/summary-accurate"; r.summaryMode = "complex";
    r.llmCalls = { { 1, 48000, 4000 }, { -1, 48000, 4000 }, { 1, 12000, 2000 } };
    const QJsonObject o = estimateRequestToJson(r);
    QCOMPARE(o.value("task").toString(), QStringLiteral("summarize"));
    QVERIFY(!o.contains("stt_model"));
    QVERIFY(!o.contains("transcript_chars"));
    const QJsonArray calls = o.value("llm_calls").toArray();
    QCOMPARE(calls.size(), 3);
    QVERIFY(calls[1].toObject().value("count").isNull());   // unknown topic count: null
    QCOMPARE(calls[0].toObject().value("count").toInt(), 1);

    cloud::EstimateResult e = cloud::estimateFromJson(obj(R"({"estimate":{"amount_micros":460000,"currency":"USD"},"low":{"amount_micros":380000,"currency":"USD"}, "high":{"amount_micros":610000,"currency":"USD"},"balance":{"amount_micros":12400000,"currency":"USD"},"required":{"amount_micros":700000,"currency":"USD"}, "enough":true,"balance_after":{"amount_micros":11940000,"currency":"USD"},"low_balance_after":false,"vat_mode":"gross", "breakdown":[{"item":"stt","model":"tanara/stt-accurate","tier":"accurate","summary_mode":null,"amount":{"amount_micros":410000,"currency":"USD"},"exact":true}, {"item":"llm","model":"tanara/summary-accurate","tier":"accurate","summary_mode":"complex","amount":{"amount_micros":50000,"currency":"USD"},"exact":false}]})"));
    QVERIFY(e.valid);
    QCOMPARE(e.breakdown.size(), 2);
    QVERIFY(e.breakdown[0].exact);
    QVERIFY(e.breakdown[0].summaryMode.isEmpty());
    QCOMPARE(estimateLevel(e), cloud::EstimateLevel::Enough);
    e.enough = false; e.balance = usd(500000);            // estimate ≤ balance < required
    QCOMPARE(estimateLevel(e), cloud::EstimateLevel::LittleReserve);
    e.balance = usd(200000);                              // balance < estimate
    QCOMPARE(estimateLevel(e), cloud::EstimateLevel::NotEnough);
}

void TestCloudTypes::charge_and_job_info()
{
    const cloud::ChargeInfo c = cloud::chargeInfoFromJson(obj(R"({"charge_id":"ch_1","charge":{"amount_micros":410000,"currency":"USD"}, "balance":{"amount_micros":11990000,"currency":"USD"},"low_balance":false,"balance_empty":false,"vat_mode":"gross","job_id":null})"));
    QVERIFY(c.valid);
    QCOMPARE(c.charge.micros, 410000);
    QVERIFY(c.jobId.isEmpty());
    const cloud::ChargeInfo h = cloud::chargeInfoFromHeaders({ { "x-tanara-charge-micros", "555" }, { "x-tanara-balance-micros", "12699445" }, { "x-tanara-currency", "USD" } });
    QVERIFY(h.valid);
    QCOMPARE(h.balance.micros, 12699445);
    QVERIFY(!cloud::chargeInfoFromHeaders({}).valid);

    const cloud::TranscriptionJobInfo j = cloud::transcriptionJobInfoFromJson(obj(R"({"id":"tr_1","status":"error","error_message":"x", "tanara":{"refunded":true,"charge":{"amount_micros":320000,"currency":"USD"},"refund":{"amount_micros":320000,"currency":"USD"}, "balance":{"amount_micros":12400000,"currency":"USD"},"error_code":"upstream_error","content_deleted":false,"job_id":"j"}})"));
    QVERIFY(j.valid && j.refunded);
    QCOMPARE(j.status, QStringLiteral("error"));
    QCOMPARE(j.refund.micros, 320000);
    QCOMPARE(j.balance.micros, 12400000);
}

void TestCloudTypes::device_code_parse()
{
    const cloud::DeviceCode d = cloud::deviceCodeFromJson(obj(R"({"device_code":"dc","user_code":"WDJB-MJHT","verification_uri":"https://a/device", "interval":0,"expires_in":600})"));
    QCOMPARE(d.userCode, QStringLiteral("WDJB-MJHT"));
    QCOMPARE(d.verificationUriComplete, QStringLiteral("https://a/device"));   // missing complete: uri
    QCOMPARE(d.interval, 1);                                                   // min. 1 s
}

// Waitlist request assembly (only the filled optional fields) + pre-check.
void TestCloudTypes::waitlist_body()
{
    cloud::WaitlistSignup s;
    s.email = "  anna@example.com ";
    s.consent = true;
    s.uiLanguage = "hu";
    QJsonObject o = waitlistToJson(s);
    QCOMPARE(o.value("email").toString(), QStringLiteral("anna@example.com"));
    QCOMPARE(o.value("consent").toBool(), true);
    QCOMPARE(o.value("source").toString(), QStringLiteral("client"));
    QCOMPARE(o.value("ui_language").toString(), QStringLiteral("hu"));
    QCOMPARE(o.value("platform").toString(), cloud::clientPlatform());
    QCOMPARE(o.value("client_version").toString(), libraryVersion());
    QVERIFY(!o.contains("use_case"));              // optional, empty: not in the body
    QVERIFY(!o.contains("meeting_languages"));
    QVERIFY(!o.contains("attribution"));           // only from the web

    s.useCase = "interviews";
    s.meetingLanguages = { "hu", "other" };
    s.uiLanguage = "de";                           // not hu/en: left out
    o = waitlistToJson(s);
    QCOMPARE(o.value("use_case").toString(), QStringLiteral("interviews"));
    QCOMPARE(o.value("meeting_languages").toArray().size(), 2);
    QVERIFY(!o.contains("ui_language"));
}

void TestCloudTypes::waitlist_validation()
{
    cloud::WaitlistSignup s;
    s.email = "anna@example.com";
    QCOMPARE(validateWaitlist(s), QStringLiteral("consent"));   // consent is required
    s.consent = true;
    QVERIFY(validateWaitlist(s).isEmpty());
    for (const char* bad : { "", "anna", "@example.com", "anna@", "anna@example", "a b@example.com", "a@b@c.hu", "anna@example." }) {
        s.email = QString::fromUtf8(bad);
        QCOMPARE(validateWaitlist(s), QStringLiteral("email"));
    }
}

void TestCloudTypes::error_422_input()
{
    // The capability endpoints' 422 invalid_input is an input error; the top-up limit stays its own kind.
    const QByteArray body = R"({"error":{"code":"invalid_input","message":"x","fields":{"lines":{"code":"required"}}}})";
    const cloud::CloudError e = cloud::parseCloudError(422, {}, body);
    QCOMPARE(e.kind, cloud::CloudErrorKind::Validation);
    QCOMPARE(cloud::toError(e).kind, Error::Kind::Input);
    QCOMPARE(e.fieldErrors.value("lines"), QStringLiteral("required"));
    const cloud::CloudError t = cloud::parseCloudError(422, {}, R"({"error":{"code":"topup_limit_exceeded"}})");
    QCOMPARE(t.kind, cloud::CloudErrorKind::TopupLimit);
}

void TestCloudTypes::cloudSettings_roundTrip()
{
    // S4: the account-level level and the "Haladó" switches (defaults, JSON round trip).
    const cloud::CloudSettings def;
    QCOMPARE(def.level, QStringLiteral("simple"));
    QVERIFY(def.estimateBeforeRun);
    QVERIFY(!def.fastForLongRecordings);
    QVERIFY(def.deleteUploadedAudio);
    QCOMPARE(cloud::CloudSettings::kLongRecordingMinutes, 90);

    // A section without the new keys keeps the defaults.
    const cloud::CloudSettings old = cloud::CloudSettings::fromJson(obj(R"({"sttTier":"fast"})"));
    QCOMPARE(old.level, QStringLiteral("simple"));
    QVERIFY(old.estimateBeforeRun);
    QVERIFY(!old.fastForLongRecordings);
    QVERIFY(old.deleteUploadedAudio);

    cloud::CloudSettings s;
    s.level = QStringLiteral("advanced");
    s.sttTier = s.llmTier = QStringLiteral("fast");
    s.estimateBeforeRun = false;
    s.fastForLongRecordings = true;
    s.deleteUploadedAudio = false;
    const QJsonObject o = s.toJson();
    QCOMPARE(o.value("level").toString(), QStringLiteral("advanced"));
    QCOMPARE(o.value("estimateBeforeRun").toBool(true), false);
    QCOMPARE(o.value("fastForLongRecordings").toBool(false), true);
    QCOMPARE(o.value("deleteUploadedAudio").toBool(true), false);
    const cloud::CloudSettings back = cloud::CloudSettings::fromJson(o);
    QCOMPARE(back.level, QStringLiteral("advanced"));
    QCOMPARE(back.sttTier, QStringLiteral("fast"));
    QCOMPARE(back.llmTier, QStringLiteral("fast"));
    QVERIFY(!back.estimateBeforeRun);
    QVERIFY(back.fastForLongRecordings);
    QVERIFY(!back.deleteUploadedAudio);

    // An unknown level (e.g. a future "pro", which the client does not offer) reads as "simple".
    QCOMPARE(cloud::CloudSettings::fromJson(obj(R"({"level":"pro"})")).level, QStringLiteral("simple"));
}

void TestCloudTypes::cloudMode_resolve()
{
    cloud::CloudSettings s;
    s.baseUrl = QStringLiteral("https://gw.example");
    // Unset: the settings decide; teaser if compiled in.
    cloud::CloudMode m = cloud::CloudMode::resolve(s, QString(), QString(), true, true);
    QVERIFY(!m.live);
    QVERIFY(m.teaser);
    QCOMPARE(m.baseUrl, QStringLiteral("https://gw.example"));
    s.enabled = true;
    m = cloud::CloudMode::resolve(s, QString(), QString(), true, true);
    QVERIFY(m.live);
    QVERIFY(!m.teaser);
    // Not compiled in: never live.
    QVERIFY(!cloud::CloudMode::resolve(s, "live", QString(), false, true).live);
    // off wins over everything; live overrides the settings; the URL from the environment.
    m = cloud::CloudMode::resolve(s, " OFF ", QString(), true, true);
    QVERIFY(!m.live && !m.teaser);
    s.enabled = false;
    m = cloud::CloudMode::resolve(s, "live", " http://127.0.0.1:8300 ", true, false);
    QVERIFY(m.live);
    QCOMPARE(m.baseUrl, QStringLiteral("http://127.0.0.1:8300"));

    // S3: the route decides, not the selected id.
    providers::RouteSettings rs;
    rs.stt.route = providers::Route::Cloud;
    rs.llm.route = providers::Route::Ask;   // unresolved Ask: not the cloud
    QVERIFY(m.selects(providers::WorkflowStep::Transcribe, rs));
    QVERIFY(!m.selects(providers::WorkflowStep::Summarize, rs));
    QVERIFY(!m.selects(providers::WorkflowStep::Record, rs));
    QVERIFY(m.selects(providers::Route::Cloud));
    QVERIFY(!m.selects(providers::Route::External));
    QCOMPARE(providers::CloudProviderId, cloud::ProviderId);
    m.live = false;
    QVERIFY(!m.selects(providers::WorkflowStep::Transcribe, rs));
    QVERIFY(!m.selects(providers::Route::Cloud));
}

void TestCloudTypes::estimator_plan()
{
    using E = cloud::CloudEstimator;
    cloud::EstimateInputs in;
    in.task = QStringLiteral("transcribe");
    in.durationMs = 60000;
    in.tracks = 2;
    in.sttModel = QStringLiteral("tanara/stt-accurate");
    in.llmModel = QStringLiteral("tanara/summary-accurate");
    cloud::EstimateRequest r = E::plan(in);
    QCOMPARE(r.sttModel, in.sttModel);
    QVERIFY(r.llmModel.isEmpty());
    QVERIFY(r.llmCalls.isEmpty());
    QCOMPARE(r.tracks, 2);

    // Summarize without a transcript: statistics (mode only).
    in.task = QStringLiteral("summarize");
    r = E::plan(in);
    QVERIFY(r.sttModel.isEmpty());
    QCOMPARE(r.summaryMode, QStringLiteral("quick"));
    QCOMPARE(r.transcriptChars, -1);
    QVERIFY(r.llmCalls.isEmpty());

    // Quick, one call.
    in.hasTranscript = true;
    in.transcriptChars = 5000;
    in.contextNoteChars = 100;
    in.singleCall = true;
    in.parts = 1;
    in.singlePromptChars = 700;
    r = E::plan(in);
    QCOMPARE(r.transcriptChars, 5000);
    QCOMPARE(r.llmCalls.size(), 1);
    QCOMPARE(r.llmCalls[0].inputChars, 5700);
    QCOMPARE(r.llmCalls[0].maxTokens, E::kMaxTokens);

    // Quick, 3 parts, 1 cached: notes for the 2 missing + one merge.
    in.singleCall = false;
    in.parts = 3;
    in.cachedParts = 1;
    in.partChars = { 1000, 2000, 3001 };
    in.notesPromptChars = 400;
    in.mergePromptChars = 500;
    in.notesMaxTokens = 3000;
    r = E::plan(in);
    QCOMPARE(r.llmCalls.size(), 2);
    QCOMPARE(r.llmCalls[0].count, 2);
    QCOMPARE(r.llmCalls[0].inputChars, 2001 + 400 + 300);   // ceil(6001 / 3) + notes prompt + (note + 200)
    QCOMPARE(r.llmCalls[0].maxTokens, 3000);
    QCOMPARE(r.llmCalls[1].inputChars, 500 + 300 + 3 * E::kNotesCharsPerPart);

    // Complex, no topic list yet: topics + unknown analyses + reduce.
    in.summaryMode = QStringLiteral("complex");
    in.topicPromptChars = 50;
    in.analysisPromptChars = 60;
    r = E::plan(in);
    QCOMPARE(r.llmCalls.size(), 3);
    QCOMPARE(r.llmCalls[0].inputChars, 5050);
    QCOMPARE(r.llmCalls[1].count, -1);
    QCOMPARE(r.llmCalls[1].inputChars, 5260);
    QCOMPARE(r.llmCalls[2].inputChars, E::kReduceInputChars);
    // Edited topic list, 2 analyses missing.
    in.topicsKnown = true;
    in.missingAnalyses = 2;
    r = E::plan(in);
    QCOMPARE(r.llmCalls.size(), 2);
    QCOMPARE(r.llmCalls[0].count, 2);
    in.missingAnalyses = 0;
    QCOMPARE(E::plan(in).llmCalls.size(), 1);   // the reduce only

    // The ICostEstimating request maps 1:1.
    providers::CostEstimateRequest q;
    q.task = "both"; q.durationMs = 5; q.llmCalls = { { -1, 10, 20 } };
    const cloud::EstimateRequest m = E::toEstimateRequest(q);
    QCOMPARE(m.task, QStringLiteral("both"));
    QCOMPARE(m.llmCalls.value(0).count, -1);
    QCOMPARE(m.llmCalls.value(0).maxTokens, 20);
}

void TestCloudTypes::gateway_kinds()
{
    using K = providers::CapabilityKind;
    QCOMPARE(cloud::gatewayKindsFor(K::Transcription), QStringList{ "stt" });
    QCOMPARE(cloud::gatewayKindsFor(K::Summarization), QStringList{ "llm" });
    QCOMPARE(cloud::gatewayKindsFor(K::ParticipantIdentification), QStringList{ "voice_embedding" });
    QCOMPARE(cloud::gatewayKindsFor(K::TextEmbedding), QStringList{ "text_embedding" });
    QVERIFY(cloud::gatewayKindsFor(K::ModelCatalog).isEmpty());
}

#endif

QTEST_GUILESS_MAIN(TestCloudTypes)
#include "test_cloud_types.moc"
