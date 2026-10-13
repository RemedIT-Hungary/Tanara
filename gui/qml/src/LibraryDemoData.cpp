#include "LibraryDemoData.h"

#include "DemoText.h"

#include "tanara/foundation/text/Text.h"
#include "tanara/library/tags/TagNames.h"

#include <QHash>

#include <algorithm>

namespace tanara_qml::demo {

using tanara::StepState;

namespace {

struct Row {
    const char* id;
    const char* title;
    const char* date;       // ISO, local time
    int minutes;
    int seconds;
    StepState transcript;
    StepState summary;
    StepState identify;
    bool stale;
    int speakers;
    int tracks;
    const char* people;     // comma-separated
    const char* text;
    const char* tags;       // tag ids comma-separated (from the fictional pool, see kTags)
    const char* titleEn;    // for the English demo language (the names come from DemoText's name pairs)
    const char* textEn;
};

// The fictional tag pool (the same ids and names as in TagDemoBackend). The third column is
// the English demo language's name.
const char* const kTags[][3] = {
    {"t-nordvik", "Nordvik", "Nordvik"}, {"t-q4", "Q4 tervezés", "Q4 planning"},
    {"t-termek", "Termék", "Product"}, {"t-partnerek", "Partnerek", "Partners"},
    {"t-belso", "Belső", "Internal"}, {"t-muzeo", "Muzeo", "Muzeo"},
    {"t-mem", "MÉM-MDK", "Heritage Lab"}, {"t-gyujtemeny", "Gyűjteménykezelés", "Collections"},
    {"t-ugyfel", "Ügyféltámogatás", "Customer support"}, {"t-palyazat", "Múzeumi pályázat", "Museum grant"},
    {"t-nordvikas", "Nordvik AS", "Nordvik AS"}, {"t-kutatas", "Kutatás", "Research"},
};

// The library of the design renders + a few older items for scrolling.
const Row kRows[] = {
    {"demo-heti", "Termékcsapat heti egyeztetés", "2026-10-02T15:00:00", 30, 34,
     StepState::Done, StepState::Done, StepState::None, false, 4, 3,
     "Kovács Lilla,Tóth Bence,Varga Nóra",
     "A bevezetés csúszik egy hetet, ezért jeleznünk kell, hogy csúszik a demó? Szerintem igen, még ma.",
     "t-termek,t-q4",
     "Product team weekly sync",
     "The launch is slipping a week, so should we tell them the demo is slipping too? I'd say yes, today."},
    {"demo-tamogatas", "Ügyféltámogatás átadás", "2026-10-02T11:00:00", 52, 10,
     StepState::None, StepState::None, StepState::None, false, 0, 3,
     "Kovács Lilla", "",
     "t-nordvik",
     "Customer support handover",
     ""},
    {"demo-partner", "Negyedéves partnertalálkozó", "2026-10-01T09:30:00", 76, 4,
     StepState::Done, StepState::Done, StepState::Done, true, 6, 4,
     "Kovács Lilla,Tóth Bence,Varga Nóra,Molnár Eszter,Szabó Áron",
     "Az összesítőt a pénteki demó után küldjük ki a partnereknek, addigra a számok is véglegesek.",
     "t-nordvik,t-partnerek,t-q4",
     "Quarterly partner review",
     "We'll send the summary to the partners after Friday's demo, by then the numbers will be final too."},
    {"demo-bemutato", "Termékbemutató, 2. kör", "2026-09-30T15:00:00", 44, 2,
     StepState::Failed, StepState::None, StepState::None, false, 0, 2,
     "Varga Nóra",
     "Azt javaslom, hogy a demó előtt még egyszer végigmegyünk a teljes forgatókönyvön.",
     "t-muzeo,t-mem,t-gyujtemeny",
     "Product demo, round 2",
     "I suggest we go through the whole script once more before the demo."},
    {"demo-arazas", "Árazás egyeztetés", "2026-09-29T15:00:00", 27, 41,
     StepState::Done, StepState::Done, StepState::Done, false, 3, 2,
     "Kovács Lilla,Szabó Áron",
     "Az éves csomag árát a Nordvik-szerződés megújítása előtt még egyeztetjük.",
     "",
     "Pricing discussion",
     "We'll agree on the annual plan price before the Nordvik contract renewal."},
    {"demo-belepo", "Nordvik heti meeting", "2026-09-29T10:30:00", 55, 12,
     StepState::Done, StepState::None, StepState::None, false, 2, 2,
     "Tóth Bence", "Az ütemtervet a jövő héten véglegesítjük a skandináv csapattal.",
     "t-nordvik",
     "Nordvik weekly meeting",
     "We'll finalize the roadmap with the Scandinavian team next week."},
    {"demo-infra", "Infrastruktúra áttekintés", "2026-09-25T10:00:00", 41, 27,
     StepState::Done, StepState::Done, StepState::None, false, 5, 3,
     "Molnár Eszter", "A mentések visszaállítását negyedévente ki kell próbálni.",
     "t-belso",
     "Infrastructure review",
     "Restoring from backups has to be tested every quarter."},
    {"demo-tervezes", "Heti tervezés", "2026-09-24T09:00:00", 33, 5,
     StepState::Done, StepState::Done, StepState::Done, false, 4, 2,
     "Kovács Lilla,Tóth Bence", "A sprint célja a keresés gyorsítása és a hibalista kiürítése.",
     "t-belso,t-q4",
     "Weekly planning",
     "The sprint goal is faster search and an empty bug list."},
    {"demo-ugyfel", "Ügyfélinterjú: kisvállalati csomag", "2026-09-22T13:00:00", 38, 49,
     StepState::Done, StepState::Done, StepState::None, false, 2, 2,
     "Varga Nóra", "A számlázás exportja a legfontosabb kérés, minden más ráér.",
     "t-ugyfel",
     "Customer interview: small business plan",
     "Billing export is the most important request, everything else can wait."},
    {"demo-retro", "Retrospektív", "2026-09-18T15:00:00", 47, 20,
     StepState::Done, StepState::None, StepState::Done, false, 5, 3,
     "Kovács Lilla,Tóth Bence,Szabó Áron", "A kiadás előtti tesztelésre több időt kell hagyni.",
     "t-belso",
     "Retrospective",
     "We need to leave more time for testing before the release."},
    {"demo-vezetoi", "Vezetői egyeztetés", "2026-09-15T08:30:00", 62, 0,
     StepState::Done, StepState::Done, StepState::Done, false, 3, 2,
     "Molnár Eszter,Szabó Áron", "A létszámterv a költségvetéssel együtt kerül a testület elé.",
     "",
     "Leadership sync",
     "The headcount plan goes to the board together with the budget."},
    {"demo-ux", "UX-áttekintés: beállítások", "2026-09-11T10:00:00", 25, 30,
     StepState::Done, StepState::Done, StepState::None, false, 3, 2,
     "Varga Nóra,Tóth Bence", "A beállításokat három csoportba rendezzük, a ritkák lejjebb kerülnek.",
     "t-termek",
     "UX review: settings",
     "We'll sort the settings into three groups and move the rarely used ones further down."},
};

QVector<DemoMeeting> build(bool en)
{
    QVector<DemoMeeting> out;
    const QDate today = now().date();
    for (const Row& r : kRows) {
        DemoMeeting d;
        tanara::library::LibraryEntry& e = d.entry;
        e.id = QString::fromLatin1(r.id);
        e.title = QString::fromUtf8(en ? r.titleEn : r.title);
        e.startedAt = QDateTime::fromString(QString::fromLatin1(r.date), Qt::ISODate);
        e.durationMs = (qint64(r.minutes) * 60 + r.seconds) * 1000;
        e.hasTranscript = r.transcript == StepState::Done;
        e.hasSummary = r.summary == StepState::Done;
        e.section = tanara::library::MeetingLibrary::sectionFor(e.startedAt, today);
        e.participants = demotext::names(QString::fromUtf8(r.people).split(QLatin1Char(','), Qt::SkipEmptyParts));
        e.state.meetingId = e.id;
        e.state.transcriptState = r.transcript;
        e.state.summaryState = r.summary;
        e.state.identifyState = r.identify;
        e.state.summaryStale = r.stale;
        e.state.staleCorrectedSpeakers = r.stale ? 3 : 0;
        if (r.transcript == StepState::Failed) {
            e.state.transcriptError.kind = tanara::JobKind::Transcribe;
            e.state.transcriptError.message = demoText(
                "A szolgáltató elutasította a kérést: az API-kulcs érvénytelen vagy lejárt.",
                "The provider rejected the request: the API key is invalid or has expired.");
            e.state.transcriptError.detail = QStringLiteral("HTTP 401 · invalid_api_key");
            e.state.transcriptError.fixActionHint = QStringLiteral("settings:stt");
            e.state.transcriptError.when = e.startedAt.addSecs(102 * 60);
        }
        d.text = QString::fromUtf8(en ? r.textEn : r.text);
        d.speakers = r.speakers;
        d.tracks = r.tracks;
        out.append(d);
    }
    std::sort(out.begin(), out.end(), [](const DemoMeeting& a, const DemoMeeting& b) {
        return a.entry.startedAt > b.entry.startedAt;
    });
    return out;
}

// The demo's tag state (from the initial kRows; bulk tagging modifies it).
struct TagState {
    QVector<DemoTag> catalog;
    QHash<QString, QStringList> assigned;
    int created = 0;
    QString language;   // the demo language the names were made in
};

TagState initialTags()
{
    TagState st;
    st.language = demotext::language();
    for (const auto& t : kTags)
        st.catalog.append({QString::fromLatin1(t[0]), QString::fromUtf8(demotext::english() ? t[2] : t[1])});
    for (const Row& r : kRows)
        st.assigned.insert(QString::fromLatin1(r.id),
                           QString::fromLatin1(r.tags).split(QLatin1Char(','), Qt::SkipEmptyParts));
    return st;
}

TagState& tagState()
{
    static TagState st = initialTags();
    if (st.language != demotext::language()) st = initialTags();   // language switch for tests
    return st;
}

void fillTags(tanara::library::LibraryEntry& e)
{
    e.tagIds.clear();
    e.tagNames.clear();
    for (const QString& id : tagState().assigned.value(e.id)) {
        const QString name = tagName(id);
        if (name.isEmpty()) continue;
        e.tagIds << id;
        e.tagNames << name;
    }
}

} // namespace

QDateTime now()
{
    return QDateTime(QDate(2026, 10, 2), QTime(17, 30));
}

const QVector<DemoMeeting>& meetings()
{
    // Per language (tests may switch the demo language at runtime).
    static const QVector<DemoMeeting> hu = build(false);
    if (!demotext::english()) return hu;
    static const QVector<DemoMeeting> en = build(true);
    return en;
}

const DemoMeeting* find(const QString& meetingId)
{
    for (const DemoMeeting& d : meetings())
        if (d.entry.id == meetingId)
            return &d;
    return nullptr;
}

tanara::library::LibraryResult query(const tanara::library::LibraryQuery& q)
{
    namespace tf = tanara::text;
    tanara::library::LibraryResult res;
    res.totalMeetings = int(meetings().size());
    const QString needle = tf::foldQuery(q.text);
    for (const DemoMeeting& d : meetings()) {
        tanara::library::LibraryEntry e = d.entry;
        fillTags(e);
        if (q.noTranscript && e.hasTranscript) continue;
        if (q.noSummary && e.hasSummary) continue;
        bool peopleOk = true;
        for (const QString& p : q.people)
            if (!e.participants.contains(p)) { peopleOk = false; break; }
        if (!peopleOk) continue;
        if (!q.tags.isEmpty() || q.untagged) {
            bool ok = q.untagged && e.tagIds.isEmpty();
            if (!ok && !q.tags.isEmpty()) {
                // All (AND) or any (OR) of the requested tags.
                int have = 0;
                for (const QString& id : q.tags)
                    if (e.tagIds.contains(id)) ++have;
                ok = q.tagsAll ? have == q.tags.size() : have > 0;
            }
            if (!ok) continue;
        }
        if (!needle.isEmpty()) {
            const QString title = tf::normalize(e.title);
            e.titleMatch = tf::find(tf::fold(title), needle);
            const QString text = tf::normalize(d.text);
            const tf::Range inText = tf::find(tf::fold(text), needle);
            if (inText.isValid()) {
                e.snippet = tf::snippet(text, inText, &e.snippetMatch);
                e.snippetMs = 12 * 60 * 1000 + 52 * 1000;
                e.snippetSpeaker = e.participants.value(0);
            }
            for (const QString& name : std::as_const(e.tagNames))
                if (tf::find(tf::fold(tf::normalize(name)), needle).isValid()) {
                    e.tagMatch = true;
                    e.tagMatchName = name;
                    break;
                }
            if (!e.titleMatch.isValid() && !inText.isValid() && !e.tagMatch) continue;
        }
        res.entries.append(e);
    }
    return res;
}

QVector<tanara::library::PersonPresence> people()
{
    QHash<QString, int> counts;
    QStringList order;
    for (const DemoMeeting& d : meetings())
        for (const QString& p : d.entry.participants) {
            if (!counts.contains(p)) order << p;
            ++counts[p];
        }
    QVector<tanara::library::PersonPresence> out;
    for (const QString& p : std::as_const(order))
        out.append({p, counts.value(p)});
    std::stable_sort(out.begin(), out.end(), [](const tanara::library::PersonPresence& a,
                                                const tanara::library::PersonPresence& b) {
        return a.meetingCount > b.meetingCount;
    });
    return out;
}

QVector<tanara::library::PendingItem> pendingItems()
{
    QVector<tanara::library::PendingItem> out;
    for (const DemoMeeting& d : meetings()) {
        const tanara::library::LibraryEntry& e = d.entry;
        tanara::library::PendingItem it;
        it.meetingId = e.id;
        it.title = e.title;
        it.startedAt = e.startedAt;
        it.durationMs = e.durationMs;
        if (e.hasTranscript) {
            if (!e.state.summaryStale) continue;
            it.kind = tanara::library::PendingKind::StaleSummary;
            it.correctedSpeakers = e.state.staleCorrectedSpeakers;
        } else if (e.state.transcriptState == StepState::Failed) {
            it.kind = tanara::library::PendingKind::TranscriptionFailed;
            it.error = e.state.transcriptError;
        }
        out.append(it);
    }
    return out;
}

QVector<DemoTag> tagCatalog()
{
    return tagState().catalog;
}

QString tagName(const QString& tagId)
{
    for (const DemoTag& t : std::as_const(tagState().catalog))
        if (t.id == tagId) return t.name;
    return {};
}

QString tagIdByName(const QString& name)
{
    const QString key = tanara::library::tagKey(name);
    if (key.isEmpty()) return {};
    for (const DemoTag& t : std::as_const(tagState().catalog))
        if (tanara::library::tagKey(t.name) == key) return t.id;
    return {};
}

QString createTag(const QString& name)
{
    const QString display = tanara::library::normalizeTagName(name);
    if (display.isEmpty()) return {};
    if (const QString id = tagIdByName(display); !id.isEmpty()) return id;
    TagState& st = tagState();
    const QString id = QStringLiteral("t-new-%1").arg(++st.created);
    st.catalog.append({id, display});
    emit tagNotifier()->changed();
    return id;
}

QStringList tagsOf(const QString& meetingId)
{
    return tagState().assigned.value(meetingId);
}

void setTagsOf(const QString& meetingId, const QStringList& tagIds)
{
    TagState& st = tagState();
    if (st.assigned.value(meetingId) == tagIds) return;
    st.assigned.insert(meetingId, tagIds);
    emit tagNotifier()->changed();
}

void resetTags()
{
    tagState() = initialTags();
    emit tagNotifier()->changed();
}

QVector<tanara::TagUsage> tagOptions()
{
    QVector<tanara::TagUsage> out;
    for (const DemoTag& t : std::as_const(tagState().catalog)) {
        tanara::TagUsage u;
        u.tag.id = t.id;
        u.tag.name = t.name;
        for (const DemoMeeting& d : meetings())
            if (tagState().assigned.value(d.entry.id).contains(t.id)) {
                ++u.meetingCount;
                if (!u.firstUsedAt.isValid() || d.entry.startedAt < u.firstUsedAt) u.firstUsedAt = d.entry.startedAt;
                if (!u.lastUsedAt.isValid() || d.entry.startedAt > u.lastUsedAt) u.lastUsedAt = d.entry.startedAt;
            }
        out.append(u);
    }
    std::stable_sort(out.begin(), out.end(), [](const tanara::TagUsage& a, const tanara::TagUsage& b) {
        if (a.meetingCount != b.meetingCount) return a.meetingCount > b.meetingCount;
        return a.tag.name.localeAwareCompare(b.tag.name) < 0;
    });
    return out;
}

int untaggedCount()
{
    int n = 0;
    for (const DemoMeeting& d : meetings()) {
        tanara::library::LibraryEntry e = d.entry;
        fillTags(e);
        if (e.tagIds.isEmpty()) ++n;
    }
    return n;
}

tanara::library::LibraryEntry entry(const QString& meetingId)
{
    const DemoMeeting* d = find(meetingId);
    if (!d) return {};
    tanara::library::LibraryEntry e = d->entry;
    fillTags(e);
    return e;
}

DemoTagNotifier* tagNotifier()
{
    static DemoTagNotifier* n = new DemoTagNotifier;
    return n;
}

QString defaultMeetingId() { return QStringLiteral("demo-partner"); }
QString failedMeetingId() { return QStringLiteral("demo-bemutato"); }

} // namespace tanara_qml::demo
