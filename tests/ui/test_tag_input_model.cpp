// TagInputModel + TagMatching — a címke-beviteli mező (C02) listája.
//  - controller nélkül a kitalált 12 címke: üres mező → legutóbbiak; gépelés → ékezet- és
//    kisbetű-független találatok (név eleje, szókezdet, bárhol) + „Új címke”; pontos egyezésnél
//    nincs „Új”; nagyon hasonló név → „HASONLÓ MÁR VAN” + „Mégis új”;
//  - kijelölés, léptetés, választás (chosen jel), kizárt id-k, limit;
//  - a varrat: ál-backenddel (setBackend) ugyanígy működik, és a készlet változását követi.
#include "AppContext.h"
#include "TagInputModel.h"
#include "TagMatching.h"
#include "test_tag_backend.h"

#include <QSignalSpy>
#include <QtTest>

using namespace tanara_qml;

namespace {

QStringList kinds(const TagInputModel& m)
{
    QStringList out;
    for (const auto& r : m.rowData()) out << r.kind;
    return out;
}
QStringList names(const TagInputModel& m)
{
    QStringList out;
    for (const auto& r : m.rowData()) out << r.name;
    return out;
}

} // namespace

class TestTagInputModel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { AppContext::instance()->setDemo(true); }

    void keysAndNearDuplicates()
    {
        QCOMPARE(tagmatch::key(QStringLiteral("Museum Plus")), QStringLiteral("museumplus"));
        QCOMPARE(tagmatch::key(QStringLiteral("MÉM-MDK")), QStringLiteral("memmdk"));
        QCOMPARE(tagmatch::normalizeName(QStringLiteral("  Q4   tervezés ")), QStringLiteral("Q4 tervezés"));
        QVERIFY(tagmatch::nearDuplicate(QStringLiteral("Museum Plus"), QStringLiteral("MuseumPlus")));
        QVERIFY(tagmatch::nearDuplicate(QStringLiteral("Termek"), QStringLiteral("Termék")));      // ugyanaz a kulcs
        QVERIFY(tagmatch::nearDuplicate(QStringLiteral("Nordvk"), QStringLiteral("Nordvik")));     // 1 hiányzó betű
        QVERIFY(tagmatch::nearDuplicate(QStringLiteral("Nordivk"), QStringLiteral("Nordvik")));    // szomszédos csere
        QVERIFY(!tagmatch::nearDuplicate(QStringLiteral("Belsö"), QStringLiteral("Belga")));
        QVERIFY(!tagmatch::nearDuplicate(QStringLiteral("SLA"), QStringLiteral("SLB")));           // rövid kulcs: csak egyezés
        // ≥ 10 hosszú kulcson 2 eltérés is belefér, rövidebben nem.
        QVERIFY(tagmatch::nearDuplicate(QStringLiteral("Gyujtemenykezeles"), QStringLiteral("Gyüjteménykezelésx")));
        QVERIFY(tagmatch::nearDuplicate(QStringLiteral("Ügyféltámogatás"), QStringLiteral("Ügyféltamogatas")));
        QVERIFY(!tagmatch::nearDuplicate(QStringLiteral("Partner"), QStringLiteral("Partnerek")));
        QCOMPARE(tagmatch::editDistance(QStringLiteral("ab"), QStringLiteral("ba")), 1);
    }

    void emptyFieldShowsRecents()
    {
        TagInputModel m;
        QCOMPARE(m.mode(), QStringLiteral("recent"));
        QCOMPARE(names(m), QStringList({"Nordvik", "Q4 tervezés", "MuseumPlus", "Partnerek"}));
        QCOMPARE(kinds(m), QStringList(4, QStringLiteral("recent")));
        QCOMPARE(m.rowData().first().count, 14);
        QCOMPARE(m.selectedRow(), 0);
        QCOMPARE(m.rows().size(), 4);
    }

    void typingMatchesAccentInsensitive()
    {
        TagInputModel m;
        m.setText(QStringLiteral("mu"));
        QCOMPARE(m.mode(), QStringLiteral("typing"));
        QCOMPARE(names(m), QStringList({"MuseumPlus", "Múzeumi pályázat", "mu"}));
        QCOMPARE(kinds(m), QStringList({"match", "match", "new"}));
        // A kiemelés az eredeti (ékezetes) névben: „Mú”.
        const QVariantMap second = m.rows().at(1).toMap();
        QCOMPARE(second.value("match").toString(), QStringLiteral("Mú"));
        QCOMPARE(second.value("after").toString(), QStringLiteral("zeumi pályázat"));
        QCOMPARE(m.selectedRow(), 0);

        m.setText(QStringLiteral("GYUJT"));
        QCOMPARE(names(m).first(), QStringLiteral("Gyűjteménykezelés"));
        QCOMPARE(m.rows().first().toMap().value("match").toString(), QStringLiteral("Gyűjt"));
    }

    void prefixThenWordStartThenSubstring()
    {
        TagInputModel m;
        // „as”: szókezdet a „Nordvik AS”-ben, szó belsejében az „Ügyféltámogatás” / „Kutatás” végén.
        m.setText(QStringLiteral("as"));
        QCOMPARE(names(m), QStringList({"Nordvik AS", "Ügyféltámogatás", "Kutatás", "as"}));
        m.setText(QStringLiteral("tervez"));
        QCOMPARE(names(m).first(), QStringLiteral("Q4 tervezés"));
    }

    void noMatchSelectsNewRow()
    {
        TagInputModel m;
        m.setText(QStringLiteral("Pilot 2027"));
        QCOMPARE(kinds(m), QStringList({"new"}));
        QCOMPARE(m.selectedRow(), 0);
        QSignalSpy chosen(&m, &TagInputModel::chosen);
        QVERIFY(m.chooseSelected());
        QCOMPARE(chosen.count(), 1);
        QCOMPARE(chosen.at(0).at(0).toString(), QStringLiteral("Pilot 2027"));
        QCOMPARE(chosen.at(0).at(1).toBool(), true);
    }

    void exactMatchOffersNoNewRow()
    {
        TagInputModel m;
        m.setText(QStringLiteral("nordvik"));
        QCOMPARE(names(m), QStringList({"Nordvik", "Nordvik AS"}));
        QVERIFY(!kinds(m).contains(QStringLiteral("new")));
        QVERIFY(!kinds(m).contains(QStringLiteral("forceNew")));
    }

    void nearDuplicateOffersExistingFirst()
    {
        TagInputModel m;
        m.setText(QStringLiteral("Museum Plus"));
        QCOMPARE(m.mode(), QStringLiteral("similar"));
        QCOMPARE(kinds(m), QStringList({"nearDuplicate", "forceNew"}));
        QCOMPARE(names(m), QStringList({"MuseumPlus", "Museum Plus"}));
        QCOMPARE(m.selectedRow(), 0);

        QSignalSpy chosen(&m, &TagInputModel::chosen);
        QVERIFY(m.chooseSelected());
        QCOMPARE(chosen.at(0).at(0).toString(), QStringLiteral("MuseumPlus"));
        QCOMPARE(chosen.at(0).at(1).toBool(), false);
        m.move(1);
        QVERIFY(m.chooseSelected());
        QCOMPARE(chosen.at(1).at(0).toString(), QStringLiteral("Museum Plus"));
        QCOMPARE(chosen.at(1).at(1).toBool(), true);

        // Elírás is „hasonló”.
        m.setText(QStringLiteral("Nordvk"));
        QCOMPARE(kinds(m).first(), QStringLiteral("nearDuplicate"));
        QCOMPARE(names(m).first(), QStringLiteral("Nordvik"));
    }

    void moveClampsAndSelectsRows()
    {
        TagInputModel m;
        m.setText(QStringLiteral("mu"));
        QSignalSpy sel(&m, &TagInputModel::selectedRowChanged);
        m.move(1);
        QCOMPARE(m.selectedRow(), 1);
        m.move(5);
        QCOMPARE(m.selectedRow(), 2);
        m.move(-10);
        QCOMPARE(m.selectedRow(), 0);
        QCOMPARE(sel.count(), 3);
        QSignalSpy chosen(&m, &TagInputModel::chosen);
        QVERIFY(m.choose(1));
        QCOMPARE(chosen.at(0).at(0).toString(), QStringLiteral("Múzeumi pályázat"));
        QVERIFY(!m.choose(9));
        // Új szöveg: a kijelölés visszaáll az első sorra.
        m.move(2);
        m.setText(QStringLiteral("q"));
        QCOMPARE(m.selectedRow(), 0);
    }

    void excludeAndLimit()
    {
        TagInputModel m;
        m.setExcludeIds({QStringLiteral("t-nordvik"), QStringLiteral("t-q4")});
        QCOMPARE(names(m), QStringList({"MuseumPlus", "Partnerek"}));
        m.setText(QStringLiteral("nord"));
        QCOMPARE(names(m), QStringList({"Nordvik AS", "nord"}));
        m.setExcludeIds({});
        m.setLimit(3);
        m.setText(QStringLiteral("e"));
        QCOMPARE(m.rowData().size(), 4);            // 3 találat + „Új címke”
        QCOMPARE(kinds(m).last(), QStringLiteral("new"));
        m.setText(QString());
        QCOMPARE(m.rowData().size(), 3);
    }

    void worksWithInjectedBackend()
    {
        FakeTagBackend backend;
        backend.addItem("a", "Alfa projekt", 4);
        backend.addItem("b", "Béta", 2);
        backend.recentIds = {"b"};
        TagInputModel m;
        m.setBackend(&backend);
        QCOMPARE(names(m), QStringList({"Béta"}));
        m.setText(QStringLiteral("proj"));
        QCOMPARE(names(m), QStringList({"Alfa projekt", "proj"}));
        QSignalSpy rows(&m, &TagInputModel::rowsChanged);
        backend.addItem("c", "Projektzárás", 1);
        emit backend.tagsChanged();
        QCOMPARE(rows.count(), 1);
        QCOMPARE(names(m), QStringList({"Projektzárás", "Alfa projekt", "proj"}));
        // Vissza a saját (demó) backendre.
        m.setBackend(nullptr);
        m.setText(QString());
        QCOMPARE(names(m).first(), QStringLiteral("Nordvik"));
    }
};

QTEST_MAIN(TestTagInputModel)
#include "test_tag_input_model.moc"
