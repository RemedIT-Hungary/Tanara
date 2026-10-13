// Soniox-style timing: the tokens mark the moment of emission (~60 ms). The tokens are
// untouched; after grouping, the merger pads the finished lines (kLeadMs forward, kTailMs
// back, up to the neighboring lines), only with the emissionTiming flag (Soniox); the Whisper
// path is unchanged. Pure functions + SonioxProvider on a hand-written reply (fake HTTP, no real network).
#include "fake_http.h"

#include "tanara/providers/stt/SonioxProvider.h"
#include "tanara/processing/stt/SonioxTiming.h"
#include "tanara/processing/summary/TranscriptMerger.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace tanara;

namespace {

TranscriptToken tok(qint64 s, qint64 e, const QString& text = QStringLiteral("x"))
{
    TranscriptToken t;
    t.text = text;
    t.startMs = s;
    t.endMs = e;
    return t;
}

Utterance utt(qint64 s, qint64 e, const QString& spk = QStringLiteral("A"))
{
    return Utterance{s, e, spk, QStringLiteral("x")};
}

TrackTranscript track(const QString& id, const QString& spk, const QVector<QPair<qint64, qint64>>& times, bool emission)
{
    TrackTranscript tt;
    tt.trackId = id;
    tt.speakerLabel = spk;
    tt.emissionTiming = emission;
    for (const auto& t : times) tt.tokens.append(TranscriptToken{QStringLiteral(" w"), QString(), t.first, t.second, 0.9, id});
    return tt;
}

} // namespace

class TestSonioxTiming : public QObject {
    Q_OBJECT

private slots:
    void padUtterancesToNeighbours()
    {
        QVector<Utterance> u{utt(1000, 1300), utt(1450, 2000, "B"), utt(5000, 5060), utt(5100, 5900, "B"),
                             utt(9000, 9060)};
        processing::sonioxtiming::padUtterances(u);
        QCOMPARE(u[0].startMs, 700);       // −300
        QCOMPARE(u[0].endMs, 1300);        // the next one's new start (max(1300, 1150) = 1300)
        QCOMPARE(u[1].startMs, 1300);      // up to the previous line's end
        QCOMPARE(u[1].endMs, 2100);        // +100
        QCOMPARE(u[2].startMs, 4700);
        QCOMPARE(u[2].endMs, 5060);        // the next (B) one's new start 5060
        QCOMPARE(u[3].startMs, 5060);
        QCOMPARE(u[3].endMs, 6000);
        QCOMPARE(u[4].startMs, 8700);
        QCOMPARE(u[4].endMs, 9160);
        for (int i = 1; i < u.size(); ++i) {
            QVERIFY(u[i].startMs >= u[i - 1].endMs);
            QVERIFY(u[i].startMs >= u[i - 1].startMs);
        }
    }

    void padUtterancesKeepsOverlapsAndTies()
    {
        // The same start (two speakers at once): they move equally; an overlapping line is not shortened.
        QVector<Utterance> u{utt(100, 400), utt(1000, 1060), utt(1000, 1500, "B"), utt(1200, 1300, "C")};
        processing::sonioxtiming::padUtterances(u);
        QCOMPARE(u[0].startMs, 0);         // not below 0
        QCOMPARE(u[1].startMs, 700);
        QCOMPARE(u[2].startMs, 700);
        QCOMPARE(u[1].endMs, 1160);        // +100 (C's start, 1200, does not limit)
        QCOMPARE(u[2].endMs, 1500);        // not shorter even with an overlap
        QCOMPARE(u[3].startMs, 1200);      // the earlier B is still speaking, so no lead-in
        QCOMPARE(u[3].endMs, 1400);
        QCOMPARE(u[0].endMs, 500);
    }

    // The bug measured on a real meeting: with token-level padding a 1560 ms gap shrank to 1160
    // ms, and the short line merged into the previous one. Line-level padding cannot do that.
    void shortLineAfterLongLineStaysSeparate()
    {
        QVector<QPair<qint64, qint64>> times;
        for (qint64 t = 0; t <= 13000; t += 1000) times.append({t, t + 60});
        times.append({14620, 14680});
        times.append({14740, 14800});
        const MergedTranscript raw = processing::mergeTranscripts({track("t1", "A", times, false)});
        const MergedTranscript padded = processing::mergeTranscripts({track("t1", "A", times, true)});
        QVERIFY(padded.emissionTiming);
        QVERIFY(!raw.emissionTiming);
        const QVector<Utterance> a = raw.segments(), b = padded.segments();
        QCOMPARE(a.size(), 2);
        QCOMPARE(b.size(), 2);
        QCOMPARE(b[1].text, a[1].text);
        QCOMPARE(b[1].startMs, 14320);
        QCOMPARE(b[1].endMs, 14900);
        QCOMPARE(b[0].endMs, 13160);
        // The tokens are untouched.
        QCOMPARE(padded.tokens.size(), raw.tokens.size());
        for (int i = 0; i < raw.tokens.size(); ++i) {
            QCOMPARE(padded.tokens[i].startMs, raw.tokens[i].startMs);
            QCOMPARE(padded.tokens[i].endMs, raw.tokens[i].endMs);
        }
    }

