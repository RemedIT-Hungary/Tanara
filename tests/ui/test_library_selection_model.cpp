// LibrarySelectionModel — a könyvtár többes kijelölése és a közös címkézés (C05, T05).
//  - demó-módban (controller nélkül): Ctrl / Shift+kattintás logikája, a panel adatai
//    (kijelöltek, közös / részleges címkék), tömeges felrakás / levétel a mintakönyvtáron;
//  - a QML-oldal: a LibrarySidebar sorain Ctrl / Shift+kattintás jelöl ki, Esc megszünteti,
//    sima kattintás megnyit; a jelölőnégyzetek csak kijelölés közben látszanak;
//  - valódi AppControllerrel, IZOLÁLT TANARA_HOME-ban: a tömeges változás a TagService-en át
//    megy, és EGY visszavonással visszaáll.
#include "AppContext.h"
#include "LibraryDemoData.h"
#include "LibraryListModel.h"
#include "LibrarySelectionModel.h"
#include "QmlApp.h"

#include "tanara/AppController.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/tags/TagService.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>
#include <memory>

using namespace tanara_qml;

class TestLibrarySelectionModel : public QObject {
    Q_OBJECT

    static QVariantMap tagRow(const LibrarySelectionModel& s, const QString& name)
    {
        for (const QVariant& v : s.tagRows())
            if (v.toMap().value("name").toString() == name) return v.toMap();
        return {};
    }

private slots:
    void init()
    {
        AppContext::instance()->setController(nullptr);
        AppContext::instance()->setDemo(true);
        demo::resetTags();
    }

    void ctrlClickStartsWithTheOpenMeeting()
    {
        LibraryListModel lib;
        LibrarySelectionModel s;
        s.setLibrary(&lib);
        s.setCurrentId(QStringLiteral("demo-partner"));
        QSignalSpy changed(&s, &LibrarySelectionModel::selectionChanged);

        s.toggle(QStringLiteral("demo-tamogatas"));
        QCOMPARE(s.count(), 2);
        // A lista sorrendjében (legújabb elöl), nem a kattintás sorrendjében.
        QCOMPARE(s.ids(), (QStringList{QStringLiteral("demo-tamogatas"), QStringLiteral("demo-partner")}));
        QCOMPARE(changed.count(), 1);
        QVERIFY(s.contains(QStringLiteral("demo-partner")));

        // Újabb Ctrl+kattintás hozzáad, a kijelölten elvesz.
        s.toggle(QStringLiteral("demo-belepo"));
        QCOMPARE(s.count(), 3);
        s.toggle(QStringLiteral("demo-tamogatas"));
        QCOMPARE(s.ids(), (QStringList{QStringLiteral("demo-partner"), QStringLiteral("demo-belepo")}));

        s.clear();
        QCOMPARE(s.count(), 0);
        // Megnyitott megbeszélés nélkül egy elemmel indul (a nézet ezt sima megnyitásnak veszi).
        s.setCurrentId(QString());
        s.toggle(QStringLiteral("demo-heti"));
        QCOMPARE(s.ids(), QStringList{QStringLiteral("demo-heti")});
    }

    void shiftClickSelectsRangeFromAnchor()
    {
        LibraryListModel lib;
        LibrarySelectionModel s;
        s.setLibrary(&lib);
        s.setCurrentId(QStringLiteral("demo-tamogatas"));    // 2. sor

        s.rangeTo(QStringLiteral("demo-arazas"));            // 5. sor
        QCOMPARE(s.ids(), (QStringList{QStringLiteral("demo-tamogatas"), QStringLiteral("demo-partner"),
                                       QStringLiteral("demo-bemutato"), QStringLiteral("demo-arazas")}));
        // A horgony marad: visszafelé is onnan számol.
        s.rangeTo(QStringLiteral("demo-heti"));
        QCOMPARE(s.ids(), (QStringList{QStringLiteral("demo-heti"), QStringLiteral("demo-tamogatas")}));
        // Ctrl+kattintás után az új horgony a kattintott elem.
        s.toggle(QStringLiteral("demo-infra"));
        s.rangeTo(QStringLiteral("demo-tervezes"));
        QCOMPARE(s.ids(), (QStringList{QStringLiteral("demo-infra"), QStringLiteral("demo-tervezes")}));
    }

