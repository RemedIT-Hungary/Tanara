// The Soniox client_reference_id format (tanara:1:<meetingId>:<jobId>:<trackId>): builder / parser
// round-trip, empty parts, the separator rule, the 256-character cap and invalid inputs. Pure functions.
#include "tanara/providers/stt/SonioxReference.h"

#include <QtTest>

using namespace tanara::providers;

class TestSonioxReference : public QObject {
    Q_OBJECT

private slots:
    void buildsVersionedReference()
    {
        const QString ref = buildClientReference(QStringLiteral("f4ebda08-2f8e-4216-8067-ccbc8b8192a4"),
                                                 QStringLiteral("7f3c2a9e-5d41-4b8e-9a61-0c2f1e8b7d34"),
                                                 QStringLiteral("mixdown"));
        QCOMPARE(ref, QStringLiteral("tanara:1:f4ebda08-2f8e-4216-8067-ccbc8b8192a4:"
                                     "7f3c2a9e-5d41-4b8e-9a61-0c2f1e8b7d34:mixdown"));
        const ClientReference p = parseClientReference(ref);
        QVERIFY(p.valid);
        QCOMPARE(p.meetingId, QStringLiteral("f4ebda08-2f8e-4216-8067-ccbc8b8192a4"));
        QCOMPARE(p.jobId, QStringLiteral("7f3c2a9e-5d41-4b8e-9a61-0c2f1e8b7d34"));
        QCOMPARE(p.trackId, QStringLiteral("mixdown"));
    }

    void emptyPartsStayPositional()
    {
        const QString ref = buildClientReference(QStringLiteral("m1"), QString(), QStringLiteral("t1"));
        QCOMPARE(ref, QStringLiteral("tanara:1:m1::t1"));
        const ClientReference p = parseClientReference(ref);
        QVERIFY(p.valid);
        QCOMPARE(p.meetingId, QStringLiteral("m1"));
        QVERIFY(p.jobId.isEmpty());
        QCOMPARE(p.trackId, QStringLiteral("t1"));

        QCOMPARE(buildClientReference(QString(), QString(), QStringLiteral("t1")), QStringLiteral("tanara:1:::t1"));
        QVERIFY(parseClientReference(QStringLiteral("tanara:1:::t1")).valid);
    }

    void allEmptyMeansNoReference()
    {
        QVERIFY(buildClientReference(QString(), QString(), QString()).isEmpty());
    }

    void separatorIsReplacedInParts()
    {
        const QString ref = buildClientReference(QStringLiteral("a:b"), QStringLiteral("j"), QStringLiteral("t:1"));
        QCOMPARE(ref, QStringLiteral("tanara:1:a_b:j:t_1"));
        const ClientReference p = parseClientReference(ref);
        QVERIFY(p.valid);
        QCOMPARE(p.meetingId, QStringLiteral("a_b"));
        QCOMPARE(p.trackId, QStringLiteral("t_1"));
    }

    void cappedAt256Characters()
    {
        const QString longPart(400, QLatin1Char('x'));
        const QString ref = buildClientReference(longPart, QStringLiteral("j"), QStringLiteral("t"));
        QCOMPARE(ref.size(), kClientReferenceMaxLength);
        QVERIFY(ref.startsWith(QStringLiteral("tanara:1:xxx")));
    }

    void invalidInputs()
    {
        const QStringList bad{
            QString(),
            QStringLiteral("tanara"),
            QStringLiteral("tanara:1"),
            QStringLiteral("tanara:1:m:j"),              // 4 parts
            QStringLiteral("tanara:1:m:j:t:extra"),      // 6 parts
            QStringLiteral("tanara:2:m:j:t"),            // unknown version
            QStringLiteral("other:1:m:j:t"),             // foreign prefix
            QStringLiteral("tc:7f3c2a9e-5d41-4b8e-9a61-0c2f1e8b7d34"),   // the gateway's own upstream id
        };
        for (const QString& s : bad) {
            const ClientReference p = parseClientReference(s);
            QVERIFY2(!p.valid, qPrintable(s));
            QVERIFY(p.meetingId.isEmpty() && p.jobId.isEmpty() && p.trackId.isEmpty());
        }
    }
};

QTEST_GUILESS_MAIN(TestSonioxReference)
#include "test_soniox_reference.moc"
