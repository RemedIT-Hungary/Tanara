// Tests for the People window's backend: people.json v3's records (PersonId -> name, nicknames,
// note), an old-format file staying untouched (read-only until `migrate person-ids`), nickname search
// in the person picker, statistics (meeting count, talk time, last seen), rename (ONE field) / merge
// (undoable) / delete / sample move / new person from a sample with their numbers, marking summaries
// stale, undo, and two PROCESSES writing at the same time.
//
// Everything runs in a temporary TANARA_HOME with made-up names; it touches no real data,
// model or ffmpeg (the "voice" is a fake embedder).
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "tanara/app/AppController.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/editing/session/PeopleDirectory.h"
#include "tanara/editing/session/SpeakerEditSession.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/editing/review/SideAnalysis.h"
#include "tanara/library/people/PeopleService.h"
#include "tanara/library/people/PeopleStats.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/VoiceprintStore.h"

#include <memory>
#include <vector>

using namespace tanara;

namespace {

const QString kSelf = QStringLiteral("Kovács Lilla");
const QString kGergely = QStringLiteral("Bárány Gergely");
const QString kGergo = QStringLiteral("B. Gergő");
const QString kEszter = QStringLiteral("Tóth Eszter");

QByteArray readFile(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

QJsonObject readJson(const QString& path)
{
    return QJsonDocument::fromJson(readFile(path)).object();
}

struct Line { qint64 startMs; qint64 durMs; QString raw; };

class FakeEmbedder : public processing::IUtteranceEmbedder {
public:
    bool open(const QString&) override { return true; }
    QVector<float> embed(qint64, qint64) override { return {1.0f, 0.0f, 0.0f}; }
};

Voiceprint print(const QString& id, const QVector<float>& e, const QString& meetingId = QString())
{
    Voiceprint p;
    p.id = id;
    p.embedding = e;
    p.sourceMeetingId = meetingId;
    p.sourceTrack = QStringLiteral("mixdown");
    p.sampleRef = QStringLiteral("mixdown.mp3#1000-9000");
    p.createdAt = QStringLiteral("2026-09-30T10:00:00");
    return p;
}

} // namespace

class PeopleServiceTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void recordRoundTripKeepsUnknownFields();
    void corruptPeopleFileIsSetAside();
    void oldFormatFileIsReadOnly();
    void oldFormatUnderTheAppIsNotTouched();
    void sameNameTwoPeople();
    void aliasSearchInPersonPicker();
    void statsCountMeetingsTalkTimeAndLastSeen();
    void statsRefreshInBackgroundAndCache();
    void renameIsOneFieldKeepsOldNameAsAlias();
    void renameToExistingNameIsRefused();
    void selfRenameChangesSettingAndSelfCannotBeDeleted();
    void aliasAddRemoveUndo();
    void noteIsStored();
    void learnedDefaultSide();
    void sampleRemoveAndUndo();
    void sampleMoveMarksStaleAndUndoRestores();
    void newPersonFromSampleAndUndoRemovesPerson();
    void similarityFromStoredEmbeddings();
    void mergeNumbersAliasAndStale();
    void mergeWithOpenEditorShowsStale();
    void deleteNumbersAndStale();
    void deleteKeepingSamplesAsAnonymous();
    void voiceprintFromManuallyAssignedMeetings();
    void sampleSourceDescription();
    void twoProcessesWriteConcurrently();

private:
    QString metaFile(const QString& name) const { return QDir(m_app->store()->metadataDir()).filePath(name); }
    // A person by name (people.json; created if missing).
    PersonId pid(const QString& name) { return m_app->people()->ensure(name); }
    // The person with this name, or null.
    PersonId find(const QString& name) const { m_app->people()->refresh(); return m_app->people()->idForName(name); }
    bool exists(const QString& name) const { return !find(name).isNull(); }
    // A transcribed meeting: lines with raw labels, speakerMap (raw -> NAME), optionally with a summary.
    Meeting addMeeting(const QString& title, const QDateTime& when, const QVector<Line>& lines,
                       const QMap<QString, QString>& speakerMap, bool hasSummary);
    library::PeopleService* svc() const { return m_app->peopleService(); }

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<app::AppController> m_app;
};

void PeopleServiceTest::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    qputenv("TANARA_HOME", m_home->path().toUtf8());
    m_app = std::make_unique<app::AppController>();
    QVERIFY(m_app->settings()->paths().primaryRecordingRoot().startsWith(m_home->path()));
    m_app->setUserSpeakerName(kSelf);
}

void PeopleServiceTest::cleanup()
{
    m_app.reset();
    m_home.reset();
}

Meeting PeopleServiceTest::addMeeting(const QString& title, const QDateTime& when,
                                      const QVector<Line>& lines,
                                      const QMap<QString, QString>& speakerMap, bool hasSummary)
{
    Meeting m = m_app->store()->createMeeting(title);
    QJsonArray segs;
    for (const Line& l : lines) {
        QJsonObject o;
        o["startMs"] = double(l.startMs);
        o["endMs"] = double(l.startMs + l.durMs);
        o["speaker"] = l.raw;
        o["text"] = QStringLiteral("szöveg");
        segs.append(o);
    }
    writeFile(QDir(m.folder).filePath("transcript.segments.json"), QJsonDocument(segs).toJson());
    m.startedAt = when;
    m.hasTranscript = true;
    m.hasSummary = hasSummary;
    for (auto it = speakerMap.constBegin(); it != speakerMap.constEnd(); ++it) m.speakerMap.insert(it.key(), pid(it.value()));
    m.mixdownFile = QStringLiteral("mixdown.mp3");
    m_app->store()->saveMeeting(m);
    return m_app->store()->load(m.id);
}

// ---- PeopleStore v3: records ----

