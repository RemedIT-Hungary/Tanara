// AppController: integration test of the structured job layer.
//
// Isolated TANARA_HOME (temp), real ffmpeg for the mixdown, and a local fake HTTP server in
// place of the Soniox-like STT and OpenAI-compatible LLM endpoints (NO real, paid call).
// Checks: stage progress, persistent (restart-surviving) errors, cancellation in every phase
// (recordings and earlier results untouched), summary.json + metadata, reordering the topic
// list and querying per-topic state.
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

#include "tanara/app/AppController.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/library/tracks/TrackCatalog.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/MeetingLibrary.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/editing/session/SpeakerEditSession.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/library/jobs/JobErrors.h"
#include "tanara/providers/chat/LlmServer.h"

using namespace tanara;

// moc gets lost on raw (R"(...)") string literals, so it does not see the helpers and the
// test bodies (only the class declaration).
#ifndef Q_MOC_RUN
namespace {

// ---- minimal fake HTTP server ----
struct FakeRequest { QByteArray method; QString path; QByteArray body; };
struct FakeReply {
    int status = 200;
    QByteArray body = "{}";
    bool hold = false;   // do not respond (a hanging request, to test cancellation)
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
        QByteArray out = "HTTP/1.1 " + QByteArray::number(rep.status) + " X\r\n"
                         "Content-Type: application/json\r\n"
                         "Content-Length: " + QByteArray::number(rep.body.size()) + "\r\n"
                         "Connection: close\r\n\r\n" + rep.body;
        s->write(out);
        s->disconnectFromHost();
    }
    QTcpServer m_srv;
};

// 16-bit mono WAV (sine): the real input of the mixdown.
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

// The Soniox flow's default (successful) replies.
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
    void summarizeLongMeetingInParts();
    void summarizePartFailureResumes();
    void summarizeCancelBetweenParts();
    // LLM context (see providers/chat/LlmContext.h, providers/chat/LlmServer.h).
    void summarizeAdaptsToSmallContext();
    void summarizeContextTooSmall();
    void summarizeLoadsModelOnLmStudio();
    void summarizeLoadRefusedAndCancelDuringLoad();
    void estimateDescribesQuickSummaryCalls();
    void topicsEditReorderAndStatuses();
    void cancelTopicQueue();
    void renamedDuringTranscriptionSurvives();
    void identifyRunsAsLastStage();
    void retranscribeSuccessClearsNamesAndCorrections();
    void staleSummaryShowsInStateAndPending();
    // A manually added, non-speaking participant survives the transcription too.
    void manualParticipantSurvivesTranscription();

    // Data safety.
    void retryAfterFailedRetranscribeClearsNames();
    void emptySttResultKeepsOldTranscript();
    void missingTrackKeepsExistingMixdown();
    void identifyResultsDroppedWhenTranscriptChanges();

private:
    void newApp();

    // People by name (people.json): created if missing (pid) / only looked up (idOf); a picker's choice; the name.
    PersonId pid(const QString& name) { return m_app->people()->ensure(name); }
    PersonId idOf(const QString& name) { m_app->people()->refresh(); return m_app->people()->idForName(name); }
    PersonChoice choice(const QString& name) { return m_app->people()->choiceFor(name); }
    QString nameOf(const PersonId& id) { m_app->people()->refresh(); return m_app->people()->displayName(id); }
    Meeting recording(const QString& title, int seconds);
    Meeting transcribed(const QString& title);
    Meeting transcribedLong(const QString& title, int minutes);
    // Like transcribedLong, but with long (~600-character) paragraphs, so a 15-minute part is
    // ~17 thousand characters.
    Meeting transcribedDense(const QString& title, int minutes);

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<FakeHttp> m_http;
    std::unique_ptr<app::AppController> m_app;
    QStringList m_errors;   // the old errorOccurred channel
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
    providers::LlmServerProbe::invalidateCache();   // the port may be reused: old server data must not linger
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
    m_app = std::make_unique<app::AppController>();
    connect(m_app.get(), &app::AppController::errorOccurred, this, [this](const QString& e) { m_errors << e; });
    tanara::app::AppSettings s = m_app->settings()->settings();
    QVERIFY(m_app->settings()->paths().primaryRecordingRoot().startsWith(m_home->path()));   // TANARA_HOME: the recordings are in the sandbox too
    s.providers.stt.selectedId = QStringLiteral("soniox");
    s.providers.stt.configs[s.providers.stt.selectedId].baseUrl = m_http->base();
    s.providers.llm.selectedId = QStringLiteral("openai-compat");
    s.providers.llm.configs[s.providers.llm.selectedId].baseUrl = m_http->base();
    s.providers.llm.configs[s.providers.llm.selectedId].model = QStringLiteral("teszt-modell");
    m_app->settings()->setSettings(s);
    m_app->setSecret(app::keys::SonioxApiKey, QStringLiteral("teszt-kulcs"));
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

