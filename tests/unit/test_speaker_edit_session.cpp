// SpeakerEditSession on its own: the line-level operations, their persistence
// (library::TranscriptRepository) and undo / redo (SpeakerUndoStack). In a temporary folder with made-up
// data; no embeddings (no model, no audio).
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/domain/Meeting.h"
#include "tanara/domain/Overlay.h"
#include "tanara/editing/session/SpeakerEditSession.h"
#include "tanara/editing/undo/SpeakerUndoStack.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/library/store/VoiceprintStore.h"

#include <algorithm>
#include <memory>

using namespace tanara;

namespace {

const QString kS1 = QStringLiteral("Beszélő 1");
const QString kS2 = QStringLiteral("Beszélő 2");

// 6 lines: Speaker 1 on the even ones, Speaker 2 on the odd ones.
QString uid(int row) { return QStringLiteral("u%1").arg(qint64(row) * 5000); }
QStringList uids(std::initializer_list<int> rows)
{
    QStringList out;
    for (int r : rows) out << uid(r);
    return out;
}

struct Fixture {
    QTemporaryDir dir;
    std::unique_ptr<library::MeetingRepository> store;
    std::unique_ptr<library::PeopleStore> people;
    std::unique_ptr<library::VoiceprintStore> prints;
    Meeting meeting;
    PersonId anna;

    Fixture()
    {
        store = std::make_unique<library::MeetingRepository>(dir.filePath(QStringLiteral("rec")),
                                                             dir.filePath(QStringLiteral("meta")));
        people = std::make_unique<library::PeopleStore>(dir.filePath(QStringLiteral("meta/people.json")));
        prints = std::make_unique<library::VoiceprintStore>(dir.filePath(QStringLiteral("meta/vp.json")));
        meeting = store->createMeeting(QStringLiteral("Szerkesztés"));
        QJsonArray segs;
        for (int i = 0; i < 6; ++i)
            segs.append(QJsonObject{{"startMs", double(i * 5000)}, {"endMs", double(i * 5000 + 4000)},
                                    {"speaker", i % 2 == 0 ? kS1 : kS2}, {"text", QStringLiteral("Sor %1").arg(i)}});
        QFile f(library::TranscriptRepository::segmentsPath(meeting.folder));
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(segs).toJson());
        f.close();
        meeting.hasTranscript = true;
        anna = people->ensure(QStringLiteral("Anna"));
        meeting.speakerMap = {{kS1, anna}};
        store->saveMeeting(meeting);
    }

    std::unique_ptr<editing::SpeakerEditSession> session()
    {
        return std::make_unique<editing::SpeakerEditSession>(store.get(), people.get(), prints.get(), meeting.id);
    }

    SpeakerOverlay overlay() const { return library::TranscriptRepository::readOverlay(meeting.folder); }
};

} // namespace

class SpeakerEditSessionTest : public QObject {
    Q_OBJECT
private slots:
    void readModel()
    {
        Fixture fx;
        auto s = fx.session();
        QVERIFY(s->hasTranscript());
        QCOMPARE(s->utteranceCount(), 6);
        QCOMPARE(s->indexOf(uid(3)), 3);
        QCOMPARE(s->utterance(uid(2)).speakerKey, kS1);
        const QVector<editing::EditorSpeaker> sp = s->speakers();
        QCOMPARE(sp.size(), 2);
        QCOMPARE(sp[0].displayName, QStringLiteral("Anna"));
        QVERIFY(sp[1].anonymous);
        QCOMPARE(sp[0].utteranceCount, 3);
        QVERIFY(!s->canUndo());
        QVERIFY(!s->hasEmbeddings());
        QCOMPARE(s->pendingEmbeddings().size(), 6);     // every line is long enough, none has a vector
    }