void PeopleServiceTest::recordRoundTripKeepsUnknownFields()
{
    const QString path = m_home->filePath("d/people.json");
    // A FUTURE build's fields (in the root and on a person) must not be lost.
    writeFile(path, R"({"version":3,"futureRoot":7,"self":"p-e","people":{
        "p-g":{"name":"Bárány Gergely","aliases":["Gergely"],"note":"PM","color":"blue"},
        "p-e":{"name":"Tóth Eszter","aliases":[],"note":"","defaultSide":"local"}}})");
    const PersonId g = PersonId::fromString("p-g"), e = PersonId::fromString("p-e");
    {
        library::PeopleStore store(path);
        QVERIFY(!store.legacy());
        QCOMPARE(store.ids(), PersonIds({g, e}));                   // sorted by name
        QCOMPARE(store.selfId(), e);
        QCOMPARE(store.displayName(g), kGergely);
        QCOMPARE(store.aliases(g), QStringList{"Gergely"});
        QCOMPARE(store.person(g).note, QStringLiteral("PM"));
        QCOMPARE(store.defaultSide(e), QStringLiteral("local"));
        QCOMPARE(store.idForName("bárány gergely"), g);             // name lookup is case-insensitive
        QVERIFY(store.addAlias(g, "G. Bárány"));
        QVERIFY(!store.addAlias(g, "gergely"));                     // already listed
        QVERIFY(!store.addAlias(g, kGergely));                      // its own name is not a nickname
        QVERIFY(store.setNote(e, "Pénzügy"));
    }
    library::PeopleStore fresh(path);
    QCOMPARE(fresh.aliases(g), QStringList({"Gergely", "G. Bárány"}));
    QCOMPARE(fresh.person(e).note, QStringLiteral("Pénzügy"));
    QJsonObject root = readJson(path);
    QCOMPARE(root.value("version").toInt(), 3);
    QCOMPARE(root.value("futureRoot").toInt(), 7);
    QCOMPARE(root.value("self").toString(), QStringLiteral("p-e"));
    QCOMPARE(root.value("people").toObject().value("p-g").toObject().value("color").toString(), QStringLiteral("blue"));

    // Rename = ONE field: the id stays, a nickname equal to the new name drops.
    QVERIFY(fresh.rename(g, "Gergely"));
    QCOMPARE(fresh.displayName(g), QStringLiteral("Gergely"));
    QCOMPARE(fresh.aliases(g), QStringList{"G. Bárány"});
    QCOMPARE(library::PeopleStore(path).displayName(g), QStringLiteral("Gergely"));
    // Merge: the dropped record folds in (its name becomes a nickname; note one under the other;
    // learned side and unknown fields where the kept one has none); self moves to the kept person.
    QVERIFY(fresh.merge(g, e));
    QVERIFY(!fresh.contains(e));
    QCOMPARE(fresh.aliases(g), QStringList({"G. Bárány", kEszter}));
    QCOMPARE(fresh.person(g).note, QStringLiteral("PM\nPénzügy"));
    QCOMPARE(fresh.defaultSide(g), QStringLiteral("local"));
    QCOMPARE(fresh.selfId(), g);
    root = readJson(path);
    QCOMPARE(root.value("people").toObject().size(), 1);
    // Delete: the whole record goes away (self too).
    QVERIFY(fresh.remove(g));
    QVERIFY(library::PeopleStore(path).ids().isEmpty());
    QVERIFY(library::PeopleStore(path).selfId().isNull());
}

void PeopleServiceTest::corruptPeopleFileIsSetAside()
{
    const QString path = m_home->filePath("d/people.json");
    writeFile(path, R"({"version":3,"people":{"p":{"name":"X","alia)");
    const QByteArray before = readFile(path);
    library::PeopleStore store(path);
    QCOMPARE(readFile(path), before);                       // unchanged by loading
    const PersonId g = store.create(kGergely);
    QVERIFY(store.addAlias(g, "Gergő"));
    QCOMPARE(QDir(m_home->filePath("d")).entryList({"people.json.corrupt-*"}, QDir::Files).size(), 1);
    QCOMPARE(library::PeopleStore(path).aliases(g), QStringList{"Gergő"});
}

void PeopleServiceTest::oldFormatFileIsReadOnly()
{
    // An old, name-keyed people.json (v1 / v2) is NOT read: the store is empty and never writes over it
    // (`tanara-cli migrate person-ids` converts it).
    for (const QByteArray& old : {QByteArray(R"({"people":["B. Gergő","Bárány Gergely"]})"),
                                  QByteArray(R"({"version":2,"people":[{"name":"Bárány Gergely","aliases":["G"],"note":""}]})")}) {
        const QString path = m_home->filePath("old/people.json");
        writeFile(path, old);
        library::PeopleStore store(path);
        QVERIFY(store.legacy());
        QVERIFY(store.ids().isEmpty());
        QVERIFY(store.create("Új Ember").isNull());
        store.setSelfId(PersonId::fromString("x"));
        QCOMPARE(readFile(path), old);                       // byte-identical
    }
}

void PeopleServiceTest::oldFormatUnderTheAppIsNotTouched()
{
    // The app starting on unconverted data: it neither reads nor overwrites the old people.json /
    // voiceprints.json (no "self" is created either).
    m_app.reset();
    const QString home = m_home->path();
    const QByteArray oldPeople = R"({"version":2,"people":[{"name":"Bárány Gergely","aliases":[],"note":""}]})";
    const QByteArray oldPrints = R"({"people":[{"name":"Bárány Gergely","prints":[{"id":"p1","embedding":[1,0,0],"dim":3}]}]})";
    writeFile(QDir(home).filePath("people.json"), oldPeople);
    writeFile(QDir(home).filePath("voiceprints.json"), oldPrints);
    m_app = std::make_unique<app::AppController>();
    QVERIFY(m_app->people()->legacy());
    QVERIFY(m_app->voiceprints()->legacy());
    QVERIFY(svc()->persons().isEmpty());
    QVERIFY(!svc()->addPerson(kEszter).ok);
    m_app->setUserSpeakerName(kSelf);
    QCOMPARE(readFile(metaFile("people.json")), oldPeople);
    QCOMPARE(readFile(metaFile("voiceprints.json")), oldPrints);
}

void PeopleServiceTest::sameNameTwoPeople()
{
    // Two different people may carry the same name: they stay apart by id (meetings, voiceprints).
    const PersonId a = m_app->people()->create(kEszter);
    const PersonId b = m_app->people()->create(kEszter);
    QVERIFY(a != b);
    QCOMPARE(m_app->people()->findByName(kEszter).size(), 2);
    m_app->voiceprints()->addPrint(a, print("pa", {1, 0, 0}));
    const Meeting m = addMeeting("Kettő", QDateTime(QDate(2026, 9, 1), QTime(9, 0)), {{0, 4000, "speaker:1"}}, {}, false);
    Meeting mm = m_app->store()->load(m.id);
    mm.speakerMap.insert("speaker:1", b);
    m_app->store()->saveMeeting(mm);
    m_app->peopleStats()->refreshNow();
    QCOMPARE(svc()->person(a).sampleCount, 1);
    QCOMPARE(svc()->person(b).sampleCount, 0);
    QCOMPARE(m_app->peopleStats()->stats(a).meetingCount, 0);
    QCOMPARE(m_app->peopleStats()->stats(b).meetingCount, 1);
    // The picker lists both (the last-seen date tells them apart).
    int seen = 0;
    for (const editing::PersonInfo& p : m_app->peopleDirectory())
        if (p.name == kEszter) {
            ++seen;
            QCOMPARE(p.lastSeen.isValid(), p.personId == b);
        }
    QCOMPARE(seen, 2);
}

