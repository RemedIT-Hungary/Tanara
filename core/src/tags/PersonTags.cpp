#include "tanara/tags/PersonTags.h"

#include <QSet>

#include <algorithm>

namespace tanara {

namespace persontags {

QString personKey(const QString& name)
{
    return name.trimmed().toLower();
}

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

using persontags::personKey;

PersonTagStats PersonTagStats::compute(const QVector<PersonTagRow>& rows, const QString& selfName)
{
    PersonTagStats s;
    const QString self = personKey(selfName);
    QHash<QString, QDateTime> displayAt;   // a megjelenített írásmód a legutóbbi megbeszélésé
    for (const PersonTagRow& r : rows) {
        ++s.m_meetings;
        QStringList tags;
        for (const QString& t : r.tagIds)
            if (!t.isEmpty() && !tags.contains(t)) tags << t;
        for (const QString& t : std::as_const(tags)) s.m_tagTotal[t]++;
        QSet<QString> seen;
        for (const QString& raw : r.participants) {
            const QString k = personKey(raw);
            if (k.isEmpty() || k == self || seen.contains(k)) continue;
            seen.insert(k);
            s.m_personTotal[k]++;
            if (!displayAt.contains(k) || r.startedAt >= displayAt.value(k)) {
                displayAt.insert(k, r.startedAt);
                s.m_display.insert(k, raw.trimmed());
            }
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

int PersonTagStats::shared(const QString& person, const QString& tagId) const
{
    const auto it = m_shared.constFind(tagId);
    return it == m_shared.constEnd() ? 0 : it->value(personKey(person));
}

QDateTime PersonTagStats::lastShared(const QString& person, const QString& tagId) const
{
    const auto it = m_last.constFind(tagId);
    return it == m_last.constEnd() ? QDateTime() : it->value(personKey(person));
}

QString PersonTagStats::displayName(const QString& person) const
{
    return m_display.value(personKey(person), person.trimmed());
}

QStringList PersonTagStats::peopleOf(const QString& tagId) const
{
    QStringList out;
    const auto it = m_shared.constFind(tagId);
    if (it == m_shared.constEnd()) return out;
    for (auto p = it->constBegin(); p != it->constEnd(); ++p) out << m_display.value(p.key(), p.key());
    return out;
}

QStringList PersonTagStats::tagsOf(const QString& person) const
{
    return m_tagsOf.value(personKey(person));
}

TagEvidence PersonTagSnapshot::evidence(const QString& personName, const QStringList& meetingTagIds) const
{
    TagEvidence ev;
    const QString k = personKey(personName);
    if (k.isEmpty() || k == selfKey || meetingTagIds.isEmpty() || !stats) return ev;
    const QStringList manualTags = manual.value(k);
    ev.personTagIds = manualTags;

    QVector<TagEvidenceItem> support;
    QVector<TagEvidenceItem> other;
    for (const QString& t : meetingTagIds) {
        if (t.isEmpty() || !tagNames.contains(t)) continue;
        TagEvidenceItem it;
        it.tagId = t;
        it.name = tagNames.value(t);
        it.shared = stats->shared(personName, t);
        it.total = stats->tagTotal(t);
        it.manual = manualTags.contains(t);
        if (it.manual || persontags::learnedLink(it.shared, it.total)) support.append(it);
        else other.append(it);
    }
    auto label = [](const QVector<TagEvidenceItem>& items) {
        const TagEvidenceItem& f = items.first();
        QString s = QStringLiteral("#") + f.name;
        if (items.size() > 1) s += QStringLiteral(" +%1").arg(items.size() - 1);
        return s + QStringLiteral(" · %1 / %2").arg(f.shared).arg(f.total);
    };
    if (!support.isEmpty()) {
        // Kézi előbb, aztán a több közös megbeszélés.
        std::stable_sort(support.begin(), support.end(), [](const TagEvidenceItem& a, const TagEvidenceItem& b) {
            if (a.manual != b.manual) return a.manual;
            return a.shared > b.shared;
        });
        for (const TagEvidenceItem& it : std::as_const(support)) ev.supportTagIds << it.tagId;
        ev.items = support;
        ev.text = label(support);
        return ev;
    }
    // Ellentmondás: csak kézi címkés személynél, akinek egyik címkéje sem a megbeszélésé.
    if (manualTags.isEmpty() || other.isEmpty()) return ev;
    for (const TagEvidenceItem& it : std::as_const(other)) ev.contradictTagIds << it.tagId;
    ev.items = other;
    ev.text = label(other);
    return ev;
}

} // namespace tanara
