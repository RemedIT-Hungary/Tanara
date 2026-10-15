// editing::ParticipantService on its own (without AppController), with a fake ParticipantAnalysisPort (the
// voice-analysis side) and real stores / editor sessions / shared undo (editing::MeetingUndoRegistry):
// adding / removing a participant as shared undo steps, approve / skip / "Only I spoke" (which drops a
// running analysis's result), the approval-pending banner, the speaker -> track binding before the
// transcript, the unheard participants, and committing a voice identification. Error paths: unknown meeting,
// no own name, nothing to commit. Made-up names, ISOLATED TANARA_HOME; no Soniox / LLM / network / ffmpeg.
#include "tanara/domain/EvidenceText.h"
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/editing/participants/ParticipantSample.h"
#include "tanara/editing/participants/ParticipantService.h"
#include "tanara/editing/session/SpeakerEditSession.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/editing/undo/MeetingUndo.h"
#include "tanara/editing/undo/MeetingUndoRegistry.h"
#include "tanara/library/people/PeopleService.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/library/store/VoiceprintStore.h"

#include <QPointer>

#include <memory>

using namespace tanara;
using editing::ParticipantService;

namespace {

// The voice-analysis side, recorded: a fixed evidence / group, a scripted "analysis running" flag.
class FakeAnalysis : public editing::ParticipantAnalysisPort {
public:
    int evidenceCalls = 0;
    bool running = false;
    QStringList cancelled;
    QStringList modelIds{QStringLiteral("campplus")};
    QVector<VoiceCluster> clusters;   // the "analysis file"

    QVector<Evidence> evidence(const Participant&, bool heard, double, const Meeting&) override
    {
        ++evidenceCalls;
        Evidence e;
        e.kind = EvidenceKind::Voice;
        e.text = heard ? QStringLiteral("hallottuk") : QStringLiteral("nincs lenyomata");
        return {e};
    }
    ParticipantGroup group(const Participant& p) override
    {
        return p.source == ParticipantSource::Manual ? ParticipantGroup::Sure : ParticipantGroup::Doubt;
    }
    QStringList analysisModelIds(const QString&) override { return modelIds; }
    bool bindRawSpeakers(QVector<Participant>&, const QString&, const QVector<TranscriptLine>&) override
    {
        return false;   // no analysis file
    }
    QVector<SpeakerBinding> approvalBindings(const QVector<Participant>&, const QStringList&,
                                             const QVector<TranscriptLine>&, const Meeting&, bool) override
    {
        return {};
    }
    bool analysisRunning(const QString&) const override { return running; }
    void cancelAnalysis(const QString& meetingId) override { cancelled << meetingId; }
    QVector<VoiceCluster> analysisClusters(const QString&) override { return clusters; }
};

} // namespace

class ParticipantServiceTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void addRemoveAsUndoSteps();
    void approveSkipSoloAndPending();
    void trackBindingBeforeTranscript();
    void identificationCommitAndUnheard();
    void errorPaths();

    // The card's row operations (UA1).
    void nameBeforeTranscript();
    void nameBeforeTranscriptFollowsTheDecision();
    void nameAfterTranscript();
    void mergeIntoBeforeTranscript();
    void mergeIntoAfterTranscript();
    void confirmParticipant();
    void selfParticipant();
    void sampleRefPersisted();
    void sampleBeforeTranscript();
    void sampleAfterTranscript();
    void rowOperationErrorPaths();

private:
    Meeting makeMeeting();
    void writeTranscript(Meeting& m, const QVector<std::tuple<qint64, qint64, QString>>& rows);
    editing::SpeakerEditSession* editorFor(const QString& meetingId);
    // A voice candidate (an analysis cluster's record).
    static Participant voiceRow(const QString& id, const PersonId& person, const QString& side, double share,
                                const QString& sampleTrack = {}, const QString& sampleRef = {});
    void saveParticipants(const QString& meetingId, const QVector<Participant>& ps);
    const Participant* find(const QVector<Participant>& ps, const QString& id) const;

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<library::MeetingRepository> m_store;
    std::unique_ptr<library::PeopleStore> m_people;
    std::unique_ptr<library::VoiceprintStore> m_prints;
    std::unique_ptr<library::PeopleService> m_peopleSvc;
    std::unique_ptr<editing::MeetingUndoRegistry> m_undo;
    std::unique_ptr<FakeAnalysis> m_analysis;
    std::unique_ptr<ParticipantService> m_svc;
    QHash<QString, QPointer<editing::SpeakerEditSession>> m_editors;
    QString m_selfName;
};

void ParticipantServiceTest::init()
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
    m_selfName.clear();
    library::PeopleService::Deps pd;
    pd.store = m_store.get();
    pd.people = m_people.get();
    pd.voiceprints = m_prints.get();
    pd.selfName = [this]() { return m_selfName; };
    pd.setSelfName = [this](const QString& n) { m_selfName = n; };
    m_peopleSvc = std::make_unique<library::PeopleService>(pd);
    m_undo = std::make_unique<editing::MeetingUndoRegistry>(
        m_store.get(), nullptr, [this](const QString& id) -> editing::SpeakerEditSession* { return m_editors.value(id); });
    m_analysis = std::make_unique<FakeAnalysis>();
    ParticipantService::Deps deps;
    deps.store = m_store.get();
    deps.people = m_people.get();
    deps.peopleService = m_peopleSvc.get();
    deps.undo = m_undo.get();
    deps.analysis = m_analysis.get();
    deps.editorFor = [this](const QString& id) { return editorFor(id); };
    deps.existingEditor = [this](const QString& id) -> editing::SpeakerEditSession* { return m_editors.value(id); };
    m_svc = std::make_unique<ParticipantService>(deps);
}