// ---- nickname search ----

void PeopleServiceTest::aliasSearchInPersonPicker()
{
    QVERIFY(svc()->addPerson(kGergely).ok);
    QVERIFY(svc()->addPerson(kGergo).ok);
    QVERIFY(svc()->addPerson("Gál Bence").ok);
    QVERIFY(svc()->addAlias(find("Gál Bence"), "Gergő bátyja").ok);
    QVERIFY(svc()->addAlias(find(kGergely), "Főnök").ok);

    const QVector<editing::PersonInfo> all = m_app->peopleDirectory();
    // Accent- and case-insensitive, name matches first, nickname-only matches after them.
    const QVector<editing::PersonInfo> hits = filterPeople(all, "GERGO");
    QCOMPARE(hits.size(), 2);
    QCOMPARE(hits[0].name, kGergo);
    QCOMPARE(hits[0].personId, find(kGergo));
    QVERIFY(hits[0].matchedAlias.isEmpty());
    QCOMPARE(hits[1].name, QStringLiteral("Gál Bence"));
    QCOMPARE(hits[1].matchedAlias, QStringLiteral("Gergő bátyja"));
    const QVector<editing::PersonInfo> boss = filterPeople(all, "fonok");
    QCOMPARE(boss.size(), 1);
    QCOMPARE(boss[0].name, kGergely);
    QVERIFY(filterPeople(all, "nincs ilyen").isEmpty());
}

// ---- statistics ----

void PeopleServiceTest::statsCountMeetingsTalkTimeAndLastSeen()
{
    const QDateTime d1(QDate(2026, 9, 24), QTime(10, 0)), d2(QDate(2026, 10, 1), QTime(9, 0));
    addMeeting("Partnerdemó", d1, {{0, 4000, "speaker:1"}, {5000, 6000, "speaker:2"}, {12000, 2000, "speaker:1"}},
               {{"speaker:1", kGergely}}, false);
    const Meeting m2 = addMeeting("Heti egyeztetés", d2, {{0, 10000, "speaker:1"}, {11000, 3000, "speaker:2"}},
                                  {{"speaker:2", kGergely}, {"speaker:1", kEszter}}, true);
    // Manual fix: the second meeting's first line is really Gergely's (per-line override).
    {
        editing::SpeakerEditSession ed(m_app->store(), nullptr, nullptr, m2.id);
        QVERIFY(ed.moveUtterances({"u0"}, "speaker:2"));
    }
    library::PeopleStats* stats = m_app->peopleStats();
    QVERIFY(!stats->ready());
    stats->refreshNow();
    QVERIFY(stats->ready());

    const library::PersonStats g = stats->stats(find(kGergely));
    QCOMPARE(g.meetingCount, 2);
    QCOMPARE(g.talkMs, qint64(4000 + 2000 + 10000 + 3000));
    QCOMPARE(g.lastSeen, d2);
    QCOMPARE(g.meetings.size(), 2);
    QCOMPARE(g.meetings[0].title, QStringLiteral("Heti egyeztetés"));   // newest first
    QCOMPARE(g.meetings[0].talkMs, qint64(13000));
    QCOMPARE(g.meetings[0].speakerKeys, QStringList{"speaker:2"});
    QVERIFY(g.meetings[0].hasSummary);
    const library::PersonStats e = stats->stats(find(kEszter));
    QCOMPARE(e.meetingCount, 1);
    QCOMPARE(e.talkMs, qint64(0));              // all its lines went to another
    QCOMPARE(stats->stats(PersonId::fromString("ismeretlen")).meetingCount, 0);
    QVERIFY(!stats->stats(PersonId::fromString("ismeretlen")).lastSeen.isValid());
}

void PeopleServiceTest::statsRefreshInBackgroundAndCache()
{
    for (int i = 0; i < 12; ++i)
        addMeeting(QStringLiteral("Megbeszélés %1").arg(i), QDateTime(QDate(2026, 9, 1 + i), QTime(10, 0)),
                   {{0, 5000, "speaker:1"}}, {{"speaker:1", kGergely}}, false);
    const PersonId g = find(kGergely), e = pid(kEszter);
    library::PeopleStats* stats = m_app->peopleStats();
    QSignalSpy changed(stats, &library::PeopleStats::changed);
    stats->refresh();
    QVERIFY(stats->busy());
    QCOMPARE(stats->stats(g).meetingCount, 0);    // the list can show meanwhile: the call does not wait
    // The point is that the call returned before the scan; how long the scan takes is not under test
    // (a loaded CI runner with 4 cores and 8 parallel tests needs well over 5 s).
    QVERIFY(changed.wait(60000));
    QCOMPARE(stats->stats(g).meetingCount, 12);
    QCOMPARE(stats->lastRescanned(), 12);
    QTRY_VERIFY_WITH_TIMEOUT(!stats->busy(), 60000);

    // Cache: it does not re-read unchanged meetings; it does a changed one.
    stats->refreshNow();
    QCOMPARE(stats->lastRescanned(), 0);
    Meeting m = m_app->store()->loadAll().first();
    m = m_app->store()->load(m.id);
    m.speakerMap.insert("speaker:1", e);
    m_app->store()->saveMeeting(m);
    stats->refreshNow();
    QCOMPARE(stats->lastRescanned(), 1);
    QCOMPARE(stats->stats(g).meetingCount, 11);
    QCOMPARE(stats->stats(e).meetingCount, 1);
    // A rename changes no meeting file: nothing to re-read.
    QVERIFY(svc()->renamePerson(e, "Tóth Eszti").ok);
    stats->refreshNow();
    QCOMPARE(stats->lastRescanned(), 0);
    QCOMPARE(stats->stats(e).meetingCount, 1);
    // A deleted meeting drops out.
    m_app->store()->deleteMeeting(m.id);
    stats->refreshNow();
    QCOMPARE(stats->stats(e).meetingCount, 0);
}

// ---- rename ----

