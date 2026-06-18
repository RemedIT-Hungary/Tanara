#include <QtTest>
#include "tanara/Types.h"

class SmokeTest : public QObject {
    Q_OBJECT
private slots:
    void version_nonEmpty() { QVERIFY(!tanara::libraryVersion().isEmpty()); }
    void settings_defaultLanguageHu() {
        tanara::AppSettings s;
        QCOMPARE(s.languageHints.value(0), QStringLiteral("hu"));
    }
};

QTEST_MAIN(SmokeTest)
#include "test_smoke.moc"
