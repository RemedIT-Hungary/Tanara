#pragma once
//
// The summary files' JSON (library-internal): summary.json's Summary part, the editable topic list
// (summary.topics.json) and the finished per-topic analyses (summary.analyses.json).
// Only SummaryRepository uses it.
#include "tanara/domain/Summary.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QVector>

namespace tanara::library {

QJsonObject toJson(const ActionItem& a);
ActionItem  actionItemFromJson(const QJsonObject& o);

QJsonObject toJson(const Summary& s);
Summary     summaryFromJson(const QJsonObject& o);

// summary.topics.json: [{id, title, summary}]; a topic without a title is dropped on reading.
QJsonArray            topicsToJson(const QVector<SummaryTopic>& topics);
QVector<SummaryTopic> topicsFromJson(const QJsonArray& arr);

// summary.analyses.json: [{topicId, title, detail, decisions, actionItems}]; an analysis without a
// topicId is dropped on reading.
QJsonArray             analysesToJson(const QVector<TopicAnalysis>& analyses);
QVector<TopicAnalysis> analysesFromJson(const QJsonArray& arr);

} // namespace tanara::library
