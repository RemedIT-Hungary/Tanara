// PeopleViewModel / PeopleListModel / PeopleWindowHost: the People window's view models.
//  - without a controller: made-up demo people, the actions do nothing;
//  - with a real AppController in an ISOLATED TANARA_HOME (QTemporaryDir), with made-up names:
//    the list appears at once and the statistics load afterwards, search (nickname,
//    highlight), sorting, sections, the actions with their notices and undo, the merge /
//    delete texts with real numbers, sample playback (silent engine), a voiceprint from the
//    meetings (fake embedder), 80+ people;
//  - tags on the person: section modes (editable / own / anonymous), accepting and rejecting
//    suggestions, "All", undo on TagService's stack (the newer stack first), search by tag,
//    the merge text with the tags; models' flags on samples, the voiceprint-backfill indicator;
//  - the QML window through its host: opening with a person selected, the demo states load
//    without a warning.
// It touches no real data, audio output or model.
#include "AppContext.h"
#include "PeopleListModel.h"
#include "PeopleViewModel.h"
#include "PeopleWindowHost.h"
#include "QmlApp.h"

#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/app/AppController.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/processing/analysis/UtteranceEmbeddings.h"
#include "tanara/library/people/PeopleService.h"
#include "tanara/library/people/PeopleStats.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/library/tags/TagService.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace tanara;
using namespace tanara_qml;

namespace {

const QString kSelf = QStringLiteral("Kovács Lilla");
const QString kGergely = QStringLiteral("Bárány Gergely");
const QString kGergo = QStringLiteral("B. Gergő");
const QString kEszter = QStringLiteral("Tóth Eszter");

struct Line { qint64 startMs; qint64 durMs; QString raw; };

class FakeEmbedder : public processing::IUtteranceEmbedder {
public:
    bool open(const QString&) override { return true; }
    QVector<float> embed(qint64, qint64) override { return {1.0f, 0.0f, 0.0f}; }
};

Voiceprint print(const QString& id, const QVector<float>& e, const QString& meetingId)
{
    Voiceprint p;
    p.id = id;
    p.embedding = e;
    p.sourceMeetingId = meetingId;
    p.sourceTrack = QStringLiteral("mixdown");
    p.sampleRef = QStringLiteral("mixdown.mp3#1000-1400");
    p.createdAt = QStringLiteral("2026-09-30T10:00:00");
    return p;
}

QStringList roleList(const PeopleListModel* model, int role)
{
    QStringList out;
    for (int i = 0; i < model->rowCount(); ++i) out << model->data(model->index(i), role).toString();
    return out;
}

} // namespace

class TestPeopleViewModel : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<app::AppController> m_app;

    void startApp()
    {
        m_home = std::make_unique<QTemporaryDir>();
        QVERIFY(m_home->isValid());
        qputenv("TANARA_HOME", m_home->path().toUtf8());
        m_app = std::make_unique<app::AppController>();
        QVERIFY(m_app->settings()->paths().primaryRecordingRoot().startsWith(m_home->path()));
        m_app->setUserSpeakerName(kSelf);
    }

    // The facades of the test controller (the view models are built on them).
    const UiServices& services() { return AppContext::instance()->servicesFor(m_app.get()); }

    // The id of an existing person by name (null if none).
    PersonId id(const QString& name) const { return m_app->people()->idForName(name); }
    PersonId ensurePerson(const QString& name)
    {
        const PersonId known = id(name);
        return known.isNull() ? m_app->peopleService()->addPerson(name).person : known;
    }

    Meeting addMeeting(const QString& title, const QDate& day, const QVector<Line>& lines,
                       const QMap<QString, QString>& speakerMap, bool hasSummary = false)
    {
        Meeting m = m_app->store()->createMeeting(title);
        QJsonArray segs;
        for (const Line& l : lines)
            segs.append(QJsonObject{{"startMs", double(l.startMs)}, {"endMs", double(l.startMs + l.durMs)},
                                    {"speaker", l.raw}, {"text", QStringLiteral("szöveg")}});
        QFile f(QDir(m.folder).filePath("transcript.segments.json"));
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(segs).toJson());
        f.close();
        QFile audio(QDir(m.folder).filePath("mixdown.mp3"));
        if (audio.open(QIODevice::WriteOnly)) audio.write("nem-valodi-hang");
        m.startedAt = QDateTime(day, QTime(10, 0));
        m.hasTranscript = true;
        m.hasSummary = hasSummary;
        for (auto it = speakerMap.constBegin(); it != speakerMap.constEnd(); ++it) m.speakerMap.insert(it.key(), ensurePerson(it.value()));
        m.mixdownFile = QStringLiteral("mixdown.mp3");
        m_app->store()->saveMeeting(m);
        
        return m_app->store()->load(m.id);
    }

    // Three meetings, four people: Gergely (2 meetings, 2 samples), Gergő (1, 1 sample),
    // Eszter (2, no samples), and the own person.
    void scenario(Meeting* first = nullptr, Meeting* second = nullptr)
    {
        const Meeting a = addMeeting("Partnerdemó – adatimport", QDate(2026, 9, 24),
            {{0, 60000, "Beszélő 1"}, {61000, 120000, "Beszélő 2"}},
            {{"Beszélő 1", kGergely}, {"Beszélő 2", kEszter}}, true);
        const Meeting b = addMeeting("Heti egyeztetés", QDate(2026, 10, 1),
            {{0, 600000, "Beszélő 1"}, {601000, 5000, "Beszélő 2"}, {607000, 5000, "Beszélő 2"},
             {613000, 5000, "Beszélő 2"}, {619000, 5000, "Beszélő 2"}},
            {{"Beszélő 1", kGergely}, {"Beszélő 2", kEszter}}, true);
        addMeeting("Régi hívás", QDate(2026, 8, 3), {{0, 30000, "Beszélő 1"}}, {{"Beszélő 1", kGergo}});
        m_app->voiceprints()->addPrint(id(kGergely), print("g1", {1, 0, 0}, a.id));
        m_app->voiceprints()->addPrint(id(kGergely), print("g2", {0, 1, 0}, b.id));
        m_app->voiceprints()->addPrint(id(kGergo), print("o1", {0.6f, 0.8f, 0}, a.id));
        if (first) *first = a;
        if (second) *second = b;
    }

    static bool waitFor(const std::function<bool()>& cond, int timeoutMs = 5000)
    {
        QElapsedTimer t;
        t.start();
        while (!cond() && t.elapsed() < timeoutMs) QTest::qWait(10);
        return cond();
    }

