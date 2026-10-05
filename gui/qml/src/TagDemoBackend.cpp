#include "TagDemoBackend.h"

#include "TagMatching.h"

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
        {QStringLiteral("t-q4"), QStringLiteral("Q4 tervezés"), 9, at(2026, 6, 10), at(2026, 10, 2, 15)},
        {QStringLiteral("t-termek"), QStringLiteral("Termék"), 8, at(2026, 2, 12), at(2026, 10, 2, 9)},
        {QStringLiteral("t-partnerek"), QStringLiteral("Partnerek"), 5, at(2026, 4, 20), at(2026, 10, 1)},
        {QStringLiteral("t-belso"), QStringLiteral("Belső"), 11, at(2026, 1, 15), at(2026, 9, 25)},
        {QStringLiteral("t-museumplus"), QStringLiteral("MuseumPlus"), 6, at(2026, 5, 6), at(2026, 9, 30, 15)},
        {QStringLiteral("t-mem"), QStringLiteral("MÉM-MDK"), 7, at(2026, 5, 6), at(2026, 9, 30, 14)},
        {QStringLiteral("t-gyujtemeny"), QStringLiteral("Gyűjteménykezelés"), 4, at(2026, 6, 2), at(2026, 9, 30, 13)},
        {QStringLiteral("t-ugyfel"), QStringLiteral("Ügyféltámogatás"), 6, at(2026, 3, 18), at(2026, 9, 18)},
        {QStringLiteral("t-palyazat"), QStringLiteral("Múzeumi pályázat"), 3, at(2026, 4, 2), at(2026, 7, 8)},
        {QStringLiteral("t-nordvikas"), QStringLiteral("Nordvik AS"), 3, at(2026, 2, 10), at(2026, 5, 20)},
        {QStringLiteral("t-kutatas"), QStringLiteral("Kutatás"), 2, at(2026, 1, 20), at(2026, 4, 14)},
    };
    // A beviteli mező „LEGUTÓBB HASZNÁLT” listája (a felhasználó utolsó hozzáadásai, T01).
    m_state.recentIds = {QStringLiteral("t-nordvik"), QStringLiteral("t-q4"), QStringLiteral("t-museumplus"),
                         QStringLiteral("t-partnerek")};
    const QList<std::tuple<const char*, const char*, int>> pairs{
        {"t-nordvik", "t-partnerek", 6}, {"t-nordvik", "t-q4", 5}, {"t-nordvik", "t-ugyfel", 4},
        {"t-nordvik", "t-nordvikas", 1}, {"t-museumplus", "t-mem", 5}, {"t-museumplus", "t-gyujtemeny", 4},
        {"t-museumplus", "t-palyazat", 2}, {"t-termek", "t-q4", 4}, {"t-belso", "t-q4", 3},
        {"t-partnerek", "t-q4", 3}, {"t-ugyfel", "t-termek", 2}, {"t-belso", "t-kutatas", 1},
    };
    for (const auto& [a, b, n] : pairs)
        m_state.cooccur.insert(pairKey(QString::fromLatin1(a), QString::fromLatin1(b)), n);
    loadMeetingDemo(demoMeetingId(), QStringLiteral("few"));
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
    s.name = QStringLiteral("Ügyféltámogatás");
    s.tagId = idOf(s.name);
    s.isNew = s.tagId.isEmpty();
    s.source = QStringLiteral("similar");
    s.reasons = {reason("title", {QStringLiteral("Ügyféltámogatás heti")})};
    s.similarMeetings = {meeting(QStringLiteral("m-support-0918"), QStringLiteral("Ügyféltámogatás heti"), at(2026, 9, 18), 40)};
    return {s};
}

