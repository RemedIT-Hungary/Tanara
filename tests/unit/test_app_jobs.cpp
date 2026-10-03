//
// AppController — strukturált feladat-réteg integrációs tesztje.
//
// Izolált TANARA_HOME (temp), valódi ffmpeg a lekeveréshez, és egy helyi ál-HTTP-szerver a
// Soniox-szerű STT- és az OpenAI-kompatibilis LLM-végpontok helyén (valódi, fizetős hívás
// NINCS). Ellenőrzi: szakasz-haladás, megmaradó (újraindítást túlélő) hibák, megszakítás
// minden fázisban (a felvételek és a korábbi eredmények érintetlenek), summary.json +
// metaadat, téma-lista átrendezése és a témánkénti állapot lekérdezése.
//
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QtEndian>
#include <functional>
#include <cmath>

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/audio/TrackCatalog.h"
#include "tanara/jobs/MeetingJobTracker.h"
#include "tanara/library/MeetingLibrary.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/edit/SpeakerOverlay.h"

using namespace tanara;

// A moc a nyers (R"(...)") string-literálokon elcsúszik → a segédeket és a teszt-törzseket
// nem látja (csak az osztály-deklarációt).
#ifndef Q_MOC_RUN
namespace {

// ---- minimális ál-HTTP-szerver ----------------------------------------------------------
struct FakeRequest { QByteArray method; QString path; QByteArray body; };
struct FakeReply {
    int status = 200;
    QByteArray body = "{}";
    bool hold = false;   // ne válaszoljon (függő kérés — megszakítás teszteléséhez)
};

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
    int count(const QByteArray& method, const QString& pathPart) const {
        int n = 0;
        for (const FakeRequest& r : log)
            if (r.method == method && r.path.contains(pathPart)) ++n;
        return n;
    }

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
        if (buf.size() < headEnd + 4 + len) return;   // a törzs még nem jött meg teljesen
        FakeRequest req;
        const QList<QByteArray> first = head.left(head.indexOf('\r')).split(' ');
        req.method = first.value(0);
        req.path = QString::fromUtf8(first.value(1));
        req.body = buf.mid(headEnd + 4, len);
        buf.clear();
        log.append(req);
        const FakeReply rep = handler ? handler(req) : FakeReply{};
        if (rep.hold) return;
        QByteArray out = "HTTP/1.1 " + QByteArray::number(rep.status) + " X\r\n"
                         "Content-Type: application/json\r\n"
                         "Content-Length: " + QByteArray::number(rep.body.size()) + "\r\n"
                         "Connection: close\r\n\r\n" + rep.body;
        s->write(out);
        s->disconnectFromHost();
    }
    QTcpServer m_srv;
};

// 16 bites mono WAV (szinusz) — a lekeverés valódi bemenete.
void writeWav(const QString& path, int seconds, int rate = 8000)
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
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(h);
    f.write(pcm);
}

QByteArray chat(const QString& content)
{
    QJsonObject msg{{"role", "assistant"}, {"content", content}};
    QJsonObject root{{"choices", QJsonArray{QJsonObject{{"message", msg}, {"finish_reason", "stop"}}}}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

const char* kTokensJson =
    R"({"tokens":[{"text":"Sziasztok,","start_ms":0,"end_ms":400,"confidence":0.9,"speaker":"1"},)"
    R"({"text":" kezdjük.","start_ms":400,"end_ms":900,"confidence":0.9,"speaker":"1"},)"
    R"({"text":" Rendben.","start_ms":2000,"end_ms":2500,"confidence":0.9,"speaker":"2"}]})";

// A Soniox-folyamat alapértelmezett (sikeres) válaszai.
FakeReply sonioxOk(const FakeRequest& r, const QByteArray& status = "completed")
{
    if (r.method == "POST" && r.path.endsWith("/files")) return {200, R"({"id":"f1"})"};
    if (r.method == "POST" && r.path.endsWith("/transcriptions")) return {200, R"({"id":"t1"})"};
    if (r.method == "GET" && r.path.endsWith("/transcript")) return {200, kTokensJson};
    if (r.method == "GET" && r.path.contains("/transcriptions/"))
        return {200, R"({"status":")" + status + R"("})"};
    return {200, "{}"};
}

} // namespace
#else
class FakeHttp;
#endif

class AppJobsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();

    void transcribeReportsStagesAndFinishes();
    void transcribeFailureKeptAfterRestart();
    void cancelTranscriptionAtProvider();
    void cancelDuringMixdownKeepsOldMixdown();
    void standaloneMixdownCancel();
    void summarizePersistsStructureAndMeta();
    void summarizeCancelAndFailure();
    void topicsEditReorderAndStatuses();
    void cancelTopicQueue();
    void renamedDuringTranscriptionSurvives();
    void identifyRunsAsLastStage();
    void retranscribeSuccessClearsNamesAndCorrections();
    void staleSummaryShowsInStateAndPending();

    // Adatbiztonság.
    void retryAfterFailedRetranscribeClearsNames();
    void emptySttResultKeepsOldTranscript();
    void missingTrackKeepsExistingMixdown();
    void identifyResultsDroppedWhenTranscriptChanges();

private:
    void newApp();
    Meeting recording(const QString& title, int seconds);
    Meeting transcribed(const QString& title);

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<FakeHttp> m_http;
    std::unique_ptr<AppController> m_app;
    QStringList m_errors;   // a régi errorOccurred csatorna
    bool m_ffmpeg = false;
};

#ifndef Q_MOC_RUN

void AppJobsTest::initTestCase()
{
    m_ffmpeg = !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();
    qputenv("TANARA_CLOUD", "off");
}

void AppJobsTest::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    qputenv("TANARA_HOME", m_home->path().toUtf8());
    m_http = std::make_unique<FakeHttp>();
    newApp();
}

void AppJobsTest::cleanup()
{
    m_app.reset();
    m_http.reset();
    m_home.reset();
}

void AppJobsTest::newApp()
{
    m_app.reset();
    m_errors.clear();
    m_app = std::make_unique<AppController>();
    connect(m_app.get(), &AppController::errorOccurred, this, [this](const QString& e) { m_errors << e; });
    AppSettings s = m_app->settings()->settings();
    QVERIFY(s.audioDir.startsWith(m_home->path()));   // TANARA_HOME: a felvételek is a sandboxban
    s.sttProviderId = QStringLiteral("soniox");
    s.sttConfigs[s.sttProviderId].baseUrl = m_http->base();
    s.llmProviderId = QStringLiteral("openai-compat");
    s.llmConfigs[s.llmProviderId].baseUrl = m_http->base();
    s.llmConfigs[s.llmProviderId].model = QStringLiteral("teszt-modell");
    m_app->settings()->setSettings(s);
    m_app->setSecret(keys::SonioxApiKey, QStringLiteral("teszt-kulcs"));
}

Meeting AppJobsTest::recording(const QString& title, int seconds)
{
    Meeting m = m_app->store()->createMeeting(title);
    Track t;
    t.id = "mic"; t.kind = TrackKind::Mic; t.deviceName = "Teszt mikrofon";
    t.file = "track_mic.wav"; t.speakerLabel = "Ádám"; t.fixedSpeaker = true; t.active = true;
    m.tracks = {t};
    m.durationMs = seconds * 1000;
    writeWav(QDir(m.folder).filePath(t.file), seconds);
    m_app->store()->saveMeeting(m);
    return m;
}