void PeopleServiceTest::renameIsOneFieldKeepsOldNameAsAlias()
{
    const Meeting m = addMeeting("Partnerdemó", QDateTime(QDate(2026, 9, 24), QTime(10, 0)),
                                 {{0, 4000, "speaker:1"}}, {{"speaker:1", kGergely}}, false);
    const PersonId g = find(kGergely);
    m_app->voiceprints()->addPrint(g, print("p1", {1, 0, 0}, m.id));
    QVERIFY(svc()->addAlias(g, "Gergely").ok);
    const QByteArray meetingBefore = readFile(QDir(m.folder).filePath("meeting.json"));
    const QByteArray printsBefore = readFile(metaFile("voiceprints.json"));

    const library::PeopleOpResult r = svc()->renamePerson(g, "Bárány Gergő");
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.name, QStringLiteral("Bárány Gergő"));
    QCOMPARE(r.person, g);
    QVERIFY(!exists(kGergely));
    QCOMPARE(svc()->person(g).name, QStringLiteral("Bárány Gergő"));
    QCOMPARE(svc()->person(g).aliases, QStringList({"Gergely", kGergely}));
    QCOMPARE(svc()->person(g).sampleCount, 1);
    // ONE field: the meeting and the voiceprints still point to the same id, their files are untouched.
    QCOMPARE(readFile(QDir(m.folder).filePath("meeting.json")), meetingBefore);
    QCOMPARE(readFile(metaFile("voiceprints.json")), printsBefore);
    QCOMPARE(m_app->store()->load(m.id).speakerMap.value("speaker:1"), g);
    // The editor (and transcript.md) shows the new name.
    editing::SpeakerEditSession* ed = m_app->editSession(m.id);
    QCOMPARE(ed->speaker("speaker:1").displayName, QStringLiteral("Bárány Gergő"));
    // Under the old name the person picker still finds it.
    QCOMPARE(filterPeople(m_app->peopleDirectory(), "gergely").size(), 1);

    // Undo: the old name is back, without the automatic nickname.
    QVERIFY(svc()->canUndo());
    QCOMPARE(svc()->undoInfo().kind, library::PeopleUndoKind::Renamed);
    const library::PeopleUndoInfo info = svc()->undo();
    QCOMPARE(info.person, g);
    QCOMPARE(svc()->person(g).name, kGergely);
    QVERIFY(!exists("Bárány Gergő"));
    QCOMPARE(svc()->person(g).aliases, QStringList{"Gergely"});
    QCOMPARE(m_app->voiceprints()->printCount(g), 1);
    QCOMPARE(ed->speaker("speaker:1").displayName, kGergely);
    QVERIFY(!svc()->canUndo());
}

void PeopleServiceTest::renameToExistingNameIsRefused()
{
    QVERIFY(svc()->addPerson(kGergely).ok);
    QVERIFY(svc()->addPerson(kGergo).ok);
    const library::PeopleOpResult r = svc()->renamePerson(find(kGergo), "bárány gergely");
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
    QVERIFY(exists(kGergo));
    QVERIFY(!svc()->renamePerson(find(kGergo), "  ").ok);
    QVERIFY(!svc()->canUndo());
    // New person with an existing name: refused, the existing one is returned.
    const library::PeopleOpResult dup = svc()->addPerson("b. gergő");
    QVERIFY(!dup.ok);
    QCOMPARE(dup.name, kGergo);
    QCOMPARE(dup.person, find(kGergo));
}

void PeopleServiceTest::selfRenameChangesSettingAndSelfCannotBeDeleted()
{
    const PersonId self = m_app->people()->selfId();
    QVERIFY(!self.isNull());
    QCOMPARE(m_app->people()->displayName(self), kSelf);
    QVERIFY(svc()->isSelf(self));
    QVERIFY(svc()->person(self).isSelf);
    QVERIFY(svc()->renamePerson(self, "Kovács-Nagy Lilla").ok);
    QCOMPARE(m_app->settings()->settings().recording.userSpeakerName, QStringLiteral("Kovács-Nagy Lilla"));
    QCOMPARE(m_app->people()->selfId(), self);
    QCOMPARE(svc()->person(self).aliases, QStringList{kSelf});

    const library::PeopleOpResult del = svc()->removePerson(self, false);
    QVERIFY(!del.ok);
    QVERIFY(svc()->exists(self));

    svc()->undo();
    QCOMPARE(m_app->settings()->settings().recording.userSpeakerName, kSelf);
    QCOMPARE(svc()->person(self).name, kSelf);
    QVERIFY(svc()->person(self).aliases.isEmpty());
}

// ---- nickname, note ----

void PeopleServiceTest::aliasAddRemoveUndo()
{
    QVERIFY(svc()->addPerson(kGergely).ok);
    const PersonId g = find(kGergely);
    QSignalSpy people(m_app.get(), &app::AppController::peopleChanged);
    QVERIFY(svc()->addAlias(g, "Gergely").ok);
    QVERIFY(svc()->addAlias(g, "G. Bárány").ok);
    QVERIFY(svc()->addAlias(g, "Geri").ok);
    QCOMPARE(people.count(), 3);                       // the person pickers refresh
    QVERIFY(!svc()->addAlias(g, "geri").ok);
    QVERIFY(!svc()->addAlias(g, " ").ok);
    QVERIFY(!svc()->addAlias(g, kGergely).ok);

    QVERIFY(svc()->removeAlias(g, "G. Bárány"));
    QCOMPARE(svc()->person(g).aliases, QStringList({"Gergely", "Geri"}));
    QCOMPARE(svc()->undoInfo().kind, library::PeopleUndoKind::AliasRemoved);
    QCOMPARE(svc()->undoInfo().detail, QStringLiteral("G. Bárány"));
    svc()->undo();
    QCOMPARE(svc()->person(g).aliases, QStringList({"Gergely", "G. Bárány", "Geri"}));   // into its place
    QVERIFY(!svc()->removeAlias(g, "nincs"));
}

void PeopleServiceTest::noteIsStored()
{
    QVERIFY(svc()->addPerson(kGergely).ok);
    const PersonId g = find(kGergely);
    QSignalSpy changed(svc(), &library::PeopleService::changed);
    svc()->setNote(g, "Northwind oldali projektvezető.\nGyakran telefonról csatlakozik.");
    QCOMPARE(changed.count(), 1);
    svc()->setNote(g, "Northwind oldali projektvezető.\nGyakran telefonról csatlakozik.");
    QCOMPARE(changed.count(), 1);    // unchanged: no write
    QCOMPARE(library::PeopleStore(metaFile("people.json")).person(g).note,
             QStringLiteral("Northwind oldali projektvezető.\nGyakran telefonról csatlakozik."));
}

