#include "tanara/provider/ConnectionTester.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace tanara {

namespace {

// A hálózati réteg hibája → kód + mondat. local: helyi végpontnál a tanács más (fut-e a szerver).
void describeNetworkError(QNetworkReply::NetworkError err, bool local, ConnectionTestResult* r)
{
    r->status = ConnectionTestResult::Status::Unreachable;
    switch (err) {
    case QNetworkReply::ConnectionRefusedError:
        r->code = QStringLiteral("ECONNREFUSED");
        r->message = local
            ? QCoreApplication::translate("ConnectionTester", "A végpont nem válaszol. Fut a helyi szerver (pl. LM Studio), és jó a port?")
            : QCoreApplication::translate("ConnectionTester", "A szolgáltató visszautasította a kapcsolatot. Jó a cím és a port?");
        break;
    case QNetworkReply::HostNotFoundError:
        r->code = QStringLiteral("ENOTFOUND");
        r->message = QCoreApplication::translate("ConnectionTester", "Nincs ilyen gép ezen a címen. Ellenőrizd a címet és az internetkapcsolatot.");
        break;
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:
        r->code = QStringLiteral("ETIMEDOUT");
        r->message = local
            ? QCoreApplication::translate("ConnectionTester", "A helyi szerver nem válaszolt időben. Lehet, hogy éppen modellt tölt be.")
            : QCoreApplication::translate("ConnectionTester", "A szolgáltató nem válaszolt időben. Ellenőrizd az internetkapcsolatot.");
        break;
    case QNetworkReply::SslHandshakeFailedError:
        r->code = QStringLiteral("ETLS");
        r->message = QCoreApplication::translate("ConnectionTester", "A biztonságos kapcsolat nem jött létre (tanúsítványhiba). "
                         "Helyi szervernél http:// kell a cím elejére?");
        break;
    case QNetworkReply::RemoteHostClosedError:
        r->code = QStringLiteral("ECONNRESET");
        r->message = QCoreApplication::translate("ConnectionTester", "A végpont megszakította a kapcsolatot. Biztosan ez az API címe?");
        break;
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::UnknownNetworkError:
        r->code = QStringLiteral("ENETDOWN");
        r->message = QCoreApplication::translate("ConnectionTester", "Nincs hálózati kapcsolat.");
        break;
    case QNetworkReply::ProtocolUnknownError:
    case QNetworkReply::ProtocolInvalidOperationError:
        r->status = ConnectionTestResult::Status::BadConfig;
        r->code = QStringLiteral("EINVAL");
        r->message = QCoreApplication::translate("ConnectionTester", "A cím nem használható. http:// vagy https:// kezdetű címet adj meg.");
        break;
    default:
        r->code = QStringLiteral("ENET %1").arg(int(err));
        r->message = QCoreApplication::translate("ConnectionTester", "A végpont nem érhető el.");
        break;
    }
}

// {"data":[{"id":…}]} (OpenAI-alak) vagy {"models":[{"id":…}]} (Soniox-alak). *ok: a válasz
// értelmezhető modell-lista volt-e.
QStringList parseModels(const QByteArray& body, bool* ok)
{
    *ok = false;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    QJsonArray arr;
    if (doc.isObject()) {
        const QJsonObject root = doc.object();
        if (root.value(QStringLiteral("data")).isArray()) {
            arr = root.value(QStringLiteral("data")).toArray();
            *ok = true;
        } else if (root.value(QStringLiteral("models")).isArray()) {
            arr = root.value(QStringLiteral("models")).toArray();
            *ok = true;
        }
    } else if (doc.isArray()) {
        arr = doc.array();
        *ok = true;
    }
    QStringList out;
    for (const QJsonValue& v : arr) {
        const QString id = v.isString() ? v.toString()
                                        : v.toObject().value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) out << id;
    }
    out.sort();
    out.removeDuplicates();
    return out;
}

} // namespace

bool isLocalEndpoint(const QString& baseUrl)
{
    const QUrl url(baseUrl.trimmed());
    const QString host = url.host().toLower();
    if (host.isEmpty()) return false;
    if (host == QLatin1String("localhost") || host.endsWith(QLatin1String(".localhost"))
        || host.endsWith(QLatin1String(".local")))
        return true;
    QHostAddress a;
    if (!a.setAddress(host)) return false;
    if (a.isLoopback()) return true;
    return a.isInSubnet(QHostAddress(QStringLiteral("10.0.0.0")), 8)
        || a.isInSubnet(QHostAddress(QStringLiteral("172.16.0.0")), 12)
        || a.isInSubnet(QHostAddress(QStringLiteral("192.168.0.0")), 16)
        || a.isInSubnet(QHostAddress(QStringLiteral("fc00::")), 7)
        || a.isLinkLocal();
}

