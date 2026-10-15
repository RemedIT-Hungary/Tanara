// DiarizationMerge (D4): the local diarization's turns written into a transcript without speakers - the
// majority (overlap) assignment, a clear split inside a line, an island that is smoothed away, a
// backchannel between pauses that is kept, tokens outside every turn, several tracks (meeting time,
// different raw speakers per track), the "speaker:N" raw keys, the metadata (and its tokens.json round
// trip), and the cases left untouched (a provider-labelled transcript, no turns).
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/processing/diarization/DiarizationMerge.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace tanara;
using namespace tanara::processing;
namespace dm = tanara::processing::diarizationmerge;

namespace {

// Words of 250 ms every 300 ms from `fromMs` (one line: no pause), all "speaker:1" (Whisper).
void words(MergedTranscript& t, qint64 fromMs, int count, const QString& prefix = QStringLiteral("w"))
{
    for (int i = 0; i < count; ++i) {
        TranscriptToken tok;
        tok.text = QStringLiteral(" %1%2").arg(prefix).arg(i);
        tok.speaker = QStringLiteral("speaker:1");
        tok.startMs = fromMs + i * 300;
        tok.endMs = tok.startMs + 250;
        tok.trackId = QStringLiteral("mixdown");
        t.tokens.append(tok);
    }
}

providers::SpeakerTurn turn(const QString& track, int speaker, qint64 a, qint64 b)
{
    providers::SpeakerTurn t;
    t.trackId = track;
    t.speakerIndex = speaker;
    t.startMs = a;
    t.endMs = b;
    return t;
}

QStringList speakers(const MergedTranscript& t)
{
    QStringList out;
    for (const TranscriptToken& tok : t.tokens) out << tok.speaker;
    return out;
}

QStringList repeat(const QString& s, int n)
{
    QStringList out;
    for (int i = 0; i < n; ++i) out << s;
    return out;
}

} // namespace

class TestDiarizationMerge : public QObject {
    Q_OBJECT
private slots:
    void majorityAndClearSplit()
    {
        MergedTranscript t;
        words(t, 0, 20);   // 0 .. 5.95 s, one line
        providers::DiarizeResult d;
        d.turns = {turn("loop", 0, 0, 3000), turn("loop", 1, 3000, 6000)};
        d.segmentationModelId = QStringLiteral("seg");
        d.embeddingModelIds = {QStringLiteral("campplus")};
        const dm::MergeResult r = dm::apply(t, d);
        QVERIFY(r.applied);
        QCOMPARE(speakers(t), repeat("speaker:1", 10) + repeat("speaker:2", 10));
        QCOMPARE(r.labels.value(dm::speakerKey("loop", 0)), QStringLiteral("speaker:1"));
        QCOMPARE(r.labels.value(dm::speakerKey("loop", 1)), QStringLiteral("speaker:2"));
        QCOMPARE(r.changedTokens, 10);
        QCOMPARE(t.speakerSource, dm::kSourceLocalDiarization);
        QCOMPARE(t.speakerModels, QStringList({"seg", "campplus"}));
        // The speaker change starts a new line.
        const QVector<Utterance> lines = t.segments();
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines[0].speaker, QStringLiteral("speaker:1"));
        QCOMPARE(lines[1].speaker, QStringLiteral("speaker:2"));

