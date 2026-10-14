#pragma once
//
// The asynchronous job of the P10 capability ports (IVoiceEmbedder, IParticipantIdentifier,
// ISummarizer, ITagSuggester, IModelCatalog, ICostEstimating). One shape for every port:
//
//   SomeJob* job = port->doSomething(request);       // the job is the provider's child
//   connect(job, &CapabilityJob::finished, ...);     // then job->result()
//   connect(job, &CapabilityJob::failed, ...);       // the classified tanara::Error
//
// Exactly one of finished / failed is emitted, never synchronously inside the call that created the
// job (a provider that computes at once delivers in the next event-loop turn), so the caller can
// connect first. cancel() stops the job: neither signal follows. The ProviderContext's cancel token
// cancels the running jobs of the provider as well.
//
// ResultJob<T> adds the typed result; a provider subclasses it (or uses FunctionJob for a
// computation that runs synchronously).
#include "tanara/foundation/error/Error.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

namespace tanara::providers {

// Progress callback of a long operation (a model download): done / total (total 0 = unknown).
using ProgressSink = std::function<void(qint64 done, qint64 total)>;

class CapabilityJob : public QObject {
    Q_OBJECT
public:
    explicit CapabilityJob(QObject* parent = nullptr);
    ~CapabilityJob() override;

    // Stops the job; neither finished nor failed follows. Idempotent.
    void cancel();
    bool isCancelled() const { return m_cancelled; }
    bool isDone() const { return m_done; }
    const Error& error() const { return m_error; }

signals:
    void progress(qint64 done, qint64 total);
    void finished();
    void failed(tanara::Error error);

public:
    // ---- for the provider that drives the job ----
    // Emit helpers: queued (the next event-loop turn), at most once, not after cancel().
    void succeedLater();
    void failLater(const Error& error);
    // Immediate variants (inside an asynchronous callback already).
    void succeed();
    void fail(const Error& error);

protected:
    // The subclass's own teardown on cancel() (abort a reply, stop a worker ...).
    virtual void onCancel() {}

private:
    bool m_cancelled = false;
    bool m_done = false;
    Error m_error;
};

template <class T>
class ResultJob : public CapabilityJob {
public:
    explicit ResultJob(QObject* parent = nullptr) : CapabilityJob(parent) {}
    // Valid after finished.
    const T& result() const { return m_result; }
    void setResult(T value) { m_result = std::move(value); }
    // setResult + succeed / succeedLater.
    void complete(T value) { setResult(std::move(value)); this->succeed(); }
    void completeLater(T value) { setResult(std::move(value)); this->succeedLater(); }

private:
    T m_result{};
};

// A job whose result is computed synchronously by a function, delivered in the next event-loop
// turn (the local adapters of cheap operations). The function returns false + fills the error on
// failure.
template <class T>
class FunctionJob : public ResultJob<T> {
public:
    using Fn = std::function<bool(T* out, Error* error)>;
    FunctionJob(Fn fn, QObject* parent) : ResultJob<T>(parent)
    {
        T value{};
        Error e;
        if (fn && fn(&value, &e)) {
            this->completeLater(std::move(value));
        } else {
            if (!e.isError()) e.kind = Error::Kind::Service;
            this->failLater(e);
        }
    }
};

// An Error of kind Input with a code (a request the provider cannot take).
Error inputError(const QString& code, const QString& message = QString());

} // namespace tanara::providers