    // Whisper path (without the flag): the lines come exactly from the token times, unchanged.
    void whisperPathUnchanged()
    {
        const MergedTranscript mt = processing::mergeTranscripts({track("t1", "A", {{0, 320}, {320, 700}}, false),
                                                      track("t2", "B", {{2000, 2450}}, false)});
        const QVector<Utterance> s = mt.segments();
        QCOMPARE(s.size(), 2);
        QCOMPARE(s[0].startMs, 0);
        QCOMPARE(s[0].endMs, 700);
        QCOMPARE(s[1].startMs, 2000);
        QCOMPARE(s[1].endMs, 2450);
        QVERIFY(mt.renderMarkdown().startsWith(QStringLiteral("`[00:00]` **A**")));
    }

    void medianDetectsEmissionTiming()
    {
        QVERIFY(!processing::sonioxtiming::looksLikeEmissionTiming({}));
        QVector<TranscriptToken> soniox{tok(0, 60), tok(120, 180), tok(240, 300), tok(400, 900)};
        QCOMPARE(processing::sonioxtiming::medianTokenWidthMs(soniox), 60);
        QVERIFY(processing::sonioxtiming::looksLikeEmissionTiming(soniox));
        QVector<TranscriptToken> whisper{tok(0, 320), tok(320, 610), tok(700, 760), tok(800, 1300)};
        QVERIFY(!processing::sonioxtiming::looksLikeEmissionTiming(whisper));
    }

    // The provider returns the hand-written Soniox reply's tokens untouched, with the emissionTiming flag.
    void providerAppliesCorrection()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString audio = dir.filePath(QStringLiteral("track_1.ogg"));
        {
            QFile f(audio);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("OggS-fake");
        }
        fakehttp::Server srv;
        srv.handler = [](const fakehttp::Request& r) -> fakehttp::Reply {
            if (r.method == "POST" && r.path.endsWith(QLatin1String("/files"))) return {200, R"({"id":"f1"})"};
            if (r.method == "POST" && r.path.endsWith(QLatin1String("/transcriptions"))) return {200, R"({"id":"t1"})"};
            if (r.method == "GET" && r.path.endsWith(QLatin1String("/transcriptions/t1")))
                return {200, R"({"id":"t1","status":"completed"})"};
            if (r.method == "GET" && r.path.endsWith(QLatin1String("/transcriptions/t1/transcript")))
                return {200, R"({"tokens":[{"text":"R","start_ms":127410,"end_ms":127470,"confidence":0.9},)"
                             R"({"text":"ok","start_ms":127530,"end_ms":127590,"confidence":0.9},)"
                             R"({"text":"i.","start_ms":127650,"end_ms":127710,"confidence":0.9}]})"};
            return {200, "{}"};
        };
        ProviderConfig cfg;
        cfg.baseUrl = srv.base();
        cfg.apiKey = QStringLiteral("test");
        providers::SonioxProvider provider(cfg);
        providers::SttRequest req;
        req.audioFilePath = audio;
        req.trackId = QStringLiteral("t1");
        req.speakerLabel = QStringLiteral("Beszélő 1");
        providers::SttJob* job = provider.transcribe(req);
        QSignalSpy done(job, &providers::SttJob::finished);
        QSignalSpy failed(job, &providers::SttJob::failed);
        QTRY_VERIFY_WITH_TIMEOUT(done.count() == 1 || failed.count() == 1, 10000);
        QCOMPARE(failed.count(), 0);
        const TrackTranscript tt = done.at(0).at(0).value<TrackTranscript>();
        QCOMPARE(tt.tokens.size(), 3);
        QVERIFY(tt.emissionTiming);
        // The tokens come with Soniox's times (the padding happens on the lines, in the merger).
        QCOMPARE(tt.tokens[0].startMs, 127410);
        QCOMPARE(tt.tokens[0].endMs, 127470);
        QCOMPARE(tt.tokens[2].startMs, 127650);
        QCOMPARE(tt.tokens[2].endMs, 127710);
        const QVector<Utterance> segs = processing::mergeTranscripts({tt}).segments();
        QCOMPARE(segs.size(), 1);
        QCOMPARE(segs[0].startMs, 127110);
        QCOMPARE(segs[0].endMs, 127810);
        QCOMPARE(tt.tokens[2].text, QStringLiteral("i."));
        QCOMPARE(tt.tokens[2].speaker, QStringLiteral("Beszélő 1"));
        delete job;
    }
};

QTEST_GUILESS_MAIN(TestSonioxTiming)
#include "test_soniox_timing.moc"
