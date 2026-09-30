#include "tanara/cloud/CloudAccount.h"
#include "tanara/store/KeyStore.h"
#include "tanara/Types.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

namespace tanara {

Q_LOGGING_CATEGORY(lcCloud, "tanara.cloud")

HttpExchange makeHttpExchange(QNetworkReply* reply, const QByteArray& body)
{
    HttpExchange ex;
    if (!reply) return ex;
    switch (reply->operation()) {
    case QNetworkAccessManager::GetOperation:    ex.method = QByteArrayLiteral("GET"); break;
    case QNetworkAccessManager::PostOperation:   ex.method = QByteArrayLiteral("POST"); break;
    case QNetworkAccessManager::DeleteOperation: ex.method = QByteArrayLiteral("DELETE"); break;
    case QNetworkAccessManager::PutOperation:    ex.method = QByteArrayLiteral("PUT"); break;
    default: ex.method = reply->request().attribute(QNetworkRequest::CustomVerbAttribute).toByteArray(); break;
    }
    ex.path   = reply->request().url().path();
    ex.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    for (const auto& pair : reply->rawHeaderPairs())
        ex.headers.insert(pair.first.toLower(), pair.second);
    ex.body = body;
    if (ex.status == 0)
        ex.networkError = reply->errorString().isEmpty() ? QStringLiteral("network error") : reply->errorString();
    return ex;
}

namespace {
QString ts(const QDateTime& d) { return d.isValid() ? d.toString(Qt::ISODateWithMs) : QString(); }
QDateTime fromTs(const QJsonValue& v) { return v.isString() ? QDateTime::fromString(v.toString(), Qt::ISODateWithMs) : QDateTime(); }
} // namespace

CloudAccount::CloudAccount(KeyStore* keys, const QString& stateDir, QObject* parent)
    : QObject(parent)
    , m_keys(keys)
    , m_statePath(QDir(stateDir).filePath(QStringLiteral("cloud-state.json")))
    , m_baseUrl(cloud::DefaultBaseUrl)
    , m_nam(new QNetworkAccessManager(this))
    , m_pollTimer(new QTimer(this))
{
    m_pollTimer->setSingleShot(true);
    connect(m_pollTimer, &QTimer::timeout, this, &CloudAccount::pollDeviceToken);
    loadState();
}

CloudAccount::~CloudAccount() = default;

void CloudAccount::setBaseUrl(const QString& url)
{
    QString u = url.trimmed();
    while (u.endsWith(QLatin1Char('/'))) u.chop(1);
    if (u.endsWith(QLatin1String("/v1"))) u.chop(3);
    m_baseUrl = u.isEmpty() ? cloud::DefaultBaseUrl : u;
}

bool CloudAccount::isLoggedIn() const { return !apiKey().isEmpty(); }
QString CloudAccount::apiKey() const { return m_keys ? m_keys->get(cloud::ApiKeySecret) : QString(); }

QList<QPair<QByteArray, QByteArray>> CloudAccount::requestHeaders(const QString& jobId,
                                                                   const QString& summaryMode) const
{
    QList<QPair<QByteArray, QByteArray>> h;
    h.append({ QByteArrayLiteral("X-Tanara-Client"), clientHeaderValue().toUtf8() });
    h.append({ QByteArrayLiteral("Accept-Language"), m_lang.toUtf8() });
    if (!jobId.isEmpty())       h.append({ QByteArrayLiteral("X-Tanara-Job-Id"), jobId.toUtf8() });
    if (!summaryMode.isEmpty()) h.append({ QByteArrayLiteral("X-Tanara-Summary-Mode"), summaryMode.toUtf8() });
    return h;
}

void CloudAccount::observeExchange(const HttpExchange& ex)
{
    if (ex.status == 0) return;
    // Min-client: minden válaszon ott van; ha a saját verziónk régebbi → cloud tiltva (K-10).
    const QString minC = QString::fromUtf8(ex.headers.value(QByteArrayLiteral("x-tanara-min-client")));
    bool tooOld = !minC.isEmpty() && semverLess(libraryVersion(), minC);
    if (ex.status == 426) {
        const CloudError e = parseCloudError(ex.status, ex.headers, ex.body);
        tooOld = true;
        if (!e.downloadUrl.isEmpty()) m_downloadUrl = e.downloadUrl;
        if (!e.minClient.isEmpty()) m_minClient = e.minClient;
    }
    if (!minC.isEmpty()) m_minClient = tooOld ? minC : m_minClient;
    if (tooOld && !m_tooOld) {
        m_tooOld = true;
        if (m_minClient.isEmpty()) m_minClient = minC;
        qCWarning(lcCloud).noquote() << "A kliens túl régi a gatewayhez; minimum:" << m_minClient;
        emit clientTooOldDetected(m_minClient);
    } else if (!tooOld && m_tooOld && !minC.isEmpty()) {
        m_tooOld = false;   // a gateway lejjebb vette a minimumot
    }

    // Egyenleg a terhelő hívások fejlécéből → a chip azonnal frissül.
    const ChargeInfo c = chargeInfoFromHeaders(ex.headers);
    if (c.valid && c.balance.isValid()) {
        m_account.balance = c.balance;
        m_account.balanceEmpty = c.balance.micros <= 0;
        emit balanceChanged(c.balance);
    }
}

void CloudAccount::send(const QByteArray& method, const QString& path, const QJsonObject* body,
                        bool authenticated, Done done)
{
    QNetworkRequest req(QUrl(apiBase() + path));
    req.setTransferTimeout(30000);
    for (const auto& h : requestHeaders())
        req.setRawHeader(h.first, h.second);
    if (authenticated) {
        const QString key = apiKey();
        if (!key.isEmpty())
            req.setRawHeader(QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + key.toUtf8());
    }
    QNetworkReply* reply = nullptr;
    if (body) {
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        reply = m_nam->sendCustomRequest(req, method, QJsonDocument(*body).toJson(QJsonDocument::Compact));
    } else if (method == "GET") {
        reply = m_nam->get(req);
    } else if (method == "DELETE") {
        reply = m_nam->deleteResource(req);
    } else {
        reply = m_nam->sendCustomRequest(req, method, QByteArray());
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply, done = std::move(done)]() {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        HttpExchange ex = makeHttpExchange(reply, data);
        if (ex.method.isEmpty()) ex.method = QByteArrayLiteral("POST");
        qCDebug(lcCloud).noquote() << ex.method << ex.path << "→" << ex.status
                                   << QString::fromUtf8(ex.headers.value(QByteArrayLiteral("x-tanara-request-id")));
        observeExchange(ex);
        if (done) done(ex);
    });
}

