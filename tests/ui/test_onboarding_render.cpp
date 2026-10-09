// Az „Első lépések” ablak QML-oldala, valódi ablak nélkül (offscreen + szoftveres renderer):
//  - minden demó-állapot (welcome · you · folders · providers · watcher · done) mindkét témában
//    QML-figyelmeztetés nélkül töltődik be, és a lépés oldala látszik;
//  - a lábléc gombjai a lépéshez igazodnak (Kihagyom csak döntési lépésen, Kész-nél „Kezdjük”);
//  - a gombok a modellen át működnek: Tovább / Kihagyom / Vissza, a Kész bezárja az ablakot;
//  - a Beállítások › Általános lapján az „Első lépések” hivatkozás demóban látszik.
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

    // A vizuális fán keres (a Repeater példányai nem a QObject-fában vannak).
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
        // A navigáció elemei is léptetnek.
        QVERIFY(click(named(win, "step-done")));
        QCOMPARE(vm->property("step").toString(), QStringLiteral("done"));
        QVERIFY(click(named(win, "nextButton")));             // Kezdjük → bezár
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
