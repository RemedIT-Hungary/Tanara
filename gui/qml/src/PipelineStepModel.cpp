#include "PipelineStepModel.h"

namespace tanara_qml {

namespace {

// The role -> the row's field name (StepRole is the whole row and has no field).
const char* fieldOf(int role)
{
    switch (role) {
    case PipelineStepModel::KeyRole: return "key";
    case PipelineStepModel::TitleRole: return "title";
    case PipelineStepModel::SubRole: return "sub";
    case PipelineStepModel::StateRole: return "state";
    case PipelineStepModel::ParallelRole: return "parallel";
    case PipelineStepModel::ProgressRole: return "progress";
    case PipelineStepModel::FixTargetRole: return "fixTarget";
    case PipelineStepModel::NoteRole: return "note";
    case PipelineStepModel::DetailRole: return "detail";
    case PipelineStepModel::PhaseRole: return "phase";
    case PipelineStepModel::CanStartRole: return "canStart";
    case PipelineStepModel::RouteRole: return "route";
    case PipelineStepModel::RouteOptionsRole: return "routeOptions";
    default: return nullptr;
    }
}

constexpr int kFirstRole = PipelineStepModel::KeyRole;
constexpr int kLastFieldRole = PipelineStepModel::RouteOptionsRole;

} // namespace

PipelineStepModel::PipelineStepModel(QObject* parent) : QAbstractListModel(parent) {}

int PipelineStepModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QHash<int, QByteArray> PipelineStepModel::roleNames() const
{
    QHash<int, QByteArray> names;
    for (int role = kFirstRole; role <= kLastFieldRole; ++role)
        names.insert(role, QByteArray(fieldOf(role)));
    names.insert(StepRole, QByteArrayLiteral("step"));
    return names;
}

QVariant PipelineStepModel::roleValue(const QVariantMap& row, int role)
{
    if (role == StepRole)
        return row;
    const char* field = fieldOf(role);
    if (!field)
        return {};
    const QVariant v = row.value(QLatin1String(field));
    if (v.isValid())
        return v;
    // Typed defaults, so a delegate's bindings never see undefined.
    switch (role) {
    case ParallelRole:
    case CanStartRole:
        return false;
    case ProgressRole:
        return -1;
    case RouteOptionsRole:
        return QVariantList();
    default:
        return QString();
    }
}

QVariant PipelineStepModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    return roleValue(m_rows.at(index.row()), role);
}

QString PipelineStepModel::keyOf(const QVariantMap& row)
{
    return row.value(QStringLiteral("key")).toString();
}

QVariantList PipelineStepModel::rows() const
{
    QVariantList out;
    out.reserve(m_rows.size());
    for (const QVariantMap& row : m_rows)
        out << row;
    return out;
}

QVariantMap PipelineStepModel::get(const QString& key) const
{
    const int i = indexOf(key);
    return i >= 0 ? m_rows.at(i) : QVariantMap();
}

int PipelineStepModel::indexOf(const QString& key) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (keyOf(m_rows.at(i)) == key)
            return i;
    return -1;
}

void PipelineStepModel::setSteps(const QVariantList& steps)
{
    QVector<QVariantMap> next;
    next.reserve(steps.size());
    QStringList nextKeys;
    for (const QVariant& v : steps) {
        next << v.toMap();
        nextKeys << keyOf(next.last());
    }
    const int before = int(m_rows.size());

    // 1. Rows whose key is gone.
    for (int i = int(m_rows.size()) - 1; i >= 0; --i) {
        if (nextKeys.contains(keyOf(m_rows.at(i)))) continue;
        beginRemoveRows(QModelIndex(), i, i);
        m_rows.remove(i);
        endRemoveRows();
    }
    // 2. Position by position: the same key in place, a later one moved up, a new one inserted.
    for (int i = 0; i < next.size(); ++i) {
        const QVariantMap& row = next.at(i);
        const QString key = nextKeys.at(i);
        int at = -1;
        for (int j = i; j < m_rows.size(); ++j)
            if (keyOf(m_rows.at(j)) == key) { at = j; break; }
        if (at < 0) {
            beginInsertRows(QModelIndex(), i, i);
            m_rows.insert(i, row);
            endInsertRows();
            continue;
        }
        if (at != i) {
            beginMoveRows(QModelIndex(), at, at, QModelIndex(), i);
            m_rows.move(at, i);
            endMoveRows();
        }
        const QVariantMap old = m_rows.at(i);
        if (old == row) continue;
        m_rows[i] = row;
        QList<int> roles;
        for (int role = kFirstRole; role <= kLastFieldRole; ++role)
            if (roleValue(old, role) != roleValue(row, role)) roles << role;
        roles << StepRole;
        const QModelIndex idx = index(i);
        emit dataChanged(idx, idx, roles);
    }
    // 3. Leftovers (duplicate keys in the old list).
    if (m_rows.size() > next.size()) {
        beginRemoveRows(QModelIndex(), int(next.size()), int(m_rows.size()) - 1);
        m_rows.resize(next.size());
        endRemoveRows();
    }
    if (int(m_rows.size()) != before)
        emit countChanged();
}

} // namespace tanara_qml
