// QML smoke test: the module's main pages (Main, Gallery) load in both themes without a QML
// warning, the bundled icons and fonts exist, and a Widgets dialog can be shown next to the
// QML window (Settings / People were still Widgets then). Runs on the offscreen platform
// (tests/CMakeLists.txt), without AppController.
#include "AppContext.h"
#include "ImageProviders.h"
#include "QmlApp.h"

#include <QDialog>
#include <QDir>
#include <QFontDatabase>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

#include <memory>

using namespace tanara_qml;

class TestQmlSmoke : public QObject {
    Q_OBJECT

    // Loads a page and returns the QML warnings produced meanwhile.
    static QStringList loadAndCollectWarnings(const QString& page, const QVariantMap& props = {})
    {
        QStringList warnings;
        QQmlApplicationEngine engine;
        QObject::connect(&engine, &QQmlEngine::warnings, &engine,
                         [&warnings](const QList<QQmlError>& list) {
                             for (const QQmlError& e : list)
                                 warnings << e.toString();
                         });
        if (!loadPage(engine, page, props, QSize(1280, 820)))
            warnings << QStringLiteral("nem tölthető be: %1").arg(page);
        QTest::qWait(150);   // bindings, animation start, popups
        return warnings;
    }

private slots:
    void initTestCase()
    {
        AppContext::instance()->setDemo(true);
    }

    void pagesLoadWithoutWarnings_data()
    {
        QTest::addColumn<QString>("page");
        QTest::addColumn<QString>("theme");
        QTest::addColumn<QVariantMap>("props");
        for (const char* theme : {"light", "dark"}) {
            const QString t = QString::fromLatin1(theme);
            QTest::addRow("Gallery-%s", theme) << "Gallery" << t << QVariantMap{};
            QTest::addRow("Gallery-dialog-%s", theme)
                << "Gallery" << t << QVariantMap{{"overlay", "dialog"}};
            QTest::addRow("Gallery-menu-%s", theme)
                << "Gallery" << t << QVariantMap{{"overlay", "menu"}};
            for (const char* state : {"meeting", "empty", "noSelection", "preTranscript"})
                QTest::addRow("Main-%s-%s", state, theme)
                    << "Main" << t << QVariantMap{{"shellState", state}, {"taskRunning", true}};
            // The meeting view: Overview, Transcript + map dock, the tab panel.
            for (const char* state : {"overviewProcessing", "overviewDone", "overviewEmpty", "overviewNobody",
                                      "readingMap", "tabTooltip"})
                QTest::addRow("Main-v3-%s-%s", state, theme)
                    << "Main" << t << QVariantMap{{"demoState", state}};
            for (const char* state : {"overviewProcessing", "overviewDone", "overviewEmpty", "overviewNobody"})
                QTest::addRow("OverviewTab-%s-%s", state, theme)
                    << "OverviewTab" << t << QVariantMap{{"demoState", state}};
            QTest::addRow("MapDock-%s", theme) << "MapDock" << t << QVariantMap{};
            QTest::addRow("MeetingHeader-%s", theme) << "MeetingHeader" << t << QVariantMap{{"demoState", "tabTooltip"}};
        }
    }
    void pagesLoadWithoutWarnings()
    {
        QFETCH(QString, page);
        QFETCH(QString, theme);
        QFETCH(QVariantMap, props);
        AppContext::instance()->setThemeMode(theme);
        const QStringList warnings = loadAndCollectWarnings(page, props);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
    }

    void bundledFontsAreAvailable()
    {
        QQmlEngine engine;
        setupEngine(engine);
        QVERIFY(QFontDatabase::families().contains(QStringLiteral("IBM Plex Sans")));
        QVERIFY(QFontDatabase::families().contains(QStringLiteral("IBM Plex Mono")));
    }

