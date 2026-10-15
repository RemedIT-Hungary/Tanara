#include "tanara/providers/chat/LlmServer.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/foundation/error/Error.h"
#include "tanara/foundation/net/HttpClient.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QDebug>

namespace tanara::providers {

using namespace llmctx;

namespace {

// A short per-address cache (we live on the main thread: no lock).
struct ProbeCacheEntry { qint64 atMs = 0; QByteArray body; };
QHash<QString, ProbeCacheEntry>& probeCache()
{
    static QHash<QString, ProbeCacheEntry> cache;
    return cache;
}

void authorize(QNetworkRequest& req, const QString& key)
{
    if (!key.trimmed().isEmpty())
        req.setRawHeader(QByteArrayLiteral("Authorization"),
                         QByteArrayLiteral("Bearer ") + key.trimmed().toUtf8());
}

QString groupedTokens(int n) { return QStringLiteral("%L1").arg(n); }

// The transport could not reach the host at all (as opposed to a timeout or an HTTP answer).
bool isConnectionError(QNetworkReply::NetworkError e)
{
    switch (e) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyNotFoundError:
        return true;
    default:
        return false;
    }
}

} // namespace

// ---- LlmServerProbe -----------------------------------------------------------------------

LlmServerProbe::LlmServerProbe(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<tanara::providers::llmctx::LlmServerInfo>();
}

LlmServerProbe::~LlmServerProbe()
{
    cancel();
}

net::HttpClient* LlmServerProbe::client()
{
    if (!m_http) m_http = new net::HttpClient(this);
    return m_http;
}

void LlmServerProbe::invalidateCache(const QString& baseUrl)
{
    if (baseUrl.isEmpty()) probeCache().clear();
    else probeCache().remove(nativeApiRoot(baseUrl));
}

void LlmServerProbe::cancel()
{
    ++m_generation;
    if (m_reply) {
        QNetworkReply* r = m_reply;
        m_reply.clear();
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
}

void LlmServerProbe::probe(const ProviderConfig& cfg, bool useCache)
{
    cancel();
    const quint64 gen = m_generation;
    const QString root = nativeApiRoot(cfg.baseUrl);
    const QString model = cfg.model.trimmed();
    const QUrl url(root + QStringLiteral("/api/v1/models"));
    const bool http = url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https");
    if (root.isEmpty() || !url.isValid() || !http || url.host().isEmpty()) {
        QTimer::singleShot(0, this, [this, gen, model] {
            if (gen != m_generation) return;
            LlmServerInfo info;
            info.model = model;
            emit finished(info);
        });
        return;
    }
    if (useCache) {
        const auto it = probeCache().constFind(root);
        if (it != probeCache().constEnd()
            && QDateTime::currentMSecsSinceEpoch() - it->atMs < kCacheMs) {
            const QByteArray body = it->body;
            QTimer::singleShot(0, this, [this, gen, body, model] {
                if (gen != m_generation) return;
                emit finished(parseLmStudioModels(body, model));
            });
            return;
        }
    }
    QNetworkRequest req(url);
    req.setTransferTimeout(m_timeoutMs);
    req.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
    authorize(req, cfg.apiKey);
    QNetworkReply* reply = client()->get(req);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, root, model] {
        reply->deleteLater();
        if (gen != m_generation) return;
        m_reply.clear();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray body;
        if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300)
            body = reply->readAll();
        // A failed (non-native) response is remembered too: we do not ask again for a short while.
        if (status > 0) probeCache().insert(root, {QDateTime::currentMSecsSinceEpoch(), body});
        LlmServerInfo info = parseLmStudioModels(body, model);
        // A dead address is not a "server without the native API": nothing answered at all. Only the
        // connection-level errors count (a timeout may be a slow live server, so that is left to the chat call).
        if (status <= 0 && isConnectionError(reply->error())) {
            info.unreachable = true;
            info.transportError = reply->errorString();
        }
        qInfo().noquote().nospace() << "[LLM] szerver-próba " << root << ": "
            << (info.isLmStudio() ? QStringLiteral("LM Studio")
                : info.unreachable ? QStringLiteral("nem érhető el (%1)").arg(info.transportError)
                                   : QStringLiteral("nincs natív API"))
            << (info.loaded() ? QStringLiteral(" · betöltve ctx=%1 parallel=%2")
                                    .arg(info.instances.first().contextLength)
                                    .arg(info.instances.first().parallel)
                              : QString());
        emit finished(info);
    });
}

// ---- LlmModelPreparer -----------------------------------------------------------------------

LlmModelPreparer::LlmModelPreparer(const ProviderConfig& cfg, QObject* parent)
    : LlmModelPreparer(ProviderContext{}, cfg, parent)
{
}