// ---- device flow ---------------------------------------------------------------------

void CloudAccount::startDeviceFlow()
{
    m_pollTimer->stop();
    m_deviceActive = false;
    QJsonObject body;
    body.insert(QStringLiteral("device_name"), QSysInfo::machineHostName());
    body.insert(QStringLiteral("platform"), clientPlatform());
    body.insert(QStringLiteral("client_version"), libraryVersion());
    send("POST", QStringLiteral("/auth/device/code"), &body, false, [this](const HttpExchange& ex) {
        const CloudError e = parseCloudError(ex.status, ex.headers, ex.body, ex.networkError);
        if (e.isError()) { emit deviceFlowFailed(e); return; }
        m_device = deviceCodeFromJson(QJsonDocument::fromJson(ex.body).object());
        if (m_device.deviceCode.isEmpty()) {
            CloudError bad; bad.kind = CloudErrorKind::Other; bad.httpStatus = ex.status;
            emit deviceFlowFailed(bad);
            return;
        }
        m_deviceActive = true;
        m_deviceExpires = QDateTime::currentDateTimeUtc().addSecs(m_device.expiresIn);
        emit deviceCodeReady(m_device);
        m_pollTimer->start(m_device.interval * 1000);
    });
}

void CloudAccount::pollDeviceToken()
{
    if (!m_deviceActive) return;
    if (QDateTime::currentDateTimeUtc() > m_deviceExpires) {
        m_deviceActive = false;
        CloudError e; e.kind = CloudErrorKind::DeviceFlow; e.code = QStringLiteral("expired_token"); e.httpStatus = 400;
        emit deviceFlowFailed(e);
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("device_code"), m_device.deviceCode);
    const QString code = m_device.deviceCode;
    send("POST", QStringLiteral("/auth/device/token"), &body, false, [this, code](const HttpExchange& ex) {
        if (!m_deviceActive || code != m_device.deviceCode) return;   // közben Mégse / új kód
        const CloudError e = parseCloudError(ex.status, ex.headers, ex.body, ex.networkError);
        if (!e.isError()) {
            const QJsonObject o = QJsonDocument::fromJson(ex.body).object();
            const QString key = o.value(QStringLiteral("api_key")).toString();
            if (key.isEmpty()) { m_pollTimer->start(m_device.interval * 1000); return; }
            m_deviceActive = false;
            if (m_keys) m_keys->set(cloud::ApiKeySecret, key);
            m_email = o.value(QStringLiteral("email")).toString();
            saveState();
            emit deviceFlowSucceeded(m_email);
            emit loggedIn();
            refreshAccount();
            fetchModels();
            return;
        }
        if (e.code == QLatin1String("authorization_pending")) {
            m_pollTimer->start(m_device.interval * 1000);
        } else if (e.kind == CloudErrorKind::Network || e.kind == CloudErrorKind::Upstream
                   || e.kind == CloudErrorKind::Maintenance || e.kind == CloudErrorKind::RateLimited) {
            // Átmeneti hiba: a szerződés szerint a kliens tovább pollozik (retry_after után).
            m_pollTimer->start(qMax(m_device.interval, e.retryAfterSec > 0 ? qMin(e.retryAfterSec, 60) : 0) * 1000);
        } else {
            m_deviceActive = false;
            emit deviceFlowFailed(e);
        }
    });
}

