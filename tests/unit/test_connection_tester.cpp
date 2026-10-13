// ConnectionTester: the core backend of Settings' "Test connection" button.
//
// With a local fake HTTP server (NO real provider call): what it asks (path, header), and how
// it classifies the reply: reachable + model list, missing model, rejected key, not an API,
// server error, refused connection, bad address. Plus recognizing a local endpoint and the
// built-in providers' probe descriptors.
#include <QtTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

#include "tanara/provider/ConnectionTester.h"
#include "tanara/provider/ProviderRegistry.h"

#include <functional>
#include <memory>

using namespace tanara;

namespace {

struct FakeRequest { QByteArray method; QString path; QByteArray auth; };
struct FakeReply { int status = 200; QByteArray body = "{}"; };

// Minimal fake HTTP server (as in test_app_jobs.cpp), recording the request headers.
class FakeHttp : public QObject {
public:
    std::function<FakeReply(const FakeRequest&)> handler;
    QVector<FakeRequest> log;

    FakeHttp()
    {
        connect(&m_srv, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* s = m_srv.nextPendingConnection()) {
                auto buf = std::make_shared<QByteArray>();
                connect(s, &QTcpSocket::readyRead, this, [this, s, buf]() { feed(s, *buf); });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
        m_srv.listen(QHostAddress::LocalHost);
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1/v1").arg(m_srv.serverPort()); }
    quint16 port() const { return m_srv.serverPort(); }
    void close() { m_srv.close(); }

private:
    void feed(QTcpSocket* s, QByteArray& buf)
    {
        buf += s->readAll();
        const int headEnd = buf.indexOf("\r\n\r\n");
        if (headEnd < 0) return;
        const QByteArray head = buf.left(headEnd);
        FakeRequest req;
        const QList<QByteArray> first = head.left(head.indexOf('\r')).split(' ');
        req.method = first.value(0);
        req.path = QString::fromUtf8(first.value(1));
        for (const QByteArray& line : head.split('\n')) {
            const QByteArray l = line.trimmed();
            if (l.toLower().startsWith("authorization:")) req.auth = l.mid(14).trimmed();
        }
        buf.clear();
        log.append(req);
        const FakeReply rep = handler ? handler(req) : FakeReply{};
        const QByteArray out = "HTTP/1.1 " + QByteArray::number(rep.status) + " X\r\n"
                               "Content-Type: application/json\r\n"
                               "Content-Length: " + QByteArray::number(rep.body.size()) + "\r\n"
                               "Connection: close\r\n\r\n" + rep.body;
        s->write(out);
        s->disconnectFromHost();
    }
    QTcpServer m_srv;
};

ProviderDescriptor probeDescriptor()
{
    ProviderDescriptor d;
    d.id = QStringLiteral("teszt");
    d.probe = {QStringLiteral("/models"), true};
    return d;
}

} // namespace

class TestConnectionTester : public QObject {
    Q_OBJECT

    // Running a probe and waiting for the result.
    static ConnectionTestResult run(const ProviderDescriptor& d, const ProviderConfig& cfg, int timeoutMs = 3000)
    {
        ConnectionTester tester;
        tester.setTimeoutMs(timeoutMs);
        QSignalSpy spy(&tester, &ConnectionTester::finished);
        const int id = tester.test(d, cfg);
        if (spy.isEmpty()) spy.wait(timeoutMs + 3000);
        if (spy.isEmpty()) return {};
        if (spy.first().at(0).toInt() != id) return {};
        return spy.first().at(1).value<ConnectionTestResult>();
    }

private slots:
    void okWithOpenAiModelList()
    {
        FakeHttp http;
        http.handler = [](const FakeRequest&) {
            return FakeReply{200, "{\"data\":[{\"id\":\"zeta\"},{\"id\":\"alfa\"}]}"};
        };
        ProviderConfig cfg;
        cfg.baseUrl = http.base() + QStringLiteral("/");     // the trailing slash does not matter
        cfg.model = QStringLiteral("alfa");
        cfg.apiKey = QStringLiteral("titok");
        const ConnectionTestResult r = run(probeDescriptor(), cfg);
        QVERIFY(r.ok());
        QCOMPARE(r.httpStatus, 200);
        QVERIFY(r.latencyMs >= 0);
        QCOMPARE(r.models, (QStringList{QStringLiteral("alfa"), QStringLiteral("zeta")}));   // sorted
        QVERIFY(r.warning.isEmpty());
        QVERIFY(r.message.isEmpty());
        // Exactly one GET to the probe path, with the key.
        QCOMPARE(http.log.size(), 1);
        QCOMPARE(http.log.first().method, QByteArray("GET"));
        QCOMPARE(http.log.first().path, QStringLiteral("/v1/models"));
        QCOMPARE(http.log.first().auth, QByteArray("Bearer titok"));
    }

