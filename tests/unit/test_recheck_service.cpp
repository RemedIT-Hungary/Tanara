// RecheckService on its own: the line embeddings come from a FAKE voice-embedding
// provider (providers::IVoiceEmbedder; no model, no audio, no ffmpeg), then the recheck from the confirmed
// lines and the pairwise review run on a SpeakerEditSession. In a temporary folder with made-up data.
//
// The scenario (as in test_speaker_recheck): under "Speaker 1" (Anna) 3 lines are really Béla's noisy voice
// (P). Confirming 3 of Anna's real lines, the recheck flags exactly these 3 lines with Béla as the suggestion.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/domain/Meeting.h"
#include "tanara/editing/recheck/FactoryVoiceEmbedder.h"
#include "tanara/editing/recheck/RecheckService.h"
#include "tanara/editing/session/SpeakerEditSession.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/library/store/UtteranceEmbeddingCache.h"
#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/providers/core/CapabilityJob.h"
#include "tanara/providers/voice/IVoiceEmbedder.h"

#include <memory>

using namespace tanara;

namespace {

const QString kB1 = QStringLiteral("Beszélő 1");
const QString kB2 = QStringLiteral("Beszélő 2");

const QVector<float> kA{1.0f, 0.0f, 0.0f};
const QVector<float> kB{0.0f, 1.0f, 0.0f};
const QVector<float> kP{0.6f, 0.8f, 0.0f};

struct Row { qint64 startMs; qint64 durMs; QString raw; char voice; };

const QVector<Row>& scenario()
{
    static const QVector<Row> rows{
        {0,     4000, kB1, 'A'},    // 0
        {5000,  4000, kB2, 'B'},    // 1
        {10000, 4000, kB1, 'A'},    // 2
        {15000, 4000, kB1, 'P'},    // 3  really Béla
        {20000, 4000, kB2, 'B'},    // 4
        {25000, 4000, kB1, 'A'},    // 5
        {30000, 4000, kB1, 'P'},    // 6  really Béla
        {35000, 4000, kB2, 'B'},    // 7
        {40000, 4000, kB1, 'A'},    // 8
        {45000, 4000, kB1, 'P'},    // 9  really Béla
        {50000, 4000, kB2, 'B'},    // 10
        {55000, 4000, kB2, 'B'},    // 11 Anna interjects, so overlap
        {56500, 1200, kB1, '-'},    // 12 the interjection (too short to embed)
        {60000, 4000, kB2, 'B'},    // 13
    };
    return rows;
}

QString uid(int row) { return QStringLiteral("u%1").arg(scenario()[row].startMs); }
QStringList uids(std::initializer_list<int> rows)
{
    QStringList out;
    for (int r : rows) out << uid(r);
    return out;
}

QVector<float> voiceAt(qint64 midMs)
{
    for (const Row& r : scenario()) {
        if (midMs < r.startMs || midMs > r.startMs + r.durMs) continue;
        switch (r.voice) {
        case 'A': return kA;
        case 'B': return kB;
        case 'P': return kP;
        default:  return {};
        }
    }
    return {};
}

// The fake provider: a clip's vector says whose voice its middle is. Every request is logged; fail=true
// makes every job fail.
class FakeVoiceEmbedder : public QObject, public providers::IVoiceEmbedder {
public:
    QVector<providers::VoiceEmbeddingRequest> requests;
    bool fail = false;

