// TagsViewModel / TagListModel — a Címkék ablak (C08, T10–T13) nézetmodellje.
//  - controller nélkül a kitalált 12 címke: rendezés, keresés kiemeléssel, a kijelölt címke
//    profilja (résztvevők „12 / 14”, kifejezések, gyakran együtt ≥ 2, megbeszélések);
//  - átnevezés (ütközés, üres név), összevonás (jelöltek, eredmény-szöveg valódi számokkal),
//    törlés (cím névelővel, szöveg) — mind visszavonható, toast-tal;
//  - T13: üres készlet; a varrat: ál-backenddel a hívások és a frissülés.
#include "AppContext.h"
#include "TagListModel.h"
#include "TagsViewModel.h"
#include "test_tag_backend.h"

#include <QSignalSpy>
#include <QtTest>

using namespace tanara_qml;

namespace {

QStringList listNames(TagsViewModel& vm)
{
    QStringList out;
    for (const TagListRow& r : vm.tags()->rows()) out << r.name;
    return out;
}

} // namespace

class TestTagsViewModel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { AppContext::instance()->setDemo(true); }

    void demoListAndDetail()
    {
        TagsViewModel vm;
        vm.setDemoState("T10");
        QCOMPARE(vm.totalCount(), 12);
        QCOMPARE(vm.count(), 12);
        QCOMPARE(vm.countText(), QStringLiteral("12 címke"));
        QCOMPARE(vm.sortLabel(), QStringLiteral("Legutóbb használt"));
        QCOMPARE(listNames(vm).mid(0, 4), QStringList({"Nordvik", "Q4 tervezés", "Termék", "Partnerek"}));
        QCOMPARE(listNames(vm).last(), QStringLiteral("Kutatás"));
        QCOMPARE(vm.tags()->rows().first().meta, QStringLiteral("14 megbeszélés · okt. 2."));
        QCOMPARE(vm.selectedId(), QStringLiteral("t-nordvik"));
        QCOMPARE(vm.selectedRow(), 0);

        const QVariantMap d = vm.detail();
        QCOMPARE(d.value("name").toString(), QStringLiteral("Nordvik"));
        QCOMPARE(d.value("meta").toString(), QStringLiteral("14 megbeszélés · először: márc. 3. · utoljára: okt. 2."));
        const QVariantList people = d.value("participants").toList();
        QCOMPARE(people.size(), 4);
        QCOMPARE(people.at(0).toMap().value("name").toString(), QStringLiteral("Kovács Anna"));
        QCOMPARE(people.at(0).toMap().value("monogram").toString(), QStringLiteral("KA"));
        QCOMPARE(people.at(0).toMap().value("countText").toString(), QStringLiteral("12 / 14"));
        QCOMPARE(d.value("terms").toStringList().size(), 8);
        // A „Nordvik AS” csak egyszer szerepelt vele: nem „gyakran együtt”.
        const QVariantList co = d.value("cooccurring").toList();
        QCOMPARE(co.size(), 3);
        QCOMPARE(co.at(0).toMap().value("name").toString(), QStringLiteral("Partnerek"));
        QCOMPARE(co.at(0).toMap().value("countText").toString(), QStringLiteral("6×"));
        const QVariantList meetings = d.value("meetings").toList();
        QCOMPARE(meetings.size(), 4);
        QCOMPARE(meetings.at(1).toMap().value("dateText").toString(), QStringLiteral("2026-10-01"));
        QCOMPARE(meetings.at(1).toMap().value("durationText").toString(), QStringLiteral("1 ó 16 p"));
        QCOMPARE(d.value("meetingsMeta").toString(), QStringLiteral("14 · legújabb elöl"));

        QSignalSpy detail(&vm, &TagsViewModel::detailChanged);
        vm.setSelectedId("t-museumplus");
        QCOMPARE(detail.count(), 1);
        QCOMPARE(vm.detail().value("name").toString(), QStringLiteral("MuseumPlus"));
        vm.setSelectedId("nincs-ilyen");
        QCOMPARE(vm.selectedId(), QStringLiteral("t-museumplus"));
    }

    void sortAndSearch()
    {
        TagsViewModel vm;
        vm.setSort("count");
        QCOMPARE(listNames(vm).mid(0, 2), QStringList({"Nordvik", "Belső"}));
        QCOMPARE(vm.sortLabel(), QStringLiteral("Leggyakoribb"));
        vm.setSort("alpha");
        QCOMPARE(listNames(vm).first(), QStringLiteral("Belső"));
        QCOMPARE(listNames(vm).last(), QStringLiteral("Ügyféltámogatás"));
        vm.setSort("bogus");
        QCOMPARE(vm.sort(), QStringLiteral("alpha"));

        vm.setFilter("NORD");
        QCOMPARE(listNames(vm), QStringList({"Nordvik", "Nordvik AS"}));
        QCOMPARE(vm.countText(), QStringLiteral("2 / 12 címke"));
        QCOMPARE(vm.tags()->rows().first().match, QStringLiteral("Nord"));
        vm.setFilter("gyujt");
        QCOMPARE(vm.tags()->rows().first().match, QStringLiteral("Gyűjt"));
        QCOMPARE(vm.tags()->rows().first().after, QStringLiteral("eménykezelés"));
        vm.setFilter("xyz");
        QCOMPARE(vm.count(), 0);
        QVERIFY(vm.searching());
    }

    void renameWithUndo()
    {
        TagsViewModel vm;
        vm.setDemoState("T10");
        QSignalSpy toast(&vm, &TagsViewModel::toast);
        QCOMPARE(vm.rename("t-nordvik", "  Nordvik   partner "), QString());
        QCOMPARE(vm.detail().value("name").toString(), QStringLiteral("Nordvik partner"));
        QCOMPARE(toast.count(), 1);
        QCOMPARE(vm.toastText(), QStringLiteral("Átnevezve: #Nordvik → #Nordvik partner"));
        QVERIFY(vm.canUndo());
        QVERIFY(!vm.rename("t-nordvik", "termek").isEmpty());          // a kulcs másé
        QVERIFY(!vm.rename("t-nordvik", "   ").isEmpty());
        QCOMPARE(vm.rename("t-nordvik", "Nordvik partner"), QString()); // változatlan: nincs lépés
        QCOMPARE(toast.count(), 1);
        vm.undo();
        QCOMPARE(vm.detail().value("name").toString(), QStringLiteral("Nordvik"));
    }

    void mergeCandidatesAndResult()
    {
        TagsViewModel vm;
        vm.setDemoState("T11");
        const QVariantList cands = vm.mergeCandidates("t-nordvik");
        QCOMPARE(cands.size(), 2);
        QCOMPARE(cands.at(0).toMap().value("name").toString(), QStringLiteral("Nordvik AS"));
        QCOMPARE(cands.at(0).toMap().value("meta").toString(), QStringLiteral("3 megbeszélés · hasonló név"));
        QCOMPARE(cands.at(1).toMap().value("name").toString(), QStringLiteral("Partnerek"));
        QCOMPARE(cands.at(1).toMap().value("meta").toString(), QStringLiteral("5 megbeszélés · 6× szerepeltek együtt"));

        const QString result = vm.mergeResultText("t-nordvikas", "t-nordvik");
        QVERIFY2(result.contains("<b>16 megbeszélés</b> (1 közös)"), qPrintable(result));
        QVERIFY2(result.contains("A „Nordvik AS” név megszűnik"), qPrintable(result));
        QVERIFY2(result.contains("a mező a #Nordvik címkét ajánlja"), qPrintable(result));

        QSignalSpy toast(&vm, &TagsViewModel::toast);
        QCOMPARE(vm.merge("t-nordvikas", "t-nordvik"), QString());
        QCOMPARE(vm.totalCount(), 11);
        QCOMPARE(vm.selectedId(), QStringLiteral("t-nordvik"));
        QCOMPARE(vm.detail().value("meetingCount").toInt(), 16);
        QCOMPARE(toast.count(), 1);
        QVERIFY(toast.at(0).at(1).toBool());
        QVERIFY(!vm.merge("t-nordvik", "t-nordvik").isEmpty());
        vm.undo();
        QCOMPARE(vm.totalCount(), 12);
        QCOMPARE(vm.detail().value("meetingCount").toInt(), 14);
    }

    void deleteWithArticleAndUndo()
    {
        TagsViewModel vm;
        vm.setDemoState("T12");
        QCOMPARE(vm.deleteTitle("t-nordvik"), QStringLiteral("Törlöd a #Nordvik címkét?"));
        QCOMPARE(vm.deleteTitle("t-ugyfel"), QStringLiteral("Törlöd az #Ügyféltámogatás címkét?"));
        QVERIFY(vm.deleteText("t-nordvik").startsWith(QStringLiteral("14 megbeszélésről lekerül. Maguk a megbeszélések")));
        vm.remove("t-nordvik");
        QCOMPARE(vm.totalCount(), 11);
        QCOMPARE(vm.selectedId(), QStringLiteral("t-q4"));              // a lista első sora
        QCOMPARE(vm.toastText(), QStringLiteral("Címke törölve: #Nordvik"));
        vm.undo();
        QCOMPARE(vm.totalCount(), 12);
        QCOMPARE(listNames(vm).first(), QStringLiteral("Nordvik"));
    }

    void emptyState()
    {
        TagsViewModel vm;
        vm.setDemoState("T13");
        QCOMPARE(vm.totalCount(), 0);
        QVERIFY(!vm.hasSelection());
        QVERIFY(vm.detail().isEmpty());
        QCOMPARE(vm.countText(), QStringLiteral("0 címke"));
    }

    void backendSeam()
    {
        FakeTagBackend backend;
        backend.addItem("a", "Alfa", 2, QDateTime(QDate(2026, 9, 1), QTime(9, 0)));
        backend.addItem("b", "Béta", 7, QDateTime(QDate(2026, 9, 3), QTime(9, 0)));
        TagProfileItem p;
        p.tagId = "b";
        p.participants = {{"Kitalált Ede", 3}};
        p.cooccurring = {{"a", 2}};
        backend.profiles.insert("b", p);

        TagsViewModel vm;
        vm.setBackend(&backend);
        QCOMPARE(listNames(vm), QStringList({"Béta", "Alfa"}));
        QCOMPARE(vm.selectedId(), QStringLiteral("b"));                // az első automatikusan
        QVERIFY(backend.calls.contains("profile:b"));
        QCOMPARE(vm.detail().value("participants").toList().at(0).toMap().value("countText").toString(),
                 QStringLiteral("3 / 7"));

        QCOMPARE(vm.rename("b", "Gamma"), QString());
        QVERIFY(backend.calls.contains("rename:b:Gamma"));
        QCOMPARE(backend.groups.last(), QStringLiteral("Átnevezve: #Béta → #Gamma"));
        QCOMPARE(listNames(vm).first(), QStringLiteral("Gamma"));
        backend.renameOk = false;
        QVERIFY(!vm.rename("b", "Alfa").isEmpty());

        vm.merge("a", "b");
        QVERIFY(backend.calls.contains("merge:a:b"));
        QCOMPARE(vm.totalCount(), 1);
        vm.remove("b");
        QVERIFY(backend.calls.contains("delete:b"));
        QVERIFY(!vm.hasSelection());
        vm.undo();
        QVERIFY(backend.calls.contains("undo"));
    }
};

QTEST_MAIN(TestTagsViewModel)
#include "test_tags_view_model.moc"
