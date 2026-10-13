#include "TagDemoBackend.h"

#include "DemoText.h"

#include "TagMatching.h"

#include <QHash>

#include <algorithm>
#include <tuple>

namespace tanara_qml {

namespace {

QDateTime at(int y, int mo, int d, int h = 10, int mi = 0)
{
    return QDateTime(QDate(y, mo, d), QTime(h, mi));
}

// A demó „most”-ja: ettől determinisztikus a kép (a hozzáadás ide teszi az utolsó használatot).
const QDateTime kNow = at(2026, 10, 5, 10, 0);

// A kitalált címkenevek angol demo-nyelven (az azonosítók változatlanok; ugyanaz a készlet,
// mint a LibraryDemoData-ban). A magyar névvel hívható, angolul a párját adja.
QString tagText(const char* hu)
{
    static const QHash<QString, QString> en{
        {QStringLiteral("Q4 tervezés"), QStringLiteral("Q4 planning")},
        {QStringLiteral("Termék"), QStringLiteral("Product")},
        {QStringLiteral("Partnerek"), QStringLiteral("Partners")},
        {QStringLiteral("Belső"), QStringLiteral("Internal")},
        {QStringLiteral("MÉM-MDK"), QStringLiteral("Heritage Lab")},
        {QStringLiteral("Gyűjteménykezelés"), QStringLiteral("Collections")},
        {QStringLiteral("Ügyféltámogatás"), QStringLiteral("Customer support")},
        {QStringLiteral("Múzeumi pályázat"), QStringLiteral("Museum grant")},
        {QStringLiteral("Kutatás"), QStringLiteral("Research")},
        {QStringLiteral("Ügyfélsiker"), QStringLiteral("Customer success")},
    };
    const QString s = QString::fromUtf8(hu);
    return demotext::english() ? en.value(s, s) : s;
}

TagMeetingItem meeting(const QString& id, const QString& title, const QDateTime& when, int minutes)
{
    return {id, title, when, qint64(minutes) * 60000};
}

TagReasonItem reason(const char* kind, const QStringList& values)
{
    return {QString::fromLatin1(kind), values};
}

} // namespace

TagDemoBackend::TagDemoBackend(Content content, QObject* parent) : TagBackend(parent)
{
    if (content == Content::Empty) return;
    // Név, megbeszélés-szám, első / utolsó használat (a T10 listája).
    m_state.tags = {
        {QStringLiteral("t-nordvik"), QStringLiteral("Nordvik"), 14, at(2026, 3, 3), at(2026, 10, 2, 16)},
        {QStringLiteral("t-q4"), tagText("Q4 tervezés"), 9, at(2026, 6, 10), at(2026, 10, 2, 15)},
        {QStringLiteral("t-termek"), tagText("Termék"), 8, at(2026, 2, 12), at(2026, 10, 2, 9)},
        {QStringLiteral("t-partnerek"), tagText("Partnerek"), 5, at(2026, 4, 20), at(2026, 10, 1)},
        {QStringLiteral("t-belso"), tagText("Belső"), 11, at(2026, 1, 15), at(2026, 9, 25)},
        {QStringLiteral("t-muzeo"), QStringLiteral("Muzeo"), 6, at(2026, 5, 6), at(2026, 9, 30, 15)},
        {QStringLiteral("t-mem"), tagText("MÉM-MDK"), 7, at(2026, 5, 6), at(2026, 9, 30, 14)},
        {QStringLiteral("t-gyujtemeny"), tagText("Gyűjteménykezelés"), 4, at(2026, 6, 2), at(2026, 9, 30, 13)},
        {QStringLiteral("t-ugyfel"), tagText("Ügyféltámogatás"), 6, at(2026, 3, 18), at(2026, 9, 18)},
        {QStringLiteral("t-palyazat"), tagText("Múzeumi pályázat"), 3, at(2026, 4, 2), at(2026, 7, 8)},
        {QStringLiteral("t-nordvikas"), QStringLiteral("Nordvik AS"), 3, at(2026, 2, 10), at(2026, 5, 20)},
        {QStringLiteral("t-kutatas"), tagText("Kutatás"), 2, at(2026, 1, 20), at(2026, 4, 14)},
    };
    // A beviteli mező „LEGUTÓBB HASZNÁLT” listája (a felhasználó utolsó hozzáadásai, T01).
    m_state.recentIds = {QStringLiteral("t-nordvik"), QStringLiteral("t-q4"), QStringLiteral("t-muzeo"),
                         QStringLiteral("t-partnerek")};
    const QList<std::tuple<const char*, const char*, int>> pairs{
        {"t-nordvik", "t-partnerek", 6}, {"t-nordvik", "t-q4", 5}, {"t-nordvik", "t-ugyfel", 4},
        {"t-nordvik", "t-nordvikas", 1}, {"t-muzeo", "t-mem", 5}, {"t-muzeo", "t-gyujtemeny", 4},
        {"t-muzeo", "t-palyazat", 2}, {"t-termek", "t-q4", 4}, {"t-belso", "t-q4", 3},
        {"t-partnerek", "t-q4", 3}, {"t-ugyfel", "t-termek", 2}, {"t-belso", "t-kutatas", 1},
    };
    for (const auto& [a, b, n] : pairs)
        m_state.cooccur.insert(pairKey(QString::fromLatin1(a), QString::fromLatin1(b)), n);
    // Személyek: megbeszélés-szám, közös megbeszélések címkénként, kézi címkék (a vault
    // UX-tervének kitalált példái: „#Nordvik 11 / 14”, előre felvett személy 0 / 14-gyel).
    const QList<std::pair<const char*, int>> totals{
        {"Kovács Lilla", 41}, {"Fehér Gábor", 16}, {"Varga Árpád", 12}, {"Szabó Bence", 8},
        {"Bárány Gergely", 23}, {"Molnár Eszter", 0}, {"Gál Bence", 12}, {"Dénes Zsófia", 17}, {"Tóth Eszter", 3}};
    for (const auto& [name, n] : totals) m_state.personTotal.insert(demoName(name), n);
    const QList<std::tuple<const char*, const char*, int>> shared{
        {"t-nordvik", "Kovács Lilla", 14}, {"t-nordvik", "Fehér Gábor", 11}, {"t-nordvik", "Varga Árpád", 9},
        {"t-nordvik", "Szabó Bence", 6}, {"t-nordvik", "Bárány Gergely", 5}, {"t-nordvik", "Gál Bence", 1},
        {"t-partnerek", "Fehér Gábor", 3}, {"t-partnerek", "Varga Árpád", 4}, {"t-partnerek", "Szabó Bence", 2},
        {"t-q4", "Fehér Gábor", 5}, {"t-q4", "Varga Árpád", 4}, {"t-q4", "Dénes Zsófia", 6},
        {"t-belso", "Fehér Gábor", 4}, {"t-belso", "Dénes Zsófia", 7}, {"t-termek", "Fehér Gábor", 2},
        {"t-ugyfel", "Bárány Gergely", 4}};
    for (const auto& [tag, name, n] : shared)
        m_state.shared[QString::fromLatin1(tag)].insert(demoName(name), n);
    m_state.personTags = {
        {demoName("Varga Árpád"), {QStringLiteral("t-nordvik"), QStringLiteral("t-partnerek")}},
        {demoName("Szabó Bence"), {QStringLiteral("t-nordvik")}},
        {demoName("Molnár Eszter"), {QStringLiteral("t-nordvik")}},
        {demoName("Dénes Zsófia"), {QStringLiteral("t-q4")}},
        {demoName("Bárány Gergely"), {QStringLiteral("t-ugyfel")}},
    };
    loadMeetingDemo(demoMeetingId(), QStringLiteral("few"));
}

void TagDemoBackend::setPersonStatsBusy(bool busy)
{
    if (m_statsBusy == busy) return;
    m_statsBusy = busy;
    emit personStatsChanged();
}

QStringList TagDemoBackend::tagsOfPerson(const QString& name) const
{
    QStringList out;
    for (const QString& id : m_state.personTags.value(name))
        if (indexOf(id) >= 0) out << id;
    return out;
}

QString TagDemoBackend::addPersonTag(const QString& name, const QString& nameOrId)
{
    if (name.trimmed().isEmpty() || name == selfName()) return {};
    QString id = idOf(nameOrId);
    if (!id.isEmpty() && m_state.personTags.value(name).contains(id)) return id;
    const QString clean = tagmatch::normalizeName(nameOrId);
    if (id.isEmpty() && clean.isEmpty()) return {};
    touch(tr("Címke a személyen"));
    if (id.isEmpty()) {
        id = QStringLiteral("t-new-%1").arg(++m_created);
        m_state.tags.push_back({id, clean, 0, kNow, kNow});
        emit tagsChanged();
    }
    m_state.personTags[name] << id;
    emit personTagsChanged(name);
    return id;
}

void TagDemoBackend::removePersonTag(const QString& name, const QString& tagId)
{
    if (!m_state.personTags.value(name).contains(tagId)) return;
    touch(tr("Címke levétele a személyről"));
    m_state.personTags[name].removeAll(tagId);
    emit personTagsChanged(name);
}

void TagDemoBackend::rejectPersonTag(const QString& name, const QString& tagId)
{
    const QString key = name + QLatin1Char('|') + tagId;
    if (m_state.personRejected.contains(key)) return;
    touch(tr("Javaslat elutasítása"));
    m_state.personRejected.insert(key);
    emit personTagsChanged(name);
}

QStringList TagDemoBackend::peopleWith(const QString& tagId) const
{
    QStringList out;
    for (auto it = m_state.personTags.cbegin(); it != m_state.personTags.cend(); ++it)
        if (it.value().contains(tagId) && it.key() != selfName()) out << it.key();
    std::sort(out.begin(), out.end(), [](const QString& a, const QString& b) { return a.localeAwareCompare(b) < 0; });
    return out;
}

PersonTagItem TagDemoBackend::personItem(const QString& name, const QString& tagId) const
{
    PersonTagItem p;
    p.name = name;
    p.tagId = tagId;
    p.shared = m_state.shared.value(tagId).value(name);
    const int i = indexOf(tagId);
    p.tagTotal = i >= 0 ? m_state.tags[i].meetingCount : 0;
    p.personTotal = m_state.personTotal.value(name);
    p.manual = m_state.personTags.value(name).contains(tagId);
    p.rejected = m_state.personRejected.contains(name + QLatin1Char('|') + tagId);
    return p;
}

QVector<PersonTagItem> TagDemoBackend::peopleStats(const QString& tagId) const
{
    // Ugyanazok a szabályok, mint a core-ban: kézi + tanult (≥ 2 közös és ≥ 30 %), saját nélkül.
    QVector<PersonTagItem> out;
    if (indexOf(tagId) < 0 || m_statsBusy) return out;
    QStringList names = peopleWith(tagId);
    const QHash<QString, int> counts = m_state.shared.value(tagId);
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        if (it.key() == selfName() || names.contains(it.key())) continue;
        const PersonTagItem p = personItem(it.key(), tagId);
        if (p.shared >= 2 && p.shared * 100 >= 30 * p.tagTotal) names << it.key();
    }
    for (const QString& n : std::as_const(names)) out << personItem(n, tagId);
    std::sort(out.begin(), out.end(), [](const PersonTagItem& a, const PersonTagItem& b) {
        if (a.manual != b.manual) return a.manual;
        if (a.shared != b.shared) return a.shared > b.shared;
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return out;
}

QVector<PersonTagItem> TagDemoBackend::suggestTagsForPerson(const QString& name) const
{
    QVector<PersonTagItem> out;
    if (name == selfName() || m_statsBusy) return out;
    for (const TagItem& t : m_state.tags) {
        const PersonTagItem p = personItem(name, t.id);
        if (p.manual || p.rejected || p.shared < 2) continue;
        if (p.shared * 100 < 30 * p.tagTotal && p.shared * 100 < 50 * p.personTotal) continue;
        out << p;
    }
    std::stable_sort(out.begin(), out.end(), [](const PersonTagItem& a, const PersonTagItem& b) {
        if (a.shared != b.shared) return a.shared > b.shared;
        return qint64(a.shared) * b.tagTotal > qint64(b.shared) * a.tagTotal;
    });
    return out;
}

void TagDemoBackend::clearAll()
{
    m_state = State();
    m_undo.clear();
    emitAll();
}

QString TagDemoBackend::pairKey(const QString& a, const QString& b)
{
    return a < b ? a + QLatin1Char('|') + b : b + QLatin1Char('|') + a;
}

int TagDemoBackend::indexOf(const QString& tagId) const
{
    for (int i = 0; i < m_state.tags.size(); ++i)
        if (m_state.tags[i].id == tagId) return i;
    return -1;
}

QString TagDemoBackend::idOf(const QString& nameOrId) const
{
    if (indexOf(nameOrId) >= 0) return nameOrId;
    const QString k = tagmatch::key(nameOrId);
    for (const TagItem& t : m_state.tags)
        if (tagmatch::key(t.name) == k) return t.id;
    return {};
}

QVector<TagSuggestionItem> TagDemoBackend::draftSuggestions(const QString& /*title*/) const
{
    // T07: „Javasolt: + Ügyféltámogatás · hasonló cím: „Ügyféltámogatás heti””.
    TagSuggestionItem s;
    s.name = tagText("Ügyféltámogatás");
    s.tagId = idOf(s.name);
    s.isNew = s.tagId.isEmpty();
    s.source = QStringLiteral("similar");
    s.reasons = {reason("title", {demoText("Ügyféltámogatás heti", "Customer support weekly")})};
    s.similarMeetings = {meeting(QStringLiteral("m-support-0918"), demoText("Ügyféltámogatás heti", "Customer support weekly"), at(2026, 9, 18), 40)};
    return {s};
}

void TagDemoBackend::loadMeetingDemo(const QString& meetingId, const QString& state)
{
    auto ids = [this](std::initializer_list<const char*> names) {
        QStringList out;
        for (const char* n : names) {
            const QString id = idOf(tagText(n));
            if (!id.isEmpty()) out << id;
        }
        return out;
    };
    auto sug = [this](const char* name, const char* source, QVector<TagReasonItem> reasons = {},
                      QVector<TagMeetingItem> similar = {}, bool isNew = false) {
        TagSuggestionItem s;
        s.name = tagText(name);
        s.tagId = isNew ? QString() : idOf(s.name);
        s.isNew = isNew || s.tagId.isEmpty();
        s.source = QString::fromLatin1(source);
        s.reasons = std::move(reasons);
        s.similarMeetings = std::move(similar);
        return s;
    };
    const auto nordvikWeekly = meeting(QStringLiteral("m-nordvik-0929"), demoText("Nordvik heti meeting", "Nordvik weekly meeting"), at(2026, 9, 29), 55);
    const auto handover = meeting(QStringLiteral("m-handover-1002"), demoText("Ügyféltámogatás átadás", "Customer support handover"), at(2026, 10, 2), 52);
    const auto quarterly = meeting(QStringLiteral("m-quarterly-0702"), demoText("Negyedéves partnertalálkozó", "Quarterly partner review"), at(2026, 7, 2), 71);

    TagSuggestionState s;
    QStringList tags;
    if (state == QLatin1String("few")) {
        tags = ids({"Nordvik", "Partnerek"});
    } else if (state == QLatin1String("many")) {
        tags = ids({"Nordvik", "Partnerek", "Q4 tervezés", "Termék", "Ügyféltámogatás", "Belső", "Muzeo",
                    "MÉM-MDK", "Kutatás", "Múzeumi pályázat"});
    } else if (state == QLatin1String("draft")) {
        // T07: átirat előtt — egy felrakott címke, a javaslat a cím alapján jön (draftSuggestions).
        tags = ids({"Nordvik"});
    } else if (state == QLatin1String("computing")) {
        tags = ids({"Nordvik"});
        s.computing = true;
    } else if (state == QLatin1String("similar")) {
        tags = ids({"Nordvik"});
        s.source = QStringLiteral("similar");
        s.items = {sug("Partnerek", "similar",
                       {reason("participant", {demoName("Varga Nóra"), demoName("Fehér Ádám")}),
                        reason("terms", {demoText("partnerportál", "partner portal"), demoText("ütemterv", "roadmap"), demoText("megújítás", "renewal")})},
                       {nordvikWeekly, handover}),
                   sug("Q4 tervezés", "similar",
                       {reason("title", {demoText("Negyedéves partnertalálkozó (júl. 2.)", "Quarterly partner review (Jul 2)")}),
                        reason("terms", {demoText("ütemterv", "roadmap"), demoText("költségkeret", "budget")})},
                       {quarterly})};
    } else if (state == QLatin1String("why")) {
        // T02: a „Miért?” panel tartalma.
        tags = ids({"Partnerek", "Q4 tervezés"});
        s.source = QStringLiteral("similar");
        s.items = {sug("Nordvik", "similar",
                       {reason("participant", {demoName("Varga Nóra"), demoName("Fehér Ádám")}),
                        reason("terms", {QStringLiteral("Nordvik"), demoText("partnerportál", "partner portal"), demoText("ütemterv", "roadmap")})},
                       {nordvikWeekly, handover}),
                   sug("Ügyféltámogatás", "similar",
                       {reason("title", {demoText("Negyedéves partnertalálkozó (júl. 2.)", "Quarterly partner review (Jul 2)")}),
                        reason("terms", {demoText("támogatási igény", "support demand"), demoText("súgóoldalak", "help pages")})},
                       {quarterly})};
    } else if (state == QLatin1String("people") || state == QLatin1String("peopleWhy")) {
        // UX-terv 3.3: a résztvevők kézi címkéiből („A résztvevők alapján”), átírás előtt is; a
        // „Miért?” panelben „Résztvevők címkéje: … (mindkettőjükön)”. peopleWhy: egy hasonlóság-
        // alapú javaslat is van (a forrás-címke akkor is a résztvevőké, mert ők állnak elöl).
        tags = ids({"Q4 tervezés"});
        s.source = QStringLiteral("people");
        s.items = {sug("Nordvik", "people",
                       {reason("peopleTags", {demoName("Fehér Gábor"), demoName("Varga Árpád")})}),
                   sug("Partnerek", "people", {reason("peopleTags", {demoName("Fehér Gábor")})})};
        if (state == QLatin1String("peopleWhy"))
            s.items << sug("Ügyféltámogatás", "similar",
                           {reason("participant", {demoName("Fehér Gábor")}),
                            reason("terms", {demoText("támogatási igény", "support demand"), demoText("súgóoldalak", "help pages")})},
                           {handover});
    } else if (state == QLatin1String("cooccur")) {
        tags = ids({"Muzeo"});
        s.source = QStringLiteral("cooccur");
        s.baseTagId = idOf(QStringLiteral("Muzeo"));
        s.items = {sug("MÉM-MDK", "cooccur"), sug("Gyűjteménykezelés", "cooccur")};
    } else if (state == QLatin1String("llm")) {
        tags = ids({"Nordvik"});
        s.source = QStringLiteral("llm");
        s.items = {sug("Termék", "llm", {reason("terms", {demoText("termékút", "product path"), demoText("kiadás", "release")})}),
                   sug("Ügyfélsiker", "llm", {reason("terms", {demoText("támogatási jegyek", "support tickets"), demoText("elégedettség", "satisfaction")})},
                       {}, true)};
    }
    m_state.meetingTags.insert(meetingId, tags);
    m_state.suggestions.insert(meetingId, s);
    QSet<QString> rejected;
    for (const QString& r : std::as_const(m_state.rejected))
        if (!r.startsWith(meetingId + QLatin1Char('|'))) rejected.insert(r);
    m_state.rejected = rejected;
    m_undo.clear();
    emit meetingTagsChanged(meetingId);
    emit suggestionsChanged(meetingId);
    emit undoChanged();
}

QVector<TagItem> TagDemoBackend::recent(int limit) const
{
    QVector<TagItem> out;
    for (const QString& id : m_state.recentIds) {
        const int i = indexOf(id);
        if (i < 0) continue;
        out << m_state.tags[i];
        if (out.size() >= limit) break;
    }
    return out;
}

void TagDemoBackend::touch(const QString& fallbackLabel)
{
    if (m_groupDepth > 0) {
        if (m_groupRecorded) return;
        m_groupRecorded = true;
        m_undo.push_back({m_groupLabel, m_state});
    } else {
        m_undo.push_back({fallbackLabel, m_state});
    }
    while (m_undo.size() > 50) m_undo.removeFirst();
    emit undoChanged();
}

void TagDemoBackend::beginGroup(const QString& label)
{
    if (m_groupDepth++ == 0) {
        m_groupLabel = label;
        m_groupRecorded = false;
    }
}

void TagDemoBackend::endGroup()
{
    if (m_groupDepth > 0) --m_groupDepth;
}

QString TagDemoBackend::addTag(const QString& meetingId, const QString& nameOrId, TagAddSource)
{
    QString id = idOf(nameOrId);
    QStringList& onMeeting = m_state.meetingTags[meetingId];
    if (!id.isEmpty() && onMeeting.contains(id)) return id;
    const QString name = tagmatch::normalizeName(nameOrId);
    if (id.isEmpty() && name.isEmpty()) return {};
    touch(tr("Címke hozzáadva"));
    QStringList& tags = m_state.meetingTags[meetingId];
    if (id.isEmpty()) {
        id = QStringLiteral("t-new-%1").arg(++m_created);
        m_state.tags.push_back({id, name, 0, kNow, kNow});
    }
    TagItem& t = m_state.tags[indexOf(id)];
    for (const QString& other : std::as_const(tags)) m_state.cooccur[pairKey(id, other)] += 1;
    tags << id;
    t.meetingCount += 1;
    t.lastUsedAt = kNow;
    m_state.recentIds.removeAll(id);
    m_state.recentIds.prepend(id);
    emit tagsChanged();
    emit meetingTagsChanged(meetingId);
    return id;
}

void TagDemoBackend::removeTag(const QString& meetingId, const QString& tagId)
{
    QStringList tags = m_state.meetingTags.value(meetingId);
    if (!tags.contains(tagId)) return;
    touch(tr("Címke eltávolítva"));
    tags.removeAll(tagId);
    m_state.meetingTags.insert(meetingId, tags);
    for (const QString& other : std::as_const(tags)) {
        const QString k = pairKey(tagId, other);
        if (m_state.cooccur.value(k) > 0) m_state.cooccur[k] -= 1;
    }
    const int i = indexOf(tagId);
    if (i >= 0) m_state.tags[i].meetingCount = std::max(0, m_state.tags[i].meetingCount - 1);
    emit tagsChanged();
    emit meetingTagsChanged(meetingId);
}

void TagDemoBackend::requestSuggestions(const QString&)
{
    // A demóban a javaslatok a loadMeetingDemo()-val töltődnek; nincs háttérszámolás.
}

QVector<QPair<QString, int>> TagDemoBackend::cooccurring(const QString& tagId) const
{
    QVector<QPair<QString, int>> out;
    for (auto it = m_state.cooccur.cbegin(); it != m_state.cooccur.cend(); ++it) {
        if (it.value() <= 0) continue;
        const QStringList ab = it.key().split(QLatin1Char('|'));
        if (ab.size() != 2 || !ab.contains(tagId)) continue;
        const QString other = ab[0] == tagId ? ab[1] : ab[0];
        if (indexOf(other) >= 0) out.push_back({other, it.value()});
    }
    std::sort(out.begin(), out.end(), [this](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return m_state.tags[indexOf(a.first)].name < m_state.tags[indexOf(b.first)].name;
    });
    return out;
}

void TagDemoBackend::requestCooccur(const QString& meetingId, const QString& tagId)
{
    const int i = indexOf(tagId);
    if (i < 0) return;
    const int base = std::max(1, m_state.tags[i].meetingCount);
    const QStringList onMeeting = m_state.meetingTags.value(meetingId);
    TagSuggestionState s;
    s.source = QStringLiteral("cooccur");
    s.baseTagId = tagId;
    for (const auto& [other, n] : cooccurring(tagId)) {
        if (n < 2 || n * 2 < base) continue;
        if (onMeeting.contains(other) || isRejected(meetingId, other)) continue;
        TagSuggestionItem item;
        item.tagId = other;
        item.name = m_state.tags[indexOf(other)].name;
        item.source = s.source;
        item.score = double(n) / base;
        s.items << item;
    }
    // Ha nincs együtt járó címke, a korábbi javaslatok maradnak.
    if (s.items.isEmpty()) return;
    m_state.suggestions.insert(meetingId, s);
    emit suggestionsChanged(meetingId);
}

void TagDemoBackend::reject(const QString& meetingId, const TagSuggestionItem& suggestion)
{
    touch(tr("Javaslat elutasítva"));
    m_state.rejected.insert(meetingId + QLatin1Char('|')
                            + (suggestion.tagId.isEmpty() ? tagmatch::key(suggestion.name) : suggestion.tagId));
    emit suggestionsChanged(meetingId);
}

bool TagDemoBackend::isRejected(const QString& meetingId, const QString& tagIdOrName) const
{
    const QString prefix = meetingId + QLatin1Char('|');
    return m_state.rejected.contains(prefix + tagIdOrName)
        || m_state.rejected.contains(prefix + tagmatch::key(tagIdOrName));
}

TagProfileItem TagDemoBackend::profile(const QString& tagId) const
{
    TagProfileItem p;
    const int i = indexOf(tagId);
    if (i < 0) return p;
    const TagItem& t = m_state.tags[i];
    p.tagId = tagId;
    p.meetingCount = t.meetingCount;
    p.cooccurring = cooccurring(tagId);
    if (tagId == QLatin1String("t-nordvik")) {
        p.participants = {{demoName("Kovács Anna"), 12}, {demoName("Szabó Bence"), 11},
                          {demoName("Lantos Réka"), 6}, {demoName("Varga Nóra"), 5}};
        p.terms = {QStringLiteral("Nordvik"), demoText("partnerportál", "partner portal"), demoText("ütemterv", "roadmap"),
                   QStringLiteral("SLA"), demoText("eszkaláció", "escalation"), demoText("Qvarko-modul", "Qvarko module"),
                   demoText("PixelTár", "PixelVault"), demoText("megújítás", "renewal")};
        p.meetings = {meeting(QStringLiteral("m-handover-1002"), demoText("Ügyféltámogatás átadás", "Customer support handover"), at(2026, 10, 2), 52),
                      meeting(QStringLiteral("m-quarterly-1001"), demoText("Negyedéves partnertalálkozó", "Quarterly partner review"), at(2026, 10, 1), 76),
                      meeting(QStringLiteral("m-nordvik-0929"), demoText("Nordvik heti meeting", "Nordvik weekly meeting"), at(2026, 9, 29), 55),
                      meeting(QStringLiteral("m-nordvik-0922"), demoText("Nordvik heti meeting – demó előtt", "Nordvik weekly meeting – before the demo"), at(2026, 9, 22), 48)};
        return p;
    }
    // A többi címke profilja sablonból: a résztvevők és a kifejezések arányosan.
    const QStringList people{demoName("Kovács Anna"), demoName("Szabó Bence"),
                             demoName("Lantos Réka"), demoName("Varga Nóra")};
    const double share[] = {0.85, 0.6, 0.4, 0.25};
    for (int k = 0; k < people.size(); ++k) {
        const int n = int(t.meetingCount * share[(k + i) % 4] + 0.5);
        if (n >= 1) p.participants.push_back({people[k], n});
    }
    std::sort(p.participants.begin(), p.participants.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    p.terms = {t.name, demoText("ütemterv", "roadmap"), demoText("egyeztetés", "sync"), demoText("határidő", "deadline")};
    const QVector<QPair<QString, int>> pool{{demoText("Termékcsapat heti egyeztetés", "Product team weekly sync"), 30},
                                            {demoText("Heti tervezés", "Weekly planning"), 33},
                                            {demoText("Termékbemutató, 2. kör", "Product demo, round 2"), 44},
                                            {demoText("Infrastruktúra áttekintés", "Infrastructure review"), 41},
                                            {demoText("Árazás egyeztetés", "Pricing discussion"), 27}};
    const int shown = std::min(t.meetingCount, 3);
    for (int k = 0; k < shown; ++k) {
        const auto& [title, minutes] = pool[(i + k) % pool.size()];
        p.meetings.push_back(meeting(QStringLiteral("m-%1-%2").arg(tagId).arg(k), title,
                                     t.lastUsedAt.addDays(-7 * k), minutes));
    }
    return p;
}

bool TagDemoBackend::rename(const QString& tagId, const QString& name)
{
    const int i = indexOf(tagId);
    const QString clean = tagmatch::normalizeName(name);
    if (i < 0 || clean.isEmpty()) return false;
    const QString other = idOf(clean);
    if (!other.isEmpty() && other != tagId) return false;
    if (m_state.tags[i].name == clean) return true;
    touch(tr("Átnevezés"));
    m_state.tags[i].name = clean;
    emitAll();
    return true;
}

void TagDemoBackend::merge(const QString& fromId, const QString& keepId)
{
    const int from = indexOf(fromId), keep = indexOf(keepId);
    if (from < 0 || keep < 0 || fromId == keepId) return;
    touch(tr("Összevonás"));
    const int common = m_state.cooccur.value(pairKey(fromId, keepId));
    m_state.tags[keep].meetingCount += m_state.tags[from].meetingCount - common;
    m_state.tags[keep].firstUsedAt = std::min(m_state.tags[keep].firstUsedAt, m_state.tags[from].firstUsedAt);
    m_state.tags[keep].lastUsedAt = std::max(m_state.tags[keep].lastUsedAt, m_state.tags[from].lastUsedAt);
    for (auto it = m_state.meetingTags.begin(); it != m_state.meetingTags.end(); ++it) {
        QStringList& tags = it.value();
        const int pos = tags.indexOf(fromId);
        if (pos < 0) continue;
        if (tags.contains(keepId)) tags.removeAt(pos);
        else tags[pos] = keepId;
    }
    // Az együtt járás összeadódik (a két címke egymással való párja megszűnik).
    for (const auto& [other, n] : cooccurring(fromId))
        if (other != keepId) m_state.cooccur[pairKey(keepId, other)] += n;
    for (auto it = m_state.cooccur.begin(); it != m_state.cooccur.end();) {
        if (it.key().split(QLatin1Char('|')).contains(fromId)) it = m_state.cooccur.erase(it);
        else ++it;
    }
    // A személyeken is egyesül.
    for (auto it = m_state.personTags.begin(); it != m_state.personTags.end(); ++it) {
        QStringList& ids = it.value();
        const int pos = ids.indexOf(fromId);
        if (pos < 0) continue;
        if (ids.contains(keepId)) ids.removeAt(pos);
        else ids[pos] = keepId;
    }
    m_state.recentIds.removeAll(fromId);
    m_state.tags.removeAt(from);
    emitAll();
}

void TagDemoBackend::remove(const QString& tagId)
{
    const int i = indexOf(tagId);
    if (i < 0) return;
    touch(tr("Címke törölve"));
    for (auto it = m_state.meetingTags.begin(); it != m_state.meetingTags.end(); ++it) it.value().removeAll(tagId);
    for (auto it = m_state.cooccur.begin(); it != m_state.cooccur.end();) {
        if (it.key().split(QLatin1Char('|')).contains(tagId)) it = m_state.cooccur.erase(it);
        else ++it;
    }
    for (auto it = m_state.rejected.begin(); it != m_state.rejected.end();) {
        if (it->endsWith(QLatin1Char('|') + tagId)) it = m_state.rejected.erase(it);
        else ++it;
    }
    for (auto it = m_state.personTags.begin(); it != m_state.personTags.end(); ++it) it.value().removeAll(tagId);
    m_state.recentIds.removeAll(tagId);
    m_state.tags.removeAt(i);
    emitAll();
}

void TagDemoBackend::undo()
{
    if (m_undo.isEmpty()) return;
    m_state = m_undo.takeLast().before;
    emitAll();
}

void TagDemoBackend::emitAll()
{
    emit tagsChanged();
    for (auto it = m_state.meetingTags.cbegin(); it != m_state.meetingTags.cend(); ++it) {
        emit meetingTagsChanged(it.key());
        emit suggestionsChanged(it.key());
    }
    emit personTagsChanged(QString());
    emit undoChanged();
}

} // namespace tanara_qml