void PeopleServiceTest::learnedDefaultSide()
{
    QVERIFY(svc()->addPerson(kGergely).ok);
    QVERIFY(svc()->addPerson(kEszter).ok);
    const PersonId g = find(kGergely), e = find(kEszter);
    QVERIFY(svc()->person(g).defaultSide.isEmpty());
    // One analysis: Gergely gets Remote from his confirmed lines, Eszter Local from the default
    // only (that does not learn), an anonymous speaker is left out.
    editing::SideReport r;
    editing::PersonSide pg; pg.speakerKey = "speaker:1"; pg.personId = g; pg.side = editing::Side::Remote; pg.basis = "lines";
    editing::PersonSide pe; pe.speakerKey = "speaker:2"; pe.personId = e; pe.side = editing::Side::Local; pe.basis = "user-name";
    editing::PersonSide a; a.speakerKey = "speaker:3"; a.side = editing::Side::Local; a.basis = "lines";
    r.persons = {pg, pe, a};
    QSignalSpy changed(svc(), &library::PeopleService::changed);
    QCOMPARE(svc()->learnDefaultSides(r), 1);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(svc()->person(g).defaultSide, QStringLiteral("remote"));
    QVERIFY(svc()->person(e).defaultSide.isEmpty());
    QCOMPARE(svc()->learnDefaultSides(r), 0);    // unchanged: no write
    // The record's field in people.json; the other data stays, and it can be deleted.
    library::PeopleStore store(metaFile("people.json"));
    QCOMPARE(store.defaultSide(g), QStringLiteral("remote"));
    QCOMPARE(store.defaultSides().value(g), QStringLiteral("remote"));
    QVERIFY(store.setDefaultSide(g, QString()));
    QVERIFY(library::PeopleStore(metaFile("people.json")).defaultSide(g).isEmpty());
    QVERIFY(library::PeopleStore(metaFile("people.json")).contains(g));
}

// ---- samples ----

void PeopleServiceTest::sampleRemoveAndUndo()
{
    const Meeting m = addMeeting("Partnerdemó – adatimport", QDateTime(QDate(2026, 9, 24), QTime(10, 0)),
                                 {{0, 4000, "speaker:1"}}, {{"speaker:1", kGergely}}, true);
    const PersonId g = find(kGergely);
    m_app->voiceprints()->addPrint(g, print("p1", {1, 0, 0}, m.id));
    QSignalSpy prints(m_app.get(), &app::AppController::voiceprintsChanged);

    const library::PeopleOpResult r = svc()->removeSample("p1");
    QVERIFY(r.ok);
    QCOMPARE(r.person, g);
    QCOMPARE(r.name, kGergely);
    QCOMPARE(prints.count(), 1);
    QCOMPARE(svc()->person(g).sampleCount, 0);
    QVERIFY(svc()->exists(g));                             // the person stays
    QCOMPARE(svc()->undoInfo().kind, library::PeopleUndoKind::SampleRemoved);
    QCOMPARE(svc()->undoInfo().detail, QStringLiteral("Partnerdemó – adatimport"));
    QVERIFY(!m_app->summaryStale(m.id).stale);             // deleting a sample is not an identity change

    svc()->undo();
    const QVector<Voiceprint> back = m_app->voiceprints()->printsFor(g);
    QCOMPARE(back.size(), 1);
    QCOMPARE(back.first().id, QStringLiteral("p1"));       // the same sample, with the same id
    QCOMPARE(back.first().sampleRef, QStringLiteral("mixdown.mp3#1000-9000"));
    QVERIFY(!svc()->removeSample("nincs").ok);
}

void PeopleServiceTest::sampleMoveMarksStaleAndUndoRestores()
{
    const Meeting m = addMeeting("Partnerdemó", QDateTime(QDate(2026, 9, 24), QTime(10, 0)),
                                 {{0, 4000, "speaker:1"}}, {{"speaker:1", kGergely}}, true);
    const Meeting other = addMeeting("Másik", QDateTime(QDate(2026, 9, 25), QTime(10, 0)),
                                     {{0, 4000, "speaker:1"}}, {{"speaker:1", kGergely}}, true);
    QVERIFY(svc()->addPerson(kGergo).ok);
    const PersonId g = find(kGergely), o = find(kGergo);
    m_app->voiceprints()->addPrint(g, print("p1", {1, 0, 0}, m.id));
    m_app->voiceprints()->addPrint(g, print("p2", {0, 1, 0}, other.id));
    QSignalSpy stale(m_app.get(), &app::AppController::summaryStaleChanged);

    const library::PeopleOpResult r = svc()->moveSample("p1", m_app->people()->choiceFor("b. gergő"));
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.person, o);
    QCOMPARE(r.name, kGergo);                              // the stored spelling
    QCOMPARE(r.staleSummaries, 1);
    QCOMPARE(m_app->voiceprints()->printCount(g), 1);
    QCOMPARE(m_app->voiceprints()->printsFor(o).first().id, QStringLiteral("p1"));
    QVERIFY(m_app->summaryStale(m.id).stale);              // the sample's source meeting
    QVERIFY(!m_app->summaryStale(other.id).stale);         // another meeting not
    QCOMPARE(stale.count(), 1);
    QVERIFY(!svc()->moveSample("p2", PersonChoice::existing(g)).ok);   // already belongs to that person

    svc()->undo();
    QCOMPARE(m_app->voiceprints()->printCount(g), 2);
    QCOMPARE(m_app->voiceprints()->printCount(o), 0);
    QVERIFY(svc()->exists(o));                             // the target existed before: it stays
    QVERIFY(!m_app->summaryStale(m.id).stale);             // the stale marking is undone too
}

void PeopleServiceTest::newPersonFromSampleAndUndoRemovesPerson()
{
    const Meeting m = addMeeting("Partnerdemó", QDateTime(QDate(2026, 9, 24), QTime(10, 0)),
                                 {{0, 4000, "speaker:1"}}, {{"speaker:1", kGergely}}, false);
    const PersonId g = find(kGergely);
    m_app->voiceprints()->addPrint(g, print("p1", {1, 0, 0}, m.id));
    m_app->voiceprints()->addPrint(g, print("p2", {0, 1, 0}, m.id));

    const library::PeopleOpResult r = svc()->moveSample("p2", PersonChoice::create("Új Ember"));
    QVERIFY(r.ok);
    QCOMPARE(r.name, QStringLiteral("Új Ember"));
    QCOMPARE(r.staleSummaries, 0);                          // no summary: nothing to mark
    QVERIFY(svc()->exists(r.person));
    QVERIFY(library::PeopleStore(metaFile("people.json")).contains(r.person));
    QCOMPARE(svc()->person(r.person).sampleCount, 1);
    QCOMPARE(svc()->person(g).sampleCount, 1);              // the sample left the earlier voiceprint

    svc()->undo();
    QVERIFY(!svc()->exists(r.person));                      // the operation created it: it disappears
    QCOMPARE(svc()->person(g).sampleCount, 2);
}