// A meeting with a finished transcript (for the summary tests), no provider call.
Meeting AppJobsTest::transcribed(const QString& title)
{
    Meeting m = recording(title, 2);
    QJsonArray toks;
    const QStringList words{"Sziasztok,", " az", " árajánlatot", " Ödön", " küldi."};
    for (int i = 0; i < words.size(); ++i)
        toks.append(QJsonObject{{"text", words[i]}, {"speaker", "speaker:1"}, {"startMs", i * 300},
                                {"endMs", i * 300 + 250}, {"confidence", 0.9}, {"trackId", "mixdown"}});
    QFile f(QDir(m.folder).filePath("transcript.tokens.json"));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(QJsonObject{{"language", "hu"}, {"tokens", toks}}).toJson());
    f.close();
    QFile seg(QDir(m.folder).filePath("transcript.segments.json"));
    if (seg.open(QIODevice::WriteOnly))
        seg.write(QJsonDocument(QJsonArray{
            QJsonObject{{"startMs", 0}, {"endMs", 550}, {"speaker", "speaker:1"}, {"text", "Sziasztok, az"}},
            QJsonObject{{"startMs", 600}, {"endMs", 1450}, {"speaker", "speaker:1"}, {"text", "árajánlatot Ödön küldi."}}}).toJson());
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
    library::MeetingJobTracker* jobs = m_app->jobs();

    // Records every progress snapshot: stage to the sequence of states reached.
    QHash<QString, QVector<StageState>> seen;
    int uploadPercentMax = -1;
    connect(jobs, &library::MeetingJobTracker::jobProgressChanged, this, [&](const QString&, const JobProgress& p) {
        if (p.kind != JobKind::Transcribe) return;
        for (const JobStage& st : p.stages) {
            QVector<StageState>& v = seen[st.id];
            if (v.isEmpty() || v.last() != st.state) v.append(st.state);
            if (st.id == "upload" && st.state == StageState::Running)
                uploadPercentMax = qMax(uploadPercentMax, st.percent);
            if (st.id == "transcribe")
                QCOMPARE(st.percent, -1);   // the provider gives no percentage, so we do not invent one
        }
    });
    QSignalSpy finished(jobs, &library::MeetingJobTracker::jobFinished);
    QSignalSpy ready(m_app.get(), &app::AppController::transcriptReady);
    QSignalSpy legacy(m_app.get(), &app::AppController::jobProgress);

    m_app->transcribeMeeting(m.id);
    // Visible at once: running, cancellable, with a stage list (the mix is needed, so it is first).
    JobProgress jp = jobs->job(m.id, JobKind::Transcribe);
    QVERIFY(jp.isValid());
    QVERIFY(jp.cancellable);
    QStringList ids;
    for (const JobStage& st : jp.stages) ids << st.id;
    QCOMPARE(ids, QStringList({"mixdown", "upload", "transcribe", "diarize"}));   // (no voice model, so no "identify")
    QCOMPARE(jp.stage("mixdown")->state, StageState::Running);
    QVERIFY(jp.stage("upload")->detail.contains("1 sáv"));
    QCOMPARE(jp.estimatedTotalSec, -1);                       // no estimate without an earlier run
    MeetingProcessingState st = m_app->processingState(m.id);
    QCOMPARE(st.transcriptState, StepState::Running);
    QVERIFY(st.mixdownRunning);
    QCOMPARE(m_app->library()->entry(m.id).state.transcriptState, StepState::Running);
    m_app->transcribeMeeting(m.id);                           // double start: no-op

    QVERIFY(ready.wait(30000));
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(1).value<JobKind>(), JobKind::Transcribe);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Done);
    QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));
    QVERIFY(legacy.count() > 0);                              // the old free-text signal works too

    // The stages went through in order.
    QCOMPARE(seen["mixdown"], QVector<StageState>({StageState::Waiting, StageState::Running, StageState::Done}));
    QCOMPARE(seen["upload"], QVector<StageState>({StageState::Waiting, StageState::Running, StageState::Done}));
    QCOMPARE(seen["transcribe"], QVector<StageState>({StageState::Waiting, StageState::Running, StageState::Done}));
    QCOMPARE(seen["diarize"], QVector<StageState>({StageState::Waiting, StageState::Running, StageState::Done}));
    QVERIFY(uploadPercentMax >= 0);                           // a real upload percentage arrived

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
    // Soniox timing: the tokens are untouched, tokens.json gets a "timing": "emission" marker,
    // the lines (segments.json) are padded (-300 / +100 ms, up to the neighboring line).
    {
        QFile tf(QDir(m.folder).filePath("transcript.tokens.json"));
        QVERIFY(tf.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(tf.readAll()).object();
        QCOMPARE(root.value("timing").toString(), QStringLiteral("emission"));
        QCOMPARE(root.value("tokens").toArray().at(1).toObject().value("startMs").toInt(), 400);
        QCOMPARE(root.value("tokens").toArray().at(1).toObject().value("endMs").toInt(), 900);
        QFile sf(QDir(m.folder).filePath("transcript.segments.json"));
        QVERIFY(sf.open(QIODevice::ReadOnly));
        const QJsonArray segs = QJsonDocument::fromJson(sf.readAll()).array();
        QCOMPARE(segs.size(), 2);
        QCOMPARE(segs.at(0).toObject().value("startMs").toInt(), 0);
        QCOMPARE(segs.at(0).toObject().value("endMs").toInt(), 1000);
        QCOMPARE(segs.at(1).toObject().value("startMs").toInt(), 1700);
        QCOMPARE(segs.at(1).toObject().value("endMs").toInt(), 2600);
    }
    QCOMPARE(m_http->count("POST", "/files"), 1);             // the double start has not sent another one
    QTRY_COMPARE(m_http->count("DELETE", "/"), 2);            // provider-side cleanup

    // Library search sees the new transcript too.
    library::LibraryQuery q;
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
    QSignalSpy finished(m_app->jobs(), &library::MeetingJobTracker::jobFinished);
    QSignalSpy errChanged(m_app->jobs(), &library::MeetingJobTracker::errorChanged);
    m_app->transcribeMeeting(m.id);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Failed);
    QCOMPARE(errChanged.count(), 1);
    QCOMPARE(m_errors.size(), 1);                             // the old global error signal arrived too

    MeetingProcessingState st = m_app->processingState(m.id);
    QCOMPARE(st.transcriptState, StepState::Failed);
    QCOMPARE(st.transcriptError.detail, QStringLiteral("HTTP 401 · invalid_api_key · Invalid API key."));
    QVERIFY(st.transcriptError.message.contains("API-kulcs"));
    QCOMPARE(st.transcriptError.fixActionHint, QStringLiteral("settings:stt"));
    // Queryable after selecting another meeting too; the other meeting is untouched.
    QCOMPARE(m_app->processingState(other.id).transcriptState, StepState::None);
    QCOMPARE(m_app->processingState(m.id).transcriptState, StepState::Failed);
    // The recording and the (already finished) mix stayed.
    QVERIFY(QDir(m.folder).exists("track_mic.wav"));
    QVERIFY(!m_app->store()->load(m.id).hasTranscript);

    // "These are waiting for you": the failed and the not yet transcribed meeting.
    QVector<library::PendingItem> pending = m_app->library()->pendingItems();
    QCOMPARE(pending.size(), 2);
    int failedCount = 0;
    for (const library::PendingItem& p : pending)
        if (p.kind == library::PendingKind::TranscriptionFailed) { ++failedCount; QCOMPARE(p.meetingId, m.id); }
    QCOMPARE(failedCount, 1);

    // Visible after a restart too.
    newApp();
    st = m_app->processingState(m.id);
    QCOMPARE(st.transcriptState, StepState::Failed);
    QCOMPARE(st.transcriptError.detail, QStringLiteral("HTTP 401 · invalid_api_key · Invalid API key."));

    // Retry with a good key: success, so the error disappears.
    m_http->handler = [](const FakeRequest& r) { return sonioxOk(r); };
    QSignalSpy ready(m_app.get(), &app::AppController::transcriptReady);
    m_app->transcribeMeeting(m.id);
    QCOMPARE(m_app->jobs()->job(m.id, JobKind::Transcribe).stages.first().id, QStringLiteral("upload"));   // the mix already exists
    QVERIFY(ready.wait(30000));
    QTRY_VERIFY(!m_app->jobs()->isBusy(m.id));
    st = m_app->processingState(m.id);
    QCOMPARE(st.transcriptState, StepState::Done);
    QVERIFY(!st.transcriptError.isValid());
    QVERIFY(!QDir(m.folder).exists(library::MeetingJobTracker::stateFileName()));
}

void AppJobsTest::cancelTranscriptionAtProvider()
{
    if (!m_ffmpeg) QSKIP("ffmpeg nem található");
    // The provider processes "forever".
    m_http->handler = [](const FakeRequest& r) { return sonioxOk(r, "processing"); };
    // Re-transcription: an earlier transcript and a named speaker exist; after cancel all stay.
    Meeting m = transcribed("Megszakított újra-átírás");
    m.speakerMap.insert("speaker:1", pid("Ödön"));
    m_app->store()->saveMeeting(m);
    const QByteArray tokensBefore = [&] { QFile f(QDir(m.folder).filePath("transcript.tokens.json")); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }();

    // There is a manual per-line fix (overlay) too, which must survive the cancel as well.
    {
        editing::SpeakerEditSession* ed = m_app->editSession(m.id);
        QVERIFY(ed);
        QCOMPARE(ed->utteranceCount(), 2);
        QVERIFY(!ed->moveUtterancesToPerson({ed->utteranceAt(1).id}, choice("Kovács Lilla")).isEmpty());
        m_app->closeEditSession(m.id);
    }
    QCOMPARE(m_app->retranscribeImpact(m.id).correctedUtterances, 1);
    QVERIFY(QFile::exists(editing::speakeredit::overlayPath(m.folder)));

    library::MeetingJobTracker* jobs = m_app->jobs();
    QSignalSpy finished(jobs, &library::MeetingJobTracker::jobFinished);
    m_app->retranscribeMeeting(m.id);
    // We wait until it runs at the provider (the mixdown and the upload are done).
    QTRY_VERIFY_WITH_TIMEOUT(jobs->job(m.id, JobKind::Transcribe).isValid()
        && jobs->job(m.id, JobKind::Transcribe).stage("transcribe")->state == StageState::Running, 30000);
    // The "transcribe" stage already runs BEFORE the REPLY to POST /transcriptions (Queued); the
    // transcription id arrives only with the reply. The first status poll signals that the id
    // exists (otherwise the cancel aborts the still in-flight POST and there is nothing to delete;
    // on Windows, because of the slower local connect, this was routinely the case).
    QTRY_VERIFY_WITH_TIMEOUT(m_http->count("GET", "/transcriptions/t1") >= 1, 30000);
    QCOMPARE(nameOf(m_app->store()->load(m.id).speakerMap.value("speaker:1")), QStringLiteral("Ödön"));   // not deleted in advance

    QVERIFY(m_app->cancelJob(m.id, JobKind::Transcribe));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Cancelled);
    QVERIFY(!m_app->cancelJob(m.id, JobKind::Transcribe));    // nothing left to cancel

    // Cancel is not an error: no errorOccurred, no persistent error; the state is consistent.
    QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));
    const MeetingProcessingState st = m_app->processingState(m.id);
    QCOMPARE(st.transcriptState, StepState::Done);            // the EARLIER transcript lives
    QVERIFY(!st.transcriptError.isValid());
    QVERIFY(!st.busy());
    const Meeting after = m_app->store()->load(m.id);
    QVERIFY(after.hasTranscript);
    QCOMPARE(nameOf(after.speakerMap.value("speaker:1")), QStringLiteral("Ödön"));
    QFile f(QDir(m.folder).filePath("transcript.tokens.json"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), tokensBefore);
    // The manual line fix is there too (the overlay would be deleted only when the NEW transcript arrives).
    QCOMPARE(m_app->retranscribeImpact(m.id).correctedUtterances, 1);
    QCOMPARE(m_app->retranscribeImpact(m.id).namedSpeakers, 2);
    QVERIFY(QDir(m.folder).exists("track_mic.wav"));          // the recording is there
    // At the provider the uploaded audio and the transcription are deleted (Soniox cleanup).
    QTRY_COMPARE(m_http->count("DELETE", "/transcriptions/t1"), 1);
    QTRY_COMPARE(m_http->count("DELETE", "/files/f1"), 1);
    // No late result arrives.
    QSignalSpy ready(m_app.get(), &app::AppController::transcriptReady);
    QTest::qWait(300);
    QCOMPARE(ready.count(), 0);
}

