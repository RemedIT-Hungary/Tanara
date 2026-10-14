#pragma once
//
// Summarization port (CapabilityKind::Summarization): a whole summary of a transcript (D6: both an
// LLM proxy and a server-side pipeline can stand behind it).
//
//  - processing::LlmSummarizer: the local pipeline (SummaryService / ComplexSummaryService) over ANY
//    IChatProvider: a local LLM, or the Tanara Cloud chat (the LLM-proxy path);
//  - cloud::CloudPipelineSummarizer: the gateway's server-side pipeline (/v1/summaries + polling).
//
// Quick mode returns the Summary; complex mode also the topic list and the per-topic analyses. The
// result is plain domain data; persisting it (library::SummaryDocument) is the caller's business.
#include "tanara/domain/Summary.h"
#include "tanara/domain/Transcript.h"
#include "tanara/providers/core/CapabilityJob.h"
#include "tanara/providers/core/ProviderDescriptor.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::providers {

enum class SummarizationMode { Quick, Complex };
QString summarizationModeName(SummarizationMode mode);   // "quick" | "complex"

// System prompts in FINAL form (override + language applied). Empty = the built-in one.
struct SummarizationPrompts {
    QString single, notes, merge;          // quick
    QString topics, analysis, reduce;      // complex
};

struct SummarizationRequest {
    SummarizationMode mode = SummarizationMode::Quick;
    QString language;                      // target language (the setting's text)
    SummarizationPrompts prompts;
    QString contextNote;                   // the meeting note
    QStringList glossary;
    MergedTranscript transcript;           // speakers already resolved
    // The transcript's utterances (source references); empty = derived from the transcript.
    QVector<summarysrc::SourceLine> lines;
    QString model;                         // empty = the provider's config
    double  temperature = 0.2;
    int     maxTokens = 8000;
};

struct SummarizationResult {
    SummarizationMode mode = SummarizationMode::Quick;
    Summary summary;                       // quick: everything; complex: exec summary + action items
    QVector<SummaryTopic>  topics;         // complex only
    QVector<TopicAnalysis> analyses;       // complex only, in topic order
    QString markdown;                      // the rendered document, if the provider gives one
    QString model;                         // the model actually used, if known
};

using SummarizationJob = ResultJob<SummarizationResult>;

class ISummarizer {
public:
    virtual ~ISummarizer() = default;
    virtual QString name() const = 0;
    virtual SummarizationJob* summarize(const SummarizationRequest& req) = 0;
};

template <> struct ProviderTraits<ISummarizer> {
    static constexpr CapabilityKind kind = CapabilityKind::Summarization;
};

} // namespace tanara::providers
