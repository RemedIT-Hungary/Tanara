// The transcript editor's view models (TranscriptEditorViewModel + TranscriptListModel +
// PersonListModel) on a REAL tanara::SpeakerEditor, in a temporary folder, with a fake
// embedder: move, multiple lines, merge, undo / redo, accepting a suggestion, filter, search,
// collapsing tracks, voiceprint. The point: moving one line NEVER resets the list model. Runs
// without a display; it touches no user data.
#include "PersonListModel.h"
#include "TranscriptEditorViewModel.h"
#include "TranscriptListModel.h"
#include "transcript_fixture.h"

#include <QSignalSpy>
#include <QtTest>

using namespace tanara;
using namespace tanara_qml;
using namespace transcript_fixture;

using Role = TranscriptListModel::Role;

namespace {

QVariant cell(const TranscriptEditorViewModel& vm, int row, Role role)
{
    return vm.rows()->data(vm.rows()->index(row), role);
}

QString speakerOf(const TranscriptEditorViewModel& vm, int row)
{
    return cell(vm, row, Role::SpeakerKeyRole).toString();
}

// Waiting for the audio analysis (background thread).
bool waitVoice(const TranscriptEditorViewModel& vm)
{
    return QTest::qWaitFor([&] { return !vm.embeddingRunning() && vm.editor()->embeddingsComplete(); }, 10000);
}

// The speaker's voiceprint indicator on the overview ("has" | "none" | "anonymous").
QString overviewVoiceprint(const TranscriptEditorViewModel& vm, const QString& key)
{
    for (const QVariant& row : vm.overview())
        if (row.toMap().value(QStringLiteral("key")).toString() == key)
            return row.toMap().value(QStringLiteral("voiceprint")).toString();
    return QStringLiteral("?");
}

int laneOf(const TranscriptEditorViewModel& vm, const QString& key)
{
    const QVariantList lanes = vm.lanes();
    for (int i = 0; i < lanes.size(); ++i)
        if (lanes[i].toMap().value(QStringLiteral("key")).toString() == key) return i;
    return -1;
}

// ---- two SIMILAR voices (pairwise review) ----
// Anna ("Speaker 1") has 4 own lines plus 3 lines in Béla's voice; Béla ("Speaker 2") has 5 lines.
// similar: cos(Anna, Béla) = 0.85 (the "similar lines" guard takes this for one person);
// otherwise the two voices are orthogonal.
struct PairScene {
    QTemporaryDir dir;
    std::unique_ptr<library::MeetingRepository> store;
    std::unique_ptr<library::PeopleStore> people;
    std::unique_ptr<library::VoiceprintStore> prints;
    Meeting meeting;

    static QString id(int row) { return QStringLiteral("u%1").arg(row * 5000); }

    explicit PairScene(bool similar)
    {
        // 0 A a . 1 B b . 2 A a . 3 A b* . 4 B b . 5 A a . 6 A b* . 7 B b . 8 A a . 9 B b .
        // 10 A b* . 11 B b
        const QVector<QPair<QString, char>> rows{
            {kB1, 'a'}, {kB2, 'b'}, {kB1, 'a'}, {kB1, 'b'}, {kB2, 'b'}, {kB1, 'a'},
            {kB1, 'b'}, {kB2, 'b'}, {kB1, 'a'}, {kB2, 'b'}, {kB1, 'b'}, {kB2, 'b'}};
        store = std::make_unique<library::MeetingRepository>(dir.filePath(QStringLiteral("rec")),
                                               dir.filePath(QStringLiteral("meta")));
        people = std::make_unique<library::PeopleStore>(dir.filePath(QStringLiteral("meta/people.json")));
        prints = std::make_unique<library::VoiceprintStore>(dir.filePath(QStringLiteral("meta/vp.json")));
        meeting = store->createMeeting(QStringLiteral("Két hasonló hang"));
        QJsonArray segs;
        for (int i = 0; i < rows.size(); ++i)
            segs.append(QJsonObject{{QStringLiteral("startMs"), double(i * 5000)},
                                    {QStringLiteral("endMs"), double(i * 5000 + 4000)},
                                    {QStringLiteral("speaker"), rows[i].first},
                                    {QStringLiteral("text"), QStringLiteral("Sor %1").arg(i)}});
        QFile f(tanara::editing::speakeredit::segmentsPath(meeting.folder));
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(segs).toJson());
        f.close();
        meeting.hasTranscript = true;
        meeting.speakerMap = {{kB1, people->ensure(QStringLiteral("Anna"))}, {kB2, people->ensure(QStringLiteral("Béla"))}};
        store->saveMeeting(meeting);
        const QVector<float> a{1.0f, 0.0f, 0.0f};
        const QVector<float> b = similar ? QVector<float>{0.85f, 0.5268f, 0.0f} : QVector<float>{0.0f, 1.0f, 0.0f};
        const QVector<tanara::TranscriptLine> lines = tanara::editing::speakeredit::loadTranscriptLines(meeting.folder);
        processing::UtteranceEmbeddingCache c;
        c.fingerprint = tanara::editing::speakeredit::transcriptFingerprint(lines);
        for (int i = 0; i < lines.size(); ++i) c.set(QStringLiteral("campplus"), lines[i].id, rows[i].second == 'a' ? a : b);
        c.save(meeting.folder);
    }

    std::unique_ptr<editing::SpeakerEditor> editor()
    {
        auto ed = std::make_unique<editing::SpeakerEditor>(store.get(), people.get(), prints.get(), meeting.id);
        // The cache is complete: the factory is needed only for the "voice model exists" state (nothing runs).
        ed->setEmbedderFactory([] { return std::make_unique<FakeEmbedder>(); });
        return ed;
    }
};

} // namespace

class TestTranscriptEditorViewModel : public QObject {
    Q_OBJECT
private slots:
    void loadsRowsSpeakersAndOverview()
    {
        Fixture fx;
        fx.setSpeakerMap({{kB2, QStringLiteral("Béla")}});
        auto ed = fx.editor(/*withEmbedder*/ false);
        TranscriptEditorViewModel vm;
        QVERIFY(!vm.hasTranscript());
        vm.setEditor(ed.get());

        QVERIFY(vm.hasTranscript());
        QVERIFY(!vm.demo());
        QCOMPARE(vm.utteranceCount(), 18);
        QCOMPARE(vm.rows()->rowCount(), 18);
        QCOMPARE(vm.speakerCount(), 2);
        QCOMPARE(vm.lanes().size(), 2);
        QCOMPARE(vm.collapsedCount(), 0);
        QVERIFY(!vm.railVisible());                         // hidden by default

        QCOMPARE(cell(vm, 0, Role::KindRole).toString(), QStringLiteral("utterance"));
        QCOMPARE(cell(vm, 0, Role::SpeakerNameRole).toString(), kB1);
        QCOMPARE(cell(vm, 1, Role::SpeakerNameRole).toString(), QStringLiteral("Béla"));
        QCOMPARE(cell(vm, 1, Role::TimeLabelRole).toString(), QStringLiteral("00:06"));
        QCOMPARE(cell(vm, 0, Role::LaneRole).toInt(), 0);
        QCOMPARE(cell(vm, 1, Role::LaneRole).toInt(), 1);
        QCOMPARE(cell(vm, 1, Role::ColorIndexRole).toInt(), 1);
        // The name appears only at a speaker change: lines 6-7 are the same speaker (Speaker 1).
        QVERIFY(cell(vm, 0, Role::HeadRole).toBool());
        QVERIFY(cell(vm, 6, Role::HeadRole).toBool());
        QVERIFY(!cell(vm, 7, Role::HeadRole).toBool());
        QVERIFY(cell(vm, 0, Role::FirstRole).toBool());

        // Overview: one row per speaker, the segments are [x, w] pairs within 0..1.
        const QVariantList ov = vm.overview();
        QCOMPARE(ov.size(), 2);
        const QList<qreal> segs = ov[0].toMap().value(QStringLiteral("segments")).value<QList<qreal>>();
        QVERIFY(!segs.isEmpty() && segs.size() % 2 == 0);
        for (qreal v : segs) QVERIFY(v >= 0.0 && v <= 1.0);
        QCOMPARE(ov[0].toMap().value(QStringLiteral("pct")).toInt()
                     + ov[1].toMap().value(QStringLiteral("pct")).toInt(), 100);
        QCOMPARE(vm.durationMs(), 108000);

        QCOMPARE(TranscriptEditorViewModel::timeLabel(3723000), QStringLiteral("1:02:03"));
        QCOMPARE(vm.utteranceForTime(30000), 5);
        QCOMPARE(vm.rowForTime(107000), 17);
        QCOMPARE(vm.timeAtFraction(0.5), 54000);

        // Playback neighbor boundaries: the previous line's end, the next line's start.
        for (int r : {1, 5, 10}) {
            const int s = vm.rowStartMs(r), e = vm.rowEndMs(r);
            QVERIFY(vm.rowEndMs(r - 1) <= s && vm.rowStartMs(r + 1) >= e);   // they do not overlap
            const QVariantMap b = vm.playBounds(s, e);
            QCOMPARE(b.value(QStringLiteral("minStartMs")).toInt(), vm.rowEndMs(r - 1));
            QCOMPARE(b.value(QStringLiteral("maxEndMs")).toInt(), vm.rowStartMs(r + 1));
        }
        QCOMPARE(vm.playBounds(vm.rowStartMs(0), vm.rowEndMs(0)).value(QStringLiteral("minStartMs")).toInt(), -1);
        QCOMPARE(vm.playBounds(vm.rowStartMs(17), vm.rowEndMs(17)).value(QStringLiteral("maxEndMs")).toInt(), -1);
        // Overlapping neighbor: the boundary would fall inside the line, so the line's edges (no lead-in / tail).
        {
            const int s = vm.rowStartMs(2) - 1, e = vm.rowStartMs(3) + 1;
            const QVariantMap b = vm.playBounds(s, e);
            QCOMPARE(b.value(QStringLiteral("maxEndMs")).toInt(), e);
        }
    }

