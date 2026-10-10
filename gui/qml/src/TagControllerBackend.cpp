#include "TagControllerBackend.h"

#include "tanara/AppController.h"
#include "tanara/tags/TagService.h"

#include <QHash>

#include <algorithm>

namespace tanara_qml {

namespace {

TagItem toItem(const tanara::TagUsage& u)
{
    return {u.tag.id, u.tag.name, u.meetingCount, u.firstUsedAt, u.lastUsedAt};
}

TagMeetingItem toItem(const tanara::MeetingRef& r)
{
    return {r.meetingId, r.title, r.startedAt, r.durationMs};
}

QString sourceName(tanara::SuggestionSource s)
{
    switch (s) {
    case tanara::SuggestionSource::Cooccur: return QStringLiteral("cooccur");
    case tanara::SuggestionSource::Llm:     return QStringLiteral("llm");
    case tanara::SuggestionSource::People:  return QStringLiteral("people");
    case tanara::SuggestionSource::Similar: break;
    }
    return QStringLiteral("similar");
}

tanara::SuggestionSource sourceOf(const QString& s)
{
    if (s == QLatin1String("cooccur")) return tanara::SuggestionSource::Cooccur;
    if (s == QLatin1String("llm")) return tanara::SuggestionSource::Llm;
    if (s == QLatin1String("people")) return tanara::SuggestionSource::People;
    return tanara::SuggestionSource::Similar;
}

QString reasonName(tanara::ReasonKind k)
{
    switch (k) {
    case tanara::ReasonKind::Participant: return QStringLiteral("participant");
    case tanara::ReasonKind::Title:       return QStringLiteral("title");
    case tanara::ReasonKind::PeopleTags:  return QStringLiteral("peopleTags");
    case tanara::ReasonKind::Terms:       break;
    }
    return QStringLiteral("terms");
}

tanara::TagSource sourceOf(TagAddSource s)
{
    switch (s) {
    case TagAddSource::Suggestion: return tanara::TagSource::Suggestion;
    case TagAddSource::Llm:        return tanara::TagSource::Llm;
    case TagAddSource::Bulk:       return tanara::TagSource::Bulk;
    case TagAddSource::Manual:     break;
    }
    return tanara::TagSource::Manual;
}

// A szűrt újrakiadás felismerése: minden eleme benne van-e a korábbi listában.
bool subsetOf(const QVector<tanara::TagSuggestion>& list, const QVector<tanara::TagSuggestion>& before)
{
    for (const tanara::TagSuggestion& s : list) {
        const bool found = std::any_of(before.begin(), before.end(), [&](const tanara::TagSuggestion& b) {
            return b.source == s.source && b.isNew == s.isNew && b.tagId == s.tagId && b.name == s.name;
        });
        if (!found) return false;
    }
    return true;
}

} // namespace

TagControllerBackend::TagControllerBackend(tanara::AppController* controller, QObject* parent)
    : TagBackend(parent), m_controller(controller)
{
    if (!controller) return;
    connect(controller, &tanara::AppController::tagSuggestionsComputing, this, [this](const QString& id) {
        m_computing.insert(id);
        emit suggestionsChanged(id);
    });
    connect(controller, &tanara::AppController::tagSuggestionsReady, this,
            [this](const QString& id, const QVector<tanara::TagSuggestion>& list) {
        const bool fresh = m_computing.remove(id);
        if (fresh || !m_lists.contains(id) || !subsetOf(list, m_lists.value(id))) m_lists.insert(id, list);
        emit suggestionsChanged(id);
    });
    if (tanara::TagService* s = service()) {
        connect(s, &tanara::TagService::tagsChanged, this, &TagBackend::tagsChanged);
        connect(s, &tanara::TagService::meetingTagsChanged, this, &TagBackend::meetingTagsChanged);
        connect(s, &tanara::TagService::undoChanged, this, &TagBackend::undoChanged);
    }
}

tanara::TagService* TagControllerBackend::service() const
{
    return m_controller ? m_controller->tags() : nullptr;
}

TagSuggestionItem TagControllerBackend::toItem(const tanara::TagSuggestion& s)
{
    TagSuggestionItem out;
    out.tagId = s.tagId;
    out.name = s.name;
    out.isNew = s.isNew;
    out.source = sourceName(s.source);
    out.score = s.score;
    for (const tanara::SuggestionReason& r : s.reasons) out.reasons.push_back({reasonName(r.kind), r.values});
    for (const tanara::MeetingRef& m : s.similarMeetings) out.similarMeetings.push_back(tanara_qml::toItem(m));
    return out;
}

QVector<TagItem> TagControllerBackend::tags() const
{
    QVector<TagItem> out;
    if (tanara::TagService* s = service())
        for (const tanara::TagUsage& u : s->all()) out.push_back(tanara_qml::toItem(u));
    return out;
}

QVector<TagItem> TagControllerBackend::recent(int limit) const
{
    tanara::TagService* s = service();
    if (!s) return {};
    QHash<QString, tanara::TagUsage> usage;
    for (const tanara::TagUsage& u : s->all()) usage.insert(u.tag.id, u);
    QVector<TagItem> out;
    for (const tanara::Tag& t : s->recent(limit)) {
        const auto it = usage.constFind(t.id);
        out.push_back(it != usage.constEnd() ? tanara_qml::toItem(*it) : TagItem{t.id, t.name, 0, {}, {}});
    }
    return out;
}

QStringList TagControllerBackend::tagsOf(const QString& meetingId) const
{
    tanara::TagService* s = service();
    return s && !meetingId.isEmpty() ? s->tagsOf(meetingId) : QStringList();
}

QString TagControllerBackend::addTag(const QString& meetingId, const QString& nameOrId, TagAddSource source)
{
    tanara::TagService* s = service();
    if (!s || meetingId.isEmpty()) return {};
    return s->addTag(meetingId, nameOrId, sourceOf(source)).id;
}

void TagControllerBackend::removeTag(const QString& meetingId, const QString& tagId)
{
    if (tanara::TagService* s = service(); s && !meetingId.isEmpty()) s->removeTag(meetingId, tagId);
}

TagSuggestionState TagControllerBackend::suggestions(const QString& meetingId) const
{
    TagSuggestionState state;
    if (!m_controller || meetingId.isEmpty()) return state;
    state.computing = m_computing.contains(meetingId);
    tanara::TagService* svc = service();
    const auto it = m_lists.constFind(meetingId);
    QVector<tanara::TagSuggestion> list = it != m_lists.constEnd() ? *it : m_controller->pendingTagSuggestions(meetingId);
    list.erase(std::remove_if(list.begin(), list.end(), [&](const tanara::TagSuggestion& sg) {
        return !sg.isNew && (!svc || !svc->tag(sg.tagId).isValid());
    }), list.end());
    for (const tanara::TagSuggestion& sg : list) state.items.push_back(toItem(sg));
    // Egy kiadott lista egy forrásból jön; az együtt járónál az alapcímke is.
    if (!list.isEmpty()) {
        state.source = sourceName(list.first().source);
        if (list.first().source == tanara::SuggestionSource::Cooccur) state.baseTagId = list.first().baseTagId;
    }
    return state;
}

void TagControllerBackend::requestSuggestions(const QString& meetingId)
{
    if (m_controller && !meetingId.isEmpty()) m_controller->requestTagSuggestions(meetingId);
}

void TagControllerBackend::requestCooccur(const QString& meetingId, const QString& tagId)
{
    if (m_controller && !meetingId.isEmpty() && !tagId.isEmpty())
        m_controller->requestCooccurSuggestions(meetingId, tagId);
}

void TagControllerBackend::reject(const QString& meetingId, const TagSuggestionItem& suggestion)
{
    tanara::TagService* s = service();
    if (!s || meetingId.isEmpty()) return;
    tanara::TagSuggestion sg;
    sg.tagId = suggestion.isNew ? QString() : suggestion.tagId;
    sg.name = suggestion.name;
    sg.isNew = suggestion.isNew;
    sg.source = sourceOf(suggestion.source);
    sg.score = suggestion.score;
    s->reject(meetingId, sg);
}

bool TagControllerBackend::isRejected(const QString& meetingId, const QString& tagIdOrName) const
{
    tanara::TagService* s = service();
    return s && s->isRejected(meetingId, tagIdOrName);
}

TagProfileItem TagControllerBackend::profile(const QString& tagId) const
{
    TagProfileItem out;
    tanara::TagService* s = service();
    if (!s || tagId.isEmpty()) return out;
    const tanara::TagProfile p = s->profile(tagId);
    out.tagId = p.tagId.isEmpty() ? tagId : p.tagId;
    out.meetingCount = p.meetingCount;
    out.participants = p.topParticipants;
    out.terms = p.topTerms;
    out.cooccurring = p.cooccurring;
    for (const tanara::MeetingRef& m : p.meetings) out.meetings.push_back(tanara_qml::toItem(m));
    return out;
}

bool TagControllerBackend::rename(const QString& tagId, const QString& name)
{
    tanara::TagService* s = service();
    return s && s->rename(tagId, name);
}

void TagControllerBackend::merge(const QString& fromId, const QString& keepId)
{
    if (tanara::TagService* s = service()) s->merge(fromId, keepId);
}

void TagControllerBackend::remove(const QString& tagId)
{
    if (tanara::TagService* s = service()) s->remove(tagId);
}

void TagControllerBackend::beginGroup(const QString& label)
{
    if (tanara::TagService* s = service()) s->beginGroup(label);
}

void TagControllerBackend::endGroup()
{
    if (tanara::TagService* s = service()) s->endGroup();
}

bool TagControllerBackend::canUndo() const
{
    tanara::TagService* s = service();
    return s && s->canUndo();
}

QString TagControllerBackend::undoLabel() const
{
    tanara::TagService* s = service();
    return s ? s->undoLabel() : QString();
}

void TagControllerBackend::undo()
{
    if (tanara::TagService* s = service()) s->undo();
}

QVector<TagSuggestionItem> TagControllerBackend::draftSuggestions(const QString& title) const
{
    QVector<TagSuggestionItem> out;
    if (!m_controller) return out;
    for (const tanara::TagSuggestion& sg : m_controller->draftTagSuggestions(title)) out.push_back(toItem(sg));
    return out;
}

} // namespace tanara_qml
