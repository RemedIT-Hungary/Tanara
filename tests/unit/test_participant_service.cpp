// editing::ParticipantService on its own (without AppController), with a fake ParticipantAnalysisPort (the
// voice-analysis side) and real stores / editor sessions / shared undo (editing::MeetingUndoRegistry):
// adding / removing a participant as shared undo steps, approve / skip / "Only I spoke" (which drops a
// running analysis's result), the approval-pending banner, the speaker -> track binding before the
// transcript, the unheard participants, and committing a voice identification. Error paths: unknown meeting,
// no own name, nothing to commit. Made-up names, ISOLATED TANARA_HOME; no Soniox / LLM / network / ffmpeg.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/editing/participants/ParticipantService.h"
#include "tanara/editing/session/SpeakerEditor.h"
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

private:
    Meeting makeMeeting();
    void writeTranscript(Meeting& m, const QVector<std::tuple<qint64, qint64, QString>>& rows);
    editing::SpeakerEditor* editorFor(const QString& meetingId);

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<library::MeetingRepository> m_store;
    std::unique_ptr<library::PeopleStore> m_people;
    std::unique_ptr<library::VoiceprintStore> m_prints;
    std::unique_ptr<library::PeopleService> m_peopleSvc;
    std::unique_ptr<editing::MeetingUndoRegistry> m_undo;
    std::unique_ptr<FakeAnalysis> m_analysis;
    std::unique_ptr<ParticipantService> m_svc;
    QHash<QString, QPointer<editing::SpeakerEditor>> m_editors;
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
        m_store.get(), nullptr, [this](const QString& id) -> editing::SpeakerEditor* { return m_editors.value(id); });
    m_analysis = std::make_unique<FakeAnalysis>();
    ParticipantService::Deps deps;
    deps.store = m_store.get();
    deps.people = m_people.get();
    deps.peopleService = m_peopleSvc.get();
    deps.undo = m_undo.get();
    deps.analysis = m_analysis.get();
    deps.editorFor = [this](const QString& id) { return editorFor(id); };
    deps.existingEditor = [this](const QString& id) -> editing::SpeakerEditor* { return m_editors.value(id); };
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

editing::SpeakerEditor* ParticipantServiceTest::editorFor(const QString& meetingId)
{
    if (editing::SpeakerEditor* ed = m_editors.value(meetingId)) return ed;
    if (m_store->load(meetingId).id.isEmpty()) return nullptr;
    auto* ed = new editing::SpeakerEditor(m_store.get(), m_people.get(), m_prints.get(), meetingId);
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
    QCOMPARE(ps.first().evidence.value(0).text, QStringLiteral("nincs lenyomata"));

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
    QCOMPARE(m.approval->candidates.at(1).group, ParticipantGroup::Sure);   // the dialog's person is manual
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

QTEST_MAIN(ParticipantServiceTest)
#include "test_participant_service.moc"
