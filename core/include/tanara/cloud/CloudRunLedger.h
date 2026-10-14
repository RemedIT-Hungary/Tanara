#pragma once
//
// CloudRunLedger: the client-side bookkeeping of Tanara Cloud processing runs (moved out of the
// AppController in P9).
//
// A run (CloudRun) = one user-visible processing (a transcription, a summary, a topic batch ...): one
// X-Tanara-Job-Id, the charges summed up, the last structured error, the write-back (refund) and the
// gateway ids of a transcription. The ledger
//  - gives the ProviderContext of a run (contextFor): the sign-in as the auth source, the gateway
//    headers, the response observer (account: min-client / balance; run: ids, charges, refund, error)
//    and the cost sink (charges -> the run's totals);
//  - gives the gateway ProviderConfig of a step (configFor);
//  - closes a run (finish: the cloudCharged report; fail: the refund / structured error report).
// The AppController forwards the ledger's signals as its own (cloudCharged / cloudRefunded / cloudError).
//
// Headless (core).
#include "tanara/cloud/CloudTypes.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/providers/core/ProviderContext.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>

namespace tanara::cloud {

class CloudAccount;

// State of a cloud processing run. Filled by the providers' response observer (ProviderContext::observe).
struct CloudRun {
    QString meetingId;
    QString kind;            // transcribe | summary | topics | complex
    QString jobId;           // X-Tanara-Job-Id: a new random value per run
    qint64  chargedMicros = 0;
    QString currency;
    int     calls = 0;       // successful charging calls
    Money   balance;         // the last known balance (after a charge / write-back)
    QString vatMode;
    CloudError lastError;    // the last non-2xx (non-DELETE) response
    bool    refunded = false;
    Money   refund;
    QString fileId, transcriptionId;
    QString lastRequestId;

    Money charged() const { return Money{ chargedMicros, currency.isEmpty() ? QStringLiteral("USD") : currency }; }
    void addCharge(const ChargeInfo& c);
    void addCost(const providers::CostEvent& e);
};
using CloudRunPtr = std::shared_ptr<CloudRun>;

// A charge as a cost event (and back): the providers module only carries it.
providers::CostEvent toCostEvent(const ChargeInfo& c);

class CloudRunLedger : public QObject {
    Q_OBJECT
public:
    // account: the caller's (AppController), its lifetime >= this one's.
    explicit CloudRunLedger(CloudAccount* account, QObject* parent = nullptr);

    CloudRunPtr newRun(const QString& meetingId, const QString& kind) const;

    // The gateway config of a step: type, baseUrl (the account's apiBase), model, temperature 0.2,
    // maxTokens. The key is NOT in it: the context's auth source gives it at request time.
    providers::ProviderConfig configFor(const QString& model, int maxTokens) const;

    // The context of a run's provider calls. run == null: a call outside any run (text embedding):
    // a fresh job id, and the response only goes to the account (min-client, balance).
    // summaryMode: X-Tanara-Summary-Mode (empty = not sent).
    providers::ProviderContext contextFor(const CloudRunPtr& run, const QString& summaryMode,
                                          net::HttpClient* http) const;

    // The run's accounting of one gateway response (what contextFor's observer does): the account's
    // observeExchange, the request id, the last error (not DELETE), the file / transcription id, the
    // charges (through the cost sink), the write-back of a failed transcription.
    static void observe(CloudAccount* account, const CloudRunPtr& run, const HttpExchange& ex,
                        const providers::CostSink& cost);

    // A successful run: drops the pending transcription, reports the charge (charged), refreshes the account.
    void finish(const CloudRunPtr& run);
    // A failed run: refund -> refunded; a structured gateway error -> failed (and clientTooOld on the
    // account). Returns false if the run has neither (the caller reports its own fallback message).
    bool fail(const CloudRunPtr& run);

signals:
    void charged(QString meetingId, QString kind, tanara::cloud::Money total, int calls,
                 tanara::cloud::Money balance, QString vatMode);
    void refunded(QString meetingId, tanara::cloud::Money refund, tanara::cloud::Money balance,
                  QString requestId);
    void failed(QString meetingId, QString kind, tanara::cloud::CloudError error,
                tanara::cloud::Money chargedSoFar);

private:
    QPointer<CloudAccount> m_account;
};

} // namespace tanara::cloud
