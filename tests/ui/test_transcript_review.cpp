// A v3 Javítás mód nézetmodellje (handoff-v3 E1–E4) a beépített „v3" KITALÁLT meetingen (valódi
// SpeakerEditor, hamis hang, kitalált sáv-aktivitás): Átnézendő csoportok (sáv-ellentmondás,
// mag-eltérés, rövid sorok, a csoportokon kívüli kétes sorok), egy csoport végrehajtása = egy
// visszavonási lépés, szennyezett mag + szétválasztás, jelöltek bizonyítékkal, „Miért nem X?",
// kézi sáv-beosztás, új személy + hasonló sorok. Kijelző nélkül fut; a felhasználó adataihoz nem nyúl.
#include "CandidateListModel.h"
#include "ReviewGroupsModel.h"
#include "TranscriptEditorViewModel.h"
#include "TranscriptListModel.h"

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

// A háttér-elemzés egy újabb körének bevárása (a művelet után indul).
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

        // A csoport-modell: kártyák + az aktív csoport alatt a „··· N sor" sor.
        ReviewGroupsModel* m = vm.reviewGroups();
        QVERIFY(m->count() >= 4);
        QCOMPARE(m->data(m->index(0), ReviewGroupsModel::RowKindRole).toString(), QStringLiteral("group"));
        QCOMPARE(m->data(m->index(1), ReviewGroupsModel::RowKindRole).toString(), QStringLiteral("more"));

        // „Egyenként": a csoport sorai a kártya alatt; B a következő sorra lép.
        const QString gid = side->id;
        const int n = int(side->ids.size());
        m->setExpanded(gid, true);
        QCOMPARE(m->data(m->index(1), ReviewGroupsModel::RowKindRole).toString(), QStringLiteral("line"));
        const int first = m->rowOfLine(side->ids.first());
        QCOMPARE(first, 1);
        QCOMPARE(m->stepLine(first, 1), 2);

        // „Mind a N → Fehér Gábor": egy lépés, a változás-sáv kiírja; visszavonás után visszajön.
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

        // „Kihagyom": a csoport ebben a munkamenetben eltűnik.
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

        // Az E1 sora: Lilla nevén, Gábor hangja a hívásról.
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
        // A mostani beszélő a végén, halványan.
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

        // „Melyik sávon beszél?": két chip; a kézi beosztás egy lépés, visszavonható.
        const QString gabor = keyOf(vm, QStringLiteral("Fehér Gábor"));
        const QVariantList tracks = vm.trackOptions(gabor);
        QCOMPARE(tracks.size(), 2);
        qInfo() << "speakerEvidence" << vm.speakerEvidence(gabor) << vm.sideBasisText(gabor);
        QVERIFY(vm.setSpeakerTracks(gabor, {QStringLiteral("loopback")}));
        QVERIFY(vm.trackOptions(gabor).at(1).toMap().value(QStringLiteral("checked")).toBool());
        vm.undo();
        QVERIFY(!vm.trackOptions(gabor).at(1).toMap().value(QStringLiteral("checked")).toBool());

        // „Nem ő? Valójában…": a teljes beszélő jelöltjei (ő maga nincs köztük).
        CandidateListModel speaker;
        speaker.setEditor(&vm);
        speaker.setSpeakerKey(gabor);
        QVERIFY(speaker.count() > 0);
        for (int i = 0; i < speaker.count(); ++i)
            QVERIFY(speaker.get(i).value(QStringLiteral("speakerKey")).toString() != gabor);
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
        // Az új személy oszlopa jelölt; a sorai „új személy" jelölőt kapnak.
        QVERIFY(vm.rows()->data(vm.rows()->index(6), TranscriptListModel::NewPersonRole).toBool());

        const QString np = vm.newPersonKey();
        const int before = vm.speakerInfo(np).value(QStringLiteral("utteranceCount")).toInt();
        QVERIFY(vm.acceptNewPersonSimilar());
        QCOMPARE(vm.speakerInfo(np).value(QStringLiteral("utteranceCount")).toInt(),
                 before + similar.value(QStringLiteral("count")).toInt());
        vm.undo();
        QCOMPARE(vm.speakerInfo(np).value(QStringLiteral("utteranceCount")).toInt(), before);
    }
};

QTEST_MAIN(TestTranscriptReview)
#include "test_transcript_review.moc"