// Kész átirattal rendelkező meeting (az összefoglaló-tesztekhez), szolgáltató-hívás nélkül.
Meeting AppJobsTest::transcribed(const QString& title)
{
    Meeting m = recording(title, 2);
    QJsonArray toks;
    const QStringList words{"Sziasztok,", " az", " árajánlatot", " Ödön", " küldi."};
    for (int i = 0; i < words.size(); ++i)
        toks.append(QJsonObject{{"text", words[i]}, {"speaker", "Beszélő 1"}, {"startMs", i * 300},
                                {"endMs", i * 300 + 250}, {"confidence", 0.9}, {"trackId", "mixdown"}});
    QFile f(QDir(m.folder).filePath("transcript.tokens.json"));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(QJsonObject{{"language", "hu"}, {"tokens", toks}}).toJson());
    f.close();
    QFile seg(QDir(m.folder).filePath("transcript.segments.json"));
    if (seg.open(QIODevice::WriteOnly))
        seg.write(QJsonDocument(QJsonArray{
            QJsonObject{{"startMs", 0}, {"endMs", 550}, {"speaker", "Beszélő 1"}, {"text", "Sziasztok, az"}},
            QJsonObject{{"startMs", 600}, {"endMs", 1450}, {"speaker", "Beszélő 1"}, {"text", "árajánlatot Ödön küldi."}}}).toJson());
    seg.close();
    m.hasTranscript = true;
    m_app->store()->saveMeeting(m);
    return m;
}

void AppJobsTest::transcribeReportsStagesAndFinishes()
{
    if (!m_ffmpeg) QSKIP("ffmpeg nem található");
    m_http->handler = [](const FakeRequest& r) { return sonioxOk(r); };
    const Meeting m = recording("Heti egyeztetés", 3);
    MeetingJobTracker* jobs = m_app->jobs();

    // Minden haladás-pillanatkép rögzítése: szakasz → az elért állapotok sorrendje.
    QHash<QString, QVector<StageState>> seen;
    int uploadPercentMax = -1;
    connect(jobs, &MeetingJobTracker::jobProgressChanged, this, [&](const QString&, const JobProgress& p) {
        if (p.kind != JobKind::Transcribe) return;
        for (const JobStage& st : p.stages) {
            QVector<StageState>& v = seen[st.id];
            if (v.isEmpty() || v.last() != st.state) v.append(st.state);
            if (st.id == "upload" && st.state == StageState::Running)
                uploadPercentMax = qMax(uploadPercentMax, st.percent);
            if (st.id == "transcribe")
                QCOMPARE(st.percent, -1);   // a szolgáltató nem ad százalékot → nem találunk ki
        }
    });
    QSignalSpy finished(jobs, &MeetingJobTracker::jobFinished);
    QSignalSpy ready(m_app.get(), &AppController::transcriptReady);
    QSignalSpy legacy(m_app.get(), &AppController::jobProgress);

    m_app->transcribeMeeting(m.id);
    // Azonnal látszik: fut, megszakítható, szakasz-listával (a keverés kell → az az első).
    JobProgress jp = jobs->job(m.id, JobKind::Transcribe);
    QVERIFY(jp.isValid());
    QVERIFY(jp.cancellable);
    QStringList ids;
    for (const JobStage& st : jp.stages) ids << st.id;
    QCOMPARE(ids, QStringList({"mixdown", "upload", "transcribe", "diarize"}));   // (hang-modell nincs → nincs „identify”)
    QCOMPARE(jp.stage("mixdown")->state, StageState::Running);
    QVERIFY(jp.stage("upload")->detail.contains("1 sáv"));
    QCOMPARE(jp.estimatedTotalSec, -1);                       // korábbi futás nélkül nincs becslés
    MeetingProcessingState st = m_app->processingState(m.id);
    QCOMPARE(st.transcriptState, StepState::Running);
    QVERIFY(st.mixdownRunning);
    QCOMPARE(m_app->library()->entry(m.id).state.transcriptState, StepState::Running);
    m_app->transcribeMeeting(m.id);                           // dupla indítás: no-op

    QVERIFY(ready.wait(30000));
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(1).value<JobKind>(), JobKind::Transcribe);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Done);
    QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));
    QVERIFY(legacy.count() > 0);                              // a régi szabad szöveges jel is él

    // A szakaszok sorrendben végigmentek.
    QCOMPARE(seen["mixdown"], QVector<StageState>({StageState::Waiting, StageState::Running, StageState::Done}));
    QCOMPARE(seen["upload"], QVector<StageState>({StageState::Waiting, StageState::Running, StageState::Done}));
    QCOMPARE(seen["transcribe"], QVector<StageState>({StageState::Waiting, StageState::Running, StageState::Done}));
    QCOMPARE(seen["diarize"], QVector<StageState>({StageState::Waiting, StageState::Running, StageState::Done}));
    QVERIFY(uploadPercentMax >= 0);                           // valós feltöltés-százalék érkezett

    st = m_app->processingState(m.id);
    QCOMPARE(st.transcriptState, StepState::Done);
    QVERIFY(!st.busy());
    QVERIFY(!st.transcriptError.isValid());
    const Meeting after = m_app->store()->load(m.id);
    QVERIFY(after.hasTranscript);
    QCOMPARE(after.mixdownFile, QStringLiteral("mixdown.mp3"));
    for (const char* f : {"transcript.md", "transcript.tokens.json", "transcript.segments.json",
                          "mixdown.mp3", "track_mic.wav"})
        QVERIFY2(QDir(m.folder).exists(f), f);
    QVERIFY(!QDir(m.folder).exists("mixdown.part.mp3"));
    QCOMPARE(m_http->count("POST", "/files"), 1);             // a dupla indítás nem küldött még egyet
    QTRY_COMPARE(m_http->count("DELETE", "/"), 2);            // szolgáltató-oldali takarítás

    // A könyvtár-keresés az új átiratot is látja.
    LibraryQuery q;
    q.text = "kezdjuk";
    QCOMPARE(m_app->library()->query(q).count(), 1);
    QVERIFY(m_app->library()->pendingItems().isEmpty());
}

void AppJobsTest::transcribeFailureKeptAfterRestart()
{
    if (!m_ffmpeg) QSKIP("ffmpeg nem található");
    m_http->handler = [](const FakeRequest& r) -> FakeReply {
        if (r.method == "POST" && r.path.endsWith("/files"))
            return {401, R"({"status_code":401,"error_type":"invalid_api_key","message":"Invalid API key."})"};
        return {200, "{}"};
    };
    const Meeting m = recording("Bukó átírás", 2);
    const Meeting other = recording("Másik meeting", 2);
    QSignalSpy finished(m_app->jobs(), &MeetingJobTracker::jobFinished);
    QSignalSpy errChanged(m_app->jobs(), &MeetingJobTracker::errorChanged);
    m_app->transcribeMeeting(m.id);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Failed);
    QCOMPARE(errChanged.count(), 1);
    QCOMPARE(m_errors.size(), 1);                             // a régi globális hiba-jel is megjött

    MeetingProcessingState st = m_app->processingState(m.id);
    QCOMPARE(st.transcriptState, StepState::Failed);
    QCOMPARE(st.transcriptError.detail, QStringLiteral("HTTP 401 · invalid_api_key · Invalid API key."));
    QVERIFY(st.transcriptError.message.contains("API-kulcs"));
    QCOMPARE(st.transcriptError.fixActionHint, QStringLiteral("settings:stt"));
    // Másik meeting kiválasztása után is lekérdezhető; a másik meeting érintetlen.
    QCOMPARE(m_app->processingState(other.id).transcriptState, StepState::None);
    QCOMPARE(m_app->processingState(m.id).transcriptState, StepState::Failed);
    // A felvétel és a (már elkészült) keverék megmaradt.
    QVERIFY(QDir(m.folder).exists("track_mic.wav"));
    QVERIFY(!m_app->store()->load(m.id).hasTranscript);

    // „Ezek várnak rád”: a bukott és a még át nem írt meeting.
    QVector<PendingItem> pending = m_app->library()->pendingItems();
    QCOMPARE(pending.size(), 2);
    int failedCount = 0;
    for (const PendingItem& p : pending)
        if (p.kind == PendingKind::TranscriptionFailed) { ++failedCount; QCOMPARE(p.meetingId, m.id); }
    QCOMPARE(failedCount, 1);

    // Újraindítás után is látszik.
    newApp();
    st = m_app->processingState(m.id);
    QCOMPARE(st.transcriptState, StepState::Failed);
    QCOMPARE(st.transcriptError.detail, QStringLiteral("HTTP 401 · invalid_api_key · Invalid API key."));

    // Újrapróbálás jó kulccsal: siker → a hiba eltűnik.
    m_http->handler = [](const FakeRequest& r) { return sonioxOk(r); };
    QSignalSpy ready(m_app.get(), &AppController::transcriptReady);
    m_app->transcribeMeeting(m.id);
    QCOMPARE(m_app->jobs()->job(m.id, JobKind::Transcribe).stages.first().id, QStringLiteral("upload"));   // keverés már van
    QVERIFY(ready.wait(30000));
    QTRY_VERIFY(!m_app->jobs()->isBusy(m.id));
    st = m_app->processingState(m.id);
    QCOMPARE(st.transcriptState, StepState::Done);
    QVERIFY(!st.transcriptError.isValid());
    QVERIFY(!QDir(m.folder).exists(MeetingJobTracker::stateFileName()));
}

