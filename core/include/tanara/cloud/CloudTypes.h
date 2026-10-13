#pragma once
//
// Tanara Cloud: client-side value types of the gateway contract (docs/cloud-gateway-api.yaml,
// 1.2.0) + the PURE (network-free, unit-testable) parsing / formatting functions.
//
// Principle (contract): the client NEVER computes prices and never converts between net and
// gross. Every amount already comes in the user's vat_mode; the client only displays it and adds
// the label ("VAT included" / "VAT excluded, reverse charge").
//
// Headless (core): no Widgets dependency.
#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

class QNetworkReply;

namespace tanara {

// ---- money ----------------------------------------------------------------------------
// Whole millionths of a unit (1 USD = 1,000,000 micros). May be negative (after a refund).
struct Money {
    qint64  micros = 0;
    QString currency;               // empty = invalid / missing
    bool isValid() const { return !currency.isEmpty(); }
};
Money moneyFromJson(const QJsonValue& v);

// Display style (design brief 7.1):
//  Balance  - balance / total: 2 decimals, rounded DOWN (never shows more)
//  Charge   - an individual charge: >= $0.01 -> 2 decimals; $0.0001-$0.01 -> 4 decimals; below that "< $0.0001"
//  Estimate - an estimate: like Charge, with an "~ " prefix (the caller decides)
enum class MoneyStyle { Balance, Charge };
// lang: "hu" -> "$12,40" (decimal comma, thousands separated by a space), otherwise "$12.40".
// Negative: "-$0.42" (a real minus sign). A non-USD currency: "12.40 EUR".
QString formatMoney(const Money& m, MoneyStyle style, const QString& lang);

// "~ X hours" (brief 7.2): >= 10 hours -> whole, rounded down; 1-10 -> one decimal, rounded down; < 1 -> minutes.
QString formatHours(double hours, const QString& lang);

// Recording length: H:MM:SS (M:SS under 1 hour).
QString formatDuration(qint64 ms);

// VAT label per the user's mode (translated with tr).
QString vatLabel(const QString& vatMode);

// Semantic version comparison ("0.1.4" < "0.2.0"). Missing parts = 0.
bool semverLess(const QString& a, const QString& b);

// The client's platform id per the contract: linux | windows | macos | unknown.
QString clientPlatform();
// "<semver>/<platform>": the value of the X-Tanara-Client header.
QString clientHeaderValue();

// ---- errors ---------------------------------------------------------------------------
enum class CloudErrorKind {
    None,
    Network,            // the gateway is unreachable (no HTTP response); no request id
    Unauthorized,       // 401 unauthorized: key revoked / missing
    InsufficientBalance,// 402 insufficient_balance
    TermsRequired,      // 403 terms_acceptance_required
    Suspended,          // 403 account_suspended
    ClientTooOld,       // 426 client_too_old
    RateLimited,        // 429 rate_limited
    SpendLimit,         // 429 spend_limit_reached
    Maintenance,        // 503 maintenance
    Upstream,           // 503 upstream_unavailable
    Internal,           // 500 internal_error
    Validation,         // 400 validation_error / other 400
    DisposableEmail,    // 400 disposable_email (waitlist)
    NotFound,           // 404
    TopupUnavailable,   // 409 topup_unavailable
    TopupLimit,         // 422 topup_limit_exceeded
    DeviceFlow,         // 400 authorization_pending / access_denied / expired_token / invalid_device_code
    Other,
};

struct CloudError {
    CloudErrorKind kind = CloudErrorKind::None;
    int     httpStatus = 0;
    QString code;               // error.code
    QString message;            // error.message (per Accept-Language)
    QString requestId;          // error.request_id or X-Tanara-Request-Id
    QString supportUrl;
    QString networkError;       // for Network: the Qt error text
    // code-specific fields
    QString suspensionReason;   // admin | payment_dispute
    QString termsVersion, termsUrl;
    Money   balance, needed;
    QString neededBasis;        // charge | hold
    QString topupUrl, contactUrl;
    QString minClient, downloadUrl;
    int     retryAfterSec = 0;
    QString limitPeriod;        // daily | monthly
    Money   limitAmount;
    QDateTime limitResetsAt;
    QString settingsUrl;
    QDateTime windowStart, windowEnd;   // maintenance
    QHash<QString, QString> fieldErrors;  // error.fields: field -> code

