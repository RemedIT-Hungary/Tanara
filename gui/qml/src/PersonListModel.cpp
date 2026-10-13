#include "PersonListModel.h"

#include "DemoText.h"

#include "AppContext.h"
#include "TranscriptEditorViewModel.h"

#include "tanara/AppController.h"
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
            {InMeetingRole, "inMeeting"},
            {MatchedAliasRole, "matchedAlias"},
            {TagHintRole, "tagHint"},
            {GroupRole, "group"}};
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
    case MatchedAliasRole: return p.matchedAlias;
    case TagHintRole: return m_hints.value(row);
    case GroupRole: return m_groups.value(row);
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

void PersonListModel::setParticipants(ParticipantsViewModel* participants)
{
    if (m_participants == participants) return;
    if (m_participants) m_participants->disconnect(this);
    m_participants = participants;
    if (m_participants)
        connect(m_participants, &ParticipantsViewModel::changed, this, &PersonListModel::refilter);
    emit participantsChanged();
}

namespace {

// The name list of the Tags window's demo (without a controller; the design packages' fictional names).
QVector<PersonInfo> demoPeople()
{
    const QList<std::pair<const char*, int>> rows{
        {"Bárány Gergely", 23}, {"Dénes Zsófia", 17}, {"Fehér Gábor", 16}, {"Gál Bence", 12},
        {"Kovács Lilla", 41}, {"Molnár Eszter", 0}, {"Szabó Bence", 8}, {"Tóth Eszter", 3}, {"Varga Árpád", 12}};
    QVector<PersonInfo> out;
    for (const auto& [name, n] : rows) {
        PersonInfo p;
        p.name = demoName(name);
        p.meetingCount = n;
        p.hasVoiceprint = n > 2;
        out.append(p);
    }
    return out;
}

// A person's best relation among the reference tags.
struct TagLink {
    QString tagName;
    int shared = 0;
    int total = 0;
    bool manual = false;
    bool better(const TagLink& o) const
    {
        if (manual != o.manual) return manual;
        return shared > o.shared;
    }
};

} // namespace

void PersonListModel::setTagId(const QString& id)
{
    if (m_tagId == id) return;
    m_tagId = id;
    emit tagIdChanged();
    refresh();
}

void PersonListModel::setTagBackend(TagBackend* backend)
{
    if (m_tags == backend) return;
    if (m_tags) m_tags->disconnect(this);
    if (m_ownTags) m_tags->deleteLater();
    m_tags = backend;
    m_ownTags = false;
    if (m_tags) {
        connect(m_tags, &TagBackend::personTagsChanged, this, &PersonListModel::refilter);
        connect(m_tags, &TagBackend::personStatsChanged, this, &PersonListModel::refilter);
        connect(m_tags, &TagBackend::meetingTagsChanged, this, &PersonListModel::refilter);
    }
    refilter();
}

TagBackend* PersonListModel::tagBackend()
{
    if (!m_tags) {
        setTagBackend(createTagBackend(AppContext::instance()->controllerObject(), this));
        m_ownTags = true;
    }
    return m_tags;
}

QStringList PersonListModel::contextTagIds() const
{
    if (!m_tagId.isEmpty()) return {m_tagId};
    const QString meetingId = m_participants ? m_participants->meetingId()
                            : m_editor ? m_editor->meetingId() : QString();
    return meetingId.isEmpty() || !m_tags ? QStringList() : m_tags->tagsOf(meetingId);
}

void PersonListModel::refresh()
{
    if (m_participants) m_all = m_participants->people();
    else if (m_editor) m_all = m_editor->people();
    else if (!m_tagId.isEmpty()) {
        // Standalone (Tags window): the full name list.
        tanara::AppController* app = AppContext::instance()->controller();
        m_all = app ? app->peopleDirectory() : demoPeople();
    } else m_all.clear();
    refilter();
}

QString PersonListModel::nameAt(int row) const
{
    return row >= 0 && row < m_shown.size() ? m_shown[row].name : QString();
}