void AppJobsTest::cancelTranscriptionAtProvider()
{
    if (!m_ffmpeg) QSKIP("ffmpeg nem található");
    // A szolgáltató „örökké” feldolgoz.
    m_http->handler = [](const FakeRequest& r) { return sonioxOk(r, "processing"); };
    // Újra-átírás: van korábbi átirat és nevesített beszélő — megszakítás után mind megmarad.
    Meeting m = transcribed("Megszakított újra-átírás");
    m.speakerMap.insert("Beszélő 1", "Ödön");
    m_app->store()->saveMeeting(m);
    const QByteArray tokensBefore = [&] { QFile f(QDir(m.folder).filePath("transcript.tokens.json")); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }();

    // Kézi soronkénti javítás is van (overlay) — ennek is túl kell élnie a megszakítást.
    {
        SpeakerEditor* ed = m_app->speakerEditor(m.id);
        QVERIFY(ed);
        QCOMPARE(ed->utteranceCount(), 2);
        QVERIFY(!ed->moveUtterancesToPerson({ed->utteranceAt(1).id}, "Kovács Lilla").isEmpty());
        m_app->closeSpeakerEditor(m.id);
    }
    QCOMPARE(m_app->retranscribeImpact(m.id).correctedUtterances, 1);
    QVERIFY(QFile::exists(speakeredit::overlayPath(m.folder)));

    MeetingJobTracker* jobs = m_app->jobs();
    QSignalSpy finished(jobs, &MeetingJobTracker::jobFinished);
    m_app->retranscribeMeeting(m.id);
    // Megvárjuk, amíg a szolgáltatónál fut (a lekeverés és a feltöltés kész).
    QTRY_VERIFY_WITH_TIMEOUT(jobs->job(m.id, JobKind::Transcribe).isValid()
        && jobs->job(m.id, JobKind::Transcribe).stage("transcribe")->state == StageState::Running, 30000);
    QCOMPARE(m_app->store()->load(m.id).speakerMap.value("Beszélő 1"), QStringLiteral("Ödön"));   // nem törlődött előre

    QVERIFY(m_app->cancelJob(m.id, JobKind::Transcribe));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Cancelled);
    QVERIFY(!m_app->cancelJob(m.id, JobKind::Transcribe));    // már nincs mit megszakítani

    // Megszakítás ≠ hiba: nincs errorOccurred, nincs megmaradó hiba; az állapot konzisztens.
    QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));
    const MeetingProcessingState st = m_app->processingState(m.id);
    QCOMPARE(st.transcriptState, StepState::Done);            // a KORÁBBI átirat él
    QVERIFY(!st.transcriptError.isValid());
    QVERIFY(!st.busy());
    const Meeting after = m_app->store()->load(m.id);
    QVERIFY(after.hasTranscript);
    QCOMPARE(after.speakerMap.value("Beszélő 1"), QStringLiteral("Ödön"));
    QFile f(QDir(m.folder).filePath("transcript.tokens.json"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), tokensBefore);
    // A kézi sor-javítás is megvan (az overlay csak az ÚJ átirat megérkezésekor törlődne).
    QCOMPARE(m_app->retranscribeImpact(m.id).correctedUtterances, 1);
    QCOMPARE(m_app->retranscribeImpact(m.id).namedSpeakers, 2);
    QVERIFY(QDir(m.folder).exists("track_mic.wav"));          // a felvétel megvan
    // A szolgáltatónál a feltöltött hang és az átírás törlődik (Soniox cleanup).
    QTRY_COMPARE(m_http->count("DELETE", "/transcriptions/t1"), 1);
    QTRY_COMPARE(m_http->count("DELETE", "/files/f1"), 1);
    // Nem érkezik késői eredmény.
    QSignalSpy ready(m_app.get(), &AppController::transcriptReady);
    QTest::qWait(300);
    QCOMPARE(ready.count(), 0);
}

void AppJobsTest::cancelDuringMixdownKeepsOldMixdown()
{
    if (!m_ffmpeg) QSKIP("ffmpeg nem található");
    m_http->handler = [](const FakeRequest& r) { return sonioxOk(r); };
    Meeting m = recording("Hosszú felvétel", 900);            // 15 perc → a keverés eltart egy ideig
    // Van egy régi (elavult) keverék — megszakítás után is ennek kell megmaradnia.
    const QByteArray oldMix = "REGI-KEVEREK";
    { QFile f(QDir(m.folder).filePath("mixdown.mp3")); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(oldMix); }
    m.mixdownFile = "mixdown.mp3";
    m.mixdownDirty = true;
    m_app->store()->saveMeeting(m);

    QSignalSpy finished(m_app->jobs(), &MeetingJobTracker::jobFinished);
    QSignalSpy mixDone(m_app.get(), &AppController::mixdownUpdated);
    m_app->transcribeMeeting(m.id);
    QVERIFY(m_app->processingState(m.id).mixdownRunning);
    QVERIFY(m_app->cancelJob(m.id, JobKind::Transcribe));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Cancelled);
    QVERIFY(mixDone.wait(10000));                             // az ffmpeg leállt
    QCOMPARE(mixDone.at(0).at(1).toBool(), false);

    QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));
    QFile f(QDir(m.folder).filePath("mixdown.mp3"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), oldMix);                            // a régi keverék érintetlen
    QVERIFY(!QDir(m.folder).exists("mixdown.part.mp3"));      // félkész fájl nem maradt
    QVERIFY(QDir(m.folder).exists("track_mic.wav"));
    const MeetingProcessingState st = m_app->processingState(m.id);
    QCOMPARE(st.transcriptState, StepState::None);
    QVERIFY(!st.mixdownRunning);
    QVERIFY(!st.busy());
    QTest::qWait(200);
    QVERIFY(m_http->log.isEmpty());                           // a szolgáltatóhoz semmi nem ment
}

