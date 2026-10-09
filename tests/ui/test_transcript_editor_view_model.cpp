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

// A beszélő hanglenyomat-jelzője az áttekintőn ("has" | "none" | "anonymous").
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

// ---- két HASONLÓ hang (páronkénti átnézés) ----------------------------------
// Anna („Beszélő 1") 4 saját sora mellett 3 Béla-hangú sor; Béla („Beszélő 2") 5 sora.
// similar: cos(Anna, Béla) = 0.85 (a „hasonló sorok" őre ezt egy embernek veszi); különben
// a két hang merőleges.
struct PairScene {
    QTemporaryDir dir;
    std::unique_ptr<MeetingStore> store;
    std::unique_ptr<PeopleStore> people;
    std::unique_ptr<VoiceprintStore> prints;
    Meeting meeting;

    static QString id(int row) { return QStringLiteral("u%1").arg(row * 5000); }

    explicit PairScene(bool similar)
    {
        // 0 A a · 1 B b · 2 A a · 3 A b* · 4 B b · 5 A a · 6 A b* · 7 B b · 8 A a · 9 B b ·
        // 10 A b* · 11 B b
        const QVector<QPair<QString, char>> rows{
            {kB1, 'a'}, {kB2, 'b'}, {kB1, 'a'}, {kB1, 'b'}, {kB2, 'b'}, {kB1, 'a'},
            {kB1, 'b'}, {kB2, 'b'}, {kB1, 'a'}, {kB2, 'b'}, {kB1, 'b'}, {kB2, 'b'}};
        store = std::make_unique<MeetingStore>(dir.filePath(QStringLiteral("rec")),
                                               dir.filePath(QStringLiteral("meta")));
        people = std::make_unique<PeopleStore>(dir.filePath(QStringLiteral("meta/people.json")));
        prints = std::make_unique<VoiceprintStore>(dir.filePath(QStringLiteral("meta/vp.json")));
        meeting = store->createMeeting(QStringLiteral("Két hasonló hang"));
        QJsonArray segs;
        for (int i = 0; i < rows.size(); ++i)
            segs.append(QJsonObject{{QStringLiteral("startMs"), double(i * 5000)},
                                    {QStringLiteral("endMs"), double(i * 5000 + 4000)},
                                    {QStringLiteral("speaker"), rows[i].first},
                                    {QStringLiteral("text"), QStringLiteral("Sor %1").arg(i)}});
        QFile f(tanara::speakeredit::segmentsPath(meeting.folder));
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(segs).toJson());
        f.close();
        meeting.hasTranscript = true;
        meeting.speakerMap = {{kB1, QStringLiteral("Anna")}, {kB2, QStringLiteral("Béla")}};
        store->saveMeeting(meeting);
        const QVector<float> a{1.0f, 0.0f, 0.0f};
        const QVector<float> b = similar ? QVector<float>{0.85f, 0.5268f, 0.0f} : QVector<float>{0.0f, 1.0f, 0.0f};
        const QVector<tanara::TranscriptLine> lines = tanara::speakeredit::loadTranscriptLines(meeting.folder);
        UtteranceEmbeddingCache c;
        c.fingerprint = tanara::speakeredit::transcriptFingerprint(lines);
        for (int i = 0; i < lines.size(); ++i) c.vectors.insert(lines[i].id, rows[i].second == 'a' ? a : b);
        c.save(meeting.folder);
    }

    std::unique_ptr<SpeakerEditor> editor()
    {
        auto ed = std::make_unique<SpeakerEditor>(store.get(), people.get(), prints.get(), meeting.id);
        // A cache teljes: a gyár csak a „van hangmodell" állapotért kell (nem fut le semmi).
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

    // Két nyers beszélő ugyanarra a névre azonosítva: a felület a nyers címkével különbözteti
    // meg őket (nameDuplicate), a sorválasztó listája és a speakerInfo ugyanazt a térképet adja.
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

        // Eltérő nevek: nincs jelölés.
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

    // A megbeszélésen szereplő személyt a BECENEVE is megtalálja a „Kinek a sora ez?" panelben
    // (a név szerinti találat változatlan; névtelen beszélőnek nincs beceneve).
    void meetingSpeakers_foundByAlias()
    {
        Fixture fx;
        auto ed = fx.editor(false);
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(vm.reassignSpeaker(kB2, QStringLiteral("Szabó Áron"), false));
        fx.people->addAlias(QStringLiteral("Szabó Áron"), QStringLiteral("Dönci"));

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

        // Kézzel a helyére téve sem bizonytalan többé — de a szűrőben a helyén marad
        // („javítva"), hogy ne tűnjön el a kurzor alól; a szűrő újbóli alkalmazásáig.
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

        // Szűrő ki: minden sor látszik (ez módváltás, itt megengedett a reset).
        vm.setUncertainOnly(false);
        QCOMPARE(vm.rows()->rowCount(), 18);
        QCOMPARE(speakerOf(vm, 17), kB1);
    }

    // Újraellenőrzés a nézetmodellen át: a megerősített A-sorok magja kiemeli a „Beszélő 1"
    // C-sorait és a 2-es címkén ragadt A-sort; a szűrő bekapcsol, a javaslat a sorig ér.
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
        QVERIFY(vm.uncertainOnly());                        // a szűrő magától bekapcsolt
        QCOMPARE(vm.uncertainCount(), 5);

        // A javaslat a sorig ér: a 17. sor hangja A-é → „Beszélő 1"; C sorainál nincs javaslat.
        const int row17 = vm.rows()->rowOfUtterance(17);
        const int row4 = vm.rows()->rowOfUtterance(4);
        QVERIFY(row17 >= 0 && row4 >= 0);
        QCOMPARE(cell(vm, row17, Role::LikelySpeakerKeyRole).toString(), kB1);
        QCOMPARE(cell(vm, row17, Role::LikelySpeakerNameRole).toString(), kB1);
        QCOMPARE(cell(vm, row4, Role::LikelySpeakerKeyRole).toString(), QString());
        QVERIFY(cell(vm, row4, Role::UncertainRole).toBool());

        // B-léptetés: végigmegy a megjelölt sorokon.
        QCOMPARE(vm.stepUncertain(-1, 1), row4);

        // A sor gombja („Beszélő 1 mondta"): áthelyezés → javítva, kikerül a bizonytalanok közül.
        QVERIFY(vm.moveUtteranceToSpeaker(uid(17), kB1));
        QCOMPARE(vm.uncertainCount(), 4);
        vm.undo();
        QCOMPARE(vm.uncertainCount(), 5);

        // A héjból (ugyanazon a szerkesztőn) futó újraellenőrzés is bekapcsolja a szűrőt.
        vm.setUncertainOnly(false);
        ed->recheckFromConfirmed();
        QVERIFY(vm.uncertainOnly());
        QCOMPARE(finished.count(), 2);
    }

    // Két hasonló hang: a „hasonló sorok" javaslat hallgat → a sáv a kettejük átnézését ajánlja,
    // de csak ha mindkét elnevezett beszélőnek van legalább 3 megerősített sora.
    void pairOffer_onlyWhenGuardBlocked_andBothHaveConfirmedLines()
    {
        PairScene sc(/*similar*/ true);
        auto ed = sc.editor();
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(waitVoice(vm));
        QSignalSpy notices(&vm, &TranscriptEditorViewModel::notice);

        // Megerősített sorok nélkül: se javaslat, se ajánlat.
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

        // „Átnézés": a két beszélő sorai, a szűrő bekapcsol, a sáv helyén értesítés.
        const QVariantMap r = vm.acceptPairOffer();
        QVERIFY(r.value(QStringLiteral("ran")).toBool());
        QCOMPARE(r.value(QStringLiteral("flagged")).toInt(), 2);
        QVERIFY(!vm.pairOfferActive());
        QVERIFY(!vm.changeActive());
        QVERIFY(vm.uncertainOnly());
        QCOMPARE(ed->undoText(), QStringLiteral("Átnézés: Anna és Béla"));
        QCOMPARE(notices.count(), 1);
        QCOMPARE(notices.last().at(0).toString(),
                 QStringLiteral("2 kétséges sor Anna és Béla között — a Bizonytalan szűrőben. "
                                "A két hang nagyon hasonló (0,85), az eredmény bizonytalan — hallgass bele."));
        for (int row : {6, 10}) {
            const int r6 = vm.rows()->rowOfUtterance(row);
            QVERIFY(r6 >= 0);
            QVERIFY(cell(vm, r6, Role::UncertainRole).toBool());
            QCOMPARE(cell(vm, r6, Role::LikelySpeakerKeyRole).toString(), kB2);
        }

        // Javítás után nincs több kétes sor: a 0-s üzenet.
        QVERIFY(vm.moveUtteranceToSpeaker(PairScene::id(6), kB2));
        QVERIFY(vm.pairOfferActive());                  // újra ajánlja (nem utasította el)
        QVERIFY(vm.moveUtteranceToSpeaker(PairScene::id(10), kB2));
        vm.recheckPair(kB1, kB2);
        QVERIFY(notices.last().at(0).toString().startsWith(
            QStringLiteral("A megerősített sorok alapján nem találtam kétséges sort Anna és Béla között.")));

        // „Most nem": erre a párra ebben a munkamenetben nem kérdez újra.
        vm.undo();
        vm.undo();
        QVERIFY(vm.moveUtteranceToSpeaker(PairScene::id(10), kB2));
        QVERIFY(vm.pairOfferActive());
        vm.declinePairOffer();
        QVERIFY(!vm.pairOfferActive());
        QVERIFY(vm.changeActive());                     // az átsorolás értesítése marad
        vm.undo();
        QVERIFY(vm.moveUtteranceToSpeaker(PairScene::id(10), kB2));
        QVERIFY(!vm.pairOfferActive());

        // Akadály: értesítésként elhangzik, nem fut.
        const int before = notices.count();
        QVERIFY(!vm.recheckPair(kB1, kB1).value(QStringLiteral("ran")).toBool());
        QCOMPARE(notices.count(), before + 1);

        // A panel pár-választója: a többi elnevezett beszélő.
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
        QVERIFY(vm.suggestionActive());                 // a rendes javaslat szól („Hasonló 2 sor is")
        QCOMPARE(vm.suggestionCount(), 2);
        QVERIFY(!vm.pairOfferActive());
        // A páros átnézés kézzel így is futtatható.
        const QVariantMap r = vm.recheckPair(kB1, kB2);
        QCOMPARE(r.value(QStringLiteral("flagged")).toInt(), 2);
        QVERIFY(r.value(QStringLiteral("centroidSimilarity")).toDouble() < 0.1);
    }

    // „Jó így, de ne használd mintának" és „Mintának használható" a soron.
    void noisy_manualConfirmAndClear()
    {
        Fixture fx;
        auto ed = fx.editor();
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(waitVoice(vm));
        QVERIFY(!cell(vm, 17, Role::NoisyRole).toBool());   // a forgatókönyvben nincs átfedés

        vm.setUncertainOnly(true);
        const int row = vm.rows()->rowOfUtterance(17);
        QVERIFY(vm.confirmRowNoisy(row));
        QCOMPARE(vm.uncertainCount(), 0);
        vm.setUncertainOnly(false);
        QVERIFY(cell(vm, 17, Role::NoisyRole).toBool());
        QVERIFY(!cell(vm, 17, Role::NoisyOverlapRole).toBool());
        QVERIFY(cell(vm, 17, Role::HeadRole).toBool());     // a pirula névsort kap
        QVERIFY(ed->utterance(uid(17)).confirmed);
        QCOMPARE(vm.undoText(), QStringLiteral("1 sor megerősítése (nem hangminta)"));

        QVERIFY(vm.setRowNoisy(17, false));
        QVERIFY(!cell(vm, 17, Role::NoisyRole).toBool());
        vm.undo();
        QVERIFY(cell(vm, 17, Role::NoisyRole).toBool());
        QVERIFY(vm.setUtteranceNoisy(uid(17), false));
        QVERIFY(!vm.setUtteranceNoisy(uid(17), false));     // már az
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
        // hogy a javított sor (amelyre a sáv javaslata vonatkozik) ne tűnjön el.
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

    // Az értesítő sáv adatai: mi történt, a „mind a N sora" folytatás (egy további lépés), és
    // hogy a visszavonás / más szerkesztés megszünteti.
    void changeNotice_lineMove_restOfSource_undo()
    {
        Fixture fx;
        auto ed = fx.editor(/*withEmbedder*/ false);
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(!vm.changeActive());
        QSignalSpy changes(&vm, &TranscriptEditorViewModel::changeChanged);

        // EGY sor (azonosítóval): csak az megy; a forrásnak (11-ből) 10 sora marad.
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
        QVERIFY(!vm.moveUtteranceToSpeaker(uid(4), kB2));  // már ott van: nincs lépés, a sáv marad
        QCOMPARE(changes.count(), 1);
        QVERIFY(!vm.moveUtteranceToSpeaker(QStringLiteral("nincs-ilyen"), kB2));

        // „Beszélő 1 mind a 10 sora": a megmaradt sorok is mennek — egy további lépés.
        const int serial = vm.changeSerial();
        QVERIFY(vm.moveRestOfSource());
        QCOMPARE(vm.speakerCount(), 1);
        QCOMPARE(vm.speakerInfo(kB2).value(QStringLiteral("utteranceCount")).toInt(), 18);
        QVERIFY(vm.changeSerial() > serial);
        QCOMPARE(vm.changeText(), QStringLiteral("Beszélő 1 mind a 10 sora átkerült ide: Beszélő 2"));
        QCOMPARE(vm.changeRestCount(), 0);                  // teljes beszélő után nincs folytatás
        QVERIFY(!vm.moveRestOfSource());

        vm.undoChange();                                    // csak a tömeges lépés
        QVERIFY(!vm.changeActive());
        QCOMPARE(vm.speakerCount(), 2);
        QCOMPARE(vm.speakerInfo(kB1).value(QStringLiteral("utteranceCount")).toInt(), 10);
        QCOMPARE(speakerOf(vm, 4), kB2);
        vm.undo();
        QCOMPARE(speakerOf(vm, 4), kB1);
        QVERIFY(!vm.canUndo());

        // Több sor vegyes forrásból: darabszám van, tömeges folytatás nincs.
        vm.selectRows(0, 1);                                // B1 + B2
        QVERIFY(vm.moveSelectionToPerson(QStringLiteral("Cili")));
        QCOMPARE(vm.changeText(), QStringLiteral("2 sor átkerült ide: Cili"));
        QCOMPARE(vm.changeRestCount(), 0);
        QCOMPARE(vm.changeSourceKey(), QString());
        // Más szerkesztés (megerősítés) megszünteti az értesítést; bezárni is lehet.
        QVERIFY(vm.confirmRow(5));
        QVERIFY(!vm.changeActive());
        QVERIFY(vm.moveUtteranceToNewParticipant(uid(2)));
        QVERIFY(vm.changeActive());
        QCOMPARE(vm.changeRestCount(), 9);
        vm.dismissChange();
        QVERIFY(!vm.changeActive());
        QVERIFY(vm.canUndo());

        // Teljes beszélő: a név nem kap ragot, a darabszám névelője a számhoz igazodik.
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

    // A javaslat a sávból fogadható el (egy lépés, új értesítéssel); a szűrőben a „Megmutatom"
    // a javasolt (nem bizonytalan) sorokat is láthatóvá teszi.
    void changeNotice_suggestion_inFilter()
    {
        Fixture fx;
        auto ed = fx.editor();
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(waitVoice(vm));
        vm.setUncertainOnly(true);
        const int filtered = vm.rows()->rowCount();

        // A 4. sor (C hangja a „Beszélő 1" címkén) új résztvevőhöz: javaslat a többi C-sorra.
        QVERIFY(vm.moveUtteranceToPerson(uid(4), QStringLiteral("Cili")));
        QVERIFY(vm.changeActive());
        QVERIFY(vm.suggestionActive());
        const int similar = vm.suggestionCount();
        QVERIFY(similar >= 3);
        QVERIFY(vm.rows()->rowOfUtterance(4) >= 0);         // a javított sor a szűrőben is látszik
        const int withAnchor = vm.rows()->rowCount();
        QVERIFY(withAnchor > filtered);

        vm.setSuggestionShown(true);                        // a javasolt sorok megjelennek
        QVERIFY(vm.rows()->rowOfUtterance(7) >= 0);
        QVERIFY(cell(vm, vm.rows()->rowOfUtterance(7), Role::SuggestedRole).toBool());
        vm.setSuggestionShown(false);
        QCOMPARE(vm.rows()->rowCount(), withAnchor);

        const QString target = vm.changeTargetKey();
        QVERIFY(vm.acceptSuggestion());
        QVERIFY(!vm.suggestionActive());
        QCOMPARE(vm.changeText(), QStringLiteral("%1 sor átkerült ide: Cili").arg(similar));
        QCOMPARE(vm.speakerInfo(target).value(QStringLiteral("utteranceCount")).toInt(), similar + 1);
        vm.undo();                                          // egy lépés: csak a javasoltak
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

    // A jelző állapota beszélőnként (áttekintő + sáv-fejléc), készítés után, és a most készült
    // lenyomat visszavonása: pontosan az az egy lenyomat törlődik.
    void voiceprint_indicatorState_createAndUndo()
    {
        Fixture fx;
        Voiceprint old;
        old.embedding = {0.0f, 1.0f, 0.0f};
        fx.prints->addPrint(QStringLiteral("Anna"), old);    // Annának már van egy régebbi mintája
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

        // Elég anyag: hány sor, hány másodperc — és nincs „hiányzik".
        const QVariantMap mat = vm.voiceprintMaterial(kB2);
        QVERIFY(mat.value(QStringLiteral("supported")).toBool());
        QVERIFY(mat.value(QStringLiteral("sufficient")).toBool());
        QVERIFY(mat.value(QStringLiteral("usableLines")).toInt() >= 3);
        QVERIFY(mat.value(QStringLiteral("usableSec")).toInt() >= 15);
        QCOMPARE(mat.value(QStringLiteral("missingSec")).toInt(), 0);
        QCOMPARE(mat.value(QStringLiteral("reason")).toString(), QString());

        // Készítés: a jelző azonnal „van"; az eredmény megmondja, miből készült.
        const QVariantMap r = vm.createVoiceprint(kB2);
        QVERIFY(r.value(QStringLiteral("ok")).toBool());
        const QString printId = r.value(QStringLiteral("printId")).toString();
        QVERIFY(!printId.isEmpty());
        QVERIFY(r.value(QStringLiteral("usedLines")).toInt() >= 3);
        QVERIFY(r.value(QStringLiteral("usedSec")).toInt() >= 15);
        QCOMPARE(fx.prints->printCount(QStringLiteral("Béla")), 1);
        QTRY_COMPARE(overviewVoiceprint(vm, kB2), QStringLiteral("has"));
        QCOMPARE(vm.lanes().at(laneOf(vm, kB2)).toMap().value(QStringLiteral("voiceprint")).toString(),
                 QStringLiteral("has"));

        // Visszavonás: pontosan ez a lenyomat törlődik (más személyé érintetlen), kétszer nem megy.
        QVERIFY(vm.removeVoiceprint(printId));
        QCOMPARE(fx.prints->printCount(QStringLiteral("Béla")), 0);
        QCOMPARE(fx.prints->printCount(QStringLiteral("Anna")), 1);
        QTRY_COMPARE(overviewVoiceprint(vm, kB2), QStringLiteral("none"));
        QVERIFY(!vm.removeVoiceprint(printId));
        QVERIFY(!vm.removeVoiceprint(QString()));
        // A lenyomat nem része az undo-veremnek: az elnevezés megmaradt, és visszavonható.
        QCOMPARE(vm.speakerInfo(kB2).value(QStringLiteral("personName")).toString(), QStringLiteral("Béla"));
        QVERIFY(vm.canUndo());
    }

    // Az értesítő sáv hanglenyomat-ajánlata: csak egy TELJES beszélő elnevezése / elnevezett
    // személybe olvasztása után, ha a személynek nincs lenyomata és itt van elég anyag.
    void changeBar_voiceprintOffer_onlyAfterNamingWholeSpeaker()
    {
        Fixture fx;
        auto ed = fx.editor();
        TranscriptEditorViewModel vm;
        vm.setEditor(ed.get());
        QVERIFY(waitVoice(vm));
        QSignalSpy notice(&vm, &TranscriptEditorViewModel::notice);

        // Egy sor, több sor, hasonló sorok: sosem ajánl.
        QVERIFY(vm.moveUtteranceToPerson(uid(4), QStringLiteral("Cili")));
        QVERIFY(vm.changeActive());
        QVERIFY(!vm.changeVoiceprintOffer());
        const QString cili = speakerOf(vm, 4);
        QVERIFY(vm.acceptSuggestion());                     // 7, 9, 14 is Cilihez: ez is soronkénti
        QVERIFY(vm.voiceprintMaterial(cili).value(QStringLiteral("sufficient")).toBool());
        QVERIFY(!vm.changeVoiceprintOffer());
        vm.selectRows(0, 2);
        QVERIFY(vm.moveSelectionToPerson(QStringLiteral("Dóra")));
        QVERIFY(!vm.changeVoiceprintOffer());
        QVERIFY(!vm.createVoiceprintFromChange());          // ajánlat nélkül nem készül semmi
        QCOMPARE(fx.prints->totalPrintCount(), 0);

        // Teljes beszélő elnevezése, elég anyaggal: ajánlat.
        QVERIFY(vm.reassignSpeaker(kB2, QStringLiteral("Béla"), false));
        QVERIFY(vm.changeVoiceprintOffer());
        QVERIFY(vm.changeUndoable());
        QVERIFY(!vm.changeVoiceprintCreated());
        QCOMPARE(fx.prints->totalPrintCount(), 0);          // az ajánlat magától nem készít

        // Elfogadva: a sáv kimondja, mi készült; a „Visszavonás" ekkor a lenyomatot törli.
        const int serial = vm.changeSerial();
        QVERIFY(vm.createVoiceprintFromChange());
        QCOMPARE(fx.prints->printCount(QStringLiteral("Béla")), 1);
        QVERIFY(vm.changeActive());
        QVERIFY(vm.changeVoiceprintCreated());
        QVERIFY(!vm.changeVoiceprintOffer());
        QVERIFY(vm.changeSerial() > serial);
        QVERIFY2(vm.changeText().startsWith(QStringLiteral("Hanglenyomat készült: Béla (")), qPrintable(vm.changeText()));
        QCOMPARE(notice.count(), 0);                        // a sáv maga a visszajelzés
        const QString undoBefore = vm.undoText();
        vm.undoChange();
        QCOMPARE(fx.prints->printCount(QStringLiteral("Béla")), 0);
        QVERIFY(vm.changeActive());
        QVERIFY(!vm.changeUndoable());
        QVERIFY(!vm.changeVoiceprintCreated());
        QVERIFY(!vm.changeVoiceprintOffer());
        QCOMPARE(vm.changeText(), QStringLiteral("A most készült hanglenyomat törölve: Béla"));
        QCOMPARE(vm.speakerInfo(kB2).value(QStringLiteral("personName")).toString(), QStringLiteral("Béla"));
        QCOMPARE(vm.undoText(), undoBefore);                // az undo-verem érintetlen

        // Elnevezett, lenyomat nélküli személybe olvasztás (összevonás; „mind a N sora"): ajánlat.
        QVERIFY(vm.moveUtteranceToSpeaker(uid(6), kB2));
        QVERIFY(!vm.changeVoiceprintOffer());
        QVERIFY(vm.changeRestCount() > 0);
        QVERIFY(vm.moveRestOfSource());
        QVERIFY(vm.changeVoiceprintOffer());
        vm.undo();
        QVERIFY(!vm.changeActive());

        // Kevés anyag: nincs ajánlat (nem nyaggatunk).
        const QString fresh = ed->moveUtterancesToNewParticipant({uid(16)});
        QVERIFY(!fresh.isEmpty());
        QVERIFY(vm.reassignSpeaker(fresh, QStringLiteral("Emma"), false));
        QVERIFY(vm.changeActive());
        QVERIFY(!vm.voiceprintMaterial(fresh).value(QStringLiteral("sufficient")).toBool());
        QVERIFY(!vm.changeVoiceprintOffer());

        // Akinek már van lenyomata: nincs ajánlat. Névtelenre állítás: nincs ajánlat.
        QVERIFY(vm.createVoiceprint(kB2).value(QStringLiteral("ok")).toBool());
        QVERIFY(vm.reassignSpeaker(cili, QStringLiteral("Béla"), false));    // Cili sorai Bélához olvadnak
        QVERIFY(vm.changeActive());
        QVERIFY(!vm.changeVoiceprintOffer());
        QVERIFY(vm.revertSpeakerToAnonymous(kB2, false));
        QVERIFY(!vm.changeVoiceprintOffer());
    }

    // Hangmodell nélkül nincs ajánlat és nincs készítés: az ok egyszer, érthetően elhangzik.
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