void PersonListModel::refilter()
{
    TagBackend* tags = tagBackend();
    const QString needle = m_query.trimmed();
    QVector<PersonInfo> shown;
    if (needle.isEmpty()) {
        shown = m_all;
        // The frequent ones first, alphabetical within (the list arrives sorted by name).
        std::stable_sort(shown.begin(), shown.end(), [](const PersonInfo& a, const PersonInfo& b) {
            return a.meetingCount > b.meetingCount;
        });
    } else {
        shown = filterPeople(m_all, needle);
    }

    // The people of the reference tags (the meeting's, or the tagId's): manual + learned >= threshold.
    QHash<QString, TagLink> links;   // foldForSearch(name) -> best relation
    QHash<QString, QString> tagNames;
    if (tags) {
        for (const TagItem& t : tags->tags()) tagNames.insert(t.id, t.name);
        for (const QString& t : contextTagIds()) {
            for (const PersonTagItem& st : tags->peopleStats(t)) {
                const TagLink link{tagNames.value(t), st.shared, st.tagTotal, st.manual};
                const QString key = foldForSearch(st.name);
                if (!links.contains(key) || link.better(links.value(key))) links.insert(key, link);
            }
        }
    }
    const QString selfKey = tags ? foldForSearch(tags->selfName()) : QString();

    const QString folded = foldForSearch(needle);
    bool exact = false;
    struct Row { PersonInfo p; bool here; QString group; QString hint; TagLink link; };
    QVector<Row> meeting, tagged, other;
    for (const PersonInfo& p : std::as_const(shown)) {
        if (!folded.isEmpty() && foldForSearch(p.name) == folded) exact = true;
        if (!m_exclude.isEmpty() && p.name.compare(m_exclude, Qt::CaseInsensitive) == 0) continue;
        const QString key = foldForSearch(p.name);
        // Tag mode: those who have it manually, and the own person, are left out.
        if (!m_tagId.isEmpty() && ((links.contains(key) && links.value(key).manual) || key == selfKey)) continue;
        const bool here = m_participants ? m_participants->isMeetingPerson(p.name)
                        : m_editor && m_editor->isMeetingPerson(p.name);
        if (here && (m_excludeMeeting || (m_hideMeeting && needle.isEmpty()))) continue;
        Row r{p, here, QStringLiteral("other"), QString(), {}};
        if (here) {
            r.group = QStringLiteral("meeting");
            meeting.append(r);
        } else if (links.contains(key)) {
            r.link = links.value(key);
            r.group = QStringLiteral("tag");
            r.hint = QStringLiteral("#%1 · %2 / %3 · ").arg(r.link.tagName).arg(r.link.shared).arg(r.link.total)
                   + tr("%n megbeszélés", "", p.meetingCount);
            tagged.append(r);
        } else {
            other.append(r);
        }
    }
    std::stable_sort(tagged.begin(), tagged.end(), [](const Row& a, const Row& b) { return a.link.better(b.link); });
    // The shared-tag group has at most 5 rows; the rest go among "others", without a mark.
    while (tagged.size() > 5) {
        Row r = tagged.takeLast();
        r.group = QStringLiteral("other");
        r.hint.clear();
        other.prepend(r);
    }

    QVector<PersonInfo> out;
    QVector<bool> inMeeting;
    QStringList hints, groups;
    for (const QVector<Row>* part : {&meeting, &tagged, &other})
        for (const Row& r : *part) {
            out.append(r.p);
            inMeeting.append(r.here);
            hints.append(r.hint);
            groups.append(r.group);
        }
    QString section;
    if (!tagged.isEmpty() && out.size() >= 6) {
        QStringList names;
        for (const Row& r : std::as_const(tagged))
            if (!names.contains(r.link.tagName)) names << r.link.tagName;
        section = names.size() == 1 ? tr("A #%1 alapján").arg(names.first()) : tr("A címkék alapján");
    }

    beginResetModel();
    m_shown = out;
    m_inMeeting = inMeeting;
    m_hints = hints;
    m_groups = groups;
    m_tagSection = section;
    // In tag mode no new person is created from here (the People window's "New person" is the place).
    m_canCreate = !needle.isEmpty() && !exact && m_tagId.isEmpty();
    endResetModel();
    emit countChanged();
}

} // namespace tanara_qml