    QString name() const override { return QStringLiteral("fake"); }
    QStringList defaultModelIds() const override { return {QStringLiteral("campplus")}; }
    providers::VoiceEmbeddingJob* embed(const providers::VoiceEmbeddingRequest& req) override
    {
        requests.append(req);
        const bool failing = fail;
        return new providers::FunctionJob<QVector<providers::VoiceVector>>(
            [req, failing](QVector<providers::VoiceVector>* out, Error* e) {
                if (failing) {
                    e->kind = Error::Kind::Service;
                    e->message = QStringLiteral("no voice model");
                    return false;
                }
                const QStringList models = req.modelIds.isEmpty() ? QStringList{QStringLiteral("campplus")}
                                                                  : req.modelIds;
                for (int i = 0; i < req.clips.size(); ++i) {
                    const providers::AudioClipRef& c = req.clips.at(i);
                    for (const QString& m : models) {
                        providers::VoiceVector v;
                        v.vector = voiceAt((c.startMs + c.endMs) / 2);
                        v.dim = int(v.vector.size());
                        v.modelId = m;
                        v.input = i;
                        out->append(v);
                    }
                }
                return true;
            },
            this);
    }
};

// A synchronous utterance embedder for the FactoryVoiceEmbedder path.
class FakeUtteranceEmbedder : public providers::IUtteranceEmbedder {
public:
    explicit FakeUtteranceEmbedder(std::shared_ptr<int> opens) : m_opens(std::move(opens)) {}
    bool open(const QString&) override { ++*m_opens; return true; }
    QVector<float> embed(qint64 startMs, qint64 endMs) override { return voiceAt((startMs + endMs) / 2); }
private:
    std::shared_ptr<int> m_opens;
};

struct Fixture {
    QTemporaryDir dir;
    std::unique_ptr<library::MeetingRepository> store;
    std::unique_ptr<library::PeopleStore> people;
    std::unique_ptr<library::VoiceprintStore> prints;
    Meeting meeting;

    Fixture()
    {
        store = std::make_unique<library::MeetingRepository>(dir.filePath(QStringLiteral("rec")),
                                                             dir.filePath(QStringLiteral("meta")));
        people = std::make_unique<library::PeopleStore>(dir.filePath(QStringLiteral("meta/people.json")));
        prints = std::make_unique<library::VoiceprintStore>(dir.filePath(QStringLiteral("meta/vp.json")));
        meeting = store->createMeeting(QStringLiteral("Újraellenőrzés"));
        QJsonArray segs;
        int i = 0;
        for (const Row& r : scenario())
            segs.append(QJsonObject{{"startMs", double(r.startMs)}, {"endMs", double(r.startMs + r.durMs)},
                                    {"speaker", r.raw}, {"text", QStringLiteral("Sor %1").arg(i++)}});
        QFile f(library::TranscriptRepository::segmentsPath(meeting.folder));
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(segs).toJson());
        f.close();
        meeting.hasTranscript = true;
        meeting.speakerMap = {{kB1, people->ensure(QStringLiteral("Anna"))}, {kB2, people->ensure(QStringLiteral("Béla"))}};
        store->saveMeeting(meeting);
    }

    std::unique_ptr<editing::SpeakerEditSession> session()
    {
        return std::make_unique<editing::SpeakerEditSession>(store.get(), people.get(), prints.get(), meeting.id);
    }
};

bool runEmbedding(editing::RecheckService& svc, bool* complete = nullptr)
{
    QSignalSpy finished(&svc, &editing::RecheckService::embeddingFinished);
    svc.startEmbedding();
    if (finished.isEmpty() && !finished.wait(10000)) return false;
    if (complete) *complete = finished.last().at(0).toBool();
    return true;
}

} // namespace

class RecheckServiceTest : public QObject {
    Q_OBJECT
private slots:
    void embedding_throughTheVoiceEmbedderPort()
    {
        Fixture fx;
        auto session = fx.session();
        FakeVoiceEmbedder fake;
        editing::RecheckService svc(session.get());
        QVERIFY(!svc.embeddingsSupported());
        svc.setVoiceEmbedder(&fake);
        QVERIFY(svc.embeddingsSupported());
        QVERIFY(!svc.embeddingsComplete());
        QVERIFY(!session->hasEmbeddings());

        QSignalSpy progress(&svc, &editing::RecheckService::embeddingProgress);
        QSignalSpy running(&svc, &editing::RecheckService::embeddingRunningChanged);
        bool complete = false;
        QVERIFY(runEmbedding(svc, &complete));
        QVERIFY(complete);
        QVERIFY(!svc.isEmbeddingRunning());
        QVERIFY(svc.embeddingError().isEmpty());
        QCOMPARE(running.count(), 2);
        QCOMPARE(progress.first().at(0).toInt(), 0);
        QCOMPARE(progress.last().at(0).toInt(), 13);     // every line but the short interjection
        QCOMPARE(progress.last().at(1).toInt(), 13);

        // The requests: clips of the meeting's mixdown, at most 16 per job, with the session's models.
        int clips = 0;
        for (const providers::VoiceEmbeddingRequest& r : std::as_const(fake.requests)) {
            QVERIFY(r.clips.size() <= 16);
            QCOMPARE(r.modelIds, QStringList{QStringLiteral("campplus")});
            for (const providers::AudioClipRef& c : r.clips) QCOMPARE(c.path, session->audioPath());
            clips += int(r.clips.size());
        }
        QCOMPARE(clips, 13);

        QVERIFY(session->hasEmbeddings());
        QVERIFY(svc.embeddingsComplete());
        QCOMPARE(session->uncertainCount(), 0);          // the regular verdict does not see the P lines
        // The vectors are in the meeting folder's cache.
        const auto cache = library::UtteranceEmbeddingCache::load(
            fx.meeting.folder, library::TranscriptRepository::linesFingerprint(
                                   library::TranscriptRepository::readLines(fx.meeting.folder)));
        QCOMPARE(cache.vectors(QStringLiteral("campplus")).value(uid(3)), kP);

        // Nothing is missing: a second run asks nothing.
        fake.requests.clear();
        QVERIFY(runEmbedding(svc, &complete));
        QVERIFY(complete);
        QVERIFY(fake.requests.isEmpty());
    }

