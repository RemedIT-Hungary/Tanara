// The meeting's shared undo stack (editing/undo/MeetingUndo.h) in AppController: the editor, the
// meeting's tag steps and the participant operations in chronological order; another
// meeting's tag step does not come here (and while it is on top, it skips the tag entry);
// "Only I spoke" swallows the editor's binding step; permanent deletion of a participant and
// permanent rejection of a suggested person for one meeting (tags.json). Made-up names,
// ISOLATED TANARA_HOME; without Soniox / LLM / network.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/app/AppController.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/editing/undo/MeetingUndo.h"
#include "tanara/editing/session/SpeakerEditSession.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/tags/TagService.h"

#include <memory>

using namespace tanara;
using Source = editing::MeetingUndoService::Source;

namespace {

const QString kSelf = QStringLiteral("Kovács Lilla");

QJsonObject readJson(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool hasParticipant(const Meeting& m, const PersonId& person)
{
    return std::any_of(m.participants.cbegin(), m.participants.cend(),
                       [&](const Participant& p) { return p.personId == person; });
}

} // namespace

class MeetingUndoTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();

    void orderAcrossSources();
    void editorUndoRedoAndTagGroups();
    void soloAbsorbsEditorStep();
    void removeParticipantDurableWithUndo();
    void rejectSuggestedPersonForMeeting();
    void trackExcludeIncludeUndo();

private:
    Meeting makeMeeting(const QString& title, bool transcript = true);
    QString tagged(const QString& title, const QString& tagId, const QStringList& people);
    // A person by name (people.json; created if missing).
    PersonId pid(const QString& name) { return m_app->people()->ensure(name); }
    PersonChoice choice(const QString& name) { return m_app->people()->choiceFor(name); }

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<app::AppController> m_app;
};

void MeetingUndoTest::initTestCase()
{
    qputenv("TANARA_CLOUD", "off");
}

void MeetingUndoTest::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    qputenv("TANARA_HOME", m_home->path().toUtf8());
    m_app = std::make_unique<app::AppController>();
    QVERIFY(m_app->store()->metadataDir().startsWith(m_home->path()));
    m_app->setUserSpeakerName(kSelf);
}

void MeetingUndoTest::cleanup()
{
    m_app.reset();
    m_home.reset();
}

// A transcript with two raw speakers (Speaker 1: 0-3 and 5-8 s, Speaker 2: 3-5 s), without audio.
Meeting MeetingUndoTest::makeMeeting(const QString& title, bool transcript)
{
    Meeting m = m_app->store()->createMeeting(title);
    m.durationMs = 8000;
    if (transcript) {
        QJsonArray toks, segs;
        const struct { qint64 s, e; const char* spk; } rows[] = {
            {0, 3000, "Beszélő 1"}, {3000, 5000, "Beszélő 2"}, {5000, 8000, "Beszélő 1"}};
        for (const auto& r : rows) {
            toks.append(QJsonObject{{"text", "x"}, {"speaker", QString::fromUtf8(r.spk)}, {"startMs", double(r.s)},
                                    {"endMs", double(r.e)}, {"confidence", 1.0}, {"trackId", "mixdown"}});
            segs.append(QJsonObject{{"startMs", double(r.s)}, {"endMs", double(r.e)},
                                    {"speaker", QString::fromUtf8(r.spk)}, {"text", "x"}});
        }
        QFile tf(QDir(m.folder).filePath("transcript.tokens.json"));
        if (tf.open(QIODevice::WriteOnly))
            tf.write(QJsonDocument(QJsonObject{{"language", "hu"}, {"tokens", toks}}).toJson());
        QFile sf(QDir(m.folder).filePath("transcript.segments.json"));
        if (sf.open(QIODevice::WriteOnly)) sf.write(QJsonDocument(segs).toJson());
        m.hasTranscript = true;
    }
    m_app->store()->saveMeeting(m);
    return m;
}

