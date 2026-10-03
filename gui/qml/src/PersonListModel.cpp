#include "PersonListModel.h"

#include "TranscriptEditorViewModel.h"

#include "tanara/edit/PeopleDirectory.h"

#include <algorithm>

using namespace tanara;

namespace tanara_qml {

PersonListModel::PersonListModel(QObject* parent) : QAbstractListModel(parent) {}

int PersonListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_shown.size());
}

QHash<int, QByteArray> PersonListModel::roleNames() const
{
    return {{NameRole, "name"},
            {HasVoiceprintRole, "hasVoiceprint"},
            {MeetingCountRole, "meetingCount"},
            {InMeetingRole, "inMeeting"}};
}

QVariant PersonListModel::data(const QModelIndex& index, int role) const
{
    const int row = index.row();
    if (row < 0 || row >= m_shown.size()) return {};
    const PersonInfo& p = m_shown[row];
    switch (role) {
    case NameRole: return p.name;
    case HasVoiceprintRole: return p.hasVoiceprint;
    case MeetingCountRole: return p.meetingCount;
    case InMeetingRole: return m_inMeeting.value(row);
    default: return {};
    }
}

void PersonListModel::setEditor(TranscriptEditorViewModel* editor)
{
    if (m_editor == editor) return;
    if (m_editor) m_editor->disconnect(this);
    m_editor = editor;
    if (m_editor) {
        connect(m_editor, &TranscriptEditorViewModel::peopleChanged, this, &PersonListModel::refresh);
        connect(m_editor, &TranscriptEditorViewModel::speakersChanged, this, &PersonListModel::refilter);
    }
    emit editorChanged();
    refresh();
}

void PersonListModel::setQuery(const QString& query)
{
    if (m_query == query) return;
    m_query = query;
    emit queryChanged();
    refilter();
}

void PersonListModel::setExcludeName(const QString& name)
{
    if (m_exclude == name) return;
    m_exclude = name;
    emit excludeNameChanged();
    refilter();
}

void PersonListModel::setHideMeetingPeople(bool hide)
{
    if (m_hideMeeting == hide) return;
    m_hideMeeting = hide;
    emit hideMeetingPeopleChanged();
    refilter();
}

void PersonListModel::setExcludeMeetingPeople(bool exclude)
{
    if (m_excludeMeeting == exclude) return;
    m_excludeMeeting = exclude;
    emit excludeMeetingPeopleChanged();
    refilter();
}

void PersonListModel::refresh()
{
    m_all = m_editor ? m_editor->people() : QVector<PersonInfo>();
    refilter();
}

QString PersonListModel::nameAt(int row) const
{
    return row >= 0 && row < m_shown.size() ? m_shown[row].name : QString();
}

void PersonListModel::refilter()
{
    const QString needle = m_query.trimmed();
    QVector<PersonInfo> shown;
    if (needle.isEmpty()) {
        shown = m_all;
        // Gyakran szereplők elöl, azon belül ábécében (a lista név szerint rendezve érkezik).
        std::stable_sort(shown.begin(), shown.end(), [](const PersonInfo& a, const PersonInfo& b) {
            return a.meetingCount > b.meetingCount;
        });
    } else {
        shown = filterPeople(m_all, needle);
    }

    const QString folded = foldForSearch(needle);
    bool exact = false;
    QVector<PersonInfo> out;
    QVector<bool> inMeeting;
    for (const PersonInfo& p : std::as_const(shown)) {
        if (!folded.isEmpty() && foldForSearch(p.name) == folded) exact = true;
        if (!m_exclude.isEmpty() && p.name.compare(m_exclude, Qt::CaseInsensitive) == 0) continue;
        const bool here = m_editor && m_editor->isMeetingPerson(p.name);
        if (here && (m_excludeMeeting || (m_hideMeeting && needle.isEmpty()))) continue;
        out.append(p);
        inMeeting.append(here);
    }

    beginResetModel();
    m_shown = out;
    m_inMeeting = inMeeting;
    m_canCreate = !needle.isEmpty() && !exact;
    endResetModel();
    emit countChanged();
}

} // namespace tanara_qml
