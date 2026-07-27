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
#include <QDebug>

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
        emit failed(tr("Nincs hálózati válasz (reply == null)."));
        return;
    }
    reply->deleteLater();

    const QByteArray data = reply->readAll();   // a hibatörzset is kiolvassuk
    if (reply->error() != QNetworkReply::NoError) {
        QString apiMsg;
        const QJsonDocument edoc = QJsonDocument::fromJson(data);
        if (edoc.isObject()) {
            const QJsonValue ev = edoc.object().value(QStringLiteral("error"));
            if (ev.isObject()) apiMsg = ev.toObject().value(QStringLiteral("message")).toString();
            else if (ev.isString()) apiMsg = ev.toString();
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        emit failed(apiMsg.isEmpty()
            ? tr("Hálózati hiba: %1").arg(reply->errorString())
            : tr("LLM hiba (HTTP %1): %2").arg(status).arg(apiMsg));
        return;
    }

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        emit failed(tr("Érvénytelen JSON válasz: %1").arg(perr.errorString()));
        return;
    }

    const QJsonObject root = doc.object();
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        emit failed(tr("A válasz nem tartalmaz 'choices' tömböt."));
        return;
    }

    const QJsonObject first = choices.at(0).toObject();
    const QJsonObject message = first.value(QStringLiteral("message")).toObject();
    QString content = message.value(QStringLiteral("content")).toString();
    if (content.trimmed().isEmpty())   // reasoning-modell: a tartalom a reasoning_content-ben lehet
        content = message.value(QStringLiteral("reasoning_content")).toString();
    if (content.trimmed().isEmpty()) {
        emit failed(tr("Üres LLM-válasz (sem content, sem reasoning_content)."));
        return;
    }
    // Debug-életjel (--debug mellett látszik): a válasz-metaadatok azonnal megmutatják, ha
    // a modell csonkolt (finish=length) vagy a reasoning-fallback aktivált (üres content).
    const QJsonObject usage = root.value(QStringLiteral("usage")).toObject();
    const bool reasoningFallback = message.value(QStringLiteral("content")).toString().trimmed().isEmpty();
    qInfo().noquote().nospace()
        << "[LLM] model=" << (m_req.model.isEmpty() ? m_cfg.model : m_req.model)
        << " finish=" << first.value(QStringLiteral("finish_reason")).toString()
        << " prompt=" << usage.value(QStringLiteral("prompt_tokens")).toInt()
        << " completion=" << usage.value(QStringLiteral("completion_tokens")).toInt()
        << " content=" << content.trimmed().size() << "ch"
        << (reasoningFallback ? " [reasoning-fallback]" : "");
    // --debug mellett a NYERS válasz (első ~4000 karakter) is a logba kerül — így élőben
    // látszik, ha a (reasoning-)modell hangosan gondolkodik a válaszba (nem a prompt hibája).
    qDebug().noquote().nospace()
        << "[LLM] nyers válasz (első 4000ch):\n" << content.left(4000);
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
