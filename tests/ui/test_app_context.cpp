// Theme and state logic of the App singleton (tanara_qml::AppContext), without a display or QML.
// Pattern for the view-model tests: tests/ui/<name>.cpp is a separate exe linking the tanara_qml lib.
#include "AppContext.h"

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
