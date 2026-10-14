#pragma once
//
// FailureSink: the last FAILED HTTP exchange of a provider call, for the remembered job error's technical
// line (library::describeJobFailure). captureFailures hooks it into a ProviderContext's response observer.
// Shared by the summarization (processing) and the transcription chain (still in AppController until P12f).
#include "tanara/foundation/net/HttpClient.h"
#include "tanara/providers/core/ProviderContext.h"

#include <memory>

namespace tanara::processing {

struct FailureSink { bool has = false; net::HttpExchange ex; };
using FailureSinkPtr = std::shared_ptr<FailureSink>;

// Extends the context's response observer: besides the existing (cloud) observer, non-2xx responses and
// network errors are also stored in the sink. The failure of cleanup DELETEs does not count.
inline void captureFailures(providers::ProviderContext& ctx, const FailureSinkPtr& sink)
{
    const auto prev = ctx.observe;
    ctx.observe = [prev, sink](const net::HttpExchange& ex) {
        if (prev) prev(ex);
        const bool ok = ex.status >= 200 && ex.status < 300;
        if (!ok && ex.method != "DELETE") { sink->has = true; sink->ex = ex; }
    };
}

} // namespace tanara::processing
