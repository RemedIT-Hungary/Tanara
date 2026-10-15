#include "tanara/foundation/error/Error.h"
#include "tanara/foundation/net/HttpClient.h"
#include "tanara/foundation/net/NetworkErrorText.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

namespace tanara {

namespace {

// A one-line, shortened text for the technical line.
QString oneLine(const QString& s, int maxLen = 200)
{
    QString t = s.simplified();
    if (t.size() > maxLen)
        t = t.left(maxLen - 1) + QChar(0x2026);
    return t;
}

// From the error body a machine code + message (the shapes of several providers).
void parseErrorBody(const QByteArray& body, QString* code, QString* message, QString* requestId)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return;
    const QJsonObject root = doc.object();
    const QJsonValue ev = root.value(QStringLiteral("error"));
    if (ev.isObject()) {                       // OpenAI-compatible / Tanara gateway
        const QJsonObject e = ev.toObject();
        *code = e.value(QStringLiteral("code")).toVariant().toString();
        if (code->isEmpty())
            *code = e.value(QStringLiteral("type")).toString();
        *message = e.value(QStringLiteral("message")).toString();
        if (requestId) *requestId = e.value(QStringLiteral("request_id")).toString();
        return;
    }
    if (ev.isString())
        *message = ev.toString();
    // Soniox: { status_code, error_type, message }
    if (code->isEmpty())
        *code = root.value(QStringLiteral("error_type")).toString();
    if (message->isEmpty())
        *message = root.value(QStringLiteral("message")).toString();
    if (message->isEmpty())
        *message = root.value(QStringLiteral("error_message")).toString();
    if (message->isEmpty())
        *message = root.value(QStringLiteral("detail")).toString();
}

} // namespace

Error Error::cancelled()
{
    Error e;
    e.kind = Kind::Cancelled;
    e.code = QStringLiteral("cancelled");
    return e;
}

QString errorKindName(Error::Kind kind)
{
    switch (kind) {
    case Error::Kind::None:        return QStringLiteral("none");
    case Error::Kind::Auth:        return QStringLiteral("auth");
    case Error::Kind::Quota:       return QStringLiteral("quota");
    case Error::Kind::Transient:   return QStringLiteral("transient");
    case Error::Kind::Input:       return QStringLiteral("input");
    case Error::Kind::Unsupported: return QStringLiteral("unsupported");
    case Error::Kind::Service:     return QStringLiteral("service");
    case Error::Kind::Cancelled:   return QStringLiteral("cancelled");
    }
    return QStringLiteral("none");
}

Error::Kind errorKindForHttpStatus(int st)
{
    if (st <= 0) return Error::Kind::Transient;
    if (st >= 200 && st < 300) return Error::Kind::None;
    if (st == 401 || st == 403) return Error::Kind::Auth;
    if (st == 402) return Error::Kind::Quota;
    if (st == 408 || st == 429 || st == 502 || st == 503 || st == 504) return Error::Kind::Transient;
    if (st == 426 || st == 501) return Error::Kind::Unsupported;
    if (st >= 500) return Error::Kind::Service;
    if (st >= 400) return Error::Kind::Input;
    return Error::Kind::Service;   // 1xx / 3xx at the end of a call: not a usable answer
}

namespace net {

Error errorFromExchange(const HttpExchange& ex)
{
    Error e;
    e.kind = errorKindForHttpStatus(ex.status);
    if (!e.isError()) return e;
    e.httpStatus = ex.status;
    if (ex.status <= 0) {
        e.code = QStringLiteral("network");
        e.detail = ex.networkError;
        e.networkErrorCode = ex.networkErrorCode;
        return e;
    }
    parseErrorBody(ex.body, &e.code, &e.message, &e.requestId);
    if (e.requestId.isEmpty())
        e.requestId = QString::fromUtf8(ex.headers.value(QByteArrayLiteral("x-tanara-request-id")));
    e.retryAfterSec = ex.headers.value(QByteArrayLiteral("retry-after")).trimmed().toInt();
    return e;
}

QString failureDetail(const HttpExchange& ex)
{
    // The translation context stays "JobErrors" (the texts were born there).
    if (ex.status <= 0) {
        const QString net = oneLine(networkErrorText(ex.networkErrorCode, ex.networkError));
        return net.isEmpty() ? QCoreApplication::translate("JobErrors", "hálózat · nincs válasz")
                             : QCoreApplication::translate("JobErrors", "hálózat · %1").arg(net);
    }
    QString code, message;
    parseErrorBody(ex.body, &code, &message, nullptr);
    QStringList parts{QStringLiteral("HTTP %1").arg(ex.status)};
    if (!code.isEmpty())
        parts << oneLine(code, 80);
    if (!message.isEmpty())
        parts << oneLine(message);
    else if (code.isEmpty() && !ex.body.trimmed().isEmpty())
        parts << oneLine(QString::fromUtf8(ex.body), 160);
    return parts.join(QStringLiteral(" · "));
}

} // namespace net

} // namespace tanara