    bool isError() const { return kind != CloudErrorKind::None; }
};

// CloudError from an HTTP response (status + headers + body). For a successful (2xx) response kind=None.
// Header keys are lowercase. A non-empty networkError -> Network (no HTTP response).
CloudError parseCloudError(int httpStatus, const QHash<QByteArray, QByteArray>& headers,
                           const QByteArray& body, const QString& networkError = QString());

// Human-readable, multi-line description of an error (for the CLI; the texts follow the cloud
// error-handling spec). chargedSoFar > 0 -> a partial failure ("the parts completed so far are
// charged..."), otherwise, if there was an HTTP response, "Nothing was charged.". At the end the error id, if any.
QString describeCloudError(const CloudError& e, const Money& chargedSoFar, const QString& lang);

// ---- account ----------------------------------------------------------------------------
struct TermsStatus {
    QString acceptedVersion;       // empty = null
    QString currentVersion;
    QString currentUrl;
    bool    hasUpcoming = false;
    QString upcomingVersion;
    QDateTime upcomingEffectiveFrom;
    QString upcomingUrl;
    QString upcomingSummary;

    // A preview (14-day) dialog is needed: there is an announced version and it is not accepted yet.
    bool needsEarlyAcceptance() const { return hasUpcoming && acceptedVersion != upcomingVersion; }
    // A mandatory dialog: the effective version is not accepted (processing gets a 403).
    bool needsAcceptance() const { return !currentVersion.isEmpty() && acceptedVersion != currentVersion; }
};
TermsStatus termsFromJson(const QJsonObject& o);

struct Notice {
    QString id;
    QString level;      // info | warning | critical
    QString message;
    QString url;
};

struct AccountInfo {
    bool    valid = false;
    QString email;
    Money   balance;
    QString vatMode;                // gross | reverse_charge
    Money   lowBalanceThreshold;
    bool    lowBalance = false;
    QDateTime lowBalanceSince;
    bool    balanceEmpty = false;
    double  hoursFast = 0.0;
    double  hoursAccurate = 0.0;
    bool    topupAvailable = false;
    QString contactUrl, dashboardUrl, usageUrl;
    QString trial;
    TermsStatus terms;
    QVector<Notice> notices;
};
AccountInfo accountFromJson(const QJsonObject& o);

// ---- catalog -----------------------------------------------------------------------
struct CloudModel {
    QString id;
    QString kind;           // stt | llm
    QString tier;           // fast | accurate | "" (a concrete / Expert model)
    bool    isVirtual = false;
    bool    expert = false;
    QString displayName;
    bool    diarization = true;     // STT only
    bool    allLanguages = true;    // languages == null
    QStringList languages;
    QStringList hiddenForLanguages;
    Money   perHour;                // STT
    Money   perMInput, perMOutput;  // LLM (1M tokenre)

