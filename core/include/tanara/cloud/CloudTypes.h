#pragma once
//
// Tanara Cloud — a gateway-szerződés (docs/cloud-gateway-api.yaml, 1.2.0) kliensoldali
// érték-típusai + a TISZTA (hálózat nélküli, unit-tesztelhető) feldolgozó / formázó függvények.
//
// Alapelv (szerződés): a kliens SOHA nem számol árat és nem vált nettó ↔ bruttó között.
// Minden összeg már a user vat_mode-ja szerint jön; a kliens csak megjeleníti és a
// jelölést („ÁFA-t tartalmaz” / „ÁFA nélkül, fordított adózás”) teszi mellé.
//
// Headless (core): nincs Widgets-függőség.
//
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

// ---- pénz ----------------------------------------------------------------------------
// Egész milliomod egység (1 USD = 1 000 000 micros). Negatív lehet (visszaterhelés után).
struct Money {
    qint64  micros = 0;
    QString currency;               // üres = érvénytelen / hiányzó
    bool isValid() const { return !currency.isEmpty(); }
};
Money moneyFromJson(const QJsonValue& v);

// Megjelenítési stílus (design-brief 7.1):
//  Balance  — egyenleg / összesítő: 2 tizedes, LEFELÉ kerekítve (sosem mutat többet)
//  Charge   — egyedi terhelés: ≥ $0,01 → 2 tizedes; $0,0001–$0,01 → 4 tizedes; < → „< $0,0001”
//  Estimate — becslés: mint a Charge, „≈ ” előtaggal a hívó dönt
enum class MoneyStyle { Balance, Charge };
// lang: "hu" → „$12,40” (tizedesvessző, ezres tagolás szóközzel), egyébként „$12.40”.
// Negatív: „−$0,42” (valódi mínuszjel). Nem-USD pénznem: „12,40 EUR”.
QString formatMoney(const Money& m, MoneyStyle style, const QString& lang);

// „≈ X óra” (brief 7.2): ≥ 10 óra → egész, lefelé; 1–10 → egy tizedes, lefelé; < 1 → perc.
QString formatHours(double hours, const QString& lang);

// Felvételi hossz: H:MM:SS (1 óra alatt M:SS).
QString formatDuration(qint64 ms);

// ÁFA-jelölés a user módja szerint (tr-rel fordítva).
QString vatLabel(const QString& vatMode);

// Szemantikus verzió-összevetés („0.1.4” < „0.2.0”). Hiányzó tagok = 0.
bool semverLess(const QString& a, const QString& b);

// A kliens platform-azonosítója a szerződés szerint: linux | windows | macos | unknown.
QString clientPlatform();
// „<semver>/<platform>” — az X-Tanara-Client fejléc értéke.
QString clientHeaderValue();

// ---- hibák ---------------------------------------------------------------------------
enum class CloudErrorKind {
    None,
    Network,            // a gateway nem érhető el (nincs HTTP-válasz) — nincs request id
    Unauthorized,       // 401 unauthorized — kulcs visszavonva / hiányzik
    InsufficientBalance,// 402 insufficient_balance
    TermsRequired,      // 403 terms_acceptance_required
    Suspended,          // 403 account_suspended
    ClientTooOld,       // 426 client_too_old
    RateLimited,        // 429 rate_limited
    SpendLimit,         // 429 spend_limit_reached
    Maintenance,        // 503 maintenance
    Upstream,           // 503 upstream_unavailable
    Internal,           // 500 internal_error
    Validation,         // 400 validation_error / egyéb 400
    DisposableEmail,    // 400 disposable_email (várólista)
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
    QString message;            // error.message (Accept-Language szerint)
    QString requestId;          // error.request_id vagy X-Tanara-Request-Id
    QString supportUrl;
    QString networkError;       // Network esetén a Qt hibaszöveg
    // kód-specifikus mezők
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
    QHash<QString, QString> fieldErrors;  // error.fields: mező → code

    bool isError() const { return kind != CloudErrorKind::None; }
};