    // Two raw speakers identified as the same name: the UI tells them apart by the raw label
    // (nameDuplicate); the line picker's list and speakerInfo give the same map.
    void sameNameSpeakers_areMarkedDuplicate()
    {
        Fixture fx;
        fx.setSpeakerMap({{kB1, QStringLiteral("Tamás")}, {kB2, QStringLiteral("Tamás")}});
        auto ed = fx.editor(/*withEmbedder*/ false);
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());

        QCOMPARE(vm.speakerCount(), 2);
        for (const QVariant& v : vm.speakers()) {
            QVERIFY(v.toMap().value(QStringLiteral("nameDuplicate")).toBool());
            QVERIFY(!v.toMap().value(QStringLiteral("rawLabel")).toString().isEmpty());
        }
        QVERIFY(vm.speakerInfo(kB2).value(QStringLiteral("nameDuplicate")).toBool());
        const QVariantList others = vm.speakersMatching(QString(), kB1);
        QCOMPARE(others.size(), 1);
        QCOMPARE(others[0].toMap().value(QStringLiteral("rawLabel")).toString(), kB2);
        QVERIFY(others[0].toMap().value(QStringLiteral("nameDuplicate")).toBool());

        // Different names: no marking.
        QVERIFY(vm.reassignSpeaker(kB2, QStringLiteral("Antal"), false));
        for (const QVariant& v : vm.speakers())
            QVERIFY(!v.toMap().value(QStringLiteral("nameDuplicate")).toBool());
    }

    void singleMove_isIncremental_andUndoable()
    {
        Fixture fx;
        auto ed = fx.editor(false);
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());

        QSignalSpy resets(vm.rows(), &QAbstractItemModel::modelReset);
        QSignalSpy inserted(vm.rows(), &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(vm.rows(), &QAbstractItemModel::rowsRemoved);
        QSignalSpy changed(vm.rows(), &QAbstractItemModel::dataChanged);
        QSignalSpy session(&vm, &TranscriptEditorViewModel::sessionChanged);

        // A click in the other column of line 4.
        QVERIFY(vm.moveRowToLane(4, 1));
        QCOMPARE(speakerOf(vm, 4), kB2);
        QVERIFY(cell(vm, 4, Role::CorrectedRole).toBool());
        QVERIFY(cell(vm, 4, Role::HeadRole).toBool());      // the "fixed" pill is in the name row
        QVERIFY(!changed.isEmpty());
        QCOMPARE(resets.count(), 0);
        QCOMPARE(inserted.count(), 0);
        QCOMPARE(removed.count(), 0);
        QCOMPARE(session.count(), 0);
        // Only the affected line (and its neighbor's name row) is notified, not the whole list.
        for (const auto& args : std::as_const(changed)) {
            const int first = args.at(0).toModelIndex().row();
            const int last = args.at(1).toModelIndex().row();
            QVERIFY2(first >= 4 && last <= 5, qPrintable(QStringLiteral("%1..%2").arg(first).arg(last)));
        }

        // To the same place again: nothing happens.
        QVERIFY(!vm.moveRowToLane(4, 1));

        QVERIFY(vm.canUndo());
        QVERIFY(!vm.undoText().isEmpty());
        vm.undo();
        QCOMPARE(speakerOf(vm, 4), kB1);
        QVERIFY(!vm.canUndo());
        QVERIFY(vm.canRedo());
        vm.redo();
        QCOMPARE(speakerOf(vm, 4), kB2);
        QCOMPARE(resets.count(), 0);

        // The fix went to disk (into the overlay), the raw transcript is untouched.
        QVERIFY(QFile::exists(editing::speakeredit::overlayPath(fx.meeting.folder)));
    }

    void multiSelect_moveIsOneStep()
    {
        Fixture fx;
        auto ed = fx.editor(false);
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QSignalSpy resets(vm.rows(), &QAbstractItemModel::modelReset);
        QSignalSpy selection(&vm, &TranscriptEditorViewModel::selectionChanged);

        vm.selectRow(4);                                    // plain click
        QCOMPARE(vm.selectedCount(), 1);
        QCOMPARE(vm.currentRow(), 4);
        vm.selectRow(7, /* Ctrl+click */ true);                   // Ctrl+click
        vm.selectRow(9, /* Shift+click from 7 */ false, /* Shift+click from 7 */ true);  // Shift+click from 7
        QCOMPARE(vm.selectedCount(), 3);                    // 7, 8, 9 (Shift marks a new range)
        vm.selectRow(4, true);
        vm.selectRow(8, true);                              // line 8 (Speaker 2) out
        QCOMPARE(vm.selectedCount(), 3);                    // 4, 7, 9
        QVERIFY(cell(vm, 4, Role::SelectedRole).toBool());
        QVERIFY(!cell(vm, 8, Role::SelectedRole).toBool());
        QVERIFY(selection.count() >= 4);

        // Keys 1-9: the selection to column 2.
        QVERIFY(vm.moveSelectionToLane(1));
        QCOMPARE(vm.selectedCount(), 0);
        for (int r : {4, 7, 9}) QCOMPARE(speakerOf(vm, r), kB2);
        vm.undo();                                          // ONE step
        for (int r : {4, 7, 9}) QCOMPARE(speakerOf(vm, r), kB1);
        QVERIFY(!vm.canUndo());

        // Drag: lines [13, 14] to column 2.
        QVERIFY(vm.moveRowsToLane(14, 13, 1));
        QCOMPARE(speakerOf(vm, 13), kB2);
        QCOMPARE(speakerOf(vm, 14), kB2);
        vm.undo();

        // To a new person: a new column is created, without a reset.
        vm.selectRow(4);
        vm.selectRow(7, true);
        QVERIFY(vm.moveSelectionToPerson(QStringLiteral("Cili")));
        QCOMPARE(vm.speakerCount(), 3);
        QCOMPARE(vm.lanes().size(), 3);
        QCOMPARE(cell(vm, 4, Role::SpeakerNameRole).toString(), QStringLiteral("Cili"));
        QCOMPARE(cell(vm, 4, Role::LaneRole).toInt(), 2);
        QVERIFY(!fx.people->idForName(QStringLiteral("Cili")).isNull());
        QCOMPARE(fx.prints->totalPrintCount(), 0);          // reassigning does not teach a voiceprint
        vm.undo();
        QCOMPARE(vm.speakerCount(), 2);

        // To an anonymous new participant.
        vm.selectRow(4);
        QVERIFY(vm.moveSelectionToNewParticipant());
        QCOMPARE(vm.speakerCount(), 3);
        QVERIFY(vm.speakers().last().toMap().value(QStringLiteral("anonymous")).toBool());
        QCOMPARE(resets.count(), 0);

        // Step up / down.
        vm.clearSelection();
        vm.selectRow(2);
        QCOMPARE(vm.stepSelection(1), 3);
        QCOMPARE(vm.stepSelection(-1), 2);
        QCOMPARE(vm.currentRow(), 2);
    }

    // A person in the meeting is also found by NICKNAME in the "Whose line is this?" panel
    // (a match by name is unchanged; an anonymous speaker has no nickname).
    void meetingSpeakers_foundByAlias()
    {
        Fixture fx;
        auto ed = fx.editor(false);
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(vm.reassignSpeaker(kB2, QStringLiteral("Szabó Áron"), false));
        fx.people->addAlias(fx.people->idForName(QStringLiteral("Szabó Áron")), QStringLiteral("Dönci"));

        const QVariantList byAlias = vm.speakersMatching(QStringLiteral("donci"), QString());
        QCOMPARE(byAlias.size(), 1);
        QCOMPARE(byAlias.first().toMap().value(QStringLiteral("key")).toString(), kB2);
        QCOMPARE(byAlias.first().toMap().value(QStringLiteral("matchedAlias")).toString(),
                 QStringLiteral("Dönci"));

        const QVariantList byName = vm.speakersMatching(QStringLiteral("szabo"), QString());
        QCOMPARE(byName.size(), 1);
        QVERIFY(byName.first().toMap().value(QStringLiteral("matchedAlias")).toString().isEmpty());

        QVERIFY(vm.speakersMatching(QStringLiteral("nincsilyen"), QString()).isEmpty());
    }

    void wholeSpeaker_reassignMergeRevert()
    {
        Fixture fx;
        auto ed = fx.editor(false);
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QSignalSpy resets(vm.rows(), &QAbstractItemModel::modelReset);
        QSignalSpy speakers(&vm, &TranscriptEditorViewModel::speakersChanged);

        // Naming a whole speaker: all its lines get the name.
        QVERIFY(vm.reassignSpeaker(kB2, QStringLiteral("Béla"), false));
        QCOMPARE(cell(vm, 1, Role::SpeakerNameRole).toString(), QStringLiteral("Béla"));
        QCOMPARE(vm.speakerInfo(kB2).value(QStringLiteral("personName")).toString(), QStringLiteral("Béla"));
        QVERIFY(speakers.count() >= 1);

        // Back to anonymous.
        QVERIFY(vm.revertSpeakerToAnonymous(kB2, false));
        QCOMPARE(cell(vm, 1, Role::SpeakerNameRole).toString(), kB2);
        vm.undo();
        QCOMPARE(cell(vm, 1, Role::SpeakerNameRole).toString(), QStringLiteral("Béla"));

        // Merge: all lines of speaker 2 go to speaker 1; undoable in one step.
        QVERIFY(vm.mergeSpeakers(kB2, kB1));
        QCOMPARE(vm.speakerCount(), 1);
        QCOMPARE(vm.lanes().size(), 1);
        for (int r = 0; r < 18; ++r) QCOMPARE(speakerOf(vm, r), kB1);
        QVERIFY(!cell(vm, 1, Role::HeadRole).toBool());     // no more speaker changes
        vm.undo();
        QCOMPARE(vm.speakerCount(), 2);
        QCOMPARE(speakerOf(vm, 1), kB2);
        QVERIFY(cell(vm, 1, Role::HeadRole).toBool());

        // Adding and removing an empty participant.
        const QString added = vm.addParticipant(QStringLiteral("Dóra"));
        QVERIFY(!added.isEmpty());
        QCOMPARE(vm.lanes().size(), 3);
        QVERIFY(vm.removeParticipant(added));
        QCOMPARE(vm.lanes().size(), 2);
        QCOMPARE(resets.count(), 0);
    }

    void suggestion_showAndAccept()
    {
        Fixture fx;
        fx.setSpeakerMap({{kB1, QStringLiteral("Anna")}, {kB2, QStringLiteral("Béla")}});
        auto ed = fx.editor();
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());                             // starts the audio analysis
        QVERIFY(vm.voiceAvailable());
        QVERIFY(waitVoice(vm));
        QCOMPARE(vm.embeddingProgress(), 1.0);
        QVERIFY(vm.voiceNote().isEmpty());

        QSignalSpy resets(vm.rows(), &QAbstractItemModel::modelReset);
        QSignalSpy sugg(&vm, &TranscriptEditorViewModel::suggestionChanged);
        QSignalSpy reveal(&vm, &TranscriptEditorViewModel::revealRequested);

        // Line 4 (C's voice) by hand to a new person: the system offers the similar ones.
        vm.selectRow(4);
        QVERIFY(vm.moveSelectionToPerson(QStringLiteral("Cili")));
        QVERIFY(vm.suggestionActive());
        QCOMPARE(vm.suggestionCount(), 3);                  // 7, 9, 14 (the short 12 is left out)
        QCOMPARE(vm.suggestionTargetName(), QStringLiteral("Cili"));
        QVERIFY(sugg.count() >= 1);
        QVERIFY(cell(vm, 4, Role::SuggestionAnchorRole).toBool());
        QVERIFY(!cell(vm, 7, Role::SuggestedRole).toBool()); // not shown yet

        // "Show me": highlight on the rail + the overview, the rail turns on, we jump to the first.
        vm.setSuggestionShown(true);
        for (int r : {7, 9, 14}) QVERIFY(cell(vm, r, Role::SuggestedRole).toBool());
        QVERIFY(!cell(vm, 12, Role::SuggestedRole).toBool());
        QVERIFY(vm.railVisible());
        QCOMPARE(reveal.last().at(0).toInt(), 7);
        const QList<qreal> marks = vm.overview().at(0).toMap().value(QStringLiteral("marks")).value<QList<qreal>>();
        QCOMPARE(marks.size(), 6);

        // "Move them": bulk move, ONE undo step.
        QVERIFY(vm.acceptSuggestion());
        QVERIFY(!vm.suggestionActive());
        QVERIFY(!vm.suggestionShown());
        const QString cili = speakerOf(vm, 4);
        for (int r : {7, 9, 14}) QCOMPARE(speakerOf(vm, r), cili);
        QVERIFY(!cell(vm, 4, Role::SuggestionAnchorRole).toBool());
        vm.undo();
        QCOMPARE(speakerOf(vm, 7), kB1);
        QCOMPARE(speakerOf(vm, 4), cili);

        // "No": the suggestion disappears, without a step.
        QVERIFY(vm.moveRowToLane(7, laneOf(vm, cili)));
        QVERIFY(vm.suggestionActive());
        vm.dismissSuggestion();
        QVERIFY(!vm.suggestionActive());
        QVERIFY(!vm.acceptSuggestion());
        QCOMPARE(resets.count(), 0);
    }

    void uncertainFilter_gapsConfirmUndo()
    {
        Fixture fx;
        auto ed = fx.editor();
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(waitVoice(vm));
        QCOMPARE(vm.uncertainCount(), 1);                   // line 17 (A's voice on label 2)
        QVERIFY(cell(vm, 17, Role::UncertainRole).toBool());
        QVERIFY(cell(vm, 17, Role::HeadRole).toBool());

        // Filter on: "... 17 certain lines hidden" + the uncertain line.
        vm.setUncertainOnly(true);
        QCOMPARE(vm.rows()->rowCount(), 2);
        QCOMPARE(cell(vm, 0, Role::KindRole).toString(), QStringLiteral("gap"));
        QCOMPARE(cell(vm, 0, Role::HiddenCountRole).toInt(), 17);
        QCOMPARE(cell(vm, 1, Role::UtteranceIndexRole).toInt(), 17);
        QCOMPARE(vm.rowForTime(30000), 1);                  // the nearest visible line
        QVERIFY(vm.rowStartMs(0) < 0);                      // the divider has no time
        QCOMPARE(vm.rowStartMs(1), 104000);

        QSignalSpy resets(vm.rows(), &QAbstractItemModel::modelReset);
        QSignalSpy removed(vm.rows(), &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(vm.rows(), &QAbstractItemModel::rowsInserted);

        // "Good as is": the line disappears from the filter (by removal, not a reset), the divider grows.
        QVERIFY(vm.confirmRow(1));
        QCOMPARE(vm.uncertainCount(), 0);
        QCOMPARE(vm.rows()->rowCount(), 1);
        QCOMPARE(cell(vm, 0, Role::HiddenCountRole).toInt(), 18);
        QCOMPARE(removed.count(), 1);
        QCOMPARE(resets.count(), 0);

        // Undo: it comes back (by insertion).
        vm.undo();
        QCOMPARE(vm.uncertainCount(), 1);
        QCOMPARE(vm.rows()->rowCount(), 2);
        QCOMPARE(cell(vm, 0, Role::HiddenCountRole).toInt(), 17);
        QCOMPARE(inserted.count(), 1);
        QCOMPARE(resets.count(), 0);

        // Even placed by hand it is no longer uncertain, but in the filter it stays in place
        // ("fixed") so the cursor does not lose it; until the filter is applied again.
        QVERIFY(vm.moveRowToLane(1, 0));
        QCOMPARE(vm.uncertainCount(), 0);
        QCOMPARE(vm.rows()->rowCount(), 2);
        QCOMPARE(cell(vm, 1, Role::UtteranceIndexRole).toInt(), 17);
        QVERIFY(cell(vm, 1, Role::CorrectedRole).toBool());
        QVERIFY(!cell(vm, 1, Role::UncertainRole).toBool());
        QCOMPARE(resets.count(), 0);
        vm.setUncertainOnly(false);
        vm.setUncertainOnly(true);
        QCOMPARE(vm.rows()->rowCount(), 1);

        // Filter off: every line is visible (a mode change, a reset is allowed here).
        vm.setUncertainOnly(false);
        QCOMPARE(vm.rows()->rowCount(), 18);
        QCOMPARE(speakerOf(vm, 17), kB1);
    }

    // Recheck through the view model: the core of the confirmed A lines highlights Speaker 1's
    // C lines and the A line stuck on label 2; the filter turns on, the suggestion reaches the line.
    void recheck_fromConfirmedLines()
    {
        Fixture fx;
        auto ed = fx.editor();
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(waitVoice(vm));
        QVERIFY(!vm.canRecheck());
        QVERIFY(vm.recheckBlocker().contains(QStringLiteral("legalább 3 sort")));
        QVariantMap blocked = vm.recheckSpeakers();
        QVERIFY(!blocked.value(QStringLiteral("ran")).toBool());
        QVERIFY(!blocked.value(QStringLiteral("blocker")).toString().isEmpty());

        QSignalSpy state(&vm, &TranscriptEditorViewModel::recheckStateChanged);
        for (int row : {0, 2, 6}) QVERIFY(vm.confirmRow(row));
        QVERIFY(vm.canRecheck());
        QVERIFY(vm.recheckBlocker().isEmpty());
        QVERIFY(state.count() >= 1);

        QSignalSpy finished(&vm, &TranscriptEditorViewModel::recheckFinished);
        const QVariantMap r = vm.recheckSpeakers();
        QVERIFY(r.value(QStringLiteral("ran")).toBool());
        QCOMPARE(r.value(QStringLiteral("flagged")).toInt(), 5);
        QCOMPARE(r.value(QStringLiteral("speakersWithConfirmedCore")).toInt(), 1);
        QCOMPARE(r.value(QStringLiteral("confirmedLines")).toInt(), 3);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.last().at(0).toInt(), 5);
        QVERIFY(vm.uncertainOnly());                        // the filter turned on by itself
        QCOMPARE(vm.uncertainCount(), 5);

        // The suggestion reaches the line: line 17's voice is A's, so "Speaker 1"; no suggestion for C's lines.
        const int row17 = vm.rows()->rowOfUtterance(17);
        const int row4 = vm.rows()->rowOfUtterance(4);
        QVERIFY(row17 >= 0 && row4 >= 0);
        QCOMPARE(cell(vm, row17, Role::LikelySpeakerKeyRole).toString(), kB1);
        QCOMPARE(cell(vm, row17, Role::LikelySpeakerNameRole).toString(), kB1);
        QCOMPARE(cell(vm, row4, Role::LikelySpeakerKeyRole).toString(), QString());
        QVERIFY(cell(vm, row4, Role::UncertainRole).toBool());

        // Stepping with B: goes through the marked lines.
        QCOMPARE(vm.stepUncertain(-1, 1), row4);

        // The line's button ("Speaker 1 said it"): move, so fixed, it leaves the uncertain ones.
        QVERIFY(vm.moveUtteranceToSpeaker(uid(17), kB1));
        QCOMPARE(vm.uncertainCount(), 4);
        vm.undo();
        QCOMPARE(vm.uncertainCount(), 5);

        // A recheck running from the shell (on the same editor) turns the filter on too.
        vm.setUncertainOnly(false);
        ed->recheckFromConfirmed();
        QVERIFY(vm.uncertainOnly());
        QCOMPARE(finished.count(), 2);
    }

    // Two similar voices: the "similar lines" suggestion is silent, so the bar offers reviewing
    // the two, but only if both named speakers have at least 3 confirmed lines.
    void pairOffer_onlyWhenGuardBlocked_andBothHaveConfirmedLines()
    {
        PairScene sc(/*similar*/ true);
        auto ed = sc.editor();
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(waitVoice(vm));
        QSignalSpy notices(&vm, &TranscriptEditorViewModel::notice);

        // Without confirmed lines: neither suggestion nor offer.
        QVERIFY(vm.moveUtteranceToSpeaker(PairScene::id(3), kB2));
        QVERIFY(vm.changeActive());
        QVERIFY(!vm.suggestionActive());
        QVERIFY(!vm.pairOfferActive());
        vm.undo();

        QVERIFY(ed->confirmUtterances({PairScene::id(0), PairScene::id(2), PairScene::id(5),
                                       PairScene::id(1), PairScene::id(4), PairScene::id(7)}));
        QSignalSpy sugg(&vm, &TranscriptEditorViewModel::suggestionChanged);
        QVERIFY(vm.moveUtteranceToSpeaker(PairScene::id(3), kB2));
        QVERIFY(vm.changeActive());
        QVERIFY(!vm.suggestionActive());
        QVERIFY(vm.pairOfferActive());
        QVERIFY(sugg.count() > 0);
        QCOMPARE(vm.pairOfferText(),
                 QStringLiteral("Anna és Béla hangja hasonló. Nézzem át kettejük sorait a megerősítettek alapján?"));

        // "Review": the two speakers' lines, the filter turns on, a notice in place of the bar.
        const QVariantMap r = vm.acceptPairOffer();
        QVERIFY(r.value(QStringLiteral("ran")).toBool());
        QCOMPARE(r.value(QStringLiteral("flagged")).toInt(), 2);
        QVERIFY(!vm.pairOfferActive());
        QVERIFY(!vm.changeActive());
        QVERIFY(vm.uncertainOnly());
        QCOMPARE(ed->undoText(), QStringLiteral("Átnézés: Anna és Béla"));
        QCOMPARE(notices.count(), 1);
        QCOMPARE(notices.last().at(0).toString(),
                 QStringLiteral("2 kétséges sor Anna és Béla között — az Átnézendő szűrőben. "
                                "A két hang nagyon hasonló (0,85), az eredmény bizonytalan — hallgass bele."));
        for (int row : {6, 10}) {
            const int r6 = vm.rows()->rowOfUtterance(row);
            QVERIFY(r6 >= 0);
            QVERIFY(cell(vm, r6, Role::UncertainRole).toBool());
            QCOMPARE(cell(vm, r6, Role::LikelySpeakerKeyRole).toString(), kB2);
        }

        // After the fix there are no more doubtful lines: the 0 message.
        QVERIFY(vm.moveUtteranceToSpeaker(PairScene::id(6), kB2));
        QVERIFY(vm.pairOfferActive());                  // offers it again (it was not rejected)
        QVERIFY(vm.moveUtteranceToSpeaker(PairScene::id(10), kB2));
        vm.recheckPair(kB1, kB2);
        QVERIFY(notices.last().at(0).toString().startsWith(
            QStringLiteral("A megerősített sorok alapján nem találtam kétséges sort Anna és Béla között.")));

        // "Not now": for this pair it does not ask again in this session.
        vm.undo();
        vm.undo();
        QVERIFY(vm.moveUtteranceToSpeaker(PairScene::id(10), kB2));
        QVERIFY(vm.pairOfferActive());
        vm.declinePairOffer();
        QVERIFY(!vm.pairOfferActive());
        QVERIFY(vm.changeActive());                     // the reassignment notice stays
        vm.undo();
        QVERIFY(vm.moveUtteranceToSpeaker(PairScene::id(10), kB2));
        QVERIFY(!vm.pairOfferActive());

        // Blocker: said as a notice, does not run.
        const int before = notices.count();
        QVERIFY(!vm.recheckPair(kB1, kB1).value(QStringLiteral("ran")).toBool());
        QCOMPARE(notices.count(), before + 1);

        // The panel's pair picker: the other named speakers.
        const QVariantList cands = vm.pairCandidates(kB1);
        QCOMPARE(cands.size(), 1);
        QCOMPARE(cands.first().toMap().value(QStringLiteral("key")).toString(), kB2);
        QCOMPARE(cands.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Béla"));
    }

    void pairOffer_notShownWhenVoicesDiffer()
    {
        PairScene sc(/*similar*/ false);
        auto ed = sc.editor();
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(waitVoice(vm));
        QVERIFY(ed->confirmUtterances({PairScene::id(0), PairScene::id(2), PairScene::id(5),
                                       PairScene::id(1), PairScene::id(4), PairScene::id(7)}));
        QVERIFY(vm.moveUtteranceToSpeaker(PairScene::id(3), kB2));
        QVERIFY(vm.suggestionActive());                 // the regular suggestion speaks ("2 similar lines too")
        QCOMPARE(vm.suggestionCount(), 2);
        QVERIFY(!vm.pairOfferActive());
        // The pairwise review can still be run by hand.
        const QVariantMap r = vm.recheckPair(kB1, kB2);
        QCOMPARE(r.value(QStringLiteral("flagged")).toInt(), 2);
        QVERIFY(r.value(QStringLiteral("centroidSimilarity")).toDouble() < 0.1);
    }

    // The stored voiceprints are part of the reference too: the result message says what it was built from.
    void recheck_referenceSummaryWithVoiceprints()
    {
        PairScene sc(/*similar*/ false);
        tanara::Voiceprint here;
        here.embedding = {1.0f, 0.0f, 0.0f};
        here.sourceMeetingId = sc.meeting.id;
        here.sampleRef = QStringLiteral("audio.ogg#0-4000");     // Anna's line 0
        sc.prints->addPrint(sc.people->ensure(QStringLiteral("Anna")), here);
        tanara::Voiceprint earlier;
        earlier.embedding = {0.0f, 1.0f, 0.0f};
        earlier.sourceMeetingId = QStringLiteral("korabbi-megbeszeles");
        sc.prints->addPrint(sc.people->ensure(QStringLiteral("Béla")), earlier);
        auto ed = sc.editor();
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(waitVoice(vm));
        QSignalSpy notices(&vm, &TranscriptEditorViewModel::notice);
        QVERIFY(ed->confirmUtterances({PairScene::id(0), PairScene::id(2), PairScene::id(5)}));

        const QVariantMap r = vm.recheckPair(kB1, kB2);
        QCOMPARE(r.value(QStringLiteral("flagged")).toInt(), 3);
        QVERIFY(!r.value(QStringLiteral("fallbackB")).toBool());
        const QString expected =
            QStringLiteral("3 kétséges sor Anna és Béla között — az Átnézendő szűrőben. "
                           "Referencia: Anna 2 sor + 1 itteni lenyomat, Béla 1 korábbi lenyomat.");
        QCOMPARE(r.value(QStringLiteral("message")).toString(), expected);
        QCOMPARE(notices.last().at(0).toString(), expected);

        const QVariantMap g = vm.recheckSpeakers();
        QVERIFY(g.value(QStringLiteral("ran")).toBool());
        QCOMPARE(g.value(QStringLiteral("referenceSummary")).toString(),
                 QStringLiteral("Referencia: Anna 2 sor + 1 itteni lenyomat, Béla 1 korábbi lenyomat."));
    }

    // "Good as is, but do not use as a sample" and "Usable as a sample" on the line.
    void noisy_manualConfirmAndClear()
    {
        Fixture fx;
        auto ed = fx.editor();
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(waitVoice(vm));
        QVERIFY(!cell(vm, 17, Role::NoisyRole).toBool());   // there is no overlap in the scenario

        vm.setUncertainOnly(true);
        const int row = vm.rows()->rowOfUtterance(17);
        QVERIFY(vm.confirmRowNoisy(row));
        QCOMPARE(vm.uncertainCount(), 0);
        vm.setUncertainOnly(false);
        QVERIFY(cell(vm, 17, Role::NoisyRole).toBool());
        QVERIFY(!cell(vm, 17, Role::NoisyOverlapRole).toBool());
        QVERIFY(cell(vm, 17, Role::HeadRole).toBool());     // the pill gets a name row
        QVERIFY(ed->utterance(uid(17)).confirmed);
        QCOMPARE(vm.undoText(), QStringLiteral("1 sor megerősítése (nem hangminta)"));

        QVERIFY(vm.setRowNoisy(17, false));
        QVERIFY(!cell(vm, 17, Role::NoisyRole).toBool());
        vm.undo();
        QVERIFY(cell(vm, 17, Role::NoisyRole).toBool());
        QVERIFY(vm.setUtteranceNoisy(uid(17), false));
        QVERIFY(!vm.setUtteranceNoisy(uid(17), false));     // already that
    }

    void filter_keepsSuggestionAnchorVisible()
    {
        Fixture fx;
        auto ed = fx.editor();
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(waitVoice(vm));
        vm.setUncertainOnly(true);
        QSignalSpy resets(vm.rows(), &QAbstractItemModel::modelReset);

        // Fixing a line outside the filter brings a suggestion: the anchor line appears in the filter,
        // so the fixed line (which the bar's suggestion refers to) does not disappear.
        QVERIFY(ed->moveUtterancesToPerson({uid(4)}, ed->personChoice(QStringLiteral("Cili"))).size() > 0);
        QVERIFY(vm.suggestionActive());
        const int anchorRow = vm.rows()->rowOfUtterance(4);
        QVERIFY(anchorRow >= 0);
        QVERIFY(cell(vm, anchorRow, Role::SuggestionAnchorRole).toBool());
        int hidden = 0, shown = 0;
        for (int r = 0; r < vm.rows()->rowCount(); ++r) {
            if (cell(vm, r, Role::KindRole).toString() == QLatin1String("gap"))
                hidden += cell(vm, r, Role::HiddenCountRole).toInt();
            else
                ++shown;
        }
        QCOMPARE(hidden + shown, 18);
        vm.dismissSuggestion();
        QCOMPARE(vm.rows()->rowOfUtterance(4), -1);
        QCOMPARE(resets.count(), 0);
    }

    // The notice bar's data: what happened, the "all N lines" continuation (one more step), and
    // that undo / another edit ends it.
    void changeNotice_lineMove_restOfSource_undo()
    {
        Fixture fx;
        auto ed = fx.editor(/*withEmbedder*/ false);
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(!vm.changeActive());
        QSignalSpy changes(&vm, &TranscriptEditorViewModel::changeChanged);

        // ONE line (with an id): only that goes; the source (out of 11) keeps 10 lines.
        QVERIFY(vm.moveUtteranceToSpeaker(uid(4), kB2));
        QCOMPARE(speakerOf(vm, 4), kB2);
        QCOMPARE(vm.speakerInfo(kB1).value(QStringLiteral("utteranceCount")).toInt(), 10);
        QVERIFY(vm.changeActive());
        QCOMPARE(changes.count(), 1);
        QCOMPARE(vm.changeText(), QStringLiteral("1 sor átkerült ide: Beszélő 2"));
        QCOMPARE(vm.changeRow(), 4);
        QCOMPARE(vm.changeRestCount(), 10);
        QCOMPARE(vm.changeRestText(), QStringLiteral("Beszélő 1 mind a 10 sora"));
        QCOMPARE(vm.changeSourceKey(), kB1);
        QCOMPARE(vm.changeTargetKey(), kB2);
        QVERIFY(!vm.moveUtteranceToSpeaker(uid(4), kB2));  // already there: no step, the bar stays
        QCOMPARE(changes.count(), 1);
        QVERIFY(!vm.moveUtteranceToSpeaker(QStringLiteral("nincs-ilyen"), kB2));

        // "All 10 lines of Speaker 1": the remaining lines go too, one more step.
        const int serial = vm.changeSerial();
        QVERIFY(vm.moveRestOfSource());
        QCOMPARE(vm.speakerCount(), 1);
        QCOMPARE(vm.speakerInfo(kB2).value(QStringLiteral("utteranceCount")).toInt(), 18);
        QVERIFY(vm.changeSerial() > serial);
        QCOMPARE(vm.changeText(), QStringLiteral("Beszélő 1 mind a 10 sora átkerült ide: Beszélő 2"));
        QCOMPARE(vm.changeRestCount(), 0);                  // no continuation after a whole speaker
        QVERIFY(!vm.moveRestOfSource());

        vm.undoChange();                                    // only the bulk step
        QVERIFY(!vm.changeActive());
        QCOMPARE(vm.speakerCount(), 2);
        QCOMPARE(vm.speakerInfo(kB1).value(QStringLiteral("utteranceCount")).toInt(), 10);
        QCOMPARE(speakerOf(vm, 4), kB2);
        vm.undo();
        QCOMPARE(speakerOf(vm, 4), kB1);
        QVERIFY(!vm.canUndo());

        // Several lines from mixed sources: there is a count, no bulk continuation.
        vm.selectRows(0, 1);                                // B1 + B2
        QVERIFY(vm.moveSelectionToPerson(QStringLiteral("Cili")));
        // A new person was created: the bar says so, and offers the similar lines.
        QCOMPARE(vm.changeText(), QStringLiteral("Új személy: Cili. 2 sor átkerült hozzá."));
        QVERIFY(vm.changeNewPerson());
        QCOMPARE(vm.newPersonKey(), vm.speakerKeyForPerson(QStringLiteral("Cili")));
        QCOMPARE(vm.changeRestCount(), 0);
        QCOMPARE(vm.changeSourceKey(), QString());
        // Another edit (confirmation) ends the notice; it can also be closed.
        QVERIFY(vm.confirmRow(5));
        QVERIFY(!vm.changeActive());
        QVERIFY(vm.moveUtteranceToNewParticipant(uid(2)));
        QVERIFY(vm.changeActive());
        QCOMPARE(vm.changeRestCount(), 9);
        vm.dismissChange();
        QVERIFY(!vm.changeActive());
        QVERIFY(vm.canUndo());

        // Whole speaker: the name gets no suffix, the count's article follows the number.
        QVERIFY(vm.reassignSpeaker(kB2, QStringLiteral("Béla"), false));
        QCOMPARE(vm.changeText(), QStringLiteral("Beszélő 2 mind a 6 sora átkerült ide: Béla"));
        QCOMPARE(vm.speakerKeyForPerson(QStringLiteral("béla")), kB2);
        QCOMPARE(vm.speakerKeyForPerson(QStringLiteral("Nincs Ilyen")), QString());
        QVERIFY(vm.revertSpeakerToAnonymous(kB2, false));
        QCOMPARE(vm.changeText(), QStringLiteral("Béla mind a 6 sora átkerült ide: Beszélő 2"));

        QVERIFY(TranscriptEditorViewModel::needsAz(1));
        QVERIFY(TranscriptEditorViewModel::needsAz(5));
        QVERIFY(TranscriptEditorViewModel::needsAz(52));
        QVERIFY(TranscriptEditorViewModel::needsAz(1000));
        QVERIFY(!TranscriptEditorViewModel::needsAz(2));
        QVERIFY(!TranscriptEditorViewModel::needsAz(11));
        QVERIFY(!TranscriptEditorViewModel::needsAz(41));
        QVERIFY(!TranscriptEditorViewModel::needsAz(100));
    }

    // The suggestion can be accepted from the bar (one step, with a new notice); in the filter
    // "Show me" also makes the suggested (not uncertain) lines visible.
    void changeNotice_suggestion_inFilter()
    {
        Fixture fx;
        auto ed = fx.editor();
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(waitVoice(vm));
        vm.setUncertainOnly(true);
        const int filtered = vm.rows()->rowCount();

        // Line 4 (C's voice on the "Speaker 1" label) to a new participant: a suggestion for the other C lines.
        QVERIFY(vm.moveUtteranceToPerson(uid(4), QStringLiteral("Cili")));
        QVERIFY(vm.changeActive());
        QVERIFY(vm.suggestionActive());
        const int similar = vm.suggestionCount();
        QVERIFY(similar >= 3);
        QVERIFY(vm.rows()->rowOfUtterance(4) >= 0);         // the fixed line shows in the filter too
        const int withAnchor = vm.rows()->rowCount();
        QVERIFY(withAnchor > filtered);

        vm.setSuggestionShown(true);                        // the suggested lines appear
        QVERIFY(vm.rows()->rowOfUtterance(7) >= 0);
        QVERIFY(cell(vm, vm.rows()->rowOfUtterance(7), Role::SuggestedRole).toBool());
        vm.setSuggestionShown(false);
        QCOMPARE(vm.rows()->rowCount(), withAnchor);

        const QString target = vm.changeTargetKey();
        QVERIFY(vm.acceptSuggestion());
        QVERIFY(!vm.suggestionActive());
        QCOMPARE(vm.changeText(), QStringLiteral("%1 sor átkerült ide: Cili").arg(similar));
        QCOMPARE(vm.speakerInfo(target).value(QStringLiteral("utteranceCount")).toInt(), similar + 1);
        vm.undo();                                          // one step: only the suggested ones
        QCOMPARE(vm.speakerInfo(target).value(QStringLiteral("utteranceCount")).toInt(), 1);
        QVERIFY(!vm.changeActive());
    }

    void search_accentInsensitive()
    {
        Fixture fx;
        auto ed = fx.editor(false);
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QSignalSpy reveal(&vm, &TranscriptEditorViewModel::revealRequested);
        QSignalSpy resets(vm.rows(), &QAbstractItemModel::modelReset);

        vm.setSearchQuery(QStringLiteral("odon"));          // "Ödön"
        QCOMPARE(vm.searchMatchCount(), 1);
        QCOMPARE(vm.searchCurrent(), 0);
        QCOMPARE(reveal.last().at(0).toInt(), 7);
        const QString rich = cell(vm, 7, Role::RichTextRole).toString();
        QVERIFY2(rich.contains(QStringLiteral(">Ödön</span>")), qPrintable(rich));
        QVERIFY(cell(vm, 6, Role::RichTextRole).toString().isEmpty());

        vm.setSearchQuery(QStringLiteral("l1"));            // L1, L10 ... L17 (line 7's text differs)
        QCOMPARE(vm.searchMatchCount(), 9);
        QCOMPARE(vm.searchStep(1), 10);
        QCOMPARE(vm.searchCurrent(), 1);
        QCOMPARE(vm.searchStep(-1), 1);
        QCOMPARE(vm.searchStep(-1), 17);                    // wraps around

        vm.setSearchQuery(QStringLiteral("nincs ilyen"));   // no hit
        QCOMPARE(vm.searchMatchCount(), 0);
        QCOMPARE(vm.searchCurrent(), -1);
        QCOMPARE(vm.searchStep(1), -1);
        QVERIFY(cell(vm, 1, Role::RichTextRole).toString().isEmpty());

        vm.setSearchQuery(QString());
        QCOMPARE(vm.searchMatchCount(), 0);
        QCOMPARE(resets.count(), 0);
    }

    void playback_rowFollowsPosition()
    {
        Fixture fx;
        auto ed = fx.editor();
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(waitVoice(vm));
        QSignalSpy playing(&vm, &TranscriptEditorViewModel::playingRowChanged);

        QCOMPARE(vm.playingRow(), -1);
        vm.setPlaybackPosition(29000, true);
        QCOMPARE(vm.playingRow(), 5);
        vm.setPlaybackPosition(29500, true);                // the same line: no signal
        QCOMPARE(playing.count(), 1);
        vm.setPlaybackPosition(36500, true);
        QCOMPARE(vm.playingRow(), 6);
        vm.setPlaybackPosition(36500, false);
        QCOMPARE(vm.playingRow(), -1);

        // In the filter a hidden line does not "play".
        vm.setPlaybackPosition(36500, true);
        vm.setUncertainOnly(true);
        QCOMPARE(vm.playingRow(), -1);
        vm.setPlaybackPosition(105000, true);
        QCOMPARE(vm.playingRow(), 1);
    }

    void voiceprint_explicitOnly_plainWords()
    {
        Fixture fx;
        auto ed = fx.editor();
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(waitVoice(vm));
        QSignalSpy notice(&vm, &TranscriptEditorViewModel::notice);

        // None is made for an anonymous speaker.
        QVariantMap r = vm.createVoiceprint(kB2);
        QVERIFY(!r.value(QStringLiteral("ok")).toBool());
        QVERIFY(!r.value(QStringLiteral("message")).toString().isEmpty());

        // Too little material: it says how much is missing.
        vm.selectRow(4);
        QVERIFY(vm.moveSelectionToPerson(QStringLiteral("Cili")));
        const QString cili = speakerOf(vm, 4);
        QVariantMap mat = vm.voiceprintMaterial(cili);
        QVERIFY(mat.value(QStringLiteral("supported")).toBool());
        QVERIFY(!mat.value(QStringLiteral("sufficient")).toBool());
        QCOMPARE(mat.value(QStringLiteral("usableSec")).toInt(), 4);
        QCOMPARE(mat.value(QStringLiteral("missingSec")).toInt(), 11);
        r = vm.createVoiceprint(cili);
        QVERIFY(!r.value(QStringLiteral("ok")).toBool());
        QVERIFY(r.value(QStringLiteral("message")).toString().contains(QStringLiteral("11")));
        QCOMPARE(fx.prints->totalPrintCount(), 0);

        // With enough material, on explicit request it is made; the track header's dot appears.
        QVERIFY(vm.acceptSuggestion());                     // 7, 9, 14 go to Cili too
        QCOMPARE(fx.prints->totalPrintCount(), 0);          // reassigning does not teach by itself
        mat = vm.voiceprintMaterial(cili);
        QVERIFY(mat.value(QStringLiteral("sufficient")).toBool());
        r = vm.createVoiceprint(cili);
        QVERIFY2(r.value(QStringLiteral("ok")).toBool(), qPrintable(r.value(QStringLiteral("message")).toString()));
        QCOMPARE(fx.prints->printCount(fx.people->idForName(QStringLiteral("Cili"))), 1);
        QVERIFY(vm.speakerInfo(cili).value(QStringLiteral("hasVoiceprint")).toBool());
        QCOMPARE(notice.count(), 1);
    }

    // The indicator's state per speaker (overview + track header), after creation, and undoing
    // the just-created voiceprint: exactly that one voiceprint is deleted.
    void voiceprint_indicatorState_createAndUndo()
    {
        Fixture fx;
        Voiceprint old;
        old.embedding = {0.0f, 1.0f, 0.0f};
        fx.prints->addPrint(fx.people->ensure(QStringLiteral("Anna")), old);    // Anna already has an older sample
        auto ed = fx.editor();
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(waitVoice(vm));

        QTRY_COMPARE(overviewVoiceprint(vm, kB1), QStringLiteral("anonymous"));
        QCOMPARE(overviewVoiceprint(vm, kB2), QStringLiteral("anonymous"));

        QVERIFY(vm.reassignSpeaker(kB2, QStringLiteral("Béla"), false));
        QTRY_COMPARE(overviewVoiceprint(vm, kB2), QStringLiteral("none"));
        QCOMPARE(vm.lanes().at(laneOf(vm, kB2)).toMap().value(QStringLiteral("voiceprint")).toString(),
                 QStringLiteral("none"));
        QCOMPARE(overviewVoiceprint(vm, kB1), QStringLiteral("anonymous"));

        // Enough material: how many lines, how many seconds, and no "missing".
        const QVariantMap mat = vm.voiceprintMaterial(kB2);
        QVERIFY(mat.value(QStringLiteral("supported")).toBool());
        QVERIFY(mat.value(QStringLiteral("sufficient")).toBool());
        QVERIFY(mat.value(QStringLiteral("usableLines")).toInt() >= 3);
        QVERIFY(mat.value(QStringLiteral("usableSec")).toInt() >= 15);
        QCOMPARE(mat.value(QStringLiteral("missingSec")).toInt(), 0);
        QCOMPARE(mat.value(QStringLiteral("reason")).toString(), QString());

        // Create: the indicator is "has" at once; the result says what it was made from.
        const QVariantMap r = vm.createVoiceprint(kB2);
        QVERIFY(r.value(QStringLiteral("ok")).toBool());
        const QString printId = r.value(QStringLiteral("printId")).toString();
        QVERIFY(!printId.isEmpty());
        QVERIFY(r.value(QStringLiteral("usedLines")).toInt() >= 3);
        QVERIFY(r.value(QStringLiteral("usedSec")).toInt() >= 15);
        QCOMPARE(fx.prints->printCount(fx.people->idForName(QStringLiteral("Béla"))), 1);
        QTRY_COMPARE(overviewVoiceprint(vm, kB2), QStringLiteral("has"));
        QCOMPARE(vm.lanes().at(laneOf(vm, kB2)).toMap().value(QStringLiteral("voiceprint")).toString(),
                 QStringLiteral("has"));

        // Undo: exactly this voiceprint is deleted (another person's is untouched), it does not work twice.
        QVERIFY(vm.removeVoiceprint(printId));
        QCOMPARE(fx.prints->printCount(fx.people->idForName(QStringLiteral("Béla"))), 0);
        QCOMPARE(fx.prints->printCount(fx.people->idForName(QStringLiteral("Anna"))), 1);
        QTRY_COMPARE(overviewVoiceprint(vm, kB2), QStringLiteral("none"));
        QVERIFY(!vm.removeVoiceprint(printId));
        QVERIFY(!vm.removeVoiceprint(QString()));
        // The voiceprint is not part of the undo stack: the naming stayed, and is undoable.
        QCOMPARE(vm.speakerInfo(kB2).value(QStringLiteral("personName")).toString(), QStringLiteral("Béla"));
        QVERIFY(vm.canUndo());
    }

    // The notice bar's voiceprint offer: only after naming a WHOLE speaker / merging into a named
    // person, if the person has no voiceprint and there is enough material here.
    void changeBar_voiceprintOffer_onlyAfterNamingWholeSpeaker()
    {
        Fixture fx;
        auto ed = fx.editor();
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(waitVoice(vm));
        QSignalSpy notice(&vm, &TranscriptEditorViewModel::notice);

        // One line, several lines, similar lines: never offers.
        QVERIFY(vm.moveUtteranceToPerson(uid(4), QStringLiteral("Cili")));
        QVERIFY(vm.changeActive());
        QVERIFY(!vm.changeVoiceprintOffer());
        const QString cili = speakerOf(vm, 4);
        QVERIFY(vm.acceptSuggestion());                     // 7, 9, 14 go to Cili too: this is per line as well
        QVERIFY(vm.voiceprintMaterial(cili).value(QStringLiteral("sufficient")).toBool());
        QVERIFY(!vm.changeVoiceprintOffer());
        vm.selectRows(0, 2);
        QVERIFY(vm.moveSelectionToPerson(QStringLiteral("Dóra")));
        QVERIFY(!vm.changeVoiceprintOffer());
        QVERIFY(!vm.createVoiceprintFromChange());          // nothing is made without an offer
        QCOMPARE(fx.prints->totalPrintCount(), 0);

        // Naming a whole speaker, with enough material: an offer.
        QVERIFY(vm.reassignSpeaker(kB2, QStringLiteral("Béla"), false));
        QVERIFY(vm.changeVoiceprintOffer());
        QVERIFY(vm.changeUndoable());
        QVERIFY(!vm.changeVoiceprintCreated());
        QCOMPARE(fx.prints->totalPrintCount(), 0);          // the offer does not create by itself

        // Accepted: the bar says what was made; "Undo" then deletes the voiceprint.
        const int serial = vm.changeSerial();
        QVERIFY(vm.createVoiceprintFromChange());
        QCOMPARE(fx.prints->printCount(fx.people->idForName(QStringLiteral("Béla"))), 1);
        QVERIFY(vm.changeActive());
        QVERIFY(vm.changeVoiceprintCreated());
        QVERIFY(!vm.changeVoiceprintOffer());
        QVERIFY(vm.changeSerial() > serial);
        QVERIFY2(vm.changeText().startsWith(QStringLiteral("Hanglenyomat készült: Béla (")), qPrintable(vm.changeText()));
        QCOMPARE(notice.count(), 0);                        // the bar itself is the feedback
        const QString undoBefore = vm.undoText();
        vm.undoChange();
        QCOMPARE(fx.prints->printCount(fx.people->idForName(QStringLiteral("Béla"))), 0);
        QVERIFY(vm.changeActive());
        QVERIFY(!vm.changeUndoable());
        QVERIFY(!vm.changeVoiceprintCreated());
        QVERIFY(!vm.changeVoiceprintOffer());
        QCOMPARE(vm.changeText(), QStringLiteral("A most készült hanglenyomat törölve: Béla"));
        QCOMPARE(vm.speakerInfo(kB2).value(QStringLiteral("personName")).toString(), QStringLiteral("Béla"));
        QCOMPARE(vm.undoText(), undoBefore);                // the undo stack is untouched

        // Merging into a named person without a voiceprint ("all N lines"): an offer.
        QVERIFY(vm.moveUtteranceToSpeaker(uid(6), kB2));
        QVERIFY(!vm.changeVoiceprintOffer());
        QVERIFY(vm.changeRestCount() > 0);
        QVERIFY(vm.moveRestOfSource());
        QVERIFY(vm.changeVoiceprintOffer());
        vm.undo();
        QVERIFY(!vm.changeActive());

        // Too little material: no offer (we do not nag).
        const QString fresh = ed->moveUtterancesToNewParticipant({uid(16)});
        QVERIFY(!fresh.isEmpty());
        QVERIFY(vm.reassignSpeaker(fresh, QStringLiteral("Emma"), false));
        QVERIFY(vm.changeActive());
        QVERIFY(!vm.voiceprintMaterial(fresh).value(QStringLiteral("sufficient")).toBool());
        QVERIFY(!vm.changeVoiceprintOffer());

        // One who already has a voiceprint: no offer. Setting to anonymous: no offer.
        QVERIFY(vm.createVoiceprint(kB2).value(QStringLiteral("ok")).toBool());
        QVERIFY(vm.reassignSpeaker(cili, QStringLiteral("Béla"), false));    // Cili's lines merge into Béla
        QVERIFY(vm.changeActive());
        QVERIFY(!vm.changeVoiceprintOffer());
        QVERIFY(vm.revertSpeakerToAnonymous(kB2, false));
        QVERIFY(!vm.changeVoiceprintOffer());
    }

    // Without a voice model there is no offer and no creation: the reason is said once, clearly.
    void withoutVoiceModel_noVoiceprintOffer()
    {
        Fixture fx;
        auto ed = fx.editor(/*withEmbedder*/ false);
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(vm.reassignSpeaker(kB2, QStringLiteral("Béla"), false));
        QVERIFY(vm.changeActive());
        QVERIFY(!vm.changeVoiceprintOffer());
        QVERIFY(!vm.createVoiceprintFromChange());
        const QVariantMap mat = vm.voiceprintMaterial(kB2);
        QVERIFY(!mat.value(QStringLiteral("supported")).toBool());
        QCOMPARE(mat.value(QStringLiteral("reason")).toString(), QStringLiteral("model"));
        QTRY_COMPARE(overviewVoiceprint(vm, kB2), QStringLiteral("none"));
        QVERIFY(!vm.createVoiceprint(kB2).value(QStringLiteral("ok")).toBool());
        QCOMPARE(fx.prints->totalPrintCount(), 0);
    }

    void withoutVoiceModel_editingStillWorks()
    {
        Fixture fx;
        auto ed = fx.editor(/*withEmbedder*/ false);
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(!vm.voiceAvailable());
        QVERIFY(!vm.embeddingRunning());
        QVERIFY(!vm.voiceNote().isEmpty());                 // we say it once, clearly
        QCOMPARE(vm.uncertainCount(), 0);
        QVERIFY(!vm.voiceprintMaterial(kB1).value(QStringLiteral("supported")).toBool());
        QVERIFY(vm.moveRowToLane(4, 1));                    // the edit goes through
        QVERIFY(!vm.suggestionActive());
        vm.undo();
        QCOMPARE(speakerOf(vm, 4), kB1);
    }

    void railVisibility_rememberedPerMeeting()
    {
        Fixture fx;
        const QString statePath = fx.dir.filePath(QStringLiteral("meta/ui-transcript.json"));
        auto ed = fx.editor(false);
        {
            TranscriptEditorViewModel vm;
            vm.setUiStatePath(statePath);
            vm.setEditor(ed.get());
            QVERIFY(!vm.railVisible());
            vm.setRailVisible(true);
        }
        QVERIFY(QFile::exists(statePath));
        {
            TranscriptEditorViewModel vm;
            vm.setUiStatePath(statePath);
            vm.setEditor(ed.get());
            QVERIFY(vm.railVisible());
            vm.setRailVisible(false);
        }
        TranscriptEditorViewModel vm;
        vm.setUiStatePath(statePath);
        vm.setEditor(ed.get());
        QVERIFY(!vm.railVisible());
    }

    void peopleModel_searchAndCreate()
    {
        Fixture fx;
        fx.setSpeakerMap({{kB2, QStringLiteral("Béla")}});
        auto ed = fx.editor(false);
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        vm.setPeopleProvider([] {
            QVector<editing::PersonInfo> all;
            for (const char* n : {"Anna", "Béla", "Ödön", "Őri Áron"}) {
                editing::PersonInfo p;
                p.name = QString::fromUtf8(n);
                p.meetingCount = p.name == QLatin1String("Anna") ? 5 : 1;
                p.hasVoiceprint = p.name == QLatin1String("Anna");
                all.append(p);
            }
            return all;
        });
        QVERIFY(vm.isMeetingPerson(QStringLiteral("béla")));

        PersonListModel people;
        people.setEditor(&vm);
        // With an empty search the meeting's participant (Béla) is left out; the frequent ones first.
        QCOMPARE(people.count(), 3);
        QCOMPARE(people.nameAt(0), QStringLiteral("Anna"));
        QVERIFY(!people.canCreate());

        people.setQuery(QStringLiteral("odo"));             // without accents, by a name fragment too
        QCOMPARE(people.count(), 1);
        QCOMPARE(people.nameAt(0), QStringLiteral("Ödön"));
        QVERIFY(people.canCreate());                        // nobody named "odo" yet

        people.setQuery(QStringLiteral("odon"));
        QVERIFY(!people.canCreate());                       // matches (accent-insensitively): no "New person"

        people.setQuery(QStringLiteral("bel"));             // when searched the participant shows up too
        QCOMPARE(people.count(), 1);
        QVERIFY(people.data(people.index(0), PersonListModel::InMeetingRole).toBool());

        people.setQuery(QStringLiteral("Zsiga"));
        QCOMPARE(people.count(), 0);
        QVERIFY(people.canCreate());

        people.setQuery(QString());
        people.setExcludeName(QStringLiteral("Anna"));
        QCOMPARE(people.count(), 2);
    }

    // Move as "not a sample" in one step, mismatched-sample warning (fake embedder), "cannot be
    // identified" + hiding, the sample meter "+N now" with the filter, the "Just now" decision log.
    void s2_decisionsDeviationAndSampleMeter()
    {
        Fixture fx;
        auto ed = fx.editor();
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(waitVoice(vm));
        QVERIFY(vm.lastDecision().isEmpty());

        // "-> Speaker 1, not a sample": the line moves AND becomes noisy, a single undo step.
        QVERIFY(vm.moveUtteranceToSpeaker(uid(17), kB1, /*noisy*/ true));
        QCOMPARE(ed->utterance(uid(17)).speakerKey, kB1);
        QVERIFY(ed->utterance(uid(17)).noisy);
        QCOMPARE(cell(vm, 17, Role::SampleStateRole).toString(), QStringLiteral("noSample"));
        const QVariantList chips = vm.lastDecision().value(QStringLiteral("chips")).toList();
        QVERIFY(!chips.isEmpty());
        QCOMPARE(chips.first().toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("noSample"));
        vm.undo();
        QCOMPARE(ed->utterance(uid(17)).speakerKey, kB2);
        QVERIFY(!ed->utterance(uid(17)).noisy);
        QVERIFY(!vm.canUndo());
        QVERIFY(vm.lastDecision().value(QStringLiteral("undone")).toBool());
        QVERIFY(vm.lastDecision().value(QStringLiteral("text")).toString().startsWith(QStringLiteral("Visszavonva: ")));

        // Mismatched sample: a B-voiced line as a SAMPLE to speaker A/C gives a warning.
        QSignalSpy devSpy(&vm, &TranscriptEditorViewModel::sampleDeviationChanged);
        QVERIFY(vm.moveUtteranceToSpeaker(uid(1), kB1));
        QVERIFY(devSpy.count() > 0);
        const QVariantMap dev = vm.sampleDeviation();
        QCOMPARE(dev.value(QStringLiteral("utteranceId")).toString(), uid(1));
        QCOMPARE(dev.value(QStringLiteral("speakerKey")).toString(), kB1);
        QVERIFY(dev.value(QStringLiteral("pct")).toInt() < 50);
        QVERIFY(dev.value(QStringLiteral("text")).toString().contains(QStringLiteral("eltér")));
        // "Make it not a sample": the warning goes away.
        QVERIFY(vm.setUtteranceNoisy(uid(1), true));
        QVERIFY(vm.sampleDeviation().isEmpty());
        QCOMPARE(cell(vm, 1, Role::SampleStateRole).toString(), QStringLiteral("noSample"));

        // "Cannot be identified" + the list can hide it.
        QVERIFY(vm.setUtteranceIgnored(uid(5), true));
        QCOMPARE(vm.ignoredCount(), 1);
        QVERIFY(cell(vm, 5, Role::IgnoredRole).toBool());
        QVERIFY(!cell(vm, 5, Role::UncertainRole).toBool());
        QCOMPARE(vm.lastDecision().value(QStringLiteral("chips")).toList().first().toMap()
                     .value(QStringLiteral("kind")).toString(), QStringLiteral("ignored"));
        vm.setHideIgnored(true);
        QCOMPARE(vm.listFilter(), QStringLiteral("hideIgnored"));
        QCOMPARE(vm.rows()->rowOfUtterance(5), -1);
        vm.setHideIgnored(false);
        QCOMPARE(vm.rows()->rowCount(), int(scenario().size()));
        QVERIFY(vm.setUtteranceIgnored(uid(5), false));
        QCOMPARE(vm.ignoredCount(), 0);

        // Sample meter: two B lines "Good as is" to Speaker 2: +2 now; the filter shows only those two.
        QVERIFY(vm.confirmUtterance(uid(8)));
        QVERIFY(vm.confirmUtterance(uid(11)));
        QVariantMap b2;
        QTRY_VERIFY([&] {
            for (const QVariant& v : vm.sampleStats())
                if (v.toMap().value(QStringLiteral("key")).toString() == kB2) b2 = v.toMap();
            return b2.value(QStringLiteral("sessionAdded")).toInt() == 2;
        }());
        const QStringList added = b2.value(QStringLiteral("sessionAddedIds")).toStringList();
        QCOMPARE(added, (QStringList{uid(8), uid(11)}));
        QVERIFY(b2.value(QStringLiteral("text")).toString().contains(QStringLiteral("+2 most")));
        QSignalSpy reveal(&vm, &TranscriptEditorViewModel::revealRequested);
        QVERIFY(vm.showSessionSamples(kB2));
        QCOMPARE(reveal.count(), 1);
        QCOMPARE(reveal.first().first().toInt(), vm.rows()->rowOfUtterance(8));
        QCOMPARE(vm.listFilter(), QStringLiteral("samples"));
        int shown = 0;
        for (int r = 0; r < vm.rows()->rowCount(); ++r) {
            if (cell(vm, r, Role::KindRole).toString() != QLatin1String("utterance")) continue;
            ++shown;
            QVERIFY(added.contains(cell(vm, r, Role::UtteranceIdRole).toString()));
        }
        QCOMPARE(shown, 2);
        vm.clearSampleFilter();
        QCOMPARE(vm.listFilter(), QString());
        QCOMPARE(vm.rows()->rowCount(), int(scenario().size()));

        // The "Just now" log: the latest is lastDecision, before it at most 5.
        const QVariantList log = vm.decisionLog();
        QVERIFY(!log.isEmpty() && log.size() <= 5);
        QVERIFY(vm.lastDecision().value(QStringLiteral("text")).toString().startsWith(QStringLiteral("Jó így: ")));
        QCOMPARE(vm.lastDecision().value(QStringLiteral("time")).toString(),
                 TranscriptEditorViewModel::timeLabel(scenario()[11].startMs));
    }

    void demoSession_isFictionalAndComplete()
    {
        // Without a controller the view model gives the built-in made-up meeting.
        TranscriptEditorViewModel vm;
        vm.resolveSession();
        QVERIFY(vm.demo());
        QVERIFY(vm.hasTranscript());
        QVERIFY(vm.utteranceCount() > 100);
        QCOMPARE(vm.speakerCount(), 4);
        QVERIFY(!vm.people().isEmpty());
        QVERIFY(waitVoice(vm));
        QVERIFY(vm.uncertainCount() > 0);

        // Accent-insensitive search in the demo text.
        vm.setSearchQuery(QStringLiteral("sugo"));
        QVERIFY(vm.searchMatchCount() > 0);
        vm.setSearchQuery(QString());

        // The suggestion state: after a manual fix the system suggests similar lines.
        vm.applyDemoState(QStringLiteral("suggestion"));
        QVERIFY(vm.suggestionActive());
        QVERIFY(vm.suggestionCount() >= 2);
        QVERIFY(vm.railVisible());

        // Many speakers: the 6 who talk most are shown, the rest "+N" / "Other (N)".
        vm.setDemoVariant(QStringLiteral("many"));
        QCOMPARE(vm.speakerCount(), 11);
        QCOMPARE(vm.lanes().size(), 6);
        QCOMPARE(vm.collapsedCount(), 5);
        QCOMPARE(vm.overview().size(), 7);
        QCOMPARE(vm.overview().last().toMap().value(QStringLiteral("colorIndex")).toInt(), -1);
        int collapsedRow = -1;
        for (int r = 0; r < vm.rows()->rowCount() && collapsedRow < 0; ++r)
            if (cell(vm, r, Role::LaneRole).toInt() == -1) collapsedRow = r;
        QVERIFY(collapsedRow >= 0);
        vm.setLanesExpanded(true);
        QCOMPARE(vm.lanes().size(), 11);
        QCOMPARE(vm.overview().size(), 11);
        QVERIFY(cell(vm, collapsedRow, Role::LaneRole).toInt() >= 0);
        vm.setLanesExpanded(false);

        // Moved to a collapsed speaker: the target gets its own column.
        const QString hidden = speakerOf(vm, collapsedRow);
        vm.selectRow(0);
        QVERIFY(vm.moveSelectionToSpeaker(hidden));
        QCOMPARE(vm.lanes().size(), 7);
        QCOMPARE(vm.collapsedCount(), 4);

        vm.setDemoVariant(QStringLiteral("two"));
        QCOMPARE(vm.speakerCount(), 2);
        QCOMPARE(vm.collapsedCount(), 0);

        vm.setDemoVariant(QStringLiteral("none"));
        QVERIFY(!vm.hasTranscript());
        QCOMPARE(vm.rows()->rowCount(), 0);

        vm.setDemoVariant(QStringLiteral("novoice"));
        QVERIFY(vm.hasTranscript());
        QVERIFY(!vm.voiceAvailable());
        QVERIFY(!vm.voiceNote().isEmpty());
    }
};

QTEST_MAIN(TestTranscriptEditorViewModel)
#include "test_transcript_editor_view_model.moc"
