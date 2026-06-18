#pragma once
//
// SummaryService — a merge-elt transcriptből LLM-mel strukturált Summary-t készít.
// A provider NEM tulajdona (nem deletálja).
//
#include "tanara/Types.h"
#include "tanara/llm/ILlmProvider.h"

#include <QObject>
#include <QString>
#include <QStringList>

namespace tanara {

class SummaryService : public QObject {
    Q_OBJECT
public:
    explicit SummaryService(ILlmProvider* provider, QObject* parent = nullptr);
    ~SummaryService() override;

    // Elindít egy összefoglaló-kérést. Az eredmény a summaryReady / summaryFailed
    // signalon keresztül érkezik (aszinkron).
    void summarize(const MergedTranscript& transcript,
                   const QString& contextNotes,
                   const QStringList& glossary,
                   const QString& model = {},
                   double temperature = 0.2,
                   int maxTokens = 8000);

signals:
    void summaryReady(const tanara::Summary& summary);
    void summaryFailed(const QString& error);

private:
    ILlmProvider* m_provider;  // not owned
};

} // namespace tanara
