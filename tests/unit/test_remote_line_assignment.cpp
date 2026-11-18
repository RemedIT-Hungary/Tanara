// The remote-line assignment in the editor session (SpeakerEditSession::planRemoteAssignment /
// applyRemoteAssignment) on a synthetic meeting in a sandbox (TANARA_HOME): synthetic line vectors in the
// embedding cache, synthetic track activity, the pre-transcript analysis windows. No audio, no models.
// Checked: two voices under one raw speaker are split to their people, an unmatched voice becomes a new
// anonymous participant, a manual correction is never changed, nothing Remote goes to the local person, the
// assignment is stored as automatic (not corrected) and survives a reload, it is one undo step, a rerun changes
// nothing, and a single-track meeting is left unchanged.
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTemporaryDir>

#include <cmath>
#include <memory>

#include "tanara/editing/session/SpeakerEditSession.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/library/store/UtteranceEmbeddingCache.h"
#include "tanara/library/store/VoiceprintStore.h"

using namespace tanara;
using editing::SpeakerEditSession;

namespace {

constexpr int kDim = 16;
const QString kModel = QStringLiteral("campplus");

QVector<float> voice(int axis, quint32 seed)
{
    QRandomGenerator rng(seed * 7919u + quint32(axis) * 104729u + 3u);
    QVector<float> v(kDim, 0.0f);
    v[axis] = 1.0f;
    for (int k = 0; k < kDim; ++k) v[k] += float(0.25 * (rng.generateDouble() - 0.5));
    double n = 0.0;
    for (float x : v) n += double(x) * x;
    for (float& x : v) x = float(x / std::sqrt(n));
    return v;
}

struct Row {
    qint64 s, e;
    QString raw;
    int voice;      // axis of the voice
    bool local;     // under the line only the mic sounds (else only the loopback)
};

TrackActivity activity(const QString& id, TrackKind kind, qint64 totalMs, const QVector<Row>& rows, bool local)
{
    TrackActivity t;
    t.trackId = id;
    t.kind = kind;
    t.frameMs = 50;
    t.dbAboveFloor = QVector<float>(int(totalMs / 50), 0.0f);
    for (const Row& r : rows)
        if (r.local == local)
            for (qint64 ms = r.s; ms < r.e; ms += 50) t.dbAboveFloor[int(ms / 50)] = 30.0f;
    return t;
}

} // namespace

class RemoteLineAssignmentTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void assignsSplitsAndKeepsManualDecisions();
    void singleTrackIsLeftUnchanged();

private:
    void build();

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<library::MeetingRepository> m_store;
    std::unique_ptr<library::PeopleStore> m_people;
    std::unique_ptr<library::VoiceprintStore> m_prints;
    Meeting m_meeting;
    QVector<Row> m_rows;
    QVector<TranscriptLine> m_lines;
    PersonId m_a, m_b, m_me;
    MeetingActivity m_activity;
    QVector<VoiceCluster> m_clusters;
};

void RemoteLineAssignmentTest::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    qputenv("TANARA_HOME", m_home->path().toUtf8());
    const QDir home(m_home->path());
    QDir().mkpath(home.filePath("rec"));
    QDir().mkpath(home.filePath("meta"));
    m_store = std::make_unique<library::MeetingRepository>(home.filePath("rec"), home.filePath("meta"));
    m_people = std::make_unique<library::PeopleStore>(home.filePath("meta/people.json"));
    m_prints = std::make_unique<library::VoiceprintStore>(home.filePath("meta/voiceprints.json"));
    m_a = m_people->create(QStringLiteral("Anna"));
    m_b = m_people->create(QStringLiteral("Bence"));
    m_me = m_people->create(QStringLiteral("Én"));
    build();
}

void RemoteLineAssignmentTest::cleanup()
{
    m_prints.reset();
    m_people.reset();
    m_store.reset();
    m_home.reset();
}

