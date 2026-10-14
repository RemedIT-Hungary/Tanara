#pragma once
//
// The common HTTP client (foundation::net): ONE QNetworkAccessManager per client, shared by every
// HTTP-based provider, the Tanara Cloud client, the connection tester and the model downloader.
//
// What it adds to a request (HttpClient::prepare, done by every send method):
//  - "User-Agent: Tanara/<version>" (unless the request already has one),
//  - the common headers (setHeader; a header the request already carries is not overwritten),
//  - the default transfer timeout (setTimeoutMs; 0 = none, which is the default: an LLM request may
//    legitimately compute for minutes without sending a byte; a request with its own timeout keeps it).
// Proxy: the application-wide Qt proxy by default; setUseSystemProxy(true) switches this client to
// the operating system's proxy configuration.
//
// Response observer chain (addObserver): every finished reply of this client is reported as an
// HttpExchange (method, path, status, lowercase headers, body, network error) BEFORE the caller's own
// finished-handler runs (the body is peeked, not consumed). Per-call observation (a provider job's
// gateway accounting) goes through providers::ProviderContext::observe instead.
//
// Headless (Qt Core + Network only).
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QNetworkRequest>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QVector>

#include <functional>

class QHttpMultiPart;
class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

namespace tanara::net {

// Result of one HTTP call: what an observer (cloud accounting, error description) needs.
struct HttpExchange {
    QByteArray method;
    QString    path;                       // the URL's full path, e.g. "/v1/transcriptions/tr_1"
    int        status = 0;                 // 0 = no HTTP response (network error)
    QHash<QByteArray, QByteArray> headers; // lowercase keys
    QByteArray body;
    QString    networkError;               // empty if there was an HTTP response
};

// From a finished QNetworkReply (the caller already read the body).
HttpExchange makeHttpExchange(QNetworkReply* reply, const QByteArray& body);

using ExchangeObserver = std::function<void(const HttpExchange&)>;
using HeaderList = QList<QPair<QByteArray, QByteArray>>;

// "Tanara/<libraryVersion()>".
QByteArray defaultUserAgent();

class HttpClient : public QObject {
    Q_OBJECT
public:
    // Own QNetworkAccessManager (a child of this client).
    explicit HttpClient(QObject* parent = nullptr);
    // An external QNetworkAccessManager (NOT owned; it must outlive the client). Test seam.
    explicit HttpClient(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~HttpClient() override;

    QNetworkAccessManager* nam() const;

    void setTimeoutMs(int ms) { m_timeoutMs = ms < 0 ? 0 : ms; }
    int  timeoutMs() const { return m_timeoutMs; }
    void setUserAgent(const QByteArray& ua) { m_userAgent = ua; }
    QByteArray userAgent() const { return m_userAgent; }
    // A header sent with every request of this client; an empty value removes it.
    void setHeader(const QByteArray& name, const QByteArray& value);
    HeaderList headers() const { return m_headers; }
    void setUseSystemProxy(bool on);
    bool usesSystemProxy() const { return m_systemProxy; }

    // Observer chain (in registration order). Returns an id for removeObserver.
    int  addObserver(ExchangeObserver observer);
    void removeObserver(int id);

    // The request with the client's additions (User-Agent, common headers, default timeout).
    QNetworkRequest prepare(QNetworkRequest request) const;
    QNetworkRequest request(const QUrl& url) const;

    QNetworkReply* get(const QNetworkRequest& request);
    QNetworkReply* post(const QNetworkRequest& request, const QByteArray& body);
    QNetworkReply* post(const QNetworkRequest& request, QHttpMultiPart* multiPart);
    QNetworkReply* deleteResource(const QNetworkRequest& request);
    QNetworkReply* sendCustomRequest(const QNetworkRequest& request, const QByteArray& verb,
                                     const QByteArray& body = QByteArray());

private:
    QNetworkReply* track(QNetworkReply* reply);

    QPointer<QNetworkAccessManager> m_nam;
    bool m_ownsNam = false;
    int m_timeoutMs = 0;
    QByteArray m_userAgent;
    HeaderList m_headers;
    bool m_systemProxy = false;
    QVector<QPair<int, ExchangeObserver>> m_observers;
    int m_nextObserverId = 1;
};

} // namespace tanara::net