void ConnectionTester::describeHttp(int httpStatus, bool hadKey, bool local, ConnectionTestResult* r)
{
    r->httpStatus = httpStatus;
    r->code = QStringLiteral("HTTP %1").arg(httpStatus);
    if (httpStatus >= 200 && httpStatus < 300) {
        r->status = ConnectionTestResult::Status::Ok;
        r->code.clear();
        r->message.clear();
    } else if (httpStatus == 401 || httpStatus == 403) {
        r->status = ConnectionTestResult::Status::AuthFailed;
        r->message = hadKey
            ? QCoreApplication::translate("ConnectionTester", "A szolgáltató elutasította a kulcsot. Ellenőrizd, hogy jól másoltad-e be, és nem járt-e le.")
            : QCoreApplication::translate("ConnectionTester", "A végpont API-kulcsot kér. Add meg a kulcsot.");
    } else if (httpStatus == 404 || httpStatus == 405) {
        r->status = ConnectionTestResult::Status::NotAnApi;
        r->message = QCoreApplication::translate("ConnectionTester", "A cím válaszol, de nem találja az API-t. A cím vége általában /v1.");
    } else if (httpStatus == 429) {
        r->status = ConnectionTestResult::Status::ServerError;
        r->message = QCoreApplication::translate("ConnectionTester", "A szolgáltató elérhető, de most korlátozza a kéréseket. Próbáld újra kicsit később.");
    } else if (httpStatus >= 500) {
        r->status = ConnectionTestResult::Status::ServerError;
        r->message = local
            ? QCoreApplication::translate("ConnectionTester", "A helyi szerver hibát jelzett. Nézd meg a szerver naplóját.")
            : QCoreApplication::translate("ConnectionTester", "A szolgáltató hibát jelzett. Próbáld újra később.");
    } else {
        r->status = ConnectionTestResult::Status::ServerError;
        r->message = QCoreApplication::translate("ConnectionTester", "A végpont váratlan választ adott.");
    }
}

ConnectionTester::ConnectionTester(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<tanara::ConnectionTestResult>();
}

ConnectionTester::~ConnectionTester()
{
    cancelAll();
}

int ConnectionTester::test(const ProviderDescriptor& descriptor, const ProviderConfig& cfg)
{
    const int id = m_nextId++;

    QString base = cfg.baseUrl.trimmed();
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    const QUrl url(base + descriptor.probe.path);
    const bool httpScheme = url.scheme() == QLatin1String("http")
                            || url.scheme() == QLatin1String("https");

    ConnectionTestResult bad;
    if (!descriptor.probe.isValid()) {
        bad.code = QStringLiteral("ENOSYS");
        bad.message = QCoreApplication::translate("ConnectionTester", "Ehhez a szolgáltatóhoz nincs kapcsolat-teszt.");
    } else if (base.isEmpty() || !url.isValid() || !httpScheme || url.host().isEmpty()) {
        bad.code = QStringLiteral("EINVAL");
        bad.message = QCoreApplication::translate("ConnectionTester", "A cím nem használható. http:// vagy https:// kezdetű címet adj meg.");
    }
    if (!bad.code.isEmpty()) {
        bad.status = ConnectionTestResult::Status::BadConfig;
        QTimer::singleShot(0, this, [this, id, bad] { emit finished(id, bad); });
        return id;
    }

    if (!m_nam) m_nam = new QNetworkAccessManager(this);
    QNetworkRequest req(url);
    req.setTransferTimeout(m_timeoutMs);
    req.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
    if (!cfg.apiKey.trimmed().isEmpty())
        req.setRawHeader(QByteArrayLiteral("Authorization"),
                         QByteArrayLiteral("Bearer ") + cfg.apiKey.trimmed().toUtf8());

    const qint64 started = QDateTime::currentMSecsSinceEpoch();
    QNetworkReply* reply = m_nam->get(req);
    m_replies.insert(id, reply);
    connect(reply, &QNetworkReply::finished, this, [this, id, reply, started, descriptor, cfg] {
        onFinished(id, reply, started, descriptor, cfg);
    });
    return id;
}

void ConnectionTester::onFinished(int id, QNetworkReply* reply, qint64 startedMs,
                                  const ProviderDescriptor& d, const ProviderConfig& cfg)
{
    reply->deleteLater();
    if (!m_replies.contains(id)) return;   // eldobták (cancel)
    m_replies.remove(id);

    const bool local = isLocalEndpoint(cfg.baseUrl);
    const bool hadKey = !cfg.apiKey.trimmed().isEmpty();
    ConnectionTestResult r;
    const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (http > 0) {
        r.latencyMs = QDateTime::currentMSecsSinceEpoch() - startedMs;
        describeHttp(http, hadKey, local, &r);
        if (r.ok() && d.probe.listsModels) {
            bool parsed = false;
            r.models = parseModels(reply->readAll(), &parsed);
            if (!parsed) {
                r.status = ConnectionTestResult::Status::NotAnApi;
                r.code = QStringLiteral("EBADRESPONSE");
                r.message = QCoreApplication::translate("ConnectionTester", "A cím válaszol, de nem a várt API-választ adja. Biztosan ez az API címe?");
            } else if (!cfg.model.trimmed().isEmpty() && !r.models.isEmpty()
                       && !r.models.contains(cfg.model.trimmed())) {
                r.warning = QCoreApplication::translate("ConnectionTester", "A beállított modell (%1) nincs a szolgáltató listájában.")
                                .arg(cfg.model.trimmed());
            }
        }
    } else {
        describeNetworkError(reply->error(), local, &r);
    }
    emit finished(id, r);
}

void ConnectionTester::cancel(int id)
{
    const QPointer<QNetworkReply> reply = m_replies.take(id);
    if (reply) reply->abort();
}

void ConnectionTester::cancelAll()
{
    const auto replies = m_replies;
    m_replies.clear();
    for (const QPointer<QNetworkReply>& r : replies)
        if (r) r->abort();
}

} // namespace tanara
