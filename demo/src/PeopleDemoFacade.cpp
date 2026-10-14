#include "demo/PeopleDemoFacade.h"

#include "demo/DemoText.h"

#include <QDate>
#include <QTime>

#include <iterator>

using namespace tanara;
using namespace tanara::library;

namespace tanara_demo {

namespace {

// The tag UX states add the design plan's further fictional people.
bool tagState(const QString& state)
{
    return state == QLatin1String("tagsSection") || state == QLatin1String("tagsSuggested")
        || state == QLatin1String("tagsSelf") || state == QLatin1String("tagsFocus");
}

// The states showing the samples' voice-model labels.
bool modelState(const QString& state)
{
    return state == QLatin1String("samplesModels") || state == QLatin1String("backfill");
}

QDate day(const char* iso) { return QDate::fromString(QString::fromLatin1(iso), Qt::ISODate); }

// The sample's length "m:ss" -> milliseconds.
qint64 lengthMs(const char* clock)
{
    const QStringList parts = QString::fromLatin1(clock).split(QLatin1Char(':'));
    return (qint64(parts.value(0).toInt()) * 60 + parts.value(1).toInt()) * 1000;
}

struct SampleSpec { const char* kind; const char* hu; const char* en; const char* titleHu; const char* titleEn;
                    const char* date; const char* length; };
struct MeetingSpec { const char* titleHu; const char* titleEn; const char* date; int minutes; };

PersonId demoId(int n) { return PersonId::fromString(QStringLiteral("demo-person-%1").arg(n)); }

} // namespace

PeopleDemoFacade::PeopleDemoFacade(QObject* parent) : IPeopleFacade(parent) { load(); }

void PeopleDemoFacade::applyDemoState(const QString& state)
{
    if (state == m_state) return;
    m_state = state;
    load();   // the view model rebuilds itself after setting a state
}

void PeopleDemoFacade::load()
{
    m_entries.clear();
    struct Row { const char* name; int meetings; int samples; bool self; };
    static const Row rows[] = {
        {"Kovács Lilla", 41, 23, true}, {"Albert Noémi", 6, 3, false}, {"Antal Benedek", 2, 0, false},
        {"Bárány Gergely", 23, 9, false}, {"B. Gergő", 4, 2, false}, {"Balogh Réka", 11, 5, false},
        {"Csonka Ferenc", 3, 0, false}, {"Dénes Zsófia", 17, 6, false}, {"Erdei Márton", 8, 4, false},
        {"Fazekas Ilona", 5, 2, false}, {"Gál Bence", 12, 7, false}, {"Halász Dóra", 9, 3, false},
        {"Szilágyi Bálint", 14, 5, false}, {"Tóth Eszter", 3, 0, false},
    };
    static const Row tagRows[] = {
        {"Fehér Gábor", 16, 6, false}, {"Varga Árpád", 12, 4, false}, {"Szabó Bence", 8, 3, false},
        {"Molnár Eszter", 0, 0, false},
    };
    static const SampleSpec samplePool[] = {
        {"mic", "Trust USB mikrofon", "Trust USB microphone",
         "Termékcsapat heti egyeztetés – Q4 ütemterv és a mobilos kiadás kockázatai",
         "Product team weekly sync – Q4 roadmap and mobile release risks", "2026-10-01", "0:42"},
        {"call", "Hívás hangja · Teams", "Call audio · Teams",
         "Northwind átadás: ügyféltámogatási folyamatok, eszkalációs szintek és SLA",
         "Northwind handover: support processes, escalation levels and SLA", "2026-09-30", "1:15"},
        {"call", "Hívás hangja · Zoom", "Call audio · Zoom", "Partnerdemó – adatimport", "Partner demo – data import",
         "2026-09-24", "0:38"},
        {"unknown", "ismeretlen eszköz", "unknown device", "Régi felvétel importálva (hangfájl)",
         "Old recording imported (audio file)", "2026-08-28", "0:51"},
        {"mic", "Trust USB mikrofon", "Trust USB microphone", "Belső retró, szeptember", "Internal retro, September",
         "2026-08-18", "0:27"},
        {"call", "Hívás hangja · Teams", "Call audio · Teams", "Roadmap-áttekintés", "Roadmap review",
         "2026-08-11", "0:33"},
        {"mix", "A megbeszélés lekevert hangja", "Meeting mixdown", "Kiadási egyeztetés", "Release sync",
         "2026-08-04", "0:19"},
        {"mic", "Trust USB mikrofon", "Trust USB microphone", "Sprint tervezés", "Sprint planning",
         "2026-07-28", "0:46"},
        {"call", "Hívás hangja · Zoom", "Call audio · Zoom", "Hibajavítási kör", "Bug-fix round",
         "2026-07-21", "0:24"},
    };
    static const MeetingSpec meetingPool[] = {
        {"Termékcsapat heti egyeztetés – Q4 ütemterv és a mobilos kiadás kockázatai",
         "Product team weekly sync – Q4 roadmap and mobile release risks", "2026-10-01", 18},
        {"Northwind átadás: ügyféltámogatási folyamatok, eszkalációs szintek és SLA",
         "Northwind handover: support processes, escalation levels and SLA", "2026-09-30", 31},
        {"Partnerdemó – adatimport", "Partner demo – data import", "2026-09-24", 9},
        {"Belső retró, szeptember", "Internal retro, September", "2026-09-18", 12},
        {"Roadmap-áttekintés", "Roadmap review", "2026-09-11", 22},
        {"Kiadási egyeztetés", "Release sync", "2026-09-04", 7},
        {"Sprint tervezés", "Sprint planning", "2026-08-28", 15},
        {"Hibajavítási kör", "Bug-fix round", "2026-08-21", 5},
    };
    const int samplePoolSize = int(std::size(samplePool));
    const int meetingPoolSize = int(std::size(meetingPool));

    const bool empty = m_state == QLatin1String("P06");
    QVector<Row> all(std::begin(rows), std::end(rows));
    if (tagState(m_state)) all.append(QVector<Row>(std::begin(tagRows), std::end(tagRows)));

    int n = 0;
    for (const Row& row : std::as_const(all)) {
        if (empty && !row.self) continue;
        Entry e;
        PersonRecord& p = e.record;
        p.id = demoId(n);
        p.name = demoName(row.name);
        p.isSelf = row.self;
        p.sampleCount = row.samples;
        e.stats.meetingCount = row.meetings;
        e.stats.talkMs = qint64(row.meetings) * 14 * 60000;
        e.stats.lastSeen = QDateTime(QDate(2026, 10, 1).addDays(-(n * 3) % 40), QTime(10, 0));

        for (int i = 0; i < row.samples; ++i) {
            const SampleSpec& s = samplePool[i % samplePoolSize];
            VoiceSample v;
            v.id = QStringLiteral("demo-%1").arg(i);
            v.person = p.id;
            v.sourceKind = QString::fromLatin1(s.kind);
            v.deviceLabel = demoText(s.hu, s.en);
            v.meetingId = QStringLiteral("demo-meeting-%1").arg(i % meetingPoolSize);
            v.meetingTitle = demoText(s.titleHu, s.titleEn);
            v.recordedAt = day(s.date);
            v.startMs = 0;
            v.endMs = lengthMs(s.length);
            v.audioExists = true;
            e.samples.append(v);
        }
        for (int i = 0; i < row.meetings; ++i) {
            const MeetingSpec& m = meetingPool[i % meetingPoolSize];
            PersonMeetingStat s;
            s.meetingId = QStringLiteral("demo-meeting-%1").arg(i % meetingPoolSize);
            s.title = demoText(m.titleHu, m.titleEn);
            s.startedAt = QDateTime(day(m.date), QTime(10, 0));
            s.talkMs = qint64(m.minutes) * 60000;
            e.stats.meetings.append(s);
        }

        if (p.name == demoName("Bárány Gergely")) {
            p.aliases = {demoText("Gergely", "Gregory"), demoText("G. Bárány", "G. Barnes")};
            p.note = demoText("Northwind oldali projektvezető. Gyakran telefonról csatlakozik, ilyenkor halkabb.",
                              "Project lead on the Northwind side. Often joins by phone, and sounds quieter then.");
            e.stats.talkMs = (6 * 60 + 12) * 60000;
            e.stats.lastSeen = QDateTime(QDate(2026, 10, 1), QTime(10, 0));
        } else if (p.name == demoName("Tóth Eszter")) {
            p.aliases = {demoText("Eszti", "Hannie")};
            e.stats.talkMs = 22 * 60000;
            e.stats.lastSeen = QDateTime(QDate(2026, 9, 26), QTime(10, 0));
            e.stats.meetings.clear();
            const struct { const char* hu; const char* en; const char* date; int minutes; } own[] = {
                {"Pénzügyi zárás, szeptember", "Financial close, September", "2026-09-26", 8},
                {"Beszállítói egyeztetés – Contoso", "Vendor sync – Contoso", "2026-09-12", 11},
                {"Kickoff: új számlázó", "Kickoff: new billing system", "2026-09-03", 3},
            };
            for (const auto& m : own) {
                PersonMeetingStat s;
                s.title = demoText(m.hu, m.en);
                s.startedAt = QDateTime(day(m.date), QTime(10, 0));
                s.talkMs = qint64(m.minutes) * 60000;
                e.stats.meetings.append(s);
            }
        } else if (p.name == demoName("Gál Bence")) {
            p.aliases = {demoText("Gergő kollégája", "Greg's colleague")};
        }
        // Learned / manual track side ("Usually: ...").
        if (p.name == demoName("Bárány Gergely") || p.name == demoName("Fehér Gábor")) {
            p.defaultSide = QStringLiteral("remote");
        } else if (p.name == demoName("Varga Árpád") || p.name == demoName("Dénes Zsófia")) {
            p.defaultSide = QStringLiteral("local");
            p.defaultSideManual = true;
        }
        m_entries.append(e);
        ++n;
    }
}

const PeopleDemoFacade::Entry* PeopleDemoFacade::find(const PersonId& id) const
{
    for (const Entry& e : m_entries)
        if (e.record.id == id) return &e;
    return nullptr;
}

QVector<PersonRecord> PeopleDemoFacade::persons() const
{
    QVector<PersonRecord> out;
    out.reserve(m_entries.size());
    for (const Entry& e : m_entries) out.append(e.record);
    return out;
}

PersonStats PeopleDemoFacade::stats(const PersonId& person) const
{
    const Entry* e = find(person);
    return e ? e->stats : PersonStats();
}

QVector<VoiceSample> PeopleDemoFacade::samples(const PersonId& person) const
{
    const Entry* e = find(person);
    return e ? e->samples : QVector<VoiceSample>();
}

QStringList PeopleDemoFacade::activeVoiceModelIds() const
{
    if (!modelState(m_state)) return {};
    return {QStringLiteral("campplus"), QStringLiteral("eres2netv2")};
}

QStringList PeopleDemoFacade::voiceprintModels(const QString& sampleId) const
{
    // Every third sample exists only with the base model.
    const int index = sampleId.mid(sampleId.lastIndexOf(QLatin1Char('-')) + 1).toInt();
    return index % 3 == 2 ? QStringList{QStringLiteral("campplus")} : activeVoiceModelIds();
}

double PeopleDemoFacade::similarity(const PersonId& a, const PersonId& b) const
{
    const Entry* ea = find(a);
    const Entry* eb = find(b);
    if (ea && eb && ea->record.name == demoName("Bárány Gergely") && eb->record.name == demoName("B. Gergő"))
        return 0.88;
    return -1;
}

VoiceprintPlan PeopleDemoFacade::voiceprintPlan(const PersonId& person) const
{
    VoiceprintPlan plan;
    const Entry* e = find(person);
    const int meetings = e ? e->stats.meetingCount : 0;
    plan.modelAvailable = true;
    plan.meetingsWithLines = meetings;
    for (int i = 0; i < meetings; ++i) plan.usableMeetingIds << QStringLiteral("demo-meeting-%1").arg(i);
    plan.usableMs = 2 * 60000;   // "about 2 minutes of voice"
    return plan;
}

MergePreview PeopleDemoFacade::mergePreview(const PersonId& loser, const PersonId& survivor) const
{
    const Entry* a = find(loser);
    const Entry* b = find(survivor);
    MergePreview p;
    p.meetingCount = (a ? a->stats.meetingCount : 0) + (b ? b->stats.meetingCount : 0);
    p.sampleCount = (a ? a->record.sampleCount : 0) + (b ? b->record.sampleCount : 0);
    p.staleSummaries = 3;
    return p;
}

DeletePreview PeopleDemoFacade::deletePreview(const PersonId& person) const
{
    const Entry* e = find(person);
    DeletePreview p;
    p.meetingCount = e ? e->stats.meetingCount : 0;
    p.sampleCount = e ? e->record.sampleCount : 0;
    p.exampleLabel = demoRaw(QStringLiteral("Távoli 2"));
    return p;
}

bool PeopleDemoFacade::setDefaultSide(const PersonId& person, const QString& side)
{
    for (Entry& e : m_entries) {
        if (e.record.id != person) continue;
        e.record.defaultSide = side;
        e.record.defaultSideManual = !side.isEmpty();
        return true;
    }
    return false;
}

} // namespace tanara_demo
