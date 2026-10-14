// The common HTTP client (foundation/net/HttpClient.h) + the classified error (foundation/error):
// User-Agent and common headers, the default timeout, the response observer chain, and the
// HttpExchange -> Error classification. Against the in-process fake server (fake_http.h): no network.
#include <QtTest>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>

#include "fake_http.h"
#include "tanara/foundation/Version.h"
#include "tanara/foundation/error/Error.h"
#include "tanara/foundation/net/HttpClient.h"

using namespace tanara;

namespace {
QByteArray headerOf(const fakehttp::Request& r, const QByteArray& name)
{
    for (const QByteArray& line : r.head.split('\n')) {
        const int colon = line.indexOf(':');
        if (colon > 0 && line.left(colon).trimmed().toLower() == name.toLower())
            return line.mid(colon + 1).trimmed();
    }
    return QByteArray();
}
} // namespace

class TestHttpClient : public QObject {
    Q_OBJECT
private slots:
    void userAgentAndCommonHeaders()
    {
        fakehttp::Server srv;
        net::HttpClient http;
        QCOMPARE(http.userAgent(), QByteArray("Tanara/") + libraryVersion().toUtf8());
        http.setHeader("X-Tanara-Client", "1.0/linux");
        http.setHeader("X-Drop", "x");
        http.setHeader("X-Drop", QByteArray());   // empty value removes it
        QNetworkRequest req(QUrl(srv.base() + "/models"));
        req.setRawHeader("X-Tanara-Client", "own");   // the request's own header wins
        QNetworkReply* r = http.get(req);
        QSignalSpy done(r, &QNetworkReply::finished);
        QVERIFY(done.wait(5000));
        r->deleteLater();
        QCOMPARE(srv.log.size(), 1);
        QCOMPARE(headerOf(srv.log[0], "User-Agent"), http.userAgent());
        QCOMPARE(headerOf(srv.log[0], "X-Tanara-Client"), QByteArray("own"));
        QVERIFY(headerOf(srv.log[0], "X-Drop").isEmpty());

        QNetworkReply* r2 = http.post(QNetworkRequest(QUrl(srv.base() + "/x")), QByteArray("{}"));
        QSignalSpy done2(r2, &QNetworkReply::finished);
        QVERIFY(done2.wait(5000));
        r2->deleteLater();
        QCOMPARE(headerOf(srv.log[1], "X-Tanara-Client"), QByteArray("1.0/linux"));
    }

    // The default timeout applies to a request without its own; 0 = none.
    void defaultTimeout()
    {
        fakehttp::Server srv;
        srv.handler = [](const fakehttp::Request&) { fakehttp::Reply r; r.hold = true; return r; };
        net::HttpClient http;
        QCOMPARE(http.timeoutMs(), 0);
        QCOMPARE(http.prepare(QNetworkRequest(QUrl("http://x/"))).transferTimeoutAsDuration().count(), 0);
        http.setTimeoutMs(300);
        QNetworkRequest own(QUrl("http://x/"));
        own.setTransferTimeout(5000);
        QCOMPARE(int(http.prepare(own).transferTimeoutAsDuration().count()), 5000);

        QElapsedTimer t;
        t.start();
        QNetworkReply* r = http.get(QNetworkRequest(QUrl(srv.base() + "/slow")));
        QSignalSpy done(r, &QNetworkReply::finished);
        QVERIFY(done.wait(5000));
        QVERIFY(r->error() != QNetworkReply::NoError);
        QVERIFY(t.elapsed() < 4000);
        r->deleteLater();
    }