    void recheck_flagsExactlyTheMislabeledLines_oneUndoStep()
    {
        Fixture fx;
        auto session = fx.session();
        FakeVoiceEmbedder fake;
        editing::RecheckService svc(session.get());
        svc.setVoiceEmbedder(&fake);
        svc.setModelNameResolver([](const QString& id) { return id == QStringLiteral("campplus") ? QStringLiteral("CAM++") : QString(); });
        QVERIFY(runEmbedding(svc));

        QVERIFY(!svc.canRecheck());
        QVERIFY(svc.recheckBlocker().contains(QStringLiteral("Előbb erősíts meg")));
        QVERIFY(!svc.recheckFromConfirmed().ran);

        QVERIFY(session->confirmUtterances(uids({0, 2, 5})));
        QVERIFY(svc.canRecheck());
        QVERIFY(svc.recheckBlocker().isEmpty());

        QSignalSpy finished(&svc, &editing::RecheckService::recheckFinished);
        const editing::RecheckService::RecheckResult r = svc.recheckFromConfirmed();
        QVERIFY(r.ran);
        QCOMPARE(r.flagged, 3);
        QCOMPARE(r.speakersWithConfirmedCore, 1);
        QCOMPARE(r.confirmedLines, 3);
        QCOMPARE(r.models, QStringList{QStringLiteral("CAM++")});
        QVERIFY(r.basisSummary().contains(QStringLiteral("CAM++")));
        QCOMPARE(finished.count(), 1);
        QCOMPARE(session->uncertainUtteranceIds(), uids({3, 6, 9}));
        for (int row : {3, 6, 9}) {
            const editing::EditorUtterance u = session->utterance(uid(row));
            QVERIFY(u.rechecked);
            QCOMPARE(u.likelySpeakerKey, kB2);
        }
        QCOMPARE(session->undoText(), QStringLiteral("Beszélők újraellenőrzése"));

        // A recheck that changes nothing adds no step.
        const QVector<quint64> serials = session->undoSerials();
        QCOMPARE(svc.recheckFromConfirmed().flagged, 3);
        QCOMPARE(session->undoSerials(), serials);

        // The flags are one undo step of the session.
        session->undo();
        QCOMPARE(session->uncertainCount(), 0);
        session->redo();
        QCOMPARE(session->uncertainUtteranceIds(), uids({3, 6, 9}));
    }

    void pairRecheck_onlyThePairsLines()
    {
        Fixture fx;
        auto session = fx.session();
        FakeVoiceEmbedder fake;
        editing::RecheckService svc(session.get());
        svc.setVoiceEmbedder(&fake);
        QVERIFY(!svc.pairRecheckBlocker(kB1, kB1).isEmpty());
        QVERIFY(svc.pairRecheckBlocker(kB1, kB2).contains(QStringLiteral("még nem készült el")));
        QVERIFY(runEmbedding(svc));
        QVERIFY(session->confirmUtterances(uids({0, 2, 5, 1, 4, 7})));
        QVERIFY(svc.pairRecheckBlocker(kB1, kB2).isEmpty());

        const editing::RecheckService::PairRecheckResult r = svc.recheckPair(kB1, kB2);
        QVERIFY(r.ran);
        QCOMPARE(r.refLinesA, 3);
        QCOMPARE(r.refLinesB, 3);
        QVERIFY(r.flagged > 0);
        for (int row : {3, 6, 9}) QCOMPARE(session->utterance(uid(row)).likelySpeakerKey, kB2);
        QCOMPARE(session->undoText(), QStringLiteral("Átnézés: Anna és Béla"));
    }

