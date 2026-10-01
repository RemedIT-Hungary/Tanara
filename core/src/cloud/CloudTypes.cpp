#include "tanara/cloud/CloudTypes.h"
#include "tanara/Types.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

#include <cmath>
#include <cstdlib>

namespace tanara {

namespace {


QDateTime parseTime(const QJsonValue& v)
{
    if (!v.isString()) return {};
    return QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
}

QString nullableString(const QJsonValue& v) { return v.isString() ? v.toString() : QString(); }

// Ezres tagolás egy nem-negatív egész számjegy-sorozatra.
QString groupThousands(const QString& digits, const QChar sep)
{
    QString out;
    const int n = digits.size();
    for (int i = 0; i < n; ++i) {
        out += digits.at(i);
        const int rest = n - 1 - i;
        if (rest > 0 && rest % 3 == 0) out += sep;
    }
    return out;
}

// |micros| → „1 284,50” / „1,284.50” adott tizedes-számmal (a kerekítést a hívó végezte:
// `units` már a kívánt tizedesjegyekre skálázott egész).
QString renderUnits(qint64 units, int decimals, const QString& lang)
{
    const bool hu = lang == QLatin1String("hu");
    qint64 scale = 1;
    for (int i = 0; i < decimals; ++i) scale *= 10;
    const qint64 whole = units / scale;
    const qint64 frac  = units % scale;
    QString s = groupThousands(QString::number(whole), hu ? QChar(0x00A0) : QLatin1Char(','));
    if (decimals > 0)
        s += (hu ? QLatin1Char(',') : QLatin1Char('.'))
             + QStringLiteral("%1").arg(frac, decimals, 10, QLatin1Char('0'));
    return s;
}

} // namespace

// ---- pénz ----------------------------------------------------------------------------

Money moneyFromJson(const QJsonValue& v)
{
    Money m;
    if (!v.isObject()) return m;
    const QJsonObject o = v.toObject();
    const QJsonValue a = o.value(QStringLiteral("amount_micros"));
    if (!a.isDouble()) return m;
    m.micros   = static_cast<qint64>(std::llround(a.toDouble()));
    m.currency = o.value(QStringLiteral("currency")).toString(QStringLiteral("USD"));
    return m;
}

QString formatMoney(const Money& m, MoneyStyle style, const QString& lang)
{
    if (!m.isValid()) return QStringLiteral("—");
    const bool neg = m.micros < 0;
    const qint64 absMicros = neg ? -m.micros : m.micros;
    const bool usd = m.currency == QLatin1String("USD");

    QString number;
    bool lessThan = false;
    if (style == MoneyStyle::Balance) {
        // Egyenleg: 2 tizedes, LEFELÉ (sose mutassunk többet, mint amennyi van).
        const qint64 cents = neg ? -static_cast<qint64>(std::floor(-static_cast<double>(absMicros) / 10000.0))
                                 : absMicros / 10000;
        number = renderUnits(cents, 2, lang);
    } else if (absMicros == 0) {
        number = renderUnits(0, 2, lang);
    } else if (absMicros < 100) {
        lessThan = true;
        number = renderUnits(1, 4, lang);   // 0,0001
    } else if (absMicros < 10000) {
        number = renderUnits((absMicros + 50) / 100, 4, lang);   // 4 tizedes, normál kerekítés
    } else {
        number = renderUnits((absMicros + 5000) / 10000, 2, lang);
    }

    QString s = usd ? QStringLiteral("$") + number
                    : number + QChar(0x00A0) + m.currency;
    if (lessThan) s = QStringLiteral("< ") + s;
    if (neg && number != renderUnits(0, 2, lang)) s = QChar(0x2212) + s;
    return s;
}

QString formatHours(double hours, const QString& lang)
{
    const bool hu = lang == QLatin1String("hu");
    if (hours < 0) hours = 0;
    if (hours >= 10.0)
        return QCoreApplication::translate("Cloud", "%1 óra").arg(static_cast<qint64>(std::floor(hours)));
    if (hours >= 1.0) {
        const double t = std::floor(hours * 10.0) / 10.0;
        QString n = QString::number(t, 'f', 1);
        if (n.endsWith(QLatin1String(".0"))) n.chop(2);
        if (hu) n.replace(QLatin1Char('.'), QLatin1Char(','));
        return QCoreApplication::translate("Cloud", "%1 óra").arg(n);
    }
    return QCoreApplication::translate("Cloud", "%1 perc").arg(static_cast<int>(std::floor(hours * 60.0)));
}

QString formatDuration(qint64 ms)
{
    if (ms < 0) ms = 0;
    const qint64 s = ms / 1000;
    const qint64 h = s / 3600, mnt = (s % 3600) / 60, sec = s % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(mnt, 2, 10, QLatin1Char('0')).arg(sec, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(mnt).arg(sec, 2, 10, QLatin1Char('0'));
}

QString vatLabel(const QString& vatMode)
{
    if (vatMode == QLatin1String("reverse_charge"))
        return QCoreApplication::translate("Cloud", "ÁFA nélkül, fordított adózás");
    return QCoreApplication::translate("Cloud", "ÁFA-t tartalmaz");
}

bool semverLess(const QString& a, const QString& b)
{
    const QStringList x = a.trimmed().split(QLatin1Char('.'));
    const QStringList y = b.trimmed().split(QLatin1Char('.'));
    for (int i = 0; i < 3; ++i) {
        const int xi = x.value(i).toInt();   // nem szám / hiányzó → 0
        const int yi = y.value(i).toInt();
        if (xi != yi) return xi < yi;
    }
    return false;
}

QString clientPlatform()
{
#if defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("unknown");
#endif
}

QString clientHeaderValue()
{
    return libraryVersion() + QLatin1Char('/') + clientPlatform();
}

// ---- hibák ---------------------------------------------------------------------------

CloudError parseCloudError(int httpStatus, const QHash<QByteArray, QByteArray>& headers,
                           const QByteArray& body, const QString& networkError)
{
    CloudError e;
    e.httpStatus = httpStatus;
    if (httpStatus == 0) {
        e.kind = CloudErrorKind::Network;
        e.networkError = networkError;
        return e;
    }
    if (httpStatus >= 200 && httpStatus < 300)
        return e;   // None

    e.requestId = QString::fromUtf8(headers.value(QByteArrayLiteral("x-tanara-request-id")));
    const QJsonObject err = QJsonDocument::fromJson(body).object().value(QStringLiteral("error")).toObject();
    e.code    = err.value(QStringLiteral("code")).toString();
    e.message = err.value(QStringLiteral("message")).toString();
    if (err.contains(QStringLiteral("request_id")))
        e.requestId = err.value(QStringLiteral("request_id")).toString();
    e.supportUrl       = err.value(QStringLiteral("support_url")).toString();
    e.suspensionReason = err.value(QStringLiteral("suspension_reason")).toString();
    e.termsVersion     = err.value(QStringLiteral("terms_version")).toString();
    e.termsUrl         = err.value(QStringLiteral("terms_url")).toString();
    e.balance          = moneyFromJson(err.value(QStringLiteral("balance")));
    e.needed           = moneyFromJson(err.value(QStringLiteral("needed")));
    e.neededBasis      = err.value(QStringLiteral("needed_basis")).toString();
    e.topupUrl         = nullableString(err.value(QStringLiteral("topup_url")));
    e.contactUrl       = err.value(QStringLiteral("contact_url")).toString();
    e.minClient        = err.value(QStringLiteral("min_client")).toString();
    e.downloadUrl      = err.value(QStringLiteral("download_url")).toString();
    e.settingsUrl      = err.value(QStringLiteral("settings_url")).toString();
    e.retryAfterSec    = err.value(QStringLiteral("retry_after")).toInt(0);
    if (e.retryAfterSec <= 0)
        e.retryAfterSec = QString::fromLatin1(headers.value(QByteArrayLiteral("retry-after"))).toInt();
    const QJsonObject lim = err.value(QStringLiteral("limit")).toObject();
    if (!lim.isEmpty()) {
        e.limitPeriod   = lim.value(QStringLiteral("period")).toString();
        e.limitAmount   = moneyFromJson(lim.value(QStringLiteral("amount")));
        e.limitResetsAt = parseTime(lim.value(QStringLiteral("resets_at")));
    }
    const QJsonObject win = err.value(QStringLiteral("window")).toObject();
    if (!win.isEmpty()) {
        e.windowStart = parseTime(win.value(QStringLiteral("starts_at")));
        e.windowEnd   = parseTime(win.value(QStringLiteral("ends_at")));
    }
    const QJsonObject fields = err.value(QStringLiteral("fields")).toObject();
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
        e.fieldErrors.insert(it.key(), it.value().toObject().value(QStringLiteral("code")).toString());
    if (e.minClient.isEmpty() && httpStatus == 426)
        e.minClient = QString::fromUtf8(headers.value(QByteArrayLiteral("x-tanara-min-client")));

    const QString& c = e.code;
    if (httpStatus == 401)                                   e.kind = CloudErrorKind::Unauthorized;
    else if (httpStatus == 402 || c == QLatin1String("insufficient_balance")) e.kind = CloudErrorKind::InsufficientBalance;
    else if (c == QLatin1String("terms_acceptance_required")) e.kind = CloudErrorKind::TermsRequired;
    else if (c == QLatin1String("account_suspended"))        e.kind = CloudErrorKind::Suspended;
    else if (httpStatus == 426)                              e.kind = CloudErrorKind::ClientTooOld;
    else if (c == QLatin1String("spend_limit_reached"))      e.kind = CloudErrorKind::SpendLimit;
    else if (httpStatus == 429)                              e.kind = CloudErrorKind::RateLimited;
    else if (c == QLatin1String("maintenance"))              e.kind = CloudErrorKind::Maintenance;
    else if (httpStatus == 503)                              e.kind = CloudErrorKind::Upstream;
    else if (httpStatus >= 500)                              e.kind = CloudErrorKind::Internal;
    else if (c == QLatin1String("disposable_email"))         e.kind = CloudErrorKind::DisposableEmail;
    else if (c == QLatin1String("authorization_pending") || c == QLatin1String("access_denied")
             || c == QLatin1String("expired_token") || c == QLatin1String("invalid_device_code"))
                                                             e.kind = CloudErrorKind::DeviceFlow;
    else if (httpStatus == 404)                              e.kind = CloudErrorKind::NotFound;
    else if (httpStatus == 409 && c == QLatin1String("topup_unavailable")) e.kind = CloudErrorKind::TopupUnavailable;
    else if (httpStatus == 422)                              e.kind = CloudErrorKind::TopupLimit;
    else if (httpStatus == 400)                              e.kind = CloudErrorKind::Validation;
    else if (httpStatus == 403)                              e.kind = CloudErrorKind::Suspended;
    else                                                     e.kind = CloudErrorKind::Other;
    return e;
}

QString describeCloudError(const CloudError& e, const Money& charged, const QString& lang)
{
    QStringList lines;
    auto m = [&](const Money& x, MoneyStyle st = MoneyStyle::Balance) { return formatMoney(x, st, lang); };
    switch (e.kind) {
    case CloudErrorKind::Network:
        lines << QCoreApplication::translate("Cloud", "Nem értük el a szervert. Ellenőrizd az internetkapcsolatot.");
        break;
    case CloudErrorKind::InsufficientBalance:
        lines << QCoreApplication::translate("Cloud", "Nincs elég egyenleg.");
        lines << (e.neededBasis == QLatin1String("hold")
                      ? QCoreApplication::translate("Cloud", "Ehhez a lépéshez legalább %1 fedezet kell az egyenlegeden. A tényleges díj ennél kevesebb lehet.").arg(m(e.needed, MoneyStyle::Charge))
                      : QCoreApplication::translate("Cloud", "Ehhez a lépéshez %1 kell.").arg(m(e.needed, MoneyStyle::Charge)));
        lines << QCoreApplication::translate("Cloud", "Az egyenleged: %1.").arg(m(e.balance));
        lines << (!e.topupUrl.isEmpty() ? QCoreApplication::translate("Cloud", "Feltöltés: %1").arg(e.topupUrl) : QCoreApplication::translate("Cloud", "A feltöltéshez írj nekünk: %1").arg(e.contactUrl));
        break;
    case CloudErrorKind::ClientTooOld:
        lines << QCoreApplication::translate("Cloud", "Frissítsd a Tanarát a Tanara Cloudhoz: legalább %1 kell; neked %2 van. A saját kulcsos mód addig is működik.")
                     .arg(e.minClient, libraryVersion());
        if (!e.downloadUrl.isEmpty()) lines << QCoreApplication::translate("Cloud", "Letöltés: %1").arg(e.downloadUrl);
        break;
    case CloudErrorKind::Maintenance:
        lines << QCoreApplication::translate("Cloud", "Karbantartás miatt a Tanara Cloud most nem elérhető.") + (e.message.isEmpty() ? QString() : QStringLiteral(" ") + e.message);
        if (e.windowStart.isValid())
            lines << QCoreApplication::translate("Cloud", "Tervezett karbantartás %1–%2 között.").arg(e.windowStart.toLocalTime().toString(QStringLiteral("HH:mm")),
                                                                   e.windowEnd.toLocalTime().toString(QStringLiteral("HH:mm")));
        break;
    case CloudErrorKind::Upstream:
        lines << QCoreApplication::translate("Cloud", "A feldolgozó szolgáltatás átmenetileg nem elérhető. Próbáld újra később.");
        break;
    case CloudErrorKind::RateLimited:
        lines << QCoreApplication::translate("Cloud", "Túl sok kérés érkezett. Próbáld újra %1 mp múlva.").arg(qMax(1, e.retryAfterSec));
        break;
    case CloudErrorKind::SpendLimit:
        lines << QCoreApplication::translate("Cloud", "Elérted ennek az eszköznek a költési limitjét.");
        if (!e.settingsUrl.isEmpty()) lines << QCoreApplication::translate("Cloud", "Beállítások a weben: %1").arg(e.settingsUrl);
        break;
    case CloudErrorKind::Unauthorized:
        lines << QCoreApplication::translate("Cloud", "Ezt az eszközt leválasztották a fiókodról. Jelentkezz be újra.");
        break;
    case CloudErrorKind::Suspended:
        lines << (e.suspensionReason == QLatin1String("payment_dispute")
                      ? QCoreApplication::translate("Cloud", "A fiókod fizetési vita miatt fel van függesztve. Részletek a weben.")
                      : QCoreApplication::translate("Cloud", "A Tanara Cloud fiókod fel van függesztve. Kérdés esetén írj a supportnak."));
        break;
    case CloudErrorKind::TermsRequired:
        lines << QCoreApplication::translate("Cloud", "A feldolgozáshoz el kell fogadnod az új ÁSZF-et (%1): %2").arg(e.termsVersion, e.termsUrl);
        break;
    default:
        lines << (e.message.isEmpty() ? QCoreApplication::translate("Cloud", "A feldolgozás hibával leállt.") : e.message);
        break;
    }
    if (charged.isValid() && charged.micros > 0)
        lines << QCoreApplication::translate("Cloud", "Az eddig elkészült részek díja: %1. Folytathatod, ekkor csak a hátralévő részekért fizetsz.")
                     .arg(m(charged, MoneyStyle::Charge));
    else if (e.kind != CloudErrorKind::Network)
        lines << QCoreApplication::translate("Cloud", "Nem terheltünk semmit.");
    if (!e.requestId.isEmpty())
        lines << QCoreApplication::translate("Cloud", "Hibaazonosító: %1 — ha írsz nekünk, küldd el ezt is. A meeting tartalmát nem látjuk.").arg(e.requestId);
    return lines.join(QLatin1Char('\n'));
}

// ---- fiók ----------------------------------------------------------------------------

TermsStatus termsFromJson(const QJsonObject& o)
{
    TermsStatus t;
    t.acceptedVersion = nullableString(o.value(QStringLiteral("accepted_version")));
    t.currentVersion  = o.value(QStringLiteral("current_version")).toString();
    t.currentUrl      = o.value(QStringLiteral("current_url")).toString();
    const QJsonValue up = o.value(QStringLiteral("upcoming"));
    if (up.isObject()) {
        const QJsonObject u = up.toObject();
        t.hasUpcoming           = true;
        t.upcomingVersion       = u.value(QStringLiteral("version")).toString();
        t.upcomingEffectiveFrom = parseTime(u.value(QStringLiteral("effective_from")));
        t.upcomingUrl           = u.value(QStringLiteral("url")).toString();
        t.upcomingSummary       = u.value(QStringLiteral("summary")).toString();
    }
    return t;
}

AccountInfo accountFromJson(const QJsonObject& o)
{
    AccountInfo a;
    a.email   = o.value(QStringLiteral("email")).toString();
    a.balance = moneyFromJson(o.value(QStringLiteral("balance")));
    a.valid   = a.balance.isValid();
    a.vatMode = o.value(QStringLiteral("vat_mode")).toString(QStringLiteral("gross"));
    a.lowBalanceThreshold = moneyFromJson(o.value(QStringLiteral("low_balance_threshold")));
    a.lowBalance      = o.value(QStringLiteral("low_balance")).toBool();
    a.lowBalanceSince = parseTime(o.value(QStringLiteral("low_balance_since")));
    a.balanceEmpty    = o.value(QStringLiteral("balance_empty")).toBool(a.balance.isValid() && a.balance.micros <= 0);
    const QJsonObject hl = o.value(QStringLiteral("hours_left")).toObject();
    a.hoursFast       = hl.value(QStringLiteral("fast")).toDouble();
    a.hoursAccurate   = hl.value(QStringLiteral("accurate")).toDouble();
    a.topupAvailable  = o.value(QStringLiteral("topup_available")).toBool();
    a.contactUrl      = o.value(QStringLiteral("contact_url")).toString();
    a.dashboardUrl    = o.value(QStringLiteral("dashboard_url")).toString();
    a.usageUrl        = o.value(QStringLiteral("usage_url")).toString();
    a.trial           = o.value(QStringLiteral("trial")).toString();
    a.terms           = termsFromJson(o.value(QStringLiteral("terms")).toObject());
    for (const QJsonValue& v : o.value(QStringLiteral("notices")).toArray()) {
        const QJsonObject n = v.toObject();
        a.notices.append({ n.value(QStringLiteral("id")).toString(),
                           n.value(QStringLiteral("level")).toString(QStringLiteral("info")),
                           n.value(QStringLiteral("message")).toString(),
                           nullableString(n.value(QStringLiteral("url"))) });
    }
    return a;
}

// ---- katalógus -----------------------------------------------------------------------

bool CloudModel::notRecommendedFor(const QString& lang) const
{
    if (lang.isEmpty() || lang == QLatin1String("auto")) return false;
    if (hiddenForLanguages.contains(lang)) return true;
    return !allLanguages && !languages.contains(lang);
}

QVector<CloudModel> modelsFromJson(const QJsonObject& root)
{
    QVector<CloudModel> out;
    for (const QJsonValue& v : root.value(QStringLiteral("data")).toArray()) {
        const QJsonObject o = v.toObject();
        const QJsonObject t = o.value(QStringLiteral("tanara")).toObject();
        if (t.isEmpty()) continue;   // Tanara-metaadat nélküli modell nem használható
        CloudModel m;
        m.id          = o.value(QStringLiteral("id")).toString();
        m.kind        = t.value(QStringLiteral("kind")).toString();
        m.tier        = nullableString(t.value(QStringLiteral("tier")));
        m.isVirtual   = t.value(QStringLiteral("virtual")).toBool();
        m.expert      = t.value(QStringLiteral("expert")).toBool();
        m.displayName = t.value(QStringLiteral("display_name")).toString(m.id);
        m.diarization = t.value(QStringLiteral("diarization")).toBool(true);
        const QJsonValue langs = t.value(QStringLiteral("languages"));
        m.allLanguages = !langs.isArray();
        for (const QJsonValue& l : langs.toArray()) m.languages << l.toString();
        for (const QJsonValue& l : t.value(QStringLiteral("hidden_for_languages")).toArray())
            m.hiddenForLanguages << l.toString();
        const QJsonObject price = t.value(QStringLiteral("price")).toObject();
        m.perHour    = moneyFromJson(price.value(QStringLiteral("per_hour")));
        m.perMInput  = moneyFromJson(price.value(QStringLiteral("per_1m_input_tokens")));
        m.perMOutput = moneyFromJson(price.value(QStringLiteral("per_1m_output_tokens")));
        if (!m.id.isEmpty()) out.append(m);
    }
    return out;
}

std::optional<CloudModel> findTierModel(const QVector<CloudModel>& models, const QString& kind,
                                        const QString& tier)
{
    for (const CloudModel& m : models)
        if (m.kind == kind && m.isVirtual && m.tier == tier) return m;
    return std::nullopt;
}

std::optional<CloudModel> findModel(const QVector<CloudModel>& models, const QString& id)
{
    for (const CloudModel& m : models)
        if (m.id == id) return m;
    return std::nullopt;
}

QString virtualModelId(const QString& kind, const QString& tier)
{
    const QString t = tier == QLatin1String("fast") ? QStringLiteral("fast") : QStringLiteral("accurate");
    return kind == QLatin1String("stt") ? QStringLiteral("tanara/stt-") + t
                                        : QStringLiteral("tanara/summary-") + t;
}

// ---- becslés -------------------------------------------------------------------------

QJsonObject estimateRequestToJson(const EstimateRequest& r)
{
    QJsonObject o;
    o.insert(QStringLiteral("task"), r.task);
    o.insert(QStringLiteral("duration_ms"), static_cast<double>(r.durationMs));
    if (r.tracks > 0)                o.insert(QStringLiteral("tracks"), r.tracks);
    if (!r.sttModel.isEmpty())       o.insert(QStringLiteral("stt_model"), r.sttModel);
    if (!r.llmModel.isEmpty())       o.insert(QStringLiteral("llm_model"), r.llmModel);
    if (!r.summaryMode.isEmpty())    o.insert(QStringLiteral("summary_mode"), r.summaryMode);
    if (!r.language.isEmpty())       o.insert(QStringLiteral("language"), r.language);
    if (r.transcriptChars >= 0)      o.insert(QStringLiteral("transcript_chars"), r.transcriptChars);
    if (!r.llmCalls.isEmpty()) {
        QJsonArray calls;
        for (const LlmCallGroup& g : r.llmCalls) {
            QJsonObject c;
            c.insert(QStringLiteral("count"), g.count < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(g.count));
            c.insert(QStringLiteral("input_chars"), g.inputChars);
            c.insert(QStringLiteral("max_tokens"), g.maxTokens < 1 ? 1 : g.maxTokens);
            calls.append(c);
        }
        o.insert(QStringLiteral("llm_calls"), calls);
    }
    return o;
}

EstimateResult estimateFromJson(const QJsonObject& o)
{
    EstimateResult e;
    e.estimate     = moneyFromJson(o.value(QStringLiteral("estimate")));
    e.low          = moneyFromJson(o.value(QStringLiteral("low")));
    e.high         = moneyFromJson(o.value(QStringLiteral("high")));
    e.balance      = moneyFromJson(o.value(QStringLiteral("balance")));
    e.required     = moneyFromJson(o.value(QStringLiteral("required")));
    e.balanceAfter = moneyFromJson(o.value(QStringLiteral("balance_after")));
    e.enough       = o.value(QStringLiteral("enough")).toBool();
    e.lowBalanceAfter = o.value(QStringLiteral("low_balance_after")).toBool();
    e.vatMode      = o.value(QStringLiteral("vat_mode")).toString(QStringLiteral("gross"));
    for (const QJsonValue& v : o.value(QStringLiteral("breakdown")).toArray()) {
        const QJsonObject b = v.toObject();
        EstimateLine l;
        l.item        = b.value(QStringLiteral("item")).toString();
        l.model       = b.value(QStringLiteral("model")).toString();
        l.tier        = nullableString(b.value(QStringLiteral("tier")));
        l.summaryMode = nullableString(b.value(QStringLiteral("summary_mode")));
        l.amount      = moneyFromJson(b.value(QStringLiteral("amount")));
        l.exact       = b.value(QStringLiteral("exact")).toBool();
        e.breakdown.append(l);
    }
    e.valid = e.estimate.isValid() && e.balance.isValid();
    return e;
}

EstimateLevel estimateLevel(const EstimateResult& e)
{
    if (e.enough) return EstimateLevel::Enough;
    if (e.balance.micros >= e.estimate.micros) return EstimateLevel::LittleReserve;
    return EstimateLevel::NotEnough;
}

// ---- terhelés / visszaírás -----------------------------------------------------------

ChargeInfo chargeInfoFromJson(const QJsonObject& o)
{
    ChargeInfo c;
    if (o.isEmpty()) return c;
    c.chargeId     = o.value(QStringLiteral("charge_id")).toString();
    c.charge       = moneyFromJson(o.value(QStringLiteral("charge")));
    c.balance      = moneyFromJson(o.value(QStringLiteral("balance")));
    c.lowBalance   = o.value(QStringLiteral("low_balance")).toBool();
    c.balanceEmpty = o.value(QStringLiteral("balance_empty")).toBool();
    c.vatMode      = o.value(QStringLiteral("vat_mode")).toString();
    c.jobId        = nullableString(o.value(QStringLiteral("job_id")));
    c.valid        = c.charge.isValid();
    return c;
}

ChargeInfo chargeInfoFromHeaders(const QHash<QByteArray, QByteArray>& headers)
{
    ChargeInfo c;
    const QByteArray ch  = headers.value(QByteArrayLiteral("x-tanara-charge-micros"));
    const QByteArray bal = headers.value(QByteArrayLiteral("x-tanara-balance-micros"));
    if (ch.isEmpty()) return c;
    const QString cur = QString::fromLatin1(headers.value(QByteArrayLiteral("x-tanara-currency"), QByteArrayLiteral("USD")));
    c.charge = { ch.toLongLong(), cur };
    if (!bal.isEmpty()) c.balance = { bal.toLongLong(), cur };
    c.balanceEmpty = c.balance.isValid() && c.balance.micros <= 0;
    c.valid = true;
    return c;
}

TranscriptionJobInfo transcriptionJobInfoFromJson(const QJsonObject& root)
{
    TranscriptionJobInfo j;
    j.status = root.value(QStringLiteral("status")).toString();
    const QJsonObject t = root.value(QStringLiteral("tanara")).toObject();
    if (t.isEmpty()) return j;
    j.valid          = true;
    j.refunded       = t.value(QStringLiteral("refunded")).toBool();
    j.charge         = moneyFromJson(t.value(QStringLiteral("charge")));
    j.refund         = moneyFromJson(t.value(QStringLiteral("refund")));
    j.balance        = moneyFromJson(t.value(QStringLiteral("balance")));
    j.errorCode      = nullableString(t.value(QStringLiteral("error_code")));
    j.contentDeleted = t.value(QStringLiteral("content_deleted")).toBool();
    j.jobId          = nullableString(t.value(QStringLiteral("job_id")));
    if (j.refunded && !j.refund.isValid()) j.refund = j.charge;   // hiányzó refund → a terhelés
    return j;
}

// ---- device flow ---------------------------------------------------------------------

DeviceCode deviceCodeFromJson(const QJsonObject& o)
{
    DeviceCode d;
    d.deviceCode              = o.value(QStringLiteral("device_code")).toString();
    d.userCode                = o.value(QStringLiteral("user_code")).toString();
    d.verificationUri         = o.value(QStringLiteral("verification_uri")).toString();
    d.verificationUriComplete = o.value(QStringLiteral("verification_uri_complete")).toString(d.verificationUri);
    d.interval                = qMax(1, o.value(QStringLiteral("interval")).toInt(5));
    d.expiresIn               = qMax(1, o.value(QStringLiteral("expires_in")).toInt(600));
    return d;
}

// ---- várólista -----------------------------------------------------------------------

QJsonObject waitlistToJson(const WaitlistSignup& s)
{
    QJsonObject o;
    o.insert(QStringLiteral("email"), s.email.trimmed());
    o.insert(QStringLiteral("consent"), s.consent);
    o.insert(QStringLiteral("source"), QStringLiteral("client"));
    if (s.uiLanguage == QLatin1String("hu") || s.uiLanguage == QLatin1String("en"))
        o.insert(QStringLiteral("ui_language"), s.uiLanguage);
    if (!s.useCase.isEmpty())
        o.insert(QStringLiteral("use_case"), s.useCase);
    if (!s.meetingLanguages.isEmpty())
        o.insert(QStringLiteral("meeting_languages"), QJsonArray::fromStringList(s.meetingLanguages));
    o.insert(QStringLiteral("platform"), s.platform.isEmpty() ? clientPlatform() : s.platform);
    o.insert(QStringLiteral("client_version"), s.clientVersion.isEmpty() ? libraryVersion() : s.clientVersion);
    return o;
}

QString validateWaitlist(const WaitlistSignup& s)
{
    const QString e = s.email.trimmed();
    const int at = e.indexOf(QLatin1Char('@'));
    if (at <= 0 || at != e.lastIndexOf(QLatin1Char('@')) || e.contains(QLatin1Char(' '))
        || !e.mid(at + 1).contains(QLatin1Char('.')) || e.endsWith(QLatin1Char('.')))
        return QStringLiteral("email");
    if (!s.consent) return QStringLiteral("consent");
    return {};
}

// ---- fordítási kapcsolók -------------------------------------------------------------

namespace cloud {
bool clientCompiled()
{
#ifdef TANARA_HAVE_CLOUD
    return true;
#else
    return false;
#endif
}
bool teaserCompiled()
{
#ifdef TANARA_HAVE_CLOUD_TEASER
    return true;
#else
    return false;
#endif
}
} // namespace cloud

} // namespace tanara
