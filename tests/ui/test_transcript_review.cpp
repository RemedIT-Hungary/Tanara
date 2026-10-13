// The v3 Edit mode's view model on the built-in MADE-UP "v3" meeting (real SpeakerEditor, fake
// audio, made-up track activity): review queue groups (track contradiction, core mismatch,
// short lines, doubtful lines outside the groups), executing a group = one undo step,
// contaminated core + splitting, candidates with evidence, "Why not X?", manual track
// assignment, new person + similar lines. Runs without a display; it touches no user data.
#include "CandidateListModel.h"
#include "tanara/edit/PeopleDirectory.h"
#include "TranscriptEditorViewModel.h"
#include "TranscriptListModel.h"

#include <QSignalSpy>
#include <QtTest>

using namespace tanara;
using namespace tanara_qml;

namespace {

bool waitIdle(const TranscriptEditorViewModel& vm)
{
    return QTest::qWaitFor([&] {
        return !vm.embeddingRunning() && vm.editor() && vm.editor()->embeddingsComplete() && !vm.reviewRunning();
    }, 15000);
}

// Waiting for another round of the background analysis (it starts after the action).
bool settle(const TranscriptEditorViewModel& vm)
{
    QTest::qWait(20);
    return QTest::qWaitFor([&] { return !vm.reviewRunning(); }, 15000);
}

const TranscriptEditorViewModel::ReviewView* viewOf(const TranscriptEditorViewModel& vm, const QString& kind)
{
    for (const auto& v : vm.reviewViews())
        if (v.kind == kind) return &v;
    return nullptr;
}

QString keyOf(const TranscriptEditorViewModel& vm, const QString& name)
{
    for (const QVariant& s : vm.speakers())
        if (s.toMap().value(QStringLiteral("name")).toString() == name)
            return s.toMap().value(QStringLiteral("key")).toString();
    return {};
}

} // namespace

class TestTranscriptReview : public QObject {
    Q_OBJECT

private slots:
    void groups_applyIsOneUndoStep()
    {
        TranscriptEditorViewModel vm;
        vm.setDemoVariant(QStringLiteral("v3"));
        vm.resolveSession();
        QVERIFY(waitIdle(vm));
        vm.applyDemoState(QStringLiteral("reviewGroups"));
        QVERIFY(settle(vm));
        QVERIFY(vm.uncertainOnly());

        for (const auto& v : vm.reviewViews())
            qInfo().noquote() << v.kind << v.ids.size() << v.title << "|" << v.subtitle;
        qInfo() << "contaminated" << vm.contaminated();

        const auto* side = viewOf(vm, QStringLiteral("sideConflict"));
        QVERIFY(side);
        QCOMPARE(vm.displayName(side->currentKey), QStringLiteral("Kovács Lilla"));
        QCOMPARE(vm.displayName(side->proposedKey), QStringLiteral("Fehér Gábor"));
        QVERIFY(viewOf(vm, QStringLiteral("coreMismatch")));
        const auto* shortLines = viewOf(vm, QStringLiteral("shortLines"));
        QVERIFY(shortLines && !shortLines->actionable);
        QVERIFY(vm.reviewCount() > 0);
        QVERIFY(!vm.contaminated().isEmpty());
        QCOMPARE(vm.contaminated().value(QStringLiteral("name")).toString(), QStringLiteral("Fehér Gábor"));

        const QString gid = side->id;
        const int n = int(side->ids.size());

        // "All N to Fehér Gábor": one step, the change bar says so; after undo it comes back.
        const QString lilla = side->currentKey;
        const QString gabor = side->proposedKey;
        const int before = vm.speakerInfo(gabor).value(QStringLiteral("utteranceCount")).toInt();
        QVERIFY(vm.applyReviewGroup(gid));
        QVERIFY(vm.changeActive());
        QCOMPARE(vm.speakerInfo(gabor).value(QStringLiteral("utteranceCount")).toInt(), before + n);
        QVERIFY(settle(vm));
        QVERIFY(!viewOf(vm, QStringLiteral("sideConflict")));
        vm.undo();
        QCOMPARE(vm.speakerInfo(gabor).value(QStringLiteral("utteranceCount")).toInt(), before);
        QVERIFY(settle(vm));
        QVERIFY(viewOf(vm, QStringLiteral("sideConflict")));
        Q_UNUSED(lilla);

        // "Skip": the group disappears in this session.
        const int count = vm.reviewCount();
        vm.skipReviewGroup(gid);
        QVERIFY(!viewOf(vm, QStringLiteral("sideConflict")));
        QCOMPARE(vm.reviewCount(), count - n);
    }