    // The observer chain sees every response before the caller, without consuming the body.
    void observerChain()
    {
        fakehttp::Server srv;
        srv.handler = [](const fakehttp::Request& req) {
            fakehttp::Reply r;
            if (req.path.endsWith("/bad")) {
                r.status = 429;
                r.body = R"({"error":{"code":"rate_limited","message":"Slow down","request_id":"req_9"}})";
                r.headers = "Retry-After: 7\r\n";
            } else {
                r.body = R"({"ok":true})";
            }
            return r;
        };
        net::HttpClient http;
        QVector<net::HttpExchange> seen;
        QStringList order;
        const int id = http.addObserver([&](const net::HttpExchange& ex) { seen.append(ex); order << "observer"; });

        QNetworkReply* ok = http.get(QNetworkRequest(QUrl(srv.base() + "/good")));
        QByteArray callerBody;
        connect(ok, &QNetworkReply::finished, this, [&] { order << "caller"; callerBody = ok->readAll(); });
        QSignalSpy done(ok, &QNetworkReply::finished);
        QVERIFY(done.wait(5000));
        ok->deleteLater();
        QCOMPARE(order, (QStringList{"observer", "caller"}));
        QCOMPARE(callerBody, QByteArray(R"({"ok":true})"));
        QCOMPARE(seen.size(), 1);
        QCOMPARE(seen[0].method, QByteArray("GET"));
        QCOMPARE(seen[0].status, 200);
        QCOMPARE(seen[0].path, QStringLiteral("/v1/good"));
        QCOMPARE(seen[0].body, callerBody);
        QVERIFY(!net::errorFromExchange(seen[0]).isError());

        QNetworkReply* bad = http.post(QNetworkRequest(QUrl(srv.base() + "/bad")), QByteArray("{}"));
        QSignalSpy done2(bad, &QNetworkReply::finished);
        QVERIFY(done2.wait(5000));
        bad->deleteLater();
        QCOMPARE(seen.size(), 2);
        const Error e = net::errorFromExchange(seen[1]);
        QCOMPARE(e.kind, Error::Kind::Transient);
        QCOMPARE(e.httpStatus, 429);
        QCOMPARE(e.code, QStringLiteral("rate_limited"));
        QCOMPARE(e.message, QStringLiteral("Slow down"));
        QCOMPARE(e.requestId, QStringLiteral("req_9"));
        QCOMPARE(e.retryAfterSec, 7);
        QCOMPARE(net::failureDetail(seen[1]), QStringLiteral("HTTP 429 · rate_limited · Slow down"));

        http.removeObserver(id);
        QNetworkReply* again = http.get(QNetworkRequest(QUrl(srv.base() + "/good")));
        QSignalSpy done3(again, &QNetworkReply::finished);
        QVERIFY(done3.wait(5000));
        again->deleteLater();
        QCOMPARE(seen.size(), 2);
    }

    // A network error: no HTTP response -> Transient, "network", the Qt text in detail.
    void networkErrorClassification()
    {
        quint16 port = 0;
        {
            QTcpServer probe;
            QVERIFY(probe.listen(QHostAddress::LocalHost));
            port = probe.serverPort();
        }   // closed: nobody listens there now
        net::HttpClient http;
        net::HttpExchange seen;
        http.addObserver([&](const net::HttpExchange& ex) { seen = ex; });
        QNetworkReply* r = http.get(QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1/v1/x").arg(port))));
        QSignalSpy done(r, &QNetworkReply::finished);
        QVERIFY(done.wait(5000));
        r->deleteLater();
        QCOMPARE(seen.status, 0);
        QVERIFY(!seen.networkError.isEmpty());
        const Error e = net::errorFromExchange(seen);
        QCOMPARE(e.kind, Error::Kind::Transient);
        QCOMPARE(e.code, QStringLiteral("network"));
        QVERIFY(!e.detail.isEmpty());
    }

    void statusKinds()
    {
        QCOMPARE(errorKindForHttpStatus(200), Error::Kind::None);
        QCOMPARE(errorKindForHttpStatus(401), Error::Kind::Auth);
        QCOMPARE(errorKindForHttpStatus(403), Error::Kind::Auth);
        QCOMPARE(errorKindForHttpStatus(402), Error::Kind::Quota);
        QCOMPARE(errorKindForHttpStatus(400), Error::Kind::Input);
        QCOMPARE(errorKindForHttpStatus(404), Error::Kind::Input);
        QCOMPARE(errorKindForHttpStatus(426), Error::Kind::Unsupported);
        QCOMPARE(errorKindForHttpStatus(500), Error::Kind::Service);
        QCOMPARE(errorKindForHttpStatus(503), Error::Kind::Transient);
        QCOMPARE(errorKindForHttpStatus(0), Error::Kind::Transient);
        QCOMPARE(errorKindName(Error::Kind::Quota), QStringLiteral("quota"));
        QCOMPARE(Error::cancelled().kind, Error::Kind::Cancelled);
    }

    // An external NAM (test seam) is used, not owned; the system-proxy switch does not break requests.
    void externalNamAndProxySwitch()
    {
        fakehttp::Server srv;
        QNetworkAccessManager nam;
        {
            net::HttpClient http(&nam);
            QCOMPARE(http.nam(), &nam);
            http.setUseSystemProxy(true);
            QVERIFY(http.usesSystemProxy());
            http.setUseSystemProxy(false);
            QNetworkReply* r = http.get(QNetworkRequest(QUrl(srv.base() + "/models")));
            QSignalSpy done(r, &QNetworkReply::finished);
            QVERIFY(done.wait(5000));
            QCOMPARE(r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
            r->deleteLater();
        }
        QVERIFY(nam.parent() == nullptr);   // still alive: the client did not own it
    }
};

QTEST_GUILESS_MAIN(TestHttpClient)
#include "test_http_client.moc"