QString MeetingUndoTest::tagged(const QString& title, const QString& tagId, const QStringList& people)
{
    Meeting m = m_app->store()->createMeeting(title);
    m.startedAt = QDateTime(QDate(2026, 9, 1), QTime(10, 0));
    for (int i = 0; i < people.size(); ++i)
        m.speakerMap.insert(QStringLiteral("Beszélő %1").arg(i + 1), pid(people.at(i)));
    m.tagIds = {tagId};
    m_app->store()->saveMeeting(m);
    return m.id;
}

void MeetingUndoTest::orderAcrossSources()
{
    const Meeting m = makeMeeting("Közös verem");
    const Meeting other = makeMeeting("Másik megbeszélés", false);
    editing::MeetingUndoService* u = m_app->meetingUndo(m.id);
    QVERIFY(u);
    QCOMPARE(m_app->meetingUndo(m.id), u);
    QVERIFY(!m_app->meetingUndo(QStringLiteral("nincs-ilyen")));
    editing::SpeakerEditSession* ed = m_app->editSession(m.id);
    QVERIFY(ed);
    QCOMPARE(u->editSession(), ed);
    library::TagService* tags = m_app->tags();
    QVERIFY(!u->canUndo());
    QSignalSpy changed(u, &editing::MeetingUndoService::changed);

    // 1. a tag on the meeting, 2. a speaker fix, 3. a manual participant, 4. a description.
    const Tag t = tags->addTag(m.id, QStringLiteral("Projekt"));
    QVERIFY(!t.id.isEmpty());
    QCOMPARE(u->count(), 1);
    QCOMPARE(u->undoSource(), Source::Tags);
    const QString key = ed->speakers().first().key;
    QVERIFY(ed->reassignSpeaker(key, choice(QStringLiteral("Fehér Gábor"))));
    QCOMPARE(u->undoSource(), Source::Speakers);
    QVERIFY(!m_app->addParticipant(m.id, choice(QStringLiteral("Molnár Eszter"))).isEmpty());
    QCOMPARE(u->undoSource(), Source::Participants);
    m_app->setMeetingContextNote(m.id, QStringLiteral("Heti egyeztetés"));
    m_app->setMeetingContextNote(m.id, QStringLiteral("Heti egyeztetés"));   // unchanged: no step
    QCOMPARE(u->count(), 4);
    QCOMPARE(u->undoText(), QStringLiteral("Leírás módosítva"));
    QVERIFY(changed.count() >= 4);

    // Another meeting's tag step does not come here.
    tags->addTag(other.id, QStringLiteral("Más"));
    QCOMPARE(u->count(), 4);

    QSignalSpy undone(u, &editing::MeetingUndoService::undone);
    QVERIFY(u->undo());
    QVERIFY(m_app->store()->load(m.id).contextNote.isEmpty());
    QCOMPARE(undone.last().at(0).toString(), QStringLiteral("Leírás módosítva"));
    QVERIFY(u->undo());
    QVERIFY(!hasParticipant(m_app->store()->load(m.id), pid(QStringLiteral("Molnár Eszter"))));
    QCOMPARE(u->undoSource(), Source::Speakers);
    QVERIFY(u->undo());
    QVERIFY(!m_app->store()->load(m.id).speakerMap.values().contains(pid(QStringLiteral("Fehér Gábor"))));
    QVERIFY(!ed->canUndo());

    // Above the tag entry in TagService sits the other meeting's step: it cannot be undone now.
    QCOMPARE(u->count(), 1);
    QVERIFY(!u->canUndo());
    QVERIFY(!u->undo());
    tags->undo();   // the other meeting's (e.g. from the Tags window)
    QVERIFY(u->canUndo());
    QCOMPARE(u->undoSource(), Source::Tags);
    QVERIFY(u->undo());
    QVERIFY(tags->tagsOf(m.id).isEmpty());
    QVERIFY(!u->canUndo());
    QCOMPARE(u->count(), 0);
}