void PeopleServiceTest::similarityFromStoredEmbeddings()
{
    const PersonId g = pid(kGergely), o = pid(kGergo), b = pid("Gál Bence"), e = pid(kEszter);
    m_app->voiceprints()->addPrint(g, print("a1", {1, 0, 0}));
    m_app->voiceprints()->addPrint(g, print("a2", {0, 1, 0}));
    m_app->voiceprints()->addPrint(o, print("b1", {0.6f, 0.8f, 0}));
    m_app->voiceprints()->addPrint(b, print("c1", {0, 0, 1}));
    // The best matching pair counts (as in recognition).
    QVERIFY(qAbs(svc()->similarity(g, o) - 0.8) < 1e-6);
    QVERIFY(qAbs(svc()->similarity(o, g) - 0.8) < 1e-6);
    QCOMPARE(svc()->similarity(g, b), 0.0);
    QCOMPARE(svc()->similarity(g, e), -1.0);   // no voiceprint: no percentage
}

// ---- merge ----

void PeopleServiceTest::mergeNumbersAliasAndStale()
{
    const QDateTime d(QDate(2026, 9, 20), QTime(10, 0));
    const Meeting both = addMeeting("Közös", d, {{0, 4000, "speaker:1"}, {5000, 5000, "speaker:2"}},
                                    {{"speaker:1", kGergely}, {"speaker:2", kGergo}}, true);
    const Meeting onlyLoser = addMeeting("Csak Gergő", d.addDays(1), {{0, 4000, "speaker:1"}},
                                         {{"speaker:1", kGergo}}, true);
    const Meeting noSummary = addMeeting("Összefoglaló nélkül", d.addDays(2), {{0, 4000, "speaker:1"}},
                                         {{"speaker:1", kGergo}}, false);
    const Meeting onlySurvivor = addMeeting("Csak Gergely", d.addDays(3), {{0, 4000, "speaker:1"}},
                                            {{"speaker:1", kGergely}}, true);
    const PersonId g = find(kGergely), o = find(kGergo);
    // The loser also appears as an approved participant and in tag rejections.
    {
        Meeting m = m_app->store()->load(onlyLoser.id);
        Participant p;
        p.id = "v1"; p.personId = o; p.approved = true;
        m.participants = {p};
        m_app->store()->saveMeeting(m);
    }
    m_app->voiceprints()->addPrint(g, print("a1", {1, 0, 0}, both.id));
    m_app->voiceprints()->addPrint(o, print("b1", {0, 1, 0}, onlyLoser.id));
    m_app->voiceprints()->addPrint(o, print("b2", {0, 1, 0}, onlyLoser.id));
    QVERIFY(svc()->addAlias(o, "Geri").ok);
    svc()->setNote(o, "Telefonról csatlakozik.");
    m_app->peopleStats()->refreshNow();

    const library::MergePreview preview = svc()->mergePreview(o, g);
    QCOMPARE(preview.meetingCount, 4);          // union: the shared meeting counts once
    QCOMPARE(preview.sampleCount, 3);
    QCOMPARE(preview.staleSummaries, 2);        // the disappearing person's meetings with a summary

    QSignalSpy stale(m_app.get(), &app::AppController::summaryStaleChanged);
    const library::PeopleOpResult r = svc()->merge(o, g);
    QVERIFY2(r.ok, qPrintable(r.error));
    // The dialog's numbers = the actual result.
    QCOMPARE(r.meetings, preview.meetingCount);
    QCOMPARE(r.samples, preview.sampleCount);
    QCOMPARE(r.staleSummaries, preview.staleSummaries);
    QCOMPARE(stale.count(), 2);

    QVERIFY(!svc()->exists(o));
    const library::PersonRecord rec = svc()->person(g);
    QCOMPARE(rec.sampleCount, 3);
    QCOMPARE(rec.aliases, QStringList({"Geri", kGergo}));     // the disappearing name became a nickname
    QCOMPARE(rec.note, QStringLiteral("Telefonról csatlakozik."));
    QCOMPARE(m_app->peopleStats()->stats(g).meetingCount, 4);
    QCOMPARE(m_app->store()->load(both.id).speakerMap.value("speaker:2"), g);
    QCOMPARE(m_app->store()->load(noSummary.id).speakerMap.value("speaker:1"), g);
    QCOMPARE(m_app->store()->load(onlyLoser.id).participants.first().personId, g);

    QVERIFY(m_app->summaryStale(both.id).stale);
    QVERIFY(m_app->summaryStale(onlyLoser.id).stale);
    QVERIFY(!m_app->summaryStale(noSummary.id).stale);       // it has no summary
    QVERIFY(!m_app->summaryStale(onlySurvivor.id).stale);    // nothing changed there

    // The merge is ONE undo step: the dropped person comes back with its id, record, samples and
    // every meeting reference; the stale marks go too.
    QVERIFY(svc()->canUndo());
    QCOMPARE(svc()->undoInfo().kind, library::PeopleUndoKind::Merged);
    const library::PeopleUndoInfo info = svc()->undo();
    QCOMPARE(info.person, o);
    QVERIFY(svc()->exists(o));
    QCOMPARE(svc()->person(o).name, kGergo);
    QCOMPARE(svc()->person(o).aliases, QStringList{"Geri"});
    QCOMPARE(svc()->person(o).note, QStringLiteral("Telefonról csatlakozik."));
    QCOMPARE(svc()->person(o).sampleCount, 2);
    QVERIFY(svc()->person(g).aliases.isEmpty());
    QVERIFY(svc()->person(g).note.isEmpty());
    QCOMPARE(svc()->person(g).sampleCount, 1);
    QCOMPARE(m_app->store()->load(both.id).speakerMap.value("speaker:2"), o);
    QCOMPARE(m_app->store()->load(both.id).speakerMap.value("speaker:1"), g);
    QCOMPARE(m_app->store()->load(noSummary.id).speakerMap.value("speaker:1"), o);
    QCOMPARE(m_app->store()->load(onlyLoser.id).participants.first().personId, o);
    QVERIFY(!m_app->summaryStale(both.id).stale);
    QVERIFY(!m_app->summaryStale(onlyLoser.id).stale);

    QVERIFY(!svc()->merge(g, g).ok);
    QVERIFY(!svc()->merge(PersonId::fromString("nincs-ilyen"), g).ok);
}

void PeopleServiceTest::mergeWithOpenEditorShowsStale()
{
    const Meeting m = addMeeting("Nyitott", QDateTime(QDate(2026, 9, 20), QTime(10, 0)),
                                 {{0, 4000, "speaker:1"}, {5000, 4000, "speaker:2"}},
                                 {{"speaker:1", kGergo}}, true);
    QVERIFY(svc()->addPerson(kGergely).ok);
    // In the main window the transcript editor is open: it must follow without a restart.
    editing::SpeakerEditSession* ed = m_app->editSession(m.id);
    QVERIFY(ed);
    QVERIFY(!ed->summaryStale().stale);
    QVERIFY(svc()->merge(find(kGergo), find(kGergely)).ok);
    QVERIFY(ed->summaryStale().stale);
    QCOMPARE(ed->speaker("speaker:1").personId, find(kGergely));
    QCOMPARE(ed->speaker("speaker:1").displayName, kGergely);
    QVERIFY(m_app->summaryStale(m.id).stale);
}