// A meeting: the local person (voice 5, mic, raw speaker:3) and three remote voices under ONE raw speaker
// (speaker:1): Anna (voice 0), Bence (voice 1), an unknown (voice 2). The provider's speaker:1 is bound to Anna.
void RemoteLineAssignmentTest::build()
{
    m_meeting = m_store->createMeeting(QStringLiteral("Távoli sorok"));
    for (const auto& [id, kind] : {std::pair{QStringLiteral("mic"), TrackKind::Mic},
                                   std::pair{QStringLiteral("loop"), TrackKind::Loopback}}) {
        Track t;
        t.id = id;
        t.deviceName = id;
        t.kind = kind;
        t.file = id + QStringLiteral(".wav");
        m_meeting.tracks.append(t);
    }
    m_rows.clear();
    qint64 t = 1000;
    for (int k = 0; k < 6; ++k)
        for (const auto& [raw, v, local] : {std::tuple{QStringLiteral("speaker:1"), 0, false},
                                            std::tuple{QStringLiteral("speaker:3"), 5, true},
                                            std::tuple{QStringLiteral("speaker:1"), 1, false},
                                            std::tuple{QStringLiteral("speaker:1"), 2, false}}) {
            m_rows.append({t, t + 4000, raw, v, local});
            t += 5000;
        }
    m_meeting.durationMs = t + 1000;
    m_meeting.speakerMap.insert(QStringLiteral("speaker:1"), m_a);
    m_meeting.speakerMap.insert(QStringLiteral("speaker:3"), m_me);
    // The pre-transcript analysis: Anna's and Bence's voices found on the loopback, the local one on the mic.
    Participant pa, pb, pm;
    pa.id = QStringLiteral("v1"); pa.personId = m_a; pa.sides = {QStringLiteral("loopback")};
    pb.id = QStringLiteral("v2"); pb.personId = m_b; pb.sides = {QStringLiteral("loopback")};
    pm.id = QStringLiteral("v3"); pm.personId = m_me; pm.sides = {QStringLiteral("mic")};
    m_meeting.participants = {pa, pb, pm};
    m_clusters.clear();
    for (const auto& [pid, v] : {std::pair{QStringLiteral("v1"), 0}, std::pair{QStringLiteral("v2"), 1}}) {
        VoiceCluster c;
        c.trackId = QStringLiteral("loop");
        c.side = QStringLiteral("loopback");
        c.participantId = pid;
        for (const Row& r : std::as_const(m_rows))
            if (r.voice == v) c.windows.append({r.s, r.e});
        c.windows.resize(3);
        m_clusters.append(c);
    }

    QJsonArray toks, segs;
    for (const Row& r : std::as_const(m_rows)) {
        toks.append(QJsonObject{{"text", "x"}, {"speaker", r.raw}, {"startMs", double(r.s)}, {"endMs", double(r.e)},
                                {"confidence", 1.0}, {"trackId", "mixdown"}});
        segs.append(QJsonObject{{"startMs", double(r.s)}, {"endMs", double(r.e)}, {"speaker", r.raw}, {"text", "x"}});
    }
    QFile tf(QDir(m_meeting.folder).filePath("transcript.tokens.json"));
    QVERIFY(tf.open(QIODevice::WriteOnly));
    tf.write(QJsonDocument(QJsonObject{{"language", "hu"}, {"tokens", toks}}).toJson());
    tf.close();
    QFile sf(QDir(m_meeting.folder).filePath("transcript.segments.json"));
    QVERIFY(sf.open(QIODevice::WriteOnly));
    sf.write(QJsonDocument(segs).toJson());
    sf.close();
    m_meeting.hasTranscript = true;
    m_store->saveMeeting(m_meeting);
    m_meeting = m_store->load(m_meeting.id);

    m_lines = editing::speakeredit::loadTranscriptLines(m_meeting.folder);
    QCOMPARE(m_lines.size(), m_rows.size());
    library::UtteranceEmbeddingCache cache;
    cache.fingerprint = editing::speakeredit::transcriptFingerprint(m_lines);
    for (int i = 0; i < m_lines.size(); ++i) cache.set(kModel, m_lines[i].id, voice(m_rows[i].voice, quint32(i) + 1));
    QVERIFY(cache.save(m_meeting.folder));

    m_activity = MeetingActivity{};
    m_activity.tracks = {activity("mic", TrackKind::Mic, m_meeting.durationMs, m_rows, true),
                         activity("loop", TrackKind::Loopback, m_meeting.durationMs, m_rows, false)};
}