LlmModelPreparer::LlmModelPreparer(const ProviderContext& ctx, const ProviderConfig& cfg, QObject* parent)
    : ILocalModelServer(parent), m_ctx(ctx), m_cfg(cfg), m_http(ctx.http)
{
}

LlmModelPreparer::~LlmModelPreparer()
{
    cancel();
}

void LlmModelPreparer::cancel()
{
    m_cancelled = true;
    if (m_probe) m_probe->cancel();
    if (m_reply) {
        QNetworkReply* r = m_reply;
        m_reply.clear();
        r->disconnect(this);
        r->abort();
        r->deleteLater();
        // After an abandoned load / unload the server's state is uncertain.
        LlmServerProbe::invalidateCache(m_cfg.baseUrl);
    }
}

void LlmModelPreparer::start(int need, int floor)
{
    if (m_started) return;          // one preparation per task, no repeats
    m_started = true;
    m_need = need;
    m_floor = floor;
    m_probe = new LlmServerProbe(this);
    m_probe->setHttpClient(m_http);
    m_probe->setTimeoutMs(m_probeTimeoutMs);
    connect(m_probe, &LlmServerProbe::finished, this, &LlmModelPreparer::onProbed);
    ProviderConfig probeCfg = m_cfg;
    probeCfg.apiKey = m_ctx.bearer(m_cfg);
    m_probe->probe(probeCfg);
}

void LlmModelPreparer::onProbed(const LlmServerInfo& info)
{
    if (m_cancelled) return;
    m_info = info;
    if (info.unreachable) {
        // No point in sending the whole prompt to a dead address: say so now, with the address.
        const QString root = nativeApiRoot(m_cfg.baseUrl);
        fail(QCoreApplication::translate("LlmServer", "A helyi LLM nem érhető el: %1 — fut az LM Studio?").arg(root),
             QCoreApplication::translate("LlmServer", "kapcsolat · %1 · %2 · %3").arg(m_cfg.type, m_cfg.baseUrl, info.transportError),
             QStringLiteral("settings:llm-unreachable"));
        return;
    }
    m_decision = decidePreload(info, m_need, m_cfg.contextLength, m_floor);
    using A = PreloadDecision::Action;
    qInfo().noquote().nospace() << "[LLM] előkészítés: igény=" << m_need << " padló=" << m_floor
        << " beállítás=" << m_cfg.contextLength << " → " << m_decision.reason
        << " (ctx " << m_decision.contextLength << ")";
    switch (m_decision.action) {
    case A::Skip:
        m_loadedContext = -1;
        emit ready();
        return;
    case A::None:
        m_loadedContext = m_decision.contextLength;
        emit ready();
        return;
    case A::Reload:
        if (m_busy && m_busy()) {
            fail(QCoreApplication::translate("LlmServer", "A(z) „%1” modellt újra kellene tölteni (%2 tokenes kontextussal, egy "
                     "szálon), de a Tanara épp egy másik kérést futtat rajta. Próbáld újra, ha "
                     "az végzett.").arg(m_info.modelKey, groupedTokens(m_decision.contextLength)),
                 QString(), QString());
            return;
        }
        m_toUnload = m_decision.unloadIds;
        emit loadingStarted(m_decision.contextLength);
        unloadNext();
        return;
    case A::Load:
        emit loadingStarted(m_decision.contextLength);
        load();
        return;
    }
}

