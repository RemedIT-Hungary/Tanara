// The QML side of the "Getting started" window, without a real window (offscreen + software renderer):
//  - every demo state (welcome, you, folders, providers, watcher, done) loads in both themes
//    without a QML warning, and the step's page is visible;
//  - the footer buttons follow the step (Skip only on a decision step, "Let's go" at Done);
//  - the buttons work through the model: Next / Skip / Back, Done closes the window;
//  - on the Settings > General page the "Getting started" link is visible in demo mode.
#include "AppContext.h"
#include "QmlApp.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

#include <memory>

using namespace tanara_qml;

class TestOnboardingRender : public QObject {
    Q_OBJECT

    // Searches the visual tree (Repeater instances are not in the QObject tree).
    static QQuickItem* inTree(QQuickItem* item, const QString& name)
    {
        if (!item) return nullptr;
        if (item->objectName() == name) return item;
        for (QQuickItem* c : item->childItems())
            if (QQuickItem* hit = inTree(c, name)) return hit;
        return nullptr;
    }
    static QQuickItem* named(QQuickWindow* win, const QString& name)
    {
        return inTree(win->contentItem(), name);
    }
    static bool click(QObject* button)
    {
        return QMetaObject::invokeMethod(button, "clicked");
    }

private slots:
    void initTestCase() { AppContext::instance()->setDemo(true); }
    void cleanupTestCase() { AppContext::instance()->setThemeMode(QStringLiteral("light")); }

    void states_data()
    {
        QTest::addColumn<QString>("theme");
        QTest::addColumn<QString>("state");
        for (const char* theme : {"light", "dark"})
            for (const char* state : {"welcome", "you", "folders", "providers", "watcher", "done"})
                QTest::addRow("%s-%s", state, theme) << QString::fromLatin1(theme) << QString::fromLatin1(state);
    }
    void states()
    {
        QFETCH(QString, theme);
        QFETCH(QString, state);
        AppContext::instance()->setThemeMode(theme);
        QQmlEngine engine;
        setupEngine(engine);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError>& list) {
            for (const QQmlError& e : list) warnings << e.toString();
        });
        QQmlComponent comp(&engine);
        comp.loadFromModule("Tanara", "OnboardingWindow");
        QVERIFY2(!comp.isError(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> obj(comp.createWithInitialProperties({{"demoState", state}}));
        auto* win = qobject_cast<QQuickWindow*>(obj.get());
        QVERIFY(win);
        win->show();
        QTest::qWait(120);
        QObject* vm = win->property("vm").value<QObject*>();
        QVERIFY(vm);
        QCOMPARE(vm->property("step").toString(), state);
        QVERIFY(named(win, "page-" + state)->isVisible());
        const bool decision = state == "you" || state == "folders" || state == "providers" || state == "watcher";
        QCOMPARE(named(win, "skipButton")->isVisible(), decision);
        QCOMPARE(named(win, "laterButton")->isVisible(), state != "done");
        QCOMPARE(named(win, "backButton")->isVisible(), state != "welcome");
        QCOMPARE(named(win, "nextButton")->property("text").toString(),
                 state == "done" ? QStringLiteral("Kezdjük") : QStringLiteral("Tovább"));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
    }

    void buttonsDriveTheModel()
    {
        AppContext::instance()->setThemeMode(QStringLiteral("light"));
        QQmlEngine engine;
        setupEngine(engine);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError>& list) {
            for (const QQmlError& e : list) warnings << e.toString();
        });
        QQmlComponent comp(&engine);
        comp.loadFromModule("Tanara", "OnboardingWindow");
        std::unique_ptr<QObject> obj(comp.createWithInitialProperties({{"demoState", "welcome"}}));
        auto* win = qobject_cast<QQuickWindow*>(obj.get());
        QVERIFY(win);
        win->show();
        QTest::qWait(60);
        QObject* vm = win->property("vm").value<QObject*>();
        QVERIFY(click(named(win, "nextButton")));
        QCOMPARE(vm->property("step").toString(), QStringLiteral("you"));
        QVERIFY(click(named(win, "skipButton")));
        QCOMPARE(vm->property("step").toString(), QStringLiteral("folders"));
        QVERIFY(click(named(win, "backButton")));
        QCOMPARE(vm->property("step").toString(), QStringLiteral("you"));
        // The navigation items step too.
        QVERIFY(click(named(win, "step-done")));
        QCOMPARE(vm->property("step").toString(), QStringLiteral("done"));
        QVERIFY(click(named(win, "nextButton")));             // "Let's go" closes
        QTRY_VERIFY(!win->isVisible());
        QVERIFY(vm->property("done").toBool());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
    }

    void settingsGeneralPageLink()
    {
        AppContext::instance()->setThemeMode(QStringLiteral("light"));
        QQmlEngine engine;
        setupEngine(engine);
        QQmlComponent comp(&engine);
        comp.loadFromModule("Tanara", "SettingsWindow");
        std::unique_ptr<QObject> obj(comp.createWithInitialProperties({{"demoState", "B01"}}));
        auto* win = qobject_cast<QQuickWindow*>(obj.get());
        QVERIFY(win);
        win->show();
        QTest::qWait(120);
        QQuickItem* link = named(win, "onboardingLink");
        QVERIFY(link);
        QVERIFY(link->isVisible());
    }
};

QTEST_MAIN(TestOnboardingRender)
#include "test_onboarding_render.moc"