void AppJobsTest::cancelDuringMixdownKeepsOldMixdown()
{
    if (!m_ffmpeg) QSKIP("ffmpeg nem található");
    m_http->handler = [](const FakeRequest& r) { return sonioxOk(r); };
    Meeting m = recording("Hosszú felvétel", 900);            // 15 minutes: the mix takes a while
    // There is an old (stale) mix: it must remain after the cancel too.
    const QByteArray oldMix = "REGI-KEVEREK";
    { QFile f(QDir(m.folder).filePath("mixdown.mp3")); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(oldMix); }
    m.mixdownFile = "mixdown.mp3";
    m.mixdownDirty = true;
    m_app->store()->saveMeeting(m);

    QSignalSpy finished(m_app->jobs(), &library::MeetingJobTracker::jobFinished);
    QSignalSpy mixDone(m_app.get(), &app::AppController::mixdownUpdated);
    m_app->transcribeMeeting(m.id);
    QVERIFY(m_app->processingState(m.id).mixdownRunning);
    QVERIFY(m_app->cancelJob(m.id, JobKind::Transcribe));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Cancelled);
    QVERIFY(mixDone.wait(10000));                             // ffmpeg stopped
    QCOMPARE(mixDone.at(0).at(1).toBool(), false);

    QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));
    QFile f(QDir(m.folder).filePath("mixdown.mp3"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), oldMix);                            // the old mix is untouched
    QVERIFY(!QDir(m.folder).exists("mixdown.part.mp3"));      // no half-finished file remained
    QVERIFY(QDir(m.folder).exists("track_mic.wav"));
    const MeetingProcessingState st = m_app->processingState(m.id);
    QCOMPARE(st.transcriptState, StepState::None);
    QVERIFY(!st.mixdownRunning);
    QVERIFY(!st.busy());
    QTest::qWait(200);
    QVERIFY(m_http->log.isEmpty());                           // nothing went to the provider
}

void AppJobsTest::standaloneMixdownCancel()
{
    if (!m_ffmpeg) QSKIP("ffmpeg nem található");
    const Meeting m = recording("Keverés", 900);
    library::MeetingJobTracker* jobs = m_app->jobs();
    QSignalSpy mixDone(m_app.get(), &app::AppController::mixdownUpdated);
    QSignalSpy finished(jobs, &library::MeetingJobTracker::jobFinished);
    m_app->regenerateMixdown(m.id);
    QVERIFY(jobs->isRunning(m.id, JobKind::Mixdown));
    m_app->regenerateMixdown(m.id);                           // already running: no-op
    QVERIFY(m_app->processingState(m.id).mixdownRunning);
    QCOMPARE(m_app->processingState(m.id).transcriptState, StepState::None);
    // A real percentage arrives from ffmpeg.
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
    QSignalSpy ready(m_app.get(), &app::AppController::summaryReady);
    QSignalSpy finished(m_app->jobs(), &library::MeetingJobTracker::jobFinished);
    m_app->summarizeMeeting(m.id);
    QVERIFY(m_app->jobs()->isRunning(m.id, JobKind::Summarize));
    QCOMPARE(m_app->processingState(m.id).summaryState, StepState::Running);
    QVERIFY(ready.wait(10000));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Done);
    QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));
    QCOMPARE(m_app->processingState(m.id).summaryState, StepState::Done);

    // The structure is persisted (summary.json) + the markdown exists too.
    QVERIFY(QDir(m.folder).exists("summary.json"));
    QVERIFY(QDir(m.folder).exists("summary.md"));
    const library::SummaryDocument doc = m_app->summaryDocument(m.id);
    QVERIFY(doc.exists);
    QVERIFY(!doc.fromMarkdown);
    QCOMPARE(doc.summary.execSummary, QStringLiteral("Rövid egyeztetés az árajánlatról."));
    QCOMPARE(doc.summary.decisions, QStringList({"Az árajánlat pénteken megy ki."}));
    QCOMPARE(doc.summary.actionItems.size(), 1);
    QCOMPARE(doc.summary.actionItems[0].owner, QStringLiteral("Ödön"));
    // The participants are the transcript's speakers (deterministically, not from the model's reply).
    QCOMPARE(doc.summary.participants, QStringList({"Beszélő 1"}));   // the summary language (Hungarian default)
    QVERIFY(doc.summary.memo.isEmpty());                      // JSON-only reply: no memo note
    // Short meeting: one call, one stage.
    QCOMPARE(m_http->count("POST", "/chat/completions"), 1);
    QVERIFY(!QDir(m.folder).exists("summary.notes.json"));
    // Metadata: when, which provider/model, in what mode.
    QCOMPARE(doc.meta.mode, library::SummaryMode::Quick);
    QCOMPARE(doc.meta.providerId, QStringLiteral("openai-compat"));
    QCOMPARE(doc.meta.model, QStringLiteral("teszt-modell"));
    QVERIFY(qAbs(doc.meta.createdAt.secsTo(QDateTime::currentDateTime())) < 30);
    QVERIFY(doc.markdown.contains("## Vezetői összefoglaló"));
    // The note copy went into the sandbox too.
    QVERIFY(!QDir(m_home->filePath("notes")).entryList({"*.md"}).isEmpty());
}

void AppJobsTest::summarizeCancelAndFailure()
{
    const Meeting m = transcribed("Megszakított összefoglaló");
    // 1) Cancelling a pending LLM call.
    m_http->handler = [](const FakeRequest& r) -> FakeReply {
        if (!r.path.endsWith("/chat/completions")) return {404, "{}"};   // no native API
        FakeReply h; h.hold = true; return h;
    };
    QSignalSpy finished(m_app->jobs(), &library::MeetingJobTracker::jobFinished);
    QSignalSpy ready(m_app.get(), &app::AppController::summaryReady);
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

    // 2) Provider error: a persistent error with a human and a technical line.
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
    QCOMPARE(st.transcriptState, StepState::Done);            // does not affect the transcript step
    // The error can be dismissed.
    m_app->jobs()->clearError(m.id, JobKind::Summarize);
    QCOMPARE(m_app->processingState(m.id).summaryState, StepState::None);
}

// Long (multi-part) transcript: one paragraph every half minute, two speakers alternating.
Meeting AppJobsTest::transcribedLong(const QString& title, int minutes)
{
    Meeting m = recording(title, 1);
    QJsonArray toks;
    for (int i = 0; i < minutes * 2; ++i)
        toks.append(QJsonObject{{"text", QStringLiteral(" A(z) %1. bekezdés szövege, elég hosszan ahhoz, hogy "
                                                        "számítson: egy valódi megbeszélésen fél perc alatt "
                                                        "ennél jóval több szó hangzik el.").arg(i)},
                                {"speaker", i % 2 ? "speaker:2" : "speaker:1"}, {"startMs", i * 30000},
                                {"endMs", i * 30000 + 20000}, {"confidence", 0.9}, {"trackId", "mixdown"}});
    QFile f(QDir(m.folder).filePath("transcript.tokens.json"));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(QJsonObject{{"language", "hu"}, {"tokens", toks}}).toJson());
    f.close();
    m.hasTranscript = true;
    m.durationMs = qint64(minutes) * 60000;
    m_app->store()->saveMeeting(m);
    return m;
}

namespace {
// From the request body: whether it is a note call, and which part.
int notesPart(const FakeRequest& r)
{
    const QJsonArray msgs = QJsonDocument::fromJson(r.body).object().value("messages").toArray();
    if (!msgs.at(0).toObject().value("content").toString().contains("ONE PART")) return 0;
    static const QRegularExpression re(QStringLiteral("^PART (\\d+) of"), QRegularExpression::MultilineOption);
    return re.match(msgs.at(1).toObject().value("content").toString()).captured(1).toInt();
}
QString partNotes(int k)
{
    return QStringLiteral("TOPICS\n### [%1:00-%1:40] Tárgy %2\n- Egy pont a(z) %2. részből.\n"
                          "DECISIONS\n- none\nOPEN\n- [%1:30] Nyitott ügy %2\nACTIONS\n- none\n")
        .arg((k - 1) * 15).arg(k);
}
const char* kMergeOk =
    R"({"execSummary":"Hosszú megbeszélés összegzése.","decisions":["Marad a terv."],)"
    R"("openQuestions":["Nyitott ügy 3"],"actionItems":[]})";
} // namespace