        // A token straddling the change goes to the larger overlap.
        MergedTranscript s;
        words(s, 0, 1);
        s.tokens[0].startMs = 2900;
        s.tokens[0].endMs = 3200;   // 100 ms in A, 200 ms in B
        const dm::MergeResult rs = dm::apply(s, d);
        QVERIFY(rs.applied);
        QCOMPARE(rs.labels.value(dm::speakerKey("loop", 1)), QStringLiteral("speaker:1"));   // B, the first label
        QCOMPARE(s.tokens[0].speaker, QStringLiteral("speaker:1"));
    }

    void islandsAreSmoothedBackchannelsKept()
    {
        // A talks 0-6 s; the diarization saw a 250 ms B blip at 3.0 s inside the line: one token -> island.
        MergedTranscript t;
        words(t, 0, 20);
        providers::DiarizeResult d;
        d.turns = {turn("loop", 0, 0, 6000), turn("loop", 1, 3000, 3250)};
        // (B's blip overlaps A's turn too; give B the larger share of that token.)
        d.turns[0].endMs = 2990;
        d.turns.append(turn("loop", 0, 3260, 6000));
        QVERIFY(dm::apply(t, d).applied);
        QCOMPARE(speakers(t), repeat("speaker:1", 20));

        // The same "igen" as its own line (pauses > 1.5 s around it) stays B's.
        MergedTranscript b;
        words(b, 0, 4);                      // A: 0 - 1.15 s
        words(b, 3000, 1, QStringLiteral("igen"));   // B alone at 3 s
        words(b, 6000, 4);                   // A again
        providers::DiarizeResult d2;
        d2.turns = {turn("loop", 0, 0, 1200), turn("loop", 1, 2950, 3300), turn("loop", 0, 5900, 7200)};
        QVERIFY(dm::apply(b, d2).applied);
        QCOMPARE(speakers(b), repeat("speaker:1", 4) + QStringList{"speaker:2"} + repeat("speaker:1", 4));
    }

    void tokensOutsideTheTurns()
    {
        MergedTranscript t;
        words(t, 0, 6);       // 0 - 1.75 s: the turn ends at 1.0 s; the rest of the line follows the block
        words(t, 10000, 2);   // 10 s: far from any turn, its own line -> the nearest turn (B at 7-8 s)
        providers::DiarizeResult d;
        d.turns = {turn("loop", 0, 0, 1000), turn("loop", 1, 7000, 8000)};
        QVERIFY(dm::apply(t, d).applied);
        QCOMPARE(speakers(t), repeat("speaker:1", 6) + repeat("speaker:2", 2));
        // A token just after a turn snaps to it (within 1 s).
        MergedTranscript s;
        words(s, 1500, 1);
        providers::DiarizeResult d1;
        d1.turns = {turn("loop", 3, 0, 1000), turn("loop", 4, 5000, 6000)};
        QVERIFY(dm::apply(s, d1).applied);
        QCOMPARE(s.tokens[0].speaker, QStringLiteral("speaker:1"));
        QCOMPARE(dm::apply(s, d1).labels.value(dm::speakerKey("loop", 3)), QStringLiteral("speaker:1"));
    }

    void tracksAreSeparateSpeakers()
    {
        // Two tracks, each with its own speaker 0 (meeting time: the mic track started 5 s later, its turn is
        // already shifted). They are different raw speakers.
        MergedTranscript t;
        words(t, 0, 10);       // 0 - 2.95 s: the loopback's speaker 0
        words(t, 5000, 10);    // 5 - 7.95 s: the mic's speaker 0
        providers::DiarizeResult d;
        d.turns = {turn("loopback", 0, 0, 3000), turn("mic", 0, 5000, 8000)};
        const dm::MergeResult r = dm::apply(t, d);
        QVERIFY(r.applied);
        QCOMPARE(speakers(t), repeat("speaker:1", 10) + repeat("speaker:2", 10));
        QCOMPARE(r.labels.value(dm::speakerKey("loopback", 0)), QStringLiteral("speaker:1"));
        QCOMPARE(r.labels.value(dm::speakerKey("mic", 0)), QStringLiteral("speaker:2"));
        // A speaker without a token gets no label.
        d.turns.append(turn("mic", 1, 20000, 21000));
        MergedTranscript t2;
        words(t2, 0, 10);
        QCOMPARE(dm::apply(t2, d).labels.size(), 1);
    }

    void untouchedCases()
    {
        providers::DiarizeResult d;
        d.turns = {turn("loop", 0, 0, 3000), turn("loop", 1, 3000, 6000)};

        // A transcript the provider diarized (Soniox): two labels -> untouched.
        MergedTranscript soniox;
        words(soniox, 0, 10);
        for (int i = 5; i < 10; ++i) soniox.tokens[i].speaker = QStringLiteral("speaker:2");
        const MergedTranscript before = soniox;
        QVERIFY(!dm::needsSpeakers(soniox));
        const dm::MergeResult r = dm::apply(soniox, d);
        QVERIFY(!r.applied);
        QVERIFY(!r.reason.isEmpty());
        QCOMPARE(speakers(soniox), speakers(before));
        QVERIFY(soniox.speakerSource.isEmpty());

        // Empty transcript / no turns.
        MergedTranscript empty;
        QVERIFY(!dm::needsSpeakers(empty));
        QVERIFY(!dm::apply(empty, d).applied);
        MergedTranscript t;
        words(t, 0, 3);
        QVERIFY(dm::needsSpeakers(t));
        QVERIFY(!dm::apply(t, providers::DiarizeResult()).applied);
        QCOMPARE(speakers(t), repeat("speaker:1", 3));
        // Unlabelled tokens count as "no speakers" too.
        for (TranscriptToken& tok : t.tokens) tok.speaker.clear();
        QVERIFY(dm::needsSpeakers(t));
        QCOMPARE(dm::rawLabel(3), QStringLiteral("speaker:3"));
    }

    void metadataRoundTrip()
    {
        QTemporaryDir dir;
        MergedTranscript t;
        t.language = QStringLiteral("hu");
        words(t, 0, 4);
        providers::DiarizeResult d;
        d.turns = {turn("loop", 0, 0, 3000)};
        d.segmentationModelId = QStringLiteral("pyannote-segmentation-3.0");
        d.embeddingModelIds = {QStringLiteral("campplus"), QStringLiteral("wespeaker-resnet34-lm")};
        QVERIFY(dm::apply(t, d).applied);
        QVERIFY(library::TranscriptRepository::writeTokens(dir.path(), t));
        const MergedTranscript back = library::TranscriptRepository::readTokens(dir.path());
        QCOMPARE(back.speakerSource, dm::kSourceLocalDiarization);
        QCOMPARE(back.speakerModels, t.speakerModels);
        QCOMPARE(speakers(back), speakers(t));
        // A provider transcript has no "speakers" object.
        MergedTranscript plain;
        words(plain, 0, 2);
        QVERIFY(library::TranscriptRepository::writeTokens(dir.path(), plain));
        QVERIFY(library::TranscriptRepository::readTokens(dir.path()).speakerSource.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestDiarizationMerge)
#include "test_diarization_merge.moc"