void AppJobsTest::standaloneMixdownCancel()
{
    if (!m_ffmpeg) QSKIP("ffmpeg nem található");
    const Meeting m = recording("Keverés", 900);
    MeetingJobTracker* jobs = m_app->jobs();
    QSignalSpy mixDone(m_app.get(), &AppController::mixdownUpdated);
    QSignalSpy finished(jobs, &MeetingJobTracker::jobFinished);
    m_app->regenerateMixdown(m.id);
    QVERIFY(jobs->isRunning(m.id, JobKind::Mixdown));
    m_app->regenerateMixdown(m.id);                           // fut már → no-op
    QVERIFY(m_app->processingState(m.id).mixdownRunning);
    QCOMPARE(m_app->processingState(m.id).transcriptState, StepState::None);
    // Valós százalék érkezik az ffmpeg-től.
    QTRY_VERIFY_WITH_TIMEOUT(jobs->job(m.id, JobKind::Mixdown).percent > 0
                             || !jobs->isRunning(m.id, JobKind::Mixdown), 10000);
    if (!jobs->isRunning(m.id, JobKind::Mixdown)) QSKIP("a keverés túl gyorsan lefutott a megszakításhoz");

    QVERIFY(m_app->cancelJob(m.id, JobKind::Mixdown));
    QVERIFY(mixDone.wait(10000));
    QCOMPARE(mixDone.count(), 1);
    QCOMPARE(mixDone.at(0).at(1).toBool(), false);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Cancelled);
    QVERIFY(m_errors.isEmpty());
    QVERIFY(!QDir(m.folder).exists("mixdown.part.mp3"));
    QVERIFY(!QDir(m.folder).exists("mixdown.mp3"));
    QVERIFY(m_app->store()->load(m.id).mixdownFile.isEmpty());
    QVERIFY(!m_app->jobs()->lastError(m.id, JobKind::Mixdown).isValid());
}

void AppJobsTest::summarizePersistsStructureAndMeta()
{
    const QString json = QStringLiteral(
        R"({"execSummary":"Rövid egyeztetés az árajánlatról.","decisions":["Az árajánlat pénteken megy ki."],)"
        R"("actionItems":[{"text":"Árajánlat elküldése","owner":"Ödön","due":"péntek"}],"participants":["Ödön","Ádám"]})");
    m_http->handler = [json](const FakeRequest& r) -> FakeReply {
        if (r.path.endsWith("/chat/completions")) return {200, chat(json)};
        return {404, "{}"};
    };
    const Meeting m = transcribed("Árajánlat");
    QVERIFY(!m_app->summaryDocument(m.id).exists);
    QSignalSpy ready(m_app.get(), &AppController::summaryReady);
    QSignalSpy finished(m_app->jobs(), &MeetingJobTracker::jobFinished);
    m_app->summarizeMeeting(m.id);
    QVERIFY(m_app->jobs()->isRunning(m.id, JobKind::Summarize));
    QCOMPARE(m_app->processingState(m.id).summaryState, StepState::Running);
    QVERIFY(ready.wait(10000));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Done);
    QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));
    QCOMPARE(m_app->processingState(m.id).summaryState, StepState::Done);

    // A struktúra perzisztálva (summary.json) + a markdown is megvan.
    QVERIFY(QDir(m.folder).exists("summary.json"));
    QVERIFY(QDir(m.folder).exists("summary.md"));
    const SummaryDocument doc = m_app->summaryDocument(m.id);
    QVERIFY(doc.exists);
    QVERIFY(!doc.fromMarkdown);
    QCOMPARE(doc.summary.execSummary, QStringLiteral("Rövid egyeztetés az árajánlatról."));
    QCOMPARE(doc.summary.decisions, QStringList({"Az árajánlat pénteken megy ki."}));
    QCOMPARE(doc.summary.actionItems.size(), 1);
    QCOMPARE(doc.summary.actionItems[0].owner, QStringLiteral("Ödön"));
    QCOMPARE(doc.summary.participants, QStringList({"Ödön", "Ádám"}));
    // Metaadat: mikor, melyik szolgáltató/modell, milyen módban.
    QCOMPARE(doc.meta.mode, SummaryMode::Quick);
    QCOMPARE(doc.meta.providerId, QStringLiteral("openai-compat"));
    QCOMPARE(doc.meta.model, QStringLiteral("teszt-modell"));
    QVERIFY(qAbs(doc.meta.createdAt.secsTo(QDateTime::currentDateTime())) < 30);
    QVERIFY(doc.markdown.contains("## Vezetői összefoglaló"));
    // A jegyzet-másolat is a sandboxba került.
    QVERIFY(!QDir(m_home->filePath("notes")).entryList({"*.md"}).isEmpty());
}

void AppJobsTest::summarizeCancelAndFailure()
{
    const Meeting m = transcribed("Megszakított összefoglaló");
    // 1) Függő LLM-hívás megszakítása.
    m_http->handler = [](const FakeRequest&) -> FakeReply { FakeReply r; r.hold = true; return r; };
    QSignalSpy finished(m_app->jobs(), &MeetingJobTracker::jobFinished);
    QSignalSpy ready(m_app.get(), &AppController::summaryReady);
    m_app->summarizeMeeting(m.id);
    QTRY_COMPARE(m_http->count("POST", "/chat/completions"), 1);
    QVERIFY(m_app->cancelJob(m.id, JobKind::Summarize));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Cancelled);
    QTest::qWait(200);
    QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));
    QCOMPARE(ready.count(), 0);
    MeetingProcessingState st = m_app->processingState(m.id);
    QCOMPARE(st.summaryState, StepState::None);
    QVERIFY(!st.summaryError.isValid());
    QVERIFY(!QDir(m.folder).exists("summary.md"));
    QVERIFY(!m_app->store()->load(m.id).hasSummary);
    QVERIFY(!m_app->cancelJob(m.id, JobKind::Summarize));

    // 2) Szolgáltató-hiba: megmaradó hiba emberi + technikai sorral.
    m_http->handler = [](const FakeRequest&) -> FakeReply {
        return {500, R"({"error":{"message":"model crashed","type":"server_error"}})"};
    };
    m_app->summarizeMeeting(m.id);
    QTRY_COMPARE(finished.count(), 2);
    QCOMPARE(finished.at(1).at(2).value<JobOutcome>(), JobOutcome::Failed);
    QCOMPARE(m_errors.size(), 1);
    st = m_app->processingState(m.id);
    QCOMPARE(st.summaryState, StepState::Failed);
    QCOMPARE(st.summaryError.detail, QStringLiteral("HTTP 500 · server_error · model crashed"));
    QCOMPARE(st.transcriptState, StepState::Done);            // az átirat-lépést nem érinti
    // A hiba elvethető.
    m_app->jobs()->clearError(m.id, JobKind::Summarize);
    QCOMPARE(m_app->processingState(m.id).summaryState, StepState::None);
}

