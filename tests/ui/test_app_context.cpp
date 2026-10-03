// Az App-singleton (tanara_qml::AppContext) téma- és állapot-logikája, kijelző és QML nélkül.
// Minta a nézetmodell-tesztekhez: tests/ui/<név>.cpp → külön exe, a tanara_qml libet linkeli.
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

        // Ugyanaz az érték nem jelez újra.
        ctx->setThemeMode(QStringLiteral("light"));
        QCOMPARE(modeSpy.count(), 2);
    }

    void controllerIsOptional()
    {
        // Képernyőkép- / galéria- / demó-módban és a tesztekben nincs AppController.
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
        QVERIFY(ctx->bridge() == nullptr);   // QPointer: a megszűnt híd nem lóg be
    }
};

QTEST_MAIN(TestAppContext)
#include "test_app_context.moc"
