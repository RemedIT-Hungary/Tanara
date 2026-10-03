#include "TopicListModel.h"

#include "tanara/AppController.h"

#include <QHash>
#include <QUuid>

namespace tanara_qml {

using namespace tanara;

bool TopicListModel::Item::operator==(const Item& o) const
{
    return topic.id == o.topic.id && topic.title == o.topic.title && topic.summary == o.topic.summary
        && state == o.state && error == o.error && errorDetail == o.errorDetail
        && hasResult == o.hasResult && result.detail == o.result.detail
        && result.decisions == o.result.decisions
        && result.actionItems.size() == o.result.actionItems.size();
}

TopicListModel::TopicListModel(QObject* parent) : QAbstractListModel(parent) {}

QString TopicListModel::stateName(TopicState state)
{
    switch (state) {
    case TopicState::Queued:  return QStringLiteral("queued");
    case TopicState::Running: return QStringLiteral("running");
    case TopicState::Done:    return QStringLiteral("done");
    case TopicState::Failed:  return QStringLiteral("failed");
    case TopicState::Waiting: break;
    }
    return QStringLiteral("waiting");
}

int TopicListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant TopicListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const Item& it = m_items.at(index.row());
    switch (role) {
    case TopicIdRole:     return it.topic.id;
    case Qt::DisplayRole:
    case TitleRole:       return it.topic.title;
    case SummaryRole:     return it.topic.summary;
    case StateRole:       return stateName(it.state);
    case ErrorRole:       return it.error;
    case ErrorDetailRole: return it.errorDetail;
    case HasResultRole:   return it.hasResult;
    case ResultTextRole:  return it.result.detail;
    case ResultDecisionsRole: return it.result.decisions;
    case ResultActionsRole: {
        QVariantList list;
        for (const ActionItem& a : it.result.actionItems)
            list << QVariantMap{{QStringLiteral("text"), a.text}, {QStringLiteral("owner"), a.owner},
                                {QStringLiteral("due"), a.due}};
        return list;
    }
    default: break;
    }
    return {};
}

QHash<int, QByteArray> TopicListModel::roleNames() const
{
    return {
        {TopicIdRole, "topicId"}, {TitleRole, "title"}, {SummaryRole, "summary"},
        {StateRole, "topicState"}, {ErrorRole, "error"}, {ErrorDetailRole, "errorDetail"},
        {HasResultRole, "hasResult"}, {ResultTextRole, "resultText"},
        {ResultDecisionsRole, "resultDecisions"}, {ResultActionsRole, "resultActions"},
    };
}

QString TopicListModel::countsText() const
{
    int done = 0, running = 0, queued = 0, failed = 0, waiting = 0;
    for (const Item& it : m_items) {
        switch (it.state) {
        case TopicState::Done:    ++done; break;
        case TopicState::Running: ++running; break;
        case TopicState::Queued:  ++queued; break;
        case TopicState::Failed:  ++failed; break;
        case TopicState::Waiting: ++waiting; break;
        }
    }
    QStringList parts{tr("%n téma", nullptr, int(m_items.size()))};
    if (done)    parts << tr("%1 kész").arg(done);
    if (running) parts << tr("%1 fut").arg(running);
    if (queued)  parts << tr("%1 sorban áll").arg(queued);
    if (failed)  parts << tr("%1 hibás").arg(failed);
    if (waiting) parts << tr("%1 vár").arg(waiting);
    return parts.join(QStringLiteral(" · "));
}

int TopicListModel::missingCount() const
{
    int n = 0;
    for (const Item& it : m_items)
        if (it.state == TopicState::Waiting || it.state == TopicState::Failed) ++n;
    return n;
}

int TopicListModel::doneCount() const
{
    int n = 0;
    for (const Item& it : m_items)
        if (it.state == TopicState::Done) ++n;
    return n;
}

bool TopicListModel::busy() const
{
    for (const Item& it : m_items)
        if (it.state == TopicState::Running || it.state == TopicState::Queued) return true;
    return false;
}

void TopicListModel::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countsChanged();
}