void AppJobsTest::topicsEditReorderAndStatuses()
{
    const Meeting m = transcribed("Témák");
    QSignalSpy topicsChanged(m_app.get(), &AppController::topicsChanged);

    // Hozzáadás: az új témák azonosítót kapnak, az üres című kimarad.
    QVector<SummaryTopic> saved = m_app->setMeetingTopics(m.id, {
        {QString(), QStringLiteral("Árazás"), QStringLiteral("Csomagok és árak")},
        {QString(), QStringLiteral("  "), QString()},
        {QString(), QStringLiteral("Pilot"), QStringLiteral("Helyszínek")},
        {QString(), QStringLiteral("Határidők"), QString()}});
    QCOMPARE(saved.size(), 3);
    QCOMPARE(topicsChanged.count(), 1);
    for (const SummaryTopic& t : saved) QVERIFY(!t.id.isEmpty());
    const QString idPrice = saved[0].id, idPilot = saved[1].id, idDue = saved[2].id;

    // Átrendezés + szerkesztés: a sorrend és a tartalom perzisztál (azonosítók maradnak).
    saved[0].title = QStringLiteral("Árazás és csomagok");
    saved = m_app->setMeetingTopics(m.id, {saved[2], saved[0], saved[1]});
    QVector<SummaryTopic> onDisk = m_app->meetingTopics(m.id);
    QCOMPARE(onDisk.size(), 3);
    QCOMPARE(onDisk[0].id, idDue);
    QCOMPARE(onDisk[1].id, idPrice);
    QCOMPARE(onDisk[1].title, QStringLiteral("Árazás és csomagok"));
    QCOMPARE(onDisk[2].id, idPilot);

    // Állapot bármikor lekérdezhető: még semmi nem futott → mind „vár”.
    QVector<TopicStatus> sts = m_app->topicStatuses(m.id);
    QCOMPARE(sts.size(), 3);
    for (const TopicStatus& s : sts) QCOMPARE(s.state, TopicState::Waiting);

    // Elemzés: az 1. téma sikerül, a 2. elbukik (HTTP 500), a 3. sikerül.
    int call = 0;
    m_http->handler = [&call](const FakeRequest& r) -> FakeReply {
        if (!r.path.endsWith("/chat/completions")) return {404, "{}"};
        ++call;
        if (call == 2) return {500, R"({"error":{"message":"overloaded","code":"overloaded"}})"};
        return {200, chat(QStringLiteral("Részletes elemzés %1.\n\n## Döntések\n- Döntés %1\n\n## Teendők\n- Teendő %1 — Ödön (péntek)\n").arg(call))};
    };
    QSignalSpy queueDone(m_app.get(), &AppController::topicAnalysisQueueFinished);
    QSignalSpy statusChanged(m_app.get(), &AppController::topicStatusChanged);
    QSignalSpy finished(m_app->jobs(), &MeetingJobTracker::jobFinished);
    m_app->generateComplexSummary(m.id, onDisk);
    // Közvetlenül indítás után: az első fut, a többi sorban áll; darab-haladás a feladaton.
    sts = m_app->topicStatuses(m.id);
    QCOMPARE(sts[0].state, TopicState::Running);
    QCOMPARE(sts[1].state, TopicState::Queued);
    QCOMPARE(sts[2].state, TopicState::Queued);
    JobProgress jp = m_app->jobs()->job(m.id, JobKind::AnalyzeTopics);
    QVERIFY(jp.isValid());
    QCOMPARE(jp.done, 0);
    QCOMPARE(jp.total, 3);
    QCOMPARE(m_app->processingState(m.id).summaryState, StepState::Running);

    QVERIFY(queueDone.wait(15000));
    QCOMPARE(queueDone.at(0).at(1).toInt(), 2);
    QCOMPARE(queueDone.at(0).at(2).toInt(), 1);
    QVERIFY(statusChanged.count() >= 6);
    sts = m_app->topicStatuses(m.id);
    QCOMPARE(sts[0].state, TopicState::Done);
    QCOMPARE(sts[1].state, TopicState::Failed);
    QCOMPARE(sts[1].topicId, idPrice);
    QCOMPARE(sts[1].errorDetail, QStringLiteral("HTTP 500 · overloaded · overloaded"));
    QVERIFY(!sts[1].error.isEmpty());
    QCOMPARE(sts[2].state, TopicState::Done);
    // Bukott téma → nincs automatikus összegzés; a lépés hibás, a kész elemzések megvannak.
    QVERIFY(!m_app->store()->load(m.id).hasSummary);
    QCOMPARE(m_app->processingState(m.id).summaryState, StepState::Failed);
    QCOMPARE(m_app->topicAnalyses(m.id).size(), 2);

    // Újraindítás után is: a téma-állapotok (a hibaüzenettel együtt) a lemezről jönnek.
    newApp();
    sts = m_app->topicStatuses(m.id);
    QCOMPARE(sts[0].state, TopicState::Done);
    QCOMPARE(sts[1].state, TopicState::Failed);
    QCOMPARE(sts[1].errorDetail, QStringLiteral("HTTP 500 · overloaded · overloaded"));
    QCOMPARE(sts[2].state, TopicState::Done);

    // A bukott téma újrafuttatása → kész; utána a záró összegzés.
    call = 10;
    QSignalSpy oneReady(m_app.get(), &AppController::topicAnalysisReady);
    m_app->analyzeTopic(m.id, onDisk[1]);
    QCOMPARE(m_app->topicStatuses(m.id)[1].state, TopicState::Running);
    QVERIFY(oneReady.wait(10000));
    QTRY_VERIFY(!m_app->jobs()->isBusy(m.id));
    sts = m_app->topicStatuses(m.id);
    for (const TopicStatus& s : sts) QCOMPARE(s.state, TopicState::Done);
    QVERIFY(!m_app->processingState(m.id).summaryError.isValid());

    m_http->handler = [](const FakeRequest&) -> FakeReply { return {200, chat(QStringLiteral("A megbeszélés röviden: minden rendben."))}; };
    QSignalSpy summaryReady(m_app.get(), &AppController::summaryReady);
    m_app->finalizeComplexSummary(m.id);
    QVERIFY(m_app->jobs()->isRunning(m.id, JobKind::Summarize));
    QVERIFY(summaryReady.wait(10000));
    const SummaryDocument doc = m_app->summaryDocument(m.id);
    QVERIFY(doc.exists);
    QCOMPARE(doc.meta.mode, SummaryMode::Topics);
    QCOMPARE(doc.meta.model, QStringLiteral("teszt-modell"));
    QCOMPARE(doc.summary.execSummary, QStringLiteral("A megbeszélés röviden: minden rendben."));
    QCOMPARE(doc.topics.size(), 3);
    QCOMPARE(doc.topics[0].topicId, idDue);                   // a szerkesztett SORRENDBEN
    QCOMPARE(doc.topics[1].topicId, idPrice);
    QCOMPARE(doc.topics[2].topicId, idPilot);
    QCOMPARE(doc.summary.actionItems.size(), 3);
    QCOMPARE(doc.summary.decisions.size(), 3);
    QCOMPARE(m_app->processingState(m.id).summaryState, StepState::Done);

    // Törlés: a téma eltűnik a listából és az állapotok közül.
    m_app->setMeetingTopics(m.id, {onDisk[0], onDisk[2]});
    QCOMPARE(m_app->meetingTopics(m.id).size(), 2);
    QCOMPARE(m_app->topicStatuses(m.id).size(), 2);
}

void AppJobsTest::cancelTopicQueue()
{
    const Meeting m = transcribed("Megszakított témák");
    const QVector<SummaryTopic> topics = m_app->setMeetingTopics(m.id, {
        {QString(), QStringLiteral("A"), QString()},
        {QString(), QStringLiteral("B"), QString()},
        {QString(), QStringLiteral("C"), QString()}});
    // Az első elemzés sikerül, a második „beragad”.
    int call = 0;
    m_http->handler = [&call](const FakeRequest&) -> FakeReply {
        if (++call == 1) return {200, chat(QStringLiteral("Elemzés.\n\n## Döntések\n- d\n"))};
        FakeReply r; r.hold = true; return r;
    };
    QSignalSpy oneReady(m_app.get(), &AppController::topicAnalysisReady);
    QSignalSpy finished(m_app->jobs(), &MeetingJobTracker::jobFinished);
    QSignalSpy summaryReady(m_app.get(), &AppController::summaryReady);
    m_app->generateComplexSummary(m.id, topics);
    QVERIFY(oneReady.wait(10000));
    QTRY_COMPARE(m_http->count("POST", "/chat/completions"), 2);
    QVector<TopicStatus> sts = m_app->topicStatuses(m.id);
    QCOMPARE(sts[0].state, TopicState::Done);
    QCOMPARE(sts[1].state, TopicState::Running);
    QCOMPARE(sts[2].state, TopicState::Queued);
    QCOMPARE(m_app->jobs()->job(m.id, JobKind::AnalyzeTopics).done, 1);

    QVERIFY(m_app->cancelJob(m.id, JobKind::AnalyzeTopics));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Cancelled);
    QTest::qWait(300);
    // A kész elemzés megmarad, a többi „vár” (nem hibás); nincs összegzés, nincs hiba.
    sts = m_app->topicStatuses(m.id);
    QCOMPARE(sts[0].state, TopicState::Done);
    QCOMPARE(sts[1].state, TopicState::Waiting);
    QCOMPARE(sts[2].state, TopicState::Waiting);
    QCOMPARE(m_app->topicAnalyses(m.id).size(), 1);
    QCOMPARE(summaryReady.count(), 0);
    QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));
    QVERIFY(!m_app->jobs()->isBusy(m.id));
    QVERIFY(!m_app->processingState(m.id).summaryError.isValid());
    QCOMPARE(m_http->count("POST", "/chat/completions"), 2);  // a 3. téma el sem indult

    // Folytatás: csak a hiányzó kettő megy (a kész nem fut újra).
    call = 0;
    m_http->handler = [](const FakeRequest&) -> FakeReply { return {200, chat(QStringLiteral("Elemzés.\n\n## Döntések\n- d\n"))}; };
    m_app->generateComplexSummary(m.id, topics);
    QCOMPARE(m_app->jobs()->job(m.id, JobKind::AnalyzeTopics).total, 2);
    QVERIFY(summaryReady.wait(15000));                        // elemzések + automatikus összegzés
    QCOMPARE(m_app->topicAnalyses(m.id).size(), 3);
    QCOMPARE(m_app->summaryDocument(m.id).meta.mode, SummaryMode::Topics);
}

