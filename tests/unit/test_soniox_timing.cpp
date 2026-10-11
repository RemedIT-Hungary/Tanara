// Soniox token-időzítés: a tokenek kiadási pillanata (~60 ms) → hang-tartomány (kLeadMs előre,
// kTailMs hátra, a szomszédokig). Tiszta függvény + a SonioxProvider egy kézzel írt válaszon
// (folyamaton belüli ál-HTTP, valódi hálózat nincs).
#include "fake_http.h"

#include "tanara/stt/SonioxProvider.h"
#include "tanara/stt/SonioxTiming.h"

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

} // namespace

class TestSonioxTiming : public QObject {
    Q_OBJECT

private slots:
    void correctionExtendsToNeighbours()
    {
        // A mért minta: „R" „ok" „i." — 60 ms széles, 60 ms szünetekkel; utána 2 s csend.
        QVector<TranscriptToken> t{tok(127410, 127470), tok(127530, 127590), tok(127650, 127710),
                                   tok(129710, 129770)};
        sonioxtiming::correctTrackTokens(t);
        QCOMPARE(t[0].startMs, 127110);   // −300 (nincs előző)
        QCOMPARE(t[0].endMs, 127470);     // a következő új kezdetéig (= a saját régi vége)
        QCOMPARE(t[1].startMs, 127470);   // az előző token végéig
        QCOMPARE(t[1].endMs, 127590);
        QCOMPARE(t[2].startMs, 127590);
        QCOMPARE(t[2].endMs, 127810);     // +100 (a következő 300 ms-mal előrébb is messze van)
        QCOMPARE(t[3].startMs, 129410);
        QCOMPARE(t[3].endMs, 129870);
        for (int i = 1; i < t.size(); ++i) QVERIFY(t[i].startMs >= t[i - 1].endMs);
    }

    void correctionClampsAtZeroSortsAndKeepsOverlapsSane()
    {
        QVector<TranscriptToken> t{tok(500, 560, QStringLiteral("b")), tok(100, 160, QStringLiteral("a")),
                                   tok(520, 900, QStringLiteral("c"))};   // c átfedi b-t
        sonioxtiming::correctTrackTokens(t);
        QCOMPARE(t[0].text, QStringLiteral("a"));
        QCOMPARE(t[0].startMs, 0);
        QCOMPARE(t[1].text, QStringLiteral("b"));
        QCOMPARE(t[1].startMs, 200);      // max(160, 500−300)
        QCOMPARE(t[2].startMs, 520);      // nem kerül későbbre az eredetinél
        QCOMPARE(t[1].endMs, 520);        // a vég a következő kezdetéig rövidül → nincs átfedés
        QCOMPARE(t[2].endMs, 1000);
        for (int i = 1; i < t.size(); ++i) QVERIFY(t[i].startMs >= t[i - 1].endMs);
        for (const auto& x : t) QVERIFY(x.endMs >= x.startMs);
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

    // A provider a kézzel írt Soniox-válasz tokenjeit korrigálva adja vissza.
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
        QCOMPARE(tt.tokens[0].startMs, 127110);
        QCOMPARE(tt.tokens[0].endMs, 127470);
        QCOMPARE(tt.tokens[1].startMs, 127470);
        QCOMPARE(tt.tokens[1].endMs, 127590);
        QCOMPARE(tt.tokens[2].startMs, 127590);
        QCOMPARE(tt.tokens[2].endMs, 127810);
        QCOMPARE(tt.tokens[2].text, QStringLiteral("i."));
        QCOMPARE(tt.tokens[2].speaker, QStringLiteral("Beszélő 1"));
        delete job;
    }
};

QTEST_GUILESS_MAIN(TestSonioxTiming)
#include "test_soniox_timing.moc"