void RemoteLineAssignmentTest::assignsSplitsAndKeepsManualDecisions()
{
    SpeakerEditSession ed(m_store.get(), m_people.get(), m_prints.get(), m_meeting.id);
    ed.setSelfPerson(SelfPerson{m_me, QStringLiteral("Én")});
    QVERIFY(ed.hasEmbeddings());
    // The user confirmed one of Bence's lines as Anna's ("Fine as is"): it stays Anna's whatever the voice says.
    int manual = -1;
    for (int i = 0; i < m_rows.size() && manual < 0; ++i)
        if (m_rows[i].voice == 1) manual = i;
    QVERIFY(ed.confirmUtterances({m_lines[manual].id}));
    const int stepsBefore = ed.undoSerials().size();

    SpeakerEditSession::RemoteAssignContext ctx;
    ctx.analysisClusters = m_clusters;
    ctx.participants = m_meeting.participants;
    ctx.activity = m_activity;
    ctx.options.minClusterMs = 8000;
    const SpeakerEditSession::RemoteAssignPlan plan = ed.planRemoteAssignment(ctx);
    QVERIFY2(plan.ran, qPrintable(plan.skipped));
    QVERIFY(!plan.singleTrack);
    QCOMPARE(plan.result.anonymous, 1);
    QCOMPARE(plan.changed, 5 + 6);   // Bence's other 5 lines + the unknown voice's 6
    QCOMPARE(plan.newParticipants, 2);

    QVERIFY(ed.applyRemoteAssignment(plan) == 11);
    QCOMPARE(ed.undoSerials().size(), stepsBefore + 1);   // ONE undo step
    QVERIFY(ed.remoteAssignmentDone());
    QString unknownKey;
    for (int i = 0; i < m_rows.size(); ++i) {
        const editing::EditorUtterance u = ed.utterance(m_lines[i].id);
        const PersonId person = ed.speaker(u.speakerKey).personId;
        if (i == manual) {
            QCOMPARE(person, m_a);
            QVERIFY(u.confirmed);
            continue;
        }
        switch (m_rows[i].voice) {
        case 0: QCOMPARE(person, m_a); break;
        case 1: QCOMPARE(person, m_b); QVERIFY(!u.manuallyCorrected); break;
        case 2:
            QVERIFY(person.isNull());
            QVERIFY(editing::speakeredit::isParticipantKey(u.speakerKey));
            if (unknownKey.isEmpty()) unknownKey = u.speakerKey;
            QCOMPARE(u.speakerKey, unknownKey);   // one anonymous participant for the voice
            break;
        case 5: QCOMPARE(person, m_me); break;   // the local side untouched
        }
        if (!m_rows[i].local) QVERIFY(person != m_me);
    }

    // Stored as automatic, not as a manual correction; survives a reload from disk.
    const SpeakerOverlay disk = library::TranscriptRepository::readOverlay(m_meeting.folder);
    int automatic = 0;
    for (auto it = disk.utterances.cbegin(); it != disk.utterances.cend(); ++it) {
        if (it.key() == m_lines[manual].id) { QVERIFY(it->confirmed); QVERIFY(!it->automatic); continue; }
        if (it->automatic) { ++automatic; QVERIFY(!it->corrected); QVERIFY(!it->speaker.isEmpty()); }
    }
    QCOMPARE(automatic, 11);
    QVERIFY(!disk.remoteAssignedAt.isEmpty());

    // A rerun changes nothing (the automatic assignment is the starting point, the anonymous voice keeps its key).
    const SpeakerEditSession::RemoteAssignPlan again = ed.planRemoteAssignment(ctx);
    QVERIFY(again.ran);
    QCOMPARE(again.changed, 0);
    QCOMPARE(again.newParticipants, 0);
    QCOMPARE(ed.applyRemoteAssignment(again), 0);
    QCOMPARE(ed.undoSerials().size(), stepsBefore + 1);

    // Undo restores the provider's binding (the run stays recorded: it does not run by itself again).
    ed.undo();
    for (int i = 0; i < m_rows.size(); ++i)
        if (!m_rows[i].local) QCOMPARE(ed.speaker(ed.utterance(m_lines[i].id).speakerKey).personId, m_a);
    QVERIFY(ed.remoteAssignmentDone());
}

void RemoteLineAssignmentTest::singleTrackIsLeftUnchanged()
{
    SpeakerEditSession ed(m_store.get(), m_people.get(), m_prints.get(), m_meeting.id);
    SpeakerEditSession::RemoteAssignContext ctx;
    ctx.analysisClusters = m_clusters;
    ctx.participants = m_meeting.participants;
    MeetingActivity micOnly;
    micOnly.tracks = {m_activity.tracks.first()};
    ctx.activity = micOnly;
    ctx.singleTrack = false;   // the measured decision (remotelines::kClusterSingleTrack)
    const SpeakerEditSession::RemoteAssignPlan plan = ed.planRemoteAssignment(ctx);
    QVERIFY(!plan.ran);
    QVERIFY(plan.singleTrack);
    QCOMPARE(plan.skipped, QStringLiteral("single-track"));
    QCOMPARE(ed.applyRemoteAssignment(plan), 0);
}

QTEST_GUILESS_MAIN(RemoteLineAssignmentTest)
#include "test_remote_line_assignment.moc"