void AppJobsTest::renamedDuringTranscriptionSurvives()
{
    if (!m_ffmpeg) QSKIP("ffmpeg nem található");
    m_http->handler = [](const FakeRequest& r) { return sonioxOk(r); };
    const Meeting m = recording("Eredeti cím", 2);
    QSignalSpy ready(m_app.get(), &AppController::transcriptReady);
    m_app->transcribeMeeting(m.id);
    // Az app közben használható: a felhasználó átnevezi a meetinget, amíg az átírás fut.
    QTRY_VERIFY_WITH_TIMEOUT(m_http->count("POST", "/files") == 1, 30000);
    m_app->renameMeeting(m.id, "Átnevezve közben");
    QVERIFY(ready.wait(30000));
    const Meeting after = m_app->store()->load(m.id);
    QCOMPARE(after.title, QStringLiteral("Átnevezve közben"));   // az átírás vége nem írta felül
    QVERIFY(after.hasTranscript);
}

void AppJobsTest::identifyRunsAsLastStage()
{
    // Csak valódi hang-modellel fut: TANARA_TEST_VOICE_MODEL=<…/campplus_sv_zh_en_16k.onnx>.
    const QString model = qEnvironmentVariable("TANARA_TEST_VOICE_MODEL");
    if (model.isEmpty() || !QFile::exists(model))
        QSKIP("TANARA_TEST_VOICE_MODEL nincs beállítva — az azonosítás-szakasz tesztje kimarad");
    if (!m_ffmpeg) QSKIP("ffmpeg nem található");
    QDir().mkpath(m_home->filePath("models"));
    QVERIFY(QFile::link(model, m_home->filePath("models/campplus_sv_zh_en_16k.onnx")));

    m_http->handler = [](const FakeRequest& r) { return sonioxOk(r); };
    const Meeting m = recording("Azonosítással", 5);
    MeetingJobTracker* jobs = m_app->jobs();
    QVector<StageState> identify;
    QStringList details;
    connect(jobs, &MeetingJobTracker::jobProgressChanged, this, [&](const QString&, const JobProgress& p) {
        if (const JobStage* st = p.stage("identify")) {
            if (identify.isEmpty() || identify.last() != st->state) identify.append(st->state);
            if (!st->detail.isEmpty() && !details.contains(st->detail)) details << st->detail;
        }
    });
    QSignalSpy ready(m_app.get(), &AppController::transcriptReady);
    QSignalSpy finished(jobs, &MeetingJobTracker::jobFinished);
    m_app->transcribeMeeting(m.id);
    if (!jobs->job(m.id, JobKind::Transcribe).stage("identify"))
        QSKIP("a build nem tartalmaz voice-ID-t");
    QVERIFY(ready.wait(30000));
    // Az átirat kész, de a feladat még fut: az utolsó szakasz az azonosítás (háttérszálon).
    QCOMPARE(finished.count(), 0);
    MeetingProcessingState st = m_app->processingState(m.id);
    QCOMPARE(st.identifyState, StepState::Running);
    QVERIFY(m_app->store()->load(m.id).hasTranscript);
    QVERIFY(finished.wait(60000));
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Done);
    QCOMPARE(identify, QVector<StageState>({StageState::Waiting, StageState::Running, StageState::Done}));
    QVERIFY(details.contains(QStringLiteral("0 / 2 beszélő")));
    QVERIFY(details.contains(QStringLiteral("2 / 2 beszélő")));   // valós darab-haladás
    st = m_app->processingState(m.id);
    QCOMPARE(st.transcriptState, StepState::Done);
    QCOMPARE(st.identifyState, StepState::Done);
    QVERIFY(m_app->jobs()->identifiedAt(m.id).isValid());
    QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));

    // Önálló (újra)azonosítás megszakítása: nincs hiba, a feladat „megszakítva” zárul.
    QVERIFY(m_app->identifyMeetingAsync(m.id));
    QVERIFY(!m_app->identifyMeetingAsync(m.id));              // már fut
    QCOMPARE(jobs->job(m.id, JobKind::Identify).total, 2);
    QVERIFY(m_app->cancelJob(m.id, JobKind::Identify));
    QVERIFY(jobs->job(m.id, JobKind::Identify).cancelling);
    QVERIFY(finished.wait(60000));
    QCOMPARE(finished.at(1).at(1).value<JobKind>(), JobKind::Identify);
    QCOMPARE(finished.at(1).at(2).value<JobOutcome>(), JobOutcome::Cancelled);
    QVERIFY(m_errors.isEmpty());
}

void AppJobsTest::retranscribeSuccessClearsNamesAndCorrections()
{
    if (!m_ffmpeg) QSKIP("ffmpeg nem található");
    m_http->handler = [](const FakeRequest& r) { return sonioxOk(r); };
    Meeting m = transcribed("Sikeres újra-átírás");
    m.speakerMap.insert("Beszélő 1", "Ödön");
    m_app->store()->saveMeeting(m);
    SpeakerEditor* ed = m_app->speakerEditor(m.id);
    QVERIFY(ed);
    QVERIFY(!ed->moveUtterancesToPerson({ed->utteranceAt(1).id}, "Kovács Lilla").isEmpty());
    QCOMPARE(m_app->retranscribeImpact(m.id).correctedUtterances, 1);

    QSignalSpy ready(m_app.get(), &AppController::transcriptReady);
    m_app->retranscribeMeeting(m.id, /*keepBackup*/ true);
    // Amíg fut, a régi nevek és javítások élnek (nem törlődtek előre).
    QCOMPARE(m_app->store()->load(m.id).speakerMap.value("Beszélő 1"), QStringLiteral("Ödön"));
    QCOMPARE(m_app->retranscribeImpact(m.id).correctedUtterances, 1);
    QVERIFY(ready.wait(30000));
    QTRY_VERIFY(!m_app->jobs()->isBusy(m.id));

    // Siker: az új átirattal a nevek ÉS a soronkénti javítások is törlődtek.
    QVERIFY(m_app->store()->load(m.id).speakerMap.isEmpty());
    const RetranscribeImpact impact = m_app->retranscribeImpact(m.id);
    QCOMPARE(impact.correctedUtterances, 0);
    QCOMPARE(impact.namedSpeakers, 0);
    QCOMPARE(impact.addedParticipants, 0);
    // A nyitott szerkesztő az ÚJ átiratot mutatja (a mock két megszólalást ad, két beszélővel).
    QCOMPARE(ed->utteranceCount(), 2);
    QVERIFY(!ed->utteranceAt(0).manuallyCorrected && !ed->utteranceAt(1).manuallyCorrected);
    // A kért másolat megvan, benne a régi átirat a javításokkal.
    const QStringList backups = QDir(m.folder).entryList({"transcript-backup-*"}, QDir::Dirs);
    QCOMPARE(backups.size(), 1);
    QVERIFY(QDir(QDir(m.folder).filePath(backups.first())).exists("transcript.tokens.json"));
    QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));

    // Törlés nyitott szerkesztővel: a szerkesztő is záródik (új kérésre nincs meeting → null).
    m_app->deleteMeeting(m.id);
    QVERIFY(m_app->speakerEditor(m.id) == nullptr);
    QVERIFY(!QDir(m.folder).exists());
}

