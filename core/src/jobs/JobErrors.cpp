#include "tanara/jobs/JobErrors.h"
#include "tanara/cloud/CloudTypes.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace tanara {

namespace {

// Egysoros, rövidített szöveg a technikai sorba.
QString oneLine(const QString& s, int maxLen = 200)
{
    QString t = s.simplified();
    if (t.size() > maxLen)
        t = t.left(maxLen - 1) + QChar(0x2026);
    return t;
}

// A hibatörzsből gépi kód + üzenet (több szolgáltató alakja).
void parseErrorBody(const QByteArray& body, QString* code, QString* message)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return;
    const QJsonObject root = doc.object();
    const QJsonValue ev = root.value(QStringLiteral("error"));
    if (ev.isObject()) {                       // OpenAI-kompatibilis / Tanara-gateway
        const QJsonObject e = ev.toObject();
        *code = e.value(QStringLiteral("code")).toVariant().toString();
        if (code->isEmpty())
            *code = e.value(QStringLiteral("type")).toString();
        *message = e.value(QStringLiteral("message")).toString();
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

QString settingsHint(JobKind kind)
{
    return kind == JobKind::Transcribe ? QStringLiteral("settings:stt")
                                       : QStringLiteral("settings:llm");
}

bool isProviderJob(JobKind kind)
{
    return kind == JobKind::Transcribe || kind == JobKind::Summarize
        || kind == JobKind::ExtractTopics || kind == JobKind::AnalyzeTopics;
}

} // namespace

QString httpFailureDetail(const HttpExchange& ex)
{
    if (ex.status <= 0) {
        const QString net = oneLine(ex.networkError);
        return net.isEmpty() ? QCoreApplication::translate("JobErrors", "hálózat · nincs válasz")
                             : QCoreApplication::translate("JobErrors", "hálózat · %1").arg(net);
    }
    QString code, message;
    parseErrorBody(ex.body, &code, &message);
    QStringList parts{QStringLiteral("HTTP %1").arg(ex.status)};
    if (!code.isEmpty())
        parts << oneLine(code, 80);
    if (!message.isEmpty())
        parts << oneLine(message);
    else if (code.isEmpty() && !ex.body.trimmed().isEmpty())
        parts << oneLine(QString::fromUtf8(ex.body), 160);
    return parts.join(QStringLiteral(" · "));
}

JobError describeJobFailure(JobKind kind, const QString& rawMessage, const HttpExchange* ex)
{
    JobError e;
    e.kind = kind;
    e.when = QDateTime::currentDateTime();
    const QString raw = rawMessage.trimmed();

    if (!ex || !isProviderJob(kind)) {
        e.message = raw.isEmpty() ? QCoreApplication::translate("JobErrors", "Ismeretlen hiba történt.") : raw;
        return e;
    }

    e.detail = httpFailureDetail(*ex);
    const int st = ex->status;
    if (st <= 0) {
        e.message = QCoreApplication::translate("JobErrors", "Nem sikerült elérni a szolgáltatót. Ellenőrizd a hálózati kapcsolatot "
                        "és a szolgáltató címét.");
        e.fixActionHint = settingsHint(kind);
    } else if (st == 401 || st == 403) {
        e.message = QCoreApplication::translate("JobErrors", "A szolgáltató nem fogadta el az API-kulcsot. Ellenőrizd vagy cseréld "
                        "le a kulcsot a Beállításokban.");
        e.fixActionHint = settingsHint(kind);
    } else if (st == 402) {
        e.message = QCoreApplication::translate("JobErrors", "A szolgáltatónál elfogyott az egyenleg vagy a keret.");
        e.fixActionHint = settingsHint(kind);
    } else if (st == 404) {
        e.message = QCoreApplication::translate("JobErrors", "A szolgáltató nem ismeri a beállított modellt vagy címet.");
        e.fixActionHint = settingsHint(kind);
    } else if (st == 408 || st == 504) {
        e.message = QCoreApplication::translate("JobErrors", "A szolgáltató nem válaszolt időben. Próbáld újra.");
    } else if (st == 413) {
        e.message = QCoreApplication::translate("JobErrors", "A szolgáltató szerint túl nagy a kérés (túl hosszú felvétel vagy átirat).");
    } else if (st == 429) {
        e.message = QCoreApplication::translate("JobErrors", "A szolgáltató átmenetileg korlátozza a kéréseket (túl sok kérés vagy "
                        "elfogyott keret). Próbáld újra később.");
    } else if (st >= 500) {
        e.message = QCoreApplication::translate("JobErrors", "A szolgáltatónál hiba történt. Próbáld újra később.");
    } else if (st >= 400) {
        e.message = raw.isEmpty() ? QCoreApplication::translate("JobErrors", "A szolgáltató elutasította a kérést.") : raw;
    } else {
        // 2xx válasz után bukott (pl. értelmezhetetlen tartalom): a nyers szöveg a magyarázat,
        // a HTTP-sor félrevezető lenne.
        e.message = raw.isEmpty() ? QCoreApplication::translate("JobErrors", "Ismeretlen hiba történt.") : raw;
        e.detail.clear();
    }
    return e;
}

JobError describeCloudFailure(JobKind kind, const CloudError& ce)
{
    JobError e;
    e.kind = kind;
    e.when = QDateTime::currentDateTime();
    if (ce.kind == CloudErrorKind::Network || ce.httpStatus <= 0) {
        e.message = QCoreApplication::translate("JobErrors", "Nem sikerült elérni a Tanara Cloudot. Ellenőrizd a hálózati kapcsolatot.");
        e.detail = ce.networkError.isEmpty() ? QCoreApplication::translate("JobErrors", "hálózat · nincs válasz")
                                             : QCoreApplication::translate("JobErrors", "hálózat · %1").arg(oneLine(ce.networkError));
        return e;
    }
    e.message = ce.message.isEmpty() ? QCoreApplication::translate("JobErrors", "A Tanara Cloud hibát jelzett.") : ce.message;
    QStringList parts{QStringLiteral("HTTP %1").arg(ce.httpStatus)};
    if (!ce.code.isEmpty())
        parts << ce.code;
    if (!ce.requestId.isEmpty())
        parts << ce.requestId;
    e.detail = parts.join(QStringLiteral(" · "));
    e.fixActionHint = QStringLiteral("cloud");
    return e;
}

} // namespace tanara
