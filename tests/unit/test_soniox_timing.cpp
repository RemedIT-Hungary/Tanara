// Soniox-féle időzítés: a tokenek a kiadás pillanatát jelölik (~60 ms). A tokenek érintetlenek;
// a merger a csoportosítás UTÁN a kész sorokat párnázza (kLeadMs előre, kTailMs hátra, a
// szomszéd sorokig) — csak emissionTiming jelzővel (Soniox); a Whisper-út változatlan.
// Tiszta függvények + a SonioxProvider egy kézzel írt válaszon (ál-HTTP, valódi hálózat nincs).
#include "fake_http.h"

#include "tanara/stt/SonioxProvider.h"
#include "tanara/stt/SonioxTiming.h"
#include "tanara/TranscriptMerger.h"

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
        sonioxtiming::padUtterances(u);
        QCOMPARE(u[0].startMs, 700);       // −300
        QCOMPARE(u[0].endMs, 1300);        // a következő új kezdete (max(1300, 1150) = 1300)
        QCOMPARE(u[1].startMs, 1300);      // az előző sor végéig
        QCOMPARE(u[1].endMs, 2100);        // +100
        QCOMPARE(u[2].startMs, 4700);
        QCOMPARE(u[2].endMs, 5060);        // a következő (B) új kezdete 5060
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
        // Azonos kezdet (két beszélő egyszerre): egyformán mozdulnak; átfedő sor nem rövidül.
        QVector<Utterance> u{utt(100, 400), utt(1000, 1060), utt(1000, 1500, "B"), utt(1200, 1300, "C")};
        sonioxtiming::padUtterances(u);
        QCOMPARE(u[0].startMs, 0);         // 0 alá nem
        QCOMPARE(u[1].startMs, 700);
        QCOMPARE(u[2].startMs, 700);
        QCOMPARE(u[1].endMs, 1160);        // +100 (a C kezdete, 1200, nem korlátoz)
        QCOMPARE(u[2].endMs, 1500);        // átfedőnél sem lesz rövidebb
        QCOMPARE(u[3].startMs, 1200);      // a korábbi B még szól → nincs elő-futás
        QCOMPARE(u[3].endMs, 1400);
        QCOMPARE(u[0].endMs, 500);
    }

    // A valódi meetingen mért hiba: token-szintű párnázásnál egy 1560 ms-os szünet 1160 ms-ra
    // szűkült, és a rövid sor beolvadt az előzőbe. A sor-szintű párnázás ezt nem teheti.
    void shortLineAfterLongLineStaysSeparate()
    {
        QVector<QPair<qint64, qint64>> times;
        for (qint64 t = 0; t <= 13000; t += 1000) times.append({t, t + 60});
        times.append({14620, 14680});
        times.append({14740, 14800});
        const MergedTranscript raw = mergeTranscripts({track("t1", "A", times, false)});
        const MergedTranscript padded = mergeTranscripts({track("t1", "A", times, true)});
        QVERIFY(padded.emissionTiming);
        QVERIFY(!raw.emissionTiming);
        const QVector<Utterance> a = raw.segments(), b = padded.segments();
        QCOMPARE(a.size(), 2);
        QCOMPARE(b.size(), 2);
        QCOMPARE(b[1].text, a[1].text);
        QCOMPARE(b[1].startMs, 14320);
        QCOMPARE(b[1].endMs, 14900);
        QCOMPARE(b[0].endMs, 13160);
        // A tokenek érintetlenek.
        QCOMPARE(padded.tokens.size(), raw.tokens.size());
        for (int i = 0; i < raw.tokens.size(); ++i) {
            QCOMPARE(padded.tokens[i].startMs, raw.tokens[i].startMs);
            QCOMPARE(padded.tokens[i].endMs, raw.tokens[i].endMs);
        }
    }

    // Whisper-út (jelző nélkül): a sorok pontosan a token-időkből, változatlanul.
    void whisperPathUnchanged()
    {
        const MergedTranscript mt = mergeTranscripts({track("t1", "A", {{0, 320}, {320, 700}}, false),
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
        QVERIFY(!sonioxtiming::looksLikeEmissionTiming({}));
        QVector<TranscriptToken> soniox{tok(0, 60), tok(120, 180), tok(240, 300), tok(400, 900)};
        QCOMPARE(sonioxtiming::medianTokenWidthMs(soniox), 60);
        QVERIFY(sonioxtiming::looksLikeEmissionTiming(soniox));
        QVector<TranscriptToken> whisper{tok(0, 320), tok(320, 610), tok(700, 760), tok(800, 1300)};
        QVERIFY(!sonioxtiming::looksLikeEmissionTiming(whisper));
    }

    // A provider a kézzel írt Soniox-válasz tokenjeit érintetlenül, emissionTiming jelzővel adja.
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
        SonioxProvider provider(cfg);
        SttRequest req;
        req.audioFilePath = audio;
        req.trackId = QStringLiteral("t1");
        req.speakerLabel = QStringLiteral("Beszélő 1");
        SttJob* job = provider.transcribe(req);
        QSignalSpy done(job, &SttJob::finished);
        QSignalSpy failed(job, &SttJob::failed);
        QTRY_VERIFY_WITH_TIMEOUT(done.count() == 1 || failed.count() == 1, 10000);
        QCOMPARE(failed.count(), 0);
        const TrackTranscript tt = done.at(0).at(0).value<TrackTranscript>();
        QCOMPARE(tt.tokens.size(), 3);
        QVERIFY(tt.emissionTiming);
        // A tokenek a Soniox szerinti időkkel jönnek (a párnázás a sorokon, a mergerben történik).
        QCOMPARE(tt.tokens[0].startMs, 127410);
        QCOMPARE(tt.tokens[0].endMs, 127470);
        QCOMPARE(tt.tokens[2].startMs, 127650);
        QCOMPARE(tt.tokens[2].endMs, 127710);
        const QVector<Utterance> segs = mergeTranscripts({tt}).segments();
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