void AppJobsTest::staleSummaryShowsInStateAndPending()
{
    const QString json = QStringLiteral(R"({"execSummary":"Rövid.","decisions":[],"actionItems":[],"participants":[]})");
    m_http->handler = [json](const FakeRequest&) -> FakeReply { return {200, chat(json)}; };
    const Meeting m = transcribed("Elavuló összefoglaló");
    QSignalSpy ready(m_app.get(), &AppController::summaryReady);
    m_app->summarizeMeeting(m.id);
    QVERIFY(ready.wait(10000));
    MeetingProcessingState st = m_app->processingState(m.id);
    QCOMPARE(st.summaryState, StepState::Done);
    QVERIFY(!st.summaryStale);
    QVERIFY(m_app->library()->pendingItems().isEmpty());

    // Beszélő-változás az összefoglaló után → elavult: állapot, könyvtár-bejegyzés, „várnak rád”.
    QSignalSpy stateChanged(m_app->jobs(), &MeetingJobTracker::stateChanged);
    QSignalSpy pendingChanged(m_app->library(), &MeetingLibrary::pendingItemsChanged);
    QSignalSpy entryChanged(m_app->library(), &MeetingLibrary::meetingChanged);
    m_app->renameSpeaker(m.id, "Beszélő 1", "Ödön", /*enroll*/ false);
    QVERIFY(stateChanged.count() >= 1);
    QVERIFY(pendingChanged.count() >= 1);
    QVERIFY(entryChanged.count() >= 1);
    st = m_app->processingState(m.id);
    QCOMPARE(st.summaryState, StepState::Done);              // megvan, csak elavult
    QVERIFY(st.summaryStale);
    QCOMPARE(st.staleCorrectedSpeakers, 1);
    QVERIFY(m_app->library()->entry(m.id).state.summaryStale);
    QVector<PendingItem> pending = m_app->library()->pendingItems();
    QCOMPARE(pending.size(), 1);
    QCOMPARE(pending[0].kind, PendingKind::StaleSummary);
    QCOMPARE(pending[0].meetingId, m.id);
    QCOMPARE(pending[0].correctedSpeakers, 1);

    // „Rendben így” → eltűnik.
    const int before = pendingChanged.count();
    m_app->dismissSummaryStale(m.id);
    QVERIFY(pendingChanged.count() > before);
    QVERIFY(!m_app->processingState(m.id).summaryStale);
    QVERIFY(m_app->library()->pendingItems().isEmpty());

    // Újra elavul, majd az összefoglaló újragenerálása törli.
    m_app->renameSpeaker(m.id, "Beszélő 1", "Lilla", false);
    QVERIFY(m_app->processingState(m.id).summaryStale);
    m_app->summarizeMeeting(m.id);
    QVERIFY(ready.wait(10000));
    QVERIFY(!m_app->processingState(m.id).summaryStale);
    QVERIFY(m_app->library()->pendingItems().isEmpty());
}

// ---- adatbiztonság ----------------------------------------------------------------------

namespace {
QByteArray fileBytes(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
} // namespace

void AppJobsTest::retryAfterFailedRetranscribeClearsNames()
{
    if (!m_ffmpeg) QSKIP("ffmpeg nem található");
    Meeting m = transcribed("Újrapróbált újra-átírás");
    m.speakerMap.insert("Beszélő 1", "Ödön");
    m_app->store()->saveMeeting(m);
    MeetingJobTracker* jobs = m_app->jobs();

    // 1) Az újra-átírás elbukik a szolgáltatónál: a régi átirat a nevével együtt megmarad.
    m_http->handler = [](const FakeRequest& r) -> FakeReply {
        if (r.method == "POST" && r.path.endsWith("/files")) return {500, R"({"message":"hiba"})"};
        return {200, "{}"};
    };
    QSignalSpy finished(jobs, &MeetingJobTracker::jobFinished);
    m_app->retranscribeMeeting(m.id);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Failed);
    QCOMPARE(m_app->store()->load(m.id).speakerMap.value("Beszélő 1"), QStringLiteral("Ödön"));

    // 2) Újrapróbálás a SIMA átírással (a hibakártya „Újra” gombja): az új diarizáció
    //    „Beszélő 1”-e más ember lehet → a régi név NEM maradhat rajta.
    m_http->handler = [](const FakeRequest& r) { return sonioxOk(r); };
    QSignalSpy ready(m_app.get(), &AppController::transcriptReady);
    m_app->transcribeMeeting(m.id);
    QVERIFY(ready.wait(30000));
    QTRY_VERIFY(!jobs->isBusy(m.id));
    QVERIFY(m_app->store()->load(m.id).speakerMap.isEmpty());
    const QByteArray md = fileBytes(QDir(m.folder).filePath("transcript.md"));
    QVERIFY(md.contains("Beszélő 1"));
    QVERIFY(!md.contains("Ödön"));
    QCOMPARE(m_app->retranscribeImpact(m.id).namedSpeakers, 0);
}

void AppJobsTest::emptySttResultKeepsOldTranscript()
{
    if (!m_ffmpeg) QSKIP("ffmpeg nem található");
    // A szolgáltató „sikeresen” végez, de egyetlen szót sem ad vissza.
    m_http->handler = [](const FakeRequest& r) -> FakeReply {
        if (r.method == "GET" && r.path.endsWith("/transcript")) return {200, R"({"tokens":[]})"};
        return sonioxOk(r);
    };
    Meeting m = transcribed("Üres eredmény");
    m.speakerMap.insert("Beszélő 1", "Ödön");
    m_app->store()->saveMeeting(m);
    {
        SpeakerEditor* ed = m_app->speakerEditor(m.id);
        QVERIFY(ed);
        QVERIFY(!ed->moveUtterancesToPerson({ed->utteranceAt(1).id}, "Kovács Lilla").isEmpty());
        m_app->closeSpeakerEditor(m.id);
    }
    const QString tokensPath = QDir(m.folder).filePath("transcript.tokens.json");
    const QString segmentsPath = QDir(m.folder).filePath("transcript.segments.json");
    const QByteArray tokensBefore = fileBytes(tokensPath);
    const QByteArray segmentsBefore = fileBytes(segmentsPath);
    const QByteArray overlayBefore = fileBytes(speakeredit::overlayPath(m.folder));
    QVERIFY(!tokensBefore.isEmpty() && !overlayBefore.isEmpty());

    MeetingJobTracker* jobs = m_app->jobs();
    QSignalSpy finished(jobs, &MeetingJobTracker::jobFinished);
    QSignalSpy ready(m_app.get(), &AppController::transcriptReady);
    m_app->retranscribeMeeting(m.id);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Failed);
    QCOMPARE(ready.count(), 0);

    // Megmaradó, érthető hiba; a régi átirat, a nevek és a kézi javítások érintetlenek.
    const MeetingProcessingState st = m_app->processingState(m.id);
    QVERIFY(st.transcriptError.isValid());
    QVERIFY2(st.transcriptError.message.contains("üres eredményt"), qPrintable(st.transcriptError.message));
    QCOMPARE(m_errors.size(), 1);
    QCOMPARE(fileBytes(tokensPath), tokensBefore);
    QCOMPARE(fileBytes(segmentsPath), segmentsBefore);
    QCOMPARE(fileBytes(speakeredit::overlayPath(m.folder)), overlayBefore);
    const Meeting after = m_app->store()->load(m.id);
    QVERIFY(after.hasTranscript);
    QCOMPARE(after.speakerMap.value("Beszélő 1"), QStringLiteral("Ödön"));
    QCOMPARE(m_app->retranscribeImpact(m.id).correctedUtterances, 1);

    // Első átírásnál is hiba (nem „kész, üres átirat”): nem keletkezik átirat-fájl.
    const Meeting fresh = recording("Csendes felvétel", 2);
    m_app->transcribeMeeting(fresh.id);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 30000);
    QCOMPARE(finished.at(1).at(2).value<JobOutcome>(), JobOutcome::Failed);
    QVERIFY(!m_app->store()->load(fresh.id).hasTranscript);
    QVERIFY(!QDir(fresh.folder).exists("transcript.tokens.json"));
    QVERIFY(!QDir(fresh.folder).exists("transcript.md"));
    QCOMPARE(ready.count(), 0);
}

