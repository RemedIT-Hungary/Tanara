//
// PeopleViewModel / PeopleListModel / PeopleWindowHost — a Személyek ablak nézetmodelljei.
//  - controller nélkül: kitalált demó-személyek (P01–P06), a műveletek nem tesznek semmit;
//  - valódi AppControllerrel, IZOLÁLT TANARA_HOME-ban (QTemporaryDir), kitalált nevekkel:
//    a lista azonnal megjelenik és a statisztika utólag töltődik be, keresés (becenév,
//    kiemelés), rendezés, szakaszok, a műveletek az értesítéseikkel és a visszavonással,
//    az összevonás / törlés szövegei valódi számokkal, minta-lejátszás (néma motor),
//    hanglenyomat a megbeszélésekből (hamis embedder), 80+ személy;
//  - a QML-ablak a gazdáján át: megnyitás személy-kijelöléssel, a demó-állapotok
//    figyelmeztetés nélkül töltődnek be.
// Valódi adathoz, hangkimenethez, modellhez nem nyúl.
//
#include "AppContext.h"
#include "PeopleListModel.h"
#include "PeopleViewModel.h"
#include "PeopleWindowHost.h"
#include "QmlApp.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/edit/UtteranceEmbeddings.h"
#include "tanara/people/PeopleService.h"
#include "tanara/people/PeopleStats.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/VoiceprintStore.h"

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

class FakeEmbedder : public IUtteranceEmbedder {
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
    std::unique_ptr<AppController> m_app;

    void startApp()
    {
        m_home = std::make_unique<QTemporaryDir>();
        QVERIFY(m_home->isValid());
        qputenv("TANARA_HOME", m_home->path().toUtf8());
        m_app = std::make_unique<AppController>();
        QVERIFY(m_app->settings()->settings().audioDir.startsWith(m_home->path()));
        m_app->setUserSpeakerName(kSelf);
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
        m.speakerMap = speakerMap;
        m.mixdownFile = QStringLiteral("mixdown.mp3");
        m_app->store()->saveMeeting(m);
        for (const QString& name : speakerMap) m_app->peopleService()->addPerson(name);
        return m_app->store()->load(m.id);
    }

    // Három megbeszélés, négy személy: Gergely (2 megbeszélés, 2 minta), Gergő (1, 1 minta),
    // Eszter (2, minta nélkül), és a saját személy.
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
        m_app->voiceprints()->addPrint(kGergely, print("g1", {1, 0, 0}, a.id));
        m_app->voiceprints()->addPrint(kGergely, print("g2", {0, 1, 0}, b.id));
        m_app->voiceprints()->addPrint(kGergo, print("o1", {0.6f, 0.8f, 0}, a.id));
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

    // ---- demó (controller nélkül) ----