void ParticipantServiceTest::cleanup()
{
    m_svc.reset();
    for (const auto& ed : std::as_const(m_editors)) delete ed.data();
    m_editors.clear();
    m_undo.reset();
    m_analysis.reset();
    m_peopleSvc.reset();
    m_prints.reset();
    m_people.reset();
    m_store.reset();
    m_home.reset();
}

editing::SpeakerEditSession* ParticipantServiceTest::editorFor(const QString& meetingId)
{
    if (editing::SpeakerEditSession* ed = m_editors.value(meetingId)) return ed;
    if (m_store->load(meetingId).id.isEmpty()) return nullptr;
    auto* ed = new editing::SpeakerEditSession(m_store.get(), m_people.get(), m_prints.get(), meetingId);
    m_editors.insert(meetingId, ed);
    m_undo->forMeeting(meetingId);   // the shared undo follows from the editor's first step
    return ed;
}

Meeting ParticipantServiceTest::makeMeeting()
{
    Meeting m = m_store->createMeeting(QStringLiteral("Résztvevők"));
    auto add = [&m](const QString& id, TrackKind kind) {
        Track t;
        t.id = id;
        t.deviceName = id;
        t.kind = kind;
        t.file = id + QStringLiteral(".wav");
        m.tracks.append(t);
    };
    add("mic", TrackKind::Mic);
    add("loop", TrackKind::Loopback);
    m.durationMs = 24000;
    m_store->saveMeeting(m);
    return m;
}

void ParticipantServiceTest::writeTranscript(Meeting& m, const QVector<std::tuple<qint64, qint64, QString>>& rows)
{
    QJsonArray toks, segs;
    for (const auto& [s, e, spk] : rows) {
        toks.append(QJsonObject{{"text", "x"}, {"speaker", spk}, {"startMs", double(s)}, {"endMs", double(e)},
                                {"confidence", 1.0}, {"trackId", "mixdown"}});
        segs.append(QJsonObject{{"startMs", double(s)}, {"endMs", double(e)}, {"speaker", spk}, {"text", "x"}});
    }
    QFile tf(QDir(m.folder).filePath("transcript.tokens.json"));
    QVERIFY(tf.open(QIODevice::WriteOnly));
    tf.write(QJsonDocument(QJsonObject{{"language", "hu"}, {"tokens", toks}}).toJson());
    tf.close();
    QFile sf(QDir(m.folder).filePath("transcript.segments.json"));
    QVERIFY(sf.open(QIODevice::WriteOnly));
    sf.write(QJsonDocument(segs).toJson());
    sf.close();
    m = m_store->load(m.id);
    m.hasTranscript = true;
    m_store->saveMeeting(m);
}

// The main path: a manual participant (a new person) and its permanent removal, each ONE step of the
// meeting's shared undo that restores the participant state.
void ParticipantServiceTest::addRemoveAsUndoSteps()
{
    const Meeting m = makeMeeting();
    QSignalSpy changed(m_svc.get(), &ParticipantService::participantsChanged);
    QSignalSpy people(m_svc.get(), &ParticipantService::peopleChanged);
    const QString id = m_svc->addParticipant(m.id, PersonChoice::create(QStringLiteral("Cecília")));
    QVERIFY(id.startsWith(QStringLiteral("m-")));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(people.count(), 1);   // the person was created
    QCOMPARE(m_analysis->evidenceCalls, 1);
    const PersonId cili = m_people->idForName(QStringLiteral("Cecília"));
    QVERIFY(!cili.isNull());
    QCOMPARE(m_svc->addParticipant(m.id, PersonChoice::existing(cili)), id);   // already there
    QVector<Participant> ps = m_svc->participants(m.id);
    QCOMPARE(ps.size(), 1);
    QCOMPARE(ps.first().source, ParticipantSource::Manual);
    QCOMPARE(evidenceText(ps.first().evidence.value(0)), QStringLiteral("nincs lenyomata"));

    editing::MeetingUndoService* undo = m_undo->forMeeting(m.id);
    QVERIFY(undo && undo->canUndo());
    QCOMPARE(undo->undoText(), QStringLiteral("Résztvevő felvéve: Cecília"));

    QVERIFY(m_svc->removeParticipant(m.id, id));
    QVERIFY(m_svc->participants(m.id).isEmpty());
    QCOMPARE(undo->count(), 2);
    QCOMPARE(undo->undoText(), QStringLiteral("Résztvevő eltávolítva: Cecília"));

    // Undo restores the removed participant (the registry re-emits the restored state), then the addition.
    QSignalSpy restored(m_undo.get(), &editing::MeetingUndoRegistry::participantsChanged);
    QVERIFY(undo->undo());
    QCOMPARE(restored.count(), 1);
    QCOMPARE(m_svc->participants(m.id).size(), 1);
    QVERIFY(undo->undo());
    QVERIFY(m_svc->participants(m.id).isEmpty());
    QVERIFY(!undo->canUndo());
}

