#include <QtTest>
#include "tanara/app/AppSettings.h"
#include "tanara/foundation/Version.h"

class SmokeTest : public QObject {
    Q_OBJECT
private slots:
    void version_nonEmpty() { QVERIFY(!tanara::libraryVersion().isEmpty()); }
    void settings_defaultLanguageHu() {
        tanara::app::AppSettings s;
        QCOMPARE(s.processing.languageHints.value(0), QStringLiteral("hu"));
    }
};

QTEST_MAIN(SmokeTest)
#include "test_smoke.moc"