void AppJobsTest::missingTrackKeepsExistingMixdown()
{
    if (!m_ffmpeg) QSKIP("ffmpeg nem található");
    m_http->handler = [](const FakeRequest& r) { return sonioxOk(r); };
    Meeting m = recording("Hiányzó sáv", 2);
    Track sys;
    sys.id = "sys"; sys.kind = TrackKind::Loopback; sys.deviceName = "Monitor of Kanto YU4";
    sys.file = "track_sys.wav"; sys.active = true;            // a fájl NINCS a lemezen
    m.tracks.append(sys);
    // Van egy teljes (mindkét sávból készült) keverék, amely elavultnak van jelölve.
    const QByteArray oldMix = "TELJES-KEVEREK";
    const QString mixPath = QDir(m.folder).filePath("mixdown.mp3");
    { QFile f(mixPath); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(oldMix); }
    m.mixdownFile = "mixdown.mp3";
    m.mixdownDirty = true;
    m_app->store()->saveMeeting(m);
    const QString sysName = m_app->tracks()->tracks(m.id).at(1).displayName;
    QVERIFY(!sysName.isEmpty());

    // Kézi újrakeverés: nem indul el, a hiba megnevezi a sávot, a régi keverék marad.
    QSignalSpy mixDone(m_app.get(), &AppController::mixdownUpdated);
    m_app->regenerateMixdown(m.id);
    QCOMPARE(mixDone.count(), 1);
    QCOMPARE(mixDone.at(0).at(1).toBool(), false);
    QCOMPARE(m_errors.size(), 1);
    QVERIFY2(m_errors.first().contains(sysName), qPrintable(m_errors.first()));
    QCOMPARE(fileBytes(mixPath), oldMix);
    QVERIFY(!m_app->jobs()->isBusy(m.id));

    // Átírás előtti automatikus újrakeverés: ugyanígy — az átírás nem indul, semmi nem megy
    // a szolgáltatóhoz, a megmaradó hiba megnevezi a sávot.
    QSignalSpy finished(m_app->jobs(), &MeetingJobTracker::jobFinished);
    m_app->transcribeMeeting(m.id);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Failed);
    QVERIFY(m_app->processingState(m.id).transcriptError.detail.contains(sysName));
    QCOMPARE(fileBytes(mixPath), oldMix);
    QTest::qWait(200);
    QVERIFY(m_http->log.isEmpty());

    // A sáv eldobása után a keverés lefut, és a régi fájlt a helyén cseréli (nem marad félkész
    // vagy félretett fájl).
    Meeting dropped = m_app->store()->load(m.id);
    dropped.tracks[1].active = false;
    m_app->store()->saveMeeting(dropped);
    mixDone.clear();
    m_app->regenerateMixdown(m.id);
    QVERIFY(mixDone.wait(30000));
    QCOMPARE(mixDone.at(0).at(1).toBool(), true);
    QVERIFY(fileBytes(mixPath) != oldMix);
    QVERIFY(fileBytes(mixPath).size() > 100);
    QVERIFY(!QDir(m.folder).exists("mixdown.part.mp3"));
    QVERIFY(!QDir(m.folder).exists("mixdown.mp3.old"));
    QVERIFY(!m_app->store()->load(m.id).mixdownDirty);
}

void AppJobsTest::identifyResultsDroppedWhenTranscriptChanges()
{
    // Ál-modellfájl: az azonosítás elindul (háttérszál), de embeddinget nem ad — a teszt a
    // vezénylést nézi (leállítás / eldobás), nem a felismerést. Valódi modell nem kell hozzá.
    QDir().mkpath(m_home->filePath("models"));
    { QFile f(m_home->filePath("models/campplus_sv_zh_en_16k.onnx")); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("nem-onnx"); }
    if (!m_app->voiceIdentificationAvailable()) QSKIP("a build nem tartalmaz voice-ID-t");

    Meeting m = transcribed("Azonosítás közben");
    { QFile f(QDir(m.folder).filePath("mixdown.mp3")); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("KEVEREK"); }
    m.mixdownFile = "mixdown.mp3";
    m.mixdownDirty = false;
    m_app->store()->saveMeeting(m);
    MeetingJobTracker* jobs = m_app->jobs();
    QSignalSpy finished(jobs, &MeetingJobTracker::jobFinished);

    // Kontroll: változatlan átirat mellett az önálló azonosítás rendben lezárul.
    QVERIFY(m_app->identifyMeetingAsync(m.id));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
    QCOMPARE(finished.at(0).at(1).value<JobKind>(), JobKind::Identify);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Done);
    QVERIFY(jobs->identifiedAt(m.id).isValid());
    jobs->markIdentified(m.id, false);

    // 1) Az azonosítás alatt megváltozik az átirat a lemezen → az eredmény a RÉGI átirat
    //    címkéire vonatkozik: eldobjuk, a meeting nem lesz „azonosítva”.
    QVERIFY(m_app->identifyMeetingAsync(m.id));
    {
        QFile f(QDir(m.folder).filePath("transcript.tokens.json"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(R"({"language":"hu","tokens":[{"text":"Más","speaker":"Beszélő 1","startMs":0,"endMs":200,"confidence":0.9,"trackId":"mixdown"}]})");
    }
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 30000);
    QCOMPARE(finished.at(1).at(1).value<JobKind>(), JobKind::Identify);
    QCOMPARE(finished.at(1).at(2).value<JobOutcome>(), JobOutcome::Cancelled);
    QVERIFY(!jobs->identifiedAt(m.id).isValid());

    // 2) Az átírás indítása leállítja a futó önálló azonosítást.
    m_http->handler = [](const FakeRequest& r) { return sonioxOk(r, "processing"); };
    QVERIFY(m_app->identifyMeetingAsync(m.id));
    m_app->transcribeMeeting(m.id);
    QVERIFY(jobs->job(m.id, JobKind::Identify).cancelling);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 3, 30000);
    QCOMPARE(finished.at(2).at(1).value<JobKind>(), JobKind::Identify);
    QCOMPARE(finished.at(2).at(2).value<JobOutcome>(), JobOutcome::Cancelled);
    QVERIFY(!jobs->identifiedAt(m.id).isValid());
    QVERIFY(jobs->isRunning(m.id, JobKind::Transcribe));
    QVERIFY(m_app->cancelJob(m.id, JobKind::Transcribe));
    QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));
}

#endif // Q_MOC_RUN

QTEST_GUILESS_MAIN(AppJobsTest)
#include "test_app_jobs.moc"
