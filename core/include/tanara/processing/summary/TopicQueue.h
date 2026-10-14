#pragma once
//
// TopicQueue: the complex (multi-pass) summary's interactive flow, moved unchanged from AppController
// (cleanup phase 2, P12e). Part of SummarizationService (it shares its LLM runs, preparation, persistence).
//  - pass 1: topic extraction from the full transcript (summary.topics.json; the UI edits the list);
//  - pass 2: the topic-analysis job queue: ONE LLM call runs at a time (local model, parallel=1), the
//    other topics wait in line; every finished analysis is persisted IMMEDIATELY (summary.analyses.json).
//    One topic's failure does not stop the queue (a Tanara Cloud error drops the meeting's remaining
//    topics: they would get the same answer);
//  - reduce: the executive summary from the analyses on disk, the action items merged in code
//    -> summary.md / summary.json (SummarizationService::summaryReady).
// Tanara Cloud: the complex summary (topic gathering + topic analyses + reduce) is ONE run with one
// X-Tanara-Job-Id while it stays open.
#include "tanara/domain/Job.h"
#include "tanara/domain/Summary.h"
#include "tanara/processing/summary/SummarizationService.h"
#include "tanara/processing/summary/SummaryLlmPort.h"

#include <QHash>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QVector>

namespace tanara::processing {

class TopicQueue : public QObject {
    Q_OBJECT
public:
    explicit TopicQueue(SummarizationService* owner);
    ~TopicQueue() override;

    // Pass 1: extracts the topics (topicsReady). If a (possibly edited) list exists, that is returned.
    void extractTopics(const QString& meetingId);
    // Pass 2 + reduce (batch): of the (edited) topics queues only those not yet analyzed, then, if
    // nothing failed, the reduce. The topic list is persisted first (resumable).
    void generate(const QString& meetingId, const QVector<SummaryTopic>& topics);
    // (Re)starts the analysis of ONE topic (carried into summary.topics.json); no reduce at the end.
    void analyze(const QString& meetingId, const SummaryTopic& topic);
    // Reduce from the analyses ON DISK (in topics.json order) -> summary.md + summaryReady.
    void finalize(const QString& meetingId);

    QVector<SummaryTopic>  topics(const QString& meetingId) const;     // summary.topics.json
    QVector<TopicAnalysis> analyses(const QString& meetingId) const;   // summary.analyses.json
    // Per-topic state at any time: running / queued / failed (remembered error) / done / waiting.
    QVector<TopicStatus> statuses(const QString& meetingId) const;
    // Saves the topic list in the given ORDER (new ids, empty titles dropped, deleted topics cancelled
    // and their remembered errors cleared). topicsChanged.
    QVector<SummaryTopic> setTopics(const QString& meetingId, const QVector<SummaryTopic>& topics);
    // Removes a single topic from the queue / cancels its running analysis.
    bool cancelTopic(const QString& meetingId, const QString& topicId);
    // Cancels the meeting's AnalyzeTopics batch: the queued topics leave, the running one is cancelled;
    // the finished analyses stay on disk (a continuation picks up from there).
    bool cancelBatch(const QString& meetingId);

    // A topic analysis is running now (the LLM is busy with it).
    bool busy() const { return m_active; }

signals:
    void topicsReady(QString meetingId, QVector<tanara::SummaryTopic> topics);
    void topicAnalysisQueued(QString meetingId, QString topicId);
    void topicAnalysisStarted(QString meetingId, QString topicId);
    void topicAnalysisReady(QString meetingId, tanara::TopicAnalysis analysis);
    void topicAnalysisFailed(QString meetingId, QString topicId, QString error);
    void topicAnalysisQueueFinished(QString meetingId, int okCount, int failCount);
    void topicsChanged(QString meetingId);
    void topicStatusChanged(QString meetingId, QString topicId);
    void jobProgress(QString meetingId, QString message);
    void errorOccurred(QString message);

private:
    using Run = SummaryLlmPort::Run;
    struct TopicJob { QString meetingId; SummaryTopic topic; };

    void enqueue(const QString& meetingId, const QVector<SummaryTopic>& topics, bool reduceWhenDone);
    void startNext();
    void updateCounts(const QString& meetingId);
    bool isQueued(const QString& meetingId, const QString& topicId) const;
    void clearActive();

    SummarizationService* m_owner;
    QVector<TopicJob> m_queue;
    bool    m_active = false;
    SummarizationService::ActiveCall m_activeCall;   // the running analysis's call (for cancelling)
    QString m_activeMeetingId;   // addressee of the running job (dedup + lifecycle signals)
    QString m_activeTopicId;
    QSet<QString> m_reduceWhenDone;             // meetingIds where an auto-reduce follows at the end of the queue
    QHash<QString, QPair<int, int>> m_counts;   // meetingId -> (ok, fail) in the current batch
    QHash<QString, int> m_batchTotal;           // meetingId -> number of topics queued into the current batch
    // Per meeting the cloud run of the open complex summary (topic gathering -> topic analyses -> reduce):
    // ONE X-Tanara-Job-Id, the per-call charges summed up. Removed on completion (done / error / cancel);
    // a continuation is thus a new run.
    QHash<QString, Run> m_complexRuns;
};

} // namespace tanara::processing