void CloudAccount::cancelDeviceFlow()
{
    m_pollTimer->stop();
    if (!m_deviceActive) return;
    m_deviceActive = false;
    QJsonObject body;
    body.insert(QStringLiteral("device_code"), m_device.deviceCode);
    send("POST", QStringLiteral("/auth/device/cancel"), &body, false, nullptr);   // 204, idempotens
}

void CloudAccount::setManualApiKey(const QString& key, const QString& email)
{
    if (m_keys) m_keys->set(cloud::ApiKeySecret, key.trimmed());
    if (!email.isEmpty()) m_email = email;
    saveState();
    emit loggedIn();
}

void CloudAccount::logout()
{
    auto clearLocal = [this]() {
        if (m_keys) m_keys->remove(cloud::ApiKeySecret);
        m_email.clear();
        m_account = AccountInfo{};
        m_accountRaw = QJsonObject();
        m_accountAt = QDateTime();
        saveState();
        emit loggedOut();
    };
    if (!isLoggedIn()) { clearLocal(); return; }
    // A helyi kulcs akkor is törlődik, ha a hívás sikertelen (K-02).
    send("POST", QStringLiteral("/auth/logout"), nullptr, true, [clearLocal](const HttpExchange&) { clearLocal(); });
}

// ---- fiók / katalógus ----------------------------------------------------------------

