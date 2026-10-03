#include "LibraryDemoData.h"

#include "tanara/library/TextFold.h"

#include <QHash>

#include <algorithm>

namespace tanara_qml::demo {

using tanara::StepState;

namespace {

struct Row {
    const char* id;
    const char* title;
    const char* date;       // ISO, helyi idő
    int minutes;
    int seconds;
    StepState transcript;
    StepState summary;
    StepState identify;
    bool stale;
    int speakers;
    int tracks;
    const char* people;     // vesszővel elválasztva
    const char* text;
};

// A design-renderek (M02 / M05) könyvtára + néhány régebbi elem a görgetéshez.
const Row kRows[] = {
    {"demo-heti", "Termékcsapat heti egyeztetés", "2026-10-02T15:00:00", 30, 34,
     StepState::Done, StepState::Done, StepState::None, false, 4, 3,
     "Kovács Lilla,Tóth Bence,Varga Nóra",
     "A bevezetés csúszik egy hetet, ezért jeleznünk kell, hogy csúszik a demó? Szerintem igen, még ma."},
    {"demo-tamogatas", "Ügyféltámogatás átadás", "2026-10-02T11:00:00", 52, 10,
     StepState::None, StepState::None, StepState::None, false, 0, 3,
     "Kovács Lilla", ""},
    {"demo-partner", "Negyedéves partnertalálkozó", "2026-10-01T09:30:00", 76, 4,
     StepState::Done, StepState::Done, StepState::Done, true, 6, 4,
     "Kovács Lilla,Tóth Bence,Varga Nóra,Molnár Eszter,Szabó Áron",
     "Az összesítőt a pénteki demó után küldjük ki a partnereknek, addigra a számok is véglegesek."},
    {"demo-bemutato", "Termékbemutató, 2. kör", "2026-09-30T15:00:00", 44, 2,
     StepState::Failed, StepState::None, StepState::None, false, 0, 2,
     "Varga Nóra",
     "Azt javaslom, hogy a demó előtt még egyszer végigmegyünk a teljes forgatókönyvön."},
    {"demo-arazas", "Árazás egyeztetés", "2026-09-29T15:00:00", 27, 41,
     StepState::Done, StepState::Done, StepState::Done, false, 3, 2,
     "Kovács Lilla,Szabó Áron", "Az éves csomag árát a következő negyedévtől emeljük."},
    {"demo-belepo", "Belépő kolléga bevezetése", "2026-09-29T10:30:00", 55, 12,
     StepState::Done, StepState::None, StepState::None, false, 2, 2,
     "Tóth Bence", "Az első héten a hozzáféréseket és a fejlesztői környezetet állítjuk be."},
    {"demo-infra", "Infrastruktúra áttekintés", "2026-09-25T10:00:00", 41, 27,
     StepState::Done, StepState::Done, StepState::None, false, 5, 3,
     "Molnár Eszter", "A mentések visszaállítását negyedévente ki kell próbálni."},
    {"demo-tervezes", "Heti tervezés", "2026-09-24T09:00:00", 33, 5,
     StepState::Done, StepState::Done, StepState::Done, false, 4, 2,
     "Kovács Lilla,Tóth Bence", "A sprint célja a keresés gyorsítása és a hibalista kiürítése."},
    {"demo-ugyfel", "Ügyfélinterjú: kisvállalati csomag", "2026-09-22T13:00:00", 38, 49,
     StepState::Done, StepState::Done, StepState::None, false, 2, 2,
     "Varga Nóra", "A számlázás exportja a legfontosabb kérés, minden más ráér."},
    {"demo-retro", "Retrospektív", "2026-09-18T15:00:00", 47, 20,
     StepState::Done, StepState::None, StepState::Done, false, 5, 3,
     "Kovács Lilla,Tóth Bence,Szabó Áron", "A kiadás előtti tesztelésre több időt kell hagyni."},
    {"demo-vezetoi", "Vezetői egyeztetés", "2026-09-15T08:30:00", 62, 0,
     StepState::Done, StepState::Done, StepState::Done, false, 3, 2,
     "Molnár Eszter,Szabó Áron", "A létszámterv a költségvetéssel együtt kerül a testület elé."},
    {"demo-ux", "UX-áttekintés: beállítások", "2026-09-11T10:00:00", 25, 30,
     StepState::Done, StepState::Done, StepState::None, false, 3, 2,
     "Varga Nóra,Tóth Bence", "A beállításokat három csoportba rendezzük, a ritkák lejjebb kerülnek."},
};

QVector<DemoMeeting> build()
{
    QVector<DemoMeeting> out;
    const QDate today = now().date();
    for (const Row& r : kRows) {
        DemoMeeting d;
        tanara::LibraryEntry& e = d.entry;
        e.id = QString::fromLatin1(r.id);
        e.title = QString::fromUtf8(r.title);
        e.startedAt = QDateTime::fromString(QString::fromLatin1(r.date), Qt::ISODate);
        e.durationMs = (qint64(r.minutes) * 60 + r.seconds) * 1000;
        e.hasTranscript = r.transcript == StepState::Done;
        e.hasSummary = r.summary == StepState::Done;
        e.section = tanara::MeetingLibrary::sectionFor(e.startedAt, today);
        e.participants = QString::fromUtf8(r.people).split(QLatin1Char(','), Qt::SkipEmptyParts);
        e.state.meetingId = e.id;
        e.state.transcriptState = r.transcript;
        e.state.summaryState = r.summary;
        e.state.identifyState = r.identify;
        e.state.summaryStale = r.stale;
        e.state.staleCorrectedSpeakers = r.stale ? 3 : 0;
        if (r.transcript == StepState::Failed) {
            e.state.transcriptError.kind = tanara::JobKind::Transcribe;
            e.state.transcriptError.message = QStringLiteral(
                "A szolgáltató elutasította a kérést: az API-kulcs érvénytelen vagy lejárt.");
            e.state.transcriptError.detail = QStringLiteral("HTTP 401 · invalid_api_key");
            e.state.transcriptError.fixActionHint = QStringLiteral("settings:stt");
            e.state.transcriptError.when = e.startedAt.addSecs(102 * 60);
        }
        d.text = QString::fromUtf8(r.text);
        d.speakers = r.speakers;
        d.tracks = r.tracks;
        out.append(d);
    }
    std::sort(out.begin(), out.end(), [](const DemoMeeting& a, const DemoMeeting& b) {
        return a.entry.startedAt > b.entry.startedAt;
    });
    return out;
}

} // namespace

QDateTime now()
{
    return QDateTime(QDate(2026, 10, 2), QTime(17, 30));
}

const QVector<DemoMeeting>& meetings()
{
    static const QVector<DemoMeeting> all = build();
    return all;
}

const DemoMeeting* find(const QString& meetingId)
{
    for (const DemoMeeting& d : meetings())
        if (d.entry.id == meetingId)
            return &d;
    return nullptr;
}

tanara::LibraryResult query(const tanara::LibraryQuery& q)
{
    namespace tf = tanara::textfold;
    tanara::LibraryResult res;
    res.totalMeetings = int(meetings().size());
    const QString needle = tf::foldQuery(q.text);
    for (const DemoMeeting& d : meetings()) {
        tanara::LibraryEntry e = d.entry;
        if (q.noTranscript && e.hasTranscript) continue;
        if (q.noSummary && e.hasSummary) continue;
        bool peopleOk = true;
        for (const QString& p : q.people)
            if (!e.participants.contains(p)) { peopleOk = false; break; }
        if (!peopleOk) continue;
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
            if (!e.titleMatch.isValid() && !inText.isValid()) continue;
        }
        res.entries.append(e);
    }
    return res;
}