void AppJobsTest::summarizeLongMeetingInParts()
{
    m_http->handler = [](const FakeRequest& r) -> FakeReply {
        if (!r.path.endsWith("/chat/completions")) return {404, "{}"};
        const int k = notesPart(r);
        return {200, chat(k > 0 ? partNotes(k) : QString::fromUtf8(kMergeOk))};
    };
    const Meeting m = transcribedLong("Hosszú megbeszélés", 40);   // 3 parts: 3 notes + 1 summary
    library::MeetingJobTracker* jobs = m_app->jobs();
    QStringList trail;   // stage states + chunk progress, as the UI sees it
    connect(jobs, &library::MeetingJobTracker::jobProgressChanged, this, [&](const QString&, const JobProgress& p) {
        if (p.kind != JobKind::Summarize) return;
        QString line;
        for (const JobStage& st : p.stages) line += st.id + ":" + QString::number(int(st.state)) + " ";
        line += QStringLiteral("%1/%2").arg(p.done).arg(p.total);
        if (trail.isEmpty() || trail.last() != line) trail << line;
    });
    QSignalSpy ready(m_app.get(), &app::AppController::summaryReady);
    m_app->summarizeMeeting(m.id);
    JobProgress jp = jobs->job(m.id, JobKind::Summarize);
    QStringList ids;
    for (const JobStage& st : jp.stages) ids << st.id;
    QCOMPARE(ids, QStringList({"notes", "merge"}));
    QCOMPARE(jp.total, 3);
    QVERIFY(jp.stage("notes")->detail.contains("3"));
    QVERIFY(ready.wait(10000));
    QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));
    QCOMPARE(m_http->count("POST", "/chat/completions"), 4);
    // Note taking went part by part (0/3 to 3/3), then the summary ran.
    QVERIFY2(trail.contains("notes:1 merge:0 0/3"), qPrintable(trail.join(" | ")));
    QVERIFY2(trail.contains("notes:1 merge:0 2/3"), qPrintable(trail.join(" | ")));
    QVERIFY2(trail.contains("notes:2 merge:1 3/3"), qPrintable(trail.join(" | ")));

    const library::SummaryDocument doc = m_app->summaryDocument(m.id);
    QCOMPARE(doc.summary.execSummary, QStringLiteral("Hosszú megbeszélés összegzése."));
    QCOMPARE(doc.summary.openQuestions, QStringList({"Nyitott ügy 3"}));
    QCOMPARE(doc.summary.memo.size(), 3);
    QCOMPARE(doc.summary.memo[2].title, QStringLiteral("Tárgy 3"));
    QCOMPARE(doc.summary.memo[2].startMs, 30 * 60000);
    QCOMPARE(doc.summary.participants, QStringList({"Beszélő 1", "Beszélő 2"}));
    QVERIFY(doc.markdown.contains("## Nyitott kérdések"));
    QVERIFY(doc.markdown.contains("## Memó\n\n### Tárgy 1 (00:00–00:40)"));
    QVERIFY(!QDir(m.folder).exists("summary.notes.json"));      // success: no leftover notes
    // The note copy contains the memo too.
    const QStringList notes = QDir(m_home->filePath("notes")).entryList({"*.md"});
    QCOMPARE(notes.size(), 1);
    QFile nf(QDir(m_home->filePath("notes")).filePath(notes.first()));
    QVERIFY(nf.open(QIODevice::ReadOnly));
    QVERIFY(QString::fromUtf8(nf.readAll()).contains("### Tárgy 2"));
}

void AppJobsTest::summarizePartFailureResumes()
{
    bool broken = true;
    m_http->handler = [&broken](const FakeRequest& r) -> FakeReply {
        const int k = notesPart(r);
        if (k == 2 && broken) return {500, R"({"error":{"message":"model crashed","type":"server_error"}})"};
        return {200, chat(k > 0 ? partNotes(k) : QString::fromUtf8(kMergeOk))};
    };
    const Meeting m = transcribedLong("Elbukó rész", 40);
    QSignalSpy finished(m_app->jobs(), &library::MeetingJobTracker::jobFinished);
    QSignalSpy ready(m_app.get(), &app::AppController::summaryReady);
    m_app->summarizeMeeting(m.id);
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Failed);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(m_http->count("POST", "/chat/completions"), 2);   // the 3rd part did not start
    // A clean error: no half-finished summary, the persistent error has a technical line.
    QVERIFY(!QDir(m.folder).exists("summary.md"));
    QVERIFY(!QDir(m.folder).exists("summary.json"));
    QVERIFY(!m_app->store()->load(m.id).hasSummary);
    MeetingProcessingState st = m_app->processingState(m.id);
    QCOMPARE(st.summaryState, StepState::Failed);
    QCOMPARE(st.summaryError.detail, QStringLiteral("HTTP 500 · server_error · model crashed"));
    QVERIFY(QDir(m.folder).exists("summary.notes.json"));     // part 1's note stayed

    // Retry: part 1 does not run again; the stage shows the part taken over from the earlier run.
    broken = false;
    m_errors.clear();
    const int before = m_http->count("POST", "/chat/completions");
    m_app->summarizeMeeting(m.id);
    const JobProgress jp = m_app->jobs()->job(m.id, JobKind::Summarize);
    QCOMPARE(jp.done, 1);
    QVERIFY(ready.wait(10000));
    QCOMPARE(m_http->count("POST", "/chat/completions") - before, 3);   // parts 2 + 3 + summary
    QCOMPARE(m_app->summaryDocument(m.id).summary.memo.size(), 3);
    QCOMPARE(m_app->processingState(m.id).summaryState, StepState::Done);
}

Meeting AppJobsTest::transcribedDense(const QString& title, int minutes)
{
    Meeting m = recording(title, 1);
    QJsonArray toks;
    const QString filler = QStringLiteral(" Ez a mondat azért van itt, hogy a bekezdés hosszú legyen, ahogy egy "
                                          "valódi megbeszélésen is sok szó hangzik el fél perc alatt.");
    for (int i = 0; i < minutes * 2; ++i)
        toks.append(QJsonObject{{"text", QStringLiteral(" A(z) %1. bekezdés.").arg(i) + filler + filler + filler + filler},
                                {"speaker", i % 2 ? "speaker:2" : "speaker:1"}, {"startMs", i * 30000},
                                {"endMs", i * 30000 + 20000}, {"confidence", 0.9}, {"trackId", "mixdown"}});
    QFile f(QDir(m.folder).filePath("transcript.tokens.json"));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(QJsonObject{{"language", "hu"}, {"tokens", toks}}).toJson());
    f.close();
    m.hasTranscript = true;
    m.durationMs = qint64(minutes) * 60000;
    m_app->store()->saveMeeting(m);
    return m;
}

namespace {
// A server without a native API (e.g. llama.cpp) that rejects a too-long note request with
// llama.cpp's error. The server's "token" here is 2.5 characters; the note request fits at
// most allowUser characters next to the system prompt (+ the 3000-token output budget).
struct SmallContextServer {
    int allowUser = 8000;
    int overflows = 0;
    int notesCalls = 0;
    int maxNotesUser = 0;
    FakeReply operator()(const FakeRequest& r) {
        if (!r.path.endsWith("/chat/completions")) return {404, R"({"error":"Unexpected endpoint"})"};
        const QJsonArray msgs = QJsonDocument::fromJson(r.body).object().value("messages").toArray();
        const int system = int(msgs.at(0).toObject().value("content").toString().size());
        int input = 0;
        for (const QJsonValue& v : msgs) input += int(v.toObject().value("content").toString().size());
        const int k = notesPart(r);
        if (k > 0) {
            ++notesCalls;
            const int allowed = system + allowUser;
            if (input > allowed) {
                ++overflows;
                const int nCtx = 3000 + int(std::ceil(allowed / 2.5));
                const int nPrompt = int(std::ceil(input / 2.5));
                return {400, QStringLiteral(R"({"error":{"code":400,"message":"request (%1 tokens) exceeds the available context size (%2 tokens), try increasing it","type":"exceed_context_size_error","n_prompt_tokens":%1,"n_ctx":%2}})")
                                 .arg(nPrompt).arg(nCtx).toUtf8()};
            }
            maxNotesUser = qMax(maxNotesUser, input - system);
            return {200, chat(partNotes(k))};
        }
        return {200, chat(QString::fromUtf8(kMergeOk))};
    }
};

// LM Studio's native API (GET /api/v1/models, POST load / unload) + chat, with state.
struct FakeLmStudio {
    QString model = QStringLiteral("teszt-modell");
    int ctx = 4096;            // 0 = not loaded
    int parallel = 4;
    bool refuseLoad = false;
    bool holdLoad = false;
    QVector<QJsonObject> loads;
    QStringList order;         // the order of requests (models | unload | load | chat)
    QString chatReply;
    FakeReply operator()(const FakeRequest& r) {
        if (r.path == "/api/v1/models") {
            order << "models";
            QJsonArray inst;
            if (ctx > 0)
                inst.append(QJsonObject{{"id", model}, {"config", QJsonObject{{"context_length", ctx}, {"parallel", parallel}}}});
            const QJsonArray models{QJsonObject{{"type", "llm"}, {"key", model}, {"loaded_instances", inst},
                                                {"max_context_length", 131072}}};
            return {200, QJsonDocument(QJsonObject{{"models", models}}).toJson(QJsonDocument::Compact)};
        }
        if (r.path == "/api/v1/models/unload") { order << "unload"; ctx = 0; return {200, R"({"instance_id":"teszt-modell"})"}; }
        if (r.path == "/api/v1/models/load") {
            order << "load";
            const QJsonObject b = QJsonDocument::fromJson(r.body).object();
            loads << b;
            if (holdLoad) { FakeReply h; h.hold = true; return h; }
            if (refuseLoad)
                return {500, R"({"error":{"type":"insufficient_resources","message":"Model loading was stopped due to insufficient system resources."}})"};
            ctx = b.value("context_length").toInt();
            parallel = b.value("parallel").toInt();
            return {200, QStringLiteral(R"({"instance_id":"teszt-modell","status":"loaded","load_config":{"context_length":%1}})")
                             .arg(ctx).toUtf8()};
        }
        if (r.path.endsWith("/chat/completions")) { order << "chat"; return {200, chat(chatReply)}; }
        return {404, "{}"};
    }
};
} // namespace