    void everyBundledIconRenders()
    {
        const QDir dir(QStringLiteral(":/qt/qml/Tanara/icons"));
        const QStringList icons = dir.entryList({QStringLiteral("*.svg")}, QDir::Files);
        QVERIFY(icons.size() >= 40);
        for (const QString& file : icons) {
            const QString name = file.chopped(4);
            const QImage img = TanaraImageProvider::renderIcon(name, QColor(0x2f, 0x62, 0xac),
                                                               1.75, QSize(24, 24));
            bool painted = false;
            for (int y = 0; y < img.height() && !painted; ++y)
                for (int x = 0; x < img.width() && !painted; ++x)
                    painted = qAlpha(img.pixel(x, y)) > 0;
            QVERIFY2(painted, qPrintable(name));
        }
        // The color really applies (it does not stay a black currentColor).
        const QImage red = TanaraImageProvider::renderIcon(QStringLiteral("square"), QColor(Qt::red),
                                                           2.0, QSize(24, 24));
        QCOMPARE(QColor(red.pixel(12, 3)).red(), 255);
        QCOMPARE(QColor(red.pixel(12, 3)).blue(), 0);
    }

    void shadowImageFadesOutwards()
    {
        const QImage img = TanaraImageProvider::renderShadow(32, 8, QColor(0, 0, 0, 128));
        QCOMPARE(img.width(), 2 * (2 * 32 + 8) + 1);
        const int mid = img.width() / 2;
        QVERIFY(qAlpha(img.pixel(mid, mid)) > 100);              // its center is saturated
        QVERIFY(qAlpha(img.pixel(0, mid)) < qAlpha(img.pixel(32, mid)));
        QVERIFY(qAlpha(img.pixel(0, 0)) < 8);                    // its corner is practically empty
    }

    void keyboardOperatesControls()
    {
        // Tab moves focus, Space / Return activate: usable without a mouse too.
        QQmlEngine engine;
        setupEngine(engine);
        QQmlComponent comp(&engine);
        comp.setData("import QtQuick\nimport Tanara\n"
                     "Item { width: 320; height: 120; property int clicks: 0\n"
                     "  TButton { objectName: \"button\"; text: \"Gomb\"; onClicked: parent.clicks++ }\n"
                     "  TCheckBox { objectName: \"check\"; y: 50; text: \"Jelölő\" }\n"
                     "  TSwitch { objectName: \"switch\"; y: 85; text: \"Kapcsoló\" } }\n",
                     QUrl(QStringLiteral("qrc:/qt/qml/Tanara/KeyboardProbe.qml")));
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem*>(comp.create()));
        QVERIFY2(root, qPrintable(comp.errorString()));

        QQuickWindow window;
        window.resize(320, 120);
        root->setParentItem(window.contentItem());
        window.show();
        window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        auto* button = root->findChild<QQuickItem*>(QStringLiteral("button"));
        auto* check = root->findChild<QQuickItem*>(QStringLiteral("check"));
        auto* sw = root->findChild<QQuickItem*>(QStringLiteral("switch"));
        QVERIFY(button && check && sw);

        button->forceActiveFocus(Qt::TabFocusReason);
        QVERIFY(button->property("visualFocus").toBool());       // the focus ring shows
        QTest::keyClick(&window, Qt::Key_Space);
        QCOMPARE(root->property("clicks").toInt(), 1);
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(root->property("clicks").toInt(), 2);

        QTest::keyClick(&window, Qt::Key_Tab);
        QVERIFY(check->hasActiveFocus());
        QTest::keyClick(&window, Qt::Key_Space);
        QVERIFY(check->property("checked").toBool());

        QTest::keyClick(&window, Qt::Key_Tab);
        QVERIFY(sw->hasActiveFocus());
        QTest::keyClick(&window, Qt::Key_Space);
        QVERIFY(sw->property("checked").toBool());