    void move_persists_undo_redo()
    {
        Fixture fx;
        auto s = fx.session();
        QSignalSpy changed(s.get(), &editing::SpeakerEditSession::utterancesChanged);
        QSignalSpy undoState(s.get(), &editing::SpeakerEditSession::undoStateChanged);

        QVERIFY(s->moveUtterances(uids({1, 3}), kS1));
        QVERIFY(!s->moveUtterances(uids({1}), kS1));      // already there: no step
        QCOMPARE(s->utterance(uid(1)).speakerKey, kS1);
        QVERIFY(s->utterance(uid(1)).manuallyCorrected);
        QCOMPARE(changed.last().at(0).toStringList(), uids({1, 3}));
        QVERIFY(undoState.count() > 0);
        QVERIFY(s->canUndo());
        QVERIFY(!s->undoText().isEmpty());
        // Persisted at once (the overlay through TranscriptRepository).
        QCOMPARE(fx.overlay().utterances.value(uid(1)).speaker, kS1);
        const QVector<quint64> serials = s->undoSerials();
        QCOMPARE(serials.size(), 1);

        s->undo();
        QCOMPARE(s->utterance(uid(1)).speakerKey, kS2);
        QVERIFY(!fx.overlay().utterances.contains(uid(1)));
        QVERIFY(!s->canUndo());
        QVERIFY(s->canRedo());
        QVERIFY(s->undoSerials().isEmpty());

        s->redo();
        QCOMPARE(s->utterance(uid(3)).speakerKey, kS1);
        QCOMPARE(s->undoSerials(), serials);              // the same step keeps its number
        QVERIFY(!s->canRedo());

        // A new session reads the persisted state, with an empty undo stack.
        s.reset();
        auto again = fx.session();
        QCOMPARE(again->utterance(uid(1)).speakerKey, kS1);
        QVERIFY(!again->canUndo());
    }

    void merge_confirm_ignore_participants()
    {
        Fixture fx;
        auto s = fx.session();
        QVERIFY(s->mergeSpeakers(kS2, kS1));
        QCOMPARE(s->speakers().size(), 1);
        QCOMPARE(s->speaker(kS1).utteranceCount, 6);
        s->undo();
        QCOMPARE(s->speakers().size(), 2);

        QVERIFY(s->confirmUtterances(uids({0})));
        QVERIFY(s->utterance(uid(0)).confirmed);
        QVERIFY(s->setUtterancesIgnored(uids({2}), true));
        QVERIFY(s->utterance(uid(2)).ignored);
        QVERIFY(s->setUtterancesNoisy(uids({4}), true));
        QVERIFY(s->utterance(uid(4)).noisy);

        const QString added = s->addParticipant();
        QVERIFY(!added.isEmpty());
        QCOMPARE(s->speakers().size(), 3);
        QVERIFY(s->removeParticipant(added));
        QCOMPARE(s->speakers().size(), 2);

        // Five steps (the undone merge left with the redo side); undone one by one back to the start.
        QVERIFY(!s->canRedo());
        int steps = 0;
        while (s->canUndo()) { s->undo(); ++steps; }
        QCOMPARE(steps, 5);
        QVERIFY(!s->utterance(uid(0)).confirmed);
        QVERIFY(!s->utterance(uid(2)).ignored);
        QVERIFY(fx.overlay().utterances.isEmpty());
    }

    void reassignToNewPerson_undoRemovesThePerson()
    {
        Fixture fx;
        auto s = fx.session();
        QSignalSpy peopleSpy(s.get(), &editing::SpeakerEditSession::peopleChanged);
        QVERIFY(s->reassignSpeaker(kS2, PersonChoice::create(QStringLiteral("Béla"))));
        QCOMPARE(s->speaker(kS2).displayName, QStringLiteral("Béla"));
        QVERIFY(!peopleSpy.isEmpty());
        const PersonId bela = s->speaker(kS2).personId;
        QVERIFY(!bela.isNull());
        QCOMPARE(fx.store->load(fx.meeting.id).speakerMap.value(kS2), bela);
        s->undo();
        QVERIFY(s->speaker(kS2).anonymous);
        QVERIFY(!fx.store->load(fx.meeting.id).speakerMap.contains(kS2));
        QVERIFY(s->personDisplayName(bela).isEmpty());   // the person created by the step is gone
        s->redo();
        QCOMPARE(s->speaker(kS2).displayName, QStringLiteral("Béla"));
    }