// A server without a native API, small context: after the "does not fit" error the remaining
// parts continue split into smaller ones (once); the next run splits that way from the start.
void AppJobsTest::summarizeAdaptsToSmallContext()
{
    const Meeting m = transcribedDense("Kis kontextus", 40);   // 3 parts, ~17 thousand characters
    auto srv = std::make_shared<SmallContextServer>();
    m_http->handler = [srv](const FakeRequest& r) { return (*srv)(r); };
    QSignalSpy ready(m_app.get(), &app::AppController::summaryReady);
    QSignalSpy progress(m_app->jobs(), &library::MeetingJobTracker::jobProgressChanged);
    m_app->summarizeMeeting(m.id);
    QVERIFY(ready.wait(20000));
    QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));
    QCOMPARE(srv->overflows, 1);                    // only the first request failed
    QVERIFY2(srv->notesCalls > 4, qPrintable(QString::number(srv->notesCalls)));
    QVERIFY(srv->maxNotesUser <= srv->allowUser);
    // The stages switched to more parts (the chunk progress shows the new part count).
    int maxTotal = 0;
    for (const auto& args : progress)
        maxTotal = qMax(maxTotal, args.at(1).value<JobProgress>().total);
    QVERIFY2(maxTotal > 3, qPrintable(QString::number(maxTotal)));
    QCOMPARE(m_app->processingState(m.id).summaryState, StepState::Done);

    // Again: with the learned context there is no overflow.
    const int calls = srv->notesCalls;
    m_app->summarizeMeeting(m.id);
    QVERIFY(ready.wait(20000));
    QCOMPARE(srv->overflows, 1);
    QVERIFY(srv->notesCalls > calls);
}

// If the context is not enough even for the smallest part: an understandable error with the
// required minimum; no repeat, no raw JSON.
void AppJobsTest::summarizeContextTooSmall()
{
    const Meeting m = transcribedDense("Túl kicsi kontextus", 40);
    auto srv = std::make_shared<SmallContextServer>();
    srv->allowUser = 1500;
    m_http->handler = [srv](const FakeRequest& r) { return (*srv)(r); };
    QSignalSpy finished(m_app->jobs(), &library::MeetingJobTracker::jobFinished);
    m_app->summarizeMeeting(m.id);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Failed);
    QCOMPARE(srv->notesCalls, 1);
    const JobError e = m_app->processingState(m.id).summaryError;
    QVERIFY2(e.fixActionHint.startsWith(QStringLiteral("settings:llm-context:")), qPrintable(e.fixActionHint));
    QVERIFY(library::contextFixTokens(e.fixActionHint) >= 8192);
    QVERIFY2(e.message.contains(QStringLiteral("kontextussal")), qPrintable(e.message));
    QVERIFY(!e.message.contains(QLatin1Char('{')));
    QVERIFY(!e.detail.contains(QLatin1Char('{')));
    QVERIFY(e.detail.contains(QStringLiteral("exceed_context_size_error")));
    QCOMPARE(m_errors.size(), 1);
    QVERIFY(!m_errors.first().contains(QLatin1Char('{')));
}

// LM Studio: 4096 context, parallel 4 (the incident): in the job's first stage the model is
// unloaded and reloaded on one thread, with enough context; then the call. The "Load with a
// larger context" request loads larger on the next run; if all is well, there is no load.
void AppJobsTest::summarizeLoadsModelOnLmStudio()
{
    auto lm = std::make_shared<FakeLmStudio>();
    lm->chatReply = QStringLiteral(R"({"execSummary":"Rövid.","decisions":[],"actionItems":[]})");
    m_http->handler = [lm](const FakeRequest& r) { return (*lm)(r); };
    const Meeting m = transcribed("LM Studio");
    QSignalSpy ready(m_app.get(), &app::AppController::summaryReady);
    QSignalSpy progress(m_app->jobs(), &library::MeetingJobTracker::jobProgressChanged);
    m_app->summarizeMeeting(m.id);
    QVERIFY(ready.wait(10000));
    QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));
    QCOMPARE(lm->order, (QStringList{"models", "unload", "load", "chat"}));
    QCOMPARE(lm->loads.size(), 1);
    QCOMPARE(lm->loads[0].value("parallel").toInt(), 1);
    const int first = lm->loads[0].value("context_length").toInt();
    QVERIFY2(first >= 8192 && first <= 32768, qPrintable(QString::number(first)));
    bool sawModelStage = false;
    for (const auto& args : progress) {
        const JobProgress p = args.at(1).value<JobProgress>();
        if (const JobStage* st = p.stage(QStringLiteral("model")))
            sawModelStage = sawModelStage || st->state == StageState::Running;
    }
    QVERIFY(sawModelStage);

    // "Load with a larger context": before the next run it reloads with 65,536.
    m_app->requestLlmContext(65536);
    m_app->summarizeMeeting(m.id);
    QVERIFY(ready.wait(10000));
    QCOMPARE(lm->loads.size(), 2);
    QCOMPARE(lm->loads[1].value("context_length").toInt(), 65536);

    // The loaded instance is adequate: no reload (the request was valid once).
    lm->order.clear();
    m_app->summarizeMeeting(m.id);
    QVERIFY(ready.wait(10000));
    QCOMPARE(lm->loads.size(), 2);
    QVERIFY(!lm->order.contains(QStringLiteral("load")));
    QVERIFY(!lm->order.contains(QStringLiteral("unload")));
}

// The load is rejected (does not fit the GPU): a single attempt, an understandable error, no
// chat request. Cancel during loading: no error, no chat.
void AppJobsTest::summarizeLoadRefusedAndCancelDuringLoad()
{
    auto lm = std::make_shared<FakeLmStudio>();
    lm->ctx = 0;
    lm->refuseLoad = true;
    m_http->handler = [lm](const FakeRequest& r) { return (*lm)(r); };
    const Meeting m = transcribed("Nem fér");
    QSignalSpy finished(m_app->jobs(), &library::MeetingJobTracker::jobFinished);
    m_app->summarizeMeeting(m.id);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Failed);
    QTest::qWait(100);
    QCOMPARE(lm->loads.size(), 1);
    QVERIFY(!lm->order.contains(QStringLiteral("chat")));
    const JobError e = m_app->processingState(m.id).summaryError;
    QVERIFY2(e.message.contains(QStringLiteral("nem fér a videókártyára")), qPrintable(e.message));
    QCOMPARE(e.fixActionHint, QStringLiteral("settings:llm-context"));
    QCOMPARE(m_errors.size(), 1);

    // Cancel during loading.
    m_app->jobs()->clearError(m.id, JobKind::Summarize);
    m_errors.clear();
    lm->refuseLoad = false;
    lm->holdLoad = true;
    m_app->summarizeMeeting(m.id);
    QTRY_COMPARE_WITH_TIMEOUT(lm->loads.size(), 2, 10000);
    QVERIFY(m_app->jobs()->job(m.id, JobKind::Summarize).stage(QStringLiteral("model")));
    QVERIFY(m_app->cancelJob(m.id, JobKind::Summarize));
    QCOMPARE(finished.count(), 2);
    QCOMPARE(finished.at(1).at(2).value<JobOutcome>(), JobOutcome::Cancelled);
    QTest::qWait(200);
    QVERIFY(!lm->order.contains(QStringLiteral("chat")));
    QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));
    QVERIFY(!m_app->processingState(m.id).summaryError.isValid());
}

void AppJobsTest::summarizeCancelBetweenParts()
{
    m_http->handler = [](const FakeRequest& r) -> FakeReply {
        const int k = notesPart(r);
        if (k == 2) { FakeReply h; h.hold = true; return h; }
        return {200, chat(k > 0 ? partNotes(k) : QString::fromUtf8(kMergeOk))};
    };
    const Meeting m = transcribedLong("Megszakított hosszú", 40);
    QSignalSpy finished(m_app->jobs(), &library::MeetingJobTracker::jobFinished);
    m_app->summarizeMeeting(m.id);
    QTRY_COMPARE(m_http->count("POST", "/chat/completions"), 2);
    QTRY_COMPARE(m_app->jobs()->job(m.id, JobKind::Summarize).done, 1);
    QVERIFY(m_app->cancelJob(m.id, JobKind::Summarize));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Cancelled);
    QTest::qWait(300);
    QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));
    QCOMPARE(m_http->count("POST", "/chat/completions"), 2);   // no further call started
    QVERIFY(!QDir(m.folder).exists("summary.md"));
    QCOMPARE(m_app->processingState(m.id).summaryState, StepState::None);
    QVERIFY(QDir(m.folder).exists("summary.notes.json"));
}

void AppJobsTest::estimateDescribesQuickSummaryCalls()
{
    // Short meeting: one call.
    const Meeting shortM = transcribed("Rövid");
    cloud::EstimateRequest r = m_app->makeEstimateRequest(shortM.id, "summarize", "quick");
    QCOMPARE(r.llmCalls.size(), 1);
    QCOMPARE(r.llmCalls[0].count, 1);
    QVERIFY(r.llmCalls[0].inputChars > r.transcriptChars);

    // 40 minutes: 3 note calls + 1 summary.
    const Meeting longM = transcribedLong("Hosszú becslés", 40);
    r = m_app->makeEstimateRequest(longM.id, "summarize", "quick");
    QCOMPARE(r.llmCalls.size(), 2);
    QCOMPARE(r.llmCalls[0].count, 3);
    QCOMPARE(r.llmCalls[0].maxTokens, 3000);
    QVERIFY(r.llmCalls[0].inputChars < r.transcriptChars);   // one part + prompt < the whole transcript
    QCOMPARE(r.llmCalls[1].count, 1);
    QCOMPARE(r.summaryMode, QStringLiteral("quick"));
}