// Approve / skip / "Only I spoke", and the banner; "Only I spoke" drops the running analysis's result.
void ParticipantServiceTest::approveSkipSoloAndPending()
{
    const Meeting m0 = makeMeeting();
    // A voice candidate (not manual) makes the banner show.
    Meeting m = m_store->load(m0.id);
    Participant voice;
    voice.id = QStringLiteral("v-1");
    voice.personId = m_people->ensure(QStringLiteral("Béla"));
    voice.source = ParticipantSource::Voice;
    m.participants.append(voice);
    m_store->saveMeeting(m);
    QVERIFY(m_svc->participantApprovalPending(m0.id));
    m_analysis->running = true;
    QVERIFY(!m_svc->participantApprovalPending(m0.id));   // not while the analysis runs
    m_analysis->running = false;

    // Approve: the checked one plus a person added in the dialog.
    Participant added;
    added.personId = m_people->ensure(QStringLiteral("Dóra"));
    QVERIFY(m_svc->approveParticipants(m0.id, {voice, added}));
    m = m_store->load(m0.id);
    QVERIFY(m.approval.has_value());
    QCOMPARE(m.approval->modelIds, QStringList{QStringLiteral("campplus")});
    QCOMPARE(m.participants.size(), 2);
    QCOMPARE(m.approval->candidates.size(), 2);
    for (const Participant& p : m.participants) QVERIFY(p.approved);
    QCOMPARE(m.approval->candidates.at(1).group, ParticipantGroup::Sure);   // the approval's person is manual
    QVERIFY(!m_svc->participantApprovalPending(m0.id));

    // Skip: a decision without a binding.
    QVERIFY(m_svc->skipApproval(m0.id));
    QVERIFY(m_store->load(m0.id).approval->skipped);

    // "Only I spoke": needs the own name; drops a running analysis's result.
    QVERIFY(!m_svc->setSoloMeeting(m0.id));   // no own name configured
    m_selfName = QStringLiteral("Ádám");
    m_analysis->running = true;
    QVERIFY(m_svc->setSoloMeeting(m0.id));
    QCOMPARE(m_analysis->cancelled, QStringList{m0.id});
    m = m_store->load(m0.id);
    QVERIFY(m.approval && m.approval->solo);
    int approved = 0;
    for (const Participant& p : m.participants)
        if (p.approved) {
            ++approved;
            QCOMPARE(m_people->displayName(p.personId), QStringLiteral("Ádám"));
        }
    QCOMPARE(approved, 1);

    // Every operation above was one step of the shared undo.
    editing::MeetingUndoService* undo = m_undo->forMeeting(m0.id);
    QCOMPARE(undo->texts(), (QStringList{QStringLiteral("Résztvevők jóváhagyva"), QStringLiteral("Jóváhagyás kihagyva"),
                                         QStringLiteral("Csak én beszéltem")}));
    QVERIFY(undo->undo());
    QVERIFY(m_store->load(m0.id).approval->skipped);
}

// The speaker -> track binding before the transcript: on the participant (normalized, unknown ids dropped),
// manual while set, inferred from the voice analysis's sides when cleared.
void ParticipantServiceTest::trackBindingBeforeTranscript()
{
    const Meeting m0 = makeMeeting();
    Meeting m = m_store->load(m0.id);
    Participant p;
    p.id = QStringLiteral("v-1");
    p.personId = m_people->ensure(QStringLiteral("Béla"));
    p.source = ParticipantSource::Voice;
    p.sides = {QStringLiteral("loopback")};
    m.participants.append(p);
    m_store->saveMeeting(m);

    ParticipantTrackBinding b = m_svc->participantTracks(m0.id, p.id);
    QVERIFY(!b.manual);
    QCOMPARE(b.trackIds, QStringList{QStringLiteral("loop")});

    QSignalSpy changed(m_svc.get(), &ParticipantService::participantsChanged);
    QVERIFY(m_svc->setParticipantTracks(m0.id, p.id, {QStringLiteral("mic"), QStringLiteral("nope")}));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(m_store->load(m0.id).participants.first().tracks, QStringList{QStringLiteral("mic")});
    b = m_svc->participantTracks(m0.id, p.id);
    QVERIFY(b.manual);
    QCOMPARE(b.trackIds, QStringList{QStringLiteral("mic")});
    QVERIFY(!m_svc->setParticipantTracks(m0.id, p.id, {QStringLiteral("mic")}));   // unchanged

    QVERIFY(m_svc->setParticipantTracks(m0.id, p.id, {}));
    b = m_svc->participantTracks(m0.id, p.id);
    QVERIFY(!b.manual);
    QCOMPARE(b.trackIds, QStringList{QStringLiteral("loop")});
}

// Committing a voice identification (speakerMap, the overlay's record, transcript.md) and the unheard
// participants afterwards.
void ParticipantServiceTest::identificationCommitAndUnheard()
{
    Meeting m = makeMeeting();
    writeTranscript(m, {{1000, 7000, "S1"}, {9000, 15000, "S2"}});
    const PersonId bela = m_people->ensure(QStringLiteral("Béla"));
    const PersonId dora = m_people->ensure(QStringLiteral("Dóra"));

    QSignalSpy map(m_svc.get(), &ParticipantService::speakerMapChanged);
    m_svc->commitIdentification(m.id, {SpeakerVoiceMatch{QStringLiteral("S2"), bela, 0.82}});
    QCOMPARE(map.count(), 1);
    m = m_store->load(m.id);
    QCOMPARE(m.speakerMap.value(QStringLiteral("S2")), bela);
    const SpeakerOverlay ov = editing::speakeredit::loadOverlay(m.folder);
    QCOMPARE(ov.identified.value(QStringLiteral("S2")).personId, bela);
    QCOMPARE(ov.identified.value(QStringLiteral("S2")).score, 0.82);
    QFile md(library::TranscriptRepository::markdownPath(m.folder));
    QVERIFY(md.open(QIODevice::ReadOnly));
    QVERIFY(QString::fromUtf8(md.readAll()).contains(QStringLiteral("Béla")));

    // Béla speaks in the transcript; Dóra was added by hand and never spoke.
    QVERIFY(!m_svc->addParticipant(m.id, PersonChoice::existing(bela)).isEmpty());
    QVERIFY(!m_svc->addParticipant(m.id, PersonChoice::existing(dora)).isEmpty());
    QVector<Participant> unheard = m_svc->unheardParticipants(m.id);   // resolved from disk
    QCOMPARE(unheard.size(), 1);
    QCOMPARE(unheard.first().personId, dora);
    QVERIFY(editorFor(m.id));
    unheard = m_svc->unheardParticipants(m.id);                         // from the open editor
    QCOMPARE(unheard.size(), 1);
    QCOMPARE(unheard.first().personId, dora);
}

