#pragma once
// Shared fixture for the view-model tests of the job views (PreTranscript / Summary / Track):
// an isolated TANARA_HOME (QTemporaryDir), a real AppController, and a local fake HTTP server
// standing in for the Soniox-like STT and OpenAI-compatible LLM endpoints; the same pattern as
// in tests/unit/test_app_jobs.cpp. No real (paid) call is made, and ~/.tanara is not touched.
//
// Header-only; the tests use it with their own QTEST_MAIN.
#include "tanara/app/AppController.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/MeetingStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtEndian>

#include <cmath>
#include <functional>
#include <memory>

namespace jobtest {

struct FakeRequest { QByteArray method; QString path; QByteArray body; };
struct FakeReply {
    int status = 200;
    QByteArray body = "{}";
    bool hold = false;   // do not respond (a hanging request, to test the running state / cancellation)
};

// Minimal fake HTTP server (127.0.0.1, random port).
class FakeHttp : public QObject {
public:
    std::function<FakeReply(const FakeRequest&)> handler;
    QVector<FakeRequest> log;

    FakeHttp() {
        connect(&m_srv, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* s = m_srv.nextPendingConnection()) {
                auto buf = std::make_shared<QByteArray>();
                connect(s, &QTcpSocket::readyRead, this, [this, s, buf]() { feed(s, *buf); });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
        m_srv.listen(QHostAddress::LocalHost);
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1/v1").arg(m_srv.serverPort()); }

private:
    void feed(QTcpSocket* s, QByteArray& buf) {
        buf += s->readAll();
        const int headEnd = buf.indexOf("\r\n\r\n");
        if (headEnd < 0) return;
        const QByteArray head = buf.left(headEnd);
        qint64 len = 0;
        for (const QByteArray& line : head.split('\n')) {
            const QByteArray l = line.trimmed().toLower();
            if (l.startsWith("content-length:")) len = l.mid(15).trimmed().toLongLong();
        }
        if (buf.size() < headEnd + 4 + len) return;   // the body has not fully arrived yet
        FakeRequest req;
        const QList<QByteArray> first = head.left(head.indexOf('\r')).split(' ');
        req.method = first.value(0);
        req.path = QString::fromUtf8(first.value(1));
        req.body = buf.mid(headEnd + 4, len);
        buf.clear();
        log.append(req);
        const FakeReply rep = handler ? handler(req) : FakeReply{};
        if (rep.hold) return;
        const QByteArray out = "HTTP/1.1 " + QByteArray::number(rep.status) + " X\r\n"
                               "Content-Type: application/json\r\n"
                               "Content-Length: " + QByteArray::number(rep.body.size()) + "\r\n"
                               "Connection: close\r\n\r\n" + rep.body;
        s->write(out);
        s->disconnectFromHost();
    }
    QTcpServer m_srv;
};

// An OpenAI-compatible chat reply with the given content.
inline QByteArray chat(const QString& content)
{
    const QJsonObject msg{{"role", "assistant"}, {"content", content}};
    const QJsonObject root{{"choices", QJsonArray{QJsonObject{{"message", msg}, {"finish_reason", "stop"}}}}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// Successful replies of the Soniox flow; status: the state of /transcriptions/<id>.
inline FakeReply sonioxOk(const FakeRequest& r, const QByteArray& status = "completed")
{
    static const QByteArray tokens =
        "{\"tokens\":[{\"text\":\"Sziasztok,\",\"start_ms\":0,\"end_ms\":400,\"confidence\":0.9,\"speaker\":\"1\"},"
        "{\"text\":\" kezdjük.\",\"start_ms\":400,\"end_ms\":900,\"confidence\":0.9,\"speaker\":\"1\"},"
        "{\"text\":\" Rendben.\",\"start_ms\":2000,\"end_ms\":2500,\"confidence\":0.9,\"speaker\":\"2\"}]}";
    if (r.method == "POST" && r.path.endsWith("/files")) return {200, "{\"id\":\"f1\"}"};
    if (r.method == "POST" && r.path.endsWith("/transcriptions")) return {200, "{\"id\":\"t1\"}"};
    if (r.method == "GET" && r.path.endsWith("/transcript")) return {200, tokens};
    if (r.method == "GET" && r.path.contains("/transcriptions/"))
        return {200, "{\"status\":\"" + status + "\"}"};
    return {200, "{}"};
}

// 16-bit mono WAV (sine): real audio for the mixdown / waveform.
inline bool writeWav(const QString& path, int seconds, int rate = 8000)
{
    const int n = seconds * rate;
    QByteArray pcm(n * 2, '\0');
    auto* s = reinterpret_cast<qint16*>(pcm.data());
    for (int i = 0; i < n; ++i)
        s[i] = qToLittleEndian<qint16>(qint16(8000.0 * std::sin(2.0 * M_PI * 220.0 * i / rate)));
    QByteArray h;
    auto le32 = [&h](quint32 v) { char b[4]; qToLittleEndian(v, b); h.append(b, 4); };
    auto le16 = [&h](quint16 v) { char b[2]; qToLittleEndian(v, b); h.append(b, 2); };
    h.append("RIFF"); le32(quint32(36 + pcm.size()));
    h.append("WAVEfmt "); le32(16); le16(1); le16(1);
    le32(quint32(rate)); le32(quint32(rate * 2)); le16(2); le16(16);
    h.append("data"); le32(quint32(pcm.size()));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(h);
    f.write(pcm);
    return true;
}

// Isolated app: own TANARA_HOME + fake server + AppController (starts without keys, so the
// "missing key" branch of the gating can be tested too; set up by configure()).
struct Sandbox {
    std::unique_ptr<QTemporaryDir> home;
    std::unique_ptr<FakeHttp> http;
    std::unique_ptr<tanara::app::AppController> app;
    bool ffmpeg = false;

    Sandbox()
    {
        qputenv("TANARA_CLOUD", "off");
        ffmpeg = !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();
        home = std::make_unique<QTemporaryDir>();
        qputenv("TANARA_HOME", home->path().toUtf8());
        http = std::make_unique<FakeHttp>();
        app = std::make_unique<tanara::app::AppController>();
        tanara::AppSettings s = app->settings()->settings();
        s.sttProviderId = QStringLiteral("soniox");
        s.sttConfigs[s.sttProviderId].baseUrl = http->base();
        s.llmProviderId = QStringLiteral("openai-compat");
        s.llmConfigs[s.llmProviderId].baseUrl = http->base();
        s.llmConfigs[s.llmProviderId].model = QStringLiteral("teszt-modell");
        app->settings()->setSettings(s);
    }
    ~Sandbox()
    {
        app.reset();
        http.reset();
        home.reset();
    }

    // Sets the Soniox key (which makes transcription startable).
    void configureStt() { app->setSecret(tanara::app::keys::SonioxApiKey, QStringLiteral("teszt-kulcs")); }

    // A recording with one (or more) WAV tracks; the second track is dropped if dropSecond.
    tanara::Meeting recording(const QString& title, int seconds, bool secondTrack = false,
                              bool dropSecond = false)
    {
        tanara::Meeting m = app->store()->createMeeting(title);
        tanara::Track t;
        t.id = "mic"; t.kind = tanara::TrackKind::Mic; t.deviceName = "Teszt mikrofon";
        t.file = "track_mic.wav"; t.speakerLabel = "Én"; t.fixedSpeaker = true; t.active = true;
        m.tracks = {t};
        writeWav(QDir(m.folder).filePath(t.file), seconds);
        if (secondTrack) {
            tanara::Track l;
            l.id = "loop"; l.kind = tanara::TrackKind::Loopback;
            l.deviceName = "Monitor of Teszt Hangkimenet";
            l.file = "track_loop.wav"; l.active = !dropSecond;
            m.tracks.append(l);
            writeWav(QDir(m.folder).filePath(l.file), seconds);
        }
        m.durationMs = seconds * 1000;
        app->store()->saveMeeting(m);
        return m;
    }

    // A meeting with a finished transcript, no provider call (two speakers).
    tanara::Meeting transcribed(const QString& title)
    {
        tanara::Meeting m = recording(title, 2);
        QJsonArray toks;
        const QStringList words{"Sziasztok,", " az", " ajánlatot", " holnap", " küldjük."};
        for (int i = 0; i < words.size(); ++i)
            toks.append(QJsonObject{{"text", words[i]}, {"speaker", i < 3 ? "Beszélő 1" : "Beszélő 2"},
                                    {"startMs", i * 300}, {"endMs", i * 300 + 250},
                                    {"confidence", 0.9}, {"trackId", "mixdown"}});
        QFile f(QDir(m.folder).filePath("transcript.tokens.json"));
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(QJsonObject{{"language", "hu"}, {"tokens", toks}}).toJson());
        f.close();
        QFile seg(QDir(m.folder).filePath("transcript.segments.json"));
        if (seg.open(QIODevice::WriteOnly))
            seg.write(QJsonDocument(QJsonArray{
                QJsonObject{{"startMs", 0}, {"endMs", 850}, {"speaker", "Beszélő 1"}, {"text", "Sziasztok, az ajánlatot"}},
                QJsonObject{{"startMs", 900}, {"endMs", 1450}, {"speaker", "Beszélő 2"}, {"text", "holnap küldjük."}}}).toJson());
        seg.close();
        m.hasTranscript = true;
        app->store()->saveMeeting(m);
        return m;
    }

    // A long (multi-part) transcript: one paragraph every half minute, two speakers alternating;
    // the quick summary splits it into ~15-minute parts (40 minutes = 3 parts). Same as in test_app_jobs.
    tanara::Meeting transcribedLong(const QString& title, int minutes)
    {
        tanara::Meeting m = recording(title, 1);
        QJsonArray toks;
        for (int i = 0; i < minutes * 2; ++i)
            toks.append(QJsonObject{{"text", QStringLiteral(" A(z) %1. bekezdés szövege, elég hosszan ahhoz, hogy "
                                                            "számítson: egy kitalált megbeszélésen fél perc alatt "
                                                            "ennél jóval több szó hangzik el.").arg(i)},
                                    {"speaker", i % 2 ? "Beszélő 2" : "Beszélő 1"}, {"startMs", i * 30000},
                                    {"endMs", i * 30000 + 20000}, {"confidence", 0.9}, {"trackId", "mixdown"}});
        QFile f(QDir(m.folder).filePath("transcript.tokens.json"));
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(QJsonObject{{"language", "hu"}, {"tokens", toks}}).toJson());
        f.close();
        m.hasTranscript = true;
        m.durationMs = qint64(minutes) * 60000;
        app->store()->saveMeeting(m);
        return m;
    }
};

// The quick-summary request: whether it is a part-note call (the "notes" prompt), and if so
// which part (1-based); 0 = a single-pass or merge call.
inline int notesPart(const FakeRequest& r)
{
    const QJsonArray msgs = QJsonDocument::fromJson(r.body).object().value("messages").toArray();
    if (!msgs.at(0).toObject().value("content").toString().contains("ONE PART")) return 0;
    static const QRegularExpression re(QStringLiteral("^PART (\\d+) of"), QRegularExpression::MultilineOption);
    return re.match(msgs.at(1).toObject().value("content").toString()).captured(1).toInt();
}

// A made-up note for one part (TOPICS / DECISIONS / OPEN / ACTIONS).
inline QString partNotes(int k)
{
    return QStringLiteral("TOPICS\n### [%1:00-%1:40] Tárgy %2\n- Egy pont a(z) %2. részből.\n"
                          "DECISIONS\n- none\nOPEN\n- [%1:30] Nyitott ügy %2\nACTIONS\n- none\n")
        .arg((k - 1) * 15).arg(k);
}

// The made-up reply for the merge.
inline QByteArray mergeReply()
{
    return chat(QStringLiteral(
        R"({"execSummary":"Hosszú megbeszélés összegzése.","decisions":["[05:00] Marad a terv."],)"
        R"("openQuestions":["[31:30] Nyitott ügy 3","Ki viszi tovább?"],)"
        R"("actionItems":[{"text":"Terv megírása","owner":"Beszélő 1","due":"péntek"}]})"));
}

} // namespace jobtest
