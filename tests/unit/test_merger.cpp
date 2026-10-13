#include <QtTest>
#include "tanara/TranscriptMerger.h"
#include "tanara/Types.h"

using namespace tanara;

class MergerTest : public QObject {
    Q_OBJECT
private slots:
    void merges_orders_and_labels();
    void renderMarkdown_containsSpeakers();

private:
    static QVector<TrackTranscript> makeTwoTracks();
};

QVector<TrackTranscript> MergerTest::makeTwoTracks() {
    // Track A: "Ádám" speaks at 0 ms and 2000 ms.
    TrackTranscript a;
    a.trackId = QStringLiteral("mic");
    a.speakerLabel = QStringLiteral("Ádám");
    a.language = QStringLiteral("hu");
    {
        TranscriptToken t;
        t.text = QStringLiteral("Szia");
        t.startMs = 0; t.endMs = 500;
        a.tokens.append(t);
    }
    {
        TranscriptToken t;
        t.text = QStringLiteral("vége");
        t.startMs = 2000; t.endMs = 2500;
        a.tokens.append(t);
    }

    // Track B: "Béla" speaks at 1000 ms (wedged between the two).
    TrackTranscript b;
    b.trackId = QStringLiteral("loopback");
    b.speakerLabel = QStringLiteral("Béla");
    b.language = QStringLiteral("hu");
    {
        TranscriptToken t;
        t.text = QStringLiteral("Helló");
        t.startMs = 1000; t.endMs = 1500;
        b.tokens.append(t);
    }

    return {a, b};
}

void MergerTest::merges_orders_and_labels() {
    const MergedTranscript m = mergeTranscripts(makeTwoTracks());

    QCOMPARE(m.tokens.size(), 3);

    // sorted by startMs
    for (int i = 1; i < m.tokens.size(); ++i)
        QVERIFY(m.tokens[i - 1].startMs <= m.tokens[i].startMs);

    // order: Ádám(0) -> Béla(1000) -> Ádám(2000)
    QCOMPARE(m.tokens[0].speaker, QStringLiteral("Ádám"));
    QCOMPARE(m.tokens[0].trackId, QStringLiteral("mic"));
    QCOMPARE(m.tokens[1].speaker, QStringLiteral("Béla"));
    QCOMPARE(m.tokens[1].trackId, QStringLiteral("loopback"));
    QCOMPARE(m.tokens[2].speaker, QStringLiteral("Ádám"));

    QCOMPARE(m.language, QStringLiteral("hu"));
}

void MergerTest::renderMarkdown_containsSpeakers() {
    const MergedTranscript m = mergeTranscripts(makeTwoTracks());
    const QString md = m.renderMarkdown();

    QVERIFY(md.contains(QStringLiteral("Ádám")));
    QVERIFY(md.contains(QStringLiteral("Béla")));
    // there is at least one timestamp prefix
    QVERIFY(md.contains(QStringLiteral("[00:00]")));
}

QTEST_GUILESS_MAIN(MergerTest)
#include "test_merger.moc"