void ParticipantServiceTest::errorPaths()
{
    const QString nope = QStringLiteral("nope");
    QSignalSpy changed(m_svc.get(), &ParticipantService::participantsChanged);
    QSignalSpy map(m_svc.get(), &ParticipantService::speakerMapChanged);
    QVERIFY(m_svc->addParticipant(nope, PersonChoice::create(QStringLiteral("X"))).isEmpty());
    QVERIFY(!m_svc->approveParticipants(nope, {}));
    QVERIFY(!m_svc->skipApproval(nope));
    QVERIFY(!m_svc->unbindParticipant(nope, QStringLiteral("p")));
    QVERIFY(!m_svc->removeParticipant(nope, QStringLiteral("p")));
    QVERIFY(!m_svc->setParticipantTracks(nope, QStringLiteral("p"), {}));
    QVERIFY(!m_svc->bindRawSpeakers(nope));
    QVERIFY(!m_svc->participantApprovalPending(nope));
    QVERIFY(m_svc->unheardParticipants(nope).isEmpty());
    m_svc->commitIdentification(nope, {SpeakerVoiceMatch{QStringLiteral("S1"), PersonId::generate(), 0.9}});

    const Meeting m = makeMeeting();
    QVERIFY(m_svc->addParticipant(m.id, PersonChoice()).isEmpty());          // anonymous choice
    QVERIFY(!m_svc->unbindParticipant(m.id, QStringLiteral("nincs")));       // no such participant
    QVERIFY(!m_svc->removeParticipant(m.id, QStringLiteral("nincs")));       // no record, no transcript
    QVERIFY(!m_svc->bindRawSpeakers(m.id));                                  // no participants
    m_svc->commitIdentification(m.id, {});                                   // nothing to commit
    QCOMPARE(changed.count(), 0);
    QCOMPARE(map.count(), 0);
    QVERIFY(!m_undo->forMeeting(m.id)->canUndo());                           // nothing changed: no step
    QVERIFY(!m_undo->forMeeting(nope));
}

// ==== the card's row operations (UA1) ===========================================================

Participant ParticipantServiceTest::voiceRow(const QString& id, const PersonId& person, const QString& side,
                                             double share, const QString& sampleTrack, const QString& sampleRef)
{
    Participant p;
    p.id = id;
    p.personId = person;
    p.source = ParticipantSource::Voice;
    p.sides = {side};
    p.talkShare = share;
    p.sampleTrackId = sampleTrack;
    p.sampleRef = sampleRef;
    return p;
}

void ParticipantServiceTest::saveParticipants(const QString& meetingId, const QVector<Participant>& ps)
{
    Meeting m = m_store->load(meetingId);
    m.participants = ps;
    m_store->saveMeeting(m);
}

const Participant* ParticipantServiceTest::find(const QVector<Participant>& ps, const QString& id) const
{
    for (const Participant& p : ps)
        if (p.id == id) return &p;
    return nullptr;
}

// Naming a voice cluster before the transcript: the record gets the person; the person's manual voiceless row
// dissolves into it (its track binding carried over); a new person is created; one undo step each.
void ParticipantServiceTest::nameBeforeTranscript()
{
    const Meeting m = makeMeeting();
    const QString cili = m_svc->addParticipant(m.id, PersonChoice::create(QStringLiteral("Cecília")));
    const PersonId ciliId = m_people->idForName(QStringLiteral("Cecília"));
    {
        Meeting mm = m_store->load(m.id);
        mm.participants.first().tracks = {QStringLiteral("loop")};
        mm.participants.prepend(voiceRow(QStringLiteral("v1"), {}, QStringLiteral("loopback"), 0.6,
                                         QStringLiteral("loop"), QStringLiteral("track_loop.wav#1000-4000")));
        mm.participants.prepend(voiceRow(QStringLiteral("v2"), {}, QStringLiteral("mic"), 0.4));
        m_store->saveMeeting(mm);
    }
    editing::MeetingUndoService* undo = m_undo->forMeeting(m.id);
    const int steps = undo->count();
    QSignalSpy changed(m_svc.get(), &ParticipantService::participantsChanged);

    // The manual row merges: the cluster row carries Cecília.
    QVERIFY(m_svc->nameParticipant(m.id, QStringLiteral("v1"), PersonChoice::existing(ciliId)));
    QCOMPARE(changed.count(), 1);
    QVector<Participant> ps = m_svc->participants(m.id);
    QCOMPARE(ps.size(), 2);
    QVERIFY(!find(ps, cili));
    const Participant* v1 = find(ps, QStringLiteral("v1"));
    QVERIFY(v1);
    QCOMPARE(v1->personId, ciliId);
    QCOMPARE(v1->tracks, QStringList{QStringLiteral("loop")});        // the manual row's track binding
    QCOMPARE(v1->sampleRef, QStringLiteral("track_loop.wav#1000-4000"));
    QVERIFY(!v1->approved);                                           // no decision yet: approval stays separate
    QCOMPARE(evidenceText(v1->evidence.value(0)), QStringLiteral("hallottuk"));  // recomputed (heard)
    QCOMPARE(undo->count(), steps + 1);
    QCOMPARE(undo->undoText(), QStringLiteral("Elnevezve: Cecília"));
    QVERIFY(!m_svc->nameParticipant(m.id, QStringLiteral("v1"), PersonChoice::existing(ciliId)));   // no change

    // A new person for the other cluster.
    QSignalSpy people(m_svc.get(), &ParticipantService::peopleChanged);
    QVERIFY(m_svc->nameParticipant(m.id, QStringLiteral("v2"), PersonChoice::create(QStringLiteral("Dénes"))));
    QCOMPARE(people.count(), 1);
    QCOMPARE(find(m_svc->participants(m.id), QStringLiteral("v2"))->personId, m_people->idForName(QStringLiteral("Dénes")));
    QCOMPARE(undo->undoText(), QStringLiteral("Elnevezve: Dénes"));

    // Undo, step by step: the manual row comes back, the cluster is anonymous again.
    QVERIFY(undo->undo());
    QVERIFY(find(m_svc->participants(m.id), QStringLiteral("v2"))->personId.isNull());
    QVERIFY(undo->undo());
    ps = m_svc->participants(m.id);
    QCOMPARE(ps.size(), 3);
    QVERIFY(find(ps, cili));
    QVERIFY(find(ps, QStringLiteral("v1"))->personId.isNull());
}

