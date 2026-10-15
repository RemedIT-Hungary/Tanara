// The diarization evaluation (D6): DER (missed / false alarm / confusion, the collar, the excluded regions,
// overlapped speech, the optimal speaker mapping), the speaker-count error, the reference formats (JSON,
// RTTM) and the reference from provider tokens - all on synthetic turns.
#include "tanara/processing/diarization/DiarizationEval.h"

#include <QJsonDocument>
#include <QtTest>

using namespace tanara::processing::diareval;

namespace {

bool near(double a, double b, double eps = 0.02) { return std::abs(a - b) <= eps; }

DerOptions noCollar()
{
    DerOptions o;
    o.collar = 0;
    return o;
}

} // namespace

class TestDiarizationEval : public QObject {
    Q_OBJECT
private slots:
    void perfectAndPermutedHypothesisIsZero()
    {
        const QVector<EvalTurn> ref{{0, 5, "A"}, {5, 10, "B"}, {12, 15, "A"}};
        DerResult r = computeDer(ref, ref, noCollar());
        QCOMPARE(r.der(), 0.0);
        QVERIFY(near(r.scored, 13.0));
        // Other labels, same partition: the mapping makes it perfect.
        const QVector<EvalTurn> hyp{{0, 5, "0"}, {5, 10, "1"}, {12, 15, "0"}};
        r = computeDer(ref, hyp, noCollar());
        QCOMPARE(r.der(), 0.0);
        QCOMPARE(r.mapping.value("0"), QStringLiteral("A"));
        QCOMPARE(r.mapping.value("1"), QStringLiteral("B"));
        QCOMPARE(r.speakerCountError(), 0);
    }

    void missedFalseAlarmAndConfusion()
    {
        const QVector<EvalTurn> ref{{0, 10, "A"}, {10, 20, "B"}};
        // Missed: the hypothesis covers only A.
        DerResult r = computeDer(ref, {{0, 10, "x"}}, noCollar());
        QVERIFY(near(r.missed, 10.0));
        QVERIFY(near(r.der(), 0.5));
        QCOMPARE(r.speakerCountError(), -1);
        // False alarm: extra speech after the reference.
        r = computeDer(ref, {{0, 10, "x"}, {10, 20, "y"}, {20, 25, "y"}}, noCollar());
        QVERIFY(near(r.falseAlarm, 5.0));
        QVERIFY(near(r.der(), 0.25));
        // Confusion: one hypothesis speaker for both -> one of them is confused.
        r = computeDer(ref, {{0, 20, "x"}}, noCollar());
        QVERIFY(near(r.confusion, 10.0));
        QVERIFY(near(r.missed, 0.0) && near(r.falseAlarm, 0.0));
        QCOMPARE(r.speakerCountError(), -1);
        // Over-split: A in two hypothesis speakers -> the smaller part is confusion, +1 speaker.
        r = computeDer(ref, {{0, 7, "x"}, {7, 10, "z"}, {10, 20, "y"}}, noCollar());
        QVERIFY(near(r.confusion, 3.0));
        QCOMPARE(r.speakerCountError(), 1);
        // The rates.
        QVERIFY(near(r.confusionRate(), 0.15));
        QCOMPARE(r.missedRate(), 0.0);
    }

    void overlappedSpeechCountsEverySpeaker()
    {
        // A and B both talk 4-6: 2 speaker-seconds per second there.
        const QVector<EvalTurn> ref{{0, 6, "A"}, {4, 10, "B"}};
        DerResult r = computeDer(ref, {{0, 6, "x"}, {6, 10, "y"}}, noCollar());
        QVERIFY(near(r.scored, 12.0));
        QVERIFY(near(r.missed, 2.0));   // B's overlapped 2 s is missed
        r = computeDer(ref, ref, noCollar());
        QCOMPARE(r.der(), 0.0);
    }

    void collarAndExcludedRegionsAreNotScored()
    {
        const QVector<EvalTurn> ref{{1, 5, "A"}};
        // The hypothesis is 0.2 s late at both ends: inside a 0.25 s collar nothing counts.
        DerResult r = computeDer(ref, {{1.2, 5.2, "x"}}, DerOptions());
        QCOMPARE(r.der(), 0.0);
        QVERIFY(near(r.scored, 3.5));   // 4 s minus 2 x 0.25 s
        r = computeDer(ref, {{1.2, 5.2, "x"}}, noCollar());
        QVERIFY(r.der() > 0.05);
        // An excluded region: neither the reference nor the hypothesis there is scored.
        r = computeDer(ref, {{1, 5, "x"}, {7, 9, "y"}}, noCollar(), {{6, 10}});
        QCOMPARE(r.der(), 0.0);
        // Nothing scored, only false alarm: 1.
        r = computeDer({}, {{0, 2, "x"}}, noCollar());
        QCOMPARE(r.der(), 1.0);
        QCOMPARE(computeDer({}, {}, noCollar()).der(), 0.0);
    }