void MeetingUndoTest::editorUndoRedoAndTagGroups()
{
    const Meeting m = makeMeeting("Szerkesztő");
    editing::SpeakerEditSession* ed = m_app->editSession(m.id);
    editing::MeetingUndoService* u = m_app->meetingUndo(m.id);
    QVERIFY(ed && u);
    library::TagService* tags = m_app->tags();

    QVERIFY(ed->reassignSpeaker(ed->speakers().first().key, choice(QStringLiteral("Varga Árpád"))));
    QCOMPARE(u->count(), 1);
    ed->undo();   // directly in the editor (e.g. the editor's own Ctrl+Z)
    QCOMPARE(u->count(), 0);
    ed->redo();   // the restored step is the newest
    QCOMPARE(u->count(), 1);
    QCOMPARE(u->undoSource(), Source::Speakers);

    // A person tag from the Overview: the group marked for the meeting belongs here.
    tags->beginGroup(QStringLiteral("Címke a résztvevőn"), m.id);
    tags->addPersonTag(pid(QStringLiteral("Varga Árpád")), QStringLiteral("Csapat"));
    tags->endGroup();
    QCOMPARE(u->count(), 2);
    QCOMPARE(u->undoSource(), Source::Tags);
    QCOMPARE(u->undoText(), QStringLiteral("Címke a résztvevőn"));

    // A person tag without a marker (e.g. from the People window): does not belong here, and
    // while it is on top the tag entry is skipped; the editor step can be undone.
    tags->addPersonTag(pid(QStringLiteral("Varga Árpád")), QStringLiteral("Másik"));
    QCOMPARE(u->count(), 2);
    QCOMPARE(u->undoSource(), Source::Speakers);
    tags->undo();
    QCOMPARE(u->undoSource(), Source::Tags);
    QVERIFY(u->undo());
    QVERIFY(tags->tagsOfPerson(pid(QStringLiteral("Varga Árpád"))).isEmpty());
    QVERIFY(u->undo());
    QVERIFY(m_app->store()->load(m.id).speakerMap.isEmpty());

    // Closing the editor: its editor entries drop out.
    QVERIFY(ed->reassignSpeaker(ed->speakers().first().key, choice(QStringLiteral("Varga Árpád"))));
    QCOMPARE(u->count(), 1);
    QSignalSpy changed(u, &editing::MeetingUndoService::changed);
    m_app->closeEditSession(m.id);
    QTRY_COMPARE(u->count(), 0);
}

void MeetingUndoTest::soloAbsorbsEditorStep()
{
    const Meeting m = makeMeeting("Csak én");
    editing::MeetingUndoService* u = m_app->meetingUndo(m.id);
    editing::SpeakerEditSession* ed = m_app->editSession(m.id);
    QVERIFY(u && ed);

    QVERIFY(m_app->setSoloMeeting(m.id));
    Meeting mm = m_app->store()->load(m.id);
    QVERIFY(mm.approval && mm.approval->solo);
    QVERIFY(hasParticipant(mm, pid(kSelf)));
    QVERIFY(ed->canUndo());   // the bindings went through the editor...
    QCOMPARE(u->count(), 1);  // ...but in the shared stack it is one step
    QCOMPARE(u->undoText(), QStringLiteral("Csak én beszéltem"));
    for (const editing::EditorSpeaker& s : ed->speakers())
        if (s.utteranceCount > 0) QCOMPARE(s.personId, pid(kSelf));

    QVERIFY(u->undo());
    mm = m_app->store()->load(m.id);
    QVERIFY(!mm.approval.has_value());
    QVERIFY(mm.participants.isEmpty());
    QVERIFY(mm.speakerMap.isEmpty());
    QVERIFY(!ed->canUndo());
    QCOMPARE(u->count(), 0);
}