    void demoServesFictionalPeople()
    {
        PeopleViewModel vm;
        QCOMPARE(vm.totalCount(), 14);
        QCOMPARE(vm.countText(), QStringLiteral("14 személy"));
        QVERIFY(vm.statsReady());
        QCOMPARE(vm.people()->nameAt(0), kSelf);                // a saját személy elöl
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
        QVERIFY(roleList(vm.people(), PeopleListModel::HeaderRole).join("").isEmpty());   // keresés közben nincs szakasz

        vm.setDemoState("P03");
        QCOMPARE(vm.selectedName(), kEszter);
        QCOMPARE(vm.sampleCount(), 0);
        QVERIFY(vm.planReady());
        QVERIFY(vm.planPossible());
        QCOMPARE(vm.planButtonText(), QStringLiteral("Minta 3 megbeszélésből"));

        vm.setDemoState("P06");
        QVERIFY(vm.onlySelf());
        QCOMPARE(vm.totalCount(), 1);

        // A demóban a műveletek nem tesznek semmit (és nem omlanak össze).
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

    // ---- valódi controllerrel ----

    void listAppearsImmediatelyAndStatsFillIn()
    {
        startApp();
        scenario();
        PeopleViewModel vm;
        QSignalSpy listChanged(&vm, &PeopleViewModel::listChanged);
        vm.setController(m_app.get());
        // Azonnal: nevek + mintaszám, megbeszélés-szám még nincs.
        QCOMPARE(vm.totalCount(), 4);
        QVERIFY(!vm.statsReady());
        QCOMPARE(vm.selectedName(), kSelf);
        const int row = vm.people()->indexOfName(kGergely);
        QCOMPARE(vm.people()->data(vm.people()->index(row), PeopleListModel::MetaRole).toString(),
                 QStringLiteral("2 minta"));
        QSignalSpy reset(vm.people(), &QAbstractItemModel::modelReset);

        vm.refresh();                                   // az ablak megnyitása: számolás a háttérben
        QVERIFY(waitFor([&] { return vm.statsReady(); }));
        QCOMPARE(vm.people()->data(vm.people()->index(row), PeopleListModel::MetaRole).toString(),
                 QStringLiteral("2 megbeszélés · 2 minta"));
        QCOMPARE(reset.count(), 0);                     // a lista nem ugrik: csak a sorok frissültek
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
        QCOMPARE(sample.value("meetingTitle").toString(), QStringLiteral("Heti egyeztetés"));   // legújabb elöl
        QCOMPARE(sample.value("date").toString(), QStringLiteral("2026-10-01"));
        QCOMPARE(sample.value("label").toString(), QStringLiteral("A megbeszélés lekevert hangja"));
        QVERIFY(sample.value("playable").toBool());
    }

    void searchSortAndSections()
    {
        startApp();
        scenario();
        m_app->peopleService()->addAlias(kEszter, "Gergő felesége");
        m_app->peopleStats()->refreshNow();
        PeopleViewModel vm;
        vm.setController(m_app.get());
        QVERIFY(vm.statsReady());

        // ABC: „Te”, utána kezdőbetűk (Á az A-hoz); a saját személy mindig elöl.
        QCOMPARE(roleList(vm.people(), PeopleListModel::NameRole), QStringList({kSelf, kGergo, kGergely, kEszter}));
        QCOMPARE(roleList(vm.people(), PeopleListModel::HeaderRole), QStringList({"Te", "B", "", "T"}));
        QCOMPARE(vm.sortLabel(), QStringLiteral("ABC"));

        vm.setSort("meetings");
        QCOMPARE(roleList(vm.people(), PeopleListModel::NameRole), QStringList({kSelf, kGergely, kEszter, kGergo}));
        QCOMPARE(roleList(vm.people(), PeopleListModel::HeaderRole), QStringList({"Te", "Többiek", "", ""}));
        vm.setSort("recent");
        QCOMPARE(vm.people()->nameAt(3), kGergo);       // a legrégebben látott a végén
        vm.setSort("abc");

        // Keresés: ékezet- és kisbetű-független, névben és becenévben; a névbeli találat kiemelve.
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
        // Mély hivatkozás: a kijelölés a keresőt is törli, ha a személy nem látszana.
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
        PeopleViewModel vm;
        vm.setController(m_app.get());
        QSignalSpy toast(&vm, &PeopleViewModel::toast);

        // Új személy: felvéve és kijelölve; létező névnél hiba + a meglévő kijelölve.
        QCOMPARE(vm.addPerson("Gál Bence"), QString());
        QCOMPARE(vm.selectedName(), QStringLiteral("Gál Bence"));
        QVERIFY(!vm.onlySelf());
        QVERIFY(!vm.addPerson("bárány gergely").isEmpty());
        QCOMPARE(vm.selectedName(), kGergely);

        // Átnevezés: a régi név becenév; minden megbeszélés követi; visszavonható.
        QVERIFY(!vm.rename(kGergo).isEmpty());                 // létező név: összevonás kell
        QCOMPARE(vm.rename("Bárány Gergő"), QString());
        QCOMPARE(vm.selectedName(), QStringLiteral("Bárány Gergő"));
        QCOMPARE(vm.aliases(), QStringList{kGergely});
        QCOMPARE(m_app->store()->load(a.id).speakerMap.value("Beszélő 1"), QStringLiteral("Bárány Gergő"));
        QCOMPARE(toast.last().at(0).toString(), QStringLiteral("Átnevezve: Bárány Gergő"));
        QCOMPARE(toast.last().at(1).toBool(), true);
        QVERIFY(vm.canUndo());
        vm.undo();
        QCOMPARE(vm.selectedName(), kGergely);
        QVERIFY(vm.aliases().isEmpty());
        QCOMPARE(m_app->store()->load(a.id).speakerMap.value("Beszélő 1"), kGergely);
        QCOMPARE(toast.last().at(1).toBool(), false);
        QVERIFY(!vm.canUndo());

        // Becenevek.
        QCOMPARE(vm.addAlias("Gergely"), QString());
        QCOMPARE(vm.addAlias("G. Bárány"), QString());
        QVERIFY(!vm.addAlias("gergely").isEmpty());
        vm.removeAlias("Gergely");
        QCOMPARE(vm.aliases(), QStringList{"G. Bárány"});
        QCOMPARE(toast.last().at(0).toString(), QStringLiteral("Becenév törölve: Gergely"));
        QCOMPARE(toast.last().at(1).toBool(), true);
        vm.undo();
        QCOMPARE(vm.aliases(), QStringList({"Gergely", "G. Bárány"}));

        // Megjegyzés: mentve; személyváltás után is megvan.
        vm.setNote("Northwind oldali projektvezető.");
        vm.setSelectedName(kEszter);
        QCOMPARE(vm.note(), QString());
        vm.setSelectedName(kGergely);
        QCOMPARE(vm.note(), QStringLiteral("Northwind oldali projektvezető."));

        // A saját személy átnevezése = a „Saját neved” beállítás.
        vm.setSelectedName(kSelf);
        QVERIFY(vm.selectedIsSelf());
        QCOMPARE(vm.rename("Kovács-Nagy Lilla"), QString());
        QCOMPARE(m_app->settings()->settings().userSpeakerName, QStringLiteral("Kovács-Nagy Lilla"));
        QVERIFY(vm.selectedIsSelf());
        QVERIFY(!vm.deletePerson(false).isEmpty());             // a saját személy nem törölhető
    }

    void sampleOperationsWithUndo()
    {
        startApp();
        Meeting a, b;
        scenario(&a, &b);
        m_app->peopleStats()->refreshNow();
        PeopleViewModel vm;
        vm.setController(m_app.get());
        vm.setSelectedName(kGergely);
        QSignalSpy toast(&vm, &PeopleViewModel::toast);

        // Törlés + visszavonás.
        vm.removeSample("g1");
        QCOMPARE(vm.sampleCount(), 1);
        QCOMPARE(toast.last().at(0).toString(), QStringLiteral("Minta törölve: Partnerdemó – adatimport"));
        QCOMPARE(toast.last().at(1).toBool(), true);
        vm.undo();
        QCOMPARE(vm.sampleCount(), 2);
        QCOMPARE(vm.selectedName(), kGergely);

        // Áthelyezés létező személyhez: a forrás-megbeszélés összefoglalója elavul.
        QVERIFY(!vm.moveSample("g1", kGergely).isEmpty());      // már nála van
        QCOMPARE(vm.moveSample("g1", kGergo), QString());
        QCOMPARE(vm.sampleCount(), 1);
        QCOMPARE(m_app->voiceprints()->printCount(kGergo), 2);
        QCOMPARE(toast.last().at(0).toString(), QStringLiteral("Minta áthelyezve ide: B. Gergő"));
        QVERIFY(m_app->summaryStale(a.id).stale);
        QVERIFY(!m_app->summaryStale(b.id).stale);
        vm.undo();
        QCOMPARE(vm.sampleCount(), 2);
        QVERIFY(!m_app->summaryStale(a.id).stale);

        // Új személy ebből a mintából + visszavonás (a létrejött személy eltűnik).
        QCOMPARE(vm.moveSample("g2", "Új Ember"), QString());
        QCOMPARE(toast.last().at(0).toString(), QStringLiteral("Új személy a mintából: Új Ember"));
        QVERIFY(vm.personExists("Új Ember"));
        QCOMPARE(vm.totalCount(), 5);
        vm.undo();
        QVERIFY(!vm.personExists("Új Ember"));
        QCOMPARE(vm.sampleCount(), 2);

        // A választó-lista: a kijelölt nélkül, becenévre is talál.
        m_app->peopleService()->addAlias(kEszter, "Eszti");
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
        PeopleViewModel vm;
        vm.setController(m_app.get());
        vm.setSelectedName(kGergely);
        QSignalSpy playing(&vm, &PeopleViewModel::playingChanged);

        vm.toggleSample("g1");                           // a minta 1000–1400 ms: 0,4 mp
        QCOMPARE(vm.playingSampleId(), QStringLiteral("g1"));
        vm.toggleSample("g2");                           // másik minta: az előző leáll
        QCOMPARE(vm.playingSampleId(), QStringLiteral("g2"));
        vm.toggleSample("g2");                           // ugyanarra: szünet
        QCOMPARE(vm.playingSampleId(), QString());
        vm.toggleSample("g1");
        QVERIFY(waitFor([&] { return vm.playingSampleId().isEmpty(); }, 3000));   // a végén magától megáll
        vm.toggleSample("g1");
        vm.removeSample("g1");                           // a lejátszott minta törlése: leáll
        QCOMPARE(vm.playingSampleId(), QString());

        // Hiányzó hangfájl: nem indul, értesít.
        QSignalSpy toast(&vm, &PeopleViewModel::toast);
        m_app->voiceprints()->addPrint(kGergely, print("gone", {1, 0, 0}, "torolt"));
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
        PeopleViewModel vm;
        vm.setController(m_app.get());
        vm.setSelectedName(kGergely);

        // Hang-hasonlóság szerint; százalék csak annál, akinek van lenyomata.
        const QVariantList cands = vm.mergeCandidates("");
        QCOMPARE(cands.size(), 3);
        QCOMPARE(cands[0].toMap().value("name").toString(), kGergo);
        QCOMPARE(cands[0].toMap().value("similarity").toInt(), 80);
        QVERIFY(cands[0].toMap().value("meta").toString().contains(QStringLiteral("hang alapján hasonló (80%)")));
        QCOMPARE(cands[1].toMap().value("similarity").toInt(), -1);
        QVERIFY(!cands[1].toMap().value("meta").toString().contains(QLatin1Char('%')));
        QCOMPARE(vm.mergeCandidates("eszt").size(), 1);

        // Az eredmény-sor a tényleges számokkal; a megszűnő név a választástól függ.
        const QString keep = vm.mergeResultText(kGergo, true);
        QVERIFY2(keep.contains(QStringLiteral("<b>3 megbeszélés</b>, <b>3 minta</b>. „B. Gergő” becenév lesz.")), qPrintable(keep));
        QVERIFY(!keep.contains(QStringLiteral("összefoglaló")));          // Gergő megbeszélésén nincs összefoglaló
        QVERIFY(keep.contains(QStringLiteral("nem vonható vissza")));
        const QString other = vm.mergeResultText(kGergo, false);
        QVERIFY(other.contains(QStringLiteral("„Bárány Gergely” becenév lesz.")));
        QVERIFY2(other.contains(QStringLiteral("2 érintett összefoglaló elavultnak jelölődik.")), qPrintable(other));

        QSignalSpy toast(&vm, &PeopleViewModel::toast);
        QCOMPARE(vm.merge(kGergo, false), QString());                     // a másik név marad
        QCOMPARE(vm.selectedName(), kGergo);
        QCOMPARE(vm.totalCount(), 3);
        QCOMPARE(vm.aliases(), QStringList{kGergely});
        QCOMPARE(vm.sampleCount(), 3);
        QCOMPARE(vm.meetingCount(), 3);
        QCOMPARE(toast.last().at(0).toString(), QStringLiteral("Összevonva: B. Gergő — 3 megbeszélés, 3 minta"));
        QCOMPARE(toast.last().at(1).toBool(), false);                     // nem visszavonható
        QVERIFY(!vm.canUndo());
    }

    void deleteTextAndKeepSamples()
    {
        startApp();
        Meeting a;
        scenario(&a);
        m_app->peopleStats()->refreshNow();
        PeopleViewModel vm;
        vm.setController(m_app.get());
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
        QCOMPARE(vm.selectedName(), QStringLiteral("Névtelen 1"));       // a megtartott minták gazdája
        QCOMPARE(vm.sampleCount(), 2);
        QCOMPARE(toast.last().at(0).toString(), QStringLiteral("Törölve: Bárány Gergely. A mintái itt maradtak: Névtelen 1"));
        QVERIFY(m_app->summaryStale(a.id).stale);

        // Minta nélküli személy: nincs mit megtartani; törlés után a helyén álló sor a kijelölt.
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
        PeopleViewModel vm;
        vm.setController(m_app.get());
        vm.setSelectedName(kEszter);

        // Hang-modell nélkül: a szöveg megmondja, gomb nincs.
        QVERIFY(waitFor([&] { return vm.planReady(); }));
        QVERIFY(!vm.planPossible());
        QVERIFY(vm.planText().contains(QStringLiteral("nincs telepítve")));

        m_app->peopleService()->setEmbedderFactory([] { return std::make_unique<FakeEmbedder>(); });
        vm.setSelectedName(kGergely);
        vm.setSelectedName(kEszter);
        QVERIFY(waitFor([&] { return vm.planReady() && vm.planPossible(); }));
        // Őszintén: 2 megbeszélésen vannak sorai, de csak az egyiken elég a hosszú, biztos sor.
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
        QVERIFY(!vm.planPossible());                    // már van hanglenyomata

        // Akinek egy megbeszélésen sincs sora.
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
        PeopleViewModel vm;
        vm.setController(m_app.get());
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
        PeopleViewModel vm;
        vm.setController(m_app.get());
        // Más ablakból (átirat-szerkesztő, Beállítások) jövő változás: a lista magától követi.
        m_app->renamePerson(kGergo, "Balla Gergő");
        QVERIFY(waitFor([&] { return vm.personExists("Balla Gergő"); }));
        QVERIFY(!vm.personExists(kGergo));
        m_app->setUserSpeakerName("Kovács L.");
        QVERIFY(waitFor([&] { return vm.people()->nameAt(0) == QStringLiteral("Kovács L."); }));
        m_app->voiceprints()->removePerson("Balla Gergő");
        emit m_app->voiceprintsChanged();
        const int row = vm.people()->indexOfName("Balla Gergő");
        QVERIFY(waitFor([&] {
            return !vm.people()->data(vm.people()->index(row), PeopleListModel::HasVoiceprintRole).toBool();
        }));
    }

    // ---- az ablak ----

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
        QCOMPARE(vm->controllerObject(), m_app.get());
        QCOMPARE(vm->selectedName(), kEszter);
        QVERIFY(waitFor([&] { return vm->statsReady(); }));       // megnyitáskor a háttérben számol
        QCOMPARE(vm->meetingCount(), 2);
        QCOMPARE(host.window()->title(), QStringLiteral("Személyek"));

        // A megjegyzés a mező elhagyásakor (itt: az ablak bezárásakor) mentődik.
        auto* note = host.window()->findChild<QQuickItem*>("noteArea");
        QVERIFY(note);
        note->setProperty("text", QStringLiteral("Pénzügy"));
        // Nyitott ablaknál újabb kérés név nélkül: a kijelölés marad.
        QVERIFY(host.open(QString()));
        QCOMPARE(vm->selectedName(), kEszter);
        host.window()->close();
        QVERIFY(waitFor([&] { return !host.isVisible(); }));
        QCOMPARE(closed.size(), 1);
        QCOMPARE(m_app->peopleService()->person(kEszter).note, QStringLiteral("Pénzügy"));

        QVERIFY(host.open(kGergely));
        QCOMPARE(vm->selectedName(), kGergely);
        host.closeNow();
        QVERIFY(!host.isVisible());
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
                                     QStringLiteral("deleteKeep"), QStringLiteral("creating")}) {
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