        root->setParentItem(nullptr);
    }

    // The shell's and the editor's shortcuts: each key does one thing, depending on focus; typing
    // into a text field fires none of them.
    void shellShortcutsDependOnFocus()
    {
        QStringList warnings;
        QQmlApplicationEngine engine;
        QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&warnings](const QList<QQmlError>& list) {
            for (const QQmlError& e : list) warnings << e.toString();
        });
        QVERIFY(loadPage(engine, QStringLiteral("Main"), {{"shellState", "meeting"}}, QSize(1280, 820)));
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
        if (!window) window = engine.findChild<QQuickWindow*>();
        QVERIFY(window);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        QTest::qWait(150);
        auto* search = window->findChild<QQuickItem*>(QStringLiteral("librarySearch"));
        auto* list = window->findChild<QQuickItem*>(QStringLiteral("transcriptList"));
        auto* transcriptSearch = window->findChild<QQuickItem*>(QStringLiteral("transcriptSearch"));
        QVERIFY(search && list && transcriptSearch);
        auto tab = [window] { return window->property("currentTab").toInt(); };

        // Ctrl+1..4: switch tab (Overview, Transcript, Executive summary, Memo), wherever focus is.
        QTest::keyClick(window, Qt::Key_2, Qt::ControlModifier);
        QCOMPARE(tab(), 1);
        QTest::keyClick(window, Qt::Key_3, Qt::ControlModifier);
        QCOMPARE(tab(), 2);
        QTest::keyClick(window, Qt::Key_4, Qt::ControlModifier);
        QCOMPARE(tab(), 3);
        QTest::keyClick(window, Qt::Key_1, Qt::ControlModifier);
        QCOMPARE(tab(), 0);

        // Ctrl+F goes to the library search; there the digits, Space and letters are text.
        QTest::keyClick(window, Qt::Key_F, Qt::ControlModifier);
        QVERIFY(search->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_2);
        QTest::keyClick(window, Qt::Key_Space);
        QTest::keyClick(window, Qt::Key_B);
        QCOMPARE(search->property("text").toString(), QStringLiteral("2 b"));
        QCOMPARE(tab(), 0);
        QTest::keyClick(window, Qt::Key_Escape);                 // Esc: the search clears
        QCOMPARE(search->property("text").toString(), QString());

        // Ctrl+Shift+F: search in the open transcript (switches there from any tab).
        QTest::keyClick(window, Qt::Key_3, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(tab(), 1);
        QTRY_VERIFY(transcriptSearch->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_A);
        QTest::keyClick(window, Qt::Key_Space);
        QCOMPARE(transcriptSearch->property("text").toString(), QStringLiteral("a "));
        QTest::keyClick(window, Qt::Key_Escape);                 // Esc: the transcript search closes
        QTRY_VERIFY(!transcriptSearch->isVisible());

        // F2: edit the title; in the field Ctrl+1 is not text, but letters are, and Space too.
        QTest::keyClick(window, Qt::Key_F2);
        auto* title = window->findChild<QQuickItem*>(QStringLiteral("meetingTitleEdit"));
        QVERIFY(title);
        QTRY_VERIFY(title->hasActiveFocus());
        const QString before = title->property("text").toString();
        QTest::keyClick(window, Qt::Key_End);
        QTest::keyClick(window, Qt::Key_Space);
        QTest::keyClick(window, Qt::Key_X);
        QCOMPARE(title->property("text").toString(), before + QStringLiteral(" x"));
        QTest::keyClick(window, Qt::Key_Escape);                 // discard
        QTRY_VERIFY(!title->hasActiveFocus());

        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
    }

    void widgetsDialogShowsNextToQmlWindow()
    {
        QQmlApplicationEngine engine;
        QVERIFY(loadPage(engine, QStringLiteral("Main"), {}, QSize(1280, 820)));
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
        if (!window)   // loadPage creates it as a child of the engine
            window = engine.findChild<QQuickWindow*>();
        QVERIFY(window);
        QVERIFY(window->isVisible());

        QDialog dialog;
        dialog.setWindowTitle(QStringLiteral("Widgets a QML mellett"));
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QVERIFY(dialog.isVisible());
        QVERIFY(window->isVisible());
        dialog.close();
    }
};

QTEST_MAIN(TestQmlSmoke)
#include "test_qml_smoke.moc"
