#pragma once
//
// ComplexSummaryService — több körös, téma-bontásos összefoglaló LLM-rétege.
// Három hívás-típus, mindegyik EGY LLM round-trip + tipizált eredmény:
//   1. requestTopics        — a teljes átiratból téma-lista (1. kör)
//   2. requestTopicAnalysis — egy témára: összegző + döntések + teendők (2. kör)
//   3. requestReduce        — a per-téma elemzésekből globális összefoglaló + összevont teendők
// A sorrendet/szekvenciát az AppController vezényli (a lokális modell parallel=1 → szekvenciális).
// A provider NEM tulajdona (nem deletálja).
//
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

    // 1. kör — téma-kinyerés a teljes (renderelt) átiratból. systemPrompt üres → default.
    void requestTopics(const QString& transcriptMd, const QString& contextNotes,
                       const QString& systemPrompt, const QString& model = {},
                       double temperature = 0.2, int maxTokens = 4000);

    // 2. kör — egy téma elemzése (a TELJES átirat + a téma fókusza). A topicId/title
    // a kapott topicból öröklődik az eredménybe (a modell csak detail/decisions/items-et ad).
    void requestTopicAnalysis(const QString& transcriptMd, const SummaryTopic& topic,
                              const QString& contextNotes, const QString& systemPrompt,
                              const QString& model = {}, double temperature = 0.2,
                              int maxTokens = 4000);

    // reduce — a per-téma elemzésekből globális vezetői összefoglaló + összevont teendők.
    void requestReduce(const QVector<TopicAnalysis>& analyses, const QString& contextNotes,
                       const QString& model = {}, double temperature = 0.2, int maxTokens = 2000);

    // A beépített default prompt-ok (a UI „Visszaállítás" + a core fallback forrása).
    static QString defaultTopicPrompt();
    static QString defaultAnalysisPrompt();

    // A reduce rendszer-promptja. Üres (default) → a beépített ("reduce" a PromptLibrary-ből).
    // Az AppController a fájl-override-dal feloldott promptot adja itt át.
    void setReducePrompt(const QString& prompt) { m_reducePrompt = prompt; }
    // A célnyelv (a beállítás szövege) — a felhasználói üzenet végi nyelvi emlékeztetőhöz.
    void setLanguage(const QString& language) { m_language = language; }

signals:
    void topicsReady(const QVector<tanara::SummaryTopic>& topics);
    void topicAnalysisReady(const tanara::TopicAnalysis& analysis);
    void reduceReady(const QString& execSummary, const QVector<tanara::ActionItem>& actionItems);
    void failed(const QString& error);

private:
    ILlmProvider* m_provider;  // not owned
    QString m_reducePrompt;    // üres → beépített default
    QString m_language;        // üres → magyar
};

} // namespace tanara
