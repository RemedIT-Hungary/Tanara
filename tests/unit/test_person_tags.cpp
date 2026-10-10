//
// Címkék a személyeken: tanult kapcsolat (statisztika szintetikus sorokból: a saját név
// kimarad, nincs 8-ra vágva), kézi címke oda-vissza + visszavonás + people.json, a címke
// törlésének / összevonásának átvezetése a személyekre, a személy-összevonás uniója, a
// javaslat-küszöbök mindkét irányban, a résztvevők alapján javasolt megbeszélés-címke,
// (személy, címke) elutasítás, és a tagEvidence támogat / ellentmond / semleges esetei.
// Kitalált nevek; minden ideiglenes mappában (illetve TANARA_HOME homokozóban).
//
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/people/PeopleService.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/PeopleStore.h"
#include "tanara/tags/PersonTags.h"
#include "tanara/tags/TagService.h"

#include <memory>

using namespace tanara;

namespace {

const QString kSelf = QStringLiteral("Kovács Lilla");
const QString kGabor = QStringLiteral("Fehér Gábor");
const QString kArpad = QStringLiteral("Varga Árpád");
const QString kEszter = QStringLiteral("Molnár Eszter");
const QString kBence = QStringLiteral("Szabó Bence");

PersonTagRow row(const QString& id, int day, const QStringList& tags, const QStringList& people)
{
    return PersonTagRow{ id, QDateTime(QDate(2026, 9, day), QTime(10, 0)), tags, people };
}

QJsonObject readJson(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

} // namespace

class PersonTagsTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    // tiszta modul
    void statsExcludeSelfAndDoNotTruncate();
    void thresholds();
    void evidenceSupportContradictNeutral();

    // TagService + PeopleStore
    void manualTagsRoundTripAndUndo();
    void removeAndMergePropagateToPeople();
    void peopleStoreRenameUnionsTags();
    void peopleStatsAndSuggestPeopleForTag();
    void suggestTagsForPerson();
    void personRejection();
    void meetingTagsFromParticipants();
    void serviceEvidence();
    void selfIsExcluded();
    void backgroundStats();

    // AppController + PeopleService
    void appMergeUnionsTagsAndRejections();
    void appRemoveAndRenamePerson();

private:
    QString addMeeting(const QString& title, int day, const QStringList& people, const QStringList& tagIds = {});
    void makeService();

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<MeetingStore> m_store;
    std::unique_ptr<PeopleStore> m_people;
    std::unique_ptr<TagService> m_tags;
};

void PersonTagsTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_store = std::make_unique<MeetingStore>(m_dir->filePath("rec"), m_dir->filePath("meta"));
    m_people = std::make_unique<PeopleStore>(m_dir->filePath("meta/people.json"));
    makeService();
}

void PersonTagsTest::cleanup()
{
    m_tags.reset();
    m_people.reset();
    m_store.reset();
    m_dir.reset();
}

void PersonTagsTest::makeService()
{
    m_tags = std::make_unique<TagService>(m_store.get(), m_dir->filePath("meta/tags.json"));
    m_tags->setPeopleStore(m_people.get());
    m_tags->setSelfNameProvider([] { return kSelf; });
}

QString PersonTagsTest::addMeeting(const QString& title, int day, const QStringList& people, const QStringList& tagIds)
{
    Meeting m = m_store->createMeeting(title);
    m.startedAt = QDateTime(QDate(2026, 9, day), QTime(10, 0));
    m.durationMs = 30 * 60 * 1000;
    Track mic;
    mic.id = "mic"; mic.kind = TrackKind::Mic; mic.file = "track_mic.ogg";
    mic.speakerLabel = kSelf; mic.fixedSpeaker = true; mic.active = true;
    m.tracks.append(mic);
    for (int i = 0; i < people.size(); ++i)
        m.speakerMap.insert(QStringLiteral("Beszélő %1").arg(i + 2), people.at(i));
    m.tagIds = tagIds;
    m_store->saveMeeting(m);
    return m.id;
}

