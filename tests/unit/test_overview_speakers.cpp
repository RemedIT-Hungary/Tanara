// OverviewSpeakers (D5): the processing panel's "Beszélők: ..." line, fixTarget and attention flag from the
// facts of the transcript's speakers; the failure reasons in Hungarian.
#include "tanara/library/OverviewSpeakers.h"

#include <QtTest>

using namespace tanara::library;

class TestOverviewSpeakers : public QObject {
    Q_OBJECT
private slots:
    void lineAndFixTargetPerSource()
    {
        SpeakerFacts f;
        f.source = QStringLiteral("provider");
        QVariantMap m = overviewSpeakers(f, false);
        QCOMPARE(m.value("line").toString(), QStringLiteral("Beszélők: a szolgáltatótól"));
        QCOMPARE(m.value("fixTarget").toString(), QString());
        QVERIFY(!m.value("attention").toBool());
        QVERIFY(!m.value("failed").toBool());

        f.source = QStringLiteral("local");
        f.canDiarize = true;
        f.hasNames = true;
        f.speakerCount = 2;
        m = overviewSpeakers(f, false);
        QCOMPARE(m.value("line").toString(), QStringLiteral("Beszélők: helyi elkülönítés"));
        QVERIFY(m.value("canDiarize").toBool());
        QVERIFY(m.value("hasNames").toBool());
        QCOMPARE(m.value("speakerCount").toInt(), 2);

        // No separation, the setting is off: a link to Settings, no attention (a deliberate choice).
        f = SpeakerFacts();
        f.source = QStringLiteral("none");
        f.blocker = QStringLiteral("disabled");
        f.canDiarize = true;
        m = overviewSpeakers(f, false);
        QCOMPARE(m.value("line").toString(), QStringLiteral("Beszélők: nincs — kapcsold be a Beállításokban"));
        QCOMPARE(m.value("fixTarget").toString(), QStringLiteral("speakers"));
        QVERIFY(!m.value("attention").toBool());
        QVERIFY(m.value("canDiarize").toBool());

        // The model is missing: the same link, and the step needs attention.
        f.blocker = QStringLiteral("model");
        f.canDiarize = false;
        m = overviewSpeakers(f, false);
        QCOMPARE(m.value("line").toString(), QStringLiteral("Beszélők: nincs — tölts le modellt a Beállításokban"));
        QCOMPARE(m.value("fixTarget").toString(), QStringLiteral("speakers"));
        QVERIFY(m.value("attention").toBool());

        // It could run, it has not.
        f.blocker.clear();
        f.canDiarize = true;
        m = overviewSpeakers(f, false);
        QCOMPARE(m.value("line").toString(), QStringLiteral("Beszélők: nincs szétválasztva"));
        QCOMPARE(m.value("fixTarget").toString(), QString());
        QVERIFY(!m.value("attention").toBool());
    }

    void failedRunNeedsAttentionWithTheReason()
    {
        SpeakerFacts f;
        f.source = QStringLiteral("none");
        f.canDiarize = true;
        f.problem = QStringLiteral("decode_failed");
        QVariantMap m = overviewSpeakers(f, false);
        QVERIFY(m.value("failed").toBool());
        QVERIFY(m.value("attention").toBool());
        QVERIFY(m.value("canDiarize").toBool());   // "Beszélők felismerése újra"
        QCOMPARE(m.value("problem").toString(), QStringLiteral("a hangsáv nem olvasható"));
        QVERIFY(m.value("line").toString().startsWith(QStringLiteral("Beszélők: a felismerés nem sikerült — ")));

        QCOMPARE(diarizationProblemText(QStringLiteral("no_segmentation_model")), QStringLiteral("nincs szegmentáló modell"));
        QCOMPARE(diarizationProblemText(QStringLiteral("no_embedding_model")), QStringLiteral("nincs hangmodell"));
        QCOMPARE(diarizationProblemText(QStringLiteral("segmentation_failed")), QStringLiteral("a szegmentálás hibát adott"));
        QCOMPARE(diarizationProblemText(QStringLiteral("no turns")), QStringLiteral("nem találtam beszélőt"));
        QCOMPARE(diarizationProblemText(QStringLiteral("whatever")), QStringLiteral("részletek a naplóban"));
    }

    void runningFlagIsCarried()
    {
        SpeakerFacts f;
        f.source = QStringLiteral("none");
        QVERIFY(overviewSpeakers(f, true).value("running").toBool());
        QVERIFY(!overviewSpeakers(f, false).value("running").toBool());
    }
};

QTEST_GUILESS_MAIN(TestOverviewSpeakers)
#include "test_overview_speakers.moc"
