#pragma once
//
// Capabilities of the configured LLM server and model preparation (LM Studio native API).
//
//  - LlmServerProbe: GET <root>/api/v1/models (read-only). If it answers, the server is LM Studio:
//    the configured model's loaded instances (context, parallelism) and the model's maximum.
//    For another server (llama.cpp, Ollama, vLLM, cloud) the result is "unknown". The response is
//    cached briefly (kCacheMs) per address; the call never blocks (asynchronous signal).
//  - LlmModelPreparer: BEFORE an LLM task it makes sure the model is loaded with at least the
//    context the task needs and parallel = 1 (parallel > 1 already caused a crash on this GPU). If
//    the loaded instance is fine, it does not touch it; if not loaded, it loads; if small /
//    parallel, it unloads and reloads, ONLY the configured model's instances, and only if Tanara
//    is not running another request on it. At most ONE load attempt per task; failure gives an
//    understandable error.
//
// The Tanara Cloud path does not use these (it has no native API).
#include "tanara/Types.h"
#include "tanara/jobs/JobTypes.h"
#include "tanara/llm/LlmContext.h"

#include <QObject>
#include <QPointer>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace tanara {

class LlmServerProbe : public QObject {
    Q_OBJECT
public:
    static constexpr int kCacheMs = 10000;

    explicit LlmServerProbe(QObject* parent = nullptr);
    ~LlmServerProbe() override;

    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    // Query; the result comes in the finished signal (asynchronous even on a cache hit).
    void probe(const ProviderConfig& cfg, bool useCache = true);
    void cancel();

    // Drops the cache for an address (empty = all). Required after a load / unload.
    static void invalidateCache(const QString& baseUrl = QString());

signals:
    void finished(const tanara::llmctx::LlmServerInfo& info);

private:
    QNetworkAccessManager* m_nam = nullptr;
    QPointer<QNetworkReply> m_reply;
    int m_timeoutMs = 3000;
    quint64 m_generation = 0;
};

class LlmModelPreparer : public QObject {
    Q_OBJECT
public:
    explicit LlmModelPreparer(const ProviderConfig& cfg, QObject* parent = nullptr);
    ~LlmModelPreparer() override;

    // True if Tanara is currently running ANOTHER LLM request (then we do not unload the model).
    void setBusyCheck(std::function<bool()> busy) { m_busy = std::move(busy); }
    void setProbeTimeoutMs(int ms) { m_probeTimeoutMs = ms; }
    void setLoadTimeoutMs(int ms) { m_loadTimeoutMs = ms; }

    // need: the task's estimated context need; floor: the requested minimum (0 = none).
    void start(int need, int floor = 0);
    // Drops the running query / unload / load; no signal follows.
    void cancel();

    const llmctx::LlmServerInfo& serverInfo() const { return m_info; }
    const llmctx::PreloadDecision& decision() const { return m_decision; }
    // The model's context after ready (-1: unknown, e.g. not LM Studio).
    int loadedContext() const { return m_loadedContext; }
    bool didLoad() const { return m_didLoad; }

signals:
    // A load / reload begins (the task's "Loading the model..." stage).
    void loadingStarted(int contextLength);
    // The task may start.
    void ready();
    // Preparation failed (the caller sets the kind).
    void failed(const tanara::JobError& error);

private:
    void onProbed(const llmctx::LlmServerInfo& info);
    void unloadNext();
    void load();
    QNetworkReply* post(const QString& path, const QByteArray& json, int timeoutMs);
    void fail(const QString& message, const QString& detail, const QString& hint);

    ProviderConfig m_cfg;
    QNetworkAccessManager* m_nam = nullptr;
    LlmServerProbe* m_probe = nullptr;
    QPointer<QNetworkReply> m_reply;
    std::function<bool()> m_busy;
    int m_need = 0;
    int m_floor = 0;
    int m_probeTimeoutMs = 3000;
    int m_loadTimeoutMs = 10 * 60 * 1000;
    llmctx::LlmServerInfo m_info;
    llmctx::PreloadDecision m_decision;
    QStringList m_toUnload;
    int m_loadedContext = -1;
    bool m_didLoad = false;
    bool m_cancelled = false;
    bool m_started = false;
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::llmctx::LlmServerInfo)
