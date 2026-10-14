// The Whisper-compatible STT provider (providers/stt/WhisperCompatProvider.h), against the in-process
// fake server (fake_http.h, no network): the multipart request (model, language, auth from the
// ProviderContext), the verbose_json parsing (top-level words, the segment fallback), the classified
// error of a failed call, and cancelling through the context's cancel token.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "fake_http.h"
#include "tanara/foundation/net/HttpClient.h"
#include "tanara/providers/core/ProviderContext.h"
#include "tanara/providers/stt/WhisperCompatProvider.h"

using namespace tanara;

class TestWhisperProvider : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QFile f(m_dir.filePath("track.ogg"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(2048, '\x11'));
    }

    void wordsWithContextAuth()
    {
        fakehttp::Server srv;
        srv.handler = [](const fakehttp::Request&) {
            fakehttp::Reply r;
            r.body = R"({"language":"en","words":[{"word":"Hello","start":0.5,"end":0.9},
                        {"word":"world","start":1.0,"end":1.4,"probability":0.8}]})";
            return r;
        };
        net::HttpClient http;
        providers::ProviderContext ctx;
        ctx.http = &http;
        ctx.auth = providers::AuthSource::fixed(QStringLiteral("ctx-key"));
        ctx.headers = {{"X-Test", "1"}};
        providers::ProviderConfig cfg;
        cfg.baseUrl = srv.base();
        cfg.model = QStringLiteral("whisper-x");
        cfg.extra.insert(QStringLiteral("language"), QStringLiteral("en"));
        providers::WhisperCompatProvider p(ctx, cfg);
        QCOMPARE(p.name(), QStringLiteral("whisper-compat"));

        providers::SttRequest req;
        req.audioFilePath = m_dir.filePath("track.ogg");
        req.trackId = QStringLiteral("t1");
        std::unique_ptr<providers::SttJob> job(p.transcribe(req));
        QSignalSpy fin(job.get(), &providers::SttJob::finished);
        QVERIFY(fin.wait(5000));
        const TrackTranscript tt = fin.first().first().value<TrackTranscript>();
        QCOMPARE(tt.language, QStringLiteral("en"));
        QCOMPARE(tt.tokens.size(), 2);
        QCOMPARE(tt.tokens[0].text, QStringLiteral("Hello"));
        QCOMPARE(tt.tokens[0].startMs, qint64(500));
        QCOMPARE(tt.tokens[1].confidence, 0.8);
        QCOMPARE(tt.tokens[1].speaker, QStringLiteral("1"));   // no diarization

        QCOMPARE(srv.log.size(), 1);
        const fakehttp::Request& r = srv.log[0];
        QCOMPARE(r.method, QByteArray("POST"));
        QCOMPARE(r.path, QStringLiteral("/v1/audio/transcriptions"));
        QVERIFY(r.head.contains("Authorization: Bearer ctx-key"));
        QVERIFY(r.head.contains("X-Test: 1"));
        QVERIFY(r.head.contains("User-Agent: Tanara/"));
        QVERIFY(r.body.contains("name=\"model\"\r\n\r\nwhisper-x"));
        QVERIFY(r.body.contains("name=\"language\"\r\n\r\nen"));
        QVERIFY(r.body.contains("verbose_json"));
    }

    // whisper.cpp without word timestamps: one token per segment.
    void segmentFallback()
    {
        fakehttp::Server srv;
        srv.handler = [](const fakehttp::Request&) {
            fakehttp::Reply r;
            r.body = R"({"segments":[{"text":" First line. ","start":0,"end":2.5}]})";
            return r;
        };
        providers::ProviderConfig cfg;
        cfg.baseUrl = srv.base();
        providers::WhisperCompatProvider p(cfg);   // no context: an own HTTP client
        providers::SttRequest req;
        req.audioFilePath = m_dir.filePath("track.ogg");
        std::unique_ptr<providers::SttJob> job(p.transcribe(req));
        QSignalSpy fin(job.get(), &providers::SttJob::finished);
        QVERIFY(fin.wait(5000));
        const TrackTranscript tt = fin.first().first().value<TrackTranscript>();
        QCOMPARE(tt.tokens.size(), 1);
        QCOMPARE(tt.tokens[0].text, QStringLiteral("First line."));
        QCOMPARE(tt.tokens[0].endMs, qint64(2500));
        QVERIFY(!srv.log[0].head.contains("Authorization"));   // no key, no header
        QVERIFY(srv.log[0].body.contains("name=\"model\"\r\n\r\nwhisper-1"));   // the default model
    }

    void failureIsClassified()
    {
        fakehttp::Server srv;
        srv.handler = [](const fakehttp::Request&) {
            fakehttp::Reply r;
            r.status = 401;
            r.body = R"({"error":{"code":"invalid_api_key","message":"bad key"}})";
            return r;
        };
        providers::ProviderConfig cfg;
        cfg.baseUrl = srv.base();
        cfg.apiKey = QStringLiteral("wrong");
        providers::WhisperCompatProvider p(cfg);
        providers::SttRequest req;
        req.audioFilePath = m_dir.filePath("track.ogg");
        std::unique_ptr<providers::SttJob> job(p.transcribe(req));
        QSignalSpy failed(job.get(), &providers::SttJob::failed);
        QVERIFY(failed.wait(5000));
        QVERIFY(failed.first().first().toString().contains(QStringLiteral("401")));
        QCOMPARE(job->error().kind, Error::Kind::Auth);
        QCOMPARE(job->error().code, QStringLiteral("invalid_api_key"));
        QVERIFY(srv.log[0].head.contains("Authorization: Bearer wrong"));   // the config's key
    }

    void cancelThroughContext()
    {
        fakehttp::Server srv;
        srv.handler = [](const fakehttp::Request&) { fakehttp::Reply r; r.hold = true; return r; };
        providers::ProviderContext ctx;
        providers::ProviderConfig cfg;
        cfg.baseUrl = srv.base();
        providers::WhisperCompatProvider p(ctx, cfg);
        providers::SttRequest req;
        req.audioFilePath = m_dir.filePath("track.ogg");
        std::unique_ptr<providers::SttJob> job(p.transcribe(req));
        QSignalSpy failed(job.get(), &providers::SttJob::failed);
        QTRY_COMPARE_WITH_TIMEOUT(srv.log.size(), 1, 5000);
        ctx.cancel.cancel();   // a copy of the provider's token
        QCOMPARE(failed.count(), 1);
        QCOMPARE(job->error().kind, Error::Kind::Cancelled);
    }

    void missingFileFails()
    {
        providers::ProviderConfig cfg;
        cfg.baseUrl = QStringLiteral("http://127.0.0.1:9/v1");
        providers::WhisperCompatProvider p(cfg);
        providers::SttRequest req;
        req.audioFilePath = m_dir.filePath("nope.ogg");
        std::unique_ptr<providers::SttJob> job(p.transcribe(req));
        QSignalSpy failed(job.get(), &providers::SttJob::failed);
        QVERIFY(failed.wait(5000));
        QVERIFY(job->error().isError());
    }

private:
    QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(TestWhisperProvider)
#include "test_whisper_provider.moc"