// ---- tiszta modul ------------------------------------------------------------------------------

void PersonTagsTest::statsExcludeSelfAndDoNotTruncate()
{
    QStringList crowd;
    for (int i = 1; i <= 12; ++i) crowd << QStringLiteral("Résztvevő %1").arg(i);
    QVector<PersonTagRow> rows;
    rows << row("m1", 1, {"nord"}, QStringList{kSelf, kGabor} + crowd)
         << row("m2", 2, {"nord"}, {kSelf, kGabor.toUpper(), kArpad})
         << row("m3", 3, {"nord", "part"}, {kSelf, kArpad})
         << row("m4", 4, {}, {kSelf, kGabor})
         << row("m5", 5, {"part"}, {kSelf});
    const PersonTagStats st = PersonTagStats::compute(rows, kSelf.toLower());

    QCOMPARE(st.meetingCount(), 5);
    QCOMPARE(st.tagTotal("nord"), 3);
    QCOMPARE(st.tagTotal("part"), 2);
    // A saját név sehol.
    QCOMPARE(st.shared(kSelf, "nord"), 0);
    QCOMPARE(st.personTotal(kSelf), 0);
    QVERIFY(!st.peopleOf("nord").contains(kSelf));
    // Nincs 8-ra vágva: mind a 12 + Gábor + Árpád.
    QCOMPARE(st.peopleOf("nord").size(), 14);
    QCOMPARE(st.shared(QStringLiteral("Résztvevő 12"), "nord"), 1);
    // Kisbetű-független; |M(p)| a címke nélküli megbeszélést is számolja.
    QCOMPARE(st.shared(kGabor, "nord"), 2);
    QCOMPARE(st.personTotal(kGabor), 3);
    QCOMPARE(st.lastShared(kGabor, "nord").date(), QDate(2026, 9, 2));
    QCOMPARE(st.displayName(kGabor.toLower()), kGabor);    // a legutóbbi (m4) írásmód
    QCOMPARE(st.tagsOf(kArpad).size(), 2);
}

void PersonTagsTest::thresholds()
{
    using namespace persontags;
    QVERIFY(!learnedLink(1, 1));          // < 2 közös
    QVERIFY(learnedLink(2, 6));           // 33 %
    QVERIFY(!learnedLink(2, 7));          // 28 %
    QVERIFY(learnedLink(3, 10));          // pont 30 %
    QVERIFY(!suggestableForPerson(1, 1, 1));
    QVERIFY(suggestableForPerson(2, 20, 4));    // 10 % a címkéé, de 50 % a személyé
    QVERIFY(!suggestableForPerson(2, 20, 5));   // 10 % / 40 %
    QVERIFY(suggestableForPerson(3, 9, 30));    // 33 % a címkéé
}