// After a decision the named row counts as approved and its approval entry follows; naming a second cluster
// with a person that already has a VOICED row merges the two (same voice).
void ParticipantServiceTest::nameBeforeTranscriptFollowsTheDecision()
{
    const Meeting m = makeMeeting();
    const PersonId bela = m_people->ensure(QStringLiteral("Béla"));
    saveParticipants(m.id, {voiceRow(QStringLiteral("v1"), {}, QStringLiteral("mic"), 0.2),
                            voiceRow(QStringLiteral("v2"), bela, QStringLiteral("loopback"), 0.5,
                                     QStringLiteral("loop"), QStringLiteral("track_loop.wav#0-3000"))});
    QVERIFY(m_svc->approveParticipants(m.id, {}));   // a decision with nobody checked
    QVERIFY(m_svc->nameParticipant(m.id, QStringLiteral("v1"), PersonChoice::create(QStringLiteral("Dóra"))));
    Meeting after = m_store->load(m.id);
    const Participant* v1 = find(after.participants, QStringLiteral("v1"));
    QVERIFY(v1 && v1->approved);
    bool entry = false;
    for (const ParticipantApprovalEntry& e : after.approval->candidates)
        if (e.participantId == QStringLiteral("v1")) {
            entry = true;
            QVERIFY(e.checked);
            QCOMPARE(e.personId, v1->personId);
        }
    QVERIFY(entry);

    // v1 named Béla too: v1 joins v2 (Béla's voiced row survives, v1's cluster belongs to it).
    QVERIFY(m_svc->nameParticipant(m.id, QStringLiteral("v1"), PersonChoice::existing(bela)));
    after = m_store->load(m.id);
    QCOMPARE(after.participants.size(), 1);
    const Participant& v2 = after.participants.first();
    QCOMPARE(v2.id, QStringLiteral("v2"));
    QCOMPARE(v2.personId, bela);
    QCOMPARE(v2.mergedFrom, QStringList{QStringLiteral("v1")});
    QCOMPARE(v2.sides, (QStringList{QStringLiteral("loopback"), QStringLiteral("mic")}));
    QCOMPARE(after.approval->candidates.size(), 1);
    QVERIFY(m_undo->forMeeting(m.id)->undo());
    QCOMPARE(m_store->load(m.id).participants.size(), 2);
}

// After the transcript: naming binds the row's speakers to the person through the editor (one undo step,
// absorbing the editor step); the manual voiceless row merges; a speaker key without a record is named too.
void ParticipantServiceTest::nameAfterTranscript()
{
    Meeting m = makeMeeting();
    writeTranscript(m, {{1000, 4000, "S1"}, {5000, 9000, "S2"}, {10000, 12000, "S1"}});
    const PersonId cili = m_people->ensure(QStringLiteral("Cecília"));
    Participant v1 = voiceRow(QStringLiteral("v1"), {}, QStringLiteral("mic"), 0.5);
    v1.rawSpeakerIds = {QStringLiteral("S1")};
    Participant manual;
    manual.id = QStringLiteral("m-1");
    manual.personId = cili;
    manual.source = ParticipantSource::Manual;
    saveParticipants(m.id, {v1, manual});
    editing::SpeakerEditSession* ed = editorFor(m.id);
    QVERIFY(ed && ed->hasTranscript());
    editing::MeetingUndoService* undo = m_undo->forMeeting(m.id);
    const int steps = undo->count();

    QVERIFY(m_svc->nameParticipant(m.id, QStringLiteral("v1"), PersonChoice::existing(cili)));
    QCOMPARE(ed->speaker(QStringLiteral("S1")).personId, cili);
    QVector<Participant> ps = m_svc->participants(m.id);
    QCOMPARE(ps.size(), 1);
    QCOMPARE(ps.first().id, QStringLiteral("v1"));
    QCOMPARE(ps.first().personId, cili);
    QCOMPARE(undo->count(), steps + 1);                 // one shared step (the editor's absorbed)
    QCOMPARE(undo->undoText(), QStringLiteral("Elnevezve: Cecília"));

    // A speaker without a record (by its key): only the editor binding.
    QVERIFY(m_svc->nameParticipant(m.id, QStringLiteral("S2"), PersonChoice::create(QStringLiteral("Dóra"))));
    QCOMPARE(ed->speaker(QStringLiteral("S2")).personId, m_people->idForName(QStringLiteral("Dóra")));
    QCOMPARE(m_svc->participants(m.id).size(), 1);
    QCOMPARE(undo->count(), steps + 2);
    QCOMPARE(undo->undoText(), QStringLiteral("Elnevezve: Dóra"));

    // Undo both: the speakers are anonymous again, the manual row is back.
    QVERIFY(undo->undo());
    QVERIFY(ed->speaker(QStringLiteral("S2")).personId.isNull());
    QVERIFY(undo->undo());
    QVERIFY(ed->speaker(QStringLiteral("S1")).personId.isNull());
    QCOMPARE(m_svc->participants(m.id).size(), 2);
}

