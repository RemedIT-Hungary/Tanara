#include "tanara/store/MeetingListModel.h"
#include "tanara/store/MeetingStore.h"

namespace tanara {

MeetingListModel::MeetingListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

MeetingListModel::MeetingListModel(MeetingStore* store, QObject* parent)
    : QAbstractListModel(parent)
{
    setStore(store);
}

int MeetingListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_meetings.size();
}

QVariant MeetingListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_meetings.size())
        return {};

    const Meeting& m = m_meetings.at(index.row());
    switch (role) {
    case IdRole:                       return m.id;
    case TitleRole:
    case Qt::DisplayRole:              return m.title;
    case DateRole:                     return m.startedAt;
    case DurationRole:                 return static_cast<qlonglong>(m.durationMs);
    case HasSummaryRole:               return m.hasSummary;
    default:                           return {};
    }
}

QHash<int, QByteArray> MeetingListModel::roleNames() const
{
    return {
        { IdRole,         QByteArrayLiteral("meetingId") },
        { TitleRole,      QByteArrayLiteral("title") },
        { DateRole,       QByteArrayLiteral("date") },
        { DurationRole,   QByteArrayLiteral("durationMs") },
        { HasSummaryRole, QByteArrayLiteral("hasSummary") },
    };
}

void MeetingListModel::setMeetings(const QVector<Meeting>& meetings)
{
    beginResetModel();
    m_meetings = meetings;
    endResetModel();
}

void MeetingListModel::setStore(MeetingStore* store)
{
    if (m_store == store)
        return;

    if (m_store)
        m_store->disconnect(this);

    m_store = store;

    if (m_store) {
        connect(m_store, &MeetingStore::meetingAdded,
                this, &MeetingListModel::refreshFromStore);
        connect(m_store, &MeetingStore::meetingUpdated,
                this, &MeetingListModel::refreshFromStore);
        refreshFromStore();
    }
}

void MeetingListModel::refreshFromStore()
{
    if (!m_store)
        return;
    setMeetings(m_store->loadAll());
}

} // namespace tanara