void PersonTagsTest::evidenceSupportContradictNeutral()
{
    QVector<PersonTagRow> rows;
    for (int i = 1; i <= 14; ++i)
        rows << row(QStringLiteral("n%1").arg(i), i, {"nord"}, i <= 11 ? QStringList{kGabor} : QStringList{kArpad});
    rows << row("p1", 20, {"part"}, {kEszter}) << row("p2", 21, {"part"}, {kEszter});
    PersonTagSnapshot snap;
    snap.stats = std::make_shared<PersonTagStats>(PersonTagStats::compute(rows, kSelf));
    snap.tagNames = { {"nord", "Nordvik"}, {"part", "Partnerek"}, {"new", "Új"} };
    snap.selfKey = persontags::personKey(kSelf);
    snap.manual.insert(persontags::personKey(kEszter), {"part"});
    snap.manual.insert(persontags::personKey(kBence), {"nord"});

    // Tanult támogatás kézi címke nélkül.
    TagEvidence ev = snap.evidence(kGabor, {"nord"});
    QVERIFY(ev.isSupport());
    QCOMPARE(ev.supportTagIds, QStringList{"nord"});
    QCOMPARE(ev.text, QStringLiteral("#Nordvik · 11 / 14"));
    QVERIFY(!ev.items.first().manual);

    // Kézi támogatás 0 közös megbeszéléssel (előre felvett ember).
    ev = snap.evidence(kBence, {"nord", "part"});
    QCOMPARE(ev.supportTagIds, QStringList{"nord"});
    QVERIFY(ev.items.first().manual);
    QCOMPARE(ev.text, QStringLiteral("#Nordvik · 0 / 14"));

    // Több támogató címke: „+1”.
    snap.manual.insert(persontags::personKey(kGabor), {"part"});
    ev = snap.evidence(kGabor, {"nord", "part"});
    QCOMPARE(ev.supportTagIds, (QStringList{"part", "nord"}));   // kézi előbb
    QVERIFY(ev.text.startsWith(QStringLiteral("#Partnerek +1 · ")));
    snap.manual.remove(persontags::personKey(kGabor));

    // Ellentmondás: kézi címkéje van, egyik sem a megbeszélésé, tanult kapcsolat sincs.
    ev = snap.evidence(kEszter, {"nord"});
    QVERIFY(ev.isContradiction());
    QVERIFY(!ev.isSupport());
    QCOMPARE(ev.contradictTagIds, QStringList{"nord"});
    QCOMPARE(ev.personTagIds, QStringList{"part"});
    QCOMPARE(ev.text, QStringLiteral("#Nordvik · 0 / 14"));

    // Semleges: címke nélküli személy (Árpád: 3/14 a küszöb alatt) sosem büntetett.
    QVERIFY(snap.evidence(kArpad, {"nord"}).isEmpty());
    // Semleges: címke nélküli megbeszélés; ismeretlen címke; saját személy.
    QVERIFY(snap.evidence(kEszter, {}).isEmpty());
    QVERIFY(snap.evidence(kEszter, {"ismeretlen"}).isEmpty());
    snap.manual.insert(persontags::personKey(kSelf), {"part"});
    QVERIFY(snap.evidence(kSelf, {"nord"}).isEmpty());
}

// ---- TagService + PeopleStore -----------------------------------------------------------------

void PersonTagsTest::manualTagsRoundTripAndUndo()
{
    m_people->add(kGabor);
    QSignalSpy changed(m_tags.get(), &TagService::personTagsChanged);
    const Tag nord = m_tags->addPersonTag(kGabor, QStringLiteral("Nordvik"));   // új címke
    QVERIFY(nord.isValid());
    const Tag part = m_tags->addPersonTag(kGabor, QStringLiteral("Partnerek"));
    QCOMPARE(m_tags->tagsOfPerson(kGabor), (QStringList{nord.id, part.id}));
    QCOMPARE(m_tags->tagsOfPerson(kGabor.toLower()), (QStringList{nord.id, part.id}));
    QVERIFY(changed.count() >= 2);
    QCOMPARE(m_tags->peopleWith(nord.id), QStringList{kGabor});

    // people.json: a rekord "tags" tömbje; egy új PeopleStore is látja.
    const QJsonArray people = readJson(m_dir->filePath("meta/people.json")).value("people").toArray();
    QCOMPARE(people.size(), 1);
    QCOMPARE(people.at(0).toObject().value("tags").toArray().size(), 2);
    PeopleStore other(m_dir->filePath("meta/people.json"));
    QCOMPARE(other.tags(kGabor), (QStringList{nord.id, part.id}));

    // Ismétlés nem duplikál; levétel; visszavonás lépésenként.
    m_tags->addPersonTag(kGabor, nord.id);
    QCOMPARE(m_tags->tagsOfPerson(kGabor).size(), 2);
    m_tags->removePersonTag(kGabor, nord.id);
    QCOMPARE(m_tags->tagsOfPerson(kGabor), QStringList{part.id});
    QVERIFY(m_tags->canUndo());
    m_tags->undo();
    QCOMPARE(m_tags->tagsOfPerson(kGabor), (QStringList{nord.id, part.id}));
    m_tags->undo();   // a Partnerek felrakása (a címke létrehozásával együtt egy lépés)
    QCOMPARE(m_tags->tagsOfPerson(kGabor), QStringList{nord.id});
    QVERIFY(!m_tags->tag(part.id).isValid());

    // setPersonTags: ismeretlen azonosító kimarad; a lista csere.
    QVERIFY(m_tags->setPersonTags(kGabor, {QStringLiteral("nincs-ilyen"), nord.id}));
    QCOMPARE(m_tags->tagsOfPerson(kGabor), QStringList{nord.id});
    QVERIFY(m_tags->setPersonTags(kGabor, {}));
    QVERIFY(m_tags->tagsOfPerson(kGabor).isEmpty());
    m_tags->undo();
    QCOMPARE(m_tags->tagsOfPerson(kGabor), QStringList{nord.id});

    // Listán nem szereplő (pl. csak hanglenyomatos) személy is címkézhető.
    m_tags->addPersonTag(kBence, nord.id);
    QCOMPARE(m_tags->tagsOfPerson(kBence), QStringList{nord.id});
    QVERIFY(!m_people->names().contains(kBence));
}