void MeetingUndoTest::removeParticipantDurableWithUndo()
{
    const Meeting m = makeMeeting("Törlés");
    editing::MeetingUndoService* u = m_app->meetingUndo(m.id);
    QVERIFY(u);
    const QString cili = m_app->addParticipant(m.id, choice(QStringLiteral("Molnár Eszter")));
    const QString dora = m_app->addParticipant(m.id, choice(QStringLiteral("Szabó Bence")));
    QVERIFY(m_app->approveParticipants(m.id, m_app->participants(m.id)));
    // Bence also on a speaker's name (from the transcript editor).
    m_app->renameSpeaker(m.id, QStringLiteral("Beszélő 2"), choice(QStringLiteral("Szabó Bence")), /*enroll*/ false);
    QCOMPARE(m_app->store()->load(m.id).speakerMap.value("Beszélő 2"), pid(QStringLiteral("Szabó Bence")));
    const int before = u->count();

    QVERIFY(m_app->removeParticipant(m.id, dora));
    QCOMPARE(u->count(), before + 1);   // the editor's anonymizing step is swallowed
    QCOMPARE(u->undoSource(), Source::Participants);
    QCOMPARE(u->undoText(), QStringLiteral("Résztvevő eltávolítva: Szabó Bence"));
    Meeting mm = m_app->store()->load(m.id);
    QVERIFY(!hasParticipant(mm, pid(QStringLiteral("Szabó Bence"))));
    QVERIFY(hasParticipant(mm, pid(QStringLiteral("Molnár Eszter"))));
    QVERIFY(mm.approval);
    for (const ParticipantApprovalEntry& e : mm.approval->candidates) QVERIFY(e.participantId != dora);
    QVERIFY(!mm.speakerMap.values().contains(pid(QStringLiteral("Szabó Bence"))));
    QVERIFY(!m_app->removeParticipant(m.id, dora));
    QVERIFY(!m_app->removeParticipant(QStringLiteral("nincs"), cili));

    // Undo: the participant, its approval entry and the speaker's name all come back.
    QVERIFY(u->undo());
    mm = m_app->store()->load(m.id);
    QVERIFY(hasParticipant(mm, pid(QStringLiteral("Szabó Bence"))));
    bool entry = false;
    for (const ParticipantApprovalEntry& e : mm.approval->candidates) entry = entry || e.participantId == dora;
    QVERIFY(entry);
    QCOMPARE(mm.speakerMap.value("Beszélő 2"), pid(QStringLiteral("Szabó Bence")));

    // Persistent: not there after a restart either.
    QVERIFY(m_app->removeParticipant(m.id, dora));
    m_app.reset();
    m_app = std::make_unique<app::AppController>();
    mm = m_app->store()->load(m.id);
    QVERIFY(!hasParticipant(mm, pid(QStringLiteral("Szabó Bence"))));
    QVERIFY(hasParticipant(mm, pid(QStringLiteral("Molnár Eszter"))));
    QCOMPARE(m_app->participants(m.id).size(), 1);
}

