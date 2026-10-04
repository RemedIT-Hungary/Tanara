#include "PeopleListModel.h"

namespace tanara_qml {

PeopleListModel::PeopleListModel(QObject* parent) : QAbstractListModel(parent) {}

int PeopleListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QHash<int, QByteArray> PeopleListModel::roleNames() const
{
    return {{NameRole, "name"},
            {MonogramRole, "monogram"},
            {IsSelfRole, "isSelf"},
            {HasVoiceprintRole, "hasVoiceprint"},
            {MetaRole, "meta"},
            {HeaderRole, "header"},
            {BeforeRole, "before"},
            {MatchRole, "match"},
            {AfterRole, "after"}};
}

QVariant PeopleListModel::data(const QModelIndex& index, int role) const
{
    const int row = index.row();
    if (row < 0 || row >= m_rows.size()) return {};
    const PeopleListRow& r = m_rows[row];
    switch (role) {
    case NameRole: return r.name;
    case MonogramRole: return r.monogram;
    case IsSelfRole: return r.isSelf;
    case HasVoiceprintRole: return r.hasVoiceprint;
    case MetaRole: return r.meta;
    case HeaderRole: return r.header;
    case BeforeRole: return r.before;
    case MatchRole: return r.match;
    case AfterRole: return r.after;
    default: return {};
    }
}

void PeopleListModel::setRows(const QVector<PeopleListRow>& rows)
{
    bool sameOrder = rows.size() == m_rows.size();
    for (int i = 0; sameOrder && i < rows.size(); ++i)
        sameOrder = rows[i].name == m_rows[i].name;
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

int PeopleListModel::indexOfName(const QString& name) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].name.compare(name, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

QString PeopleListModel::nameAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows[row].name : QString();
}

} // namespace tanara_qml