// Egy HTTP-válaszból (status + fejlécek + törzs) CloudError. Sikeres (2xx) válaszra kind=None.
// A fejléc-kulcsok kisbetűsek. networkError nem üres → Network (nincs HTTP-válasz).
CloudError parseCloudError(int httpStatus, const QHash<QByteArray, QByteArray>& headers,
                           const QByteArray& body, const QString& networkError = QString());

// Ember-olvasható, többsoros leírás egy hibához (a CLI-nek; a szövegek a K-09…K-12 szerint).
// chargedSoFar > 0 → részleges hiba („Az eddig elkészült részek díja…”), különben — ha volt
// HTTP-válasz — „Nem terheltünk semmit.”. A végén a hibaazonosító, ha van.
QString describeCloudError(const CloudError& e, const Money& chargedSoFar, const QString& lang);

// ---- fiók ----------------------------------------------------------------------------
struct TermsStatus {
    QString acceptedVersion;       // üres = null
    QString currentVersion;
    QString currentUrl;
    bool    hasUpcoming = false;
    QString upcomingVersion;
    QDateTime upcomingEffectiveFrom;
    QString upcomingUrl;
    QString upcomingSummary;

    // Előzetes (14 napos) dialógus kell: van bejelentett verzió, és még nincs elfogadva.
    bool needsEarlyAcceptance() const { return hasUpcoming && acceptedVersion != upcomingVersion; }
    // Kötelező dialógus: a hatályos verzió nincs elfogadva (a feldolgozás 403-at kap).
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

// ---- katalógus -----------------------------------------------------------------------
struct CloudModel {
    QString id;
    QString kind;           // stt | llm
    QString tier;           // fast | accurate | "" (konkrét / Expert modell)
    bool    isVirtual = false;
    bool    expert = false;
    QString displayName;
    bool    diarization = true;     // csak STT
    bool    allLanguages = true;    // languages == null
    QStringList languages;
    QStringList hiddenForLanguages;
    Money   perHour;                // STT
    Money   perMInput, perMOutput;  // LLM (1M tokenre)