void TagDemoBackend::loadMeetingDemo(const QString& meetingId, const QString& state)
{
    auto ids = [this](std::initializer_list<const char*> names) {
        QStringList out;
        for (const char* n : names) {
            const QString id = idOf(QString::fromUtf8(n));
            if (!id.isEmpty()) out << id;
        }
        return out;
    };
    auto sug = [this](const char* name, const char* source, QVector<TagReasonItem> reasons = {},
                      QVector<TagMeetingItem> similar = {}, bool isNew = false) {
        TagSuggestionItem s;
        s.name = QString::fromUtf8(name);
        s.tagId = isNew ? QString() : idOf(s.name);
        s.isNew = isNew || s.tagId.isEmpty();
        s.source = QString::fromLatin1(source);
        s.reasons = std::move(reasons);
        s.similarMeetings = std::move(similar);
        return s;
    };
    const auto nordvikWeekly = meeting(QStringLiteral("m-nordvik-0929"), QStringLiteral("Nordvik heti meeting"), at(2026, 9, 29), 55);
    const auto handover = meeting(QStringLiteral("m-handover-1002"), QStringLiteral("Ügyféltámogatás átadás"), at(2026, 10, 2), 52);
    const auto quarterly = meeting(QStringLiteral("m-quarterly-0702"), QStringLiteral("Negyedéves partnertalálkozó"), at(2026, 7, 2), 71);

    TagSuggestionState s;
    QStringList tags;
    if (state == QLatin1String("few")) {
        tags = ids({"Nordvik", "Partnerek"});
    } else if (state == QLatin1String("many")) {
        tags = ids({"Nordvik", "Partnerek", "Q4 tervezés", "Termék", "Ügyféltámogatás", "Belső", "MuseumPlus",
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
                       {reason("participant", {QStringLiteral("Varga Nóra"), QStringLiteral("Fehér Ádám")}),
                        reason("terms", {QStringLiteral("partnerportál"), QStringLiteral("ütemterv"), QStringLiteral("megújítás")})},
                       {nordvikWeekly, handover}),
                   sug("Q4 tervezés", "similar",
                       {reason("title", {QStringLiteral("Negyedéves partnertalálkozó (júl. 2.)")}),
                        reason("terms", {QStringLiteral("ütemterv"), QStringLiteral("költségkeret")})},
                       {quarterly})};
    } else if (state == QLatin1String("why")) {
        // T02: a „Miért?” panel tartalma.
        tags = ids({"Partnerek", "Q4 tervezés"});
        s.source = QStringLiteral("similar");
        s.items = {sug("Nordvik", "similar",
                       {reason("participant", {QStringLiteral("Varga Nóra"), QStringLiteral("Fehér Ádám")}),
                        reason("terms", {QStringLiteral("Nordvik"), QStringLiteral("partnerportál"), QStringLiteral("ütemterv")})},
                       {nordvikWeekly, handover}),
                   sug("Ügyféltámogatás", "similar",
                       {reason("title", {QStringLiteral("Negyedéves partnertalálkozó (júl. 2.)")}),
                        reason("terms", {QStringLiteral("támogatási igény"), QStringLiteral("súgóoldalak")})},
                       {quarterly})};
    } else if (state == QLatin1String("cooccur")) {
        tags = ids({"MuseumPlus"});
        s.source = QStringLiteral("cooccur");
        s.baseTagId = idOf(QStringLiteral("MuseumPlus"));
        s.items = {sug("MÉM-MDK", "cooccur"), sug("Gyűjteménykezelés", "cooccur")};
    } else if (state == QLatin1String("llm")) {
        tags = ids({"Nordvik"});
        s.source = QStringLiteral("llm");
        s.items = {sug("Termék", "llm", {reason("terms", {QStringLiteral("termékút"), QStringLiteral("kiadás")})}),
                   sug("Ügyfélsiker", "llm", {reason("terms", {QStringLiteral("támogatási jegyek"), QStringLiteral("elégedettség")})},
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
        p.participants = {{QStringLiteral("Kovács Anna"), 12}, {QStringLiteral("Szabó Bence"), 11},
                          {QStringLiteral("Lantos Réka"), 6}, {QStringLiteral("Varga Nóra"), 5}};
        p.terms = {QStringLiteral("Nordvik"), QStringLiteral("partnerportál"), QStringLiteral("ütemterv"),
                   QStringLiteral("SLA"), QStringLiteral("eszkaláció"), QStringLiteral("Qvarko-modul"),
                   QStringLiteral("PixelTár"), QStringLiteral("megújítás")};
        p.meetings = {meeting(QStringLiteral("m-handover-1002"), QStringLiteral("Ügyféltámogatás átadás"), at(2026, 10, 2), 52),
                      meeting(QStringLiteral("m-quarterly-1001"), QStringLiteral("Negyedéves partnertalálkozó"), at(2026, 10, 1), 76),
                      meeting(QStringLiteral("m-nordvik-0929"), QStringLiteral("Nordvik heti meeting"), at(2026, 9, 29), 55),
                      meeting(QStringLiteral("m-nordvik-0922"), QStringLiteral("Nordvik heti meeting – demó előtt"), at(2026, 9, 22), 48)};
        return p;
    }
    // A többi címke profilja sablonból: a résztvevők és a kifejezések arányosan.
    const QStringList people{QStringLiteral("Kovács Anna"), QStringLiteral("Szabó Bence"),
                             QStringLiteral("Lantos Réka"), QStringLiteral("Varga Nóra")};
    const double share[] = {0.85, 0.6, 0.4, 0.25};
    for (int k = 0; k < people.size(); ++k) {
        const int n = int(t.meetingCount * share[(k + i) % 4] + 0.5);
        if (n >= 1) p.participants.push_back({people[k], n});
    }
    std::sort(p.participants.begin(), p.participants.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    p.terms = {t.name, QStringLiteral("ütemterv"), QStringLiteral("egyeztetés"), QStringLiteral("határidő")};
    const QVector<QPair<QString, int>> pool{{QStringLiteral("Termékcsapat heti egyeztetés"), 30},
                                            {QStringLiteral("Heti tervezés"), 33},
                                            {QStringLiteral("Termékbemutató, 2. kör"), 44},
                                            {QStringLiteral("Infrastruktúra áttekintés"), 41},
                                            {QStringLiteral("Árazás egyeztetés"), 27}};
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
    emit undoChanged();
}

} // namespace tanara_qml