// ---- delete ----

void PeopleServiceTest::deleteNumbersAndStale()
{
    const QDateTime d(QDate(2026, 9, 20), QTime(10, 0));
    const Meeting a = addMeeting("Egyik", d, {{0, 4000, "Távoli 2"}}, {{"Távoli 2", kGergely}}, true);
    const Meeting b = addMeeting("Másik", d.addDays(1), {{0, 4000, "speaker:1"}}, {{"speaker:1", kGergely}}, false);
    const PersonId g = find(kGergely);
    m_app->voiceprints()->addPrint(g, print("a1", {1, 0, 0}, a.id));
    QVERIFY(svc()->addAlias(g, "Gergely").ok);
    QVERIFY(svc()->removeAlias(g, "Gergely"));       // let there be one undoable step
    m_app->peopleStats()->refreshNow();

    const library::DeletePreview preview = svc()->deletePreview(g);
    QCOMPARE(preview.meetingCount, 2);
    QCOMPARE(preview.sampleCount, 1);
    QCOMPARE(preview.staleSummaries, 1);
    QCOMPARE(preview.exampleLabel, QStringLiteral("speaker:1"));   // the newest meeting's label

    const library::PeopleOpResult r = svc()->removePerson(g, false);
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.meetings, 2);
    QCOMPARE(r.samples, 1);
    QCOMPARE(r.staleSummaries, 1);
    QVERIFY(r.person.isNull());
    QVERIFY(!svc()->exists(g));
    QCOMPARE(m_app->voiceprints()->totalPrintCount(), 0);
    QVERIFY(m_app->store()->load(a.id).speakerMap.isEmpty());       // an anonymous speaker stays
    QVERIFY(m_app->summaryStale(a.id).stale);
    QVERIFY(!m_app->summaryStale(b.id).stale);
    QVERIFY(!library::PeopleStore(metaFile("people.json")).contains(g));
    QVERIFY(!svc()->canUndo());                                      // the delete cannot be undone
    QVERIFY(!svc()->removePerson(g, false).ok);
}

void PeopleServiceTest::deleteKeepingSamplesAsAnonymous()
{
    const Meeting a = addMeeting("Egyik", QDateTime(QDate(2026, 9, 20), QTime(10, 0)),
                                 {{0, 4000, "speaker:1"}}, {{"speaker:1", kGergely}}, false);
    const PersonId g = find(kGergely);
    m_app->voiceprints()->addPrint(g, print("a1", {1, 0, 0}, a.id));
    m_app->voiceprints()->addPrint(g, print("a2", {0, 1, 0}, a.id));
    QVERIFY(svc()->addPerson("Névtelen 1").ok);       // taken name: the next free one is needed

    const library::PeopleOpResult r = svc()->removePerson(g, true);
    QVERIFY(r.ok);
    QCOMPARE(r.name, QStringLiteral("Névtelen 2"));
    QVERIFY(!svc()->exists(g));
    QVERIFY(svc()->exists(r.person));
    QCOMPARE(svc()->person(r.person).sampleCount, 2);
    QVERIFY(library::PeopleStore(metaFile("people.json")).contains(r.person));
    // An anonymous speaker stays on the meeting; the anonymous PERSON does not go onto it.
    QVERIFY(m_app->store()->load(a.id).speakerMap.isEmpty());

    // Without samples there is nothing to keep: no anonymous person is created.
    QVERIFY(svc()->addPerson(kEszter).ok);
    const library::PeopleOpResult none = svc()->removePerson(find(kEszter), true);
    QVERIFY(none.ok);
    QVERIFY(none.person.isNull());
    QVERIFY(!exists("Névtelen 3"));
}

// ---- voiceprint from the manually assigned meetings ----

void PeopleServiceTest::voiceprintFromManuallyAssignedMeetings()
{
    const QDateTime d(QDate(2026, 9, 3), QTime(10, 0));
    // Enough material: 4 x 5 s of certain lines.
    const Meeting rich = addMeeting("Kickoff", d,
        {{0, 5000, "speaker:1"}, {6000, 5000, "speaker:1"}, {12000, 5000, "speaker:1"}, {18000, 5000, "speaker:1"},
         {24000, 5000, "speaker:2"}}, {{"speaker:1", kEszter}}, false);
    // Little material: a single 4 s line.
    const Meeting poor = addMeeting("Rövid", d.addDays(3), {{0, 4000, "speaker:1"}}, {{"speaker:1", kEszter}}, false);
    const PersonId e = find(kEszter);
    m_app->peopleStats()->refreshNow();

    // Without a voice model: the plan says it cannot work.
    library::VoiceprintPlan plan = svc()->voiceprintPlan(e);
    QVERIFY(!plan.modelAvailable);
    QVERIFY(!svc()->createVoiceprintFromMeeting(e, rich.id).ok);

    svc()->setEmbedderFactory([] { return std::make_unique<FakeEmbedder>(); });
    plan = svc()->voiceprintPlan(e);
    QVERIFY(plan.modelAvailable);
    QCOMPARE(plan.meetingsWithLines, 2);
    QCOMPARE(plan.usableMeetingIds, QStringList{rich.id});
    QCOMPARE(plan.usableMs, qint64(20000));

    QSignalSpy prints(m_app.get(), &app::AppController::voiceprintsChanged);
    const editing::VoiceprintResult bad = svc()->createVoiceprintFromMeeting(e, poor.id);
    QVERIFY(!bad.ok);
    QVERIFY(!bad.error.isEmpty());
    const editing::VoiceprintResult ok = svc()->createVoiceprintFromMeeting(e, rich.id);
    QVERIFY2(ok.ok, qPrintable(ok.error));
    QCOMPARE(ok.usedMs, qint64(20000));
    QCOMPARE(prints.count(), 1);
    const QVector<Voiceprint> made = m_app->voiceprints()->printsFor(e);
    QCOMPARE(made.size(), 1);
    QCOMPARE(made.first().sourceMeetingId, rich.id);
    // Someone with no line in any meeting: an empty plan.
    QVERIFY(svc()->addPerson(kGergo).ok);
    QCOMPARE(svc()->voiceprintPlan(find(kGergo)).meetingsWithLines, 0);
}