    // A modell nem ajánlott ehhez a nyelvhez (hidden_for_languages vagy nincs a languages-ben).
    bool notRecommendedFor(const QString& lang) const;
};
QVector<CloudModel> modelsFromJson(const QJsonObject& root);
// A kind + tier virtuális modellje a katalógusból (nincs → nullopt).
std::optional<CloudModel> findTierModel(const QVector<CloudModel>& models, const QString& kind,
                                        const QString& tier);
std::optional<CloudModel> findModel(const QVector<CloudModel>& models, const QString& id);
// A kind + tier virtuális modellnév (katalógus nélkül): tanara/stt-accurate, tanara/summary-fast …
QString virtualModelId(const QString& kind, const QString& tier);

// ---- becslés -------------------------------------------------------------------------
struct LlmCallGroup {
    int count = 1;          // < 0 → null (a gateway statisztikából becsül)
    int inputChars = 0;
    int maxTokens = 1;
};

struct EstimateRequest {
    QString task;           // transcribe | summarize | both
    qint64  durationMs = 0;
    int     tracks = 0;
    QString sttModel, llmModel;
    QString summaryMode;    // quick | complex | ""
    QString language;       // "" → nem küldjük
    int     transcriptChars = -1;   // < 0 → nem küldjük
    QVector<LlmCallGroup> llmCalls; // üres → nem küldjük
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

// A K-06 három egyenleg-állapota (kiegészítés-2 1.12):
//  Enough        balance ≥ required                    → indítható
//  LittleReserve estimate ≤ balance < required         → indítható, figyelmeztetéssel
//  NotEnough     balance < estimate                    → tiltva, feltöltés
enum class EstimateLevel { Enough, LittleReserve, NotEnough };
EstimateLevel estimateLevel(const EstimateResult& e);

// ---- terhelés / visszaírás -----------------------------------------------------------
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
// A X-Tanara-Charge-Micros / -Balance-Micros / -Currency fejlécekből (valid=false, ha nincsenek).
ChargeInfo chargeInfoFromHeaders(const QHash<QByteArray, QByteArray>& headers);

struct TranscriptionJobInfo {
    bool    valid = false;
    QString status;             // queued | processing | completed | error (a válasz status-a)
    bool    refunded = false;
    Money   charge, refund, balance;
    QString errorCode;
    bool    contentDeleted = false;
    QString jobId;
};
// A GET /v1/transcriptions/{id} teljes válaszából (status + tanara).
TranscriptionJobInfo transcriptionJobInfoFromJson(const QJsonObject& root);

// ---- device flow ---------------------------------------------------------------------
struct DeviceCode {
    QString deviceCode, userCode, verificationUri, verificationUriComplete;
    int interval = 5;
    int expiresIn = 600;
};
DeviceCode deviceCodeFromJson(const QJsonObject& o);

// ---- várólista (MKT-02) --------------------------------------------------------------
struct WaitlistSignup {
    QString email;
    bool    consent = false;
    QString uiLanguage;             // hu | en
    QString useCase;                // meetings | interviews | audio_files | other | "" (null)
    QStringList meetingLanguages;   // hu | en | other
    QString platform;               // automatikus (clientPlatform())
    QString clientVersion;          // automatikus (libraryVersion())
};
// A POST /v1/waitlist törzse (source=client). Csak a kitöltött opcionális mezők kerülnek bele.
QJsonObject waitlistToJson(const WaitlistSignup& s);
// Kliensoldali előellenőrzés (a gomb előtt): üres = rendben, különben a hibás mező neve
// ("email" | "consent"). A szerver a végső bíró (400 validation_error / disposable_email).
QString validateWaitlist(const WaitlistSignup& s);

// ---- HTTP-csere (a providerek gateway-hookjához) --------------------------------------
// Egy gateway-hívás eredménye: a cloud-réteg ebből olvassa a fejléceket (min-client,
// request id, terhelés), a hibát és az additív `tanara` mezőket.
struct HttpExchange {
    QByteArray method;
    QString    path;                       // pl. "/transcriptions" (a baseUrl utáni rész)
    int        status = 0;                 // 0 = nincs HTTP-válasz (hálózati hiba)
    QHash<QByteArray, QByteArray> headers; // kisbetűs kulcsok
    QByteArray body;
    QString    networkError;               // üres, ha volt HTTP-válasz
};
// Egy befejezett QNetworkReply-ból (a törzset a hívó már kiolvasta). path = az URL teljes
// útvonala (pl. "/v1/transcriptions/tr_1").
HttpExchange makeHttpExchange(QNetworkReply* reply, const QByteArray& body);

// ---- konfiguráció --------------------------------------------------------------------
namespace cloud {
inline const QString ProviderId     = QStringLiteral("tanara-cloud");
inline const QString ApiKeySecret   = QStringLiteral("tanara.cloud.apiKey");
inline const QString DefaultBaseUrl = QStringLiteral("https://api.tanara.remedit.hu");
// Adatkezelési tájékoztató (a szerződés nem adja; feltételezés — lásd handover).
inline const QString PrivacyUrl     = QStringLiteral("https://app.tanara.remedit.hu/legal/privacy");

// Fordítási kapcsolók (CMake: TANARA_BUILD_CLOUD, TANARA_CLOUD_TEASER).
bool clientCompiled();   // a bejelentkezős cloud-kliens befordult
bool teaserCompiled();   // a „Hamarosan” panel befordult
} // namespace cloud

} // namespace tanara

Q_DECLARE_METATYPE(tanara::Money)
Q_DECLARE_METATYPE(tanara::CloudError)
Q_DECLARE_METATYPE(tanara::AccountInfo)
Q_DECLARE_METATYPE(tanara::EstimateResult)
Q_DECLARE_METATYPE(tanara::ChargeInfo)
Q_DECLARE_METATYPE(tanara::DeviceCode)
Q_DECLARE_METATYPE(tanara::TermsStatus)