void PersonTagsTest::removeAndMergePropagateToPeople()
{
    const Tag nord = m_tags->create("Nordvik");
    const Tag nordOld = m_tags->create("Nordvik régi");
    const Tag part = m_tags->create("Partnerek");
    const QString m1 = addMeeting("Egyeztetés", 1, {kGabor}, {nordOld.id});
    m_tags->setPersonTags(kGabor, {nordOld.id, part.id});
    m_tags->setPersonTags(kArpad, {nordOld.id, nord.id});
    m_tags->rejectPersonTag(kEszter, nordOld.id);

    // Összevonás: a személyeken csere, duplikátum nélkül; az elutasítás is átszáll.
    m_tags->merge(nordOld.id, nord.id);
    QCOMPARE(m_tags->tagsOfPerson(kGabor), (QStringList{nord.id, part.id}));
    QCOMPARE(m_tags->tagsOfPerson(kArpad), QStringList{nord.id});
    QCOMPARE(m_tags->tagsOf(m1), QStringList{nord.id});
    QVERIFY(m_tags->isRejectedForPerson(kEszter, nord.id));
    m_tags->undo();   // egy lépés: meeting + személyek + készlet
    QCOMPARE(m_tags->tagsOfPerson(kGabor), (QStringList{nordOld.id, part.id}));
    QCOMPARE(m_tags->tagsOfPerson(kArpad), (QStringList{nordOld.id, nord.id}));
    QCOMPARE(m_tags->tagsOf(m1), QStringList{nordOld.id});

    // Törlés: minden személyről lekerül; visszavonható.
    m_tags->remove(part.id);
    QCOMPARE(m_tags->tagsOfPerson(kGabor), QStringList{nordOld.id});
    QVERIFY(!m_people->tags(kGabor).contains(part.id));
    m_tags->undo();
    QCOMPARE(m_tags->tagsOfPerson(kGabor), (QStringList{nordOld.id, part.id}));
}

void PersonTagsTest::peopleStoreRenameUnionsTags()
{
    m_people->add(kGabor);
    m_people->add(kArpad);
    m_people->setTags(kGabor, {"a", "b"});
    m_people->setTags(kArpad, {"b", "c"});
    m_people->rename(kGabor, kArpad, true);   // összevonás: a két rekord egy lesz
    QCOMPARE(m_people->tags(kArpad), (QStringList{"b", "c", "a"}));
    QVERIFY(m_people->tags(kGabor).isEmpty());
    m_people->rename(kArpad, kEszter);        // sima átnevezés: a címkék mennek
    QCOMPARE(m_people->tags(kEszter), (QStringList{"b", "c", "a"}));
    m_people->remove(kEszter);
    QVERIFY(m_people->allTags().isEmpty());
}

