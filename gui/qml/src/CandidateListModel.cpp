#include "CandidateListModel.h"

#include "TranscriptEditorViewModel.h"

#include "tanara/edit/PeopleDirectory.h"

#include <QTimer>

using namespace tanara;

namespace tanara_qml {

CandidateListModel::CandidateListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int CandidateListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QHash<int, QByteArray> CandidateListModel::roleNames() const
{
    return {
        {SpeakerKeyRole, "speakerKey"},
        {NameRole, "name"},
        {PersonNameRole, "personName"},
        {ColorIndexRole, "colorIndex"},
        {SubTextRole, "subText"},
        {EvidenceRole, "evidence"},
        {OtherSideRole, "otherSide"},
        {CurrentRole, "current"},
        {DimmedRole, "dimmed"},
        {KeyHintRole, "keyHint"},
        {ScoreRole, "score"},
    };
}

QVariant CandidateListModel::data(const QModelIndex& index, int role) const
{
    const int row = index.row();
    if (row < 0 || row >= m_items.size()) return {};
    const Item& it = m_items[row];
    switch (role) {
    case SpeakerKeyRole: return it.speakerKey;
    case NameRole: return it.name;
    case PersonNameRole: return it.personName;
    case ColorIndexRole: return it.colorIndex;
    case SubTextRole: return it.subText;
    case EvidenceRole: return it.evidence;
    case OtherSideRole: return it.otherSide;
    case CurrentRole: return it.current;
    case DimmedRole: return it.otherSide || it.current;
    case KeyHintRole: return it.keyHint;
    case ScoreRole: return it.score;
    default: return {};
    }
}

void CandidateListModel::setEditor(TranscriptEditorViewModel* editor)
{
    if (m_editor == editor) return;
    if (m_editor) m_editor->disconnect(this);
    m_editor = editor;
    if (m_editor) {
        connect(m_editor, &TranscriptEditorViewModel::reviewChanged, this, &CandidateListModel::scheduleRefresh);
        connect(m_editor, &TranscriptEditorViewModel::speakersChanged, this, &CandidateListModel::scheduleRefresh);
        connect(m_editor, &TranscriptEditorViewModel::sessionChanged, this, &CandidateListModel::scheduleRefresh);
    }
    emit editorChanged();
    refresh();
}

void CandidateListModel::setUtteranceId(const QString& id)
{
    if (m_utteranceId == id) return;
    m_utteranceId = id;
    emit queryChanged();
    refresh();
}

void CandidateListModel::setSpeakerKey(const QString& key)
{
    if (m_speakerKey == key) return;
    m_speakerKey = key;
    emit queryChanged();
    refresh();
}

void CandidateListModel::setQuery(const QString& query)
{
    if (m_query == query) return;
    m_query = query;
    emit queryChanged();
    refresh();
}

void CandidateListModel::setLimit(int limit)
{
    if (m_limit == limit) return;
    m_limit = limit;
    emit queryChanged();
    refresh();
}

void CandidateListModel::scheduleRefresh()
{
    if (m_refreshQueued) return;
    m_refreshQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_refreshQueued = false;
        refresh();
    });
}

void CandidateListModel::refresh()
{
    QVector<Item> items;
    const bool lineMode = !m_utteranceId.isEmpty();
    if (m_editor && (lineMode || !m_speakerKey.isEmpty())) {
        const QVector<Candidate> cands = lineMode ? m_editor->candidatesForLine(m_utteranceId)
                                                  : m_editor->candidatesForSpeaker(m_speakerKey);
        QString currentKey = m_speakerKey;
        QString sideCtx;
        if (lineMode) {
            currentKey = m_editor->lineInfo(m_utteranceId).value(QStringLiteral("speakerKey")).toString();
            sideCtx = m_editor->lineSideName(m_utteranceId);
        } else {
            sideCtx = m_editor->speakerSideName(m_speakerKey);
        }
        const QString needle = foldForSearch(m_query.trimmed());
        QHash<QString, int> meetings;
        for (const PersonInfo& p : m_editor->people()) meetings.insert(p.name.toCaseFolded(), p.meetingCount);

        Item current;
        bool hasCurrent = false;
        for (const Candidate& c : cands) {
            Item it;
            it.speakerKey = c.speakerKey;
            it.personName = c.personName;
            if (!c.speakerKey.isEmpty()) {
                const auto view = m_editor->speakerView(c.speakerKey);
                it.name = view.name;
                it.colorIndex = view.colorIndex;
            } else {
                it.name = c.personName;
            }
            if (it.name.isEmpty()) continue;
            if (!needle.isEmpty() && !foldForSearch(it.name).contains(needle)) continue;
            it.otherSide = c.otherSide;
            it.current = lineMode && c.speakerKey == currentKey;
            it.score = c.score;
            // A „most ő" sornál csak az ellene szóló bizonyíték érdekes (miért nem ő).
            QVector<Evidence> ev;
            for (const Evidence& e : c.evidence)
                if (!it.current || e.polarity == Polarity::Contradict) ev.append(e);
            it.evidence = m_editor->evidenceList(ev, sideCtx, lineMode, /*chipsOnly*/ true);
            if (it.current) it.subText = tr("most ő");
            else if (!c.speakerKey.isEmpty()) it.subText = tr("%n sor itt", nullptr, c.linesHere);
            else it.subText = tr("%n megbeszélés", nullptr, meetings.value(c.personName.toCaseFolded()));
            if (it.current) {
                current = it;
                hasCurrent = true;
            } else {
                items.append(it);
            }
        }
        if (m_limit > 0 && needle.isEmpty() && items.size() > m_limit) items.resize(m_limit);
        if (needle.isEmpty())
            for (int i = 0; i < items.size() && i < 3; ++i) items[i].keyHint = QString::number(i + 1);
        if (hasCurrent) items.append(current);
    }
    beginResetModel();
    const bool countChange = items.size() != m_items.size();
    m_items = items;
    endResetModel();
    if (countChange) emit countChanged();
}

QVariantMap CandidateListModel::get(int row) const
{
    if (row < 0 || row >= m_items.size()) return {};
    const Item& it = m_items[row];
    return {{QStringLiteral("speakerKey"), it.speakerKey},
            {QStringLiteral("name"), it.name},
            {QStringLiteral("personName"), it.personName},
            {QStringLiteral("current"), it.current},
            {QStringLiteral("otherSide"), it.otherSide}};
}

int CandidateListModel::rowForKey(int key) const
{
    const QString k = QString::number(key);
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items[i].keyHint == k) return i;
    return -1;
}

} // namespace tanara_qml
