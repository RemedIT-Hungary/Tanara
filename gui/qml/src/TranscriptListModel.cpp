#include "TranscriptListModel.h"

#include "TranscriptEditorViewModel.h"

#include <QSet>

using tanara::EditorUtterance;

namespace tanara_qml {

TranscriptListModel::TranscriptListModel(TranscriptEditorViewModel* owner)
    : QAbstractListModel(owner), m_vm(owner)
{
}

int TranscriptListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QHash<int, QByteArray> TranscriptListModel::roleNames() const
{
    return {
        {KindRole, "kind"},
        {UtteranceIdRole, "utteranceId"},
        {UtteranceIndexRole, "utteranceIndex"},
        {StartMsRole, "startMs"},
        {EndMsRole, "endMs"},
        {TimeLabelRole, "timeLabel"},
        {TextRole, "lineText"},
        {RichTextRole, "richText"},
        {SpeakerKeyRole, "speakerKey"},
        {SpeakerNameRole, "speakerName"},
        {ColorIndexRole, "colorIndex"},
        {LaneRole, "lane"},
        {HeadRole, "head"},
        {FirstRole, "first"},
        {UncertainRole, "uncertain"},
        {CorrectedRole, "corrected"},
        {SelectedRole, "selected"},
        {SuggestedRole, "suggested"},
        {SuggestionAnchorRole, "suggestionAnchor"},
        {HiddenCountRole, "hiddenCount"},
        {NoisyRole, "noisy"},
        {NoisyOverlapRole, "noisyOverlap"},
        {LikelySpeakerKeyRole, "likelySpeakerKey"},
        {LikelySpeakerNameRole, "likelySpeakerName"},
    };
}

bool TranscriptListModel::headAt(int row) const
{
    const Row& r = m_rows[row];
    if (r.gap) return false;
    if (row == 0 || m_rows[row - 1].gap) return true;
    const auto& utts = m_vm->utterances();
    const EditorUtterance& u = utts[r.utterance];
    // A jelölt (bizonytalan / javítva / egymásra beszéltek) sor mindig kap névsort: a pirula ott jelenik meg.
    if (u.uncertain || u.manuallyCorrected || u.noisy) return true;
    return utts[m_rows[row - 1].utterance].speakerKey != u.speakerKey;
}

QVariant TranscriptListModel::data(const QModelIndex& index, int role) const
{
    const int row = index.row();
    if (row < 0 || row >= m_rows.size()) return {};
    const Row& r = m_rows[row];
    if (r.gap) {
        switch (role) {
        case KindRole: return QStringLiteral("gap");
        case HiddenCountRole: return r.hidden;
        case UtteranceIndexRole: return -1;
        case LaneRole: return -2;
        case FirstRole: return row == 0;
        case HeadRole:
        case UncertainRole:
        case CorrectedRole:
        case SelectedRole:
        case SuggestedRole:
        case SuggestionAnchorRole:
        case NoisyRole:
        case NoisyOverlapRole: return false;
        case StartMsRole:
        case EndMsRole:
        case ColorIndexRole: return 0;
        default: return QString();
        }
    }
    const EditorUtterance& u = m_vm->utterances()[r.utterance];
    switch (role) {
    case KindRole: return QStringLiteral("utterance");
    case UtteranceIdRole: return u.id;
    case UtteranceIndexRole: return r.utterance;
    case StartMsRole: return int(u.startMs);
    case EndMsRole: return int(u.endMs);
    case TimeLabelRole: return TranscriptEditorViewModel::timeLabel(u.startMs);
    case TextRole: return u.text;
    case RichTextRole: return m_vm->richText(r.utterance);
    case SpeakerKeyRole: return u.speakerKey;
    case SpeakerNameRole: return m_vm->speakerView(u.speakerKey).name;
    case ColorIndexRole: return m_vm->speakerView(u.speakerKey).colorIndex;
    case LaneRole: return m_vm->speakerView(u.speakerKey).lane;
    case HeadRole: return headAt(row);
    case FirstRole: return row == 0;
    case UncertainRole: return u.uncertain;
    case CorrectedRole: return u.manuallyCorrected;
    case SelectedRole: return m_vm->isSelected(r.utterance);
    case SuggestedRole: return m_vm->isSuggested(r.utterance);
    case SuggestionAnchorRole: return m_vm->suggestionAnchor() == r.utterance;
    case HiddenCountRole: return 0;
    case NoisyRole: return u.noisy;
    case NoisyOverlapRole: return u.noisyOverlap;
    case LikelySpeakerKeyRole: return u.likelySpeakerKey;
    case LikelySpeakerNameRole:
        return u.likelySpeakerKey.isEmpty() ? QString() : m_vm->speakerView(u.likelySpeakerKey).name;
    default: return {};
    }
}

int TranscriptListModel::rowOfUtterance(int utteranceIndex) const
{
    return m_rowOfUtt.value(utteranceIndex, -1);
}

int TranscriptListModel::utteranceOfRow(int row) const
{
    if (row < 0 || row >= m_rows.size() || m_rows[row].gap) return -1;
    return m_rows[row].utterance;
}

int TranscriptListModel::nearestRow(int utteranceIndex) const
{
    if (m_rows.isEmpty() || utteranceIndex < 0) return -1;
    const int direct = rowOfUtterance(utteranceIndex);
    if (direct >= 0) return direct;
    // Szűrőben: az első látható megszólalás-sor, amely nem korábbi a keresettnél.
    int last = -1;
    for (int r = 0; r < m_rows.size(); ++r) {
        if (m_rows[r].gap) continue;
        if (m_rows[r].utterance >= utteranceIndex) return r;
        last = r;
    }
    return last;
}

QVector<TranscriptListModel::Row> TranscriptListModel::computeRows() const
{
    const auto& utts = m_vm->utterances();
    QVector<Row> out;
    if (!m_vm->uncertainOnly()) {
        out.reserve(utts.size());
        for (int i = 0; i < utts.size(); ++i) out.append(Row{false, i, 0});
        return out;
    }
    // Szűrő: a bizonytalan sorok, a szűrő bekapcsolása óta kézzel javítottak (ne tűnjenek el a
    // kurzor alól), a javaslat horgonya, és — amíg a „Megmutatom" él — a javasolt sorok. A
    // köztük lévő biztos futamok egy-egy elválasztóvá csukódnak.
    const int anchor = m_vm->suggestionAnchor();
    int runStart = -1;
    for (int i = 0; i < utts.size(); ++i) {
        const bool show = utts[i].uncertain || i == anchor || m_vm->isSuggested(i)
            || (utts[i].manuallyCorrected && m_vm->isSticky(i));
        if (!show) {
            if (runStart < 0) runStart = i;
            continue;
        }
        if (runStart >= 0) {
            out.append(Row{true, runStart, i - runStart});
            runStart = -1;
        }
        out.append(Row{false, i, 0});
    }
    if (runStart >= 0) out.append(Row{true, runStart, int(utts.size()) - runStart});
    return out;
}

void TranscriptListModel::reindex()
{
    m_rowOfUtt.fill(-1, m_vm->utterances().size());
    for (int r = 0; r < m_rows.size(); ++r)
        if (!m_rows[r].gap) m_rowOfUtt[m_rows[r].utterance] = r;
}

void TranscriptListModel::rebuild()
{
    beginResetModel();
    m_rows = computeRows();
    reindex();
    endResetModel();
    emit countChanged();
}

void TranscriptListModel::syncFilter()
{
    const QVector<Row> target = computeRows();
    QSet<int> targetKeys;
    targetKeys.reserve(target.size());
    for (const Row& r : target) targetKeys.insert(r.key());

    bool structural = false;
    // 1) Törlés hátulról, összefüggő futamonként.
    for (int r = int(m_rows.size()) - 1; r >= 0;) {
        if (targetKeys.contains(m_rows[r].key())) { --r; continue; }
        int first = r;
        while (first > 0 && !targetKeys.contains(m_rows[first - 1].key())) --first;
        beginRemoveRows(QModelIndex(), first, r);
        m_rows.remove(first, r - first + 1);
        endRemoveRows();
        structural = true;
        r = first - 1;
    }
    // 2) Beszúrás elölről (mindkét lista kulcs szerint rendezett).
    QVector<int> gapChanged;
    for (int t = 0; t < target.size();) {
        if (t < m_rows.size() && m_rows[t].key() == target[t].key()) {
            if (m_rows[t].gap && m_rows[t].hidden != target[t].hidden) {
                m_rows[t].hidden = target[t].hidden;
                gapChanged.append(t);
            }
            ++t;
            continue;
        }
        int last = t;
        while (last + 1 < target.size()
               && (t >= m_rows.size() || target[last + 1].key() < m_rows[t].key()))
            ++last;
        beginInsertRows(QModelIndex(), t, last);
        for (int i = t; i <= last; ++i) m_rows.insert(i, target[i]);
        endInsertRows();
        structural = true;
        t = last + 1;
    }
    reindex();
    for (int r : std::as_const(gapChanged))
        emit dataChanged(index(r), index(r), {HiddenCountRole});
    if (structural) {
        if (!m_rows.isEmpty())
            emit dataChanged(index(0), index(int(m_rows.size()) - 1), {HeadRole, FirstRole});
        emit countChanged();
    }
}

void TranscriptListModel::notifyUtterances(const QVector<int>& utteranceIndices, const QList<int>& roles)
{
    for (int u : utteranceIndices) {
        const int r = rowOfUtterance(u);
        if (r < 0) continue;
        emit dataChanged(index(r), index(r), roles);
        // A következő sor névsora ettől a sortól függ (beszélőváltás).
        if (r + 1 < m_rows.size() && (roles.isEmpty() || roles.contains(SpeakerKeyRole)))
            emit dataChanged(index(r + 1), index(r + 1), {HeadRole});
    }
}

void TranscriptListModel::notifyAll(const QList<int>& roles)
{
    if (m_rows.isEmpty()) return;
    emit dataChanged(index(0), index(int(m_rows.size()) - 1), roles);
}

} // namespace tanara_qml