void CloudAccount::refreshAccount()
{
    if (!isLoggedIn()) return;
    send("GET", QStringLiteral("/account"), nullptr, true, [this](const HttpExchange& ex) {
        const CloudError e = parseCloudError(ex.status, ex.headers, ex.body, ex.networkError);
        if (e.isError()) { emit accountFailed(e); return; }
        m_accountRaw = QJsonDocument::fromJson(ex.body).object();
        m_account = accountFromJson(m_accountRaw);
        m_accountAt = QDateTime::currentDateTime();
        if (!m_account.email.isEmpty()) m_email = m_account.email;
        saveState();
        emit accountUpdated(m_account);
        emit balanceChanged(m_account.balance);
    });
}

void CloudAccount::fetchModels()
{
    if (!isLoggedIn()) return;
    send("GET", QStringLiteral("/models"), nullptr, true, [this](const HttpExchange& ex) {
        const CloudError e = parseCloudError(ex.status, ex.headers, ex.body, ex.networkError);
        if (e.isError()) { emit modelsFailed(e); return; }
        m_modelsRaw = QJsonDocument::fromJson(ex.body).object();
        m_models = modelsFromJson(m_modelsRaw);
        saveState();
        emit modelsUpdated();
    });
}

void CloudAccount::estimate(const EstimateRequest& req,
                            std::function<void(const EstimateResult&, const CloudError&)> done)
{
    const QJsonObject body = estimateRequestToJson(req);
    send("POST", QStringLiteral("/estimate"), &body, true, [done = std::move(done)](const HttpExchange& ex) {
        const CloudError e = parseCloudError(ex.status, ex.headers, ex.body, ex.networkError);
        EstimateResult r;
        if (!e.isError()) r = estimateFromJson(QJsonDocument::fromJson(ex.body).object());
        if (done) done(r, e);
    });
}

void CloudAccount::acceptTerms(const QString& version)
{
    QJsonObject body;
    body.insert(QStringLiteral("version"), version);
    send("POST", QStringLiteral("/account/terms-acceptance"), &body, true, [this](const HttpExchange& ex) {
        const CloudError e = parseCloudError(ex.status, ex.headers, ex.body, ex.networkError);
        if (e.isError()) { emit termsFailed(e); return; }
        const TermsStatus t = termsFromJson(QJsonDocument::fromJson(ex.body).object().value(QStringLiteral("terms")).toObject());
        m_account.terms = t;
        m_termsPostponed = QDateTime();
        saveState();
        emit termsAccepted(t);
        refreshAccount();
    });
}

void CloudAccount::createTopupLink()
{
    send("POST", QStringLiteral("/account/topup-link"), nullptr, true, [this](const HttpExchange& ex) {
        CloudError e = parseCloudError(ex.status, ex.headers, ex.body, ex.networkError);
        if (e.isError()) {
            if (e.contactUrl.isEmpty()) e.contactUrl = m_account.contactUrl;
            emit topupFailed(e);
            return;
        }
        emit topupLinkReady(QJsonDocument::fromJson(ex.body).object().value(QStringLiteral("url")).toString());
    });
}

void CloudAccount::joinWaitlist(const WaitlistSignup& signup)
{
    const QJsonObject body = waitlistToJson(signup);
    const QString email = signup.email.trimmed();
    send("POST", QStringLiteral("/waitlist"), &body, false, [this, email](const HttpExchange& ex) {
        const CloudError e = parseCloudError(ex.status, ex.headers, ex.body, ex.networkError);
        if (e.isError()) { emit waitlistFailed(e); return; }
        emit waitlistJoined(email);
    });
}

// ---- függő átírások ------------------------------------------------------------------

void CloudAccount::addPendingTranscription(const QString& transcriptionId, const QString& fileId)
{
    if (transcriptionId.isEmpty()) return;
    m_pending.insert(transcriptionId, fileId);
    saveState();
}

void CloudAccount::removePendingTranscription(const QString& transcriptionId)
{
    if (m_pending.remove(transcriptionId) > 0) saveState();
}