void MeetingUndoTest::rejectSuggestedPersonForMeeting()
{
    library::TagService* tags = m_app->tags();
    const Tag nord = tags->create(QStringLiteral("Nordvik"));
    const QStringList crew{QStringLiteral("Fehér Gábor"), QStringLiteral("Varga Árpád"),
                           QStringLiteral("Molnár Eszter"), QStringLiteral("Szabó Bence"), kSelf};
    for (int i = 1; i <= 3; ++i) tagged(QStringLiteral("Nordvik %1").arg(i), nord.id, crew);
    const QString target = tagged(QStringLiteral("Új Nordvik"), nord.id, {});
    const QString second = tagged(QStringLiteral("Másik Nordvik"), nord.id, {});
    tags->refreshPersonStatsNow();

    // At most 3 suggestions; the own person and those already participants are left out.
    QVector<library::PersonTagStat> sugg = tags->suggestPeopleForMeeting(target);
    QCOMPARE(sugg.size(), 3);
    for (const library::PersonTagStat& s : sugg) QVERIFY(s.personId != pid(kSelf));
    QCOMPARE(tags->suggestPeopleForMeeting(target, {}, 0).size(), 4);
    QCOMPARE(tags->suggestPeopleForMeeting(target, {pid(QStringLiteral("fehér gábor"))}, 0).size(), 3);

    // x on the suggested person: only for this meeting, persistent, in the shared stack.
    editing::MeetingUndoService* u = m_app->meetingUndo(target);
    QVERIFY(u);
    const PersonId gabor = pid(QStringLiteral("Fehér Gábor"));
    tags->rejectPersonForMeeting(gabor, target);
    QVERIFY(tags->isPersonRejectedForMeeting(gabor, target));
    QVERIFY(!tags->isPersonRejectedForMeeting(gabor, second));
    QCOMPARE(tags->suggestPeopleForMeeting(target, {}, 0).size(), 3);
    for (const library::PersonTagStat& s : tags->suggestPeopleForMeeting(target, {}, 0)) QVERIFY(s.personId != gabor);
    QCOMPARE(tags->suggestPeopleForMeeting(second, {}, 0).size(), 4);
    QCOMPARE(u->count(), 1);
    QCOMPARE(u->undoSource(), Source::Tags);

    // In tags.json {"personId", "meetingId"} without a tag; a new instance sees it too.
    const QString path = QDir(m_app->store()->metadataDir()).filePath("tags.json");
    bool stored = false;
    for (const QJsonValue& v : readJson(path).value("rejected").toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value("personId").toString() == gabor.toString() && o.value("meetingId").toString() == target
            && !o.contains("tagId"))
            stored = true;
    }
    QVERIFY(stored);
    {
        library::TagService fresh(m_app->store(), path);
        QVERIFY(fresh.isPersonRejectedForMeeting(gabor, target));
    }

    // Undo from the shared stack.
    QVERIFY(u->undo());
    QVERIFY(!tags->isPersonRejectedForMeeting(gabor, target));
    QCOMPARE(tags->suggestPeopleForMeeting(target, {}, 0).size(), 4);

    // Deleting the meeting removes the rejection too.
    tags->rejectPersonForMeeting(gabor, target);
    const int n = tags->rejectedCount();
    m_app->deleteMeeting(target);
    QCOMPARE(tags->rejectedCount(), n - 1);
}

// "Exclude" / "Include" in the shared stack (the audio files are missing: the mixdown is not
// made, but restoring the track state is independent of that).
void MeetingUndoTest::trackExcludeIncludeUndo()
{
    Meeting m = makeMeeting("Sávok", false);
    for (const char* id : {"a", "b"}) {
        Track t;
        t.id = QString::fromLatin1(id);
        t.deviceName = t.id;
        t.file = QStringLiteral("track_%1.wav").arg(t.id);
        t.active = true;
        m.tracks.append(t);
    }
    m_app->store()->saveMeeting(m);
    editing::MeetingUndoService* u = m_app->meetingUndo(m.id);
    QVERIFY(u);
    QSignalSpy tracks(m_app.get(), &app::AppController::tracksChanged);

    m_app->excludeTrack(m.id, "b");
    QVERIFY(!m_app->store()->load(m.id).tracks.at(1).included());
    QCOMPARE(u->undoText(), QStringLiteral("Sáv kivéve"));
    m_app->excludeTrack(m.id, "b");   // already excluded: no new step
    QCOMPARE(u->count(), 1);

    tracks.clear();
    QVERIFY(u->undo());
    const Meeting back = m_app->store()->load(m.id);
    QVERIFY(back.tracks.at(1).included());
    QVERIFY(back.tracks.at(1).excludedReason.isEmpty());
    QVERIFY(back.mixdownDirty);
    QVERIFY(tracks.count() >= 1);
    QCOMPARE(u->count(), 0);
}

QTEST_GUILESS_MAIN(MeetingUndoTest)
#include "test_meeting_undo.moc"
