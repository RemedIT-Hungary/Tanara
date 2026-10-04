#include "tanara/llm/OpenAiCompatibleProvider.h"
#include "tanara/cloud/CloudTypes.h"

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
#include <QRegularExpression>

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

ReasoningSwitch reasoningSwitchFor(const QString& setting, const QString& model)
{
    if (setting.trimmed().compare(QLatin1String("on"), Qt::CaseInsensitive) == 0)
        return ReasoningSwitch::None;
    // "auto" / "off" / ismeretlen érték → kikapcsolás; a módszert a modellcsalád dönti el.
    // Qwen 3.x (és a QwQ) a reasoning_effort-ot figyelmen kívül hagyja → előtöltés kell.
    static const QRegularExpression qwen(QStringLiteral("qwen-?(\\d+)"),
                                         QRegularExpression::CaseInsensitiveOption);
    const QString m = model.toLower();
    if (m.contains(QLatin1String("qwq")))
        return ReasoningSwitch::Prefill;
    const QRegularExpressionMatch qm = qwen.match(m);
    if (qm.hasMatch() && qm.captured(1).toInt() >= 3)
        return ReasoningSwitch::Prefill;
    return ReasoningSwitch::Effort;   // Gemma és minden más: a nem ismerő szerver figyelmen kívül hagyja
}

QJsonObject buildChatCompletionBody(const ProviderConfig& cfg, const LlmRequest& req)
{
    const QString model = !req.model.isEmpty() ? req.model : cfg.model;
    QVector<ChatMessage> msgs = req.messages;
    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("temperature"), req.temperature);
    body.insert(QStringLiteral("max_tokens"), req.maxTokens);
    body.insert(QStringLiteral("stream"), false);
    switch (reasoningSwitchFor(cfg.reasoning, model)) {
    case ReasoningSwitch::None:
        break;
    case ReasoningSwitch::Effort:
        body.insert(QStringLiteral("reasoning_effort"), QStringLiteral("none"));
        break;
    case ReasoningSwitch::Prefill:
        // Az üres gondolkodás-blokk után a modell rögtön a választ írja (csak ha a felhasználó
        // üzenete az utolsó — egy meglévő asszisztens-előtöltést nem írunk felül).
        if (!msgs.isEmpty() && msgs.last().role == QLatin1String("user"))
            msgs.append({QStringLiteral("assistant"), QStringLiteral("<think>\n\n</think>\n\n")});
        break;
    }
    body.insert(QStringLiteral("messages"), buildMessages(msgs));
    return body;
}

QString stripThinking(const QString& content)
{
    QString s = content;
    int i = 0;
    while (i < s.size() && s.at(i).isSpace()) ++i;
    if (s.mid(i, 7).compare(QLatin1String("<think>"), Qt::CaseInsensitive) == 0) {
        const int end = s.indexOf(QLatin1String("</think>"), i, Qt::CaseInsensitive);
        if (end < 0) return QString();          // csak gondolkodás, válasz nincs
        return s.mid(end + 8).trimmed();
    }
    // Előtöltés után a modell néha a záró címkével folytat.
    if (s.mid(i, 8).compare(QLatin1String("</think>"), Qt::CaseInsensitive) == 0)
        return s.mid(i + 8).trimmed();
    return s;
}

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
    const QJsonObject body = buildChatCompletionBody(m_cfg, m_req);

    QNetworkRequest request(chatCompletionsUrl(m_cfg.baseUrl));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    if (!m_cfg.apiKey.isEmpty()) {
        const QByteArray auth = QByteArrayLiteral("Bearer ") + m_cfg.apiKey.toUtf8();
        request.setRawHeader(QByteArrayLiteral("Authorization"), auth);
    }
    // Gateway-mód (Tanara Cloud): additív X-Tanara-* / Accept-Language fejlécek.
    for (const auto& h : m_cfg.extraHeaders)
        request.setRawHeader(h.first, h.second);

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
    if (m_cfg.onExchange)                        // gateway-hook: terhelés / strukturált hiba
        m_cfg.onExchange(makeHttpExchange(reply, data));
    if (reply->error() != QNetworkReply::NoError) {
        QString apiMsg;
        const QJsonDocument edoc = QJsonDocument::fromJson(data);
        if (edoc.isObject()) {
            const QJsonValue ev = edoc.object().value(QStringLiteral("error"));
            if (ev.isObject()) apiMsg = ev.toObject().value(QStringLiteral("message")).toString();
            else if (ev.isString()) apiMsg = ev.toString();
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        m_errorStatus = status;
        m_errorBody = data;
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
    const QString rawContent = message.value(QStringLiteral("content")).toString();
    // A gondolkodás SOHA nem kerül a válaszba: a reasoning_content-et nem használjuk, a
    // tartalom elejére szivárgott <think>…</think> blokkot levesszük.
    const QString content = stripThinking(rawContent);
    const QString finish = first.value(QStringLiteral("finish_reason")).toString();
    const QJsonObject usage = root.value(QStringLiteral("usage")).toObject();
    const int thinking = message.value(QStringLiteral("reasoning_content")).toString().size()
                       + message.value(QStringLiteral("reasoning")).toString().size()
                       + (rawContent.size() - content.size());
    // Debug-életjel (--debug mellett látszik): a válasz-metaadatok azonnal megmutatják, ha
    // a modell csonkolt (finish=length) vagy gondolkodott.
    qInfo().noquote().nospace()
        << "[LLM] model=" << (m_req.model.isEmpty() ? m_cfg.model : m_req.model)
        << " finish=" << finish
        << " prompt=" << usage.value(QStringLiteral("prompt_tokens")).toInt()
        << " completion=" << usage.value(QStringLiteral("completion_tokens")).toInt()
        << " content=" << content.trimmed().size() << "ch"
        << " thinking=" << thinking << "ch";
    if (content.trimmed().isEmpty()) {
        emit failed(thinking > 0
            ? tr("A modell nem adott választ, csak „gondolkodott” (%1 karakter, finish=%2). "
                 "Kapcsold ki a gondolkodást a modell beállításainál, vagy növeld a max. tokenszámot.")
                  .arg(thinking).arg(finish)
            : tr("Üres LLM-válasz (finish=%1).").arg(finish));
        return;
    }
    // --debug mellett a NYERS válasz (első ~4000 karakter) is a logba kerül.
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
