#pragma once
//
// Handling of the LLM context size: PURE (I/O-free) building blocks:
//  - token estimate from characters, and the context need of a call (input + output budget),
//  - the standard context steps (8k / 16k / 20k / 32k ...),
//  - recognizing the "does not fit the context" error from the server's response (LM Studio /
//    llama.cpp exceed_context_size_error, OpenAI context_length_exceeded, vLLM / text forms),
//  - parsing LM Studio's native API (GET /api/v1/models): the configured model's loaded
//    instances (context, parallelism) and the model's maximum,
//  - the pre-load decision (keep / load / reload).
// The network part (query, load) is done by the classes of LlmServer.h.
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::providers {
namespace llmctx {

// Character / token ratio for the estimate. Measured on Hungarian transcripts (Gemma / Qwen
// tokenizer) about 2.5 characters per token; for English prompts this overestimates (~4 there),
// which is the safe direction for the context need.
constexpr double kCharsPerToken = 2.5;
// Tokens of the chat template (role markers, system header) per call.
constexpr int kTemplateOverheadTokens = 256;
// Safety margin on the input estimate (+10%).
constexpr double kSafetyFactor = 1.10;
// A part must carry at least this many tokens of transcript for per-part notes to make sense (~2-3
// minutes of speech); with a smaller context we do not split further.
constexpr int kMinPartTokens = 1000;

// Estimated token count from a character count (rounded up).
int estimateTokens(qint64 chars);
// A call's context need: the input's (system prompt + user message) estimated tokens with the
// template and the margin, plus the output budget (max_tokens).
int callContextNeed(qint64 inputChars, int outputTokens);
// The inverse of callContextNeed(): with a given context, at most how many characters of transcript
// fit in a call (besides the prompt and the output budget). -1 if not even kMinPartTokens fits.
int partBudgetChars(int contextTokens, qint64 promptChars, int outputTokens);
// Context need of a typical summary part (~15 minutes of Hungarian speech + notes prompt + 3000
// tokens of notes), for the connection test's warning.
int typicalSummaryPartNeed();

// The standard context steps, in ascending order.
QVector<int> contextSteps();
// The smallest step covering the need; with maxContext > 0 at most that. If the need exceeds every
// step, the need rounded up to 1024 (also up to the maximum).
int contextStepFor(int need, int maxContext = 0);

// Data of the "does not fit the context" error.
struct ContextOverflow {
    bool matched = false;
    int promptTokens = -1;      // tokens of the request (if the server said)
    int contextTokens = -1;     // the loaded context (n_ctx / maximum context length)
    QString code;               // exceed_context_size_error | context_length_exceeded | …
};
// Recognition from the error body and/or the error text. The body may be LM Studio / llama.cpp
// ({"error":{"type":"exceed_context_size_error","n_prompt_tokens":...,"n_ctx":...}}), the same JSON
// embedded in the text ("Engine protocol predict request returned 400: {...}"), OpenAI
// ({"error":{"code":"context_length_exceeded","message":"... maximum context length is 8192
// tokens. However, your messages resulted in 9000 tokens ..."}}) or vLLM-like text.
ContextOverflow parseContextOverflow(const QByteArray& body, const QString& text = QString());

// ---- LM Studio native API ----------------------------------------------------------------

struct LlmInstanceInfo {
    QString id;                 // id of the instance (e.g. "qwen3.8-27b" or "qwen3.8-27b:2")
    int contextLength = -1;     // -1 = unknown
    int parallel = -1;          // -1 = unknown (older server: treated as 1)
};

struct LlmServerInfo {
    enum class Kind { Unknown, LmStudio };
    Kind kind = Kind::Unknown;  // Unknown: no native API (llama.cpp, Ollama, vLLM, cloud ...)
    QString model;              // the configured model (the one the query was about)
    bool modelListed = false;   // the configured model is in the server's list
    QString modelKey;           // the list's key (sent for loading)
    int maxContext = -1;        // the model's max_context_length
    QVector<LlmInstanceInfo> instances;   // loaded instances of the configured model
    QStringList otherLoaded;    // keys of OTHER loaded models (LLMs)
    bool isLmStudio() const { return kind == Kind::LmStudio; }
    bool loaded() const { return !instances.isEmpty(); }
};

// The native API root from the configured (OpenAI-compatible) address: the trailing "/v1" (and "/")
// stripped: "http://localhost:1234/v1" -> "http://localhost:1234".
QString nativeApiRoot(const QString& baseUrl);
// Parsing the GET /api/v1/models response for the configured model. An unparseable body -> Kind::Unknown.
LlmServerInfo parseLmStudioModels(const QByteArray& json, const QString& model);

// The pre-load decision.
struct PreloadDecision {
    enum class Action {
        Skip,      // nothing to do / cannot (not LM Studio, the model is not in the list)
        None,      // the loaded instance is fine
        Load,      // not loaded -> load
        Reload,    // loaded, but the context is small or parallel > 1 -> unload + load
    };
    Action action = Action::Skip;
    int contextLength = 0;      // the context to request (Load / Reload)
    QStringList unloadIds;      // Reload: the configured model's instances to replace
    // The context to request is smaller than the estimated need (because of a fixed setting or the
    // model's maximum); the run starts anyway and the summary splits into smaller parts.
    bool contextInsufficient = false;
    QString reason;             // machine reason: not-lmstudio | model-unlisted | ok | not-loaded |
                                //          context-small | parallel
};
// need: the task's estimated need; setting: ProviderConfig::contextLength (0 = automatic);
// floor: the minimum requested by the user ("Load with a larger context"), 0 = none.
PreloadDecision decidePreload(const LlmServerInfo& info, int need, int setting, int floor = 0);

} // namespace llmctx
} // namespace tanara::providers