    void aggregation()
    {
        DerResult a = computeDer({{0, 10, "A"}}, {{0, 5, "x"}}, noCollar());
        const DerResult b = computeDer({{0, 10, "A"}}, {{0, 10, "x"}}, noCollar());
        a += b;
        QVERIFY(near(a.scored, 20.0));
        QVERIFY(near(a.der(), 0.25));
        QCOMPARE(a.refSpeakers, 2);
        QVERIFY(a.mapping.isEmpty());
    }

    void referenceFormats()
    {
        Reference ref;
        ref.source = QStringLiteral("test");
        ref.notes << QStringLiteral("synthetic");
        TrackReference t;
        t.trackId = QStringLiteral("mic");
        t.file = QStringLiteral("track_mic.ogg");
        t.startOffsetMs = 250;
        t.turns = {{0.5, 2.0, "A"}, {3.0, 4.5, "B"}};
        t.excluded = {{10, 12}};
        ref.tracks << t;
        const QByteArray json = QJsonDocument(referenceToJson(ref)).toJson();
        QString err;
        const Reference back = parseReferenceJson(json, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(back.tracks.size(), 1);
        QCOMPARE(back.track("mic")->startOffsetMs, 250);
        QCOMPARE(back.track("mic")->turns.size(), 2);
        QCOMPARE(back.track("mic")->turns[1].speaker, QStringLiteral("B"));
        QCOMPARE(back.track("mic")->excluded.size(), 1);
        QVERIFY(back.track("none") == nullptr);
        QVERIFY(parseReferenceJson("{\"format\":\"other\"}", &err).tracks.isEmpty());
        QVERIFY(!err.isEmpty());

        const QString rttm = toRttm(QStringLiteral("mic"), t.turns) + toRttm(QStringLiteral("loop"), {{1, 2, "Remote 1"}});
        const Reference r = parseRttm(rttm, &err);
        QCOMPARE(r.tracks.size(), 2);
        QCOMPARE(r.track("mic")->turns.size(), 2);
        QVERIFY(near(r.track("mic")->turns[0].end, 2.0, 1e-6));
        QCOMPARE(r.track("loop")->turns[0].speaker, QStringLiteral("Remote_1"));
        QVERIFY(parseRttm(QStringLiteral("SPEAKER x 1 abc 1 <NA> <NA> A <NA> <NA>"), &err).tracks.isEmpty());
    }

    void referenceFromTokens()
    {
        // Emission-time tokens (points): A at 1.0, 1.4, 1.9 s; a 3 s pause; A at 5.0 s; B at 2.0 s.
        const QVector<RefToken> tokens{{1000, 1060, "A"}, {1400, 1460, "A"}, {1900, 1960, "A"},
                                       {5000, 5060, "A"}, {2000, 2060, "B"}};
        const QVector<Interval> ua = utterancesOf(tokens.mid(0, 4));
        QCOMPARE(ua.size(), 2);
        QVERIFY(near(ua[0].start, 0.7, 1e-6));   // 1.0 - lead 0.3
        QVERIFY(near(ua[0].end, 2.06, 1e-6));    // 1.96 + tail 0.1
        const QVector<EvalTurn> turns = turnsFromTokens(tokens);
        QCOMPARE(turns.size(), 3);
        QCOMPARE(speakersOf(turns), QStringList({"A", "B"}));
        QVERIFY(turns[0].start <= turns[1].start && turns[1].start <= turns[2].start);
        // Track assignment by votes.
        QCOMPARE(assignTrack({{"mic", 8.0}, {"loop", 2.0}}), QStringLiteral("mic"));
        QCOMPARE(assignTrack({{"mic", 6.0}, {"loop", 4.0}}), QString());
        QCOMPARE(assignTrack({}), QString());
    }
};

QTEST_GUILESS_MAIN(TestDiarizationEval)
#include "test_diarization_eval.moc"
