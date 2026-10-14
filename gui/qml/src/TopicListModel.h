#pragma once
//
// Tanara QML: the per-topic analysis' topic list: per card a title, description, state
// (waiting / queued / running / done / failed with a message) and the finished analysis' result.
//
// The disk is the truth (summary.topics.json + summary.analyses.json + the leftover errors),
// so after a restart it continues where it left off. Every edit (add, rewrite, delete,
// reorder) persists immediately through ISummaryFacade::setTopics. On the facade's signals
// (topicsChanged / topicStatusChanged / topicAnalysisReady) it refreshes PER ROW: with an
// unchanged topic order only dataChanged goes out, a full reset only on an unknown reorder.
//
// SummaryViewModel owns it (from QML: summaryVm.topics); with the fictional facade it works with
// sample data, and editing then happens in the fixture's memory.
//
#include "tanara/domain/Types.h"
#include "tanara/domain/Job.h"
#include "tanara/processing/summary/ISummaryFacade.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class TopicListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("A TopicListModel-t a SummaryViewModel adja (topics).")

    Q_PROPERTY(int count READ count NOTIFY countsChanged)
    // "5 topics · 2 done · 1 running · 1 failed · 1 waiting"
    Q_PROPERTY(QString countsText READ countsText NOTIFY countsChanged)
    // How many topics have no finished analysis and are neither running nor queued (-> "Analyze missing").
    Q_PROPERTY(int missingCount READ missingCount NOTIFY countsChanged)
    Q_PROPERTY(int doneCount READ doneCount NOTIFY countsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY countsChanged)   // there is a running or queued topic

public:
    enum Role {
        TopicIdRole = Qt::UserRole + 1,
        TitleRole,
        SummaryRole,          // the topic's short description
        StateRole,            // waiting | queued | running | done | failed
        ErrorRole,
        ErrorDetailRole,
        HasResultRole,
        ResultTextRole,       // the finished analysis' text (markdown)
        ResultDecisionsRole,  // QStringList
        ResultActionsRole,    // [{ text, owner, due }]
        ResultOpenQuestionsRole,  // QStringList: the questions left open in the topic
        // The error's fix actions (see jobsupport::FixAction): the Settings button's label / page,
        // or for a context error with LM Studio the reload token count (0 = none).
        FixActionLabelRole,
        FixActionPageRole,
        FixReloadContextRole,
    };

    explicit TopicListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_items.size()); }
    QString countsText() const;
    int missingCount() const;
    int doneCount() const;
    bool busy() const;

    // Sets the source: the facade and the meeting (nullptr facade: no topics).
    void bind(tanara::processing::ISummaryFacade* facade, const QString& meetingId);
    void clear();

    // Editing: all persist immediately. With an empty title nothing happens (false).
    Q_INVOKABLE bool addTopic(const QString& title, const QString& summary);
    Q_INVOKABLE bool updateTopic(int row, const QString& title, const QString& summary);
    Q_INVOKABLE void removeTopic(int row);
    // By id (after a confirm dialog the row index may have shifted meanwhile). False = no such one.
    Q_INVOKABLE bool removeTopicById(const QString& topicId);
    Q_INVOKABLE void moveTopic(int from, int to);
    // Cancels the running / queued analysis of a single topic.
    Q_INVOKABLE void cancelTopic(int row);
    Q_INVOKABLE QString topicIdAt(int row) const;

    static QString stateName(tanara::TopicState state);

signals:
    void countsChanged();

private:
    struct Item {
        tanara::SummaryTopic topic;
        tanara::TopicState state = tanara::TopicState::Waiting;
        QString error;
        QString errorDetail;
        QString fixActionHint;
        bool hasResult = false;
        tanara::TopicAnalysis result;
        bool operator==(const Item& o) const;
    };

    void sync();                 // reads the disk state, building a per-row difference
    void persist();              // saves the current order (setMeetingTopics)
    QVector<tanara::SummaryTopic> topicList() const;

    QPointer<tanara::processing::ISummaryFacade> m_facade;
    QList<QMetaObject::Connection> m_connections;
    QString m_meetingId;
    QVector<Item> m_items;
};

} // namespace tanara_qml
