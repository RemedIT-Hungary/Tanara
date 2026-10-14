#include "tanara/cloud/CloudRunLedger.h"
#include "tanara/cloud/CloudAccount.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>

namespace tanara::cloud {

void CloudRun::addCharge(const ChargeInfo& c)
{
    if (!c.valid) return;
    chargedMicros += c.charge.micros;
    if (currency.isEmpty()) currency = c.charge.currency;
    if (c.balance.isValid()) balance = c.balance;
    if (!c.vatMode.isEmpty()) vatMode = c.vatMode;
    ++calls;
}

void CloudRun::addCost(const providers::CostEvent& e)
{
    if (e.kind != providers::CostEvent::Kind::Charge) return;
    ChargeInfo c;
    c.valid = true;
    c.charge = Money{e.micros, e.currency};
    if (!e.balanceCurrency.isEmpty()) c.balance = Money{e.balanceMicros, e.balanceCurrency};
    c.vatMode = e.vatMode;
    addCharge(c);
}

providers::CostEvent toCostEvent(const ChargeInfo& c)
{
    providers::CostEvent e;
    e.kind = providers::CostEvent::Kind::Charge;
    e.micros = c.charge.micros;
    e.currency = c.charge.currency;
    if (c.balance.isValid()) {
        e.balanceMicros = c.balance.micros;
        e.balanceCurrency = c.balance.currency;
    }
    e.vatMode = c.vatMode;
    return e;
}

CloudRunLedger::CloudRunLedger(CloudAccount* account, QObject* parent)
    : QObject(parent), m_account(account)
{
}

CloudRunPtr CloudRunLedger::newRun(const QString& meetingId, const QString& kind) const
{
    auto run = std::make_shared<CloudRun>();
    run->meetingId = meetingId;
    run->kind = kind;
    run->jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return run;
}

providers::ProviderConfig CloudRunLedger::configFor(const QString& model, int maxTokens) const
{
    providers::ProviderConfig cfg;
    cfg.type = ProviderId;
    cfg.baseUrl = m_account ? m_account->apiBase() : QString();
    cfg.model = model;
    cfg.temperature = 0.2;
    cfg.maxTokens = maxTokens;
    return cfg;
}

providers::ProviderContext CloudRunLedger::contextFor(const CloudRunPtr& run, const QString& summaryMode,
                                                      net::HttpClient* http) const
{
    providers::ProviderContext ctx;
    ctx.http = http;
    QPointer<CloudAccount> acc = m_account;
    ctx.auth = providers::AuthSource::bearer([acc]() { return acc ? acc->apiKey() : QString(); });
    ctx.jobId = run ? run->jobId : QUuid::createUuid().toString(QUuid::WithoutBraces);
    ctx.language = acc ? acc->language() : QString();
    ctx.logCategory = QStringLiteral("tanara.cloud");
    if (acc) ctx.headers = acc->requestHeaders(ctx.jobId, summaryMode);
    if (run)
        ctx.cost = [run](const providers::CostEvent& e) { run->addCost(e); };
    const providers::CostSink cost = ctx.cost;
    ctx.observe = [acc, run, cost](const HttpExchange& ex) { observe(acc, run, ex, cost); };
    return ctx;
}

void CloudRunLedger::observe(CloudAccount* acc, const CloudRunPtr& run, const HttpExchange& ex,
                             const providers::CostSink& cost)
{
    if (acc) acc->observeExchange(ex);
    if (!run) return;
    const QString rid = QString::fromUtf8(ex.headers.value(QByteArrayLiteral("x-tanara-request-id")));
    if (!rid.isEmpty()) run->lastRequestId = rid;
    const bool ok = ex.status >= 200 && ex.status < 300;
    if (!ok) {
        if (ex.method != "DELETE")   // the failure of cleanup DELETEs must not overwrite the real error
            run->lastError = parseCloudError(ex.status, ex.headers, ex.body, ex.networkError);
        return;
    }
    auto book = [&cost, &run](const ChargeInfo& c) {
        if (!c.valid) return;
        if (cost) cost(toCostEvent(c));
        else run->addCharge(c);
    };
    const QJsonObject o = QJsonDocument::fromJson(ex.body).object();
    static const QRegularExpression statusPath(QStringLiteral("/transcriptions/[^/]+$"));
    if (ex.method == "POST" && ex.path.endsWith(QLatin1String("/files"))) {
        run->fileId = o.value(QStringLiteral("id")).toString();
    } else if (ex.method == "POST" && ex.path.endsWith(QLatin1String("/transcriptions"))) {
        run->transcriptionId = o.value(QStringLiteral("id")).toString();
        ChargeInfo c = chargeInfoFromJson(o.value(QStringLiteral("tanara")).toObject());
        if (!c.valid) c = chargeInfoFromHeaders(ex.headers);
        book(c);
        // The id of the transcription in progress (for the startup write-back check).
        if (acc) acc->addPendingTranscription(run->transcriptionId, run->fileId);
    } else if (ex.method == "POST" && ex.path.endsWith(QLatin1String("/chat/completions"))) {
        ChargeInfo c = chargeInfoFromJson(o.value(QStringLiteral("usage")).toObject()
                                              .value(QStringLiteral("tanara_charge")).toObject());
        if (!c.valid) c = chargeInfoFromHeaders(ex.headers);
        book(c);
    } else if (ex.method == "GET" && statusPath.match(ex.path).hasMatch()) {
        // The write-back shows in the response that first gives status: error; the provider sends the DELETE
        // ONLY after that (we read out the tanara field first).
        const TranscriptionJobInfo j = transcriptionJobInfoFromJson(o);
        if (j.refunded) {
            run->refunded = true;
            run->refund = j.refund;
            if (j.balance.isValid()) {
                run->balance = j.balance;
                if (acc) emit acc->balanceChanged(j.balance);
            }
        }
    }
}

void CloudRunLedger::finish(const CloudRunPtr& run)
{
    if (!run) return;
    if (!run->transcriptionId.isEmpty() && m_account)
        m_account->removePendingTranscription(run->transcriptionId);
    if (run->calls > 0)
        emit charged(run->meetingId, run->kind, run->charged(), run->calls, run->balance, run->vatMode);
    if ((run->balance.isValid() || run->calls > 0) && m_account)
        m_account->refreshAccount();   // refreshing low_balance / hours_left / notices
}

bool CloudRunLedger::fail(const CloudRunPtr& run)
{
    if (run && !run->transcriptionId.isEmpty() && m_account)
        m_account->removePendingTranscription(run->transcriptionId);
    if (run && run->refunded) {
        emit refunded(run->meetingId, run->refund, run->balance, run->lastRequestId);
        if (m_account) m_account->refreshAccount();
        return true;
    }
    if (run && run->lastError.isError()) {
        if (run->lastError.kind == CloudErrorKind::ClientTooOld && m_account)
            emit m_account->clientTooOldDetected(run->lastError.minClient);
        emit failed(run->meetingId, run->kind, run->lastError, run->charged());
        if (run->calls > 0 && m_account) m_account->refreshAccount();
        return true;
    }
    return false;
}

} // namespace tanara::cloud
