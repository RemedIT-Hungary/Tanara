#include "PersonListModel.h"

#include "DemoText.h"

#include "AppContext.h"
#include "TranscriptEditorViewModel.h"

#include "tanara/editing/participants/IParticipantsFacade.h"
#include "tanara/editing/session/PeopleDirectory.h"

#include <algorithm>

using namespace tanara;

namespace tanara_qml {

PersonListModel::PersonListModel(QObject* parent) : QAbstractListModel(parent)
{
    // The services of the moment: a new controller / App.demo changes where the lists come from.
    AppContext* ctx = AppContext::instance();
    connect(ctx, &AppContext::controllerChanged, this, &PersonListModel::dropServices);
    connect(ctx, &AppContext::demoChanged, this, &PersonListModel::dropServices);
}

PersonListModel::PersonListModel(const UiServices& services, QObject* parent) : QAbstractListModel(parent)
{
    m_explicitServices = true;
    m_directory = services.participants(this);
    setTagBackend(services.tagBackend(this));
    m_ownTags = true;
}

void PersonListModel::dropServices()
{
    if (m_explicitServices) return;
    if (m_ownTags && m_tags) {
        m_tags->disconnect(this);
        m_tags->deleteLater();
        m_tags = nullptr;
        m_ownTags = false;
    }
    if (m_directory && m_directory->parent() == this) m_directory->deleteLater();   // a fictional one of ours
    m_directory = nullptr;
    refresh();
}

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
    const editing::PersonInfo& p = m_shown[row];
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

namespace {

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
        setTagBackend(AppContext::instance()->services().tagBackend(this));
        m_ownTags = true;
    }
    return m_tags;
}

QStringList PersonListModel::contextTagIds() const
{
    if (!m_tagId.isEmpty()) return {m_tagId};
    const QString meetingId = m_editor ? m_editor->meetingId() : QString();
    return meetingId.isEmpty() || !m_tags ? QStringList() : m_tags->tagsOf(meetingId);
}

void PersonListModel::refresh()
{
    if (m_editor) m_all = m_editor->people();
    else if (!m_tagId.isEmpty()) {
        // Standalone (Tags window): the full name list.
        if (!m_directory) m_directory = AppContext::instance()->services().participants(this);
        m_all = m_directory->peopleDirectory();
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
    QVector<editing::PersonInfo> shown;
    if (needle.isEmpty()) {
        shown = m_all;
        // The frequent ones first, alphabetical within (the list arrives sorted by name).
        std::stable_sort(shown.begin(), shown.end(), [](const editing::PersonInfo& a, const editing::PersonInfo& b) {
            return a.meetingCount > b.meetingCount;
        });
    } else {
        shown = filterPeople(m_all, needle);
    }

    // The people of the reference tags (the meeting's, or the tagId's): manual + learned >= threshold.
    QHash<QString, TagLink> links;   // text::foldForSearch(name) -> best relation
    QHash<QString, QString> tagNames;
    if (tags) {
        for (const TagItem& t : tags->tags()) tagNames.insert(t.id, t.name);
        for (const QString& t : contextTagIds()) {
            for (const PersonTagItem& st : tags->peopleStats(t)) {
                const TagLink link{tagNames.value(t), st.shared, st.tagTotal, st.manual};
                const QString key = text::foldForSearch(st.name);
                if (!links.contains(key) || link.better(links.value(key))) links.insert(key, link);
            }
        }
    }
    const QString selfKey = tags ? text::foldForSearch(tags->selfName()) : QString();

    const QString folded = text::foldForSearch(needle);
    bool exact = false;
    struct Row { editing::PersonInfo p; bool here; QString group; QString hint; TagLink link; };
    QVector<Row> meeting, tagged, other;
    for (const editing::PersonInfo& p : std::as_const(shown)) {
        if (!folded.isEmpty() && text::foldForSearch(p.name) == folded) exact = true;
        if (!m_exclude.isEmpty() && p.name.compare(m_exclude, Qt::CaseInsensitive) == 0) continue;
        const QString key = text::foldForSearch(p.name);
        // Tag mode: those who have it manually, and the own person, are left out.
        if (!m_tagId.isEmpty() && ((links.contains(key) && links.value(key).manual) || key == selfKey)) continue;
        const bool here = m_editor && m_editor->isMeetingPerson(p.name);
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

    QVector<editing::PersonInfo> out;
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
