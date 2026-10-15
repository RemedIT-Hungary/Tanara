#include "tanara/library/jobs/JobErrors.h"
#include "tanara/providers/chat/LlmContext.h"
#include "tanara/foundation/net/NetworkErrorText.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace tanara::library {

namespace {

// A one-line, shortened text for the technical line.
QString oneLine(const QString& s, int maxLen = 200)
{
    QString t = s.simplified();
    if (t.size() > maxLen)
        t = t.left(maxLen - 1) + QChar(0x2026);
    return t;
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

QString tokens(int n) { return QStringLiteral("%L1").arg(n); }

} // namespace

int contextFixTokens(const QString& hint)
{
    if (!hint.contains(QLatin1String("-context"))) return 0;
    const int colon = hint.lastIndexOf(QLatin1Char(':'));
    bool ok = false;
    const int n = colon >= 0 ? hint.mid(colon + 1).toInt(&ok) : 0;
    return ok && n > 0 ? n : 0;
}

bool isReloadContextHint(const QString& hint)
{
    return hint.startsWith(QLatin1String("llm:reload-context"));
}

JobError describeContextOverflow(JobKind kind, const providers::llmctx::ContextOverflow& ov,
                                 const ContextFailureHint& hint, int httpStatus)
{
    JobError e;
    e.kind = kind;
    e.when = QDateTime::currentDateTime();

    // The recommended context: the caller's estimate, otherwise the request + a usual output budget;
    // in any case a step larger than the current one.
    int rec = hint.recommendedContext;
    if (rec <= 0 && ov.promptTokens > 0) rec = providers::llmctx::contextStepFor(ov.promptTokens + 4096);
    if (ov.contextTokens > 0 && rec > 0 && rec <= ov.contextTokens)
        rec = providers::llmctx::contextStepFor(ov.contextTokens + 1);
    if (rec > 0) rec = providers::llmctx::contextStepFor(rec);

    QString msg;
    if (ov.contextTokens > 0 && ov.promptTokens > 0)
        msg = QCoreApplication::translate("JobErrors", "A modell %1 tokenes kontextussal van betöltve, a kérés %2 token volt — nem fér bele.")
                  .arg(tokens(ov.contextTokens), tokens(ov.promptTokens));
    else if (ov.contextTokens > 0)
        msg = QCoreApplication::translate("JobErrors", "A modell %1 tokenes kontextussal van betöltve, és a kérés nem fért bele.")
                  .arg(tokens(ov.contextTokens));
    else
        msg = QCoreApplication::translate("JobErrors", "A kérés nem fért bele a modell kontextusába.");

    if (hint.cloud) {
        msg += QLatin1Char(' ') + QCoreApplication::translate("JobErrors", "A Tanara Cloud modellje ekkora kérést nem tud feldolgozni.");
        e.fixActionHint = QStringLiteral("cloud");
    } else if (hint.lmStudio) {
        msg += QLatin1Char(' ') + (rec > 0
            ? QCoreApplication::translate("JobErrors", "A Tanara újra tudja tölteni az LM Studióban legalább %1 tokenes kontextussal, és újraindítja a feladatot.").arg(tokens(rec))
            : QCoreApplication::translate("JobErrors", "Töltsd be nagyobb kontextussal, és próbáld újra."));
        e.fixActionHint = rec > 0 ? QStringLiteral("llm:reload-context:%1").arg(rec)
                                  : QStringLiteral("settings:llm-context");
    } else {
        msg += QLatin1Char(' ') + (rec > 0
            ? QCoreApplication::translate("JobErrors", "Töltsd be a modellt a szerveren legalább %1 tokenes kontextussal, vagy válassz nagyobb kontextusú modellt.").arg(tokens(rec))
            : QCoreApplication::translate("JobErrors", "Töltsd be a modellt a szerveren nagyobb kontextussal, vagy válassz nagyobb kontextusú modellt."));
        e.fixActionHint = rec > 0 ? QStringLiteral("settings:llm-context:%1").arg(rec)
                                  : QStringLiteral("settings:llm-context");
    }
    e.message = msg;

    QStringList parts;
    if (httpStatus > 0) parts << QStringLiteral("HTTP %1").arg(httpStatus);
    parts << (ov.code.isEmpty() ? QStringLiteral("context overflow") : ov.code);
    if (ov.promptTokens > 0)
        parts << QCoreApplication::translate("JobErrors", "kérés %1 token").arg(ov.promptTokens);
    if (ov.contextTokens > 0)
        parts << QCoreApplication::translate("JobErrors", "kontextus %1 token").arg(ov.contextTokens);
    e.detail = parts.join(QStringLiteral(" · "));
    return e;
}

QString httpFailureDetail(const net::HttpExchange& ex)
{
    return net::failureDetail(ex);
}

JobError describeJobFailure(JobKind kind, const QString& rawMessage, const net::HttpExchange* ex,
                            const ContextFailureHint* context)
{
    JobError e;
    e.kind = kind;
    e.when = QDateTime::currentDateTime();
    const QString raw = rawMessage.trimmed();

    // Every form of the "does not fit the context" error (LM Studio / llama.cpp / OpenAI / vLLM) gets
    // its own explanation with the numbers written out; the raw JSON does not reach the user.
    if (kind != JobKind::Transcribe && kind != JobKind::Mixdown && kind != JobKind::Import
        && kind != JobKind::Identify && kind != JobKind::Export) {
        const providers::llmctx::ContextOverflow ov =
            providers::llmctx::parseContextOverflow(ex ? ex->body : QByteArray(), raw);
        if (ov.matched)
            return describeContextOverflow(kind, ov, context ? *context : ContextFailureHint{},
                                           ex ? ex->status : 0);
    }

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
        // Failed after a 2xx response (e.g. unparseable content): the raw text is the explanation; the HTTP
        // line would be misleading.
        e.message = raw.isEmpty() ? QCoreApplication::translate("JobErrors", "Ismeretlen hiba történt.") : raw;
        e.detail.clear();
    }
    return e;
}

JobError describeCloudFailure(JobKind kind, const Error& ce)
{
    JobError e;
    e.kind = kind;
    e.when = QDateTime::currentDateTime();
    if (ce.httpStatus <= 0) {
        e.message = QCoreApplication::translate("JobErrors", "Nem sikerült elérni a Tanara Cloudot. Ellenőrizd a hálózati kapcsolatot.");
        const QString net = oneLine(net::networkErrorText(ce.networkErrorCode, ce.detail));
        e.detail = net.isEmpty() ? QCoreApplication::translate("JobErrors", "hálózat · nincs válasz")
                                 : QCoreApplication::translate("JobErrors", "hálózat · %1").arg(net);
        return e;
    }
    const providers::llmctx::ContextOverflow ov =
        providers::llmctx::parseContextOverflow(QByteArray(), ce.code + QLatin1Char(' ') + ce.message);
    if (ov.matched && kind != JobKind::Transcribe) {
        ContextFailureHint hint;
        hint.cloud = true;
        JobError c = describeContextOverflow(kind, ov, hint, ce.httpStatus);
        if (!ce.requestId.isEmpty()) c.detail += QStringLiteral(" · ") + ce.requestId;
        return c;
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

} // namespace tanara::library