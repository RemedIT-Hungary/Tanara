// Tags on people: learned association (statistics from synthetic lines: the own name is left
// out, not cut to 8), manual tag both ways + undo + people.json, carrying a tag delete /
// merge over to the people, the union on a person merge, the suggestion thresholds in both
// directions, the meeting tag suggested from the participants, (person, tag) rejection, and
// tagEvidence's supports / contradicts / neutral cases.
// Made-up names; everything in a temporary folder (or a TANARA_HOME sandbox).
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "tanara/app/AppController.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/library/people/PeopleService.h"
#include "tanara/library/store/MeetingStore.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/tags/PersonTags.h"
#include "tanara/library/tags/TagService.h"

#include <memory>

using namespace tanara;

namespace {

const QString kSelf = QStringLiteral("Kovács Lilla");
const QString kGabor = QStringLiteral("Fehér Gábor");
const QString kArpad = QStringLiteral("Varga Árpád");
const QString kEszter = QStringLiteral("Molnár Eszter");
const QString kBence = QStringLiteral("Szabó Bence");

library::PersonTagRow row(const QString& id, int day, const QStringList& tags, const QStringList& people)
{
    return library::PersonTagRow{ id, QDateTime(QDate(2026, 9, day), QTime(10, 0)), tags, people };
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

    // pure module
    void statsExcludeSelfAndDoNotTruncate();
    void thresholds();
    void evidenceSupportContradictNeutral();
    void evidenceContradictionGuards();
    void evidenceExcludesOwnMeeting();
    void learningQuestionsRules();

    // TagService + PeopleStore
    void manualTagsRoundTripAndUndo();
    void removeAndMergePropagateToPeople();
    void peopleStoreRenameUnionsTags();
    void peopleStatsAndSuggestPeopleForTag();
    void suggestTagsForPerson();
    void personRejection();
    void meetingTagsFromParticipants();
    void meetingTagsFromManualParticipantsBeforeTranscript();
    void learningQuestionsServiceAskOnceAndAnswer();
    void serviceEvidence();
    void selfIsExcluded();
    void backgroundStats();

    // AppController + PeopleService
    void appMergeUnionsTagsAndRejections();
    void appRemoveAndRenamePerson();
    void appPublishesParticipantTagSuggestions();

private:
    QString addMeeting(const QString& title, int day, const QStringList& people, const QStringList& tagIds = {});
    void makeService();

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<library::MeetingStore> m_store;
    std::unique_ptr<library::PeopleStore> m_people;
    std::unique_ptr<library::TagService> m_tags;
};

void PersonTagsTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_store = std::make_unique<library::MeetingStore>(m_dir->filePath("rec"), m_dir->filePath("meta"));
    m_people = std::make_unique<library::PeopleStore>(m_dir->filePath("meta/people.json"));
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
    m_tags = std::make_unique<library::TagService>(m_store.get(), m_dir->filePath("meta/tags.json"));
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

// ---- pure module ----

void PersonTagsTest::statsExcludeSelfAndDoNotTruncate()
{
    QStringList crowd;
    for (int i = 1; i <= 12; ++i) crowd << QStringLiteral("Résztvevő %1").arg(i);
    QVector<library::PersonTagRow> rows;
    rows << row("m1", 1, {"nord"}, QStringList{kSelf, kGabor} + crowd)
         << row("m2", 2, {"nord"}, {kSelf, kGabor.toUpper(), kArpad})
         << row("m3", 3, {"nord", "part"}, {kSelf, kArpad})
         << row("m4", 4, {}, {kSelf, kGabor})
         << row("m5", 5, {"part"}, {kSelf});
    const library::PersonTagStats st = library::PersonTagStats::compute(rows, kSelf.toLower());

    QCOMPARE(st.meetingCount(), 5);
    QCOMPARE(st.tagTotal("nord"), 3);
    QCOMPARE(st.tagTotal("part"), 2);
    // The own name is nowhere.
    QCOMPARE(st.shared(kSelf, "nord"), 0);
    QCOMPARE(st.personTotal(kSelf), 0);
    QVERIFY(!st.peopleOf("nord").contains(kSelf));
    // Not cut to 8: all 12 + Gábor + Árpád.
    QCOMPARE(st.peopleOf("nord").size(), 14);
    QCOMPARE(st.shared(QStringLiteral("Résztvevő 12"), "nord"), 1);
    // Case-insensitive; |M(p)| counts the untagged meeting too.
    QCOMPARE(st.shared(kGabor, "nord"), 2);
    QCOMPARE(st.personTotal(kGabor), 3);
    QCOMPARE(st.lastShared(kGabor, "nord").date(), QDate(2026, 9, 2));
    QCOMPARE(st.displayName(kGabor.toLower()), kGabor);    // the latest (m4) spelling
    QCOMPARE(st.tagsOf(kArpad).size(), 2);
}

void PersonTagsTest::thresholds()
{
    using namespace library::persontags;
    QVERIFY(!library::persontags::learnedLink(1, 1));          // < 2 in common
    QVERIFY(library::persontags::learnedLink(2, 6));           // 33 %
    QVERIFY(!library::persontags::learnedLink(2, 7));          // 28 %
    QVERIFY(library::persontags::learnedLink(3, 10));          // exactly 30%
    QVERIFY(!library::persontags::suggestableForPerson(1, 1, 1));
    QVERIFY(library::persontags::suggestableForPerson(2, 20, 4));    // 10% of the tag, but 50% of the person
    QVERIFY(!library::persontags::suggestableForPerson(2, 20, 5));   // 10 % / 40 %
    QVERIFY(library::persontags::suggestableForPerson(3, 9, 30));    // 33% of the tag
}

void PersonTagsTest::evidenceSupportContradictNeutral()
{
    QVector<library::PersonTagRow> rows;
    for (int i = 1; i <= 14; ++i)
        rows << row(QStringLiteral("n%1").arg(i), i, {"nord"}, i <= 11 ? QStringList{kGabor} : QStringList{kArpad});
    rows << row("p1", 20, {"part"}, {kEszter}) << row("p2", 21, {"part"}, {kEszter});
    library::PersonTagSnapshot snap;
    snap.stats = std::make_shared<library::PersonTagStats>(library::PersonTagStats::compute(rows, kSelf));
    snap.tagNames = { {"nord", "Nordvik"}, {"part", "Partnerek"}, {"new", "Új"} };
    snap.selfKey = library::persontags::personKey(kSelf);
    snap.manual.insert(library::persontags::personKey(kEszter), {"part"});
    snap.manual.insert(library::persontags::personKey(kBence), {"nord"});

    // Learned support without a manual tag.
    library::TagEvidence ev = snap.evidence(kGabor, {"nord"});
    QVERIFY(ev.isSupport());
    QCOMPARE(ev.supportTagIds, QStringList{"nord"});
    QCOMPARE(ev.text, QStringLiteral("#Nordvik"));
    QCOMPARE(ev.detail, QStringLiteral("11 / 14"));
    QVERIFY(!ev.items.first().manual);

    // Manual support with 0 shared meetings (a person added in advance).
    ev = snap.evidence(kBence, {"nord", "part"});
    QCOMPARE(ev.supportTagIds, QStringList{"nord"});
    QVERIFY(ev.items.first().manual);
    QCOMPARE(ev.text, QStringLiteral("#Nordvik"));
    QCOMPARE(ev.detail, QStringLiteral("0 / 14"));

    // Several supporting tags: "+1".
    snap.manual.insert(library::persontags::personKey(kGabor), {"part"});
    ev = snap.evidence(kGabor, {"nord", "part"});
    QCOMPARE(ev.supportTagIds, (QStringList{"part", "nord"}));   // manual first
    QCOMPARE(ev.text, QStringLiteral("#Partnerek +1"));
    snap.manual.remove(library::persontags::personKey(kGabor));

    // Contradiction: it has a manual tag, none is the meeting's, and there is no learned association either.
    ev = snap.evidence(kEszter, {"nord"});
    QVERIFY(ev.isContradiction());
    QVERIFY(!ev.isSupport());
    QCOMPARE(ev.contradictTagIds, QStringList{"nord"});
    QCOMPARE(ev.personTagIds, QStringList{"part"});
    QCOMPARE(ev.personTagNames, QStringList{"Partnerek"});
    QCOMPARE(ev.text, QStringLiteral("#Nordvik"));
    QCOMPARE(ev.detail, QStringLiteral("0 / 14"));

    // Neutral: a person without tags (Árpád: 3/14 is below the threshold) is never penalized.
    QVERIFY(snap.evidence(kArpad, {"nord"}).isEmpty());
    // Neutral: an untagged meeting; an unknown tag; the own person.
    QVERIFY(snap.evidence(kEszter, {}).isEmpty());
    QVERIFY(snap.evidence(kEszter, {"ismeretlen"}).isEmpty());
    snap.manual.insert(library::persontags::personKey(kSelf), {"part"});
    QVERIFY(snap.evidence(kSelf, {"nord"}).isEmpty());
}

void PersonTagsTest::evidenceContradictionGuards()
{
    // #Nordvik: 6 meetings (mature), #New: 2 (young). Eszter's manual tag is #Partners; Bence has
    // no tag, but was at 5 tagged (#Partners) meetings; Nóra has no tag, at 2.
    const QString nora = QStringLiteral("Kiss Nóra");
    QVector<library::PersonTagRow> rows;
    for (int i = 1; i <= 6; ++i) rows << row(QStringLiteral("n%1").arg(i), i, {"nord"}, {kGabor});
    rows << row("u1", 7, {"new"}, {kGabor}) << row("u2", 8, {"new"}, {kGabor});
    for (int i = 1; i <= 5; ++i) rows << row(QStringLiteral("p%1").arg(i), 10 + i, {"part"}, {kBence});
    rows << row("q1", 20, {"part"}, {nora}) << row("q2", 21, {"part"}, {nora});
    rows << row("e1", 22, {"nord"}, {kArpad});
    library::PersonTagSnapshot snap;
    snap.stats = std::make_shared<library::PersonTagStats>(library::PersonTagStats::compute(rows, kSelf));
    snap.tagNames = { {"nord", "Nordvik"}, {"part", "Partnerek"}, {"new", "Új"} };
    snap.selfKey = library::persontags::personKey(kSelf);
    snap.manual.insert(library::persontags::personKey(kEszter), {"part"});
    snap.manual.insert(library::persontags::personKey(kArpad), {"part"});

    // All four hold: manual tag, mature tag, c = 0, so it contradicts.
    library::TagEvidence ev = snap.evidence(kEszter, {"nord"});
    QVERIFY(ev.isContradiction());
    QCOMPARE(ev.detail, QStringLiteral("0 / 7"));
    // (2) without a manual tag >= 5 tagged meetings are enough too; the "other tags" come from the learned ones.
    ev = snap.evidence(kBence, {"nord"});
    QVERIFY(ev.isContradiction());
    QCOMPARE(ev.personTagNames, QStringList{"Partnerek"});
    // (2) does not hold: an untagged person with few tagged meetings never penalizes.
    QVERIFY(snap.evidence(nora, {"nord"}).isEmpty());
    // (1) does not hold: the meeting has no (known) tag.
    QVERIFY(snap.evidence(kEszter, {}).isEmpty());
    // (3) does not hold: Árpád has already been at a #Nordvik meeting once (c = 1, below the threshold).
    QVERIFY(snap.evidence(kArpad, {"nord"}).isEmpty());
    // (4) does not hold: a young tag (|M(t)| = 2 < 5) does not penalize; not with mixed tags either.
    QVERIFY(snap.evidence(kEszter, {"new"}).isEmpty());
    QVERIFY(snap.evidence(kEszter, {"nord", "new"}).isEmpty());
}

void PersonTagsTest::evidenceExcludesOwnMeeting()
{
    // Gábor at 2 / 6 #Nordvik meetings: at the threshold. The examined meeting's own line (where
    // it is already under Gábor's name) does not count: 1 / 5, so no learned support.
    QVector<library::PersonTagRow> rows;
    for (int i = 1; i <= 6; ++i)
        rows << row(QStringLiteral("n%1").arg(i), i, {"nord"}, i <= 2 ? QStringList{kGabor} : QStringList{kArpad});
    library::PersonTagSnapshot snap;
    snap.stats = std::make_shared<library::PersonTagStats>(library::PersonTagStats::compute(rows, kSelf));
    snap.tagNames = { {"nord", "Nordvik"} };
    QVERIFY(snap.evidence(kGabor, {"nord"}).isSupport());
    QCOMPARE(snap.evidence(kGabor, {"nord"}, "n3").detail, QStringLiteral("2 / 5"));
    QVERIFY(snap.evidence(kGabor, {"nord"}, "n2").isEmpty());
}

void PersonTagsTest::learningQuestionsRules()
{
    // The examined meeting: "cur" (#Nordvik, #Partners). On other meetings: Gábor and Árpád 1-1
    // on #Nordvik, Bence 2, Eszter none (new person), Nóra one untagged.
    const QString nora = QStringLiteral("Kiss Nóra");
    QVector<library::PersonTagRow> rows;
    rows << row("a", 1, {"nord"}, {kGabor, kArpad}) << row("b", 2, {"nord"}, {kBence})
         << row("c", 3, {"nord"}, {kBence}) << row("d", 4, {}, {nora})
         << row("cur", 5, {"nord", "part"}, {kGabor, kArpad, kBence});
    library::PersonTagSnapshot snap;
    snap.stats = std::make_shared<library::PersonTagStats>(library::PersonTagStats::compute(rows, kSelf));
    snap.tagNames = { {"nord", "Nordvik"}, {"part", "Partnerek"} };
    snap.selfKey = library::persontags::personKey(kSelf);

    const QStringList names{kGabor, kArpad, kBence, kEszter, nora, kSelf, QString()};
    QVector<library::PersonTagQuestion> q = snap.learningQuestions("cur", {"nord", "part"}, names);
    // Gábor and Árpád: c becomes 2 now, so one group question; Bence is already at 3, so none;
    // Eszter is a new person, so both tags; Nóra for the first time on #Nordvik, so none; own and empty are left out.
    QCOMPARE(q.size(), 3);
    QCOMPARE(q.at(0).tagId, QStringLiteral("nord"));
    QCOMPARE(q.at(0).people, (QStringList{kGabor, kArpad}));
    QVERIFY(!q.at(0).newPeople);
    QCOMPARE(q.at(1).tagId, QStringLiteral("nord"));
    QCOMPARE(q.at(1).people, QStringList{kEszter});
    QVERIFY(q.at(1).newPeople);
    QCOMPARE(q.at(2).tagId, QStringLiteral("part"));
    QCOMPARE(q.at(2).people, QStringList{kEszter});

    // A manual tag and a rejection exclude; no question on an untagged meeting.
    snap.manual.insert(library::persontags::personKey(kGabor), {"nord"});
    q = snap.learningQuestions("cur", {"nord"}, {kGabor, kArpad, kEszter},
                               [](const QString& n, const QString& t) { return n == kEszter && t == "nord"; });
    QCOMPARE(q.size(), 1);
    QCOMPARE(q.at(0).people, QStringList{kArpad});
    QVERIFY(snap.learningQuestions("d", {}, {nora}).isEmpty());
}

// ---- TagService + PeopleStore -----------------------------------------------------------------

void PersonTagsTest::manualTagsRoundTripAndUndo()
{
    m_people->add(kGabor);
    QSignalSpy changed(m_tags.get(), &library::TagService::personTagsChanged);
    const Tag nord = m_tags->addPersonTag(kGabor, QStringLiteral("Nordvik"));   // new tag
    QVERIFY(nord.isValid());
    const Tag part = m_tags->addPersonTag(kGabor, QStringLiteral("Partnerek"));
    QCOMPARE(m_tags->tagsOfPerson(kGabor), (QStringList{nord.id, part.id}));
    QCOMPARE(m_tags->tagsOfPerson(kGabor.toLower()), (QStringList{nord.id, part.id}));
    QVERIFY(changed.count() >= 2);
    QCOMPARE(m_tags->peopleWith(nord.id), QStringList{kGabor});

    // people.json: the record's "tags" array; a new PeopleStore sees it too.
    const QJsonArray people = readJson(m_dir->filePath("meta/people.json")).value("people").toArray();
    QCOMPARE(people.size(), 1);
    QCOMPARE(people.at(0).toObject().value("tags").toArray().size(), 2);
    library::PeopleStore other(m_dir->filePath("meta/people.json"));
    QCOMPARE(other.tags(kGabor), (QStringList{nord.id, part.id}));

    // A repeat does not duplicate; removal; undo step by step.
    m_tags->addPersonTag(kGabor, nord.id);
    QCOMPARE(m_tags->tagsOfPerson(kGabor).size(), 2);
    m_tags->removePersonTag(kGabor, nord.id);
    QCOMPARE(m_tags->tagsOfPerson(kGabor), QStringList{part.id});
    QVERIFY(m_tags->canUndo());
    m_tags->undo();
    QCOMPARE(m_tags->tagsOfPerson(kGabor), (QStringList{nord.id, part.id}));
    m_tags->undo();   // applying Partners (with creating the tag, one step)
    QCOMPARE(m_tags->tagsOfPerson(kGabor), QStringList{nord.id});
    QVERIFY(!m_tags->tag(part.id).isValid());

    // setPersonTags: an unknown id is left out; the list is replaced.
    QVERIFY(m_tags->setPersonTags(kGabor, {QStringLiteral("nincs-ilyen"), nord.id}));
    QCOMPARE(m_tags->tagsOfPerson(kGabor), QStringList{nord.id});
    QVERIFY(m_tags->setPersonTags(kGabor, {}));
    QVERIFY(m_tags->tagsOfPerson(kGabor).isEmpty());
    m_tags->undo();
    QCOMPARE(m_tags->tagsOfPerson(kGabor), QStringList{nord.id});

    // A person not on the list (e.g. one that only has a voiceprint) can be tagged too.
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

    // Merge: replacement on the people, without duplicates; the rejection carries over too.
    m_tags->merge(nordOld.id, nord.id);
    QCOMPARE(m_tags->tagsOfPerson(kGabor), (QStringList{nord.id, part.id}));
    QCOMPARE(m_tags->tagsOfPerson(kArpad), QStringList{nord.id});
    QCOMPARE(m_tags->tagsOf(m1), QStringList{nord.id});
    QVERIFY(m_tags->isRejectedForPerson(kEszter, nord.id));
    m_tags->undo();   // one step: meeting + people + set
    QCOMPARE(m_tags->tagsOfPerson(kGabor), (QStringList{nordOld.id, part.id}));
    QCOMPARE(m_tags->tagsOfPerson(kArpad), (QStringList{nordOld.id, nord.id}));
    QCOMPARE(m_tags->tagsOf(m1), QStringList{nordOld.id});

    // Delete: removed from every person; undoable.
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
    m_people->rename(kGabor, kArpad, true);   // merge: the two records become one
    QCOMPARE(m_people->tags(kArpad), (QStringList{"b", "c", "a"}));
    QVERIFY(m_people->tags(kGabor).isEmpty());
    m_people->rename(kArpad, kEszter);        // plain rename: the tags go along
    QCOMPARE(m_people->tags(kEszter), (QStringList{"b", "c", "a"}));
    m_people->remove(kEszter);
    QVERIFY(m_people->allTags().isEmpty());
}

void PersonTagsTest::peopleStatsAndSuggestPeopleForTag()
{
    const Tag nord = m_tags->create("Nordvik");
    // 6 Nordvik meetings: Gábor on all, Árpád on 2 (33%), Eszter on 1.
    for (int i = 1; i <= 6; ++i) {
        QStringList who{kGabor};
        if (i <= 2) who << kArpad;
        if (i == 3) who << kEszter;
        addMeeting(QStringLiteral("Nordvik %1").arg(i), i, who, {nord.id});
    }
    addMeeting("Más", 10, {kArpad});
    m_tags->setPersonTags(kBence, {nord.id});   // a person added in advance, 0 meetings

    const QVector<library::PersonTagStat> list = m_tags->peopleStats(nord.id);
    QCOMPARE(list.size(), 3);                     // Eszter (1/6) does not reach the threshold
    QCOMPARE(list.at(0).name, kBence);            // manual first
    QVERIFY(list.at(0).manual);
    QCOMPARE(list.at(0).shared, 0);
    QCOMPARE(list.at(1).name, kGabor);
    QCOMPARE(list.at(1).shared, 6);
    QCOMPARE(list.at(1).tagTotal, 6);
    QCOMPARE(list.at(2).name, kArpad);
    QCOMPARE(list.at(2).personTotal, 3);

    QVector<library::PersonTagStat> sugg = m_tags->suggestPeopleForTag(nord.id);
    QCOMPARE(sugg.size(), 2);                     // Bence is manual, so not a suggestion
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
    // Nordvik: 3 meetings, Gábor on 2 (67%). Big: 20 meetings, Gábor on 2 (10%), but Gábor was
    // at 4 meetings in total, so 50% of the person. Partners: Gábor on 1. One-off: 1/1.
    addMeeting("N1", 1, {kGabor}, {nord.id, big.id});
    addMeeting("N2", 2, {kGabor}, {nord.id, big.id});
    addMeeting("N3", 3, {kArpad}, {nord.id});
    addMeeting("P1", 4, {kGabor}, {part.id});
    addMeeting("E1", 5, {kGabor}, {one.id});
    for (int i = 0; i < 18; ++i) addMeeting(QStringLiteral("Nagy %1").arg(i), 6 + i % 20, {kArpad}, {big.id});

    QVector<library::PersonTagStat> sugg = m_tags->suggestTagsForPerson(kGabor);
    QCOMPARE(sugg.size(), 2);
    QCOMPARE(sugg.at(0).tagId, nord.id);
    QCOMPARE(sugg.at(1).tagId, big.id);
    QCOMPARE(sugg.at(1).personTotal, 4);

    // An already applied tag is not a suggestion; the person's tag list shows everything.
    m_tags->setPersonTags(kGabor, {nord.id});
    sugg = m_tags->suggestTagsForPerson(kGabor);
    QCOMPARE(sugg.size(), 1);
    QCOMPARE(sugg.at(0).tagId, big.id);
    const QVector<library::PersonTagStat> all = m_tags->personTagStats(kGabor);
    QCOMPARE(all.size(), 4);
    QVERIFY(all.at(0).manual);
    QCOMPARE(all.at(0).tagId, nord.id);

    // A person with < 2 meetings gets no suggestion.
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
    // The evidence stays (the statistics are true); the list shows the rejection.
    QVERIFY(m_tags->tagEvidence(kGabor, {nord.id}).isSupport());
    QVERIFY(m_tags->peopleStats(nord.id).at(0).rejected);
    // It does not disturb the meeting-level rejection question.
    QVERIFY(!m_tags->isRejected(QStringLiteral("x"), nord.id));

    // Persistent: in tags.json with a "person" field, without meetingId; a new instance sees it too.
    const QJsonArray rej = readJson(m_dir->filePath("meta/tags.json")).value("rejected").toArray();
    QCOMPARE(rej.size(), 1);
    QCOMPARE(rej.at(0).toObject().value("person").toString(), kGabor);
    QVERIFY(!rej.at(0).toObject().contains("meetingId"));
    makeService();
    QVERIFY(m_tags->isRejectedForPerson(kGabor, nord.id));

    // Undoable; clearRejected clears this too.
    m_tags->undo();   // new instance: empty stack, so no effect
    QVERIFY(m_tags->isRejectedForPerson(kGabor, nord.id));
    m_tags->clearRejected();
    QVERIFY(!m_tags->isRejectedForPerson(kGabor, nord.id));
    m_tags->undo();
    QVERIFY(m_tags->isRejectedForPerson(kGabor, nord.id));

    // A meeting-level person rejection applies to that meeting only.
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
    m_tags->setPersonTags(kSelf, {part.id});   // the own person cannot be tagged
    QVERIFY(m_tags->tagsOfPerson(kSelf).isEmpty());

    // Two participants: only the tags on both of them.
    const QString m1 = addMeeting("Új egyeztetés", 1, {kGabor, kArpad});
    QVector<TagSuggestion> s = m_tags->suggestMeetingTagsFromParticipants(m1);
    QCOMPARE(s.size(), 1);
    QCOMPARE(s.at(0).tagId, nord.id);
    QCOMPARE(s.at(0).source, SuggestionSource::People);
    QCOMPARE(s.at(0).reasons.size(), 1);
    QCOMPARE(s.at(0).reasons.at(0).kind, ReasonKind::PeopleTags);
    QCOMPARE(s.at(0).reasons.at(0).values, (QStringList{kGabor, kArpad}));

    // A tag that is on the meeting and one rejected there are left out.
    m_tags->reject(m1, s.at(0));
    QVERIFY(m_tags->suggestMeetingTagsFromParticipants(m1).isEmpty());

    // A single non-own participant (+ the own microphone): their tags.
    const QString m2 = addMeeting("Kettesben", 2, {kGabor}, {part.id});
    s = m_tags->suggestMeetingTagsFromParticipants(m2);
    QCOMPARE(s.size(), 1);
    QCOMPARE(s.at(0).tagId, nord.id);

    // An explicit name list (e.g. manual participants, before the transcript).
    QCOMPARE(m_tags->suggestTagsForPeople({kGabor, kEszter}, QString()).size(), 0);
    QCOMPARE(m_tags->suggestTagsForPeople({kSelf, kArpad}, QString()).size(), 1);
}

void PersonTagsTest::meetingTagsFromManualParticipantsBeforeTranscript()
{
    // Before the transcript there is no speaker map: manual and approved participants count; an
    // unapproved audio candidate does not.
    const Tag nord = m_tags->create("Nordvik");
    m_tags->setPersonTags(kGabor, {nord.id});
    m_tags->setPersonTags(kArpad, {nord.id});
    m_tags->setPersonTags(kEszter, {nord.id});
    const QString mid = addMeeting("Felvétel", 3, {});
    Meeting m = m_store->load(mid);
    Participant manual;
    manual.id = "m-1"; manual.personName = kGabor; manual.source = ParticipantSource::Manual;
    m.participants = {manual};
    m_store->saveMeeting(m);
    // A single non-own participant with the tag on them: a suggestion.
    QVector<TagSuggestion> s = m_tags->suggestMeetingTagsFromParticipants(mid);
    QCOMPARE(s.size(), 1);
    QCOMPARE(s.at(0).reasons.at(0).values, QStringList{kGabor});

    Participant guess;
    guess.id = "v-1"; guess.personName = kEszter; guess.source = ParticipantSource::Voice;
    Participant approved;
    approved.id = "v-2"; approved.personName = kArpad; approved.source = ParticipantSource::Voice;
    approved.approved = true;
    m = m_store->load(mid);
    m.participants << guess << approved;
    m_store->saveMeeting(m);
    s = m_tags->suggestMeetingTagsFromParticipants(mid);
    QCOMPARE(s.size(), 1);
    QCOMPARE(s.at(0).reasons.at(0).values, (QStringList{kGabor, kArpad}));   // Eszter (candidate) not
}

void PersonTagsTest::learningQuestionsServiceAskOnceAndAnswer()
{
    const Tag nord = m_tags->create("Nordvik");
    addMeeting("Első", 1, {kGabor, kArpad}, {nord.id});
    const QString mid = addMeeting("Második", 2, {kGabor, kArpad}, {nord.id});
    QVector<library::PersonTagQuestion> q = m_tags->learningQuestions(mid, {kGabor, kArpad});
    QCOMPARE(q.size(), 1);
    QCOMPARE(q.at(0).tagName, QStringLiteral("Nordvik"));
    QCOMPARE(q.at(0).people, (QStringList{kGabor, kArpad}));

    // Asks once.
    m_tags->markLearningAsked(mid, q);
    QVERIFY(m_tags->learningQuestions(mid, {kGabor, kArpad}).isEmpty());

    // "Yes": for both of them, ONE undo step.
    m_tags->answerLearning(q.at(0), true);
    QCOMPARE(m_tags->tagsOfPerson(kGabor), QStringList{nord.id});
    QCOMPARE(m_tags->tagsOfPerson(kArpad), QStringList{nord.id});
    m_tags->undo();
    QVERIFY(m_tags->tagsOfPerson(kGabor).isEmpty());
    QVERIFY(m_tags->tagsOfPerson(kArpad).isEmpty());

    // "No": a persistent (person, tag) rejection, one step; afterwards it does not ask again.
    m_tags->answerLearning(q.at(0), false);
    QVERIFY(m_tags->isRejectedForPerson(kGabor, nord.id));
    QVERIFY(m_tags->isRejectedForPerson(kArpad, nord.id));
    const QString third = addMeeting("Harmadik", 3, {kGabor}, {nord.id});
    Q_UNUSED(third);
    m_tags->undo();
    QVERIFY(!m_tags->isRejectedForPerson(kGabor, nord.id));
    QVERIFY(!m_tags->isRejectedForPerson(kArpad, nord.id));
}

void PersonTagsTest::serviceEvidence()
{
    const Tag nord = m_tags->create("Nordvik");
    const Tag part = m_tags->create("Partnerek");
    for (int i = 1; i <= 4; ++i) addMeeting(QStringLiteral("N%1").arg(i), i, {kGabor}, {nord.id});
    m_tags->setPersonTags(kEszter, {part.id});

    library::TagEvidence ev = m_tags->tagEvidence(kGabor, {nord.id});
    QCOMPARE(ev.supportTagIds, QStringList{nord.id});
    QCOMPARE(ev.text, QStringLiteral("#Nordvik"));
    QCOMPARE(ev.detail, QStringLiteral("4 / 4"));
    // Eszter has a manual tag, but #Nordvik is not yet mature (4 < 5 meetings): no penalty.
    QVERIFY(m_tags->tagEvidence(kEszter, {nord.id}).isEmpty());
    addMeeting(QStringLiteral("N5"), 5, {kArpad}, {nord.id});
    ev = m_tags->tagEvidence(kEszter, {nord.id});
    QCOMPARE(ev.contradictTagIds, QStringList{nord.id});
    QVERIFY(m_tags->tagEvidence(kArpad, {nord.id}).isEmpty());
    QVERIFY(m_tags->tagEvidence(kSelf, {nord.id}).isEmpty());

    // The snapshot can be passed between threads: it gives the same.
    const library::PersonTagSnapshot snap = m_tags->personTagSnapshot();
    QCOMPARE(snap.evidence(kGabor, {nord.id}).detail, QStringLiteral("4 / 5"));
}

void PersonTagsTest::selfIsExcluded()
{
    const Tag nord = m_tags->create("Nordvik");
    for (int i = 1; i <= 3; ++i) addMeeting(QStringLiteral("N%1").arg(i), i, {kGabor}, {nord.id});
    QVERIFY(!m_tags->setPersonTags(kSelf, {nord.id}));
    QVERIFY(!m_tags->addPersonTag(kSelf.toUpper(), nord.id).isValid());
    for (const library::PersonTagStat& s : m_tags->peopleStats(nord.id)) QVERIFY(s.name != kSelf);
    QVERIFY(m_tags->personTagStats(kSelf).isEmpty());
    QVERIFY(m_tags->suggestTagsForPerson(kSelf).isEmpty());
    QCOMPARE(m_tags->personStats()->personTotal(kSelf), 0);
    // The old profile (TagProfile) still contains it: this is the trap we avoid.
    bool selfInProfile = false;
    for (const auto& p : m_tags->profile(nord.id).topParticipants) selfInProfile |= p.first == kSelf;
    QVERIFY(selfInProfile);
}

void PersonTagsTest::backgroundStats()
{
    const Tag nord = m_tags->create("Nordvik");
    for (int i = 1; i <= 3; ++i) addMeeting(QStringLiteral("N%1").arg(i), i, {kGabor}, {nord.id});
    QSignalSpy spy(m_tags.get(), &library::TagService::personStatsChanged);
    m_tags->ensurePersonStats();
    QTRY_VERIFY(!m_tags->personStatsBusy());
    QCOMPARE(m_tags->personStats()->shared(kGabor, nord.id), 3);

    // New meeting: the old snapshot lives until the new one is ready.
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
    app::AppController app;
    QVERIFY(app.settings()->settings().audioDir.startsWith(home.path()));
    app.setUserSpeakerName(kSelf);
    library::TagService* tags = app.tags();
    library::PeopleService* ps = app.peopleService();
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
    QCOMPARE(tags->rejectedCount(), 1);   // union, without repeats
    // Undoing the disappearing name's tag step does not overwrite the union.
    while (tags->canUndo()) tags->undo();
    QVERIFY(tags->tagsOfPerson(kGabor).isEmpty());
    qunsetenv("TANARA_HOME");
}

void PersonTagsTest::appRemoveAndRenamePerson()
{
    QTemporaryDir home;
    qputenv("TANARA_HOME", home.path().toUtf8());
    app::AppController app;
    app.setUserSpeakerName(kSelf);
    library::TagService* tags = app.tags();
    library::PeopleService* ps = app.peopleService();
    QVERIFY(ps->addPerson(kGabor).ok);
    QVERIFY(ps->addPerson(kEszter).ok);
    const Tag nord = tags->create("Nordvik");
    tags->setPersonTags(kGabor, {nord.id});
    tags->rejectPersonTag(kGabor, QStringLiteral("x"));   // unknown tag: not recorded
    QCOMPARE(tags->rejectedCount(), 0);
    const Tag part = tags->create("Partnerek");
    tags->rejectPersonTag(kGabor, part.id);

    // Rename: the tag and the rejection follow; undo works under the new name.
    QVERIFY(ps->renamePerson(kGabor, kBence).ok);
    QCOMPARE(tags->tagsOfPerson(kBence), QStringList{nord.id});
    QVERIFY(tags->isRejectedForPerson(kBence, part.id));
    tags->undo();   // the Partners rejection
    tags->undo();   // creating Partners
    tags->undo();   // applying Nordvik (now on Bence)
    QVERIFY(tags->tagsOfPerson(kBence).isEmpty());
    tags->setPersonTags(kBence, {nord.id});

    // Delete: the tags go with the record, and the rejections too; the sample holder does not inherit.
    tags->rejectPersonTag(kBence, nord.id);
    tags->rejectPersonTag(kEszter, nord.id);
    QVERIFY(ps->removePerson(kBence, false).ok);
    QVERIFY(tags->tagsOfPerson(kBence).isEmpty());
    QVERIFY(!tags->isRejectedForPerson(kBence, nord.id));
    QVERIFY(tags->isRejectedForPerson(kEszter, nord.id));
    QVERIFY(tags->peopleWith(nord.id).isEmpty());
    qunsetenv("TANARA_HOME");
}

void PersonTagsTest::appPublishesParticipantTagSuggestions()
{
    QTemporaryDir home;
    qputenv("TANARA_HOME", home.path().toUtf8());
    {
        app::AppController app;
        QVERIFY(app.settings()->settings().audioDir.startsWith(home.path()));
        app.setUserSpeakerName(kSelf);
        library::TagService* tags = app.tags();
        const Tag nord = tags->create("Nordvik");
        tags->setPersonTags(kGabor, {nord.id});
        tags->setPersonTags(kArpad, {nord.id});
        const QString mid = app.store()->createMeeting(QStringLiteral("Új felvétel")).id;

        QSignalSpy ready(&app, &app::AppController::tagSuggestionsReady);
        app.requestTagSuggestions(mid);
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty(), 10000);
        auto hasPeople = [&] {
            for (const TagSuggestion& sg : app.pendingTagSuggestions(mid))
                if (sg.source == SuggestionSource::People && sg.tagId == nord.id) return true;
            return false;
        };
        QVERIFY(!hasPeople());

        // Manual participants before the transcript: the list refreshes by itself (participantsChanged).
        app.addParticipant(mid, kGabor);
        app.addParticipant(mid, kArpad);
        QVERIFY(hasPeople());
        const QVector<TagSuggestion> list = app.pendingTagSuggestions(mid);
        QCOMPARE(list.first().source, SuggestionSource::People);   // first
        QCOMPARE(list.first().reasons.first().kind, ReasonKind::PeopleTags);

        // Removing the person tag refreshes too; the applied tag is left out.
        tags->removePersonTag(kArpad, nord.id);
        QVERIFY(!hasPeople());
        tags->setPersonTags(kArpad, {nord.id});
        QVERIFY(hasPeople());
        tags->addTag(mid, nord.id);
        QVERIFY(!hasPeople());
    }
    qunsetenv("TANARA_HOME");
}

QTEST_GUILESS_MAIN(PersonTagsTest)
#include "test_person_tags.moc"