// "Same voice as ..." before the transcript: the records merge (raw speakers, sides, share, clusters, the
// larger cluster's sample); the target's person wins, an anonymous target takes the source's.
void ParticipantServiceTest::mergeIntoBeforeTranscript()
{
    const Meeting m = makeMeeting();
    const PersonId bela = m_people->ensure(QStringLiteral("Béla"));
    saveParticipants(m.id, {voiceRow(QStringLiteral("v1"), {}, QStringLiteral("mic"), 0.2,
                                     QStringLiteral("mic"), QStringLiteral("track_mic.wav#0-2000")),
                            voiceRow(QStringLiteral("v2"), bela, QStringLiteral("loopback"), 0.5,
                                     QStringLiteral("loop"), QStringLiteral("track_loop.wav#5000-9000"))});
    editing::MeetingUndoService* undo = m_undo->forMeeting(m.id);

    QVERIFY(m_svc->mergeParticipantInto(m.id, QStringLiteral("v1"), QStringLiteral("v2")));
    QVector<Participant> ps = m_svc->participants(m.id);
    QCOMPARE(ps.size(), 1);
    QCOMPARE(ps.first().id, QStringLiteral("v2"));
    QCOMPARE(ps.first().personId, bela);
    QCOMPARE(ps.first().mergedFrom, QStringList{QStringLiteral("v1")});
    QCOMPARE(ps.first().sides, (QStringList{QStringLiteral("loopback"), QStringLiteral("mic")}));
    QVERIFY(qAbs(ps.first().talkShare - 0.7) < 1e-9);
    QCOMPARE(ps.first().sampleRef, QStringLiteral("track_loop.wav#5000-9000"));   // the larger cluster's
    QCOMPARE(undo->undoText(), QStringLiteral("Összevonva: névtelen hang → Béla"));
    QVERIFY(undo->undo());
    QCOMPARE(m_svc->participants(m.id).size(), 2);

    // Into the anonymous row: it takes Béla and the larger (Béla's) sample.
    QVERIFY(m_svc->mergeParticipantInto(m.id, QStringLiteral("v2"), QStringLiteral("v1")));
    ps = m_svc->participants(m.id);
    QCOMPARE(ps.size(), 1);
    QCOMPARE(ps.first().id, QStringLiteral("v1"));
    QCOMPARE(ps.first().personId, bela);
    QCOMPARE(ps.first().sampleTrackId, QStringLiteral("loop"));
    QCOMPARE(ps.first().mergedFrom, QStringList{QStringLiteral("v2")});

    // The merged cluster belongs to the surviving row (the binding / sample side sees it so).
    VoiceCluster c;
    c.participantId = QStringLiteral("v2");
    c.trackId = QStringLiteral("loop");
    const QVector<VoiceCluster> merged = editing::participantsample::withMerges({c}, ps);
    QCOMPARE(merged.first().participantId, QStringLiteral("v1"));
}

// After the transcript: the speakers of `from` go to `into`'s person (one step); two anonymous speakers
// without records merge in the editor.
void ParticipantServiceTest::mergeIntoAfterTranscript()
{
    Meeting m = makeMeeting();
    writeTranscript(m, {{1000, 4000, "S1"}, {5000, 9000, "S2"}, {10000, 12000, "S3"}, {13000, 15000, "S4"}});
    const PersonId bela = m_people->ensure(QStringLiteral("Béla"));
    Participant v1 = voiceRow(QStringLiteral("v1"), {}, QStringLiteral("mic"), 0.3);
    v1.rawSpeakerIds = {QStringLiteral("S1")};
    Participant v2 = voiceRow(QStringLiteral("v2"), bela, QStringLiteral("loopback"), 0.3);
    v2.rawSpeakerIds = {QStringLiteral("S2")};
    saveParticipants(m.id, {v1, v2});
    editing::SpeakerEditSession* ed = editorFor(m.id);
    QVERIFY(ed->applyBindings({{QStringLiteral("S2"), bela, {}}}, QStringLiteral("setup")));
    editing::MeetingUndoService* undo = m_undo->forMeeting(m.id);
    const int steps = undo->count();

    QVERIFY(m_svc->mergeParticipantInto(m.id, QStringLiteral("v1"), QStringLiteral("v2")));
    for (const editing::EditorUtterance& u : ed->utterances())
        if (u.rawLabel == QLatin1String("S1") || u.rawLabel == QLatin1String("S2"))
            QCOMPARE(ed->speaker(u.speakerKey).personId, bela);
    QVector<Participant> ps = m_svc->participants(m.id);
    QCOMPARE(ps.size(), 1);
    QCOMPARE(ps.first().rawSpeakerIds, (QStringList{QStringLiteral("S2"), QStringLiteral("S1")}));
    QCOMPARE(ps.first().mergedFrom, QStringList{QStringLiteral("v1")});
    QCOMPARE(undo->count(), steps + 1);
    QCOMPARE(undo->undoText(), QStringLiteral("Összevonva: névtelen hang → Béla"));
    QVERIFY(undo->undo());
    QCOMPARE(m_svc->participants(m.id).size(), 2);
    QVERIFY(ed->speaker(QStringLiteral("S1")).personId.isNull());

    // Two anonymous speakers without records: S3 joins S4 (one editor step).
    const int before = undo->count();
    QVERIFY(m_svc->mergeParticipantInto(m.id, QStringLiteral("S3"), QStringLiteral("S4")));
    for (const editing::EditorUtterance& u : ed->utterances())
        if (u.rawLabel == QLatin1String("S3")) QCOMPARE(u.speakerKey, QStringLiteral("S4"));
    QCOMPARE(undo->count(), before + 1);
    QVERIFY(undo->undo());
    for (const editing::EditorUtterance& u : ed->utterances())
        if (u.rawLabel == QLatin1String("S3")) QCOMPARE(u.speakerKey, QStringLiteral("S3"));
}