void AppJobsTest::topicsEditReorderAndStatuses()
{
    const Meeting m = transcribed("Témák");
    QSignalSpy topicsChanged(m_app.get(), &app::AppController::topicsChanged);

    // Add: the new topics get ids, the one with an empty title is left out.
    QVector<SummaryTopic> saved = m_app->setMeetingTopics(m.id, {
        {QString(), QStringLiteral("Árazás"), QStringLiteral("Csomagok és árak")},
        {QString(), QStringLiteral("  "), QString()},
        {QString(), QStringLiteral("Pilot"), QStringLiteral("Helyszínek")},
        {QString(), QStringLiteral("Határidők"), QString()}});
    QCOMPARE(saved.size(), 3);
    QCOMPARE(topicsChanged.count(), 1);
    for (const SummaryTopic& t : saved) QVERIFY(!t.id.isEmpty());
    const QString idPrice = saved[0].id, idPilot = saved[1].id, idDue = saved[2].id;

    // Reorder + edit: the order and content persist (ids stay).
    saved[0].title = QStringLiteral("Árazás és csomagok");
    saved = m_app->setMeetingTopics(m.id, {saved[2], saved[0], saved[1]});
    QVector<SummaryTopic> onDisk = m_app->meetingTopics(m.id);
    QCOMPARE(onDisk.size(), 3);
    QCOMPARE(onDisk[0].id, idDue);
    QCOMPARE(onDisk[1].id, idPrice);
    QCOMPARE(onDisk[1].title, QStringLiteral("Árazás és csomagok"));
    QCOMPARE(onDisk[2].id, idPilot);

    // State is queryable any time: nothing has run yet, so all "waiting".
    QVector<TopicStatus> sts = m_app->topicStatuses(m.id);
    QCOMPARE(sts.size(), 3);
    for (const TopicStatus& s : sts) QCOMPARE(s.state, TopicState::Waiting);

    // Analysis: topic 1 succeeds, 2 fails (HTTP 500), 3 succeeds.
    int call = 0;
    m_http->handler = [&call](const FakeRequest& r) -> FakeReply {
        if (!r.path.endsWith("/chat/completions")) return {404, "{}"};
        ++call;
        if (call == 2) return {500, R"({"error":{"message":"overloaded","code":"overloaded"}})"};
        return {200, chat(QStringLiteral("Részletes elemzés %1.\n\n## Döntések\n- Döntés %1\n\n## Teendők\n- Teendő %1 — Ödön (péntek)\n").arg(call))};
    };
    QSignalSpy queueDone(m_app.get(), &app::AppController::topicAnalysisQueueFinished);
    QSignalSpy statusChanged(m_app.get(), &app::AppController::topicStatusChanged);
    QSignalSpy finished(m_app->jobs(), &library::MeetingJobTracker::jobFinished);
    m_app->generateComplexSummary(m.id, onDisk);
    // Right after starting: the first runs, the others queue; chunk progress on the job.
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
    // A failed topic: no automatic summary; the step is failed, the finished analyses exist.
    QVERIFY(!m_app->store()->load(m.id).hasSummary);
    QCOMPARE(m_app->processingState(m.id).summaryState, StepState::Failed);
    QCOMPARE(m_app->topicAnalyses(m.id).size(), 2);

    // After a restart too: the topic states (with the error message) come from disk.
    newApp();
    sts = m_app->topicStatuses(m.id);
    QCOMPARE(sts[0].state, TopicState::Done);
    QCOMPARE(sts[1].state, TopicState::Failed);
    QCOMPARE(sts[1].errorDetail, QStringLiteral("HTTP 500 · overloaded · overloaded"));
    QCOMPARE(sts[2].state, TopicState::Done);

    // Re-running the failed topic: done; then the closing summary.
    call = 10;
    QSignalSpy oneReady(m_app.get(), &app::AppController::topicAnalysisReady);
    m_app->analyzeTopic(m.id, onDisk[1]);
    QCOMPARE(m_app->topicStatuses(m.id)[1].state, TopicState::Running);
    QVERIFY(oneReady.wait(10000));
    QTRY_VERIFY(!m_app->jobs()->isBusy(m.id));
    sts = m_app->topicStatuses(m.id);
    for (const TopicStatus& s : sts) QCOMPARE(s.state, TopicState::Done);
    QVERIFY(!m_app->processingState(m.id).summaryError.isValid());

    m_http->handler = [](const FakeRequest&) -> FakeReply { return {200, chat(QStringLiteral("A megbeszélés röviden: minden rendben."))}; };
    QSignalSpy summaryReady(m_app.get(), &app::AppController::summaryReady);
    m_app->finalizeComplexSummary(m.id);
    QVERIFY(m_app->jobs()->isRunning(m.id, JobKind::Summarize));
    QVERIFY(summaryReady.wait(10000));
    const library::SummaryDocument doc = m_app->summaryDocument(m.id);
    QVERIFY(doc.exists);
    QCOMPARE(doc.meta.mode, library::SummaryMode::Topics);
    QCOMPARE(doc.meta.model, QStringLiteral("teszt-modell"));
    QCOMPARE(doc.summary.execSummary, QStringLiteral("A megbeszélés röviden: minden rendben."));
    QCOMPARE(doc.topics.size(), 3);
    QCOMPARE(doc.topics[0].topicId, idDue);                   // in the edited ORDER
    QCOMPARE(doc.topics[1].topicId, idPrice);
    QCOMPARE(doc.topics[2].topicId, idPilot);
    QCOMPARE(doc.summary.actionItems.size(), 3);
    QCOMPARE(doc.summary.decisions.size(), 3);
    QCOMPARE(m_app->processingState(m.id).summaryState, StepState::Done);

    // Delete: the topic disappears from the list and from the states.
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
    // The first analysis succeeds, the second "hangs".
    int call = 0;
    m_http->handler = [&call](const FakeRequest& r) -> FakeReply {
        if (!r.path.endsWith("/chat/completions")) return {404, "{}"};   // no native API
        if (++call == 1) return {200, chat(QStringLiteral("Elemzés.\n\n## Döntések\n- d\n"))};
        FakeReply h; h.hold = true; return h;
    };
    QSignalSpy oneReady(m_app.get(), &app::AppController::topicAnalysisReady);
    QSignalSpy finished(m_app->jobs(), &library::MeetingJobTracker::jobFinished);
    QSignalSpy summaryReady(m_app.get(), &app::AppController::summaryReady);
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
    // The finished analysis stays, the rest "waiting" (not failed); no summary, no error.
    sts = m_app->topicStatuses(m.id);
    QCOMPARE(sts[0].state, TopicState::Done);
    QCOMPARE(sts[1].state, TopicState::Waiting);
    QCOMPARE(sts[2].state, TopicState::Waiting);
    QCOMPARE(m_app->topicAnalyses(m.id).size(), 1);
    QCOMPARE(summaryReady.count(), 0);
    QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));
    QVERIFY(!m_app->jobs()->isBusy(m.id));
    QVERIFY(!m_app->processingState(m.id).summaryError.isValid());
    QCOMPARE(m_http->count("POST", "/chat/completions"), 2);  // the 3rd topic did not even start

    // Resume: only the missing two run (the finished one does not run again).
    call = 0;
    m_http->handler = [](const FakeRequest&) -> FakeReply { return {200, chat(QStringLiteral("Elemzés.\n\n## Döntések\n- d\n"))}; };
    m_app->generateComplexSummary(m.id, topics);
    QCOMPARE(m_app->jobs()->job(m.id, JobKind::AnalyzeTopics).total, 2);
    QVERIFY(summaryReady.wait(15000));                        // analyses + automatic summary
    QCOMPARE(m_app->topicAnalyses(m.id).size(), 3);
    QCOMPARE(m_app->summaryDocument(m.id).meta.mode, library::SummaryMode::Topics);
}

void AppJobsTest::renamedDuringTranscriptionSurvives()
{
    if (!m_ffmpeg) QSKIP("ffmpeg nem található");
    m_http->handler = [](const FakeRequest& r) { return sonioxOk(r); };
    const Meeting m = recording("Eredeti cím", 2);
    QSignalSpy ready(m_app.get(), &app::AppController::transcriptReady);
    m_app->transcribeMeeting(m.id);
    // The app stays usable meanwhile: the user renames the meeting while the transcription runs.
    QTRY_VERIFY_WITH_TIMEOUT(m_http->count("POST", "/files") == 1, 30000);
    m_app->renameMeeting(m.id, "Átnevezve közben");
    QVERIFY(ready.wait(30000));
    const Meeting after = m_app->store()->load(m.id);
    QCOMPARE(after.title, QStringLiteral("Átnevezve közben"));   // the end of the transcription did not overwrite it
    QVERIFY(after.hasTranscript);
}

