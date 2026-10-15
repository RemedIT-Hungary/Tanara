#include "tanara/foundation/net/NetworkErrorText.h"
#include "tanara/providers/embedding/OpenAiCompatibleEmbeddingProvider.h"
#include "tanara/providers/core/ProviderConfig.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace tanara::providers {

namespace {

QUrl embeddingsUrl(const QString& baseUrl)
{
    QString b = baseUrl.trimmed();
    while (b.endsWith(QLatin1Char('/'))) b.chop(1);
    return QUrl(b + QStringLiteral("/embeddings"));
}

} // namespace

OpenAiCompatibleEmbeddingJob::OpenAiCompatibleEmbeddingJob(net::HttpClient* http,
                                                           const ProviderContext& ctx,
                                                           const ProviderConfig& cfg,
                                                           const EmbeddingRequest& req,
                                                           QObject* parent)
    : EmbeddingJob(parent), m_http(http), m_ctx(ctx), m_cfg(cfg), m_req(req)
{
}

OpenAiCompatibleEmbeddingJob::~OpenAiCompatibleEmbeddingJob()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
}

void OpenAiCompatibleEmbeddingJob::start()
{
    if (m_done) return;
    if (m_req.texts.isEmpty()) {
        m_done = true;
        emit finished({});
        return;
    }
    if (!embeddingsUrl(m_cfg.baseUrl).isValid() || m_cfg.baseUrl.trimmed().isEmpty()) {
        m_done = true;
        m_error.kind = Error::Kind::Input;
        m_error.code = QStringLiteral("missing_base_url");
        emit failed(tr("Hiányzik a beágyazó végpont címe."));
        return;
    }
    sendNext();
}

void OpenAiCompatibleEmbeddingJob::cancel()
{
    m_done = true;   // no signal goes out
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void OpenAiCompatibleEmbeddingJob::sendNext()
{
    m_batch = int(std::min<qsizetype>(kBatchSize, m_req.texts.size() - m_next));
    QJsonArray input;
    for (int i = 0; i < m_batch; ++i) input.append(m_req.texts.at(m_next + i));
    QJsonObject body;
    body.insert(QStringLiteral("model"), m_req.model.isEmpty() ? m_cfg.model : m_req.model);
    body.insert(QStringLiteral("input"), input);

    QNetworkRequest request(embeddingsUrl(m_cfg.baseUrl));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QString bearer = m_ctx.bearer(m_cfg).trimmed();
    if (!bearer.isEmpty())
        request.setRawHeader(QByteArrayLiteral("Authorization"),
                             QByteArrayLiteral("Bearer ") + bearer.toUtf8());
    m_ctx.applyHeaders(request);
    if (!m_http) {
        m_done = true;
        m_error.kind = Error::Kind::Service;
        emit failed(tr("Hálózati hiba: %1").arg(QStringLiteral("no HTTP client")));
        return;
    }
    m_reply = m_http->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::finished, this, &OpenAiCompatibleEmbeddingJob::onReply);
}

void OpenAiCompatibleEmbeddingJob::onReply()
{
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    if (!reply || m_done) return;
    reply->deleteLater();
    const QByteArray data = reply->readAll();
    const net::HttpExchange ex = net::makeHttpExchange(reply, data);
    m_ctx.report(ex);
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError) {
        m_done = true;
        m_errorStatus = status;
        m_error = net::errorFromExchange(ex);
        // Both a Tanara Cloud structured error (error.code/message) and the OpenAI shape carry the human
        // sentence in error.message.
        QString apiMsg;
        const QJsonDocument edoc = QJsonDocument::fromJson(data);
        if (edoc.isObject()) {
            const QJsonValue ev = edoc.object().value(QStringLiteral("error"));
            if (ev.isObject()) apiMsg = ev.toObject().value(QStringLiteral("message")).toString();
            else if (ev.isString()) apiMsg = ev.toString();
        }
        if (status == 0)
            emit failed(tr("Hálózati hiba: %1").arg(net::networkErrorText(int(reply->error()), reply->errorString())));
        else if (!apiMsg.isEmpty())
            emit failed(tr("Beágyazási hiba (HTTP %1): %2").arg(status).arg(apiMsg));
        else
            emit failed(tr("Beágyazási hiba (HTTP %1).").arg(status));
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    const QJsonArray arr = doc.object().value(QStringLiteral("data")).toArray();
    if (arr.size() != m_batch) {
        m_done = true;
        m_error.kind = Error::Kind::Service;
        m_error.httpStatus = status;
        m_error.code = QStringLiteral("bad_response");
        emit failed(tr("Érvénytelen beágyazási válasz (%1 vektor jött %2 szövegre).")
                        .arg(arr.size()).arg(m_batch));
        return;
    }
    // The data[] items by "index" (if present), otherwise in the response's order.
    QVector<QVector<float>> batch(m_batch);
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject o = arr.at(i).toObject();
        const int idx = o.contains(QStringLiteral("index")) ? o.value(QStringLiteral("index")).toInt(i) : i;
        const QJsonArray emb = o.value(QStringLiteral("embedding")).toArray();
        if (idx < 0 || idx >= m_batch || emb.isEmpty()) {
            m_done = true;
            m_error.kind = Error::Kind::Service;
            m_error.httpStatus = status;
            m_error.code = QStringLiteral("bad_response");
            emit failed(tr("Érvénytelen beágyazási válasz (hiányzó vektor)."));
            return;
        }
        QVector<float> v;
        v.reserve(emb.size());
        for (const QJsonValue& x : emb) v.append(float(x.toDouble()));
        batch[idx] = v;
    }
    m_out += batch;
    m_next += m_batch;
    if (m_next < m_req.texts.size()) {
        sendNext();
        return;
    }
    m_done = true;
    emit finished(m_out);
}

// ---- provider ------------------------------------------------------------------------------

OpenAiCompatibleEmbeddingProvider::OpenAiCompatibleEmbeddingProvider(const ProviderConfig& cfg,
                                                                     QObject* parent)
    : OpenAiCompatibleEmbeddingProvider(ProviderContext{}, cfg, parent)
{
}

OpenAiCompatibleEmbeddingProvider::OpenAiCompatibleEmbeddingProvider(const ProviderContext& ctx,
                                                                     const ProviderConfig& cfg,
                                                                     QObject* parent)
    : QObject(parent), m_ctx(ctx), m_cfg(cfg),
      m_http(ctx.http ? ctx.http.data() : new net::HttpClient(this))
{
}

OpenAiCompatibleEmbeddingProvider::~OpenAiCompatibleEmbeddingProvider() = default;

QString OpenAiCompatibleEmbeddingProvider::name() const
{
    return QStringLiteral("openai-compat-embedding");
}

EmbeddingJob* OpenAiCompatibleEmbeddingProvider::embed(const EmbeddingRequest& req)
{
    auto* job = new OpenAiCompatibleEmbeddingJob(m_http, m_ctx, m_cfg, req, this);
    m_ctx.cancel.onCancel([j = QPointer<OpenAiCompatibleEmbeddingJob>(job)]() { if (j) j->cancel(); });
    // The start goes into the event loop so the caller connects the signals first.
    QTimer::singleShot(0, job, [job]() { job->start(); });
    return job;
}

} // namespace tanara::providers