void TopicListModel::bind(AppController* controller, const QString& meetingId)
{
    if (controller != m_controller) {
        for (const QMetaObject::Connection& c : std::as_const(m_connections))
            disconnect(c);
        m_connections.clear();
        m_controller = controller;
        if (controller) {
            auto mine = [this](const QString& id) { return !m_meetingId.isEmpty() && id == m_meetingId; };
            m_connections << connect(controller, &AppController::topicsChanged, this,
                                     [this, mine](const QString& id) { if (mine(id)) sync(); });
            m_connections << connect(controller, &AppController::topicStatusChanged, this,
                                     [this, mine](const QString& id, const QString&) { if (mine(id)) sync(); });
            m_connections << connect(controller, &AppController::topicAnalysisReady, this,
                                     [this, mine](const QString& id, const TopicAnalysis&) { if (mine(id)) sync(); });
            m_connections << connect(controller, &AppController::topicAnalysisFailed, this,
                                     [this, mine](const QString& id, const QString&, const QString&) { if (mine(id)) sync(); });
        }
    }
    const bool meetingChanged = meetingId != m_meetingId;
    m_meetingId = meetingId;
    if (meetingChanged) {
        // Másik meeting: a sorok nem feleltethetők meg egymásnak → teljes csere.
        beginResetModel();
        m_items.clear();
        endResetModel();
    }
    sync();
}

void TopicListModel::sync()
{
    QVector<Item> fresh;
    if (m_controller && !m_meetingId.isEmpty()) {
        const QVector<SummaryTopic> topics = m_controller->meetingTopics(m_meetingId);
        QHash<QString, TopicStatus> statuses;
        for (const TopicStatus& s : m_controller->topicStatuses(m_meetingId))
            statuses.insert(s.topicId, s);
        QHash<QString, TopicAnalysis> analyses;
        for (const TopicAnalysis& a : m_controller->topicAnalyses(m_meetingId))
            analyses.insert(a.topicId, a);
        fresh.reserve(topics.size());
        for (const SummaryTopic& t : topics) {
            Item it;
            it.topic = t;
            const TopicStatus st = statuses.value(t.id);
            it.state = st.state;
            it.error = st.error;
            it.errorDetail = st.errorDetail;
            const auto a = analyses.constFind(t.id);
            if (a != analyses.constEnd()) {
                it.hasResult = true;
                it.result = *a;
            }
            fresh.append(it);
        }
    }

    auto sameIds = [](const QVector<Item>& a, const QVector<Item>& b, int n) {
        for (int i = 0; i < n; ++i)
            if (a[i].topic.id != b[i].topic.id) return false;
        return true;
    };

    if (fresh.size() == m_items.size() && sameIds(fresh, m_items, int(fresh.size()))) {
        // Azonos sorrend: csak a változott sorok jeleznek.
        for (int i = 0; i < fresh.size(); ++i) {
            if (fresh[i] == m_items[i]) continue;
            m_items[i] = fresh[i];
            emit dataChanged(index(i), index(i));
        }
    } else if (fresh.size() > m_items.size() && sameIds(fresh, m_items, int(m_items.size()))) {
        // A végére került új téma (hozzáadás / javaslat).
        for (int i = 0; i < m_items.size(); ++i) {
            if (fresh[i] == m_items[i]) continue;
            m_items[i] = fresh[i];
            emit dataChanged(index(i), index(i));
        }
        const int first = int(m_items.size());
        beginInsertRows(QModelIndex(), first, int(fresh.size()) - 1);
        m_items = fresh;
        endInsertRows();
    } else {
        beginResetModel();
        m_items = fresh;
        endResetModel();
    }
    emit countsChanged();
}

QVector<SummaryTopic> TopicListModel::topicList() const
{
    QVector<SummaryTopic> list;
    list.reserve(m_items.size());
    for (const Item& it : m_items)
        list.append(it.topic);
    return list;
}

void TopicListModel::persist()
{
    if (m_controller && !m_meetingId.isEmpty())
        m_controller->setMeetingTopics(m_meetingId, topicList());   // → topicsChanged → sync()
    else
        emit countsChanged();
}

bool TopicListModel::addTopic(const QString& title, const QString& summary)
{
    const QString t = title.trimmed();
    if (t.isEmpty())
        return false;
    SummaryTopic topic;
    topic.title = t;
    topic.summary = summary.trimmed();
    if (m_controller && !m_meetingId.isEmpty()) {
        QVector<SummaryTopic> list = topicList();
        list.append(topic);                               // az azonosítót a core adja
        m_controller->setMeetingTopics(m_meetingId, list);
        return true;
    }
    topic.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    beginInsertRows(QModelIndex(), int(m_items.size()), int(m_items.size()));
    Item it;
    it.topic = topic;
    m_items.append(it);
    endInsertRows();
    emit countsChanged();
    return true;
}