void PersonTagsTest::peopleStatsAndSuggestPeopleForTag()
{
    const Tag nord = m_tags->create("Nordvik");
    // 6 Nordvik-megbeszélés: Gábor mindegyiken, Árpád 2-n (33 %), Eszter 1-en.
    for (int i = 1; i <= 6; ++i) {
        QStringList who{kGabor};
        if (i <= 2) who << kArpad;
        if (i == 3) who << kEszter;
        addMeeting(QStringLiteral("Nordvik %1").arg(i), i, who, {nord.id});
    }
    addMeeting("Más", 10, {kArpad});
    m_tags->setPersonTags(kBence, {nord.id});   // előre felvett ember, 0 megbeszélés

    const QVector<PersonTagStat> list = m_tags->peopleStats(nord.id);
    QCOMPARE(list.size(), 3);                     // Eszter (1/6) nem éri el a küszöböt
    QCOMPARE(list.at(0).name, kBence);            // kézi előbb
    QVERIFY(list.at(0).manual);
    QCOMPARE(list.at(0).shared, 0);
    QCOMPARE(list.at(1).name, kGabor);
    QCOMPARE(list.at(1).shared, 6);
    QCOMPARE(list.at(1).tagTotal, 6);
    QCOMPARE(list.at(2).name, kArpad);
    QCOMPARE(list.at(2).personTotal, 3);

    QVector<PersonTagStat> sugg = m_tags->suggestPeopleForTag(nord.id);
    QCOMPARE(sugg.size(), 2);                     // Bence kézi → nem javaslat
    QCOMPARE(sugg.at(0).name, kGabor);
    sugg = m_tags->suggestPeopleForTag(nord.id, {kGabor.toUpper()});
    QCOMPARE(sugg.size(), 1);
    QCOMPARE(sugg.at(0).name, kArpad);
    QCOMPARE(m_tags->suggestPeopleForTag(nord.id, {}, 1).size(), 1);
}

void PersonTagsTest::suggestTagsForPerson()
{
    const Tag nord = m_tags->create("Nordvik");
    const Tag part = m_tags->create("Partnerek");
    const Tag big = m_tags->create("Nagy");
    const Tag one = m_tags->create("Egyszeri");
    // Nordvik: 3 megbeszélés, Gábor 2-n (67 %). Nagy: 20 megbeszélés, Gábor 2-n (10 %), de Gábor
    // összesen 4 megbeszélésen volt → 50 % a személyé. Partnerek: Gábor 1-en. Egyszeri: 1/1.
    addMeeting("N1", 1, {kGabor}, {nord.id, big.id});
    addMeeting("N2", 2, {kGabor}, {nord.id, big.id});
    addMeeting("N3", 3, {kArpad}, {nord.id});
    addMeeting("P1", 4, {kGabor}, {part.id});
    addMeeting("E1", 5, {kGabor}, {one.id});
    for (int i = 0; i < 18; ++i) addMeeting(QStringLiteral("Nagy %1").arg(i), 6 + i % 20, {kArpad}, {big.id});

    QVector<PersonTagStat> sugg = m_tags->suggestTagsForPerson(kGabor);
    QCOMPARE(sugg.size(), 2);
    QCOMPARE(sugg.at(0).tagId, nord.id);
    QCOMPARE(sugg.at(1).tagId, big.id);
    QCOMPARE(sugg.at(1).personTotal, 4);

    // A már felrakott címke nem javaslat; a személy címke-listája mindent mutat.
    m_tags->setPersonTags(kGabor, {nord.id});
    sugg = m_tags->suggestTagsForPerson(kGabor);
    QCOMPARE(sugg.size(), 1);
    QCOMPARE(sugg.at(0).tagId, big.id);
    const QVector<PersonTagStat> all = m_tags->personTagStats(kGabor);
    QCOMPARE(all.size(), 4);
    QVERIFY(all.at(0).manual);
    QCOMPARE(all.at(0).tagId, nord.id);

    // < 2 megbeszéléses személynek nincs javaslat.
    QVERIFY(m_tags->suggestTagsForPerson(kEszter).isEmpty());
}