    void panelDataShowsFullAndPartialTags()
    {
        LibraryListModel lib;
        LibrarySelectionModel s;
        s.setLibrary(&lib);
        // A T05 kijelölése.
        s.selectIds({QStringLiteral("demo-belepo"), QStringLiteral("demo-tamogatas"), QStringLiteral("demo-partner")});
        QCOMPARE(s.count(), 3);

        const QVariantList items = s.items();
        QCOMPARE(items.size(), 3);
        QCOMPARE(items.at(0).toMap().value("title").toString(), QStringLiteral("Ügyféltámogatás átadás"));
        QCOMPARE(items.at(0).toMap().value("dateText").toString(), QStringLiteral("okt. 2."));
        QCOMPARE(items.at(1).toMap().value("tagsText").toString(), QStringLiteral("#Nordvik #Partnerek #Q4 tervezés"));

        QCOMPARE(s.tagRows().size(), 3);
        const QVariantMap nordvik = tagRow(s, QStringLiteral("Nordvik"));
        QVERIFY(nordvik.value("full").toBool());
        QCOMPARE(nordvik.value("countText").toString(), QStringLiteral("3/3"));
        QCOMPARE(nordvik.value("note").toString(), QStringLiteral("mindegyiken rajta van"));
        const QVariantMap partnerek = tagRow(s, QStringLiteral("Partnerek"));
        QVERIFY(!partnerek.value("full").toBool());
        QCOMPARE(partnerek.value("countText").toString(), QStringLiteral("1/3"));
        QCOMPARE(partnerek.value("note").toString(), QStringLiteral("csak egyiken · kattintásra mindegyikre"));
        QCOMPARE(tagRow(s, QStringLiteral("Q4 tervezés")).value("note").toString(), QStringLiteral("csak egyiken"));
        QCOMPARE(s.fullTagIds(), QStringList{QStringLiteral("t-nordvik")});
    }

    void demoBulkAddAndRemove()
    {
        LibraryListModel lib;
        LibrarySelectionModel s;
        s.setLibrary(&lib);
        s.selectIds({QStringLiteral("demo-tamogatas"), QStringLiteral("demo-partner"), QStringLiteral("demo-belepo")});

        // A részleges címke kattintásra mindegyikre felkerül (azonosítóval).
        s.addTagToAll(QStringLiteral("t-partnerek"));
        QVERIFY(tagRow(s, QStringLiteral("Partnerek")).value("full").toBool());
        QCOMPARE(demo::tagsOf(QStringLiteral("demo-tamogatas")),
                 (QStringList{QStringLiteral("t-nordvik"), QStringLiteral("t-partnerek")}));
        // A lista sorai is frissülnek.
        const int row = lib.indexOfMeeting(QStringLiteral("demo-tamogatas"));
        QCOMPARE(lib.data(lib.index(row), LibraryListModel::TagNamesRole).toStringList(),
                 (QStringList{QStringLiteral("Nordvik"), QStringLiteral("Partnerek")}));

        // Név szerint: a meglévőt használja (kis-nagybetű / ékezet nem számít) …
        s.addTagToAll(QStringLiteral("belso"));
        QVERIFY(tagRow(s, QStringLiteral("Belső")).value("full").toBool());
        // … ismeretlen névnél új címke.
        s.addTagToAll(QStringLiteral("Pilot 2027"));
        QVERIFY(tagRow(s, QStringLiteral("Pilot 2027")).value("full").toBool());
        QVERIFY(!demo::tagIdByName(QStringLiteral("Pilot 2027")).isEmpty());

        // × → mindegyikről lekerül.
        s.removeTagFromAll(QStringLiteral("t-nordvik"));
        QVERIFY(tagRow(s, QStringLiteral("Nordvik")).isEmpty());
        QVERIFY(!demo::tagsOf(QStringLiteral("demo-partner")).contains(QStringLiteral("t-nordvik")));
        QVERIFY(demo::tagsOf(QStringLiteral("demo-partner")).contains(QStringLiteral("t-q4")));
    }

