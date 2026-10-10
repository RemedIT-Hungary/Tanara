#include "ReviewGroupsModel.h"

#include "TranscriptEditorViewModel.h"

#include <algorithm>

namespace tanara_qml {

namespace {

QString iconOf(const QString& kind)
{
    if (kind == QLatin1String("sideConflict")) return QStringLiteral("arrow-right-left");
    if (kind == QLatin1String("similarToNewPerson")) return QStringLiteral("user-plus");
    if (kind == QLatin1String("shortLines")) return QStringLiteral("ear");
    return QStringLiteral("audio-lines");
}

} // namespace

ReviewGroupsModel::ReviewGroupsModel(TranscriptEditorViewModel* owner)
    : QAbstractListModel(owner), m_vm(owner)
{
}

int ReviewGroupsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QHash<int, QByteArray> ReviewGroupsModel::roleNames() const
{
    return {
        {RowKindRole, "rowKind"},
        {GroupIdRole, "groupId"},
        {GroupKindRole, "groupKind"},
        {TitleRole, "title"},
        {SubtitleRole, "subtitle"},
        {CountRole, "lineCount"},
        {EvidenceRole, "evidence"},
        {ProposedKeyRole, "proposedKey"},
        {ProposedNameRole, "proposedName"},
        {ActionableRole, "actionable"},
        {CanApplyRole, "canApply"},
        {ExpandedRole, "expanded"},
        {ActiveRole, "active"},
        {IconRole, "iconName"},
        {UtteranceIdRole, "utteranceId"},
        {SpeakerKeyRole, "speakerKey"},
        {SpeakerNameRole, "speakerName"},
        {ColorIndexRole, "colorIndex"},
        {TimeLabelRole, "timeLabel"},
        {LineTextRole, "lineText"},
        {StartMsRole, "startMs"},
        {EndMsRole, "endMs"},
        {ReasonRole, "reason"},
        {LikelyKeyRole, "likelyKey"},
        {LikelyNameRole, "likelyName"},
        {DecidedRole, "decided"},
        {DecidedTextRole, "decidedText"},
    };
}

QString ReviewGroupsModel::activeId() const
{
    const auto& views = m_vm->reviewViews();
    for (const auto& v : views)
        if (v.id == m_active) return m_active;
    for (const auto& v : views)
        if (v.actionable && !v.ids.isEmpty()) return v.id;
    return {};
}

QVariant ReviewGroupsModel::data(const QModelIndex& index, int role) const
{
    const int row = index.row();
    if (row < 0 || row >= m_rows.size()) return {};
    const Row& r = m_rows[row];
    const auto& views = m_vm->reviewViews();
    if (r.view < 0 || r.view >= views.size()) return {};
    const TranscriptEditorViewModel::ReviewView& v = views[r.view];
    const bool canApply = v.actionable && (!v.proposedKey.isEmpty() || !v.proposedName.isEmpty());

    switch (role) {
    case RowKindRole: return r.kind == Group ? QStringLiteral("group") : r.kind == Line ? QStringLiteral("line")
                                                                                       : QStringLiteral("more");
    case GroupIdRole: return v.id;
    case GroupKindRole: return v.kind;
    case TitleRole: return v.title;
    case SubtitleRole: return v.subtitle;
    case CountRole: return int(v.ids.size());
    case EvidenceRole: return v.evidence;
    case ProposedKeyRole: return v.proposedKey;
    case ProposedNameRole:
        return !v.proposedKey.isEmpty() ? m_vm->speakerView(v.proposedKey).name : v.proposedName;
    case ActionableRole: return v.actionable;
    case CanApplyRole: return canApply;
    case ExpandedRole: return m_expanded.contains(v.id);
    case ActiveRole: return v.id == activeId();
    case IconRole: return iconOf(v.kind);
    default: break;
    }

    if (r.kind != Line) {
        switch (role) {
        case StartMsRole:
        case EndMsRole:
        case ColorIndexRole: return 0;
        case DecidedRole: return false;
        default: return QString();
        }
    }
    const int ui = m_vm->utteranceIndexOf(r.utteranceId);
    if (ui < 0) return {};
    const tanara::EditorUtterance& u = m_vm->utterances()[ui];
    const bool inGroup = v.ids.contains(r.utteranceId);
    switch (role) {
    case UtteranceIdRole: return u.id;
    case SpeakerKeyRole: return u.speakerKey;
    case SpeakerNameRole: return m_vm->speakerView(u.speakerKey).name;
    case ColorIndexRole: return m_vm->speakerView(u.speakerKey).colorIndex;
    case TimeLabelRole: return TranscriptEditorViewModel::timeLabel(u.startMs);
    case LineTextRole: return u.text;
    case StartMsRole: return int(u.startMs);
    case EndMsRole: return int(u.endMs);
    case ReasonRole: return u.uncertainReason;
    case LikelyKeyRole: {
        if (!canApply) return u.likelySpeakerKey;
        return v.proposedKey;
    }
    case LikelyNameRole: {
        if (!canApply) return u.likelySpeakerKey.isEmpty() ? QString() : m_vm->speakerView(u.likelySpeakerKey).name;
        return !v.proposedKey.isEmpty() ? m_vm->speakerView(v.proposedKey).name : v.proposedName;
    }
    case DecidedRole: return !inGroup || u.manuallyCorrected || u.confirmed;
    case DecidedTextRole:
        if (u.manuallyCorrected || (!inGroup && u.speakerKey != v.currentKey && !v.currentKey.isEmpty()))
            return tr("javítva");
        if (u.confirmed || !inGroup) return tr("jó így");
        return QString();
    default: return {};
    }
}

QVector<ReviewGroupsModel::Row> ReviewGroupsModel::compute()
{
    const auto& views = m_vm->reviewViews();
    const QString active = activeId();
    QVector<Row> out;
    QSet<QString> alive;
    for (int g = 0; g < views.size(); ++g) {
        const auto& v = views[g];
        alive.insert(v.id);
        out.append(Row{Group, g, QString(), QStringLiteral("g:") + v.id});
        if (m_expanded.contains(v.id)) {
            // A már megjelenített (ragadós) sorok + a csoport mostani sorai, időrendben.
            QStringList ids = m_shown.value(v.id);
            for (const QString& id : v.ids)
                if (!ids.contains(id)) ids << id;
            std::sort(ids.begin(), ids.end(), [this](const QString& a, const QString& b) {
                return m_vm->utteranceIndexOf(a) < m_vm->utteranceIndexOf(b);
            });
            ids.erase(std::remove_if(ids.begin(), ids.end(),
                                     [this](const QString& id) { return m_vm->utteranceIndexOf(id) < 0; }),
                      ids.end());
            m_shown.insert(v.id, ids);
            for (const QString& id : std::as_const(ids))
                out.append(Row{Line, g, id, QStringLiteral("l:%1:%2").arg(v.id, id)});
        } else if (v.id == active && v.actionable && !v.ids.isEmpty()) {
            out.append(Row{More, g, QString(), QStringLiteral("m:") + v.id});
        }
    }
    // Megszűnt csoport: a kinyitás és a ragadós sorai is mennek.
    for (auto it = m_shown.begin(); it != m_shown.end();)
        it = alive.contains(it.key()) ? std::next(it) : m_shown.erase(it);
    for (auto it = m_expanded.begin(); it != m_expanded.end();)
        it = alive.contains(*it) ? std::next(it) : m_expanded.erase(it);
    return out;
}

void ReviewGroupsModel::apply(const QVector<Row>& target)
{
    QSet<QString> targetKeys;
    for (const Row& r : target) targetKeys.insert(r.key);
    QHash<QString, int> targetPos;
    for (int i = 0; i < target.size(); ++i) targetPos.insert(target[i].key, i);

    // A közös sorok sorrendje egyezik? Ha nem, teljes újraépítés.
    int last = -1;
    bool ordered = true;
    for (const Row& r : std::as_const(m_rows)) {
        const auto it = targetPos.constFind(r.key);
        if (it == targetPos.constEnd()) continue;
        if (it.value() < last) { ordered = false; break; }
        last = it.value();
    }
    if (!ordered) {
        beginResetModel();
        m_rows = target;
        endResetModel();
        emit countChanged();
        return;
    }
    const int before = int(m_rows.size());
    // 1) Törlés hátulról, összefüggő futamonként.
    for (int r = int(m_rows.size()) - 1; r >= 0;) {
        if (targetKeys.contains(m_rows[r].key)) { --r; continue; }
        int first = r;
        while (first > 0 && !targetKeys.contains(m_rows[first - 1].key)) --first;
        beginRemoveRows(QModelIndex(), first, r);
        m_rows.remove(first, r - first + 1);
        endRemoveRows();
        r = first - 1;
    }
    // 2) Beszúrás elölről.
    for (int t = 0; t < target.size();) {
        if (t < m_rows.size() && m_rows[t].key == target[t].key) {
            m_rows[t] = target[t];      // a csoport-index változhatott
            ++t;
            continue;
        }
        int end = t;
        QSet<QString> present;
        for (const Row& r : std::as_const(m_rows)) present.insert(r.key);
        while (end + 1 < target.size() && !present.contains(target[end + 1].key)) ++end;
        beginInsertRows(QModelIndex(), t, end);
        for (int i = t; i <= end; ++i) m_rows.insert(i, target[i]);
        endInsertRows();
        t = end + 1;
    }
    if (!m_rows.isEmpty()) emit dataChanged(index(0), index(int(m_rows.size()) - 1));
    if (before != m_rows.size()) emit countChanged();
}

void ReviewGroupsModel::sync()
{
    apply(compute());
}

void ReviewGroupsModel::refreshUtterances(const QVector<int>& utteranceIndices)
{
    if (m_rows.isEmpty() || utteranceIndices.isEmpty()) return;
    QSet<QString> ids;
    for (int i : utteranceIndices)
        if (i >= 0 && i < m_vm->utterances().size()) ids.insert(m_vm->utterances()[i].id);
    for (int r = 0; r < m_rows.size(); ++r)
        if (m_rows[r].kind == Line && ids.contains(m_rows[r].utteranceId)) emit dataChanged(index(r), index(r));
}

void ReviewGroupsModel::refreshAll()
{
    if (!m_rows.isEmpty()) emit dataChanged(index(0), index(int(m_rows.size()) - 1));
}

void ReviewGroupsModel::reset()
{
    beginResetModel();
    m_expanded.clear();
    m_shown.clear();
    m_active.clear();
    m_rows.clear();
    endResetModel();
    sync();
    emit countChanged();
}

void ReviewGroupsModel::setExpanded(const QString& groupId, bool expanded)
{
    if (expanded == m_expanded.contains(groupId)) return;
    if (expanded) m_expanded.insert(groupId);
    else {
        m_expanded.remove(groupId);
        m_shown.remove(groupId);
    }
    m_active = groupId;
    sync();
}

int ReviewGroupsModel::rowOfGroup(const QString& groupId) const
{
    const QString key = QStringLiteral("g:") + groupId;
    for (int r = 0; r < m_rows.size(); ++r)
        if (m_rows[r].key == key) return r;
    return -1;
}

int ReviewGroupsModel::rowOfLine(const QString& utteranceId) const
{
    for (int r = 0; r < m_rows.size(); ++r)
        if (m_rows[r].kind == Line && m_rows[r].utteranceId == utteranceId) return r;
    return -1;
}

QString ReviewGroupsModel::utteranceAt(int row) const
{
    return row >= 0 && row < m_rows.size() && m_rows[row].kind == Line ? m_rows[row].utteranceId : QString();
}

QString ReviewGroupsModel::groupAt(int row) const
{
    const auto& views = m_vm->reviewViews();
    if (row < 0 || row >= m_rows.size()) return {};
    const int v = m_rows[row].view;
    return v >= 0 && v < views.size() ? views[v].id : QString();
}

int ReviewGroupsModel::stepLine(int fromRow, int direction)
{
    // A döntésre váró sorok sorrendje: a csoportok sorrendjében, csoporton belül időrendben.
    const auto& views = m_vm->reviewViews();
    QVector<QPair<QString, QString>> order;     // (csoport, sor)
    for (const auto& v : views) {
        if (!v.actionable) continue;
        for (const QString& id : v.ids) order.append({v.id, id});
    }
    if (order.isEmpty()) return -1;
    int pos = -1;
    const QString fromId = utteranceAt(fromRow);
    if (!fromId.isEmpty()) {
        for (int i = 0; i < order.size(); ++i)
            if (order[i].second == fromId) { pos = i; break; }
    }
    if (pos < 0 && fromRow >= 0) {
        // Csoport-fejlécről / sor nélkül: a csoport első sora előtt állunk.
        const QString g = groupAt(fromRow);
        for (int i = 0; i < order.size(); ++i)
            if (order[i].first == g) { pos = direction < 0 ? i : i - 1; break; }
    }
    const int n = int(order.size());
    const int step = direction < 0 ? -1 : 1;
    int next = pos < 0 ? (step > 0 ? 0 : n - 1) : ((pos + step) % n + n) % n;
    const auto& target = order[next];
    if (!m_expanded.contains(target.first)) setExpanded(target.first, true);
    else m_active = target.first;
    return rowOfLine(target.second);
}

} // namespace tanara_qml
