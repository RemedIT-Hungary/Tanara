#include "MeetingTableModel.h"

#include <QDateTime>

namespace tanara_gui {

namespace {
QString durationToMSS(qint64 ms) {
    const qint64 totalSec = ms / 1000;
    const qint64 mm = totalSec / 60;
    const qint64 ss = totalSec % 60;
    return QStringLiteral("%1:%2").arg(mm).arg(ss, 2, 10, QLatin1Char('0'));
}
} // namespace

MeetingTableModel::MeetingTableModel(QObject* parent)
    : QAbstractTableModel(parent) {}

int MeetingTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid())
        return 0;
    return m_meetings.size();
}

int MeetingTableModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid())
        return 0;
    return ColumnCount;
}

QVariant MeetingTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_meetings.size())
        return {};

    const tanara::Meeting& m = m_meetings.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColTime:
            return m.startedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        case ColDuration:
            return durationToMSS(m.durationMs);
        case ColName: {
            QString t = m.title;
            if (m.hasTranscript) t += QStringLiteral("  📝");
            if (m.hasSummary)    t += QStringLiteral("  ✓");
            return t;
        }
        default:
            return {};
        }
    }

    // EditRole = rendezési role (típushelyes nyers érték).
    if (role == Qt::EditRole) {
        switch (index.column()) {
        case ColTime:     return m.startedAt;
        case ColDuration: return static_cast<qlonglong>(m.durationMs);
        case ColName:     return m.title;
        default:          return {};
        }
    }

    return {};
}

QVariant MeetingTableModel::headerData(int section, Qt::Orientation orientation,
                                       int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return QAbstractTableModel::headerData(section, orientation, role);
    switch (section) {
    case ColTime:     return QStringLiteral("Idő");
    case ColDuration: return QStringLiteral("Hossz");
    case ColName:     return QStringLiteral("Név");
    default:          return {};
    }
}

void MeetingTableModel::setMeetings(QVector<tanara::Meeting> meetings) {
    beginResetModel();
    m_meetings = std::move(meetings);
    endResetModel();
}

tanara::Meeting MeetingTableModel::meetingAt(int row) const {
    if (row < 0 || row >= m_meetings.size())
        return {};
    return m_meetings.at(row);
}

QString MeetingTableModel::idAt(int row) const {
    if (row < 0 || row >= m_meetings.size())
        return {};
    return m_meetings.at(row).id;
}

} // namespace tanara_gui