QVector<tanara::PersonPresence> people()
{
    QHash<QString, int> counts;
    QStringList order;
    for (const DemoMeeting& d : meetings())
        for (const QString& p : d.entry.participants) {
            if (!counts.contains(p)) order << p;
            ++counts[p];
        }
    QVector<tanara::PersonPresence> out;
    for (const QString& p : std::as_const(order))
        out.append({p, counts.value(p)});
    std::stable_sort(out.begin(), out.end(), [](const tanara::PersonPresence& a,
                                                const tanara::PersonPresence& b) {
        return a.meetingCount > b.meetingCount;
    });
    return out;
}

QVector<tanara::PendingItem> pendingItems()
{
    QVector<tanara::PendingItem> out;
    for (const DemoMeeting& d : meetings()) {
        const tanara::LibraryEntry& e = d.entry;
        tanara::PendingItem it;
        it.meetingId = e.id;
        it.title = e.title;
        it.startedAt = e.startedAt;
        it.durationMs = e.durationMs;
        if (e.hasTranscript) {
            if (!e.state.summaryStale) continue;
            it.kind = tanara::PendingKind::StaleSummary;
            it.correctedSpeakers = e.state.staleCorrectedSpeakers;
        } else if (e.state.transcriptState == StepState::Failed) {
            it.kind = tanara::PendingKind::TranscriptionFailed;
            it.error = e.state.transcriptError;
        }
        out.append(it);
    }
    return out;
}

QString defaultMeetingId() { return QStringLiteral("demo-partner"); }
QString failedMeetingId() { return QStringLiteral("demo-bemutato"); }

} // namespace tanara_qml::demo