    void sonioxShapedModelListAndMissingModel()
    {
        FakeHttp http;
        http.handler = [](const FakeRequest&) {
            return FakeReply{200, "{\"models\":[{\"id\":\"stt-async-v5\"},{\"id\":\"stt-rt-v4\"}]}"};
        };
        ProviderConfig cfg;
        cfg.baseUrl = http.base();
        cfg.model = QStringLiteral("nincs-ilyen");
        const ConnectionTestResult r = run(probeDescriptor(), cfg);
        QVERIFY(r.ok());                                   // the connection is alive
        QCOMPARE(r.models.size(), 2);
        QVERIFY(r.warning.contains(QStringLiteral("nincs-ilyen")));   // but the model is not in the list
        QVERIFY(http.log.first().auth.isEmpty());          // without a key there is no Authorization
    }

    void rejectedKey()
    {
        FakeHttp http;
        http.handler = [](const FakeRequest&) { return FakeReply{401, "{\"error\":\"unauthorized\"}"}; };
        ProviderConfig cfg;
        cfg.baseUrl = http.base();
        cfg.apiKey = QStringLiteral("rossz");
        ConnectionTestResult r = run(probeDescriptor(), cfg);
        QCOMPARE(r.status, ConnectionTestResult::Status::AuthFailed);
        QCOMPARE(r.code, QStringLiteral("HTTP 401"));
        QVERIFY(r.message.contains(QStringLiteral("kulcs")));
        QVERIFY(r.latencyMs >= 0);                         // a reply came, just a rejecting one

        // Without a key the advice differs: the endpoint asks for a key.
        cfg.apiKey.clear();
        const ConnectionTestResult r2 = run(probeDescriptor(), cfg);
        QCOMPARE(r2.status, ConnectionTestResult::Status::AuthFailed);
        QVERIFY(r2.message != r.message);
    }

    void notAnApi()
    {
        FakeHttp http;
        http.handler = [](const FakeRequest&) { return FakeReply{404, "not found"}; };
        ProviderConfig cfg;
        cfg.baseUrl = http.base();
        ConnectionTestResult r = run(probeDescriptor(), cfg);
        QCOMPARE(r.status, ConnectionTestResult::Status::NotAnApi);
        QCOMPARE(r.code, QStringLiteral("HTTP 404"));

        // 200, but not a model list (e.g. a web page): we do not count it as success.
        http.handler = [](const FakeRequest&) { return FakeReply{200, "<html>hello</html>"}; };
        r = run(probeDescriptor(), cfg);
        QCOMPARE(r.status, ConnectionTestResult::Status::NotAnApi);
        QCOMPARE(r.code, QStringLiteral("EBADRESPONSE"));
        QVERIFY(!r.ok());
    }

    void serverErrors()
    {
        FakeHttp http;
        ProviderConfig cfg;
        cfg.baseUrl = http.base();
        http.handler = [](const FakeRequest&) { return FakeReply{500, "{}"}; };
        ConnectionTestResult r = run(probeDescriptor(), cfg);
        QCOMPARE(r.status, ConnectionTestResult::Status::ServerError);
        QCOMPARE(r.code, QStringLiteral("HTTP 500"));
        http.handler = [](const FakeRequest&) { return FakeReply{429, "{}"}; };
        r = run(probeDescriptor(), cfg);
        QCOMPARE(r.status, ConnectionTestResult::Status::ServerError);
        QCOMPARE(r.code, QStringLiteral("HTTP 429"));
    }

    void connectionRefused()
    {
        // A certainly closed local port: we open it, remember it, close it.
        quint16 port = 0;
        {
            FakeHttp http;
            port = http.port();
            http.close();
        }
        ProviderConfig cfg;
        cfg.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(port);
        // On Windows a closed port is retried even after RST (~2 s per attempt), so a 3 s probe time
        // would end in ETIMEDOUT; the app's default (8 s) is plenty.
#if defined(Q_OS_WIN)
        const ConnectionTestResult r = run(probeDescriptor(), cfg, 8000);
#else
        const ConnectionTestResult r = run(probeDescriptor(), cfg);
#endif
        QCOMPARE(r.status, ConnectionTestResult::Status::Unreachable);
        QCOMPARE(r.code, QStringLiteral("ECONNREFUSED"));
        QCOMPARE(r.httpStatus, 0);
        QCOMPARE(r.latencyMs, qint64(-1));
        QVERIFY(r.message.contains(QStringLiteral("helyi szerver")));   // for a local address this is the advice
    }