    // The model is not recommended for this language (hidden_for_languages, or not in languages).
    bool notRecommendedFor(const QString& lang) const;
};
QVector<CloudModel> modelsFromJson(const QJsonObject& root);
// The virtual model of kind + tier from the catalog (none -> nullopt).
std::optional<CloudModel> findTierModel(const QVector<CloudModel>& models, const QString& kind,
                                        const QString& tier);
std::optional<CloudModel> findModel(const QVector<CloudModel>& models, const QString& id);
// The virtual model name of kind + tier (without a catalog): tanara/stt-accurate, tanara/summary-fast ...
QString virtualModelId(const QString& kind, const QString& tier);

// ---- estimate -------------------------------------------------------------------------
struct LlmCallGroup {
    int count = 1;          // < 0 -> null (the gateway estimates from statistics)
    int inputChars = 0;
    int maxTokens = 1;
};

struct EstimateRequest {
    QString task;           // transcribe | summarize | both
    qint64  durationMs = 0;
    int     tracks = 0;
    QString sttModel, llmModel;
    QString summaryMode;    // quick | complex | ""
    QString language;       // "" -> not sent
    int     transcriptChars = -1;   // < 0 -> not sent
    QVector<LlmCallGroup> llmCalls; // empty -> not sent
};
QJsonObject estimateRequestToJson(const EstimateRequest& r);

struct EstimateLine {
    QString item;           // stt | llm
    QString model;
    QString tier;
    QString summaryMode;
    Money   amount;
    bool    exact = false;
};

struct EstimateResult {
    bool  valid = false;
    Money estimate, low, high, balance, required, balanceAfter;
    bool  enough = false;
    bool  lowBalanceAfter = false;
    QString vatMode;
    QVector<EstimateLine> breakdown;
};
EstimateResult estimateFromJson(const QJsonObject& o);

// The three balance states of the pre-run check:
//  Enough        balance >= required                    -> can start
//  LittleReserve estimate <= balance < required         -> can start, with a warning
//  NotEnough     balance < estimate                     -> blocked, top-up
enum class EstimateLevel { Enough, LittleReserve, NotEnough };
EstimateLevel estimateLevel(const EstimateResult& e);

// ---- charge / write-back -----------------------------------------------------------
struct ChargeInfo {
    bool    valid = false;
    QString chargeId;
    Money   charge, balance;
    bool    lowBalance = false;
    bool    balanceEmpty = false;
    QString vatMode;
    QString jobId;
};
ChargeInfo chargeInfoFromJson(const QJsonObject& o);
// From the X-Tanara-Charge-Micros / -Balance-Micros / -Currency headers (valid=false if absent).
ChargeInfo chargeInfoFromHeaders(const QHash<QByteArray, QByteArray>& headers);

struct TranscriptionJobInfo {
    bool    valid = false;
    QString status;             // queued | processing | completed | error (the response's status)
    bool    refunded = false;
    Money   charge, refund, balance;
    QString errorCode;
    bool    contentDeleted = false;
    QString jobId;
};
// From the full response of GET /v1/transcriptions/{id} (status + tanara).
TranscriptionJobInfo transcriptionJobInfoFromJson(const QJsonObject& root);

// ---- device flow ---------------------------------------------------------------------
struct DeviceCode {
    QString deviceCode, userCode, verificationUri, verificationUriComplete;
    int interval = 5;
    int expiresIn = 600;
};
DeviceCode deviceCodeFromJson(const QJsonObject& o);

// ---- waitlist ---------------------------------------------------------------------
struct WaitlistSignup {
    QString email;
    bool    consent = false;
    QString uiLanguage;             // hu | en
    QString useCase;                // meetings | interviews | audio_files | other | "" (null)
    QStringList meetingLanguages;   // hu | en | other
    QString platform;               // automatic (clientPlatform())
    QString clientVersion;          // automatic (libraryVersion())
};
// Body of POST /v1/waitlist (source=client). Only the filled optional fields are included.
QJsonObject waitlistToJson(const WaitlistSignup& s);
// Client-side pre-check (before the button): empty = fine, otherwise the name of the invalid
// field ("email" | "consent"). The server is the final judge (400 validation_error / disposable_email).
QString validateWaitlist(const WaitlistSignup& s);

// ---- HTTP exchange (for the providers' gateway hook) --------------------------------------
// Result of one gateway call: the cloud layer reads from it the headers (min-client, request id,
// charge), the error and the additive `tanara` fields.
struct HttpExchange {
    QByteArray method;
    QString    path;                       // e.g. "/transcriptions" (the part after baseUrl)
    int        status = 0;                 // 0 = no HTTP response (network error)
    QHash<QByteArray, QByteArray> headers; // lowercase keys
    QByteArray body;
    QString    networkError;               // empty if there was an HTTP response
};
// From a finished QNetworkReply (the caller already read the body). path = the URL's full
// path (e.g. "/v1/transcriptions/tr_1").
HttpExchange makeHttpExchange(QNetworkReply* reply, const QByteArray& body);

// ---- configuration --------------------------------------------------------------------
namespace cloud {
inline const QString ProviderId     = QStringLiteral("tanara-cloud");
inline const QString ApiKeySecret   = QStringLiteral("tanara.cloud.apiKey");
inline const QString DefaultBaseUrl = QStringLiteral("https://api.tanara.remedit.hu");
// Privacy notice (the contract does not give it; an assumption, see the handover).
inline const QString PrivacyUrl     = QStringLiteral("https://app.tanara.remedit.hu/legal/privacy");

// Compile switches (CMake: TANARA_BUILD_CLOUD, TANARA_CLOUD_TEASER).
bool clientCompiled();   // the sign-in cloud client is compiled in
bool teaserCompiled();   // the "Coming soon" panel is compiled in
} // namespace cloud

} // namespace tanara

Q_DECLARE_METATYPE(tanara::Money)
Q_DECLARE_METATYPE(tanara::CloudError)
Q_DECLARE_METATYPE(tanara::AccountInfo)
Q_DECLARE_METATYPE(tanara::EstimateResult)
Q_DECLARE_METATYPE(tanara::ChargeInfo)
Q_DECLARE_METATYPE(tanara::DeviceCode)
Q_DECLARE_METATYPE(tanara::TermsStatus)