void PersonTagsTest::personRejection()
{
    const Tag nord = m_tags->create("Nordvik");
    for (int i = 1; i <= 3; ++i) addMeeting(QStringLiteral("N%1").arg(i), i, {kGabor}, {nord.id});
    QCOMPARE(m_tags->suggestTagsForPerson(kGabor).size(), 1);
    QCOMPARE(m_tags->suggestPeopleForTag(nord.id).size(), 1);
    const int before = m_tags->rejectedCount();

    m_tags->rejectPersonTag(kGabor, nord.id);
    QVERIFY(m_tags->isRejectedForPerson(kGabor.toLower(), nord.id));
    QVERIFY(m_tags->suggestTagsForPerson(kGabor).isEmpty());
    QVERIFY(m_tags->suggestPeopleForTag(nord.id).isEmpty());
    QCOMPARE(m_tags->rejectedCount(), before + 1);
    // A bizonyíték marad (a statisztika igaz); a lista jelzi az elutasítást.
    QVERIFY(m_tags->tagEvidence(kGabor, {nord.id}).isSupport());
    QVERIFY(m_tags->peopleStats(nord.id).at(0).rejected);
    // A meeting-szintű elutasítás-kérdést nem zavarja.
    QVERIFY(!m_tags->isRejected(QStringLiteral("x"), nord.id));

    // Tartós: a tags.json-ban "person" mezővel, meetingId nélkül; új példány is látja.
    const QJsonArray rej = readJson(m_dir->filePath("meta/tags.json")).value("rejected").toArray();
    QCOMPARE(rej.size(), 1);
    QCOMPARE(rej.at(0).toObject().value("person").toString(), kGabor);
    QVERIFY(!rej.at(0).toObject().contains("meetingId"));
    makeService();
    QVERIFY(m_tags->isRejectedForPerson(kGabor, nord.id));

    // Visszavonható; a clearRejected ezt is törli.
    m_tags->undo();   // új példány: üres verem → hatástalan
    QVERIFY(m_tags->isRejectedForPerson(kGabor, nord.id));
    m_tags->clearRejected();
    QVERIFY(!m_tags->isRejectedForPerson(kGabor, nord.id));
    m_tags->undo();
    QVERIFY(m_tags->isRejectedForPerson(kGabor, nord.id));

    // Megbeszélés-szintű személy-elutasítás csak arra a megbeszélésre.
    m_tags->rejectPersonTag(kArpad, nord.id, QStringLiteral("m-1"));
    QVERIFY(m_tags->isRejectedForPerson(kArpad, nord.id, QStringLiteral("m-1")));
    QVERIFY(!m_tags->isRejectedForPerson(kArpad, nord.id, QStringLiteral("m-2")));
    QVERIFY(!m_tags->isRejectedForPerson(kArpad, nord.id));
}

void PersonTagsTest::meetingTagsFromParticipants()
{
    const Tag nord = m_tags->create("Nordvik");
    const Tag part = m_tags->create("Partnerek");
    m_tags->setPersonTags(kGabor, {nord.id, part.id});
    m_tags->setPersonTags(kArpad, {nord.id});
    m_tags->setPersonTags(kSelf, {part.id});   // a saját személy nem címkézhető
    QVERIFY(m_tags->tagsOfPerson(kSelf).isEmpty());

    // Két résztvevő: csak a mindkettőjükön lévő.
    const QString m1 = addMeeting("Új egyeztetés", 1, {kGabor, kArpad});
    QVector<TagSuggestion> s = m_tags->suggestMeetingTagsFromParticipants(m1);
    QCOMPARE(s.size(), 1);
    QCOMPARE(s.at(0).tagId, nord.id);
    QCOMPARE(s.at(0).source, SuggestionSource::People);
    QCOMPARE(s.at(0).reasons.size(), 1);
    QCOMPARE(s.at(0).reasons.at(0).kind, ReasonKind::PeopleTags);
    QCOMPARE(s.at(0).reasons.at(0).values, (QStringList{kGabor, kArpad}));

    // A meetingen lévő és az ott elutasított címke kimarad.
    m_tags->reject(m1, s.at(0));
    QVERIFY(m_tags->suggestMeetingTagsFromParticipants(m1).isEmpty());

    // Egyetlen nem-saját résztvevő (+ a saját mikrofon): az ő címkéi.
    const QString m2 = addMeeting("Kettesben", 2, {kGabor}, {part.id});
    s = m_tags->suggestMeetingTagsFromParticipants(m2);
    QCOMPARE(s.size(), 1);
    QCOMPARE(s.at(0).tagId, nord.id);

    // Explicit névlista (pl. kézi résztvevők, még átírás előtt).
    QCOMPARE(m_tags->suggestTagsForPeople({kGabor, kEszter}, QString()).size(), 0);
    QCOMPARE(m_tags->suggestTagsForPeople({kSelf, kArpad}, QString()).size(), 1);
}

