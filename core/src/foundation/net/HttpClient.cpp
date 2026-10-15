#include "tanara/foundation/net/HttpClient.h"
#include "tanara/foundation/Version.h"

#include <QHttpMultiPart>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QUrl>

namespace tanara::net {

namespace {

// Asks the operating system for every request (the per-client switch of setUseSystemProxy).
class SystemProxyFactory : public QNetworkProxyFactory {
public:
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery& query) override
    {
        return QNetworkProxyFactory::systemProxyForQuery(query);
    }
};

QByteArray methodOf(QNetworkReply* reply)
{
    switch (reply->operation()) {
    case QNetworkAccessManager::GetOperation:    return QByteArrayLiteral("GET");
    case QNetworkAccessManager::PostOperation:   return QByteArrayLiteral("POST");
    case QNetworkAccessManager::DeleteOperation: return QByteArrayLiteral("DELETE");
    case QNetworkAccessManager::PutOperation:    return QByteArrayLiteral("PUT");
    case QNetworkAccessManager::HeadOperation:   return QByteArrayLiteral("HEAD");
    default: return reply->request().attribute(QNetworkRequest::CustomVerbAttribute).toByteArray();
    }
}

} // namespace

HttpExchange makeHttpExchange(QNetworkReply* reply, const QByteArray& body)
{
    HttpExchange ex;
    if (!reply) return ex;
    ex.method = methodOf(reply);
    ex.path   = reply->request().url().path();
    ex.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    for (const auto& pair : reply->rawHeaderPairs())
        ex.headers.insert(pair.first.toLower(), pair.second);
    ex.body = body;
    if (ex.status == 0) {
        ex.networkErrorCode = int(reply->error());
        ex.networkError = reply->errorString().isEmpty() ? QStringLiteral("network error") : reply->errorString();
    }
    return ex;
}

QByteArray defaultUserAgent()
{
    return QByteArrayLiteral("Tanara/") + libraryVersion().toUtf8();
}

HttpClient::HttpClient(QObject* parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)), m_ownsNam(true),
      m_userAgent(defaultUserAgent())
{
}

HttpClient::HttpClient(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam), m_ownsNam(false), m_userAgent(defaultUserAgent())
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
        m_ownsNam = true;
    }
}

HttpClient::~HttpClient() = default;

QNetworkAccessManager* HttpClient::nam() const { return m_nam; }

void HttpClient::setHeader(const QByteArray& name, const QByteArray& value)
{
    for (int i = 0; i < m_headers.size(); ++i) {
        if (m_headers.at(i).first.compare(name, Qt::CaseInsensitive) == 0) {
            if (value.isEmpty()) m_headers.removeAt(i);
            else m_headers[i].second = value;
            return;
        }
    }
    if (!value.isEmpty()) m_headers.append({name, value});
}

void HttpClient::setUseSystemProxy(bool on)
{
    if (on == m_systemProxy || !m_nam) return;
    m_systemProxy = on;
    // The NAM takes ownership of the factory; nullptr returns to the application-wide proxy.
    m_nam->setProxyFactory(on ? new SystemProxyFactory : nullptr);
}

int HttpClient::addObserver(ExchangeObserver observer)
{
    const int id = m_nextObserverId++;
    m_observers.append({id, std::move(observer)});
    return id;
}

void HttpClient::removeObserver(int id)
{
    for (int i = 0; i < m_observers.size(); ++i)
        if (m_observers.at(i).first == id) { m_observers.removeAt(i); return; }
}

QNetworkRequest HttpClient::prepare(QNetworkRequest request) const
{
    if (!m_userAgent.isEmpty() && !request.hasRawHeader(QByteArrayLiteral("User-Agent")))
        request.setRawHeader(QByteArrayLiteral("User-Agent"), m_userAgent);
    for (const auto& h : m_headers)
        if (!request.hasRawHeader(h.first)) request.setRawHeader(h.first, h.second);
    if (m_timeoutMs > 0 && request.transferTimeoutAsDuration().count() == 0)
        request.setTransferTimeout(m_timeoutMs);
    return request;
}

QNetworkRequest HttpClient::request(const QUrl& url) const
{
    return prepare(QNetworkRequest(url));
}

QNetworkReply* HttpClient::track(QNetworkReply* reply)
{
    if (reply && !m_observers.isEmpty()) {
        // Connected before the caller's own handler, so the observers see the response first; the body is
        // peeked so the caller can still read it.
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            const HttpExchange ex = makeHttpExchange(reply, reply->peek(reply->bytesAvailable()));
            const auto observers = m_observers;
            for (const auto& o : observers)
                if (o.second) o.second(ex);
        });
    }
    return reply;
}

QNetworkReply* HttpClient::get(const QNetworkRequest& request)
{
    return track(m_nam->get(prepare(request)));
}

QNetworkReply* HttpClient::post(const QNetworkRequest& request, const QByteArray& body)
{
    return track(m_nam->post(prepare(request), body));
}

QNetworkReply* HttpClient::post(const QNetworkRequest& request, QHttpMultiPart* multiPart)
{
    return track(m_nam->post(prepare(request), multiPart));
}

QNetworkReply* HttpClient::deleteResource(const QNetworkRequest& request)
{
    return track(m_nam->deleteResource(prepare(request)));
}

QNetworkReply* HttpClient::sendCustomRequest(const QNetworkRequest& request, const QByteArray& verb,
                                             const QByteArray& body)
{
    return track(m_nam->sendCustomRequest(prepare(request), verb, body));
}

} // namespace tanara::net
