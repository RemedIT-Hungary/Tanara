#include "tanara/cloud/detail/GatewayClient.h"
#include "tanara/cloud/CloudSession.h"

#include <QHttpMultiPart>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace tanara::cloud::detail {

GatewayClient::GatewayClient(const providers::ProviderContext& ctx, const providers::ProviderConfig& cfg,
                             CloudSession* session)
    : m_ctx(ctx), m_cfg(cfg), m_session(session)
{
}

QString GatewayClient::apiBase() const
{
    QString b = m_cfg.baseUrl.trimmed();
    if (b.isEmpty() && m_session) b = m_session->apiBase();
    while (b.endsWith(QLatin1Char('/'))) b.chop(1);
    return b;
}

net::HttpClient* GatewayClient::http() const
{
    if (m_ctx.http) return m_ctx.http;
    return m_session ? m_session->httpClient() : nullptr;
}

void GatewayClient::prepare(QNetworkRequest& req) const
{
    QString bearer = m_ctx.bearer(m_cfg).trimmed();
    if (bearer.isEmpty() && m_session) bearer = m_session->apiKey();
    if (!bearer.isEmpty())
        req.setRawHeader(QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + bearer.toUtf8());
    if (!m_ctx.headers.isEmpty()) {
        m_ctx.applyHeaders(req);
    } else if (m_session) {
        for (const auto& h : m_session->requestHeaders(m_ctx.jobId))
            req.setRawHeader(h.first, h.second);
    }
}

QNetworkReply* GatewayClient::finish(QNetworkReply* reply, QObject* receiver, Done done) const
{
    if (!reply) return nullptr;
    const providers::ProviderContext ctx = m_ctx;
    QPointer<CloudSession> session = m_session;
    QPointer<QObject> guard(receiver);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, ctx, session, guard, done]() {
        reply->deleteLater();
        if (reply->property("tanaraAborted").toBool()) return;
        const QByteArray body = reply->readAll();
        const HttpExchange ex = net::makeHttpExchange(reply, body);
        if (ctx.observe) ctx.report(ex);
        else if (session) session->observeExchange(ex);
        if (guard && done) done(ex);
    });
    return reply;
}

QNetworkReply* GatewayClient::getJson(const QString& path, QObject* receiver, Done done) const
{
    net::HttpClient* client = http();
    if (!client) return nullptr;
    QNetworkRequest req(QUrl(apiBase() + path));
    prepare(req);
    return finish(client->get(req), receiver, std::move(done));
}

QNetworkReply* GatewayClient::postJson(const QString& path, const QByteArray& body, QObject* receiver,
                                       Done done) const
{
    net::HttpClient* client = http();
    if (!client) return nullptr;
    QNetworkRequest req(QUrl(apiBase() + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    prepare(req);
    return finish(client->post(req, body), receiver, std::move(done));
}

QNetworkReply* GatewayClient::postMultipart(const QString& path, QHttpMultiPart* multipart,
                                            QObject* receiver, Done done) const
{
    net::HttpClient* client = http();
    if (!client) { delete multipart; return nullptr; }
    QNetworkRequest req(QUrl(apiBase() + path));
    prepare(req);
    QNetworkReply* reply = client->post(req, multipart);
    multipart->setParent(reply);
    return finish(reply, receiver, std::move(done));
}

Error GatewayClient::errorOf(const HttpExchange& ex)
{
    Error e = toError(parseCloudError(ex.status, ex.headers, ex.body, ex.networkError, ex.networkErrorCode));
    if (!e.isError()) {   // a 2xx without the expected content
        e.kind = Error::Kind::Service;
        e.httpStatus = ex.status;
        e.code = QStringLiteral("bad_response");
    }
    return e;
}

void abortReply(QNetworkReply* reply)
{
    if (!reply) return;
    reply->setProperty("tanaraAborted", true);
    reply->abort();
}

} // namespace tanara::cloud::detail