void PersonTagsTest::serviceEvidence()
{
    const Tag nord = m_tags->create("Nordvik");
    const Tag part = m_tags->create("Partnerek");
    for (int i = 1; i <= 4; ++i) addMeeting(QStringLiteral("N%1").arg(i), i, {kGabor}, {nord.id});
    m_tags->setPersonTags(kEszter, {part.id});

    TagEvidence ev = m_tags->tagEvidence(kGabor, {nord.id});
    QCOMPARE(ev.supportTagIds, QStringList{nord.id});
    QCOMPARE(ev.text, QStringLiteral("#Nordvik · 4 / 4"));
    ev = m_tags->tagEvidence(kEszter, {nord.id});
    QCOMPARE(ev.contradictTagIds, QStringList{nord.id});
    QVERIFY(m_tags->tagEvidence(kArpad, {nord.id}).isEmpty());
    QVERIFY(m_tags->tagEvidence(kSelf, {nord.id}).isEmpty());

    // A pillanatkép szálak közt átadható: ugyanazt adja.
    const PersonTagSnapshot snap = m_tags->personTagSnapshot();
    QCOMPARE(snap.evidence(kGabor, {nord.id}).text, QStringLiteral("#Nordvik · 4 / 4"));
}

void PersonTagsTest::selfIsExcluded()
{
    const Tag nord = m_tags->create("Nordvik");
    for (int i = 1; i <= 3; ++i) addMeeting(QStringLiteral("N%1").arg(i), i, {kGabor}, {nord.id});
    QVERIFY(!m_tags->setPersonTags(kSelf, {nord.id}));
    QVERIFY(!m_tags->addPersonTag(kSelf.toUpper(), nord.id).isValid());
    for (const PersonTagStat& s : m_tags->peopleStats(nord.id)) QVERIFY(s.name != kSelf);
    QVERIFY(m_tags->personTagStats(kSelf).isEmpty());
    QVERIFY(m_tags->suggestTagsForPerson(kSelf).isEmpty());
    QCOMPARE(m_tags->personStats()->personTotal(kSelf), 0);
    // A régi profil (TagProfile) továbbra is tartalmazza — ez a csapda, amit elkerülünk.
    bool selfInProfile = false;
    for (const auto& p : m_tags->profile(nord.id).topParticipants) selfInProfile |= p.first == kSelf;
    QVERIFY(selfInProfile);
}

void PersonTagsTest::backgroundStats()
{
    const Tag nord = m_tags->create("Nordvik");
    for (int i = 1; i <= 3; ++i) addMeeting(QStringLiteral("N%1").arg(i), i, {kGabor}, {nord.id});
    QSignalSpy spy(m_tags.get(), &TagService::personStatsChanged);
    m_tags->ensurePersonStats();
    QTRY_VERIFY(!m_tags->personStatsBusy());
    QCOMPARE(m_tags->personStats()->shared(kGabor, nord.id), 3);

    // Új megbeszélés: a régi pillanatkép él, amíg az új el nem készül.
    spy.clear();
    addMeeting("N4", 4, {kGabor}, {nord.id});
    QTRY_VERIFY(spy.count() > 0 && !m_tags->personStatsBusy());
    QCOMPARE(m_tags->personStats()->shared(kGabor, nord.id), 4);
    QCOMPARE(m_tags->personStats()->tagTotal(nord.id), 4);
}