    void badConfigNeverSendsARequest()
    {
        ProviderConfig cfg;
        ConnectionTestResult r = run(probeDescriptor(), cfg);            // empty address
        QCOMPARE(r.status, ConnectionTestResult::Status::BadConfig);
        QCOMPARE(r.code, QStringLiteral("EINVAL"));
        cfg.baseUrl = QStringLiteral("ftp://example.invalid/v1");        // not http(s)
        r = run(probeDescriptor(), cfg);
        QCOMPARE(r.status, ConnectionTestResult::Status::BadConfig);
        cfg.baseUrl = QStringLiteral("localhost:1234/v1");               // without a scheme
        r = run(probeDescriptor(), cfg);
        QCOMPARE(r.status, ConnectionTestResult::Status::BadConfig);

        // Without a probe descriptor the provider cannot be tested.
        cfg.baseUrl = QStringLiteral("http://127.0.0.1:9/v1");
        r = run(ProviderDescriptor{}, cfg);
        QCOMPARE(r.status, ConnectionTestResult::Status::BadConfig);
        QCOMPARE(r.code, QStringLiteral("ENOSYS"));
    }

    void cancelSuppressesTheResult()
    {
        FakeHttp http;
        http.handler = [](const FakeRequest&) { return FakeReply{200, "{\"data\":[]}"}; };
        ConnectionTester tester;
        QSignalSpy spy(&tester, &ConnectionTester::finished);
        ProviderConfig cfg;
        cfg.baseUrl = http.base();
        const int id = tester.test(probeDescriptor(), cfg);
        QVERIFY(tester.running(id));
        tester.cancel(id);
        QVERIFY(!tester.running(id));
        QTest::qWait(300);
        QCOMPARE(spy.size(), 0);
    }

    void localEndpoints()
    {
        QVERIFY(isLocalEndpoint(QStringLiteral("http://localhost:1234/v1")));
        QVERIFY(isLocalEndpoint(QStringLiteral("http://127.0.0.1:8000/v1")));
        QVERIFY(isLocalEndpoint(QStringLiteral("http://[::1]:8000/v1")));
        QVERIFY(isLocalEndpoint(QStringLiteral("http://192.168.1.20:1234/v1")));
        QVERIFY(isLocalEndpoint(QStringLiteral("http://10.0.0.5/v1")));
        QVERIFY(isLocalEndpoint(QStringLiteral("http://nas.local:1234")));
        QVERIFY(!isLocalEndpoint(QStringLiteral("https://api.soniox.com/v1")));
        QVERIFY(!isLocalEndpoint(QStringLiteral("https://api.openai.com/v1")));
        QVERIFY(!isLocalEndpoint(QStringLiteral("http://172.32.0.1/v1")));   // outside 172.16/12
        QVERIFY(!isLocalEndpoint(QString()));
    }

    void builtinProvidersDeclareAProbe()
    {
        registerBuiltinProviders();
        // Every built-in (own-key) provider says what can be asked of it for free.
        for (const ProviderDescriptor& d : SttProviderRegistry::instance().all()) {
            if (d.authMode == AuthMode::Login) continue;
            QVERIFY2(d.probe.isValid(), qPrintable(d.id));
            QVERIFY2(d.probe.path.startsWith(QLatin1Char('/')), qPrintable(d.id));
        }
        for (const ProviderDescriptor& d : LlmProviderRegistry::instance().all()) {
            if (d.authMode == AuthMode::Login) continue;
            QVERIFY2(d.probe.isValid(), qPrintable(d.id));
        }
        // The "Advanced" fields come from the descriptor.
        const ProviderDescriptor llm = LlmProviderRegistry::instance().descriptor(QStringLiteral("openai-compat"));
        int advanced = 0;
        for (const ConfigField& f : llm.fields)
            if (f.advanced) ++advanced;
        QCOMPARE(advanced, 2);   // temperature + max tokens
    }

    void describeHttpClassification()
    {
        ConnectionTestResult r;
        ConnectionTester::describeHttp(204, false, false, &r);
        QVERIFY(r.ok());
        QVERIFY(r.code.isEmpty());
        ConnectionTester::describeHttp(403, true, false, &r);
        QCOMPARE(r.status, ConnectionTestResult::Status::AuthFailed);
        ConnectionTester::describeHttp(503, false, true, &r);
        QCOMPARE(r.status, ConnectionTestResult::Status::ServerError);
        QVERIFY(r.message.contains(QStringLiteral("helyi szerver")));
        ConnectionTester::describeHttp(418, false, false, &r);
        QCOMPARE(r.status, ConnectionTestResult::Status::ServerError);
        QCOMPARE(r.code, QStringLiteral("HTTP 418"));
    }
};

QTEST_GUILESS_MAIN(TestConnectionTester)
#include "test_connection_tester.moc"