    void recheckMarks_oneStep()
    {
        Fixture fx;
        auto s = fx.session();
        QVector<editing::SpeakerEditSession::RecheckMark> marks;
        marks.append({1, true, kS1});
        marks.append({3, false, QString()});
        QVERIFY(s->applyRecheckMarks(marks, [] { return QStringLiteral("Recheck"); }));
        QCOMPARE(s->undoText(), QStringLiteral("Recheck"));
        const editing::EditorUtterance u = s->utterance(uid(1));
        QVERIFY(u.rechecked && u.uncertain);
        QCOMPARE(u.likelySpeakerKey, kS1);
        QCOMPARE(s->uncertainUtteranceIds(), uids({1}));
        // The same marks again change nothing: no step.
        QVERIFY(!s->applyRecheckMarks(marks, [] { return QStringLiteral("Recheck"); }));
        QCOMPARE(s->undoSerials().size(), 1);
        // A confirmation releases the flag.
        QVERIFY(s->confirmUtterances(uids({1})));
        QVERIFY(!s->utterance(uid(1)).uncertain);
        s->undo();
        s->undo();
        QCOMPARE(s->uncertainCount(), 0);
    }

    void reload_dropsTheUndoStack()
    {
        Fixture fx;
        auto s = fx.session();
        QSignalSpy about(s.get(), &editing::SpeakerEditSession::aboutToReload);
        QSignalSpy reloaded(s.get(), &editing::SpeakerEditSession::reloaded);
        QVERIFY(s->moveUtterances(uids({1}), kS1));
        s->refreshFromDisk();                             // nothing changed outside: the stack stays
        QVERIFY(s->canUndo());
        QVERIFY(about.isEmpty());

        // An external overlay change (another writer): reload, the stack is dropped.
        SpeakerOverlay ov = fx.overlay();
        ov.utterances[uid(5)].confirmed = true;
        QVERIFY(library::TranscriptRepository::writeOverlay(fx.meeting.folder, ov));
        s->refreshFromDisk();
        QCOMPARE(about.count(), 1);
        QCOMPARE(about.last().at(0).toBool(), false);
        QCOMPARE(reloaded.count(), 1);
        QVERIFY(!s->canUndo());
        QVERIFY(s->utterance(uid(5)).confirmed);

        s->reloadTranscript();
        QCOMPARE(about.last().at(0).toBool(), true);
        QCOMPARE(reloaded.count(), 2);

        QSignalSpy closing(s.get(), &editing::SpeakerEditSession::closing);
        s.reset();
        QCOMPARE(closing.count(), 1);
    }

    void undoStack_capAndSerials()
    {
        editing::SpeakerUndoStack st;
        QVERIFY(!st.canUndo());
        for (int i = 0; i < editing::SpeakerUndoStack::kMaxSteps + 5; ++i) {
            editing::SpeakerEditCommand c;
            c.text = QString::number(i);
            st.push(c);
        }
        const QVector<quint64> serials = st.undoSerials();
        QCOMPARE(serials.size(), editing::SpeakerUndoStack::kMaxSteps);
        QVERIFY(std::is_sorted(serials.cbegin(), serials.cend()));
        QCOMPARE(st.undoText(), QString::number(editing::SpeakerUndoStack::kMaxSteps + 4));

        const editing::SpeakerEditCommand top = st.takeUndo();
        QCOMPARE(top.serial, serials.last());
        QVERIFY(st.canRedo());
        QCOMPARE(st.redoText(), top.text);
        const editing::SpeakerEditCommand again = st.takeRedo();
        QCOMPARE(again.serial, top.serial);
        QCOMPARE(st.undoSerials(), serials);

        st.takeUndo();
        editing::SpeakerEditCommand fresh;
        st.push(fresh);                                   // a new step clears the redo side
        QVERIFY(!st.canRedo());
        QVERIFY(st.undoSerials().last() > serials.last());
        st.clear();
        QVERIFY(!st.canUndo() && !st.canRedo());
    }
};

QTEST_GUILESS_MAIN(SpeakerEditSessionTest)
#include "test_speaker_edit_session.moc"
