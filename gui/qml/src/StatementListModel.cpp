#include "StatementListModel.h"

namespace tanara_qml {

StatementListModel::StatementListModel(QObject* parent) : QAbstractListModel(parent) {}

int StatementListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QString StatementListModel::stamp(qint64 ms)
{
    if (ms < 0)
        return QString();
    const qint64 total = ms / 1000;
    return QStringLiteral("%1:%2").arg(total / 60, 2, 10, QLatin1Char('0'))
                                  .arg(total % 60, 2, 10, QLatin1Char('0'));
}

QVariantMap StatementListModel::spanMap(const Span& s)
{
    return {{QStringLiteral("startMs"), s.startMs}, {QStringLiteral("endMs"), s.endMs},
            {QStringLiteral("stamp"), stamp(s.startMs)},
            {QStringLiteral("utteranceIds"), s.utteranceIds},
            {QStringLiteral("affected"), s.affected}};
}

QVariantMap StatementListModel::rowMap(const Row& r)
{
    QVariantList spans;
    for (const Span& s : r.spans)
        spans << spanMap(s);
    return {{QStringLiteral("statementId"), r.id}, {QStringLiteral("kind"), r.kind},
            {QStringLiteral("text"), r.text}, {QStringLiteral("spans"), spans},
            {QStringLiteral("hasSources"), !r.spans.isEmpty()},
            {QStringLiteral("owner"), r.owner}, {QStringLiteral("owners"), r.owners},
            {QStringLiteral("due"), r.due}, {QStringLiteral("flagged"), r.flagged},
            {QStringLiteral("affected"), r.affected},
            {QStringLiteral("staleBecause"), r.staleBecause},
            {QStringLiteral("ownerStale"), r.ownerStale},
            {QStringLiteral("ownerStaleIndex"), r.ownerStaleIndex}};
}

QVariant StatementListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row& r = m_rows.at(index.row());
    switch (role) {
    case StatementIdRole: return r.id;
    case KindRole: return r.kind;
    case Qt::DisplayRole:
    case TextRole: return r.text;
    case SpansRole: return rowMap(r).value(QStringLiteral("spans"));
    case HasSourcesRole: return !r.spans.isEmpty();
    case OwnerRole: return r.owner;
    case OwnersRole: return r.owners;
    case DueRole: return r.due;
    case FlaggedRole: return r.flagged;
    case AffectedRole: return r.affected;
    case StaleBecauseRole: return r.staleBecause;
    case OwnerStaleRole: return r.ownerStale;
    case OwnerStaleIndexRole: return r.ownerStaleIndex;
    default: return {};
    }
}

QHash<int, QByteArray> StatementListModel::roleNames() const
{
    return {{StatementIdRole, "statementId"}, {KindRole, "kind"}, {TextRole, "text"},
            {SpansRole, "spans"}, {HasSourcesRole, "hasSources"}, {OwnerRole, "owner"},
            {OwnersRole, "owners"}, {DueRole, "due"}, {FlaggedRole, "flagged"},
            {AffectedRole, "affected"}, {StaleBecauseRole, "staleBecause"},
            {OwnerStaleRole, "ownerStale"}, {OwnerStaleIndexRole, "ownerStaleIndex"}};
}

QVariantList StatementListModel::rows() const
{
    QVariantList out;
    out.reserve(m_rows.size());
    for (const Row& r : m_rows)
        out << rowMap(r);
    return out;
}

void StatementListModel::setRows(const QVector<Row>& rows)
{
    // Változatlan tartalomnál nincs reset: a QML-elemek (és egy nyitott felugró) maradnak.
    if (rows.size() == m_rows.size()) {
        bool same = true;
        for (int i = 0; same && i < rows.size(); ++i)
            same = rowMap(rows.at(i)) == rowMap(m_rows.at(i));
        if (same)
            return;
    }
    beginResetModel();
    m_rows = rows;
    endResetModel();
    emit rowsChanged();
}

bool StatementListModel::setFlagged(const QString& id, bool flagged)
{
    const int i = indexOfId(id);
    if (i < 0)
        return false;
    if (m_rows[i].flagged != flagged) {
        m_rows[i].flagged = flagged;
        emit dataChanged(index(i), index(i), {FlaggedRole});
        emit rowsChanged();
    }
    return true;
}

int StatementListModel::indexOfId(const QString& id) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).id == id)
            return i;
    return -1;
}

QVariantMap StatementListModel::get(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    return rowMap(m_rows.at(row));
}

} // namespace tanara_qml
