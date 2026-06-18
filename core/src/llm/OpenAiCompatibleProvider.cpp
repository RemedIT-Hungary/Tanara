#include "tanara/llm/OpenAiCompatibleProvider.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QUrl>
#include <QTimer>

namespace tanara {

namespace {

// baseUrl + "/chat/completions" képzése úgy, hogy a dupla / elkerüljük.
QUrl chatCompletionsUrl(const QString& baseUrl)
{
    QString b = baseUrl;
    while (b.endsWith(QLatin1Char('/')))
        b.chop(1);
    return QUrl(b + QStringLiteral("/chat/completions"));
}

QJsonArray buildMessages(const QVector<ChatMessage>& msgs)
{
    QJsonArray arr;
    for (const ChatMessage& m : msgs) {
        QJsonObject o;
        o.insert(QStringLiteral("role"), m.role);
        o.insert(QStringLiteral("content"), m.content);
        arr.append(o);
    }
    return arr;
}

} // namespace

// ---- OpenAiCompatibleJob ---------------------------------------------------

OpenAiCompatibleJob::OpenAiCompatibleJob(QNetworkAccessManager* nam,
                                         const ProviderConfig& cfg,
                                         const LlmRequest& req,
                                         QObject* parent)
    : LlmJob(parent)
    , m_nam(nam)
    , m_cfg(cfg)
    , m_req(req)
{
}

OpenAiCompatibleJob::~OpenAiCompatibleJob() = default;

void OpenAiCompatibleJob::start()
{
    const QString model = !m_req.model.isEmpty() ? m_req.model : m_cfg.model;

    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("messages"), buildMessages(m_req.messages));
    body.insert(QStringLiteral("temperature"), m_req.temperature);
    body.insert(QStringLiteral("max_tokens"), m_req.maxTokens);
    body.insert(QStringLiteral("stream"), false);

    QNetworkRequest request(chatCompletionsUrl(m_cfg.baseUrl));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    if (!m_cfg.apiKey.isEmpty()) {
        const QByteArray auth = QByteArrayLiteral("Bearer ") + m_cfg.apiKey.toUtf8();
        request.setRawHeader(QByteArrayLiteral("Authorization"), auth);
    }

    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    m_reply = m_nam->post(request, payload);
    connect(m_reply, &QNetworkReply::finished, this, &OpenAiCompatibleJob::onFinished);
}

void OpenAiCompatibleJob::cancel()
{
    if (m_reply && m_reply->isRunning())
        m_reply->abort();
}

void OpenAiCompatibleJob::onFinished()
{
    if (m_done)
        return;
    m_done = true;

    QNetworkReply* reply = m_reply;
    if (!reply) {
        emit failed(QStringLiteral("Nincs hálózati válasz (reply == null)."));
        return;
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit failed(QStringLiteral("Hálózati hiba: %1").arg(reply->errorString()));
        return;
    }

    const QByteArray data = reply->readAll();
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        emit failed(QStringLiteral("Érvénytelen JSON válasz: %1").arg(perr.errorString()));
        return;
    }

    const QJsonObject root = doc.object();
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        emit failed(QStringLiteral("A válasz nem tartalmaz 'choices' tömböt."));
        return;
    }

    const QJsonObject first = choices.at(0).toObject();
    const QJsonObject message = first.value(QStringLiteral("message")).toObject();
    if (!message.contains(QStringLiteral("content"))) {
        emit failed(QStringLiteral("Hiányzik a choices[0].message.content mező."));
        return;
    }

    const QString content = message.value(QStringLiteral("content")).toString();
    emit finished(content);
}

// ---- OpenAiCompatibleProvider ----------------------------------------------

OpenAiCompatibleProvider::OpenAiCompatibleProvider(const ProviderConfig& cfg, QObject* parent)
    : QObject(parent)
    , m_cfg(cfg)
    , m_nam(new QNetworkAccessManager(this))
{
}

OpenAiCompatibleProvider::~OpenAiCompatibleProvider() = default;

QString OpenAiCompatibleProvider::name() const
{
    return QStringLiteral("openai-compat");
}

bool OpenAiCompatibleProvider::supportsStreaming() const
{
    return false;
}

LlmJob* OpenAiCompatibleProvider::chat(const LlmRequest& req)
{
    auto* job = new OpenAiCompatibleJob(m_nam, m_cfg, req, this);
    // A kérés indítását az event-loopba toljuk, hogy a hívó még a finished/failed
    // előtt össze tudja kötni a signalokat (gyors/cache-elt válasz esetén is).
    QTimer::singleShot(0, job, [job]() { job->start(); });
    return job;
}

} // namespace tanara