bool TopicListModel::updateTopic(int row, const QString& title, const QString& summary)
{
    if (row < 0 || row >= m_items.size() || title.trimmed().isEmpty())
        return false;
    Item& it = m_items[row];
    if (it.topic.title == title.trimmed() && it.topic.summary == summary.trimmed())
        return true;
    it.topic.title = title.trimmed();
    it.topic.summary = summary.trimmed();
    emit dataChanged(index(row), index(row), {TitleRole, SummaryRole, Qt::DisplayRole});
    persist();
    return true;
}

bool TopicListModel::removeTopicById(const QString& topicId)
{
    if (topicId.isEmpty())
        return false;
    for (int row = 0; row < rowCount(); ++row) {
        if (topicIdAt(row) != topicId) continue;
        removeTopic(row);
        return true;
    }
    return false;
}

void TopicListModel::removeTopic(int row)
{
    if (row < 0 || row >= m_items.size())
        return;
    // A futó / sorban álló elemzést előbb leállítjuk — a törölt témára ne menjen hívás.
    if (m_controller && (m_items[row].state == TopicState::Running
                         || m_items[row].state == TopicState::Queued))
        m_controller->cancelTopicAnalysis(m_meetingId, m_items[row].topic.id);
    beginRemoveRows(QModelIndex(), row, row);
    m_items.removeAt(row);
    endRemoveRows();
    persist();
}

void TopicListModel::moveTopic(int from, int to)
{
    if (from < 0 || from >= m_items.size() || to < 0 || to >= m_items.size() || from == to)
        return;
    // A Qt beginMoveRows cél-indexe „a sor elé” értendő → lefelé mozgatásnál +1.
    beginMoveRows(QModelIndex(), from, from, QModelIndex(), to > from ? to + 1 : to);
    m_items.move(from, to);
    endMoveRows();
    persist();
}

void TopicListModel::cancelTopic(int row)
{
    if (row < 0 || row >= m_items.size() || !m_controller)
        return;
    m_controller->cancelTopicAnalysis(m_meetingId, m_items[row].topic.id);
}

QString TopicListModel::topicIdAt(int row) const
{
    return row >= 0 && row < m_items.size() ? m_items[row].topic.id : QString();
}

// Kitalált mintaadat (M08).
void TopicListModel::loadDemo()
{
    for (const QMetaObject::Connection& c : std::as_const(m_connections))
        disconnect(c);
    m_connections.clear();
    m_controller = nullptr;
    m_meetingId.clear();

    auto make = [](const char* id, const QString& title, const QString& summary, TopicState st) {
        Item it;
        it.topic = SummaryTopic{QString::fromLatin1(id), title, summary};
        it.state = st;
        return it;
    };
    QVector<Item> items;
    Item a = make("demo-1", tr("Támogatási igény alakulása"),
                  tr("Jegyszám, felhasználónkénti arány, okok"), TopicState::Done);
    a.hasResult = true;
    a.result.detail = tr("A jegyek száma harmadával csökkent, miközben az aktív felhasználók 12%-kal "
                         "nőttek. A csökkenés fő oka a termékbe épített súgó; a partnerek ugyanezt "
                         "tapasztalják.");
    Item b = make("demo-2", tr("Súgóoldalak kiterjesztése"),
                  tr("Melyik termékre, milyen sorrendben"), TopicState::Done);
    b.hasResult = true;
    b.result.detail = tr("A beállítási oldalak mintáját viszik tovább. A számlázás előtt a folyamatot "
                         "egyszerűsítik.");
    Item c = make("demo-3", tr("Számlázási folyamat"), tr("Termék vagy dokumentáció a gond?"),
                  TopicState::Running);
    Item d = make("demo-4", tr("Dokumentációs kapacitás"), tr("Létszámigény és költségvetés"),
                  TopicState::Failed);
    d.error = tr("A szolgáltató időtúllépés miatt megszakította.");
    Item e = make("demo-5", tr("Következő negyedév mérőszámai"), tr("Mit és hogyan mérünk"),
                  TopicState::Waiting);
    items << a << b << c << d << e;

    beginResetModel();
    m_items = items;
    endResetModel();
    emit countsChanged();
}

} // namespace tanara_qml
