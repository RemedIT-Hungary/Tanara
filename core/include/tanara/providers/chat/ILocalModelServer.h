#pragma once
//
// Optional capability (CapabilityKind::LocalModelServer): a handler of a LOCAL model server that can
// prepare the configured chat model before a task (load it with enough context, one slot). Registered
// under the SAME id as the chat provider it belongs to ("openai-compat" -> LM Studio's native API,
// providers/chat/LlmServer.h); the caller asks the registry whether the selected chat provider has
// one: registry.create<ILocalModelServer>(chatId, ctx, cfg, parent) - nullptr = nothing to prepare
// (e.g. the Tanara Cloud chat).
//
// One object prepares ONE task: start() once, then exactly one of ready() / failed(), unless cancelled.
#include "tanara/domain/Job.h"
#include "tanara/providers/chat/LlmContext.h"
#include "tanara/providers/core/ProviderDescriptor.h"

#include <QObject>

#include <functional>

namespace tanara::providers {

class ILocalModelServer : public QObject {
    Q_OBJECT
public:
    explicit ILocalModelServer(QObject* parent = nullptr) : QObject(parent) {}
    ~ILocalModelServer() override = default;

    // True if Tanara is currently running ANOTHER request on the model (then it must not be unloaded).
    virtual void setBusyCheck(std::function<bool()> busy) = 0;
    // need: the task's estimated context need; floor: the requested minimum (0 = none).
    virtual void start(int need, int floor = 0) = 0;
    // Drops the running query / load; no signal follows.
    virtual void cancel() = 0;

    // What the server said about the model (valid after ready / failed).
    virtual const llmctx::LlmServerInfo& serverInfo() const = 0;
    // The model's context after ready (-1: unknown).
    virtual int loadedContext() const = 0;

signals:
    // A load / reload begins (the task's "Loading the model..." stage).
    void loadingStarted(int contextLength);
    // The task may start.
    void ready();
    // Preparation failed (the caller sets the kind).
    void failed(const tanara::JobError& error);
};

template <> struct ProviderTraits<ILocalModelServer> {
    static constexpr CapabilityKind kind = CapabilityKind::LocalModelServer;
};

} // namespace tanara::providers
