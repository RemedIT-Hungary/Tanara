#pragma once
//
// ComplexSummaryService: LLM layer of the multi-pass, topic-based summary.
// Three call types, each ONE LLM round trip with a typed result:
//   1. requestTopics        - topic list from the full transcript (pass 1)
//   2. requestTopicAnalysis - per topic: summary + decisions + action items (pass 2)
//   3. requestReduce        - global summary + merged action items from the per-topic analyses
// AppController sequences the calls (the local model runs with parallel=1, so strictly in order).
// The provider is NOT owned (never deleted).
#include "tanara/Types.h"
#include "tanara/llm/ILlmProvider.h"

#include <QObject>
#include <QString>
#include <QVector>

namespace tanara {

class ComplexSummaryService : public QObject {
    Q_OBJECT
public:
    explicit ComplexSummaryService(ILlmProvider* provider, QObject* parent = nullptr);
    ~ComplexSummaryService() override;

    // Pass 1: topic extraction from the full (rendered) transcript. Empty systemPrompt = default.
    void requestTopics(const QString& transcriptMd, const QString& contextNotes,
                       const QString& systemPrompt, const QString& model = {},
                       double temperature = 0.2, int maxTokens = 4000);

    // Pass 2: analysis of one topic (the FULL transcript + the topic's focus). topicId/title
    // are inherited from the given topic into the result (the model only returns detail/decisions/items).
    void requestTopicAnalysis(const QString& transcriptMd, const SummaryTopic& topic,
                              const QString& contextNotes, const QString& systemPrompt,
                              const QString& model = {}, double temperature = 0.2,
                              int maxTokens = 4000);

    // Reduce: global executive summary + merged action items from the per-topic analyses.
    void requestReduce(const QVector<TopicAnalysis>& analyses, const QString& contextNotes,
                       const QString& model = {}, double temperature = 0.2, int maxTokens = 2000);

    // The built-in default prompts (source of the UI "Reset" and of the core fallback).
    static QString defaultTopicPrompt();
    static QString defaultAnalysisPrompt();

    // System prompt of the reduce step. Empty (default) = the built-in ("reduce" from PromptLibrary).
    // AppController passes the prompt resolved with the file override here.
    void setReducePrompt(const QString& prompt) { m_reducePrompt = prompt; }
    // The target language (the setting's text), for the language reminder at the end of the user message.
    void setLanguage(const QString& language) { m_language = language; }

signals:
    void topicsReady(const QVector<tanara::SummaryTopic>& topics);
    void topicAnalysisReady(const tanara::TopicAnalysis& analysis);
    void reduceReady(const QString& execSummary, const QVector<tanara::ActionItem>& actionItems);
    void failed(const QString& error);

private:
    ILlmProvider* m_provider;  // not owned
    QString m_reducePrompt;    // empty = built-in default
    QString m_language;        // empty = Hungarian
};

} // namespace tanara