    void contaminatedCore_split()
    {
        TranscriptEditorViewModel vm;
        vm.setDemoVariant(QStringLiteral("v3"));
        vm.resolveSession();
        QVERIFY(waitIdle(vm));
        vm.applyDemoState(QStringLiteral("contaminatedCore"));
        QVERIFY(settle(vm));
        const QString key = vm.contaminated().value(QStringLiteral("speakerKey")).toString();
        QVERIFY(!key.isEmpty());
        const int lines = vm.speakerInfo(key).value(QStringLiteral("utteranceCount")).toInt();
        QVERIFY(vm.splitSpeaker(key));
        QVERIFY(vm.speakerInfo(key).value(QStringLiteral("utteranceCount")).toInt() < lines);
        QVERIFY(vm.changeActive());
        QVERIFY(settle(vm));
        QVERIFY(vm.contaminated().isEmpty());
        vm.undo();
        QCOMPARE(vm.speakerInfo(key).value(QStringLiteral("utteranceCount")).toInt(), lines);
    }

    void lineCandidates_evidence_whyNot_tracks()
    {
        TranscriptEditorViewModel vm;
        vm.setDemoVariant(QStringLiteral("v3"));
        vm.resolveSession();
        QVERIFY(waitIdle(vm));
        vm.applyDemoState(QStringLiteral("fixLinePopover"));
        QVERIFY(settle(vm));

        // The picker row: under Lilla's name, Gábor's voice from the call.
        const QString line = vm.rowUtteranceId(2);
        QCOMPARE(vm.rows()->data(vm.rows()->index(2), TranscriptListModel::UncertainReasonRole).toString(),
                 QStringLiteral("side"));
        QVERIFY(vm.rows()->data(vm.rows()->index(2), TranscriptListModel::SideConflictRole).toBool());
        QVERIFY(vm.rows()->data(vm.rows()->index(0), TranscriptListModel::ConfirmedRole).toBool());
        QVERIFY(vm.rows()->data(vm.rows()->index(1), TranscriptListModel::ShortRole).toBool());

        CandidateListModel cands;
        cands.setLimit(3);
        cands.setEditor(&vm);
        cands.setUtteranceId(line);
        QVERIFY(cands.count() >= 2);
        const QVariantMap top = cands.get(0);
        qInfo() << "top" << top << cands.data(cands.index(0), CandidateListModel::EvidenceRole);
        QCOMPARE(top.value(QStringLiteral("name")).toString(), QStringLiteral("Fehér Gábor"));
        QCOMPARE(cands.rowForKey(1), 0);
        // The search finds by nickname too (matching the other person lists), the hit is in the substring.
        vm.setPeopleProvider([] {
            PersonInfo p;
            p.name = QStringLiteral("Fehér Gábor");
            p.aliases = QStringList{QStringLiteral("Gabi")};
            return QVector<PersonInfo>{p};
        });
        cands.setQuery(QStringLiteral("gabi"));
        QVERIFY(cands.count() >= 1);
        QCOMPARE(cands.get(0).value(QStringLiteral("name")).toString(), QStringLiteral("Fehér Gábor"));
        QVERIFY(cands.data(cands.index(0), CandidateListModel::SubTextRole).toString().contains(QStringLiteral("Gabi")));
        cands.setQuery(QStringLiteral("zzz"));
        QCOMPARE(cands.count(), 0);
        cands.setQuery(QString());
        vm.setPeopleProvider({});
        // The current speaker at the end, dimmed.
        const int last = cands.count() - 1;
        QVERIFY(cands.data(cands.index(last), CandidateListModel::CurrentRole).toBool());
        QVERIFY(cands.data(cands.index(last), CandidateListModel::DimmedRole).toBool());

        const QVariantList why = vm.whyNot(line);
        qInfo() << "whyNot" << why;
        QVERIFY(!why.isEmpty());
        bool sideReason = false;
        for (const QVariant& w : why)
            sideReason |= w.toMap().value(QStringLiteral("fixTarget")).toString() == QLatin1String("tracks");
        QVERIFY(sideReason);

        // "Which track does it speak on?": two chips; manual assignment is one step, undoable.
        const QString gabor = keyOf(vm, QStringLiteral("Fehér Gábor"));
        const QVariantList tracks = vm.trackOptions(gabor);
        QCOMPARE(tracks.size(), 2);
        qInfo() << "speakerEvidence" << vm.speakerEvidence(gabor) << vm.sideBasisText(gabor);
        QVERIFY(vm.setSpeakerTracks(gabor, {QStringLiteral("loopback")}));
        QVERIFY(vm.trackOptions(gabor).at(1).toMap().value(QStringLiteral("checked")).toBool());
        vm.undo();
        QVERIFY(!vm.trackOptions(gabor).at(1).toMap().value(QStringLiteral("checked")).toBool());

        // "Not them? Actually...": the whole speaker's candidates (they themselves are not among them).
        CandidateListModel speaker;
        speaker.setEditor(&vm);
        speaker.setSpeakerKey(gabor);
        QVERIFY(speaker.count() > 0);
        for (int i = 0; i < speaker.count(); ++i)
            QVERIFY(speaker.get(i).value(QStringLiteral("speakerKey")).toString() != gabor);
    }