void AppJobsTest::identifyRunsAsLastStage()
{
    // Runs only with a real voice model: TANARA_TEST_VOICE_MODEL=<.../campplus_sv_zh_en_16k.onnx>.
    const QString model = qEnvironmentVariable("TANARA_TEST_VOICE_MODEL");
    if (model.isEmpty() || !QFile::exists(model))
        QSKIP("TANARA_TEST_VOICE_MODEL nincs beállítva — az azonosítás-szakasz tesztje kimarad");
    if (!m_ffmpeg) QSKIP("ffmpeg nem található");
    QDir().mkpath(m_home->filePath("models"));
    QVERIFY(QFile::link(model, m_home->filePath("models/campplus_sv_zh_en_16k.onnx")));

    m_http->handler = [](const FakeRequest& r) { return sonioxOk(r); };
    const Meeting m = recording("Azonosítással", 5);
    library::MeetingJobTracker* jobs = m_app->jobs();
    QVector<StageState> identify;
    QStringList details;
    connect(jobs, &library::MeetingJobTracker::jobProgressChanged, this, [&](const QString&, const JobProgress& p) {
        if (const JobStage* st = p.stage("identify")) {
            if (identify.isEmpty() || identify.last() != st->state) identify.append(st->state);
            if (!st->detail.isEmpty() && !details.contains(st->detail)) details << st->detail;
        }
    });
    QSignalSpy ready(m_app.get(), &app::AppController::transcriptReady);
    QSignalSpy finished(jobs, &library::MeetingJobTracker::jobFinished);
    m_app->transcribeMeeting(m.id);
    if (!jobs->job(m.id, JobKind::Transcribe).stage("identify"))
        QSKIP("a build nem tartalmaz voice-ID-t");
    QVERIFY(ready.wait(30000));
    // The transcript is done, but the job is still running: the last stage is identification (on a background thread).
    QCOMPARE(finished.count(), 0);
    MeetingProcessingState st = m_app->processingState(m.id);
    QCOMPARE(st.identifyState, StepState::Running);
    QVERIFY(m_app->store()->load(m.id).hasTranscript);
    QVERIFY(finished.wait(60000));
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Done);
    QCOMPARE(identify, QVector<StageState>({StageState::Waiting, StageState::Running, StageState::Done}));
    QVERIFY(details.contains(QStringLiteral("0 / 2 beszélő")));
    QVERIFY(details.contains(QStringLiteral("2 / 2 beszélő")));   // real chunk progress
    st = m_app->processingState(m.id);
    QCOMPARE(st.transcriptState, StepState::Done);
    QCOMPARE(st.identifyState, StepState::Done);
    QVERIFY(m_app->jobs()->identifiedAt(m.id).isValid());
    QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));

    // Cancelling a standalone (re)identification: no error, the job ends "canceled".
    QVERIFY(m_app->identifyMeetingAsync(m.id));
    QVERIFY(!m_app->identifyMeetingAsync(m.id));              // already running
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
    m.speakerMap.insert("speaker:1", pid("Ödön"));
    m_app->store()->saveMeeting(m);
    editing::SpeakerEditSession* ed = m_app->editSession(m.id);
    QVERIFY(ed);
    QVERIFY(!ed->moveUtterancesToPerson({ed->utteranceAt(1).id}, choice("Kovács Lilla")).isEmpty());
    QCOMPARE(m_app->retranscribeImpact(m.id).correctedUtterances, 1);

    QSignalSpy ready(m_app.get(), &app::AppController::transcriptReady);
    m_app->retranscribeMeeting(m.id, /*keepBackup*/ true);
    // While it runs, the old names and fixes live (they were not deleted in advance).
    QCOMPARE(nameOf(m_app->store()->load(m.id).speakerMap.value("speaker:1")), QStringLiteral("Ödön"));
    QCOMPARE(m_app->retranscribeImpact(m.id).correctedUtterances, 1);
    QVERIFY(ready.wait(30000));
    QTRY_VERIFY(!m_app->jobs()->isBusy(m.id));

    // Success: with the new transcript the names AND the per-line fixes were deleted too.
    QVERIFY(m_app->store()->load(m.id).speakerMap.isEmpty());
    const editing::RetranscribeImpact impact = m_app->retranscribeImpact(m.id);
    QCOMPARE(impact.correctedUtterances, 0);
    QCOMPARE(impact.namedSpeakers, 0);
    QCOMPARE(impact.addedParticipants, 0);
    // The open editor shows the NEW transcript (the mock gives two utterances, with two speakers).
    QCOMPARE(ed->utteranceCount(), 2);
    QVERIFY(!ed->utteranceAt(0).manuallyCorrected && !ed->utteranceAt(1).manuallyCorrected);
    // The requested copy exists, with the old transcript and the fixes in it.
    const QStringList backups = QDir(m.folder).entryList({"transcript-backup-*"}, QDir::Dirs);
    QCOMPARE(backups.size(), 1);
    QVERIFY(QDir(QDir(m.folder).filePath(backups.first())).exists("transcript.tokens.json"));
    QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));

    // Deleting with an open editor: the editor closes too (a new request finds no meeting, so null).
    m_app->deleteMeeting(m.id);
    QVERIFY(m_app->editSession(m.id) == nullptr);
    QVERIFY(!QDir(m.folder).exists());
}

void AppJobsTest::manualParticipantSurvivesTranscription()
{
    if (!m_ffmpeg) QSKIP("ffmpeg nem található");
    m_http->handler = [](const FakeRequest& r) { return sonioxOk(r); };
    const Meeting m = recording("Kézi résztvevő", 3);
    const QString cili = m_app->addParticipant(m.id, choice(QStringLiteral("Cecília")));
    QVERIFY(!cili.isEmpty());
    QVERIFY(m_app->unheardParticipants(m.id).isEmpty());   // no "not heard" without a transcript

    QSignalSpy ready(m_app.get(), &app::AppController::transcriptReady);
    m_app->transcribeMeeting(m.id);
    QVERIFY(ready.wait(30000));
    QTRY_VERIFY(!m_app->jobs()->isBusy(m.id));

    auto findCili = [&](const QVector<Participant>& ps) -> const Participant* {
        for (const Participant& p : ps)
            if (p.id == cili) return &p;
        return nullptr;
    };
    QVector<Participant> ps = m_app->participants(m.id);
    const Participant* p = findCili(ps);
    QVERIFY(p);
    QCOMPARE(nameOf(p->personId), QStringLiteral("Cecília"));
    QVERIFY(p->rawSpeakerIds.isEmpty());
    QCOMPARE(p->source, ParticipantSource::Manual);
    QVector<Participant> unheard = m_app->unheardParticipants(m.id);
    QCOMPARE(unheard.size(), 1);
    QCOMPARE(unheard.first().id, cili);

    // Once a speaker is assigned its name, it is no longer "not heard".
    m_app->renameSpeaker(m.id, QStringLiteral("speaker:1"), choice(QStringLiteral("Cecília")), /*enroll*/ false);
    QVERIFY(m_app->unheardParticipants(m.id).isEmpty());

    // It survives a re-transcription too (the names are deleted, the participant is not).
    ready.clear();
    m_app->retranscribeMeeting(m.id);
    QVERIFY(ready.wait(30000));
    QTRY_VERIFY(!m_app->jobs()->isBusy(m.id));
    ps = m_app->participants(m.id);
    QVERIFY(findCili(ps));
    QCOMPARE(m_app->unheardParticipants(m.id).size(), 1);
    QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join("; ")));
}

void AppJobsTest::staleSummaryShowsInStateAndPending()
{
    const QString json = QStringLiteral(R"({"execSummary":"Rövid.","decisions":[],"actionItems":[],"participants":[]})");
    m_http->handler = [json](const FakeRequest&) -> FakeReply { return {200, chat(json)}; };
    const Meeting m = transcribed("Elavuló összefoglaló");
    QSignalSpy ready(m_app.get(), &app::AppController::summaryReady);
    m_app->summarizeMeeting(m.id);
    QVERIFY(ready.wait(10000));
    MeetingProcessingState st = m_app->processingState(m.id);
    QCOMPARE(st.summaryState, StepState::Done);
    QVERIFY(!st.summaryStale);
    QVERIFY(m_app->library()->pendingItems().isEmpty());

    // A speaker change after the summary makes it stale: state, library entry, "waiting for you".
    QSignalSpy stateChanged(m_app->jobs(), &library::MeetingJobTracker::stateChanged);
    QSignalSpy pendingChanged(m_app->library(), &library::MeetingLibrary::pendingItemsChanged);
    QSignalSpy entryChanged(m_app->library(), &library::MeetingLibrary::meetingChanged);
    m_app->renameSpeaker(m.id, "speaker:1", choice("Ödön"), /*enroll*/ false);
    QVERIFY(stateChanged.count() >= 1);
    QVERIFY(pendingChanged.count() >= 1);
    QVERIFY(entryChanged.count() >= 1);
    st = m_app->processingState(m.id);
    QCOMPARE(st.summaryState, StepState::Done);              // exists, just stale
    QVERIFY(st.summaryStale);
    QCOMPARE(st.staleCorrectedSpeakers, 1);
    QVERIFY(m_app->library()->entry(m.id).state.summaryStale);
    QVector<library::PendingItem> pending = m_app->library()->pendingItems();
    QCOMPARE(pending.size(), 1);
    QCOMPARE(pending[0].kind, library::PendingKind::StaleSummary);
    QCOMPARE(pending[0].meetingId, m.id);
    QCOMPARE(pending[0].correctedSpeakers, 1);

    // "Fine as is": it disappears.
    const int before = pendingChanged.count();
    m_app->dismissSummaryStale(m.id);
    QVERIFY(pendingChanged.count() > before);
    QVERIFY(!m_app->processingState(m.id).summaryStale);
    QVERIFY(m_app->library()->pendingItems().isEmpty());

    // Stale again, then regenerating the summary clears it.
    m_app->renameSpeaker(m.id, "speaker:1", choice("Lilla"), false);
    QVERIFY(m_app->processingState(m.id).summaryStale);
    m_app->summarizeMeeting(m.id);
    QVERIFY(ready.wait(10000));
    QVERIFY(!m_app->processingState(m.id).summaryStale);
    QVERIFY(m_app->library()->pendingItems().isEmpty());
}

