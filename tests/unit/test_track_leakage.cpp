// Cross-track leakage (D6, plan 4.2/3) on synthetic PCM: a remote voice leaking quietly into the microphone
// is detected and attributed to the loopback; real double-talk (loud on both tracks), a single track and a
// track whose speech is all leakage are left alone; the levels and the time offsets.
#include "tanara/processing/diarization/TrackLeakage.h"

#include <QtTest>

#include <cmath>

using namespace tanara::processing::leakage;

namespace {

constexpr int kSr = 16000;
constexpr double kPi = 3.14159265358979323846;

// A "voice": a 200 Hz tone of the given amplitude over [from, to) seconds (added).
void tone(QVector<float>& pcm, double from, double to, float amp, double hz = 200.0)
{
    for (int i = int(from * kSr); i < int(to * kSr) && i < pcm.size(); ++i)
        pcm[i] += amp * float(std::sin(2.0 * kPi * hz * i / kSr));
}

TrackInput input(const QString& id, const QVector<float>& pcm, const QVector<Turn>& speech, qint64 offsetMs = 0)
{
    TrackInput t;
    t.trackId = id;
    t.startOffsetMs = offsetMs;
    t.frameDb = frameDb(pcm, Options().frameMs);
    t.speech = speech;
    return t;
}

} // namespace

class TestTrackLeakage : public QObject {
    Q_OBJECT
private slots:
    void levels()
    {
        QVector<float> pcm(4 * kSr, 0.0f);
        tone(pcm, 1, 2, 0.5f);
        const QVector<float> db = frameDb(pcm, 50);
        QCOMPARE(db.size(), 80);
        QCOMPARE(db.first(), -100.0f);                       // digital silence
        QVERIFY(std::abs(db[30] - 20.0f * std::log10(0.5f / std::sqrt(2.0f))) < 0.2f);   // sine RMS
        QVERIFY(std::abs(meanLevel(db, 1, 2, 50) - db[30]) < 0.2f);
        QVERIFY(meanLevel(db, 1, 2, 50) > meanLevel(db, 0.5, 2.5, 50));   // silence lowers the energy mean
        QCOMPARE(meanLevel(db, 10, 11, 50), -100.0f);        // outside the audio
        QVERIFY(std::abs(speechLevel(db, {{1, 2}}, 50, 0.9) - db[30]) < 0.2f);
        QCOMPARE(speechLevel(db, {}, 50, 0.9), -100.0f);
    }

    void remoteVoiceLeakingIntoTheMic()
    {
        // Mic: the owner talks loudly 1-3 s and 9-10 s; the remote voice leaks in at -26 dB 5-7 s.
        // Loopback: the remote talks 5-7 s and 9-10 s (double-talk with the owner at 9-10 s).
        QVector<float> mic(12 * kSr, 0.0f), loop(12 * kSr, 0.0f);
        tone(mic, 1, 3, 0.4f);
        tone(mic, 9, 10, 0.4f);
        tone(mic, 5, 7, 0.02f, 330);
        tone(loop, 5, 7, 0.3f, 330);
        tone(loop, 9, 10, 0.3f, 330);
        const QVector<TrackInput> tracks{input("mic", mic, {{1, 3}, {5, 7}, {9, 10}}),
                                         input("loop", loop, {{5, 7}, {9, 10}})};
        const QVector<QVector<Leak>> leaks = detect(tracks);
        QCOMPARE(leaks.size(), 2);
        QCOMPARE(leaks[0].size(), 1);                    // only the quiet copy on the mic
        QCOMPARE(leaks[0][0].turn, 1);
        QCOMPARE(leaks[0][0].sourceTrackId, QStringLiteral("loop"));
        QVERIFY(leaks[0][0].levelDb < -20.0f);
        QVERIFY(leaks[0][0].sourceLevelDb > -3.0f);
        QVERIFY(leaks[1].isEmpty());                     // the loopback's speech is all its own
    }

    void notCoveredOrNotQuietIsKept()
    {
        QVector<float> mic(10 * kSr, 0.0f), loop(10 * kSr, 0.0f);
        tone(mic, 1, 3, 0.4f);
        tone(mic, 5, 7, 0.02f);
        tone(loop, 5, 5.8, 0.3f, 330);   // covers only 40 % of the quiet mic turn
        QVector<QVector<Leak>> leaks = detect({input("mic", mic, {{1, 3}, {5, 7}}), input("loop", loop, {{5, 5.8}})});
        QVERIFY(leaks[0].isEmpty());
        // Fully covered, but the loopback itself is quiet there (not at its speech level).
        QVector<float> loop2(10 * kSr, 0.0f);
        tone(loop2, 1, 2, 0.3f, 330);
        tone(loop2, 5, 7, 0.01f, 330);
        leaks = detect({input("mic", mic, {{1, 3}, {5, 7}}), input("loop", loop2, {{1, 2}, {5, 7}})});
        QVERIFY(leaks[0].isEmpty());
        QVERIFY(leaks[1].isEmpty());
        // A single track: nothing to compare.
        QVERIFY(detect({input("mic", mic, {{1, 3}, {5, 7}})})[0].isEmpty());
        // A mic that ONLY hears the loudspeaker: its speech level is the leaked level -> kept.
        QVector<float> onlyLeak(10 * kSr, 0.0f), loop3(10 * kSr, 0.0f);
        tone(onlyLeak, 5, 7, 0.02f);
        tone(loop3, 5, 7, 0.3f, 330);
        leaks = detect({input("mic", onlyLeak, {{5, 7}}), input("loop", loop3, {{5, 7}})});
        QVERIFY(leaks[0].isEmpty());
    }

    void echoOfTheOwnerOnTheLoopbackAndOffsets()
    {
        // The direction is not assumed: the owner's echo on the loopback (-20 dB) is a leak FROM the mic.
        // The loopback file starts 2 s later (startOffsetMs): its turn 3-5 s = meeting 5-7 s.
        QVector<float> mic(12 * kSr, 0.0f), loop(10 * kSr, 0.0f);
        tone(mic, 5, 7, 0.4f);
        tone(loop, 0.5, 2.5, 0.3f, 330);   // the remote's own speech: meeting 2.5-4.5
        tone(loop, 3, 5, 0.03f);           // the echo: meeting 5-7
        const QVector<QVector<Leak>> leaks =
            detect({input("mic", mic, {{5, 7}}), input("loop", loop, {{0.5, 2.5}, {3, 5}}, 2000)});
        QVERIFY(leaks[0].isEmpty());
        QCOMPARE(leaks[1].size(), 1);
        QCOMPARE(leaks[1][0].sourceTrackId, QStringLiteral("mic"));
        QVERIFY(std::abs(leaks[1][0].span.start - 3.0) < 1e-9);   // track time
    }
};

QTEST_GUILESS_MAIN(TestTrackLeakage)
#include "test_track_leakage.moc"
