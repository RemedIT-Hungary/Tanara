#include "tanara/library/tags/PersonTags.h"

#include <QSet>

#include <algorithm>

namespace tanara::library {

namespace persontags {

bool learnedLink(int shared, int tagTotal)
{
    return shared >= kMinShared && tagTotal > 0 && shared * 100 >= kTagRatioPct * tagTotal;
}

bool suggestableForPerson(int shared, int tagTotal, int personTotal)
{
    if (shared < kMinShared) return false;
    return learnedLink(shared, tagTotal) || (personTotal > 0 && shared * 100 >= kPersonRatioPct * personTotal);
}

} // namespace persontags

PersonTagStats PersonTagStats::compute(const QVector<PersonTagRow>& rows, const PersonId& self)
{
    PersonTagStats s;
    for (const PersonTagRow& r : rows) {
        ++s.m_meetings;
        QStringList tags;
        for (const QString& t : r.tagIds)
            if (!t.isEmpty() && !tags.contains(t)) tags << t;
        for (const QString& t : std::as_const(tags)) s.m_tagTotal[t]++;
        if (!r.meetingId.isEmpty()) s.m_meetingTags.insert(r.meetingId, tags);
        QSet<PersonId> seen;
        for (const PersonId& k : r.participants) {
            if (k.isNull() || k == self || seen.contains(k)) continue;
            seen.insert(k);
            s.m_personTotal[k]++;
            if (!tags.isEmpty()) s.m_personTagged[k]++;
            if (!r.meetingId.isEmpty()) s.m_meetingPeople[r.meetingId] << k;
            for (const QString& t : std::as_const(tags)) {
                int& c = s.m_shared[t][k];
                if (c == 0) s.m_tagsOf[k] << t;
                ++c;
                QDateTime& last = s.m_last[t][k];
                if (!last.isValid() || r.startedAt > last) last = r.startedAt;
            }
        }
    }
    return s;
}

int PersonTagStats::shared(const PersonId& person, const QString& tagId) const
{
    const auto it = m_shared.constFind(tagId);
    return it == m_shared.constEnd() ? 0 : it->value(person);
}

QDateTime PersonTagStats::lastShared(const PersonId& person, const QString& tagId) const
{
    const auto it = m_last.constFind(tagId);
    return it == m_last.constEnd() ? QDateTime() : it->value(person);
}

PersonIds PersonTagStats::peopleOf(const QString& tagId) const
{
    PersonIds out;
    const auto it = m_shared.constFind(tagId);
    if (it == m_shared.constEnd()) return out;
    for (auto p = it->constBegin(); p != it->constEnd(); ++p) out << p.key();
    return out;
}

QStringList PersonTagStats::tagsOf(const PersonId& person) const
{
    return m_tagsOf.value(person);
}

bool PersonTagStats::meetingHasPerson(const QString& meetingId, const PersonId& person) const
{
    const auto it = m_meetingPeople.constFind(meetingId);
    return it != m_meetingPeople.constEnd() && it->contains(person);
}

namespace {

// Numbers without the meeting's own row: c(p,t), |M(t)|, |M(p)| and the tagged meetings.
struct Counts {
    int shared = 0, tagTotal = 0;
};

Counts countsWithout(const PersonTagStats& st, const PersonId& person, const QString& tagId, const QString& meetingId)
{
    Counts c{ st.shared(person, tagId), st.tagTotal(tagId) };
    if (meetingId.isEmpty() || !st.meetingTags(meetingId).contains(tagId)) return c;
    c.tagTotal = std::max(0, c.tagTotal - 1);
    if (st.meetingHasPerson(meetingId, person)) c.shared = std::max(0, c.shared - 1);
    return c;
}

int personTotalWithout(const PersonTagStats& st, const PersonId& person, const QString& meetingId)
{
    const int n = st.personTotal(person);
    return !meetingId.isEmpty() && st.meetingHasPerson(meetingId, person) ? std::max(0, n - 1) : n;
}

int personTaggedWithout(const PersonTagStats& st, const PersonId& person, const QString& meetingId)
{
    const int n = st.personTagged(person);
    const bool here = !meetingId.isEmpty() && st.meetingHasPerson(meetingId, person)
        && !st.meetingTags(meetingId).isEmpty();
    return here ? std::max(0, n - 1) : n;
}

} // namespace

TagEvidence PersonTagSnapshot::evidence(const PersonId& personName, const QStringList& meetingTagIds,
                                        const QString& meetingId) const
{
    TagEvidence ev;
    const PersonId& k = personName;
    if (k.isNull() || k == self || meetingTagIds.isEmpty() || !stats) return ev;
    const QStringList manualTags = manual.value(k);

    QVector<TagEvidenceItem> support;
    QVector<TagEvidenceItem> other;
    for (const QString& t : meetingTagIds) {
        if (t.isEmpty() || !tagNames.contains(t)) continue;
        if (std::any_of(support.cbegin(), support.cend(), [&](const TagEvidenceItem& x) { return x.tagId == t; })
            || std::any_of(other.cbegin(), other.cend(), [&](const TagEvidenceItem& x) { return x.tagId == t; }))
            continue;
        const Counts c = countsWithout(*stats, personName, t, meetingId);
        TagEvidenceItem it;
        it.tagId = t;
        it.name = tagNames.value(t);
        it.shared = c.shared;
        it.total = c.tagTotal;
        it.manual = manualTags.contains(t);
        if (it.manual || persontags::learnedLink(it.shared, it.total)) support.append(it);
        else other.append(it);
    }
    auto fill = [&ev](const QVector<TagEvidenceItem>& items) {
        const TagEvidenceItem& f = items.first();
        ev.items = items;
        ev.text = QStringLiteral("#") + f.name;
        if (items.size() > 1) ev.text += QStringLiteral(" +%1").arg(items.size() - 1);
        ev.detail = QStringLiteral("%1 / %2").arg(f.shared).arg(f.total);
    };
    if (!support.isEmpty()) {
        // Manual first, then the more shared meetings.
        std::stable_sort(support.begin(), support.end(), [](const TagEvidenceItem& a, const TagEvidenceItem& b) {
            if (a.manual != b.manual) return a.manual;
            return a.shared > b.shared;
        });
        for (const TagEvidenceItem& it : std::as_const(support)) ev.supportTagIds << it.tagId;
        ev.personTagIds = manualTags;
        for (const QString& t : std::as_const(ev.personTagIds)) ev.personTagNames << tagNames.value(t);
        fill(support);
        return ev;
    }
    // Contradiction (UX plan rule 4, all four conditions): (1) the meeting has a (known) tag;
    if (other.isEmpty()) return ev;
    // (2) the person has a manual tag, or enough tagged meetings;
    if (manualTags.isEmpty()
        && personTaggedWithout(*stats, personName, meetingId) < persontags::kContradictPersonTagged)
        return ev;
    // (3) c = 0 for every tag (and none is manually on them, which would be support);
    // (4) every tag is mature.
    for (const TagEvidenceItem& it : std::as_const(other))
        if (it.shared > 0 || it.total < persontags::kMatureTagMeetings) return ev;
    for (const TagEvidenceItem& it : std::as_const(other)) ev.contradictTagIds << it.tagId;
    // The "Usually on other tags" list: manual tags, failing that the learned ones by frequency.
    QStringList elsewhere = manualTags;
    if (elsewhere.isEmpty()) {
        elsewhere = stats->tagsOf(personName);
        elsewhere.removeIf([&](const QString& t) { return countsWithout(*stats, personName, t, meetingId).shared <= 0; });
        std::stable_sort(elsewhere.begin(), elsewhere.end(), [&](const QString& a, const QString& b) {
            return countsWithout(*stats, personName, a, meetingId).shared
                 > countsWithout(*stats, personName, b, meetingId).shared;
        });
    }
    for (const QString& t : std::as_const(elsewhere)) {
        if (!tagNames.contains(t)) continue;
        ev.personTagIds << t;
        ev.personTagNames << tagNames.value(t);
    }
    fill(other);
    return ev;
}

QVector<PersonTagQuestion> PersonTagSnapshot::learningQuestions(
    const QString& meetingId, const QStringList& meetingTagIds, const PersonIds& people,
    const std::function<bool(const PersonId&, const QString&)>& rejected) const
{
    QVector<PersonTagQuestion> out;
    if (!stats || meetingTagIds.isEmpty()) return out;
    QStringList tags;
    for (const QString& t : meetingTagIds)
        if (!t.isEmpty() && tagNames.contains(t) && !tags.contains(t)) tags << t;
    if (tags.isEmpty()) return out;
    auto bucket = [&out, this](const QString& tagId, bool isNew) -> PersonTagQuestion& {
        for (PersonTagQuestion& q : out)
            if (q.tagId == tagId && q.newPeople == isNew) return q;
        out.append(PersonTagQuestion{ tagId, tagNames.value(tagId), {}, {}, isNew });
        return out.last();
    };
    QSet<PersonId> seen;
    for (const PersonId& k : people) {
        if (k.isNull() || k == self || seen.contains(k)) continue;
        seen.insert(k);
        const QStringList manualTags = manual.value(k);
        const bool isNew = personTotalWithout(*stats, k, meetingId) == 0;
        for (const QString& t : std::as_const(tags)) {
            if (manualTags.contains(t) || (rejected && rejected(k, t))) continue;
            if (!isNew && countsWithout(*stats, k, t, meetingId).shared != 1) continue;
            PersonTagQuestion& q = bucket(t, isNew);
            q.people << k;
            q.names << personName(names, k);
        }
    }
    // A multi-person question first; on a tie the meeting's tag order (stable).
    std::stable_sort(out.begin(), out.end(), [](const PersonTagQuestion& a, const PersonTagQuestion& b) {
        return a.people.size() > b.people.size();
    });
    return out;
}

} // namespace tanara::library