// "Was there" on a doubtful row and its undo: the record's approved flag and the approval entry, one step each.
void ParticipantServiceTest::confirmParticipant()
{
    const Meeting m = makeMeeting();
    const PersonId bela = m_people->ensure(QStringLiteral("Béla"));
    saveParticipants(m.id, {voiceRow(QStringLiteral("v1"), bela, QStringLiteral("loopback"), 0.4),
                            voiceRow(QStringLiteral("v2"), {}, QStringLiteral("mic"), 0.6)});
    editing::MeetingUndoService* undo = m_undo->forMeeting(m.id);

    QVERIFY(!m_svc->confirmParticipant(m.id, QStringLiteral("v2"), true));   // anonymous: name it first
    QVERIFY(m_svc->confirmParticipant(m.id, QStringLiteral("v1"), true));
    QVERIFY(find(m_svc->participants(m.id), QStringLiteral("v1"))->approved);
    QVERIFY(!m_store->load(m.id).approval.has_value());                       // no decision is made by it
    QCOMPARE(undo->undoText(), QStringLiteral("Ott volt: Béla"));
    QVERIFY(!m_svc->confirmParticipant(m.id, QStringLiteral("v1"), true));    // no change

    // With a decision: the entry's check follows, both ways.
    QVERIFY(m_svc->approveParticipants(m.id, {}));
    QVERIFY(!find(m_svc->participants(m.id), QStringLiteral("v1"))->approved);
    QVERIFY(m_svc->confirmParticipant(m.id, QStringLiteral("v1"), true));
    auto checked = [&]() {
        const Meeting now = m_store->load(m.id);
        for (const ParticipantApprovalEntry& e : now.approval->candidates)
            if (e.participantId == QStringLiteral("v1")) return e.checked;
        return false;
    };
    QVERIFY(checked());
    QVERIFY(m_svc->confirmParticipant(m.id, QStringLiteral("v1"), false));
    QVERIFY(!checked());
    QVERIFY(!find(m_svc->participants(m.id), QStringLiteral("v1"))->approved);
    QCOMPARE(undo->undoText(), QStringLiteral("Ott volt, visszavonva: Béla"));
    QVERIFY(undo->undo());
    QVERIFY(checked());
}

// "This is me": needs the own name; merges with the own manual row.
void ParticipantServiceTest::selfParticipant()
{
    const Meeting m = makeMeeting();
    saveParticipants(m.id, {voiceRow(QStringLiteral("v1"), {}, QStringLiteral("mic"), 0.7)});
    QString why;
    QVERIFY(!m_svc->setSelfParticipant(m.id, QStringLiteral("v1"), &why));   // no own name configured
    QVERIFY(!why.isEmpty());
    QVERIFY(!m_undo->forMeeting(m.id)->canUndo());

    m_selfName = QStringLiteral("Ádám");
    m_peopleSvc->ensureSelf();
    const PersonId self = m_peopleSvc->selfPerson().id;
    QVERIFY(!self.isNull());
    const QString own = m_svc->addParticipant(m.id, PersonChoice::existing(self));
    QVERIFY(!own.isEmpty());
    QVERIFY(m_svc->setSelfParticipant(m.id, QStringLiteral("v1"), &why));
    QVERIFY(why.isEmpty());
    const QVector<Participant> ps = m_svc->participants(m.id);
    QCOMPARE(ps.size(), 1);
    QCOMPARE(ps.first().id, QStringLiteral("v1"));
    QCOMPARE(ps.first().personId, self);
    editing::MeetingUndoService* undo = m_undo->forMeeting(m.id);
    QCOMPARE(undo->undoText(), QStringLiteral("Elnevezve: Ádám"));
    QVERIFY(undo->undo());
    QCOMPARE(m_svc->participants(m.id).size(), 2);
}

