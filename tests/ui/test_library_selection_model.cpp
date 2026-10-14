// LibrarySelectionModel: the library's multi-selection and shared tagging.
//  - in demo mode (no controller): the Ctrl / Shift+click logic, the panel data (selected
//    meetings, shared / partial tags), bulk add / remove on the sample library;
//  - the QML side: Ctrl / Shift+click on LibrarySidebar rows selects, Esc clears, a plain click
//    opens; the checkboxes show only while selecting;
//  - with a real AppController in an ISOLATED TANARA_HOME: a bulk change goes through
//    TagService and is undone by ONE undo step.
#include "AppContext.h"
#include "FacadeTestSupport.h"
#include "demo/LibraryDemoData.h"
#include "LibraryListModel.h"
#include "LibrarySelectionModel.h"
#include "QmlApp.h"

#include "tanara/app/AppController.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/tags/TagService.h"

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
        tanara_demo::resetTags();
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
        // In list order (newest first), not in click order.
        QCOMPARE(s.ids(), (QStringList{QStringLiteral("demo-tamogatas"), QStringLiteral("demo-partner")}));
        QCOMPARE(changed.count(), 1);
        QVERIFY(s.contains(QStringLiteral("demo-partner")));

        // Another Ctrl+click adds; on a selected one it removes.
        s.toggle(QStringLiteral("demo-belepo"));
        QCOMPARE(s.count(), 3);
        s.toggle(QStringLiteral("demo-tamogatas"));
        QCOMPARE(s.ids(), (QStringList{QStringLiteral("demo-partner"), QStringLiteral("demo-belepo")}));

        s.clear();
        QCOMPARE(s.count(), 0);
        // With no meeting open it starts with one item (the view treats this as a plain open).
        s.setCurrentId(QString());
        s.toggle(QStringLiteral("demo-heti"));
        QCOMPARE(s.ids(), QStringList{QStringLiteral("demo-heti")});
    }

    void shiftClickSelectsRangeFromAnchor()
    {
        LibraryListModel lib;
        LibrarySelectionModel s;
        s.setLibrary(&lib);
        s.setCurrentId(QStringLiteral("demo-tamogatas"));    // row 2

        s.rangeTo(QStringLiteral("demo-arazas"));            // row 5
        QCOMPARE(s.ids(), (QStringList{QStringLiteral("demo-tamogatas"), QStringLiteral("demo-partner"),
                                       QStringLiteral("demo-bemutato"), QStringLiteral("demo-arazas")}));
        // The anchor stays: going backwards counts from it too.
        s.rangeTo(QStringLiteral("demo-heti"));
        QCOMPARE(s.ids(), (QStringList{QStringLiteral("demo-heti"), QStringLiteral("demo-tamogatas")}));
        // After a Ctrl+click the new anchor is the clicked item.
        s.toggle(QStringLiteral("demo-infra"));
        s.rangeTo(QStringLiteral("demo-tervezes"));
        QCOMPARE(s.ids(), (QStringList{QStringLiteral("demo-infra"), QStringLiteral("demo-tervezes")}));
    }

    void panelDataShowsFullAndPartialTags()
    {
        LibraryListModel lib;
        LibrarySelectionModel s;
        s.setLibrary(&lib);
        // The selection from the design sample.
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

        // A partial tag is added to all of them on click (with its id).
        s.addTagToAll(QStringLiteral("t-partnerek"));
        QTRY_VERIFY(tagRow(s, QStringLiteral("Partnerek")).value("full").toBool());   // the list refreshes on the next turn
        QCOMPARE(tanara_demo::tagsOf(QStringLiteral("demo-tamogatas")),
                 (QStringList{QStringLiteral("t-nordvik"), QStringLiteral("t-partnerek")}));
        // The list rows update too.
        const int row = lib.indexOfMeeting(QStringLiteral("demo-tamogatas"));
        QTRY_COMPARE(lib.data(lib.index(row), LibraryListModel::TagNamesRole).toStringList(),
                     (QStringList{QStringLiteral("Nordvik"), QStringLiteral("Partnerek")}));

        // By name: uses the existing one (case / accents do not matter) ...
        s.addTagToAll(QStringLiteral("belso"));
        QTRY_VERIFY(tagRow(s, QStringLiteral("Belső")).value("full").toBool());
        // ... a new tag for an unknown name.
        s.addTagToAll(QStringLiteral("Pilot 2027"));
        QTRY_VERIFY(tagRow(s, QStringLiteral("Pilot 2027")).value("full").toBool());
        QVERIFY(!tanara_demo::tagIdByName(QStringLiteral("Pilot 2027")).isEmpty());

        // x removes it from all of them.
        s.removeTagFromAll(QStringLiteral("t-nordvik"));
        QTRY_VERIFY(tagRow(s, QStringLiteral("Nordvik")).isEmpty());
        QVERIFY(!tanara_demo::tagsOf(QStringLiteral("demo-partner")).contains(QStringLiteral("t-nordvik")));
        QVERIFY(tanara_demo::tagsOf(QStringLiteral("demo-partner")).contains(QStringLiteral("t-q4")));
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

        // The list rows (delegates are only in the visual tree, not in the QObject tree).
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
        // Ctrl+click: together with the open meeting that makes two; the checkboxes appear.
        QTest::mouseClick(&window, Qt::LeftButton, Qt::ControlModifier, rowCenter(QStringLiteral("demo-tamogatas")));
        QCOMPARE(selection->count(), 2);
        QVERIFY(checkBoxShown(QStringLiteral("demo-heti")));
        // Shift+click: a range from the anchor.
        QTest::mouseClick(&window, Qt::LeftButton, Qt::ShiftModifier, rowCenter(QStringLiteral("demo-arazas")));
        QCOMPARE(selection->ids(), (QStringList{QStringLiteral("demo-tamogatas"), QStringLiteral("demo-partner"),
                                                QStringLiteral("demo-bemutato"), QStringLiteral("demo-arazas")}));
        // Esc (on the list) clears.
        QTest::keyClick(&window, Qt::Key_Escape);
        QCOMPARE(selection->count(), 0);
        // Plain click: opens, without selecting.
        QTest::mouseClick(&window, Qt::LeftButton, Qt::ControlModifier, rowCenter(QStringLiteral("demo-heti")));
        QCOMPARE(selection->count(), 2);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, rowCenter(QStringLiteral("demo-arazas")));
        QCOMPARE(selection->count(), 0);
        QCOMPARE(sidebar->property("currentMeetingId").toString(), QStringLiteral("demo-arazas"));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
    }

    // ---- real controller, isolated TANARA_HOME ----
    void realBulkTaggingIsOneUndoStep()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("TANARA_HOME", home.path().toUtf8());
        qputenv("TANARA_CLOUD", "off");
        AppContext::instance()->setDemo(false);
        {
            tanara::app::AppController app;
            QStringList ids;
            for (int i = 0; i < 3; ++i) {
                tanara::Meeting m = app.store()->createMeeting(QStringLiteral("Megbeszélés %1").arg(i));
                m.durationMs = 60 * 1000;
                app.store()->saveMeeting(m);
                ids << m.id;
            }
            tanara::library::TagService* tags = app.tags();
            const tanara::Tag alfa = tags->addTag(ids.at(0), QStringLiteral("Alfa"));

            LibraryListModel lib(facadetest::libraryOf(&app));
            LibrarySelectionModel s(facadetest::libraryOf(&app));
            s.setLibrary(&lib);
            QTRY_COMPARE(lib.count(), 3);
            s.selectIds(ids);
            QCOMPARE(s.count(), 3);
            QCOMPARE(tagRow(s, QStringLiteral("Alfa")).value("countText").toString(), QStringLiteral("1/3"));

            // New name: create + add to all three, in one step.
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

            // Partial: added to all, then x removes from all; each is one step.
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

            // A deleted meeting drops out of the selection.
            app.deleteMeeting(ids.at(2));
            QTRY_COMPARE(s.count(), 2);
        }
        qunsetenv("TANARA_HOME");
    }
};

QTEST_MAIN(TestLibrarySelectionModel)
#include "test_library_selection_model.moc"