// ---- data safety ----

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
    m.speakerMap.insert("speaker:1", pid("Ödön"));
    m_app->store()->saveMeeting(m);
    library::MeetingJobTracker* jobs = m_app->jobs();

    // 1) The re-transcription fails at the provider: the old transcript stays with its names.
    m_http->handler = [](const FakeRequest& r) -> FakeReply {
        if (r.method == "POST" && r.path.endsWith("/files")) return {500, R"({"message":"hiba"})"};
        return {200, "{}"};
    };
    QSignalSpy finished(jobs, &library::MeetingJobTracker::jobFinished);
    m_app->retranscribeMeeting(m.id);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Failed);
    QCOMPARE(nameOf(m_app->store()->load(m.id).speakerMap.value("speaker:1")), QStringLiteral("Ödön"));

    // 2) Retry with a PLAIN transcription (the error card's "Retry" button): the new diarization's
    //    "Speaker 1" may be a different person, so the old name must NOT stay on it.
    m_http->handler = [](const FakeRequest& r) { return sonioxOk(r); };
    QSignalSpy ready(m_app.get(), &app::AppController::transcriptReady);
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
    // The provider finishes "successfully" but returns not a single word.
    m_http->handler = [](const FakeRequest& r) -> FakeReply {
        if (r.method == "GET" && r.path.endsWith("/transcript")) return {200, R"({"tokens":[]})"};
        return sonioxOk(r);
    };
    Meeting m = transcribed("Üres eredmény");
    m.speakerMap.insert("speaker:1", pid("Ödön"));
    m_app->store()->saveMeeting(m);
    {
        editing::SpeakerEditSession* ed = m_app->editSession(m.id);
        QVERIFY(ed);
        QVERIFY(!ed->moveUtterancesToPerson({ed->utteranceAt(1).id}, choice("Kovács Lilla")).isEmpty());
        m_app->closeEditSession(m.id);
    }
    const QString tokensPath = QDir(m.folder).filePath("transcript.tokens.json");
    const QString segmentsPath = QDir(m.folder).filePath("transcript.segments.json");
    const QByteArray tokensBefore = fileBytes(tokensPath);
    const QByteArray segmentsBefore = fileBytes(segmentsPath);
    const QByteArray overlayBefore = fileBytes(editing::speakeredit::overlayPath(m.folder));
    QVERIFY(!tokensBefore.isEmpty() && !overlayBefore.isEmpty());

    library::MeetingJobTracker* jobs = m_app->jobs();
    QSignalSpy finished(jobs, &library::MeetingJobTracker::jobFinished);
    QSignalSpy ready(m_app.get(), &app::AppController::transcriptReady);
    m_app->retranscribeMeeting(m.id);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Failed);
    QCOMPARE(ready.count(), 0);

    // A persistent, understandable error; the old transcript, names and manual fixes are untouched.
    const MeetingProcessingState st = m_app->processingState(m.id);
    QVERIFY(st.transcriptError.isValid());
    QVERIFY2(st.transcriptError.message.contains("üres eredményt"), qPrintable(st.transcriptError.message));
    QCOMPARE(m_errors.size(), 1);
    QCOMPARE(fileBytes(tokensPath), tokensBefore);
    QCOMPARE(fileBytes(segmentsPath), segmentsBefore);
    QCOMPARE(fileBytes(editing::speakeredit::overlayPath(m.folder)), overlayBefore);
    const Meeting after = m_app->store()->load(m.id);
    QVERIFY(after.hasTranscript);
    QCOMPARE(nameOf(after.speakerMap.value("speaker:1")), QStringLiteral("Ödön"));
    QCOMPARE(m_app->retranscribeImpact(m.id).correctedUtterances, 1);

    // On a first transcription too it is an error (not "done, empty transcript"): no transcript file is created.
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
    sys.file = "track_sys.wav"; sys.active = true;            // the file is NOT on disk
    m.tracks.append(sys);
    // There is a complete mix (made from both tracks) that is marked stale.
    const QByteArray oldMix = "TELJES-KEVEREK";
    const QString mixPath = QDir(m.folder).filePath("mixdown.mp3");
    { QFile f(mixPath); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(oldMix); }
    m.mixdownFile = "mixdown.mp3";
    m.mixdownDirty = true;
    m_app->store()->saveMeeting(m);
    const QString sysName = m_app->tracks()->tracks(m.id).at(1).displayName;
    QVERIFY(!sysName.isEmpty());

    // Manual remix: it does not start, the error names the track, the old mix stays.
    QSignalSpy mixDone(m_app.get(), &app::AppController::mixdownUpdated);
    m_app->regenerateMixdown(m.id);
    QCOMPARE(mixDone.count(), 1);
    QCOMPARE(mixDone.at(0).at(1).toBool(), false);
    QCOMPARE(m_errors.size(), 1);
    QVERIFY2(m_errors.first().contains(sysName), qPrintable(m_errors.first()));
    QCOMPARE(fileBytes(mixPath), oldMix);
    QVERIFY(!m_app->jobs()->isBusy(m.id));

    // Automatic remix before transcription: the same, the transcription does not start, nothing
    // goes to the provider, the persistent error names the track.
    QSignalSpy finished(m_app->jobs(), &library::MeetingJobTracker::jobFinished);
    m_app->transcribeMeeting(m.id);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Failed);
    QVERIFY(m_app->processingState(m.id).transcriptError.detail.contains(sysName));
    QCOMPARE(fileBytes(mixPath), oldMix);
    QTest::qWait(200);
    QVERIFY(m_http->log.isEmpty());

    // After dropping the track the mixdown runs and replaces the old file in place (no
    // half-finished or set-aside file remains).
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
    // Fake model file: identification starts (background thread) but gives no embedding; the test
    // looks at the orchestration (stopping / discarding), not the recognition. No real model needed.
    QDir().mkpath(m_home->filePath("models"));
    { QFile f(m_home->filePath("models/campplus_sv_zh_en_16k.onnx")); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("nem-onnx"); }
    if (!m_app->voiceIdentificationAvailable()) QSKIP("a build nem tartalmaz voice-ID-t");

    Meeting m = transcribed("Azonosítás közben");
    { QFile f(QDir(m.folder).filePath("mixdown.mp3")); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("KEVEREK"); }
    m.mixdownFile = "mixdown.mp3";
    m.mixdownDirty = false;
    m_app->store()->saveMeeting(m);
    library::MeetingJobTracker* jobs = m_app->jobs();
    QSignalSpy finished(jobs, &library::MeetingJobTracker::jobFinished);

    // Control: with an unchanged transcript the standalone identification ends cleanly.
    QVERIFY(m_app->identifyMeetingAsync(m.id));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
    QCOMPARE(finished.at(0).at(1).value<JobKind>(), JobKind::Identify);
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Done);
    QVERIFY(jobs->identifiedAt(m.id).isValid());
    jobs->markIdentified(m.id, false);

    // 1) During identification the transcript changes on disk, so the result belongs to the OLD
    //    transcript's labels: we discard it, the meeting does not become "identified".
    QVERIFY(m_app->identifyMeetingAsync(m.id));
    {
        QFile f(QDir(m.folder).filePath("transcript.tokens.json"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(R"({"language":"hu","tokens":[{"text":"Más","speaker":"speaker:1","startMs":0,"endMs":200,"confidence":0.9,"trackId":"mixdown"}]})");
    }
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 30000);
    QCOMPARE(finished.at(1).at(1).value<JobKind>(), JobKind::Identify);
    QCOMPARE(finished.at(1).at(2).value<JobOutcome>(), JobOutcome::Cancelled);
    QVERIFY(!jobs->identifiedAt(m.id).isValid());

    // 2) Starting transcription stops a running standalone identification.
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
