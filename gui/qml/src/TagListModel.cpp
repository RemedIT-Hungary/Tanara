#include "TagListModel.h"

namespace tanara_qml {

TagListModel::TagListModel(QObject* parent) : QAbstractListModel(parent) {}

int TagListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QHash<int, QByteArray> TagListModel::roleNames() const
{
    return {{IdRole, "tagId"}, {NameRole, "name"}, {MetaRole, "meta"},
            {BeforeRole, "before"}, {MatchRole, "match"}, {AfterRole, "after"}};
}

QVariant TagListModel::data(const QModelIndex& index, int role) const
{
    const int row = index.row();
    if (row < 0 || row >= m_rows.size()) return {};
    const TagListRow& r = m_rows[row];
    switch (role) {
    case IdRole: return r.id;
    case NameRole: return r.name;
    case MetaRole: return r.meta;
    case BeforeRole: return r.before;
    case MatchRole: return r.match;
    case AfterRole: return r.after;
    default: return {};
    }
}

void TagListModel::setRows(const QVector<TagListRow>& rows)
{
    bool sameOrder = rows.size() == m_rows.size();
    for (int i = 0; sameOrder && i < rows.size(); ++i) sameOrder = rows[i].id == m_rows[i].id;
    if (!sameOrder) {
        const bool countDiffers = rows.size() != m_rows.size();
        beginResetModel();
        m_rows = rows;
        endResetModel();
        if (countDiffers) emit countChanged();
        return;
    }
    for (int i = 0; i < rows.size(); ++i) {
        if (rows[i] == m_rows[i]) continue;
        m_rows[i] = rows[i];
        emit dataChanged(index(i), index(i));
    }
}

int TagListModel::indexOfId(const QString& id) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].id == id) return i;
    return -1;
}

QString TagListModel::idAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows[row].id : QString();
}

} // namespace tanara_qml
