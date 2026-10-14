#pragma once
//
// GatewayClient (cloud-internal): one call of the Tanara Cloud gateway's capability endpoints for
// the cloud adapters. It resolves, per call:
//  - the address: ProviderConfig::baseUrl (the CloudRunLedger's configFor gives the apiBase), else
//    the session's apiBase();
//  - the bearer: the context's auth source, else the config's apiKey, else the session's key;
//  - the headers: the context's (the ledger gives X-Tanara-Client / Job-Id ...), else the session's;
//  - the HTTP client: the context's, else the session's.
// Every response goes to the context's observer (the run's bookkeeping), or, without one, to the
// session (min-client, balance).
#include "tanara/cloud/CloudSession.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/providers/core/ProviderContext.h"

#include <QByteArray>
#include <QPointer>
#include <QString>

#include <functional>

class QHttpMultiPart;
class QNetworkReply;
class QObject;

namespace tanara::cloud {

namespace detail {

class GatewayClient {
public:
    GatewayClient() = default;
    GatewayClient(const providers::ProviderContext& ctx, const providers::ProviderConfig& cfg,
                  CloudSession* session);

    QString apiBase() const;
    net::HttpClient* http() const;
    const providers::ProviderContext& context() const { return m_ctx; }
    const providers::ProviderConfig& config() const { return m_cfg; }
    CloudSession* session() const { return m_session; }

    using Done = std::function<void(const HttpExchange&)>;
    // JSON body (POST) or none (GET). path: under apiBase, e.g. "/voice/identify". The reply is a
    // child of the HTTP client; `done` runs only while `receiver` lives. Returns the reply (abort it
    // to cancel; then `done` does not run), or null if there is no HTTP client.
    QNetworkReply* getJson(const QString& path, QObject* receiver, Done done) const;
    QNetworkReply* postJson(const QString& path, const QByteArray& body, QObject* receiver, Done done) const;
    QNetworkReply* postMultipart(const QString& path, QHttpMultiPart* multipart, QObject* receiver,
                                 Done done) const;

    // The classified error of a failed exchange (the gateway's structured error).
    static Error errorOf(const HttpExchange& ex);
    static bool ok(const HttpExchange& ex) { return ex.status >= 200 && ex.status < 300; }

private:
    QNetworkReply* finish(QNetworkReply* reply, QObject* receiver, Done done) const;
    void prepare(class QNetworkRequest& req) const;

    providers::ProviderContext m_ctx;
    providers::ProviderConfig m_cfg;
    QPointer<CloudSession> m_session;
};

// Aborts a reply without delivering it.
void abortReply(QNetworkReply* reply);

} // namespace detail
} // namespace tanara::cloud
