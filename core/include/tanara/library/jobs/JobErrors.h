#pragma once
//
// A failed background task -> a displayable error (human explanation + a technical line).
// Pure functions (no state, no I/O); AppController calls them with the provider's last failed
// HTTP response (ProviderConfig::onExchange) and the raw error text.
#include "tanara/domain/Job.h"
#include "tanara/providers/chat/LlmContext.h"

namespace tanara::cloud { struct HttpExchange; }
namespace tanara::cloud { struct CloudError; }

namespace tanara::library {


// The technical line of a failed HTTP exchange: "HTTP 401 | invalid_api_key | <message>".
// For a network error (no HTTP response): "network | <Qt error text>". From the body it recognizes
// the Soniox ({error_type, message}), the OpenAI-compatible ({error:{code|type, message}}) and the
// Tanara gateway ({error:{code, message, request_id}}) shapes.
QString httpFailureDetail(const cloud::HttpExchange& ex);

// A displayable error from the raw error text and (if any) the last failed HTTP exchange.
//  - message: a human explanation based on the status code (401/403 key, 402 balance, 429 quota,
//    5xx provider error, network error ...); the raw text for an unknown case.
//  - detail: httpFailureDetail(), or, without an HTTP exchange, empty (if message already is the raw
//    text), or the raw text (if message is generic).
//  - fixActionHint: "settings:stt" / "settings:llm" for key and address errors, otherwise empty.
// Known circumstances for fixing a "does not fit the model's context" error (AppController fills
// them from the task's estimated need and the server probe).
struct ContextFailureHint {
    int  recommendedContext = -1;   // context recommended for the task (tokens); -1 = unknown
    bool lmStudio = false;          // the server is LM Studio, so Tanara can reload the model itself
    bool cloud = false;             // Tanara Cloud: nothing to load, only the explanation
};

// Fix hints for the context error (JobError::fixActionHint):
//  "llm:reload-context:<N>"   - LM Studio: reload with a context of at least N tokens + retry,
//  "settings:llm-context:<N>" - another server: the model must be loaded there with a larger context
//                               (the Settings LLM card); N may be missing.
// From the hint: the token count (0 if none), and whether it is a reload.
int contextFixTokens(const QString& fixActionHint);
bool isReloadContextHint(const QString& fixActionHint);

JobError describeJobFailure(JobKind kind, const QString& rawMessage,
                            const cloud::HttpExchange* failedExchange = nullptr,
                            const ContextFailureHint* context = nullptr);

// Human description of a context overflow: "The model is loaded with a 4096-token context, the
// request was 6042 tokens: it does not fit." + what the user should do; the technical line without
// JSON ("HTTP 400 | exceed_context_size_error | request 6042 tokens | context 4096 tokens").
// httpStatus: 0 if there is no HTTP response data.
JobError describeContextOverflow(JobKind kind, const providers::llmctx::ContextOverflow& overflow,
                                 const ContextFailureHint& hint, int httpStatus = 0);

// From a Tanara Cloud structured error (the gateway's message is already in the user's language).
JobError describeCloudFailure(JobKind kind, const cloud::CloudError& error);

} // namespace tanara::library