#include "tanara/embedding/OpenAiCompatibleEmbeddingProvider.h"
#include "tanara/cloud/CloudTypes.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace tanara {

namespace {

QUrl embeddingsUrl(const QString& baseUrl)
{
    QString b = baseUrl.trimmed();
    while (b.endsWith(QLatin1Char('/'))) b.chop(1);
    return QUrl(b + QStringLiteral("/embeddings"));
}

} // namespace

OpenAiCompatibleEmbeddingJob::OpenAiCompatibleEmbeddingJob(QNetworkAccessManager* nam,
                                                           const ProviderConfig& cfg,
                                                           const EmbeddingRequest& req,
                                                           QObject* parent)
    : EmbeddingJob(parent), m_nam(nam), m_cfg(cfg), m_req(req)
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
        emit failed(tr("Hiányzik a beágyazó végpont címe."));
        return;
    }
    sendNext();
}

void OpenAiCompatibleEmbeddingJob::cancel()
{
    m_done = true;   // jel nem megy ki
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
    if (!m_cfg.apiKey.trimmed().isEmpty())
        request.setRawHeader(QByteArrayLiteral("Authorization"),
                             QByteArrayLiteral("Bearer ") + m_cfg.apiKey.trimmed().toUtf8());
    for (const auto& h : m_cfg.extraHeaders)
        request.setRawHeader(h.first, h.second);
    m_reply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::finished, this, &OpenAiCompatibleEmbeddingJob::onReply);
}

void OpenAiCompatibleEmbeddingJob::onReply()
{
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    if (!reply || m_done) return;
    reply->deleteLater();
    const QByteArray data = reply->readAll();
    if (m_cfg.onExchange)
        m_cfg.onExchange(makeHttpExchange(reply, data));
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError) {
        m_done = true;
        m_errorStatus = status;
        // A Tanara Cloud strukturált hibája (error.code/message) és az OpenAI-alak egyaránt
        // az error.message-ben hozza az emberi mondatot.
        QString apiMsg;
        const QJsonDocument edoc = QJsonDocument::fromJson(data);
        if (edoc.isObject()) {
            const QJsonValue ev = edoc.object().value(QStringLiteral("error"));
            if (ev.isObject()) apiMsg = ev.toObject().value(QStringLiteral("message")).toString();
            else if (ev.isString()) apiMsg = ev.toString();
        }
        if (status == 0)
            emit failed(tr("Hálózati hiba: %1").arg(reply->errorString()));
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
        emit failed(tr("Érvénytelen beágyazási válasz (%1 vektor jött %2 szövegre).")
                        .arg(arr.size()).arg(m_batch));
        return;
    }
    // A data[] elemei az "index" szerint (ha van), különben a válasz sorrendjében.
    QVector<QVector<float>> batch(m_batch);
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject o = arr.at(i).toObject();
        const int idx = o.contains(QStringLiteral("index")) ? o.value(QStringLiteral("index")).toInt(i) : i;
        const QJsonArray emb = o.value(QStringLiteral("embedding")).toArray();
        if (idx < 0 || idx >= m_batch || emb.isEmpty()) {
            m_done = true;
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
    : QObject(parent), m_cfg(cfg), m_nam(new QNetworkAccessManager(this))
{
}

OpenAiCompatibleEmbeddingProvider::~OpenAiCompatibleEmbeddingProvider() = default;

QString OpenAiCompatibleEmbeddingProvider::name() const
{
    return QStringLiteral("openai-compat-embedding");
}

EmbeddingJob* OpenAiCompatibleEmbeddingProvider::embed(const EmbeddingRequest& req)
{
    auto* job = new OpenAiCompatibleEmbeddingJob(m_nam, m_cfg, req, this);
    // Az indítás az eseményhurokba kerül, hogy a hívó előbb bekösse a jeleket.
    QTimer::singleShot(0, job, [job]() { job->start(); });
    return job;
}

} // namespace tanara