// The participant's sample and merges are persisted with the meeting (additive fields).
void ParticipantServiceTest::sampleRefPersisted()
{
    const Meeting m = makeMeeting();
    Participant p = voiceRow(QStringLiteral("v1"), {}, QStringLiteral("mic"), 0.5, QStringLiteral("mic"),
                             QStringLiteral("track_mic.wav#2000-8000"));
    p.mergedFrom = {QStringLiteral("v7")};
    saveParticipants(m.id, {p, voiceRow(QStringLiteral("v2"), {}, QStringLiteral("mic"), 0.1)});
    const QVector<Participant> back = m_store->load(m.id).participants;
    QCOMPARE(back.size(), 2);
    QCOMPARE(back.at(0).sampleTrackId, QStringLiteral("mic"));
    QCOMPARE(back.at(0).sampleRef, QStringLiteral("track_mic.wav#2000-8000"));
    QCOMPARE(back.at(0).mergedFrom, QStringList{QStringLiteral("v7")});
    QVERIFY(back.at(1).sampleRef.isEmpty() && back.at(1).sampleTrackId.isEmpty() && back.at(1).mergedFrom.isEmpty());
    // An absent field stays absent in the file (the format grows only additively).
    QFile f(QDir(m.folder).filePath(QStringLiteral("meeting.json")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray json = f.readAll();
    QCOMPARE(json.count("\"sampleRef\""), 1);
}

// Before the transcript: the record's stored sample, failing that its (merged) largest cluster's; a manual
// row without a voice has none.
void ParticipantServiceTest::sampleBeforeTranscript()
{
    const Meeting m = makeMeeting();
    Participant stored = voiceRow(QStringLiteral("v1"), {}, QStringLiteral("mic"), 0.5, QStringLiteral("mic"),
                                  QStringLiteral("track_mic.wav#2000-8000"));
    Participant fromClusters = voiceRow(QStringLiteral("v2"), {}, QStringLiteral("loopback"), 0.3);
    fromClusters.mergedFrom = {QStringLiteral("v9")};
    Participant manual;
    manual.id = QStringLiteral("m-1");
    manual.source = ParticipantSource::Manual;
    manual.personId = m_people->ensure(QStringLiteral("Cecília"));
    saveParticipants(m.id, {stored, fromClusters, manual});
    VoiceCluster small, big;
    small.trackId = QStringLiteral("loop");
    small.participantId = QStringLiteral("v2");
    small.windows = {{0, 2000}};
    small.sampleRef = QStringLiteral("track_loop.wav#0-2000");
    big.trackId = QStringLiteral("loop");
    big.participantId = QStringLiteral("v9");   // merged into v2
    big.windows = {{10000, 20000}};
    big.sampleRef = QStringLiteral("track_loop.wav#12000-18000");
    m_analysis->clusters = {small, big};

    const QVector<editing::ParticipantSample> s =
        m_svc->participantSamples(m.id, {QStringLiteral("v1"), QStringLiteral("v2"), QStringLiteral("m-1"), QStringLiteral("x")});
    QCOMPARE(s.size(), 4);
    QCOMPARE(s.at(0).trackId, QStringLiteral("mic"));
    QCOMPARE(s.at(0).startMs, 2000);
    QCOMPARE(s.at(0).endMs, 8000);
    QCOMPARE(s.at(1).trackId, QStringLiteral("loop"));   // the merged-in larger cluster
    QCOMPARE(s.at(1).startMs, 12000);
    QCOMPARE(s.at(1).endMs, 18000);
    QVERIFY(!s.at(2).isValid());
    QVERIFY(!s.at(3).isValid());
}

// After the transcript: the speaker's longest clean line, capped at 15 s, on the best-overlapping cluster's
// track (failing that the stored sample's track); by participant id or speaker key; none without a record.
void ParticipantServiceTest::sampleAfterTranscript()
{
    Meeting m = makeMeeting();
    writeTranscript(m, {{0, 900, "S1"}, {2000, 30000, "S1"}, {31000, 36000, "S1"}, {40000, 44000, "S2"},
                        {50000, 52000, "S3"}});
    Participant v1 = voiceRow(QStringLiteral("v1"), {}, QStringLiteral("loopback"), 0.8);
    v1.rawSpeakerIds = {QStringLiteral("S1")};
    Participant v2 = voiceRow(QStringLiteral("v2"), {}, QStringLiteral("mic"), 0.2, QStringLiteral("mic"),
                              QStringLiteral("track_mic.wav#40000-44000"));
    v2.rawSpeakerIds = {QStringLiteral("S2")};
    saveParticipants(m.id, {v1, v2});
    VoiceCluster mic, loop;
    mic.trackId = QStringLiteral("mic");
    mic.participantId = QStringLiteral("v1");
    mic.windows = {{0, 900}};
    loop.trackId = QStringLiteral("loop");
    loop.participantId = QStringLiteral("v1");
    loop.windows = {{2500, 20000}};
    m_analysis->clusters = {mic, loop};

    editing::ParticipantSample s = m_svc->participantSample(m.id, QStringLiteral("v1"));
    QCOMPARE(s.trackId, QStringLiteral("loop"));
    QCOMPARE(s.startMs, 2000);
    QCOMPARE(s.endMs, 2000 + editing::participantsample::kMaxSampleMs);
    QCOMPARE(m_svc->participantSample(m.id, QStringLiteral("S1")).startMs, 2000);   // by the speaker's key

    // The long line is noisy: the longest clean one wins.
    editing::SpeakerEditSession* ed = editorFor(m.id);
    QString longId;
    for (const editing::EditorUtterance& u : ed->utterances())
        if (u.startMs == 2000) longId = u.id;
    QVERIFY(ed->setUtterancesNoisy({longId}, true));
    s = m_svc->participantSample(m.id, QStringLiteral("v1"));
    QCOMPARE(s.startMs, 31000);
    QCOMPARE(s.endMs, 36000);

    // v2 has no cluster: the stored sample's track. S3 has no record: nothing.
    s = m_svc->participantSample(m.id, QStringLiteral("v2"));
    QCOMPARE(s.trackId, QStringLiteral("mic"));
    QCOMPARE(s.startMs, 40000);
    QCOMPARE(s.endMs, 44000);
    QVERIFY(!m_svc->participantSample(m.id, QStringLiteral("S3")).isValid());
}

void ParticipantServiceTest::rowOperationErrorPaths()
{
    const QString nope = QStringLiteral("nope");
    QVERIFY(!m_svc->nameParticipant(nope, QStringLiteral("v1"), PersonChoice::create(QStringLiteral("X"))));
    QVERIFY(!m_svc->mergeParticipantInto(nope, QStringLiteral("a"), QStringLiteral("b")));
    QVERIFY(!m_svc->confirmParticipant(nope, QStringLiteral("a"), true));
    QVERIFY(!m_svc->participantSample(nope, QStringLiteral("a")).isValid());
    const Meeting m = makeMeeting();
    saveParticipants(m.id, {voiceRow(QStringLiteral("v1"), {}, QStringLiteral("mic"), 0.5)});
    QSignalSpy people(m_svc.get(), &ParticipantService::peopleChanged);
    QVERIFY(!m_svc->nameParticipant(m.id, QStringLiteral("nincs"), PersonChoice::create(QStringLiteral("X"))));
    QCOMPARE(people.count(), 0);                                    // no person created for a missing row
    QVERIFY(m_people->idForName(QStringLiteral("X")).isNull());
    QVERIFY(!m_svc->nameParticipant(m.id, QStringLiteral("v1"), PersonChoice()));   // anonymous choice
    QVERIFY(!m_svc->mergeParticipantInto(m.id, QStringLiteral("v1"), QStringLiteral("v1")));
    QVERIFY(!m_svc->mergeParticipantInto(m.id, QStringLiteral("v1"), QStringLiteral("nincs")));
    QVERIFY(!m_svc->confirmParticipant(m.id, QStringLiteral("nincs"), true));
    QVERIFY(!m_svc->confirmParticipant(m.id, QStringLiteral("v1"), false));         // already not confirmed
    QVERIFY(!m_undo->forMeeting(m.id)->canUndo());
}

QTEST_MAIN(ParticipantServiceTest)
#include "test_participant_service.moc"