QNetworkReply* LlmModelPreparer::post(const QString& path, const QByteArray& json, int timeoutMs)
{
    if (!m_http) m_http = new net::HttpClient(this);
    QNetworkRequest req(QUrl(nativeApiRoot(m_cfg.baseUrl) + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(timeoutMs);
    authorize(req, m_ctx.bearer(m_cfg));
    QNetworkReply* reply = m_http->post(req, json);
    m_reply = reply;
    return reply;
}

void LlmModelPreparer::unloadNext()
{
    if (m_cancelled) return;
    if (m_toUnload.isEmpty()) { load(); return; }
    const QString id = m_toUnload.takeFirst();
    const QByteArray body = QJsonDocument(QJsonObject{{QStringLiteral("instance_id"), id}})
                                .toJson(QJsonDocument::Compact);
    QNetworkReply* reply = post(QStringLiteral("/api/v1/models/unload"), body, 60000);
    connect(reply, &QNetworkReply::finished, this, [this, reply, id] {
        reply->deleteLater();
        if (m_cancelled || reply != m_reply) return;
        m_reply.clear();
        LlmServerProbe::invalidateCache(m_cfg.baseUrl);
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // 404: the instance is no longer loaded, so the goal is met.
        if ((status >= 200 && status < 300) || status == 404) {
            qInfo().noquote() << "[LLM] modell-példány kivéve:" << id;
            unloadNext();
            return;
        }
        fail(QCoreApplication::translate("LlmServer", "Nem sikerült kivenni a(z) „%1” modell régi példányát az LM Studióból, ezért "
                 "nem tölthető be újra nagyobb kontextussal.").arg(id),
             QCoreApplication::translate("LlmServer", "kivétel · %1").arg(net::failureDetail(net::makeHttpExchange(reply, data))),
             QStringLiteral("settings:llm-context"));
    });
}

void LlmModelPreparer::load()
{
    if (m_cancelled) return;
    const int ctx = m_decision.contextLength;
    QJsonObject o{
        {QStringLiteral("model"), m_info.modelKey},
        {QStringLiteral("parallel"), 1},
        {QStringLiteral("echo_load_config"), true},
    };
    if (ctx > 0) o.insert(QStringLiteral("context_length"), ctx);
    qInfo().noquote() << "[LLM] modell betöltése:" << m_info.modelKey << "ctx=" << ctx << "parallel=1";
    QNetworkReply* reply = post(QStringLiteral("/api/v1/models/load"),
                                QJsonDocument(o).toJson(QJsonDocument::Compact), m_loadTimeoutMs);
    connect(reply, &QNetworkReply::finished, this, [this, reply, ctx] {
        reply->deleteLater();
        if (m_cancelled || reply != m_reply) return;
        m_reply.clear();
        LlmServerProbe::invalidateCache(m_cfg.baseUrl);
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const net::HttpExchange ex = net::makeHttpExchange(reply, data);
        if (status >= 200 && status < 300) {
            const QJsonObject root = QJsonDocument::fromJson(data).object();
            const int got = root.value(QStringLiteral("load_config")).toObject()
                                .value(QStringLiteral("context_length")).toInt(ctx);
            m_loadedContext = got;
            m_didLoad = true;
            LlmInstanceInfo li;
            li.id = root.value(QStringLiteral("instance_id")).toString(m_info.modelKey);
            li.contextLength = got;
            li.parallel = 1;
            m_info.instances = {li};
            qInfo().noquote() << "[LLM] betöltve:" << li.id << "ctx=" << got;
            emit ready();
            return;
        }
        const QString detail = QCoreApplication::translate("LlmServer", "betöltés · %1").arg(net::failureDetail(ex));
        if (status <= 0) {
            fail(QCoreApplication::translate("LlmServer", "Az LM Studio nem válaszolt a(z) „%1” modell betöltése közben.").arg(m_info.modelKey),
                 detail, QStringLiteral("settings:llm-context"));
            return;
        }
        QString code;
        const QJsonObject err = QJsonDocument::fromJson(data).object().value(QStringLiteral("error")).toObject();
        code = err.value(QStringLiteral("type")).toString();
        if (code.isEmpty()) code = err.value(QStringLiteral("code")).toString();
        if (status == 404 || code == QLatin1String("model_not_found")) {
            fail(QCoreApplication::translate("LlmServer", "Az LM Studio nem találja a beállított modellt (%1).").arg(m_info.modelKey),
                 detail, QStringLiteral("settings:llm"));
            return;
        }
        QString msg = QCoreApplication::translate("LlmServer", "Az LM Studio nem tudta betölteni a(z) „%1” modellt %2 tokenes "
                          "kontextussal. Valószínűleg ekkora kontextussal nem fér a videókártyára — "
                          "állíts be kisebb kontextust a Beállításokban.")
                          .arg(m_info.modelKey, groupedTokens(ctx));
        if (!m_info.otherLoaded.isEmpty())
            msg += QLatin1Char(' ')
                 + QCoreApplication::translate("LlmServer", "A szerveren más modell is be van töltve (%1); azt a Tanara nem veszi ki.")
                       .arg(m_info.otherLoaded.join(QStringLiteral(", ")));
        fail(msg, detail, QStringLiteral("settings:llm-context"));
    });
}

void LlmModelPreparer::fail(const QString& message, const QString& detail, const QString& hint)
{
    // Every preparation failure is logged with the provider and the address (the user sees the message in the UI,
    // the log must say where it went wrong too).
    qWarning().noquote().nospace() << "[LLM] az előkészítés nem sikerült: provider=" << m_cfg.type
        << " url=" << m_cfg.baseUrl << " hiba=" << message << (detail.isEmpty() ? QString() : QStringLiteral(" | ") + detail);
    JobError e;
    e.message = message;
    e.detail = detail;
    e.fixActionHint = hint;
    e.when = QDateTime::currentDateTime();
    emit failed(e);
}

} // namespace tanara::providers