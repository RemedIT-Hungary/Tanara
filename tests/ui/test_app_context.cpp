// Theme and state logic of the App singleton (tanara_qml::AppContext), without a display or QML.
// Pattern for the view-model tests: tests/ui/<name>.cpp is a separate exe linking the tanara_qml lib.
#include "AppContext.h"
#include "QmlApp.h"
#include "ShellFormat.h"
#include "TrackActivitySource.h"

#include "demo/RecorderDemoFacade.h"

#include "tanara/app/AppController.h"
#include "tanara/recording/IRecorderFacade.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QtTest>

using tanara_qml::AppContext;

class TestAppContext : public QObject {
    Q_OBJECT
private slots:
    void normalizesThemeMode()
    {
        QCOMPARE(AppContext::normalizedThemeMode(QStringLiteral("dark")), QStringLiteral("dark"));
        QCOMPARE(AppContext::normalizedThemeMode(QStringLiteral(" Light ")), QStringLiteral("light"));
        QCOMPARE(AppContext::normalizedThemeMode(QStringLiteral("system")), QStringLiteral("system"));
        QCOMPARE(AppContext::normalizedThemeMode(QString()), QStringLiteral("system"));
        QCOMPARE(AppContext::normalizedThemeMode(QStringLiteral("neon")), QStringLiteral("system"));
    }

    void explicitThemeOverridesSystem()
    {
        AppContext* ctx = AppContext::instance();
        QSignalSpy darkSpy(ctx, &AppContext::darkChanged);
        QSignalSpy modeSpy(ctx, &AppContext::themeModeChanged);

        ctx->setThemeMode(QStringLiteral("dark"));
        QVERIFY(ctx->dark());
        QCOMPARE(ctx->themeMode(), QStringLiteral("dark"));

        ctx->setThemeMode(QStringLiteral("light"));
        QVERIFY(!ctx->dark());
        QCOMPARE(modeSpy.count(), 2);
        QVERIFY(darkSpy.count() >= 1);

        // The same value does not signal again.
        ctx->setThemeMode(QStringLiteral("light"));
        QCOMPARE(modeSpy.count(), 2);
    }

    void controllerIsOptional()
    {
        // In screenshot / gallery / demo mode and in the tests there is no AppController.
        AppContext* ctx = AppContext::instance();
        QVERIFY(ctx->controller() == nullptr);
        QVERIFY(ctx->controllerObject() == nullptr);

        QSignalSpy demoSpy(ctx, &AppContext::demoChanged);
        ctx->setDemo(true);
        QVERIFY(ctx->demo());
        QCOMPARE(demoSpy.count(), 1);
        ctx->setDemo(false);
    }

    // No controller (or App.demo): the view-model factories get the fictional facades of tanara_demo.
    void servicesAreFictionalWithoutAController()
    {
        AppContext* ctx = AppContext::instance();
        const tanara_qml::UiServices& demo = ctx->demoServices();
        QVERIFY(qobject_cast<tanara_demo::RecorderDemoFacade*>(demo.recorder));
        QCOMPARE(ctx->services().recorder, demo.recorder);
        QCOMPARE(ctx->servicesFor(nullptr).recorder, demo.recorder);
        QObject owner;
        tanara_qml::TrackActivitySource* a = demo.trackActivity(QStringLiteral("loading"), &owner);
        QVERIFY(a && a->parent() == &owner);
        QVERIFY(a->computing(QStringLiteral("x")));
        QVERIFY(a->activity(QStringLiteral("x")).isEmpty());
        tanara_qml::TrackActivitySource* b = demo.trackActivity(QString(), &owner);
        QVERIFY(!b->computing(QStringLiteral("x")));
        QCOMPARE(b->activity(QStringLiteral("x")).tracks.size(), 4);
        QCOMPARE(b->meeting(QString()).durationMs, qint64((30 * 60 + 34) * 1000));
    }

    // A controller's services are real, cached per controller, and go away with it.
    void servicesOfAControllerAreReal()
    {
        AppContext* ctx = AppContext::instance();
        QPointer<tanara::recording::IRecorderFacade> first;
        {
            tanara::app::AppController c;
            const tanara_qml::UiServices& set = ctx->servicesFor(&c);
            QVERIFY(set.recorder);
            QVERIFY(!qobject_cast<tanara_demo::RecorderDemoFacade*>(set.recorder));
            QCOMPARE(&ctx->servicesFor(&c), &set);                    // cached
            QCOMPARE(set.recorder->tagController(), static_cast<QObject*>(&c));
            QCOMPARE(set.recorder->parent(), static_cast<QObject*>(&c));   // lives and dies with the controller
            QVERIFY(set.recorder != ctx->demoServices().recorder);
            first = set.recorder;
            // services() follows the process controller, unless App.demo is on.
            ctx->setController(&c);
            QCOMPARE(ctx->services().recorder, set.recorder);
            ctx->setDemo(true);
            QCOMPARE(ctx->services().recorder, ctx->demoServices().recorder);
            ctx->setDemo(false);
            ctx->setController(nullptr);
        }
        QVERIFY(first.isNull());
    }

    // The one playback / track time text of the three QML time formatters.
    void shellFormatClock()
    {
        const tanara_qml::ShellFormatBridge f;
        QCOMPARE(f.clock(0), QStringLiteral("0:00"));
        QCOMPARE(f.clock(0, true), QStringLiteral("00:00"));
        QCOMPARE(f.clock(17999, true), QStringLiteral("00:17"));      // floored to the second
        QCOMPARE(f.clock((30 * 60 + 34) * 1000), QStringLiteral("30:34"));
        QCOMPARE(f.clock((3600 + 4 * 60 + 5) * 1000), QStringLiteral("1:04:05"));
        QCOMPARE(f.clock(-5000), QStringLiteral("0:00"));

        // ... and it is what QML sees as ShellFormat.
        QQmlEngine engine;
        tanara_qml::setupEngine(engine);
        QQmlComponent comp(&engine);
        comp.setData("import QtQuick\nimport Tanara\nQtObject { property string t: ShellFormat.clock(76004000) }",
                     QUrl());
        std::unique_ptr<QObject> obj(comp.create());
        QVERIFY2(obj, qPrintable(comp.errorString()));
        QCOMPARE(obj->property("t").toString(), QStringLiteral("21:06:44"));
    }

    void bridgeIsTrackedAndClearedOnDestroy()
    {
        AppContext* ctx = AppContext::instance();
        QSignalSpy spy(ctx, &AppContext::bridgeChanged);
        {
            QObject bridge;
            ctx->setBridge(&bridge);
            QCOMPARE(ctx->bridge(), &bridge);
            QCOMPARE(spy.count(), 1);
        }
        QVERIFY(ctx->bridge() == nullptr);   // QPointer: a destroyed bridge does not dangle
    }
};

QTEST_MAIN(TestAppContext)
#include "test_app_context.moc"