void PeopleServiceTest::sampleSourceDescription()
{
    Meeting m = addMeeting("Partnerdemó – adatimport", QDateTime(QDate(2026, 9, 24), QTime(10, 0)),
                           {{0, 4000, "speaker:1"}}, {{"speaker:1", kGergely}}, false);
    const PersonId g = find(kGergely);
    Track mic;
    mic.id = "mic"; mic.kind = TrackKind::Mic; mic.file = "track_mic.ogg"; mic.deviceName = "Trust USB mikrofon";
    Track loop;
    loop.id = "loop"; loop.kind = TrackKind::Loopback; loop.file = "track_loop.ogg";
    loop.deviceName = "Monitor of Headset Communication";
    loop.startOffsetMs = 10000;   // a track switched on later: the sample's meeting time becomes file time
    m.tracks = {mic, loop};
    m_app->store()->saveMeeting(m);
    writeFile(QDir(m.folder).filePath("track_mic.ogg"), "x");

    Voiceprint a = print("s-mic", {1, 0, 0}, m.id);
    a.sourceTrack = "mic"; a.sampleRef = "track_mic.ogg#2000-44000";
    Voiceprint b = print("s-call", {1, 0, 0}, m.id);
    b.sourceTrack = "loop"; b.sampleRef = "track_loop.ogg#12000-38000";
    Voiceprint c = print("s-mix", {1, 0, 0}, m.id);
    Voiceprint e = print("s-gone", {1, 0, 0}, "torolt-megbeszeles");
    e.sourceTrack.clear(); e.createdAt = "2026-08-28T09:00:00";
    for (const Voiceprint& p : {a, b, c, e}) m_app->voiceprints()->addPrint(g, p);

    const QVector<library::VoiceSample> samples = svc()->samples(g);
    QCOMPARE(samples.size(), 4);
    auto byId = [&](const QString& id) {
        for (const library::VoiceSample& s : samples) if (s.id == id) return s;
        return library::VoiceSample();
    };
    const library::VoiceSample sm = byId("s-mic");
    QCOMPARE(sm.person, g);
    QCOMPARE(sm.sourceKind, QStringLiteral("mic"));
    QCOMPARE(sm.deviceLabel, QStringLiteral("Trust USB mikrofon"));
    QCOMPARE(sm.meetingTitle, QStringLiteral("Partnerdemó – adatimport"));
    QCOMPARE(sm.recordedAt, QDate(2026, 9, 24));
    QCOMPARE(sm.durationMs(), qint64(42000));
    QVERIFY(sm.audioExists);
    QCOMPARE(sm.audioPath, QDir(m.folder).filePath("track_mic.ogg"));
    QCOMPARE(byId("s-call").sourceKind, QStringLiteral("call"));
    QCOMPARE(byId("s-call").startMs, qint64(2000));
    QCOMPARE(byId("s-call").endMs, qint64(28000));
    QVERIFY(!byId("s-call").audioExists);
    QCOMPARE(byId("s-mix").sourceKind, QStringLiteral("mix"));
    const library::VoiceSample gone = byId("s-gone");
    QCOMPARE(gone.sourceKind, QStringLiteral("unknown"));
    QVERIFY(gone.meetingTitle.isEmpty());
    QCOMPARE(gone.recordedAt, QDate(2026, 8, 28));     // without the meeting, the day the sample was made
    QCOMPARE(samples.last().id, QStringLiteral("s-gone"));   // newest first
}

// ---- two processes ----

namespace {
const PersonId kParent = PersonId::fromString("p-parent");
const PersonId kChild = PersonId::fromString("p-child");
const PersonId kShared = PersonId::fromString("p-shared");
}

void PeopleServiceTest::twoProcessesWriteConcurrently()
{
    // ANOTHER process (this test exe in child mode) writes into the same two files while we
    // write too: at the end both sides' every change must be there.
    const QString meta = m_app->store()->metadataDir();
    QVERIFY(m_app->people()->restore(Person{kShared, kGergely, {}, {}, {}, {}, false}));
    QProcess child;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("TANARA_PEOPLE_CHILD", meta);
    child.setProcessEnvironment(env);
    child.start(QCoreApplication::applicationFilePath(), QStringList());
    QVERIFY(child.waitForStarted(10000));

    const int n = 40;
    for (int i = 0; i < n; ++i) {
        QVERIFY(svc()->addPerson(QStringLiteral("Szülő %1").arg(i)).ok);
        QVERIFY(svc()->addAlias(kShared, QStringLiteral("szülő-becenév %1").arg(i)).ok);
        m_app->voiceprints()->addPrint(kParent, print(QStringLiteral("parent-%1").arg(i), {1, 0, 0}));
    }
    QVERIFY(child.waitForFinished(60000));
    QCOMPARE(child.exitCode(), 0);

    svc()->reload();
    library::PeopleStore people(QDir(meta).filePath("people.json"));
    const QStringList aliases = people.aliases(kShared);
    const library::VoiceprintStore prints(QDir(meta).filePath("voiceprints.json"));
    for (int i = 0; i < n; ++i) {
        QVERIFY2(!people.idForName(QStringLiteral("Szülő %1").arg(i)).isNull(), qPrintable(QString::number(i)));
        QVERIFY2(!people.idForName(QStringLiteral("Gyerek %1").arg(i)).isNull(), qPrintable(QString::number(i)));
        QVERIFY2(aliases.contains(QStringLiteral("szülő-becenév %1").arg(i)), qPrintable(QString::number(i)));
        QVERIFY2(aliases.contains(QStringLiteral("gyerek-becenév %1").arg(i)), qPrintable(QString::number(i)));
    }
    QCOMPARE(prints.printCount(kParent), n);
    QCOMPARE(prints.printCount(kChild), n);
    // The running instance sees the other process's data too.
    QVERIFY(exists("Gyerek 0"));
    QCOMPARE(svc()->person(kShared).aliases.size(), 2 * n);
    // No lock file remains.
    QVERIFY(QDir(meta).entryList({"*.lock"}, QDir::Files | QDir::Hidden).isEmpty());
}

// The child process: writes into the two files of the given metadata folder (through the
// stores, as another Tanara process would).
static int runChild(const QString& meta)
{
    library::PeopleStore people(QDir(meta).filePath("people.json"));
    library::VoiceprintStore prints(QDir(meta).filePath("voiceprints.json"));
    for (int i = 0; i < 40; ++i) {
        if (people.create(QStringLiteral("Gyerek %1").arg(i)).isNull()) return 3;
        if (!people.addAlias(kShared, QStringLiteral("gyerek-becenév %1").arg(i))) return 2;
        prints.addPrint(kChild, print(QStringLiteral("child-%1").arg(i), {0, 1, 0}));
    }
    return 0;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString childMeta = qEnvironmentVariable("TANARA_PEOPLE_CHILD");
    if (!childMeta.isEmpty()) return runChild(childMeta);
    PeopleServiceTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_people_service.moc"