    // If every named speaker is assigned by hand, a person not on the line's side is excluded:
    // at the very BOTTOM of the popup list, dimmed, with a "not them by their track" chip, with no
    // shortcut, but selectable.
    void lineCandidates_sideVeto()
    {
        TranscriptEditorViewModel vm;
        vm.setDemoVariant(QStringLiteral("v3"));
        vm.resolveSession();
        QVERIFY(waitIdle(vm));
        vm.applyDemoState(QStringLiteral("fixLinePopover"));
        QVERIFY(settle(vm));
        const QString line = vm.rowUtteranceId(2);   // under Lilla's name, in the call's voice
        const QString gabor = keyOf(vm, QStringLiteral("Fehér Gábor"));
        int named = 0;
        for (const QVariant& v : vm.speakers()) {
            const QVariantMap sp = v.toMap();
            if (sp.value(QStringLiteral("anonymous")).toBool()) continue;
            const QString key = sp.value(QStringLiteral("key")).toString();
            QVERIFY(vm.setSpeakerTracks(key, {key == gabor ? QStringLiteral("loopback") : QStringLiteral("mic")}));
            ++named;
        }
        QVERIFY(named >= 3);
        QVERIFY(settle(vm));

        CandidateListModel cands;
        cands.setLimit(3);
        cands.setEditor(&vm);
        cands.setUtteranceId(line);
        QVERIFY(cands.count() >= 3);
        QCOMPARE(cands.get(0).value(QStringLiteral("name")).toString(), QStringLiteral("Fehér Gábor"));
        int firstVetoed = -1;
        for (int i = 0; i < cands.count(); ++i) {
            const QModelIndex ix = cands.index(i);
            const bool vetoed = cands.data(ix, CandidateListModel::VetoedRole).toBool();
            qInfo() << i << cands.get(i).value(QStringLiteral("name")).toString() << "vetoed" << vetoed
                    << cands.data(ix, CandidateListModel::EvidenceRole);
            if (!vetoed) {
                QCOMPARE(firstVetoed, -1);   // no non-excluded one after the excluded
                continue;
            }
            if (firstVetoed < 0) firstVetoed = i;
            QVERIFY(cands.data(ix, CandidateListModel::DimmedRole).toBool());
            QVERIFY(cands.data(ix, CandidateListModel::KeyHintRole).toString().isEmpty());
            bool chip = false;
            for (const QVariant& e : cands.data(ix, CandidateListModel::EvidenceRole).toList())
                chip |= e.toMap().value(QStringLiteral("text")).toString() == QStringLiteral("a sávja szerint nem ő")
                     && e.toMap().value(QStringLiteral("polarity")).toString() == QLatin1String("contradict");
            QVERIFY(chip);
            QVERIFY(cands.get(i).value(QStringLiteral("vetoed")).toBool());
        }
        QVERIFY(firstVetoed > 0);
        // The current speaker (Lilla, microphone) is excluded too: "current", with the exclusion chip.
        bool currentVetoChip = false;
        for (int i = 0; i < cands.count(); ++i) {
            if (!cands.data(cands.index(i), CandidateListModel::CurrentRole).toBool()) continue;
            for (const QVariant& e : cands.data(cands.index(i), CandidateListModel::EvidenceRole).toList())
                currentVetoChip |= e.toMap().value(QStringLiteral("text")).toString() == QStringLiteral("a sávja szerint nem ő");
        }
        QVERIFY(currentVetoChip);
    }