void CloudAccount::checkPendingTranscriptions()
{
    if (!isLoggedIn()) return;
    const auto pending = m_pending;
    for (auto it = pending.constBegin(); it != pending.constEnd(); ++it) {
        const QString trId = it.key(), fileId = it.value();
        send("GET", QStringLiteral("/transcriptions/") + trId, nullptr, true,
             [this, trId, fileId](const HttpExchange& ex) {
            if (ex.status == 404) { removePendingTranscription(trId); return; }
            if (ex.status != 200) return;   // később újra
            const TranscriptionJobInfo j = transcriptionJobInfoFromJson(QJsonDocument::fromJson(ex.body).object());
            if (j.status == QLatin1String("queued") || j.status == QLatin1String("processing"))
                return;   // még fut — a következő indításkor újra megnézzük
            if (j.refunded) {
                if (j.balance.isValid()) { m_account.balance = j.balance; emit balanceChanged(j.balance); }
                emit previousTranscriptionRefunded(j.refund, j.balance);
            }
            // A tanara-mezőt kiolvastuk → az upstream tartalom takarítása (idempotens).
            send("DELETE", QStringLiteral("/transcriptions/") + trId, nullptr, true, nullptr);
            if (!fileId.isEmpty()) send("DELETE", QStringLiteral("/files/") + fileId, nullptr, true, nullptr);
            removePendingTranscription(trId);
        });
    }
}

// ---- helyi állapot -------------------------------------------------------------------

void CloudAccount::postponeTerms() { m_termsPostponed = QDateTime::currentDateTime(); saveState(); }
void CloudAccount::dismissLowBanner() { m_lowDismissed = QDateTime::currentDateTime(); saveState(); }
void CloudAccount::dismissNotice(const QString& id)
{
    if (!m_dismissedNotices.contains(id)) { m_dismissedNotices << id; saveState(); }
}

void CloudAccount::loadState()
{
    QFile f(m_statePath);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    m_email          = o.value(QStringLiteral("email")).toString();
    m_accountRaw     = o.value(QStringLiteral("account")).toObject();
    if (!m_accountRaw.isEmpty()) m_account = accountFromJson(m_accountRaw);
    m_accountAt      = fromTs(o.value(QStringLiteral("accountFetchedAt")));
    m_modelsRaw      = o.value(QStringLiteral("models")).toObject();
    if (!m_modelsRaw.isEmpty()) m_models = modelsFromJson(m_modelsRaw);
    const QJsonObject p = o.value(QStringLiteral("pendingTranscriptions")).toObject();
    for (auto it = p.constBegin(); it != p.constEnd(); ++it) m_pending.insert(it.key(), it.value().toString());
    m_termsPostponed = fromTs(o.value(QStringLiteral("termsPostponedAt")));
    m_lowDismissed   = fromTs(o.value(QStringLiteral("lowBannerDismissedAt")));
    for (const QJsonValue& v : o.value(QStringLiteral("dismissedNotices")).toArray())
        m_dismissedNotices << v.toString();
}

void CloudAccount::saveState() const
{
    QJsonObject o;
    o.insert(QStringLiteral("email"), m_email);
    o.insert(QStringLiteral("account"), m_accountRaw);
    o.insert(QStringLiteral("accountFetchedAt"), ts(m_accountAt));
    o.insert(QStringLiteral("models"), m_modelsRaw);
    QJsonObject p;
    for (auto it = m_pending.constBegin(); it != m_pending.constEnd(); ++it) p.insert(it.key(), it.value());
    o.insert(QStringLiteral("pendingTranscriptions"), p);
    o.insert(QStringLiteral("termsPostponedAt"), ts(m_termsPostponed));
    o.insert(QStringLiteral("lowBannerDismissedAt"), ts(m_lowDismissed));
    o.insert(QStringLiteral("dismissedNotices"), QJsonArray::fromStringList(m_dismissedNotices));
    QDir().mkpath(QFileInfo(m_statePath).absolutePath());
    QSaveFile f(m_statePath);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    f.commit();
}

} // namespace tanara