    void sidebarClicksSelect()
    {
        QQmlEngine engine;
        setupEngine(engine);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError>& list) {
            for (const QQmlError& e : list) warnings << e.toString();
        });
        QQuickWindow window;
        window.resize(276, 1200);
        QQmlComponent comp(&engine);
        comp.loadFromModule("Tanara", "LibrarySidebar");
        QVERIFY2(!comp.isError(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> obj(comp.createWithInitialProperties(
            {{"currentMeetingId", QStringLiteral("demo-partner")}}));
        auto* sidebar = qobject_cast<QQuickItem*>(obj.get());
        QVERIFY(sidebar);
        sidebar->setParentItem(window.contentItem());
        sidebar->setSize(window.size());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(80);
        auto* selection = sidebar->property("selection").value<LibrarySelectionModel*>();
        QVERIFY(selection);

        // A lista sorai (a delegate-ek csak a vizuális fában vannak, a QObject-fában nem).
        std::function<QQuickItem*(QQuickItem*, const QString&)> findRow =
            [&](QQuickItem* parent, const QString& id) -> QQuickItem* {
            for (QQuickItem* it : parent->childItems()) {
                if (it->isVisible() && it->property("meetingId").toString() == id
                    && it->property("selectionMode").isValid())
                    return it;
                if (QQuickItem* hit = findRow(it, id)) return hit;
            }
            return nullptr;
        };
        auto rowCenter = [&](const QString& id) -> QPoint {
            QQuickItem* it = findRow(sidebar, id);
            return it ? it->mapToScene(QPointF(it->width() / 2, it->height() / 2)).toPoint() : QPoint(-1, -1);
        };
        auto checkBoxShown = [&](const QString& id) {
            QQuickItem* it = findRow(sidebar, id);
            return it && it->property("selectionMode").toBool();
        };
        QVERIFY(findRow(sidebar, QStringLiteral("demo-heti")));

        QVERIFY(!checkBoxShown(QStringLiteral("demo-heti")));
        // Ctrl+kattintás: a megnyitott megbeszéléssel együtt kettő; a jelölőnégyzetek megjelennek.
        QTest::mouseClick(&window, Qt::LeftButton, Qt::ControlModifier, rowCenter(QStringLiteral("demo-tamogatas")));
        QCOMPARE(selection->count(), 2);
        QVERIFY(checkBoxShown(QStringLiteral("demo-heti")));
        // Shift+kattintás: tartomány a horgonytól.
        QTest::mouseClick(&window, Qt::LeftButton, Qt::ShiftModifier, rowCenter(QStringLiteral("demo-arazas")));
        QCOMPARE(selection->ids(), (QStringList{QStringLiteral("demo-tamogatas"), QStringLiteral("demo-partner"),
                                                QStringLiteral("demo-bemutato"), QStringLiteral("demo-arazas")}));
        // Esc (a listán) megszünteti.
        QTest::keyClick(&window, Qt::Key_Escape);
        QCOMPARE(selection->count(), 0);
        // Sima kattintás: megnyitás, kijelölés nélkül.
        QTest::mouseClick(&window, Qt::LeftButton, Qt::ControlModifier, rowCenter(QStringLiteral("demo-heti")));
        QCOMPARE(selection->count(), 2);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, rowCenter(QStringLiteral("demo-arazas")));
        QCOMPARE(selection->count(), 0);
        QCOMPARE(sidebar->property("currentMeetingId").toString(), QStringLiteral("demo-arazas"));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
    }

    // ---- valódi controller, izolált TANARA_HOME ----
    void realBulkTaggingIsOneUndoStep()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("TANARA_HOME", home.path().toUtf8());
        qputenv("TANARA_CLOUD", "off");
        AppContext::instance()->setDemo(false);
        {
            tanara::AppController app;
            QStringList ids;
            for (int i = 0; i < 3; ++i) {
                tanara::Meeting m = app.store()->createMeeting(QStringLiteral("Megbeszélés %1").arg(i));
                m.durationMs = 60 * 1000;
                app.store()->saveMeeting(m);
                ids << m.id;
            }
            tanara::TagService* tags = app.tags();
            const tanara::Tag alfa = tags->addTag(ids.at(0), QStringLiteral("Alfa"));

            LibraryListModel lib;
            lib.setController(&app);
            LibrarySelectionModel s;
            s.setController(&app);
            s.setLibrary(&lib);
            QTRY_COMPARE(lib.count(), 3);
            s.selectIds(ids);
            QCOMPARE(s.count(), 3);
            QCOMPARE(tagRow(s, QStringLiteral("Alfa")).value("countText").toString(), QStringLiteral("1/3"));

            // Új név: létrehozás + felrakás mind a háromra — egy lépés.
            s.addTagToAll(QStringLiteral("Béta"));
            const tanara::Tag beta = tags->byName(QStringLiteral("Béta"));
            QVERIFY(beta.isValid());
            for (const QString& id : ids)
                QVERIFY(tags->tagsOf(id).contains(beta.id));
            QTRY_VERIFY(tagRow(s, QStringLiteral("Béta")).value("full").toBool());
            QVERIFY(tags->canUndo());
            tags->undo();
            for (const QString& id : ids)
                QVERIFY(!tags->tagsOf(id).contains(beta.id));
            QVERIFY(tags->tagsOf(ids.at(0)).contains(alfa.id));

            // Részleges → mindegyikre, majd × mindegyikről; mindkettő egy-egy lépés.
            s.addTagToAll(alfa.id);
            for (const QString& id : ids)
                QVERIFY(tags->tagsOf(id).contains(alfa.id));
            s.removeTagFromAll(alfa.id);
            for (const QString& id : ids)
                QVERIFY(!tags->tagsOf(id).contains(alfa.id));
            tags->undo();
            for (const QString& id : ids)
                QVERIFY(tags->tagsOf(id).contains(alfa.id));
            QTRY_VERIFY(tagRow(s, QStringLiteral("Alfa")).value("full").toBool());

            // Törölt megbeszélés kiesik a kijelölésből.
            app.deleteMeeting(ids.at(2));
            QTRY_COMPARE(s.count(), 2);
        }
        qunsetenv("TANARA_HOME");
    }
};

QTEST_MAIN(TestLibrarySelectionModel)
#include "test_library_selection_model.moc"