    // Quick "+ #Tag": the meeting's tag, if the person does not have it yet; the own person and
    // anyone who already has it get no offer. After applying, the candidates are recomputed.
    void quickTag_offeredOnlyWhenMissing()
    {
        TranscriptEditorViewModel vm;
        vm.setDemoVariant(QStringLiteral("v3"));
        vm.resolveSession();
        QVERIFY(waitIdle(vm));
        QVERIFY(vm.quickTagFor(QStringLiteral("Kovács Lilla")).isEmpty());     // own person
        QVERIFY(vm.quickTagFor(QStringLiteral("Fehér Gábor")).isEmpty());      // already #Nordvik
        QVERIFY(vm.quickTagFor(QString()).isEmpty());
        const QVariantMap q = vm.quickTagFor(QStringLiteral("Molnár Eszter"));
        QCOMPARE(q.value(QStringLiteral("tagId")).toString(), QStringLiteral("nordvik"));
        QCOMPARE(q.value(QStringLiteral("name")).toString(), QStringLiteral("Nordvik"));
        QCOMPARE(q.value(QStringLiteral("label")).toString(), QStringLiteral("+ #Nordvik"));

        QSignalSpy review(&vm, &TranscriptEditorViewModel::reviewChanged);
        QVERIFY(vm.addQuickTag(QStringLiteral("Molnár Eszter"), QStringLiteral("nordvik")));
        QVERIFY(review.count() > 0);
        QVERIFY(vm.quickTagFor(QStringLiteral("Molnár Eszter")).isEmpty());
        QVERIFY(vm.editor()->personTagIds(QStringLiteral("Molnár Eszter")).contains(QStringLiteral("nordvik")));
        QVERIFY(!vm.addQuickTag(QStringLiteral("Kovács Lilla"), QStringLiteral("nordvik")));
        QVERIFY(settle(vm));
        // The tag evidence already shows the shared tag.
        bool shared = false;
        for (const QVariant& e : vm.speakerEvidence(keyOf(vm, QStringLiteral("Molnár Eszter"))))
            shared |= e.toMap().value(QStringLiteral("kind")).toString() == QLatin1String("tag")
                   && e.toMap().value(QStringLiteral("polarity")).toString() == QLatin1String("support");
        QVERIFY(shared);
    }

    // The "speakerWhy" demo: Gábor on two tracks (manual, several tracks), Árpád has no #Nordvik yet.
    void speakerWhyDemo_multiTrackAndQuickTag()
    {
        TranscriptEditorViewModel vm;
        vm.setDemoVariant(QStringLiteral("v3"));
        vm.resolveSession();
        QVERIFY(waitIdle(vm));
        vm.applyDemoState(QStringLiteral("speakerWhy"));
        QVERIFY(settle(vm));
        const QString gabor = keyOf(vm, QStringLiteral("Fehér Gábor"));
        QCOMPARE(vm.editor()->trackAssignment(gabor), (QStringList{QStringLiteral("mic"), QStringLiteral("loopback")}));
        const QVariantList tracks = vm.trackOptions(gabor);
        QCOMPARE(tracks.size(), 2);
        QVERIFY(tracks.at(0).toMap().value(QStringLiteral("checked")).toBool());
        QVERIFY(tracks.at(1).toMap().value(QStringLiteral("checked")).toBool());
        QCOMPARE(vm.quickTagFor(QStringLiteral("Varga Árpád")).value(QStringLiteral("name")).toString(),
                 QStringLiteral("Nordvik"));
    }