// ---- AppController + PeopleService ---------------------------------------------------------

void PersonTagsTest::appMergeUnionsTagsAndRejections()
{
    QTemporaryDir home;
    qputenv("TANARA_HOME", home.path().toUtf8());
    AppController app;
    QVERIFY(app.settings()->settings().audioDir.startsWith(home.path()));
    app.setUserSpeakerName(kSelf);
    TagService* tags = app.tags();
    PeopleService* ps = app.peopleService();
    QVERIFY(ps->addPerson(kGabor).ok);
    QVERIFY(ps->addPerson(kArpad).ok);
    const Tag nord = tags->create("Nordvik");
    const Tag part = tags->create("Partnerek");
    const Tag third = tags->create("Harmadik");
    tags->setPersonTags(kGabor, {nord.id});
    tags->setPersonTags(kArpad, {part.id, nord.id});
    tags->rejectPersonTag(kGabor, third.id);
    tags->rejectPersonTag(kArpad, third.id);

    QVERIFY(ps->merge(kGabor, kArpad).ok);
    QCOMPARE(tags->tagsOfPerson(kArpad), (QStringList{part.id, nord.id}));
    QVERIFY(tags->tagsOfPerson(kGabor).isEmpty());
    QVERIFY(tags->isRejectedForPerson(kArpad, third.id));
    QVERIFY(!tags->isRejectedForPerson(kGabor, third.id));
    QCOMPARE(tags->rejectedCount(), 1);   // unió, ismétlés nélkül
    // A megszűnő név címke-lépésének visszavonása nem írja felül az uniót.
    while (tags->canUndo()) tags->undo();
    QVERIFY(tags->tagsOfPerson(kGabor).isEmpty());
    qunsetenv("TANARA_HOME");
}

void PersonTagsTest::appRemoveAndRenamePerson()
{
    QTemporaryDir home;
    qputenv("TANARA_HOME", home.path().toUtf8());
    AppController app;
    app.setUserSpeakerName(kSelf);
    TagService* tags = app.tags();
    PeopleService* ps = app.peopleService();
    QVERIFY(ps->addPerson(kGabor).ok);
    QVERIFY(ps->addPerson(kEszter).ok);
    const Tag nord = tags->create("Nordvik");
    tags->setPersonTags(kGabor, {nord.id});
    tags->rejectPersonTag(kGabor, QStringLiteral("x"));   // ismeretlen címke → nincs rögzítve
    QCOMPARE(tags->rejectedCount(), 0);
    const Tag part = tags->create("Partnerek");
    tags->rejectPersonTag(kGabor, part.id);

    // Átnevezés: a címke és az elutasítás követi; a visszavonás az új néven működik.
    QVERIFY(ps->renamePerson(kGabor, kBence).ok);
    QCOMPARE(tags->tagsOfPerson(kBence), QStringList{nord.id});
    QVERIFY(tags->isRejectedForPerson(kBence, part.id));
    tags->undo();   // a Partnerek elutasítása
    tags->undo();   // a Partnerek létrehozása
    tags->undo();   // a Nordvik felrakása (most már Bencén)
    QVERIFY(tags->tagsOfPerson(kBence).isEmpty());
    tags->setPersonTags(kBence, {nord.id});

    // Törlés: a címkék a rekorddal mennek, az elutasítások is; a mintatartó nem örököl.
    tags->rejectPersonTag(kBence, nord.id);
    tags->rejectPersonTag(kEszter, nord.id);
    QVERIFY(ps->removePerson(kBence, false).ok);
    QVERIFY(tags->tagsOfPerson(kBence).isEmpty());
    QVERIFY(!tags->isRejectedForPerson(kBence, nord.id));
    QVERIFY(tags->isRejectedForPerson(kEszter, nord.id));
    QVERIFY(tags->peopleWith(nord.id).isEmpty());
    qunsetenv("TANARA_HOME");
}

QTEST_GUILESS_MAIN(PersonTagsTest)
#include "test_person_tags.moc"
