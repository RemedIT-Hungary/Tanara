// foundation/time: the shared time formatters. Each variant keeps the format of the place it came
// from (prompt timestamps, UI clock, CLI listings, job durations).
#include <QtTest>

#include "tanara/foundation/time/TimeFormat.h"

using namespace tanara;

class TimeFormatTest : public QObject {
    Q_OBJECT
private slots:
    void timestamp_minutesNotCapped()
    {
        QCOMPARE(timefmt::timestamp(0), QStringLiteral("00:00"));
        QCOMPARE(timefmt::timestamp(59999), QStringLiteral("00:59"));
        QCOMPARE(timefmt::timestamp(4503000), QStringLiteral("75:03"));
    }
    void clock_hoursAndPadding()
    {
        QCOMPARE(timefmt::clock(0), QStringLiteral("0:00"));
        QCOMPARE(timefmt::clock(65000), QStringLiteral("1:05"));
        QCOMPARE(timefmt::clock(65000, true), QStringLiteral("01:05"));
        QCOMPARE(timefmt::clock(3723000), QStringLiteral("1:02:03"));
        QCOMPARE(timefmt::clock(3723000, true), QStringLiteral("1:02:03"));
        QCOMPARE(timefmt::clock(-5000), QStringLiteral("0:00"));
        QCOMPARE(timefmt::clock(1999), QStringLiteral("0:01"));   // truncated, not rounded
    }
    void clockMs_tenths()
    {
        QCOMPARE(timefmt::clockMs(65432), QStringLiteral("01:05.4"));
        QCOMPARE(timefmt::clockMs(3723000), QStringLiteral("62:03.0"));
    }
    void hms_alwaysHours()
    {
        QCOMPARE(timefmt::hms(65000), QStringLiteral("0:01:05"));
        QCOMPARE(timefmt::hms(3723000), QStringLiteral("1:02:03"));
    }
    void duration_rounded()
    {
        QCOMPARE(timefmt::duration(-1), QStringLiteral("–"));
        QCOMPARE(timefmt::duration(0), QStringLiteral("00:00"));
        QCOMPARE(timefmt::duration(1499), QStringLiteral("00:01"));
        QCOMPARE(timefmt::duration(1500), QStringLiteral("00:02"));
        QCOMPARE(timefmt::duration(59500), QStringLiteral("01:00"));
        QCOMPARE(timefmt::duration(3600000), QStringLiteral("1:00:00"));
    }
};

QTEST_GUILESS_MAIN(TimeFormatTest)
#include "test_timefmt.moc"
