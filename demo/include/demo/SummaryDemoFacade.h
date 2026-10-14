#pragma once
//
// SummaryDemoFacade: the fictional implementation of tanara::processing::ISummaryFacade for
// --demo, --gallery, --qml-shot and the tests. No meeting, no file, no provider is touched.
//
// The scenario (the Summary tab's design state, "stale" when empty) selects the fixture:
//   done / stale / memo / memoShort / oldSummary / oldMemo / running / topicsDoc : a finished summary
//     of a 76-minute "Quarterly partner meeting" (A family);
//   empty / emptyBlocked / emptyRunning* / emptyError* / emptyNote : no summary yet;
//   topics : a topic list with analyses in the per-topic workspace;
//   sourcesOn / sourcesOff / sourcePopover / memoSections / staleTargeted / noSources : the quick
//     summary with source links of a 2 h meeting with fictional utterances (B family).
// dismissStale / clearError / flagStatement / setTopics are remembered in memory.
#include "tanara/processing/summary/ISummaryFacade.h"

#include <QHash>
#include <QSet>

namespace tanara_demo {

class SummaryDemoFacade : public tanara::processing::ISummaryFacade {
    Q_OBJECT
public:
    explicit SummaryDemoFacade(const QString& scenario = {}, QObject* parent = nullptr);

    bool fictional() const override { return true; }
    std::optional<tanara::processing::SummaryUiPreset> uiPreset() const override;

    tanara::processing::SummaryData load(const QString& meetingId) const override;
    tanara::processing::SummaryJobs jobs(const QString& meetingId) const override;
    tanara::processing::SummarySpeakers speakers(const QString& meetingId) const override;
    tanara::processing::SummaryQuote quote(const QString& meetingId, const QString& utteranceId) const override;
    QString chatProviderName(const QString& providerId) const override;
    bool topicsResumable(const QString&) const override { return false; }

    bool flagStatement(const QString& meetingId, const QString& statementId) override;
    void dismissStale(const QString& meetingId) override;
    void clearError(const QString& meetingId) override;

    QVector<tanara::SummaryTopic> topics(const QString& meetingId) const override;
    QVector<tanara::TopicStatus> topicStatuses(const QString& meetingId) const override;
    QVector<tanara::TopicAnalysis> topicAnalyses(const QString& meetingId) const override;
    void setTopics(const QString& meetingId, const QVector<tanara::SummaryTopic>& topics) override;
    void cancelTopic(const QString&, const QString&) override {}

private:
    tanara::processing::SummaryData build() const;
    void buildEmpty(tanara::processing::SummaryData& d) const;
    void buildDone(tanara::processing::SummaryData& d) const;
    void buildSources(tanara::processing::SummaryData& d) const;
    tanara::processing::SummaryNotePin notePin() const;
    tanara::JobProgress demoJob(const QString& kind) const;
    QVector<tanara::MemoSection> demoMemo(bool longForm) const;
    void loadDemoTopics();

    QString m_state;
    bool m_staleDismissed = false;
    bool m_errorCleared = false;
    QSet<QString> m_flagged;
    QVector<tanara::SummaryTopic> m_topics;
    QHash<QString, tanara::TopicStatus> m_statuses;
    QHash<QString, tanara::TopicAnalysis> m_analyses;
};

} // namespace tanara_demo
