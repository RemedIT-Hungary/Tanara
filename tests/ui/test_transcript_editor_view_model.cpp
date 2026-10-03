// Az átirat-szerkesztő nézetmodelljei (TranscriptEditorViewModel + TranscriptListModel +
// PersonListModel) egy VALÓDI tanara::SpeakerEditor-on, ideiglenes mappában, hamis
// embedderrel: áthelyezés, több sor, összevonás, undo / redo, javaslat elfogadása, szűrő,
// keresés, sávok összecsukása, hanglenyomat. A lényeg: egy sor áthelyezése SOSEM reseteli
// a lista-modellt. Kijelző nélkül fut; a felhasználó adataihoz nem nyúl.
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

// A hang-elemzés (háttérszál) bevárása.
bool waitVoice(const TranscriptEditorViewModel& vm)
{
    return QTest::qWaitFor([&] { return !vm.embeddingRunning() && vm.editor()->embeddingsComplete(); }, 10000);
}

int laneOf(const TranscriptEditorViewModel& vm, const QString& key)
{
    const QVariantList lanes = vm.lanes();
    for (int i = 0; i < lanes.size(); ++i)
        if (lanes[i].toMap().value(QStringLiteral("key")).toString() == key) return i;
    return -1;
}

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
        QVERIFY(!vm.railVisible());                         // alapból rejtve

        QCOMPARE(cell(vm, 0, Role::KindRole).toString(), QStringLiteral("utterance"));
        QCOMPARE(cell(vm, 0, Role::SpeakerNameRole).toString(), kB1);
        QCOMPARE(cell(vm, 1, Role::SpeakerNameRole).toString(), QStringLiteral("Béla"));
        QCOMPARE(cell(vm, 1, Role::TimeLabelRole).toString(), QStringLiteral("00:06"));
        QCOMPARE(cell(vm, 0, Role::LaneRole).toInt(), 0);
        QCOMPARE(cell(vm, 1, Role::LaneRole).toInt(), 1);
        QCOMPARE(cell(vm, 1, Role::ColorIndexRole).toInt(), 1);
        // Névsor csak beszélőváltáskor: a 6–7. sor ugyanazé (Beszélő 1).
        QVERIFY(cell(vm, 0, Role::HeadRole).toBool());
        QVERIFY(cell(vm, 6, Role::HeadRole).toBool());
        QVERIFY(!cell(vm, 7, Role::HeadRole).toBool());
        QVERIFY(cell(vm, 0, Role::FirstRole).toBool());

        // Áttekintő: beszélőnként egy sor, a szegmensek [x, w] párok 0..1 között.
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

        // Kattintás a 4. sor másik oszlopába.
        QVERIFY(vm.moveRowToLane(4, 1));
        QCOMPARE(speakerOf(vm, 4), kB2);
        QVERIFY(cell(vm, 4, Role::CorrectedRole).toBool());
        QVERIFY(cell(vm, 4, Role::HeadRole).toBool());      // a „javítva" pirula a névsorban van
        QVERIFY(!changed.isEmpty());
        QCOMPARE(resets.count(), 0);
        QCOMPARE(inserted.count(), 0);
        QCOMPARE(removed.count(), 0);
        QCOMPARE(session.count(), 0);
        // Csak az érintett sor (és a szomszédja névsora) kap értesítést, nem az egész lista.
        for (const auto& args : std::as_const(changed)) {
            const int first = args.at(0).toModelIndex().row();
            const int last = args.at(1).toModelIndex().row();
            QVERIFY2(first >= 4 && last <= 5, qPrintable(QStringLiteral("%1..%2").arg(first).arg(last)));
        }

        // Ugyanoda még egyszer: nem történik semmi.
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

        // A javítás a lemezre került (az overlay-be), a nyers átirat érintetlen.
        QVERIFY(QFile::exists(speakeredit::overlayPath(fx.meeting.folder)));
    }

    void multiSelect_moveIsOneStep()
    {
        Fixture fx;
        auto ed = fx.editor(false);
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QSignalSpy resets(vm.rows(), &QAbstractItemModel::modelReset);
        QSignalSpy selection(&vm, &TranscriptEditorViewModel::selectionChanged);

        vm.selectRow(4);                                    // sima kattintás
        QCOMPARE(vm.selectedCount(), 1);
        QCOMPARE(vm.currentRow(), 4);
        vm.selectRow(7, /*toggle*/ true);                   // Ctrl+kattintás
        vm.selectRow(9, /*toggle*/ false, /*range*/ true);  // Shift+kattintás a 7-től
        QCOMPARE(vm.selectedCount(), 3);                    // 7, 8, 9 (a Shift új tartományt jelöl)
        vm.selectRow(4, true);
        vm.selectRow(8, true);                              // a 8. (Beszélő 2) ki
        QCOMPARE(vm.selectedCount(), 3);                    // 4, 7, 9
        QVERIFY(cell(vm, 4, Role::SelectedRole).toBool());
        QVERIFY(!cell(vm, 8, Role::SelectedRole).toBool());
        QVERIFY(selection.count() >= 4);

        // Az 1–9 billentyű: a kijelölés a 2. oszlopba.
        QVERIFY(vm.moveSelectionToLane(1));
        QCOMPARE(vm.selectedCount(), 0);
        for (int r : {4, 7, 9}) QCOMPARE(speakerOf(vm, r), kB2);
        vm.undo();                                          // EGY lépés
        for (int r : {4, 7, 9}) QCOMPARE(speakerOf(vm, r), kB1);
        QVERIFY(!vm.canUndo());

        // Húzás: a [13, 14] sorok a 2. oszlopba.
        QVERIFY(vm.moveRowsToLane(14, 13, 1));
        QCOMPARE(speakerOf(vm, 13), kB2);
        QCOMPARE(speakerOf(vm, 14), kB2);
        vm.undo();

        // Új személyhez: új oszlop jön létre, reset nélkül.
        vm.selectRow(4);
        vm.selectRow(7, true);
        QVERIFY(vm.moveSelectionToPerson(QStringLiteral("Cili")));
        QCOMPARE(vm.speakerCount(), 3);
        QCOMPARE(vm.lanes().size(), 3);
        QCOMPARE(cell(vm, 4, Role::SpeakerNameRole).toString(), QStringLiteral("Cili"));
        QCOMPARE(cell(vm, 4, Role::LaneRole).toInt(), 2);
        QVERIFY(fx.people->names().contains(QStringLiteral("Cili")));
        QCOMPARE(fx.prints->totalPrintCount(), 0);          // az átsorolás nem tanít lenyomatot
        vm.undo();
        QCOMPARE(vm.speakerCount(), 2);

        // Névtelen új résztvevőhöz.
        vm.selectRow(4);
        QVERIFY(vm.moveSelectionToNewParticipant());
        QCOMPARE(vm.speakerCount(), 3);
        QVERIFY(vm.speakers().last().toMap().value(QStringLiteral("anonymous")).toBool());
        QCOMPARE(resets.count(), 0);

        // Fel / le léptetés.
        vm.clearSelection();
        vm.selectRow(2);
        QCOMPARE(vm.stepSelection(1), 3);
        QCOMPARE(vm.stepSelection(-1), 2);
        QCOMPARE(vm.currentRow(), 2);
    }

    void wholeSpeaker_reassignMergeRevert()
    {
        Fixture fx;
        auto ed = fx.editor(false);
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QSignalSpy resets(vm.rows(), &QAbstractItemModel::modelReset);
        QSignalSpy speakers(&vm, &TranscriptEditorViewModel::speakersChanged);

        // Teljes beszélő elnevezése: minden sora megkapja a nevet.
        QVERIFY(vm.reassignSpeaker(kB2, QStringLiteral("Béla"), false));
        QCOMPARE(cell(vm, 1, Role::SpeakerNameRole).toString(), QStringLiteral("Béla"));
        QCOMPARE(vm.speakerInfo(kB2).value(QStringLiteral("personName")).toString(), QStringLiteral("Béla"));
        QVERIFY(speakers.count() >= 1);

        // Vissza névtelenre.
        QVERIFY(vm.revertSpeakerToAnonymous(kB2, false));
        QCOMPARE(cell(vm, 1, Role::SpeakerNameRole).toString(), kB2);
        vm.undo();
        QCOMPARE(cell(vm, 1, Role::SpeakerNameRole).toString(), QStringLiteral("Béla"));

        // Összevonás: a 2-es minden sora az 1-eshez kerül; egy lépésben visszavonható.
        QVERIFY(vm.mergeSpeakers(kB2, kB1));
        QCOMPARE(vm.speakerCount(), 1);
        QCOMPARE(vm.lanes().size(), 1);
        for (int r = 0; r < 18; ++r) QCOMPARE(speakerOf(vm, r), kB1);
        QVERIFY(!cell(vm, 1, Role::HeadRole).toBool());     // nincs több beszélőváltás
        vm.undo();
        QCOMPARE(vm.speakerCount(), 2);
        QCOMPARE(speakerOf(vm, 1), kB2);
        QVERIFY(cell(vm, 1, Role::HeadRole).toBool());

        // Üres résztvevő felvétele és eltávolítása.
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
        vm.setEditor(ed.get());                             // elindítja a hang-elemzést
        QVERIFY(vm.voiceAvailable());
        QVERIFY(waitVoice(vm));
        QCOMPARE(vm.embeddingProgress(), 1.0);
        QVERIFY(vm.voiceNote().isEmpty());

        QSignalSpy resets(vm.rows(), &QAbstractItemModel::modelReset);
        QSignalSpy sugg(&vm, &TranscriptEditorViewModel::suggestionChanged);
        QSignalSpy reveal(&vm, &TranscriptEditorViewModel::revealRequested);

        // A 4. sor (C hangja) kézzel új személyhez → a rendszer felajánlja a hasonlókat.
        vm.selectRow(4);
        QVERIFY(vm.moveSelectionToPerson(QStringLiteral("Cili")));
        QVERIFY(vm.suggestionActive());
        QCOMPARE(vm.suggestionCount(), 3);                  // 7, 9, 14 (a rövid 12. kimarad)
        QCOMPARE(vm.suggestionTargetName(), QStringLiteral("Cili"));
        QVERIFY(sugg.count() >= 1);
        QVERIFY(cell(vm, 4, Role::SuggestionAnchorRole).toBool());
        QVERIFY(!cell(vm, 7, Role::SuggestedRole).toBool()); // még nem mutatjuk

        // „Megmutatom": kiemelés a sínen + az áttekintőn, a sín bekapcsol, az elsőre ugrunk.
        vm.setSuggestionShown(true);
        for (int r : {7, 9, 14}) QVERIFY(cell(vm, r, Role::SuggestedRole).toBool());
        QVERIFY(!cell(vm, 12, Role::SuggestedRole).toBool());
        QVERIFY(vm.railVisible());
        QCOMPARE(reveal.last().at(0).toInt(), 7);
        const QList<qreal> marks = vm.overview().at(0).toMap().value(QStringLiteral("marks")).value<QList<qreal>>();
        QCOMPARE(marks.size(), 6);

        // „Átrakom": tömeges áthelyezés, EGY visszavonási lépés.
        QVERIFY(vm.acceptSuggestion());
        QVERIFY(!vm.suggestionActive());
        QVERIFY(!vm.suggestionShown());
        const QString cili = speakerOf(vm, 4);
        for (int r : {7, 9, 14}) QCOMPARE(speakerOf(vm, r), cili);
        QVERIFY(!cell(vm, 4, Role::SuggestionAnchorRole).toBool());
        vm.undo();
        QCOMPARE(speakerOf(vm, 7), kB1);
        QCOMPARE(speakerOf(vm, 4), cili);

        // „Nem": a javaslat eltűnik, lépés nélkül.
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
        QCOMPARE(vm.uncertainCount(), 1);                   // a 17. sor (A hangja a 2-es címkén)
        QVERIFY(cell(vm, 17, Role::UncertainRole).toBool());
        QVERIFY(cell(vm, 17, Role::HeadRole).toBool());

        // Szűrő be: „··· 17 biztos sor elrejtve" + a bizonytalan sor.
        vm.setUncertainOnly(true);
        QCOMPARE(vm.rows()->rowCount(), 2);
        QCOMPARE(cell(vm, 0, Role::KindRole).toString(), QStringLiteral("gap"));
        QCOMPARE(cell(vm, 0, Role::HiddenCountRole).toInt(), 17);
        QCOMPARE(cell(vm, 1, Role::UtteranceIndexRole).toInt(), 17);
        QCOMPARE(vm.rowForTime(30000), 1);                  // a legközelebbi látható sor
        QVERIFY(vm.rowStartMs(0) < 0);                      // az elválasztónak nincs ideje
        QCOMPARE(vm.rowStartMs(1), 104000);

        QSignalSpy resets(vm.rows(), &QAbstractItemModel::modelReset);
        QSignalSpy removed(vm.rows(), &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(vm.rows(), &QAbstractItemModel::rowsInserted);

        // „Jó így": a sor eltűnik a szűrőből (törléssel, nem resettel), az elválasztó nő.
        QVERIFY(vm.confirmRow(1));
        QCOMPARE(vm.uncertainCount(), 0);
        QCOMPARE(vm.rows()->rowCount(), 1);
        QCOMPARE(cell(vm, 0, Role::HiddenCountRole).toInt(), 18);
        QCOMPARE(removed.count(), 1);
        QCOMPARE(resets.count(), 0);

        // Visszavonás: visszakerül (beszúrással).
        vm.undo();
        QCOMPARE(vm.uncertainCount(), 1);
        QCOMPARE(vm.rows()->rowCount(), 2);
        QCOMPARE(cell(vm, 0, Role::HiddenCountRole).toInt(), 17);
        QCOMPARE(inserted.count(), 1);
        QCOMPARE(resets.count(), 0);

        // Kézzel a helyére téve sem bizonytalan többé.
        QVERIFY(vm.moveRowToLane(1, 0));
        QCOMPARE(vm.rows()->rowCount(), 1);

        // Szűrő ki: minden sor látszik (ez módváltás, itt megengedett a reset).
        vm.setUncertainOnly(false);
        QCOMPARE(vm.rows()->rowCount(), 18);
        QCOMPARE(speakerOf(vm, 17), kB1);
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

        // A szűrőn kívüli sor javítása javaslatot hoz: a horgony sora megjelenik a szűrőben,
        // hogy a javaslat-doboznak legyen helye.
        QVERIFY(ed->moveUtterancesToPerson({uid(4)}, QStringLiteral("Cili")).size() > 0);
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

    void search_accentInsensitive()
    {
        Fixture fx;
        auto ed = fx.editor(false);
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QSignalSpy reveal(&vm, &TranscriptEditorViewModel::revealRequested);
        QSignalSpy resets(vm.rows(), &QAbstractItemModel::modelReset);

        vm.setSearchQuery(QStringLiteral("odon"));          // „Ödön"
        QCOMPARE(vm.searchMatchCount(), 1);
        QCOMPARE(vm.searchCurrent(), 0);
        QCOMPARE(reveal.last().at(0).toInt(), 7);
        const QString rich = cell(vm, 7, Role::RichTextRole).toString();
        QVERIFY2(rich.contains(QStringLiteral(">Ödön</span>")), qPrintable(rich));
        QVERIFY(cell(vm, 6, Role::RichTextRole).toString().isEmpty());

        vm.setSearchQuery(QStringLiteral("l1"));            // L1, L10 … L17 (a 7. szövege más)
        QCOMPARE(vm.searchMatchCount(), 9);
        QCOMPARE(vm.searchStep(1), 10);
        QCOMPARE(vm.searchCurrent(), 1);
        QCOMPARE(vm.searchStep(-1), 1);
        QCOMPARE(vm.searchStep(-1), 17);                    // körbefordul

        vm.setSearchQuery(QStringLiteral("nincs ilyen"));   // üres találat
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
        vm.setPlaybackPosition(29500, true);                // ugyanaz a sor: nincs jel
        QCOMPARE(playing.count(), 1);
        vm.setPlaybackPosition(36500, true);
        QCOMPARE(vm.playingRow(), 6);
        vm.setPlaybackPosition(36500, false);
        QCOMPARE(vm.playingRow(), -1);

        // A szűrőben a rejtett sor nem „játszik".
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

        // Névtelen beszélőhöz nem készül.
        QVariantMap r = vm.createVoiceprint(kB2);
        QVERIFY(!r.value(QStringLiteral("ok")).toBool());
        QVERIFY(!r.value(QStringLiteral("message")).toString().isEmpty());

        // Kevés anyag: megmondja, mennyi hiányzik.
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

        // Elég anyaggal, kifejezett kérésre elkészül; a sáv-fejléc pöttye megjelenik.
        QVERIFY(vm.acceptSuggestion());                     // 7, 9, 14 is Cilihez
        QCOMPARE(fx.prints->totalPrintCount(), 0);          // az átsorolás magától nem tanít
        mat = vm.voiceprintMaterial(cili);
        QVERIFY(mat.value(QStringLiteral("sufficient")).toBool());
        r = vm.createVoiceprint(cili);
        QVERIFY2(r.value(QStringLiteral("ok")).toBool(), qPrintable(r.value(QStringLiteral("message")).toString()));
        QCOMPARE(fx.prints->printCount(QStringLiteral("Cili")), 1);
        QVERIFY(vm.speakerInfo(cili).value(QStringLiteral("hasVoiceprint")).toBool());
        QCOMPARE(notice.count(), 1);
    }

    void withoutVoiceModel_editingStillWorks()
    {
        Fixture fx;
        auto ed = fx.editor(/*withEmbedder*/ false);
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(!vm.voiceAvailable());
        QVERIFY(!vm.embeddingRunning());
        QVERIFY(!vm.voiceNote().isEmpty());                 // egyszer, érthetően megmondjuk
        QCOMPARE(vm.uncertainCount(), 0);
        QVERIFY(!vm.voiceprintMaterial(kB1).value(QStringLiteral("supported")).toBool());
        QVERIFY(vm.moveRowToLane(4, 1));                    // a szerkesztés megy
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
            QVector<PersonInfo> all;
            for (const char* n : {"Anna", "Béla", "Ödön", "Őri Áron"}) {
                PersonInfo p;
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
        // Üres keresőnél a meeting résztvevője (Béla) kimarad; a gyakori elöl.
        QCOMPARE(people.count(), 3);
        QCOMPARE(people.nameAt(0), QStringLiteral("Anna"));
        QVERIFY(!people.canCreate());

        people.setQuery(QStringLiteral("odo"));             // ékezet nélkül, névrészletre is
        QCOMPARE(people.count(), 1);
        QCOMPARE(people.nameAt(0), QStringLiteral("Ödön"));
        QVERIFY(people.canCreate());                        // „odo" nevű személy még nincs

        people.setQuery(QStringLiteral("odon"));
        QVERIFY(!people.canCreate());                       // (ékezet-függetlenül) egyezik: nincs „Új személy"

        people.setQuery(QStringLiteral("bel"));             // keresve a résztvevő is megjelenik
        QCOMPARE(people.count(), 1);
        QVERIFY(people.data(people.index(0), PersonListModel::InMeetingRole).toBool());

        people.setQuery(QStringLiteral("Zsiga"));
        QCOMPARE(people.count(), 0);
        QVERIFY(people.canCreate());

        people.setQuery(QString());
        people.setExcludeName(QStringLiteral("Anna"));
        QCOMPARE(people.count(), 2);
    }

    void demoSession_isFictionalAndComplete()
    {
        // Controller nélkül a nézetmodell a beépített kitalált meetinget adja.
        TranscriptEditorViewModel vm;
        vm.resolveSession();
        QVERIFY(vm.demo());
        QVERIFY(vm.hasTranscript());
        QVERIFY(vm.utteranceCount() > 100);
        QCOMPARE(vm.speakerCount(), 4);
        QVERIFY(!vm.people().isEmpty());
        QVERIFY(waitVoice(vm));
        QVERIFY(vm.uncertainCount() > 0);

        // Ékezet-független keresés a demó szövegében.
        vm.setSearchQuery(QStringLiteral("sugo"));
        QVERIFY(vm.searchMatchCount() > 0);
        vm.setSearchQuery(QString());

        // A javaslat-állapot: egy kézi javítás után a rendszer hasonló sorokat ajánl.
        vm.applyDemoState(QStringLiteral("suggestion"));
        QVERIFY(vm.suggestionActive());
        QVERIFY(vm.suggestionCount() >= 2);
        QVERIFY(vm.railVisible());

        // Sok beszélő: a 6 legtöbbet beszélő látszik, a többi „+N" / „Egyéb (N)".
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

        // Összecsukott beszélőhöz áthelyezve a cél saját oszlopot kap.
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
