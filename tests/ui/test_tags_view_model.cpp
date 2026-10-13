// TagsViewModel / TagListModel: the view model of the Tags window.
//  - without a controller the made-up 12 tags: sorting, search with highlight, the selected tag's
//    profile (participants "12 / 14", phrases, often together >= 2, meetings);
//  - rename (conflict, empty name), merge (candidates, result text with real numbers),
//    delete (title with an article, text), all undoable, with a toast;
//  - T13: empty set; the seam: calls and refresh with a fake backend;
//  - People list (manual + learned, by the counter): Apply / remove / reject with undo; the
//    delete / merge text with the people; the person picker's tag mode (PersonListModel.tagId).
#include "AppContext.h"
#include "PersonListModel.h"
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
        QCOMPARE(d.value("meta").toString(), QStringLiteral("14 megbeszélés · 5 személy · először: márc. 3. · utoljára: okt. 2."));
        const QVariantList people = d.value("participants").toList();
        QCOMPARE(people.size(), 4);
        QCOMPARE(people.at(0).toMap().value("name").toString(), QStringLiteral("Kovács Anna"));
        QCOMPARE(people.at(0).toMap().value("monogram").toString(), QStringLiteral("KA"));
        QCOMPARE(people.at(0).toMap().value("countText").toString(), QStringLiteral("12 / 14"));
        QCOMPARE(d.value("terms").toStringList().size(), 8);
        // "Nordvik AS" appeared with it only once: not "often together".
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
        vm.setSelectedId("t-muzeo");
        QCOMPARE(detail.count(), 1);
        QCOMPARE(vm.detail().value("name").toString(), QStringLiteral("Muzeo"));
        vm.setSelectedId("nincs-ilyen");
        QCOMPARE(vm.selectedId(), QStringLiteral("t-muzeo"));
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
        QVERIFY(!vm.rename("t-nordvik", "termek").isEmpty());          // the key belongs to another
        QVERIFY(!vm.rename("t-nordvik", "   ").isEmpty());
        QCOMPARE(vm.rename("t-nordvik", "Nordvik partner"), QString()); // unchanged: no step
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
        QVERIFY2(result.contains("A címke 3 személyen is egyesül."), qPrintable(result));

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
        QVERIFY(vm.deleteText("t-nordvik").startsWith(QStringLiteral("14 megbeszélésről és 3 személyről lekerül. Maguk a megbeszélések")));
        QVERIFY(vm.deleteText("t-termek").startsWith(QStringLiteral("8 megbeszélésről lekerül. Maguk")));
        vm.remove("t-nordvik");
        QCOMPARE(vm.totalCount(), 11);
        QCOMPARE(vm.selectedId(), QStringLiteral("t-q4"));              // the list's first row
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
        QCOMPARE(vm.selectedId(), QStringLiteral("b"));                // the first automatically
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

    void peopleListStates()
    {
        TagsViewModel vm;
        vm.setDemoState("people");
        auto people = [&] { return vm.detail().value("people").toList(); };
        auto row = [&](const QString& name) {
            for (const QVariant& v : people())
                if (v.toMap().value("name").toString() == name) return v.toMap();
            return QVariantMap();
        };
        // By the counter, applied and learned together; the own person (14 / 14) is left out.
        QStringList names;
        for (const QVariant& v : people()) names << v.toMap().value("name").toString();
        QCOMPARE(names, QStringList({"Fehér Gábor", "Varga Árpád", "Szabó Bence", "Bárány Gergely", "Molnár Eszter"}));
        QCOMPARE(row("Fehér Gábor").value("countText").toString(), QStringLiteral("11 / 14"));
        QVERIFY(!row("Fehér Gábor").value("manual").toBool());
        QVERIFY(row("Varga Árpád").value("manual").toBool());
        QCOMPARE(row("Molnár Eszter").value("countText").toString(), QStringLiteral("0 / 14"));
        QCOMPARE(row("Fehér Gábor").value("monogram").toString(), QStringLiteral("FG"));

        // Apply: becomes manual, one undoable step.
        QSignalSpy toast(&vm, &TagsViewModel::toast);
        vm.addPerson("Fehér Gábor");
        QVERIFY(row("Fehér Gábor").value("manual").toBool());
        QCOMPARE(toast.count(), 1);
        QVERIFY(toast.last().at(1).toBool());
        QVERIFY(vm.deleteText("t-nordvik").startsWith(QStringLiteral("14 megbeszélésről és 4 személyről lekerül.")));
        vm.undo();
        QVERIFY(!row("Fehér Gábor").value("manual").toBool());

        // x: the learned row disappears (rejection), the applied one can be removed.
        vm.rejectPerson("Bárány Gergely");
        QVERIFY(row("Bárány Gergely").isEmpty());
        vm.removePerson("Molnár Eszter");
        QVERIFY(row("Molnár Eszter").isEmpty());           // 0 in common: not kept even as learned
        QCOMPARE(people().size(), 3);
        QVERIFY(vm.detail().value("meta").toString().contains("3 személy"));
        vm.undo();
        vm.undo();
        QCOMPARE(people().size(), 5);

        // Empty: an explanatory text with the tag's name (with an article).
        vm.setDemoState("peopleEmpty");
        QCOMPARE(vm.selectedId(), QStringLiteral("t-kutatas"));
        QVERIFY(people().isEmpty());
        QCOMPARE(vm.detail().value("emptyPeopleText").toString(),
                 QStringLiteral("Még senki. Ha a #Kutatás megbeszéléseken elnevezed a beszélőket, itt megjelennek."));
        QCOMPARE(vm.detail().value("meta").toString(), QStringLiteral("2 megbeszélés · először: jan. 20. · utoljára: ápr. 14."));
    }

    void personPickerTagMode()
    {
        // Without a controller: the demo name list; people who have it by hand and the own person are
        // left out, those with a learned link come first with the "#Nordvik, 11 / 14, 16 meetings" row.
        PersonListModel model;
        model.setTagId("t-nordvik");
        QStringList names, hints, groups;
        for (int i = 0; i < model.count(); ++i) {
            const QModelIndex ix = model.index(i);
            names << model.data(ix, PersonListModel::NameRole).toString();
            hints << model.data(ix, PersonListModel::TagHintRole).toString();
            groups << model.data(ix, PersonListModel::GroupRole).toString();
        }
        QCOMPARE(names, QStringList({"Fehér Gábor", "Bárány Gergely", "Dénes Zsófia", "Gál Bence", "Tóth Eszter"}));
        QCOMPARE(hints.at(0), QStringLiteral("#Nordvik · 11 / 14 · 16 megbeszélés"));
        QCOMPARE(hints.at(1), QStringLiteral("#Nordvik · 5 / 14 · 23 megbeszélés"));
        QCOMPARE(hints.at(2), QString());
        QCOMPARE(groups, QStringList({"tag", "tag", "other", "other", "other"}));
        // 5 rows: no section title (needs >= 6).
        QCOMPARE(model.tagSectionTitle(), QString());
        // In tag mode a new person is not created.
        model.setQuery("Új Ember");
        QVERIFY(!model.canCreate());
        model.setQuery("");
        model.setTagId("t-q4");
        QCOMPARE(model.tagSectionTitle(), QStringLiteral("A #Q4 tervezés alapján"));
        QCOMPARE(model.data(model.index(0), PersonListModel::NameRole).toString(), QStringLiteral("Fehér Gábor"));
    }
};

QTEST_MAIN(TestTagsViewModel)
#include "test_tags_view_model.moc"