private slots:
    void cleanup()
    {
        AppContext::instance()->setController(nullptr);
        m_app.reset();
        m_home.reset();
        qunsetenv("TANARA_HOME");
    }

    // ---- demo (without a controller) ----

    void demoServesFictionalPeople()
    {
        PeopleViewModel vm;
        QCOMPARE(vm.totalCount(), 14);
        QCOMPARE(vm.countText(), QStringLiteral("14 személy"));
        QVERIFY(vm.statsReady());
        QCOMPARE(vm.people()->nameAt(0), kSelf);                // the own person first
        QCOMPARE(vm.people()->data(vm.people()->index(0), PeopleListModel::HeaderRole).toString(),
                 QStringLiteral("Te"));

        vm.setDemoState("P01");
        QCOMPARE(vm.selectedName(), kGergely);
        QCOMPARE(vm.aliases(), QStringList({"Gergely", "G. Bárány"}));
        QCOMPARE(vm.sampleCount(), 9);
        QCOMPARE(vm.samplesSubText(), QStringLiteral("9 minta · 5 forrásból"));
        QCOMPARE(vm.meetingCount(), 23);
        QCOMPARE(vm.detailMeta(), QStringLiteral("Utoljára: okt. 1. · 23 megbeszélés · 6 ó 12 p beszéd"));
        QCOMPARE(vm.playingSampleId(), QStringLiteral("demo-1"));

        vm.setDemoState("P02");
        QCOMPARE(vm.query(), QStringLiteral("ger"));
        QCOMPARE(vm.countText(), QStringLiteral("3 találat"));
        QVERIFY(roleList(vm.people(), PeopleListModel::HeaderRole).join("").isEmpty());   // no section while searching

        vm.setDemoState("P03");
        QCOMPARE(vm.selectedName(), kEszter);
        QCOMPARE(vm.sampleCount(), 0);
        QTRY_VERIFY(vm.planReady());                               // the fictional facade answers like the real one: a turn later
        QVERIFY(vm.planPossible());
        QCOMPARE(vm.planButtonText(), QStringLiteral("Minta 3 megbeszélésből"));

        vm.setDemoState("P06");
        QVERIFY(vm.onlySelf());
        QCOMPARE(vm.totalCount(), 1);

        // In demo mode the actions do nothing (and do not crash).
        vm.setDemoState("P01");
        QCOMPARE(vm.rename("Más Név"), QString());
        vm.removeAlias("Gergely");
        vm.removeSample("demo-0");
        vm.undo();
        QCOMPARE(vm.selectedName(), kGergely);
        QCOMPARE(vm.aliases().size(), 2);
        QCOMPARE(vm.mergeCandidates("gerg").first().toMap().value("similarity").toInt(), 88);
        QVERIFY(vm.mergeResultText(kGergo, true).contains(QStringLiteral("<b>27 megbeszélés</b>")));
        QVERIFY(vm.deleteText(false).contains(QStringLiteral("„Távoli 2”")));
    }


    // ---- tags on the person (demo) ----

    void demoPersonTagsSectionAndSuggestions()
    {
        PeopleViewModel vm;
        vm.setDemoState("tagsSection");
        QCOMPARE(vm.selectedName(), QStringLiteral("Varga Árpád"));
        QCOMPARE(vm.tagsMode(), QStringLiteral("edit"));
        auto names = [](const QVariantList& list) {
            QStringList out;
            for (const QVariant& v : list) out << v.toMap().value("name").toString();
            return out;
        };
        QCOMPARE(names(vm.personTags()), QStringList({"Nordvik", "Partnerek"}));
        QCOMPARE(names(vm.tagSuggestions()), QStringList({"Q4 tervezés"}));
        QCOMPARE(vm.tagSuggestions().first().toMap().value("countText").toString(), QStringLiteral("4/9"));
        QCOMPARE(vm.tagSuggestions().first().toMap().value("toolTip").toString(),
                 QStringLiteral("A #Q4 tervezés 9 megbeszéléséből 4 alkalommal ott volt."));
        QCOMPARE(vm.tagHintText(), QString());

        // Accept: it is added, the suggestion disappears; a toast + Ctrl+Z on the tag stack.
        QSignalSpy toast(&vm, &PeopleViewModel::toast);
        vm.acceptTagSuggestion(vm.tagSuggestions().first().toMap().value("id").toString());
        QCOMPARE(names(vm.personTags()), QStringList({"Nordvik", "Partnerek", "Q4 tervezés"}));
        QVERIFY(vm.tagSuggestions().isEmpty());
        QCOMPARE(toast.last().at(0).toString(), QStringLiteral("Címke hozzáadva: #Q4 tervezés"));
        QVERIFY(toast.last().at(1).toBool());
        QVERIFY(vm.canUndo());
        vm.undo();
        QCOMPARE(names(vm.personTags()), QStringList({"Nordvik", "Partnerek"}));
        QCOMPARE(names(vm.tagSuggestions()), QStringList({"Q4 tervezés"}));

        // x: a persistent rejection, undoable.
        vm.rejectTagSuggestion(vm.tagSuggestions().first().toMap().value("id").toString());
        QVERIFY(vm.tagSuggestions().isEmpty());
        vm.undo();
        QCOMPARE(vm.tagSuggestions().size(), 1);

        // From the field: an existing name, a new name, removal.
        vm.addPersonTag(QStringLiteral("Belső"));
        vm.addPersonTag(QStringLiteral("Vadonatúj"));
        QCOMPARE(names(vm.personTags()), QStringList({"Nordvik", "Partnerek", "Belső", "Vadonatúj"}));
        vm.removePersonTag(vm.personTags().first().toMap().value("id").toString());
        QCOMPARE(names(vm.personTags()), QStringList({"Partnerek", "Belső", "Vadonatúj"}));

        // Four suggestions: the view shows three + "+1"; "All" is one step.
        vm.setDemoState("tagsSuggested");
        QCOMPARE(vm.selectedName(), QStringLiteral("Fehér Gábor"));
        QVERIFY(vm.personTags().isEmpty());
        QCOMPARE(names(vm.tagSuggestions()), QStringList({"Nordvik", "Q4 tervezés", "Belső", "Partnerek"}));
        QCOMPARE(vm.tagSuggestions().first().toMap().value("countText").toString(), QStringLiteral("11/14"));
        vm.acceptAllTagSuggestions();
        QCOMPARE(vm.personTags().size(), 4);
        QCOMPARE(toast.last().at(0).toString(), QStringLiteral("4 javasolt címke hozzáadva"));
        vm.undo();
        QVERIFY(vm.personTags().isEmpty());

        // Few meetings: an explanatory line.
        QVERIFY(vm.selectPerson(QStringLiteral("Molnár Eszter")));
        QCOMPARE(vm.tagHintText(), QStringLiteral("A javaslathoz legalább két megbeszélés kell."));

        // The own person: no editable section.
        vm.setDemoState("tagsSelf");
        QCOMPARE(vm.selectedName(), kSelf);
        QCOMPARE(vm.tagsMode(), QStringLiteral("self"));
        QVERIFY(vm.personTags().isEmpty());
    }

    void demoSearchFindsTagsAndSampleModels()
    {
        PeopleViewModel vm;
        vm.setDemoState("tagsSection");
        vm.setQuery(QStringLiteral("nordvik"));
        const QStringList names = roleList(vm.people(), PeopleListModel::NameRole);
        QCOMPARE(names, QStringList({"Molnár Eszter", "Szabó Bence", "Varga Árpád"}));
        const int row = vm.people()->indexOfName(QStringLiteral("Varga Árpád"));
        QVERIFY(vm.people()->data(vm.people()->index(row), PeopleListModel::MetaRole).toString()
                    .startsWith(QStringLiteral("címke: #Nordvik · ")));
        QVERIFY(vm.people()->data(vm.people()->index(row), PeopleListModel::TagHitRole).toBool());
        vm.setQuery(QStringLiteral("#partn"));
        QCOMPARE(roleList(vm.people(), PeopleListModel::NameRole), QStringList({"Varga Árpád"}));
        vm.setQuery(QString());
        // Without a search no tag goes into the row's meta.
        QVERIFY(!roleList(vm.people(), PeopleListModel::MetaRole).join("|").contains(QStringLiteral("címke")));

        // Samples per model: "2/2 models"; the third only with the default model.
        vm.setDemoState("samplesModels");
        QVERIFY(!vm.backfillRunning());
        const QVariantMap first = vm.samples().at(0).toMap();
        const QVariantMap third = vm.samples().at(2).toMap();
        QCOMPARE(first.value("modelText").toString(), QStringLiteral("2/2 modell"));
        QVERIFY(first.value("modelComplete").toBool());
        QCOMPARE(third.value("modelText").toString(), QStringLiteral("1/2 modell"));
        QVERIFY(!third.value("modelComplete").toBool());
        QVERIFY(third.value("modelTip").toString().contains(QStringLiteral("Hiányzik: ")));
        vm.setDemoState("backfill");
        QVERIFY(vm.backfillRunning());
        // With one model (P01 has no model list in the demo) there is no flag.
        vm.setDemoState("P01");
        QVERIFY(!vm.samples().at(0).toMap().contains("modelText"));
    }

    // ---- tags on the person (with a real controller) ----

    void personTagsWithRealTagService()
    {
        startApp();
        Meeting a, b;
        scenario(&a, &b);
        library::TagService* tags = m_app->tags();
        QVERIFY(tags);
        const QString nordvik = tags->addTag(a.id, QStringLiteral("Nordvik")).id;
        tags->addTag(b.id, nordvik);
        tags->refreshPersonStatsNow();

        PeopleViewModel vm(services());
        vm.setSelectedName(kGergely);
        QCOMPARE(vm.tagsMode(), QStringLiteral("edit"));
        QVERIFY(vm.personTags().isEmpty());
        // Gergely was at both #Nordvik meetings: 2/2.
        QCOMPARE(vm.tagSuggestions().size(), 1);
        QCOMPARE(vm.tagSuggestions().first().toMap().value("countText").toString(), QStringLiteral("2/2"));

        // First a person step (deleting a nickname), then a tag step: Ctrl+Z undoes the newer one.
        QCOMPARE(vm.addAlias(QStringLiteral("Geri")), QString());
        vm.removeAlias(QStringLiteral("Geri"));
        vm.acceptTagSuggestion(nordvik);
        QCOMPARE(tags->tagsOfPerson(id(kGergely)), QStringList({nordvik}));
        QCOMPARE(vm.personTags().size(), 1);
        vm.undo();
        QVERIFY(tags->tagsOfPerson(id(kGergely)).isEmpty());
        QVERIFY(!vm.aliases().contains(QStringLiteral("Geri")));
        vm.undo();
        QVERIFY(vm.aliases().contains(QStringLiteral("Geri")));

        // Search by tag + the merge text, when both have tags.
        vm.acceptTagSuggestion(nordvik);
        vm.setSelectedName(kGergo);
        QCOMPARE(vm.tagHintText(), QStringLiteral("A javaslathoz legalább két megbeszélés kell."));
        vm.addPersonTag(QStringLiteral("Partnerek"));
        QCOMPARE(tags->tagsOfPerson(id(kGergo)).size(), 1);
        QVERIFY2(vm.mergeResultText(kGergely, false).contains(QStringLiteral("A címkék összeadódnak (#Partnerek, #Nordvik).")),
                 qPrintable(vm.mergeResultText(kGergely, false)));
        vm.setQuery(QStringLiteral("nordv"));
        QCOMPARE(roleList(vm.people(), PeopleListModel::NameRole), QStringList({kGergely}));
        vm.setQuery(QString());

        // The own person and the sample holder "Anonymous N".
        vm.setSelectedName(kSelf);
        QCOMPARE(vm.tagsMode(), QStringLiteral("self"));
        QCOMPARE(m_app->peopleService()->addPerson(QStringLiteral("Névtelen 3")).ok, true);
        vm.refresh();
        vm.setSelectedName(QStringLiteral("Névtelen 3"));
        QCOMPARE(vm.tagsMode(), QStringLiteral("none"));
    }

    // ---- with a real controller ----

    void listAppearsImmediatelyAndStatsFillIn()
    {
        startApp();
        scenario();
        PeopleViewModel vm(services());
        QSignalSpy listChanged(&vm, &PeopleViewModel::listChanged);
        // At once: names + sample count, the meeting count is not there yet.
        QCOMPARE(vm.totalCount(), 4);
        QVERIFY(!vm.statsReady());
        QCOMPARE(vm.selectedName(), kSelf);
        const int row = vm.people()->indexOfName(kGergely);
        QCOMPARE(vm.people()->data(vm.people()->index(row), PeopleListModel::MetaRole).toString(),
                 QStringLiteral("2 minta"));
        QSignalSpy reset(vm.people(), &QAbstractItemModel::modelReset);

        vm.refresh();                                   // opening the window: counting in the background
        QVERIFY(waitFor([&] { return vm.statsReady(); }));
        QCOMPARE(vm.people()->data(vm.people()->index(row), PeopleListModel::MetaRole).toString(),
                 QStringLiteral("2 megbeszélés · 2 minta"));
        QCOMPARE(reset.count(), 0);                     // the list does not jump: only the rows were refreshed
        const int eszter = vm.people()->indexOfName(kEszter);
        QCOMPARE(vm.people()->data(vm.people()->index(eszter), PeopleListModel::MetaRole).toString(),
                 QStringLiteral("2 megbeszélés · nincs hanglenyomat"));
        QVERIFY(!vm.people()->data(vm.people()->index(eszter), PeopleListModel::HasVoiceprintRole).toBool());

        vm.setSelectedName(kGergely);
        QCOMPARE(vm.detailMeta(), QStringLiteral("Utoljára: okt. 1. · 2 megbeszélés · 11 p beszéd"));
        QCOMPARE(vm.meetingCount(), 2);
        QCOMPARE(vm.meetings().first().toMap().value("title").toString(), QStringLiteral("Heti egyeztetés"));
        QCOMPARE(vm.meetings().first().toMap().value("date").toString(), QStringLiteral("2026-10-01"));
        QCOMPARE(vm.meetings().first().toMap().value("talk").toString(), QStringLiteral("10 p"));
        QCOMPARE(vm.sampleCount(), 2);
        QCOMPARE(vm.samplesSubText(), QStringLiteral("2 minta · 1 forrásból"));
        const QVariantMap sample = vm.samples().first().toMap();
        QCOMPARE(sample.value("meetingTitle").toString(), QStringLiteral("Heti egyeztetés"));   // newest first
        QCOMPARE(sample.value("date").toString(), QStringLiteral("2026-10-01"));
        QCOMPARE(sample.value("label").toString(), QStringLiteral("A megbeszélés lekevert hangja"));
        QVERIFY(sample.value("playable").toBool());
    }

    void searchSortAndSections()
    {
        startApp();
        scenario();
        m_app->peopleService()->addAlias(id(kEszter), "Gergő felesége");
        m_app->peopleStats()->refreshNow();
        PeopleViewModel vm(services());
        QVERIFY(vm.statsReady());

        // Alphabetical: "You", then initials (an accented letter files under its base letter); the own person always first.
        QCOMPARE(roleList(vm.people(), PeopleListModel::NameRole), QStringList({kSelf, kGergo, kGergely, kEszter}));
        QCOMPARE(roleList(vm.people(), PeopleListModel::HeaderRole), QStringList({"Te", "B", "", "T"}));
        QCOMPARE(vm.sortLabel(), QStringLiteral("ABC"));

        vm.setSort("meetings");
        QCOMPARE(roleList(vm.people(), PeopleListModel::NameRole), QStringList({kSelf, kGergely, kEszter, kGergo}));
        QCOMPARE(roleList(vm.people(), PeopleListModel::HeaderRole), QStringList({"Te", "Többiek", "", ""}));
        vm.setSort("recent");
        QCOMPARE(vm.people()->nameAt(3), kGergo);       // the least recently seen at the end
        vm.setSort("abc");

        // Search: accent- and case-insensitive, in name and nickname; a name hit is highlighted.
        vm.setQuery("GERGO");
        QVERIFY(vm.searching());
        QCOMPARE(vm.countText(), QStringLiteral("2 találat"));
        QCOMPARE(roleList(vm.people(), PeopleListModel::NameRole), QStringList({kGergo, kEszter}));
        const QModelIndex hit = vm.people()->index(0);
        QCOMPARE(vm.people()->data(hit, PeopleListModel::BeforeRole).toString(), QStringLiteral("B. "));
        QCOMPARE(vm.people()->data(hit, PeopleListModel::MatchRole).toString(), QStringLiteral("Gergő"));
        QCOMPARE(vm.people()->data(hit, PeopleListModel::AfterRole).toString(), QString());
        const QModelIndex aliasHit = vm.people()->index(1);
        QCOMPARE(vm.people()->data(aliasHit, PeopleListModel::MatchRole).toString(), QString());
        QVERIFY(vm.people()->data(aliasHit, PeopleListModel::MetaRole).toString().startsWith(QStringLiteral("„Gergő felesége”")));
        QVERIFY(roleList(vm.people(), PeopleListModel::HeaderRole).join("").isEmpty());

        vm.setQuery("nincs ilyen");
        QCOMPARE(vm.countText(), QStringLiteral("0 találat"));
        QCOMPARE(vm.people()->count(), 0);
        // Deep link: the selection clears the search too, if the person would not be visible.
        QVERIFY(vm.selectPerson("tóth eszter"));
        QCOMPARE(vm.selectedName(), kEszter);
        QCOMPARE(vm.query(), QString());
        QVERIFY(vm.selectedRow() >= 0);
        QVERIFY(!vm.selectPerson("Nincs Ilyen"));
    }

    void addRenameAliasNoteWithUndoToast()
    {
        startApp();
        Meeting a;
        scenario(&a);
        m_app->peopleStats()->refreshNow();
        PeopleViewModel vm(services());
        QSignalSpy toast(&vm, &PeopleViewModel::toast);

        // New person: added and selected; for an existing name an error + the existing one selected.
        QCOMPARE(vm.addPerson("Gál Bence"), QString());
        QCOMPARE(vm.selectedName(), QStringLiteral("Gál Bence"));
        QVERIFY(!vm.onlySelf());
        QVERIFY(!vm.addPerson("bárány gergely").isEmpty());
        QCOMPARE(vm.selectedName(), kGergely);

        // Rename: the old name becomes a nickname; every meeting follows; undoable.
        QVERIFY(!vm.rename(kGergo).isEmpty());                 // existing name: a merge is needed
        QCOMPARE(vm.rename("Bárány Gergő"), QString());
        QCOMPARE(vm.selectedName(), QStringLiteral("Bárány Gergő"));
        QCOMPARE(vm.aliases(), QStringList{kGergely});
        QCOMPARE(m_app->people()->displayName(m_app->store()->load(a.id).speakerMap.value("Beszélő 1")), QStringLiteral("Bárány Gergő"));
        QCOMPARE(toast.last().at(0).toString(), QStringLiteral("Átnevezve: Bárány Gergő"));
        QCOMPARE(toast.last().at(1).toBool(), true);
        QVERIFY(vm.canUndo());
        vm.undo();
        QCOMPARE(vm.selectedName(), kGergely);
        QVERIFY(vm.aliases().isEmpty());
        QCOMPARE(m_app->people()->displayName(m_app->store()->load(a.id).speakerMap.value("Beszélő 1")), kGergely);
        QCOMPARE(toast.last().at(1).toBool(), false);
        QVERIFY(!vm.canUndo());

        // Nicknames.
        QCOMPARE(vm.addAlias("Gergely"), QString());
        QCOMPARE(vm.addAlias("G. Bárány"), QString());
        QVERIFY(!vm.addAlias("gergely").isEmpty());
        vm.removeAlias("Gergely");
        QCOMPARE(vm.aliases(), QStringList{"G. Bárány"});
        QCOMPARE(toast.last().at(0).toString(), QStringLiteral("Becenév törölve: Gergely"));
        QCOMPARE(toast.last().at(1).toBool(), true);
        vm.undo();
        QCOMPARE(vm.aliases(), QStringList({"Gergely", "G. Bárány"}));

        // Note: saved; still there after switching person.
        vm.setNote("Northwind oldali projektvezető.");
        vm.setSelectedName(kEszter);
        QCOMPARE(vm.note(), QString());
        vm.setSelectedName(kGergely);
        QCOMPARE(vm.note(), QStringLiteral("Northwind oldali projektvezető."));

        // Renaming the own person = the "Your name" setting.
        vm.setSelectedName(kSelf);
        QVERIFY(vm.selectedIsSelf());
        QCOMPARE(vm.rename("Kovács-Nagy Lilla"), QString());
        QCOMPARE(m_app->settings()->settings().recording.userSpeakerName, QStringLiteral("Kovács-Nagy Lilla"));
        QVERIFY(vm.selectedIsSelf());
        QVERIFY(!vm.deletePerson(false).isEmpty());             // the own person cannot be deleted
    }

    void sampleOperationsWithUndo()
    {
        startApp();
        Meeting a, b;
        scenario(&a, &b);
        m_app->peopleStats()->refreshNow();
        PeopleViewModel vm(services());
        vm.setSelectedName(kGergely);
        QSignalSpy toast(&vm, &PeopleViewModel::toast);

        // Delete + undo.
        vm.removeSample("g1");
        QCOMPARE(vm.sampleCount(), 1);
        QCOMPARE(toast.last().at(0).toString(), QStringLiteral("Minta törölve: Partnerdemó – adatimport"));
        QCOMPARE(toast.last().at(1).toBool(), true);
        vm.undo();
        QCOMPARE(vm.sampleCount(), 2);
        QCOMPARE(vm.selectedName(), kGergely);

        // Moving to an existing person: the source meeting's summary becomes stale.
        QVERIFY(!vm.moveSample("g1", kGergely).isEmpty());      // already belongs to that person
        QCOMPARE(vm.moveSample("g1", kGergo), QString());
        QCOMPARE(vm.sampleCount(), 1);
        QCOMPARE(m_app->voiceprints()->printCount(id(kGergo)), 2);
        QCOMPARE(toast.last().at(0).toString(), QStringLiteral("Minta áthelyezve ide: B. Gergő"));
        QVERIFY(m_app->summaryStale(a.id).stale);
        QVERIFY(!m_app->summaryStale(b.id).stale);
        vm.undo();
        QCOMPARE(vm.sampleCount(), 2);
        QVERIFY(!m_app->summaryStale(a.id).stale);

        // New person from this sample + undo (the created person disappears).
        QCOMPARE(vm.moveSample("g2", "Új Ember"), QString());
        QCOMPARE(toast.last().at(0).toString(), QStringLiteral("Új személy a mintából: Új Ember"));
        QVERIFY(vm.personExists("Új Ember"));
        QCOMPARE(vm.totalCount(), 5);
        vm.undo();
        QVERIFY(!vm.personExists("Új Ember"));
        QCOMPARE(vm.sampleCount(), 2);

        // The picker list: without the selected one, finds by nickname too.
        m_app->peopleService()->addAlias(id(kEszter), "Eszti");
        QTest::qWait(20);
        QCOMPARE(vm.personChoices("").size(), 3);
        const QVariantList hits = vm.personChoices("eszti");
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.first().toMap().value("name").toString(), kEszter);
    }

    void samplePlaybackUsesSilentBackend()
    {
        startApp();
        scenario();
        m_app->peopleStats()->refreshNow();
        PeopleViewModel vm(services());
        vm.setSelectedName(kGergely);
        QSignalSpy playing(&vm, &PeopleViewModel::playingChanged);

        vm.toggleSample("g1");                           // the sample 1000-1400 ms: 0.4 s
        QCOMPARE(vm.playingSampleId(), QStringLiteral("g1"));
        vm.toggleSample("g2");                           // another sample: the previous one stops
        QCOMPARE(vm.playingSampleId(), QStringLiteral("g2"));
        vm.toggleSample("g2");                           // the same one: pause
        QCOMPARE(vm.playingSampleId(), QString());
        vm.toggleSample("g1");
        QVERIFY(waitFor([&] { return vm.playingSampleId().isEmpty(); }, 3000));   // stops by itself at the end
        vm.toggleSample("g1");
        vm.removeSample("g1");                           // deleting the playing sample: it stops
        QCOMPARE(vm.playingSampleId(), QString());

        // Missing audio file: does not start, notifies.
        QSignalSpy toast(&vm, &PeopleViewModel::toast);
        m_app->voiceprints()->addPrint(id(kGergely), print("gone", {1, 0, 0}, "torolt"));
        QVERIFY(waitFor([&] { return vm.sampleCount() == 2; }));
        vm.toggleSample("gone");
        QCOMPARE(vm.playingSampleId(), QString());
        QCOMPARE(toast.count(), 1);
        QCOMPARE(toast.last().at(1).toBool(), false);
    }

    void mergeCandidatesBySimilarityAndRealNumbers()
    {
        startApp();
        scenario();
        m_app->peopleStats()->refreshNow();
        PeopleViewModel vm(services());
        vm.setSelectedName(kGergely);

        // By voice similarity; a percentage only for someone who has a voiceprint.
        const QVariantList cands = vm.mergeCandidates("");
        QCOMPARE(cands.size(), 3);
        QCOMPARE(cands[0].toMap().value("name").toString(), kGergo);
        QCOMPARE(cands[0].toMap().value("similarity").toInt(), 80);
        QVERIFY(cands[0].toMap().value("meta").toString().contains(QStringLiteral("hang alapján hasonló (80%)")));
        QCOMPARE(cands[1].toMap().value("similarity").toInt(), -1);
        QVERIFY(!cands[1].toMap().value("meta").toString().contains(QLatin1Char('%')));
        QCOMPARE(vm.mergeCandidates("eszt").size(), 1);

        // The result row with the actual numbers; the name that disappears depends on the choice.
        const QString keep = vm.mergeResultText(kGergo, true);
        QVERIFY2(keep.contains(QStringLiteral("<b>3 megbeszélés</b>, <b>3 minta</b>. „B. Gergő” becenév lesz.")), qPrintable(keep));
        QVERIFY(!keep.contains(QStringLiteral("összefoglaló")));          // Gergő's meeting has no summary
        QVERIFY(!keep.contains(QStringLiteral("nem vonható vissza")));   // P11: a merge is undoable
        const QString other = vm.mergeResultText(kGergo, false);
        QVERIFY(other.contains(QStringLiteral("„Bárány Gergely” becenév lesz.")));
        QVERIFY2(other.contains(QStringLiteral("2 érintett összefoglaló elavultnak jelölődik.")), qPrintable(other));

        QSignalSpy toast(&vm, &PeopleViewModel::toast);
        QCOMPARE(vm.merge(kGergo, false), QString());                     // the other name stays
        QCOMPARE(vm.selectedName(), kGergo);
        QCOMPARE(vm.totalCount(), 3);
        QCOMPARE(vm.aliases(), QStringList{kGergely});
        QCOMPARE(vm.sampleCount(), 3);
        QCOMPARE(vm.meetingCount(), 3);
        QCOMPARE(toast.last().at(0).toString(), QStringLiteral("Összevonva: B. Gergő — 3 megbeszélés, 3 minta"));
        QCOMPARE(toast.last().at(1).toBool(), true);                      // undoable (P11)
        QVERIFY(vm.canUndo());
        // Undo: both people are back, with their own samples, and the meetings name Gergely again.
        vm.undo();
        QCOMPARE(vm.totalCount(), 4);
        QVERIFY(vm.personExists(kGergely));
        QVERIFY(vm.personExists(kGergo));
        QCOMPARE(m_app->voiceprints()->printCount(id(kGergely)), 2);
        QCOMPARE(m_app->voiceprints()->printCount(id(kGergo)), 1);
        QCOMPARE(toast.last().at(0).toString(), QStringLiteral("Az összevonás visszavonva: Bárány Gergely"));   // the restored one
    }

    void deleteTextAndKeepSamples()
    {
        startApp();
        Meeting a;
        scenario(&a);
        m_app->peopleStats()->refreshNow();
        PeopleViewModel vm(services());
        vm.setSelectedName(kGergely);
        QVERIFY(vm.deleteHasSamples());
        QCOMPARE(vm.deleteText(false),
                 QStringLiteral("2 megbeszélésen névtelen beszélőként marad meg (pl. „Beszélő 1”), a szöveg nem változik. "
                                "2 hangmintája törlődik, így hang alapján többé nem ismerjük fel. "
                                "2 összefoglaló elavultnak jelölődik. A törlés nem vonható vissza."));
        QVERIFY(vm.deleteText(true).contains(QStringLiteral("2 hangmintája megmarad egy új, névtelen személynél")));

        QSignalSpy toast(&vm, &PeopleViewModel::toast);
        QCOMPARE(vm.deletePerson(true), QString());
        QVERIFY(!vm.personExists(kGergely));
        QCOMPARE(vm.selectedName(), QStringLiteral("Névtelen 1"));       // the host of the kept samples
        QCOMPARE(vm.sampleCount(), 2);
        QCOMPARE(toast.last().at(0).toString(), QStringLiteral("Törölve: Bárány Gergely. A mintái itt maradtak: Névtelen 1"));
        QVERIFY(m_app->summaryStale(a.id).stale);

        // A person without samples: nothing to keep; after deletion the row in its place is the selected one.
        vm.setSelectedName(kEszter);
        QVERIFY(!vm.deleteHasSamples());
        QVERIFY(vm.deleteText(false).contains(QStringLiteral("2 megbeszélésen")));
        QCOMPARE(vm.deletePerson(false), QString());
        QVERIFY(!vm.personExists(kEszter));
        QVERIFY(vm.hasSelection());
        QCOMPARE(toast.last().at(0).toString(), QStringLiteral("Törölve: Tóth Eszter"));
    }

    void voiceprintFromMeetingsPlanAndCreation()
    {
        startApp();
        Meeting a, b;
        scenario(&a, &b);
        m_app->peopleStats()->refreshNow();
        PeopleViewModel vm(services());
        vm.setSelectedName(kEszter);

        // Without a voice model: the text says so, there is no button.
        QVERIFY(waitFor([&] { return vm.planReady(); }));
        QVERIFY(!vm.planPossible());
        QVERIFY(vm.planText().contains(QStringLiteral("nincs telepítve")));

        m_app->peopleService()->setEmbedderFactory([] { return std::make_unique<FakeEmbedder>(); });
        vm.setSelectedName(kGergely);
        vm.setSelectedName(kEszter);
        QVERIFY(waitFor([&] { return vm.planReady() && vm.planPossible(); }));
        // Honest: they have lines in 2 meetings, but only one has a long enough, certain line.
        QVERIFY(vm.planText().contains(QStringLiteral("2 megbeszélés sorait")));
        QCOMPARE(vm.planButtonText(), QStringLiteral("Minta 1 megbeszélésből"));
        QCOMPARE(vm.planNote(), QStringLiteral("kb. 20 másodperc hang, a gépen marad · 1 megbeszélésen nincs elég hosszú sor"));

        QSignalSpy toast(&vm, &PeopleViewModel::toast);
        vm.createVoiceprint();
        QVERIFY(vm.creatingVoiceprint());
        QVERIFY(waitFor([&] { return !vm.creatingVoiceprint(); }));
        QCOMPARE(toast.last().at(0).toString(), QStringLiteral("1 minta készült."));
        QCOMPARE(vm.sampleCount(), 1);
        QCOMPARE(vm.samples().first().toMap().value("meetingTitle").toString(), QStringLiteral("Heti egyeztetés"));
        QVERIFY(!vm.planPossible());                    // already has a voiceprint

        // Someone with no line in any meeting.
        QCOMPARE(vm.addPerson("Gál Bence"), QString());
        QVERIFY(waitFor([&] { return vm.planReady(); }));
        QVERIFY(!vm.planPossible());
        QVERIFY(vm.planText().contains(QStringLiteral("sincsenek sorai")));
    }

    void staysResponsiveWithManyPeople()
    {
        startApp();
        for (int i = 0; i < 120; ++i)
            m_app->peopleService()->addPerson(QStringLiteral("Személy %1").arg(i, 3, 10, QLatin1Char('0')));
        PeopleViewModel vm(services());
        QCOMPARE(vm.totalCount(), 121);
        QElapsedTimer timer;
        timer.start();
        for (const QString& q : {"s", "sz", "sze", "szem", "személy 05", ""}) vm.setQuery(q);
        vm.setSort("meetings");
        vm.setSort("abc");
        QVERIFY2(timer.elapsed() < 400, qPrintable(QString::number(timer.elapsed())));
        vm.setQuery("személy 05");
        QCOMPARE(vm.people()->count(), 10);
    }

    void externalChangesShowUpWithoutReopening()
    {
        startApp();
        scenario();
        m_app->peopleStats()->refreshNow();
        PeopleViewModel vm(services());
        // A change coming from another window (transcript editor, Settings): the list follows by itself.
        m_app->renamePerson(id(kGergo), "Balla Gergő");
        QVERIFY(waitFor([&] { return vm.personExists("Balla Gergő"); }));
        QVERIFY(!vm.personExists(kGergo));
        m_app->setUserSpeakerName("Kovács L.");
        QVERIFY(waitFor([&] { return vm.people()->nameAt(0) == QStringLiteral("Kovács L."); }));
        m_app->voiceprints()->removePerson(id("Balla Gergő"));
        emit m_app->voiceprintsChanged();
        const int row = vm.people()->indexOfName("Balla Gergő");
        QVERIFY(waitFor([&] {
            return !vm.people()->data(vm.people()->index(row), PeopleListModel::HasVoiceprintRole).toBool();
        }));
    }

    // ---- the window ----

    void hostOpensWindowWithPersonPreselected()
    {
        startApp();
        scenario();
        PeopleWindowHost host(m_app.get());
        QSignalSpy closed(&host, &PeopleWindowHost::closed);
        QVERIFY(host.open(kEszter));
        QVERIFY(host.isVisible());
        PeopleViewModel* vm = host.viewModel();
        QVERIFY(vm);
        QCOMPARE(vm->selectedName(), kEszter);
        QVERIFY(waitFor([&] { return vm->statsReady(); }));       // counts in the background on opening
        QCOMPARE(vm->meetingCount(), 2);
        QCOMPARE(host.window()->title(), QStringLiteral("Személyek"));

        // The note is saved when the field is left (here: when the window closes).
        auto* note = host.window()->findChild<QQuickItem*>("noteArea");
        QVERIFY(note);
        note->setProperty("text", QStringLiteral("Pénzügy"));
        // With the window open, a new request without a name: the selection stays.
        QVERIFY(host.open(QString()));
        QCOMPARE(vm->selectedName(), kEszter);
        host.window()->close();
        QVERIFY(waitFor([&] { return !host.isVisible(); }));
        QCOMPARE(closed.size(), 1);
        QCOMPARE(m_app->peopleService()->person(id(kEszter)).note, QStringLiteral("Pénzügy"));

        QVERIFY(host.open(kGergely));
        QCOMPARE(vm->selectedName(), kGergely);
        host.closeNow();
        QVERIFY(!host.isVisible());
    }

    void tagsSectionInTheWindow()
    {
        QQmlEngine engine;
        setupEngine(engine);
        QQmlComponent comp(&engine);
        comp.loadFromModule("Tanara", "PeopleWindow");
        QVERIFY2(!comp.isError(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> obj(comp.createWithInitialProperties({{"demoState", "tagsSuggested"}}));
        auto* win = qobject_cast<QQuickWindow*>(obj.get());
        QVERIFY(win);
        win->show();
        QTest::qWait(80);
        auto* vm = qobject_cast<PeopleViewModel*>(win->property("vm").value<QObject*>());
        auto* section = win->findChild<QQuickItem*>("personTagsSection");
        QVERIFY(vm && section && section->isVisible());
        // Repeater rows are visual children (without a QObject parent): go through the childItems tree.
        std::function<int(QQuickItem*)> countChips = [&](QQuickItem* item) {
            int n = item->objectName() == QLatin1String("personTagSuggestion") && item->isVisible();
            for (QQuickItem* c : item->childItems()) n += countChips(c);
            return n;
        };
        auto visibleChips = [&] { return countChips(section); };
        QCOMPARE(visibleChips(), 3);                       // three + "+1"
        auto* overflow = section->findChild<QQuickItem*>("personTagOverflow");
        QVERIFY(overflow && overflow->isVisible());
        QVERIFY(QMetaObject::invokeMethod(overflow, "click"));
        QTest::qWait(20);
        QCOMPARE(visibleChips(), 4);
        QVERIFY(!overflow->isVisible());
        auto* all = section->findChild<QQuickItem*>("personTagAcceptAll");
        QVERIFY(all && all->isVisible());
        QVERIFY(QMetaObject::invokeMethod(all, "click"));
        QCOMPARE(vm->personTags().size(), 4);
        QVERIFY(win->property("toastItem").value<QObject*>()->property("shown").toBool());
        // For the own person only the explanatory line.
        vm->setSelectedName(kSelf);
        QTest::qWait(20);
        QVERIFY(section->findChild<QQuickItem*>("personTagsSelf")->isVisible());
        QVERIFY(!section->findChild<QQuickItem*>("personTagField")->isVisible());
    }

    // ---- learned track side, overwritable ----

    void defaultSideLearnedAndManual()
    {
        startApp();
        scenario();
        // A second store on the same people.json (the path of learning: SpeakerEditor / PeopleService).
        library::PeopleStore learner(m_app->settings()->paths().dataFile(paths::files::People));
        PeopleViewModel vm(services());
        vm.setSelectedName(kGergely);
        QVERIFY(vm.defaultSideVisible());
        QCOMPARE(vm.defaultSide(), QString());
        QCOMPARE(vm.defaultSideNote(), QString());

        // A manual override is protected from learning.
        vm.setDefaultSide(QStringLiteral("remote"));
        QCOMPARE(vm.defaultSide(), QStringLiteral("remote"));
        QCOMPARE(vm.defaultSideNote(), QStringLiteral("kézzel beállítva"));
        library::PersonRecord r = m_app->peopleService()->person(id(kGergely));
        QCOMPARE(r.defaultSide, QStringLiteral("remote"));
        QVERIFY(r.defaultSideManual);
        QVERIFY(!learner.setDefaultSide(id(kGergely), QStringLiteral("local")));   // learned entry
        QCOMPARE(m_app->peopleService()->person(id(kGergely)).defaultSide, QStringLiteral("remote"));

        // "no data": deleted, learning may write again.
        vm.setDefaultSide(QString());
        QCOMPARE(vm.defaultSide(), QString());
        QVERIFY(learner.setDefaultSide(id(kGergely), QStringLiteral("local")));
        m_app->peopleService()->reload();
        vm.refresh();
        vm.setSelectedName(kGergely);
        QCOMPARE(vm.defaultSide(), QStringLiteral("local"));
        QCOMPARE(vm.defaultSideNote(), QStringLiteral("tanult"));

        // The own person has no such row.
        vm.setSelectedName(kSelf);
        QVERIFY(!vm.defaultSideVisible());
    }

    void defaultSideSegmentInTheWindow()
    {
        QQmlEngine engine;
        setupEngine(engine);
        QQmlComponent comp(&engine);
        comp.loadFromModule("Tanara", "PeopleWindow");
        QVERIFY2(!comp.isError(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> obj(comp.createWithInitialProperties({{"demoState", "P01"}}));
        auto* win = qobject_cast<QQuickWindow*>(obj.get());
        QVERIFY(win);
        win->show();
        QTest::qWait(80);
        auto* vm = qobject_cast<PeopleViewModel*>(win->property("vm").value<QObject*>());
        auto* row = win->findChild<QQuickItem*>("defaultSideRow");
        auto* seg = win->findChild<QQuickItem*>("defaultSideSegment");
        QVERIFY(vm && row && seg && row->isVisible());
        QCOMPARE(seg->property("value").toString(), QStringLiteral("remote"));   // Bárány Gergely: learned
        QVERIFY(QMetaObject::invokeMethod(seg, "picked", Q_ARG(QString, QStringLiteral("local"))));
        QCOMPARE(vm->defaultSide(), QStringLiteral("local"));
        QCOMPARE(vm->defaultSideNote(), QStringLiteral("kézzel beállítva"));
        vm->setSelectedName(QStringLiteral("Kovács Lilla"));
        QTest::qWait(20);
        QVERIFY(!row->isVisible());
    }

    // ---- the "Tags" link scrolls to the Tags section ----

    void hostOpensAtTagsSection()
    {
        startApp();
        scenario();
        PeopleWindowHost host(m_app.get());
        QVERIFY(host.open(kEszter));
        PeopleViewModel* vm = host.viewModel();
        QVERIFY(vm);
        QSignalSpy section(vm, &PeopleViewModel::sectionRequested);
        QVERIFY(host.open(kGergely, QStringLiteral("tags")));
        QCOMPARE(vm->selectedName(), kGergely);
        QCOMPARE(section.size(), 1);
        QCOMPARE(section.first().first().toString(), QStringLiteral("tags"));
        host.closeNow();
    }

    void tagsFocusScrollsAndHighlights()
    {
        QQmlEngine engine;
        setupEngine(engine);
        QQmlComponent comp(&engine);
        comp.loadFromModule("Tanara", "PeopleWindow");
        QVERIFY2(!comp.isError(), qPrintable(comp.errorString()));
        // A low window, so there is something to scroll.
        std::unique_ptr<QObject> obj(comp.createWithInitialProperties({{"demoState", "tagsFocus"}, {"height", 560}}));
        auto* win = qobject_cast<QQuickWindow*>(obj.get());
        QVERIFY(win);
        win->show();
        auto* flash = win->findChild<QQuickItem*>("tagsFlash");
        QVERIFY(flash);
        QTRY_VERIFY(flash->property("opacity").toDouble() > 0.5);
        auto* section = win->findChild<QQuickItem*>("personTagsSection");
        QVERIFY(section && section->isVisible());
        // The section is at the top of the scrolled view (the header scrolled out).
        const QPointF top = section->mapToItem(win->contentItem(), QPointF(0, 0));
        QVERIFY2(top.y() < 60, qPrintable(QString::number(top.y())));
    }

    void demoStatesLoadWithoutWarnings()
    {
        QQmlEngine engine;
        setupEngine(engine);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError>& list) {
            for (const QQmlError& e : list) warnings << e.toString();
        });
        for (const QString& state : {QStringLiteral("P01"), QStringLiteral("P02"), QStringLiteral("P03"),
                                     QStringLiteral("P04"), QStringLiteral("P05"), QStringLiteral("P06"),
                                     QStringLiteral("newPerson"), QStringLiteral("sampleNew"),
                                     QStringLiteral("sampleMove"), QStringLiteral("noResult"),
                                     QStringLiteral("deleteKeep"), QStringLiteral("creating"),
                                     QStringLiteral("tagsSection"), QStringLiteral("tagsSuggested"),
                                     QStringLiteral("tagsSelf"), QStringLiteral("samplesModels"),
                                     QStringLiteral("backfill"), QStringLiteral("tagsFocus")}) {
            QQmlComponent comp(&engine);
            comp.loadFromModule("Tanara", "PeopleWindow");
            QVERIFY2(!comp.isError(), qPrintable(comp.errorString()));
            std::unique_ptr<QObject> obj(comp.createWithInitialProperties({{"demoState", state}}));
            auto* win = qobject_cast<QQuickWindow*>(obj.get());
            QVERIFY(win);
            win->show();
            QTest::qWait(80);
            auto* vm = qobject_cast<PeopleViewModel*>(win->property("vm").value<QObject*>());
            QVERIFY(vm);
            if (state == QLatin1String("P04")) QVERIFY(win->property("mergeDialog").value<QObject*>()->property("visible").toBool());
            if (state == QLatin1String("P05")) QVERIFY(win->property("deleteDialog").value<QObject*>()->property("visible").toBool());
            if (state == QLatin1String("P03")) QVERIFY(win->property("toastItem").value<QObject*>()->property("shown").toBool());
            QVERIFY2(warnings.isEmpty(), qPrintable(state + QStringLiteral(": ") + warnings.join(QLatin1Char('\n'))));
        }
    }
};

QTEST_MAIN(TestPeopleViewModel)
#include "test_people_view_model.moc"