    // Recheck: the result says what it relied on (lines, samples, models).
    void recheck_reportsBasis()
    {
        TranscriptEditorViewModel vm;
        vm.setDemoVariant(QStringLiteral("v3"));
        vm.resolveSession();
        QVERIFY(waitIdle(vm));
        vm.applyDemoState(QStringLiteral("markers"));
        QVERIFY(settle(vm));
        const QVariantMap r = vm.recheckSpeakers();
        qInfo() << r;
        QVERIFY(r.value(QStringLiteral("ran")).toBool());
        QVERIFY(r.value(QStringLiteral("basisSummary")).toString().startsWith(QStringLiteral("Alapja: ")));
        QVERIFY(r.value(QStringLiteral("basisSummary")).toString().contains(QStringLiteral("megerősített sor")));
        QVERIFY(!r.value(QStringLiteral("models")).toStringList().isEmpty());
        QVERIFY(r.value(QStringLiteral("message")).toString().contains(r.value(QStringLiteral("basisSummary")).toString()));
        QVERIFY(!r.value(QStringLiteral("message")).toString().contains(QStringLiteral("Bizonytalan")));
    }

    void newPerson_similarLines()
    {
        TranscriptEditorViewModel vm;
        vm.setDemoVariant(QStringLiteral("v3"));
        vm.resolveSession();
        QVERIFY(waitIdle(vm));
        vm.applyDemoState(QStringLiteral("newPersonSimilar"));
        QVERIFY(settle(vm));
        QVERIFY(!vm.newPersonKey().isEmpty());
        QCOMPARE(vm.displayName(vm.newPersonKey()), QStringLiteral("Nagy Péter"));
        QVERIFY(vm.changeNewPerson());
        QVERIFY(vm.changeText().contains(QStringLiteral("Nagy Péter")));
        const QVariantMap similar = vm.newPersonSimilar();
        qInfo() << "similar" << similar << vm.newPersonReadiness();
        QVERIFY(similar.value(QStringLiteral("count")).toInt() > 0);
        QVERIFY(vm.similarShown());
        QVERIFY(!vm.similarMarks().isEmpty());
        // The new person's column is marked; its lines get a "new person" marker.
        QVERIFY(vm.rows()->data(vm.rows()->index(6), TranscriptListModel::NewPersonRole).toBool());

        const QString np = vm.newPersonKey();
        const int before = vm.speakerInfo(np).value(QStringLiteral("utteranceCount")).toInt();
        QVERIFY(vm.acceptNewPersonSimilar());
        QCOMPARE(vm.speakerInfo(np).value(QStringLiteral("utteranceCount")).toInt(),
                 before + similar.value(QStringLiteral("count")).toInt());
        vm.undo();
        QCOMPARE(vm.speakerInfo(np).value(QStringLiteral("utteranceCount")).toInt(), before);
    }

    // A new person on a tagged meeting: the change bar asks once.
    void newPerson_tagQuestion()
    {
        TranscriptEditorViewModel vm;
        vm.setDemoVariant(QStringLiteral("v3"));
        vm.resolveSession();
        QVERIFY(waitIdle(vm));
        vm.applyDemoState(QStringLiteral("newPersonTag"));
        QVERIFY(settle(vm));
        QVERIFY(vm.changeNewPerson());
        QVariantMap q = vm.changeTagQuestion();
        QCOMPARE(q.value(QStringLiteral("text")).toString(),
                 QStringLiteral("Nagy Péter új személy. Ráteszem a #Nordvik címkét?"));
        QVERIFY(q.value(QStringLiteral("pairs")).toList().isEmpty());
        vm.answerChangeTag(true);
        q = vm.changeTagQuestion();
        QCOMPARE(q.value(QStringLiteral("answer")).toString(),
                 QStringLiteral("Címke a személyen: #Nordvik · Nagy Péter"));
        vm.undoChangeTagAnswer();
        QVERIFY(vm.changeTagQuestion().isEmpty());
        QVERIFY(vm.changeActive());                     // the reassignment notice stays

        // In the similar-lines demo (and in any untagged situation) there is no question.
        TranscriptEditorViewModel other;
        other.setDemoVariant(QStringLiteral("v3"));
        other.resolveSession();
        QVERIFY(waitIdle(other));
        other.applyDemoState(QStringLiteral("newPersonSimilar"));
        QVERIFY(settle(other));
        QVERIFY(other.changeTagQuestion().isEmpty());
    }
};

QTEST_MAIN(TestTranscriptReview)
#include "test_transcript_review.moc"
