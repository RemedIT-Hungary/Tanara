#pragma once
//
// ProviderContext: everything a provider (and its jobs) gets from the caller besides its persisted
// configuration (ProviderConfig). One object, handed to every factory of the ProviderRegistry:
//
//  http        - the shared HTTP client (foundation/net/HttpClient.h); null = the provider uses a
//                private one (tests, a provider created without the app)
//  auth        - where the bearer token comes from: a SecretStore key (apiKey()), a callback
//                (bearer(): the Tanara Cloud session), or nothing (then ProviderConfig::apiKey)
//  headers     - per-call request headers (the Tanara Cloud gateway: X-Tanara-Job-Id, summary mode)
//  observe     - every HTTP response of the provider's jobs (success, error, network error):
//                charge / balance / request id / structured error for the caller
//  cost        - cost events (estimate / charge / refund) the caller books (cloud::CloudRunLedger)
//  cancel      - a shared cancel token: cancelling it cancels the provider's running jobs
//  jobId, language, logCategory - correlation, the user's language, the logging category
//
// A value type: copies share the cancel token's state.
#include "tanara/foundation/net/HttpClient.h"

#include <QPointer>
#include <QString>

#include <functional>
#include <memory>

class QNetworkRequest;

namespace tanara { class SecretStore; }

namespace tanara::providers {

struct ProviderConfig;

class AuthSource {
public:
    AuthSource() = default;
    // The value of a SecretStore key, read at call time (the store must outlive the source).
    static AuthSource apiKey(const SecretStore* store, const QString& secretKey);
    // A callback (e.g. the Tanara Cloud session's current key).
    static AuthSource bearer(std::function<QString()> token);
    // A fixed token.
    static AuthSource fixed(const QString& token);

    bool isSet() const { return bool(m_token); }
    QString token() const { return m_token ? m_token() : QString(); }

private:
    std::function<QString()> m_token;
};

// A cost event. Amounts in whole millionths of the currency unit (1 USD = 1,000,000 micros), as the
// Tanara Cloud gateway sends them; the providers module does not interpret them further.
struct CostEvent {
    enum class Kind { Estimate, Charge, Refund };
    Kind    kind = Kind::Charge;
    qint64  micros = 0;
    QString currency;
    qint64  balanceMicros = 0;      // the balance after the event (if balanceCurrency is not empty)
    QString balanceCurrency;
    QString vatMode;
    QString requestId;
};
using CostSink = std::function<void(const CostEvent&)>;

// A shared cancel flag with listeners. Copies share the state.
class CancelToken {
public:
    CancelToken();
    void cancel() const;
    bool isCancelled() const;
    // Called once on cancel() (immediately, if the token is already cancelled).
    void onCancel(std::function<void()> fn) const;

private:
    struct State;
    std::shared_ptr<State> d;
};

struct ProviderContext {
    QPointer<net::HttpClient> http;
    AuthSource       auth;
    net::HeaderList  headers;
    net::ExchangeObserver observe;
    CostSink         cost;
    CancelToken      cancel;
    QString          jobId;
    QString          language;
    QString          logCategory;

    // The bearer token for a request: the auth source if set, otherwise the config's runtime apiKey.
    QString bearer(const ProviderConfig& cfg) const;
    // Adds the per-call headers to a request.
    void applyHeaders(QNetworkRequest& request) const;
    // Reports one HTTP response to the observer (if any).
    void report(const net::HttpExchange& ex) const { if (observe) observe(ex); }
};

} // namespace tanara::providers