    void embedding_failureAndCancel()
    {
        Fixture fx;
        auto session = fx.session();
        FakeVoiceEmbedder fake;
        fake.fail = true;
        editing::RecheckService svc(session.get());
        svc.setVoiceEmbedder(&fake);
        bool complete = true;
        QVERIFY(runEmbedding(svc, &complete));
        QVERIFY(!complete);
        QCOMPARE(svc.embeddingError(), QStringLiteral("no voice model"));
        QVERIFY(!session->hasEmbeddings());
        QVERIFY(!svc.recheckBlocker().isEmpty());

        // Cancelling right after the start: the run ends without a result, a new run completes.
        fake.fail = false;
        QSignalSpy finished(&svc, &editing::RecheckService::embeddingFinished);
        svc.startEmbedding();
        QVERIFY(svc.isEmbeddingRunning());
        svc.cancelEmbedding();
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.last().at(0).toBool(), false);
        QVERIFY(!svc.isEmbeddingRunning());
        QVERIFY(runEmbedding(svc, &complete));
        QVERIFY(complete);
        QVERIFY(svc.embeddingsComplete());
    }

    void embedding_reloadStopsTheRun_andTheSessionMayCloseFirst()
    {
        Fixture fx;
        auto session = fx.session();
        FakeVoiceEmbedder fake;
        auto svc = std::make_unique<editing::RecheckService>(session.get());
        svc->setVoiceEmbedder(&fake);
        QSignalSpy running(svc.get(), &editing::RecheckService::embeddingRunningChanged);
        QSignalSpy finished(svc.get(), &editing::RecheckService::embeddingFinished);
        svc->startEmbedding();
        QVERIFY(svc->isEmbeddingRunning());
        session->reloadTranscript();                    // a new transcript: the run's results are dropped
        QVERIFY(!svc->isEmbeddingRunning());
        QCOMPARE(running.count(), 2);
        QTest::qWait(50);
        QVERIFY(finished.isEmpty());                    // a stopped run does not report a finish
        QVERIFY(!session->hasEmbeddings());

        svc->startEmbedding();
        session.reset();                                // the session goes first: the service just stops
        QVERIFY(!svc->isEmbeddingRunning());
        QVERIFY(svc->session() == nullptr);
        QVERIFY(!svc->canRecheck());
        svc.reset();
    }

    void factoryPath_oneDecodePerRun()
    {
        Fixture fx;
        auto session = fx.session();
        auto opens = std::make_shared<int>(0);
        editing::RecheckService svc(session.get());
        svc.setEmbedderFactory([opens] { return std::make_unique<FakeUtteranceEmbedder>(opens); });
        QVERIFY(svc.embeddingsSupported());
        bool complete = false;
        QVERIFY(runEmbedding(svc, &complete));
        QVERIFY(complete);
        QCOMPARE(*opens, 1);                            // one audio decode for every batch of the run
        QVERIFY(svc.embeddingsComplete());
        QCOMPARE(session->utterance(uid(3)).uncertain, false);
    }

    void factoryVoiceEmbedder_rejectsPcmAndReportsOpenFailure()
    {
        editing::FactoryVoiceEmbedder none({});
        providers::VoiceEmbeddingRequest pcm;
        pcm.pcm = {QVector<float>(16000, 0.1f)};
        auto* j1 = none.embed(pcm);
        QSignalSpy failed1(j1, &providers::CapabilityJob::failed);
        QVERIFY(failed1.wait(2000));
        QCOMPARE(j1->error().code, QStringLiteral("clips_only"));

        providers::VoiceEmbeddingRequest clips;
        clips.clips = {providers::AudioClipRef{QStringLiteral("/nincs/ilyen.ogg"), 0, 3000}};
        auto* j2 = none.embed(clips);
        QSignalSpy failed2(j2, &providers::CapabilityJob::failed);
        QVERIFY(failed2.wait(2000));
        QCOMPARE(j2->error().code, QStringLiteral("open_failed"));
    }
};

QTEST_GUILESS_MAIN(RecheckServiceTest)
#include "test_recheck_service.moc"
