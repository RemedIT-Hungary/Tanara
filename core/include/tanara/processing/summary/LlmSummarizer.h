#pragma once
//
// LlmSummarizer: the local ISummarizer over ANY chat provider (D6): a local LLM, or the Tanara Cloud
// chat (the LLM-proxy path). A wrapper of the existing services, no new pipeline:
//  - quick:   SummaryService (one call for a short meeting, per-part notes + merge for a long one);
//  - complex: ComplexSummaryService: topic extraction -> one analysis per topic (in order: the local
//             model runs with parallel=1) -> reduce. The topic list is not edited in between (that is
//             the GUI's interactive flow, still orchestrated by AppController).
// No cache file: the request is self-contained (SummaryRequest::cachePath stays empty).
#include "tanara/providers/chat/IChatProvider.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/providers/summary/ISummarizer.h"

#include <QObject>
#include <QPointer>

namespace tanara::processing {

class LlmSummarizer : public QObject, public providers::ISummarizer {
    Q_OBJECT
public:
    static inline const QString Id = QStringLiteral("llm");

    // chat: not owned (it must outlive the jobs). cfg: the LLM config (model, temperature, max tokens)
    // used where the request leaves them empty.
    LlmSummarizer(providers::IChatProvider* chat, const providers::ProviderConfig& cfg, QObject* parent = nullptr);

    // Sets the chat provider (the registry factory creates it as this object's child).
    void setChatProvider(providers::IChatProvider* chat) { m_chat = chat; }

    QString name() const override { return Id; }
    providers::SummarizationJob* summarize(const providers::SummarizationRequest& req) override;

private:
    providers::IChatProvider* m_chat;
    providers::ProviderConfig m_cfg;
};

} // namespace tanara::processing
