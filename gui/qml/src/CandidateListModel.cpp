#include "CandidateListModel.h"

#include "TranscriptEditorViewModel.h"

#include "tanara/domain/EvidenceText.h"
#include "tanara/editing/session/PeopleDirectory.h"

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
        {PersonIdRole, "personId"},
        {ColorIndexRole, "colorIndex"},
        {SubTextRole, "subText"},
        {EvidenceRole, "evidence"},
        {OtherSideRole, "otherSide"},
        {CurrentRole, "current"},
        {DimmedRole, "dimmed"},
        {KeyHintRole, "keyHint"},
        {ScoreRole, "score"},
        {VetoedRole, "vetoed"},
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
    case PersonIdRole: return it.personId;
    case ColorIndexRole: return it.colorIndex;
    case SubTextRole: return it.subText;
    case EvidenceRole: return it.evidence;
    case OtherSideRole: return it.otherSide;
    case CurrentRole: return it.current;
    case DimmedRole: return it.otherSide || it.vetoed || it.current;
    case KeyHintRole: return it.keyHint;
    case ScoreRole: return it.score;
    case VetoedRole: return it.vetoed;
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
        const QString needle = text::foldForSearch(m_query.trimmed());
        QHash<PersonId, int> meetings;
        QHash<PersonId, QStringList> aliases;   // nicknames for the search
        for (const editing::PersonInfo& p : m_editor->people()) {
            if (p.personId.isNull()) continue;     // not a stored person (a fictional demo name): nothing to join
            meetings.insert(p.personId, p.meetingCount);
            if (!p.aliases.isEmpty()) aliases.insert(p.personId, p.aliases);
        }
        // The search matches the name or the nickname; the matched nickname goes into the subtext ("Dompa" · ...).
        auto matchedAlias = [&](const PersonId& person) -> QString {
            for (const QString& a : aliases.value(person))
                if (text::foldForSearch(a).contains(needle)) return a;
            return {};
        };

        Item current;
        bool hasCurrent = false;
        QVector<Item> vetoed;   // at the very bottom of the list (G10)
        for (const Candidate& c : cands) {
            Item it;
            it.speakerKey = c.speakerKey;
            it.personName = c.personId.isNull() ? QString() : c.displayName;
            it.personId = c.personId.toString();
            if (!c.speakerKey.isEmpty()) {
                const auto view = m_editor->speakerView(c.speakerKey);
                it.name = view.name;
                it.colorIndex = view.colorIndex;
            } else {
                it.name = c.displayName;
            }
            if (it.name.isEmpty()) continue;
            QString aliasHit;
            if (!needle.isEmpty() && !text::foldForSearch(it.name).contains(needle)) {
                aliasHit = matchedAlias(c.personId);
                if (aliasHit.isEmpty()) continue;
            }
            it.otherSide = c.otherSide;
            it.vetoed = c.vetoed;
            it.current = lineMode && c.speakerKey == currentKey;
            it.score = c.score;
            // For the "is them now" row only the evidence against is interesting (why not them). The
            // excluded candidate's chip is short: "not them per their track" (the full reason is in the bubble).
            QVector<Evidence> ev;
            for (Evidence e : c.evidence) {
                if (it.current && e.polarity != Polarity::Contradict) continue;
                if (c.vetoed && e.kind == EvidenceKind::Manual && e.polarity == Polarity::Contradict) {
                    e.detail = evidenceText(e) + QStringLiteral(" · ") + evidenceDetail(e);
                    e.text = tr("a sávja szerint nem ő");
                }
                ev.append(e);
            }
            it.evidence = m_editor->evidenceList(ev, sideCtx, lineMode, /*chipsOnly*/ true);
            if (it.current) it.subText = tr("most ő");
            else if (!c.speakerKey.isEmpty()) it.subText = tr("%n sor itt", nullptr, c.linesHere);
            else it.subText = tr("%n megbeszélés", nullptr, meetings.value(c.personId));
            if (!aliasHit.isEmpty()) it.subText = tr("„%1”").arg(aliasHit) + QStringLiteral(" · ") + it.subText;
            if (it.current) {
                current = it;
                hasCurrent = true;
            } else if (it.vetoed) {
                vetoed.append(it);
            } else {
                items.append(it);
            }
        }
        if (m_limit > 0 && needle.isEmpty() && items.size() > m_limit) items.resize(m_limit);
        if (m_limit > 0 && needle.isEmpty() && vetoed.size() > m_limit) vetoed.resize(m_limit);
        if (needle.isEmpty())
            for (int i = 0; i < items.size() && i < 3; ++i) items[i].keyHint = QString::number(i + 1);
        if (hasCurrent) items.append(current);
        items += vetoed;
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
            {QStringLiteral("personId"), it.personId},
            {QStringLiteral("current"), it.current},
            {QStringLiteral("otherSide"), it.otherSide},
            {QStringLiteral("vetoed"), it.vetoed}};
}

int CandidateListModel::rowForKey(int key) const
{
    const QString k = QString::number(key);
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items[i].keyHint == k) return i;
    return -1;
}

} // namespace tanara_qml
