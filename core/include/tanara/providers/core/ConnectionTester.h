#pragma once
//
// ConnectionTester: the backend of the Settings "Test connection" button.
//
// WHAT IT CHECKS (and what not): it sends a single GET request to the probe endpoint given in the
// provider's descriptor (ConnectionProbe: typically GET <baseUrl>/models), with the given API key.
//  - whether the address is reachable (DNS, TCP, TLS) and how long it takes to answer;
//  - whether the provider accepts the key (401 / 403 = no);
//  - whether it gives an API-like answer (a JSON model list) and the configured model is in it.
// It does NOT start a transcription / summary, does not spend anything, and does not prove that the
// model actually loads or that the account has quota; only a real run shows that.
//
// Headless (core): QtNetwork. The provider-specific knowledge is in the descriptor (ProviderDescriptor::probe).
#include "tanara/domain/Types.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/providers/core/ProviderDescriptor.h"
#include "tanara/providers/chat/LlmContext.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>

class QNetworkReply;
namespace tanara::net { class HttpClient; }

namespace tanara::providers {

struct ConnectionTestResult {
    enum class Status {
        Ok,            // reachable, the key is fine (a model warning may still apply)
        BadConfig,     // missing / bad address; no request went out
        Unreachable,   // no connection was made (refused, no such host, timeout, TLS)
        AuthFailed,    // 401 / 403
        NotAnApi,      // the address answers, but not the expected API (404, not JSON)
        ServerError,   // 5xx / 429 / other HTTP error
    };
    Status status = Status::BadConfig;
    bool ok() const { return status == Status::Ok; }
    int httpStatus = 0;          // 0 = no HTTP response arrived
    qint64 latencyMs = -1;       // from sending the request to the response; -1 = no response
    QString code;                // technical code: ECONNREFUSED, ENOTFOUND, ETIMEDOUT, ETLS, HTTP 401 ...
    QString message;             // a human sentence in the UI language (on error)
    QString warning;             // alongside success: e.g. the configured model is not in the list
    QStringList models;          // if the probe gives a model list (sorted)
    // For an LLM with your own key, after a successful probe: the native server API's (LM Studio) data:
    // loaded context, parallelism, the model's maximum. For another server kind == Unknown.
    llmctx::LlmServerInfo server;
    QString serverText;          // a human line from the above (empty if there is no native API)
};

// Human summary of the server probe (serverText) and its warnings (parallel > 1, little context).
// A pure function, testable. APPENDS to *warning (after an existing warning).
void describeLlmServer(const llmctx::LlmServerInfo& info, QString* text, QString* warning);

// Whether the address points to the local machine / local network (localhost, 127.0.0.0/8, ::1,
// RFC 1918, *.local); then an API key is typically not needed.
bool isLocalEndpoint(const QString& baseUrl);

class ConnectionTester : public QObject {
    Q_OBJECT
public:
    explicit ConnectionTester(QObject* parent = nullptr);
    ~ConnectionTester() override;

    // The HTTP client to use (not owned); none = an own one.
    void setHttpClient(net::HttpClient* http) { m_http = http; }
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    int timeoutMs() const { return m_timeoutMs; }

    // Starts a probe; the result comes in the finished(id, ...) signal (even for a bad configuration, in
    // the next event-loop turn). cfg.apiKey is the runtime key (empty = no header).
    int test(const ProviderDescriptor& descriptor, const ProviderConfig& cfg);
    // Drops a running probe (no finished follows).
    void cancel(int id);
    void cancelAll();
    bool running(int id) const { return m_replies.contains(id) || m_probes.contains(id); }

    // Classifying an HTTP status + its human sentence and technical code (testable).
    static void describeHttp(int httpStatus, bool hadKey, bool local, ConnectionTestResult* out);

signals:
    void finished(int id, const tanara::providers::ConnectionTestResult& result);

private:
    void onFinished(int id, QNetworkReply* reply, qint64 startedMs, const ProviderDescriptor& d,
                    const ProviderConfig& cfg);

    QPointer<net::HttpClient> m_http;
    QHash<int, QPointer<QNetworkReply>> m_replies;
    QHash<int, QPointer<QObject>> m_probes;   // the native server probe (LLM) while running
    int m_nextId = 1;
    int m_timeoutMs = 8000;
};

} // namespace tanara::providers
Q_DECLARE_METATYPE(tanara::providers::ConnectionTestResult)
