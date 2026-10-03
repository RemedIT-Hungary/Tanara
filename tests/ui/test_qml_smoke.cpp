// QML füstteszt: a modul fő oldalai (Main, Gallery) mindkét témában QML-figyelmeztetés
// nélkül betöltődnek, a bundle-ölt ikonok és betűk megvannak, és Widgets-párbeszédablak
// megjeleníthető a QML-ablak mellett (a Beállítások / Személyek egyelőre Widgetek).
// Offscreen platformon fut (tests/CMakeLists.txt), AppController nélkül.
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

    // Betölt egy oldalt, és visszaadja a közben keletkezett QML-figyelmeztetéseket.
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
        QTest::qWait(150);   // kötések, animáció-indulás, felugrók
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
        // A szín tényleg érvényesül (nem fekete currentColor marad).
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
        QVERIFY(qAlpha(img.pixel(mid, mid)) > 100);              // a közepe telített
        QVERIFY(qAlpha(img.pixel(0, mid)) < qAlpha(img.pixel(32, mid)));
        QVERIFY(qAlpha(img.pixel(0, 0)) < 8);                    // a sarka gyakorlatilag üres
    }

    void keyboardOperatesControls()
    {
        // Tab viszi a fókuszt, Space / Return aktivál — egér nélkül is használható.
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
        QVERIFY(button->property("visualFocus").toBool());       // → fókuszgyűrű látszik
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

    void widgetsDialogShowsNextToQmlWindow()
    {
        QQmlApplicationEngine engine;
        QVERIFY(loadPage(engine, QStringLiteral("Main"), {}, QSize(1280, 820)));
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
        if (!window)   // a loadPage a motor gyerekeként hozza létre
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
