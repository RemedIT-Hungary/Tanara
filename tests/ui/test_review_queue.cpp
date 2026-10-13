// Az Átnézendő munkalistája (S3, ReviewQueueModel) a beépített „v3" KITALÁLT meetingen (valódi
// SpeakerEditor, hamis hang): fagyasztott időrendi pillanatkép, kurzor, Később, beérkező halmaz,
// beemelés a végére, alreadyDecided, javaslat-frissülés, kör-zárás + következmény-jelölés,
// mintázat-számláló, nyitó-javaslatok, demó-állapotok. Kijelző nélkül fut; a felhasználó adataihoz
// nem nyúl. A „kézzel összerakott" csoportos esetek (syncFromReview / startFrom explicit bemenettel)
// az eseményhurok pörgetése NÉLKÜL futnak, hogy a háttér-elemzés ne írja felül a bemenetet.
#include "ReviewQueueModel.h"
#include "TranscriptEditorViewModel.h"

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

bool settle(const TranscriptEditorViewModel& vm)
{
    QTest::qWait(20);
    return QTest::qWaitFor([&] { return !vm.reviewRunning(); }, 15000);
}

bool openV3(TranscriptEditorViewModel& vm, const QString& state = QStringLiteral("reviewExpanded"))
{
    vm.setDemoVariant(QStringLiteral("v3"));
    vm.resolveSession();
    if (!waitIdle(vm)) return false;
    if (state.isEmpty()) return true;
    vm.applyDemoState(state);
    return settle(vm);
}

QStringList idsOf(const ReviewQueueModel* q)
{
    QStringList out;
    for (int r = 0; r < q->rowCount(); ++r) out << q->data(q->index(r), ReviewQueueModel::UtteranceIdRole).toString();
    return out;
}

QVariant role(const ReviewQueueModel* q, int row, int r)
{
    return q->data(q->index(row), r);
}

QString stateAt(const ReviewQueueModel* q, int row)
{
    return role(q, row, ReviewQueueModel::StateRole).toString();
}

bool flagged(const EditorUtterance& u)
{
    return u.confirmed || u.manuallyCorrected || u.ignored;
}

// Egy beszélő, aki nem `except`.
QString otherSpeaker(const TranscriptEditorViewModel& vm, const QString& except)
{
    for (const QVariant& s : vm.speakers()) {
        const QString key = s.toMap().value(QStringLiteral("key")).toString();
        if (key != except && vm.speakerView(key).lane >= 0) return key;
    }
    return {};
}

int countState(const ReviewQueueModel* q, const QString& state)
{
    int n = 0;
    for (int r = 0; r < q->rowCount(); ++r) n += stateAt(q, r) == state ? 1 : 0;
    return n;
}

} // namespace

class TestReviewQueue : public QObject {
    Q_OBJECT

private slots:
    // A pillanatkép időrendi; döntés után a sor a HELYÉN marad, a kurzor a következőre lép, és
    // a háttér-elemzés után sem változik sem a sorrend, sem a sorok száma.
    void snapshot_listStaysAfterDecisions()
    {
        TranscriptEditorViewModel vm;
        QVERIFY(openV3(vm));
        ReviewQueueModel* q = vm.reviewQueue();
        QVERIFY(!vm.queueActive());
        q->start();
        QVERIFY(vm.queueActive());
        QVERIFY2(q->count() >= 6, qPrintable(QString::number(q->count())));
        qInfo() << "queue" << q->count() << "views" << vm.reviewViews().size();
        const QStringList order = idsOf(q);
        for (int i = 1; i < order.size(); ++i)
            QVERIFY(vm.utteranceIndexOf(order[i - 1]) < vm.utteranceIndexOf(order[i]));
        for (int r = 0; r < q->count(); ++r) QCOMPARE(stateAt(q, r), QStringLiteral("pending"));
        QCOMPARE(q->currentIndex(), 0);
        QCOMPARE(q->currentUtterance(), order[0]);
        QVERIFY(role(q, 0, ReviewQueueModel::CurrentRole).toBool());
        QVERIFY(!role(q, 0, ReviewQueueModel::ReasonKindRole).toString().isEmpty());

        // „Jó így" → decided a helyén, kurzor → 1.
        QVERIFY(vm.confirmUtterance(order[0]));
        QCOMPARE(stateAt(q, 0), QStringLiteral("decided"));
        QCOMPARE(role(q, 0, ReviewQueueModel::DecidedKindRole).toString(), QStringLiteral("confirmed"));
        QCOMPARE(q->currentIndex(), 1);
        // Áthelyezés (a javaslatra, ha van) → decided / corrected, kurzor → 2.
        const QString cur1 = role(q, 1, ReviewQueueModel::SpeakerKeyRole).toString();
        QString target = role(q, 1, ReviewQueueModel::LikelyKeyRole).toString();
        if (target.isEmpty() || target == cur1) target = otherSpeaker(vm, cur1);
        QVERIFY(vm.moveUtteranceToSpeaker(order[1], target));
        QCOMPARE(stateAt(q, 1), QStringLiteral("decided"));
        QCOMPARE(role(q, 1, ReviewQueueModel::DecidedKindRole).toString(), QStringLiteral("corrected"));
        QCOMPARE(role(q, 1, ReviewQueueModel::OriginalKeyRole).toString(), cur1);
        QCOMPARE(q->currentIndex(), 2);
        // Nem azonosítható → ignored, kurzor → 3.
        QVERIFY(vm.setUtteranceIgnored(order[2], true));
        QCOMPARE(stateAt(q, 2), QStringLiteral("ignored"));
        QCOMPARE(q->currentIndex(), 3);
        QCOMPARE(q->decidedCount(), 2);
        QCOMPARE(q->ignoredCount(), 1);

        // A háttér-elemzés újraszámol: a munkalista nem mozdul.
        QVERIFY(settle(vm));
        QCOMPARE(idsOf(q), order);
        QCOMPARE(q->currentIndex(), 3);
        QCOMPARE(stateAt(q, 0), QStringLiteral("decided"));
        QCOMPARE(stateAt(q, 1), QStringLiteral("decided"));
        QCOMPARE(stateAt(q, 2), QStringLiteral("ignored"));
        qInfo() << "incoming after decisions" << q->incomingCount();
        for (const QString& id : q->incomingIds()) QCOMPARE(q->indexOf(id), -1);
    }

    void cursor_later_undo()
    {
        TranscriptEditorViewModel vm;
        QVERIFY(openV3(vm));
        ReviewQueueModel* q = vm.reviewQueue();
        q->start();
        const QStringList order = idsOf(q);
        const int n = q->count();
        QVERIFY(n >= 5);

        QVERIFY(q->next());
        QCOMPARE(q->currentIndex(), 1);
        QVERIFY(q->previous());
        QCOMPARE(q->currentIndex(), 0);
        QVERIFY(!q->previous());
        QVERIFY(q->jumpTo(2));
        QCOMPARE(q->currentUtterance(), order[2]);
        QVERIFY(!q->jumpTo(n));
        QVERIFY(q->jumpTo(0));

        // Később: a sor a VÉGÉRE, `later`; a kurzor a következő nyitott sorra (ami most a 0. helyen áll).
        QVERIFY(q->later());
        QCOMPARE(q->count(), n);
        QCOMPARE(idsOf(q).last(), order[0]);
        QCOMPARE(stateAt(q, n - 1), QStringLiteral("later"));
        QCOMPARE(q->laterCount(), 1);
        QCOMPARE(q->currentIndex(), 0);
        QCOMPARE(q->currentUtterance(), order[1]);
        QCOMPARE(role(q, n - 1, ReviewQueueModel::OrderRole).toInt(), n - 1);

        // Döntés → következő; vissza a döntöttre, ott újradöntés → a következő nyitottra.
        QVERIFY(vm.confirmUtterance(order[1]));
        QCOMPARE(q->currentUtterance(), order[2]);
        QVERIFY(q->previous());
        QCOMPARE(q->currentUtterance(), order[1]);
        QCOMPARE(stateAt(q, 0), QStringLiteral("decided"));
        const QString cur = role(q, 0, ReviewQueueModel::SpeakerKeyRole).toString();
        QVERIFY(vm.moveUtteranceToSpeaker(order[1], otherSpeaker(vm, cur)));
        QCOMPARE(stateAt(q, 0), QStringLiteral("decided"));
        QCOMPARE(q->currentUtterance(), order[2]);

        // Visszavonás: a legutóbbi „Jó így" vissza → a sor újra pending, a kurzor rá ugrik.
        QVERIFY(vm.confirmUtterance(order[2]));
        QCOMPARE(q->currentUtterance(), order[3]);
        vm.undo();
        QCOMPARE(stateAt(q, 1), QStringLiteral("pending"));
        QCOMPARE(q->currentUtterance(), order[2]);
        QCOMPARE(idsOf(q).mid(0, 3), (QStringList{order[1], order[2], order[3]}));

        // A later sorra is lehet ugrani, és ott dönteni.
        QVERIFY(q->jumpTo(n - 1));
        QVERIFY(vm.confirmUtterance(order[0]));
        QCOMPARE(stateAt(q, n - 1), QStringLiteral("decided"));
        QCOMPARE(q->laterCount(), 0);
    }

    // Beérkező halmaz (+N új vár), beemelés a VÉGÉRE, alreadyDecided, javaslat-frissülés.
    void incoming_admit_alreadyDecided_suggestionUpdated()
    {
        TranscriptEditorViewModel vm;
        QVERIFY(openV3(vm));
        ReviewQueueModel* q = vm.reviewQueue();
        q->start();
        const QStringList order = idsOf(q);
        const int n = q->count();
        QVERIFY(n >= 5);
        QVERIFY(vm.confirmUtterance(order[0]));
        QCOMPARE(q->currentIndex(), 1);

        QVector<TranscriptEditorViewModel::ReviewView> views = vm.reviewViews();
        QVector<EditorUtterance> utts = vm.utterances();
        // Egy új kétes sor (a munkalistán kívül).
        QString newId;
        for (EditorUtterance& u : utts) {
            if (q->indexOf(u.id) >= 0 || flagged(u) || u.uncertain) continue;
            bool inView = false;
            for (const auto& v : views) inView = inView || v.ids.contains(u.id);
            if (inView) continue;
            u.uncertain = true;
            u.likelySpeakerKey.clear();
            newId = u.id;
            break;
        }
        QVERIFY(!newId.isEmpty());
        // A 3. sor a háttérben már nem kétes.
        const QString alreadyId = order[3];
        for (auto& v : views) v.ids.removeAll(alreadyId);
        utts[vm.utteranceIndexOf(alreadyId)].uncertain = false;
        // A 2. sor javaslata megváltozik.
        const QString suggId = order[2];
        const QString oldLikely = role(q, 2, ReviewQueueModel::LikelyKeyRole).toString();
        const QString cur2 = role(q, 2, ReviewQueueModel::SpeakerKeyRole).toString();
        QString newLikely;
        for (const QVariant& s : vm.speakers()) {
            const QString key = s.toMap().value(QStringLiteral("key")).toString();
            if (key != oldLikely && key != cur2) { newLikely = key; break; }
        }
        TranscriptEditorViewModel::ReviewView fake;
        fake.id = QStringLiteral("test:sugg");
        fake.kind = QStringLiteral("coreMismatch");
        fake.ids = {suggId};
        fake.currentKey = cur2;
        fake.proposedKey = newLikely;
        views.prepend(fake);

        QSignalSpy sugg(q, &ReviewQueueModel::suggestionUpdated);
        QSignalSpy inc(q, &ReviewQueueModel::incomingChanged);
        q->syncFromReview(views, utts);
        QCOMPARE(idsOf(q), order);
        QCOMPARE(q->count(), n);
        QCOMPARE(q->currentIndex(), 1);
        QCOMPARE(q->incomingCount(), 1);
        QCOMPARE(q->incomingIds(), QStringList{newId});
        QVERIFY(inc.count() >= 1);
        QCOMPARE(stateAt(q, 3), QStringLiteral("alreadyDecided"));
        QCOMPARE(q->alreadyDecidedCount(), 1);
        QVERIFY(role(q, 2, ReviewQueueModel::SuggestionUpdatedRole).toBool());
        QCOMPARE(role(q, 2, ReviewQueueModel::LikelyKeyRole).toString(), newLikely);
        QCOMPARE(sugg.count(), 1);
        QCOMPARE(sugg.first().first().toString(), suggId);
        QVERIFY(!role(q, 1, ReviewQueueModel::SuggestionUpdatedRole).toBool());

        // Beemelés: a VÉGÉRE, a kurzor nem mozdul.
        QCOMPARE(q->admitIncoming(), 1);
        QCOMPARE(q->count(), n + 1);
        QCOMPARE(idsOf(q).mid(0, n), order);
        QCOMPARE(idsOf(q).last(), newId);
        QVERIFY(role(q, n, ReviewQueueModel::AdmittedRole).toBool());
        QCOMPARE(stateAt(q, n), QStringLiteral("pending"));
        QCOMPARE(q->currentIndex(), 1);
        QCOMPARE(q->incomingCount(), 0);

        // alreadyDecided (S7): nem nyitott — a kurzor átugorja; ha rááll, a Később tovább lép (a
        // sor a helyén marad).
        const int open = q->openCount();
        QVERIFY(q->jumpTo(2));
        QVERIFY(q->advance());
        QVERIFY(q->currentIndex() != 3);
        QCOMPARE(q->nextOpenIndex() == 3, false);
        QCOMPARE(q->confirmableIds().contains(alreadyId), vm.utterances()[vm.utteranceIndexOf(alreadyId)].endMs
                     - vm.utterances()[vm.utteranceIndexOf(alreadyId)].startMs >= 1500);
        QVERIFY(q->jumpTo(4));
        QVERIFY(q->retreat());
        QVERIFY(q->currentIndex() != 3);
        QVERIFY(q->jumpTo(3));
        QVERIFY(q->later());
        QCOMPARE(idsOf(q).at(3), alreadyId);
        QCOMPARE(stateAt(q, 3), QStringLiteral("alreadyDecided"));
        QCOMPARE(q->openCount(), open);
        QCOMPARE(q->currentIndex(), 4);
        // Döntés a magától eldőlt sor előtt: a kurzor átugorja.
        QVERIFY(q->jumpTo(2));
        QVERIFY(vm.confirmUtterance(order[2]));
        QCOMPARE(q->currentIndex(), 4);
        // „Enter: a név marad": a megerősítés decided-dé teszi.
        QVERIFY(q->jumpTo(3));
        QVERIFY(vm.confirmUtterance(alreadyId));
        QCOMPARE(stateAt(q, 3), QStringLiteral("decided"));
    }

    // Kör-zárás: számok, minta-növekmény, következmény-jelölés; új kör a beérkezőkből + később.
    void roundSummary_consequence_nextRound()
    {
        TranscriptEditorViewModel vm;
        QVERIFY(openV3(vm));
        ReviewQueueModel* q = vm.reviewQueue();
        q->start();
        // Három hosszabb sor „Jó így" (minta lesz), egy nem azonosítható, egy Később.
        int confirmed = 0;
        for (int r = 0; r < q->count() && confirmed < 3; ++r) {
            const int len = role(q, r, ReviewQueueModel::EndMsRole).toInt() - role(q, r, ReviewQueueModel::StartMsRole).toInt();
            if (stateAt(q, r) != QStringLiteral("pending") || len < 2000) continue;
            QVERIFY(q->jumpTo(r));
            QVERIFY(vm.confirmUtterance(q->currentUtterance()));
            ++confirmed;
        }
        QCOMPARE(confirmed, 3);
        int r = 0;
        while (stateAt(q, r) != QStringLiteral("pending")) ++r;
        QVERIFY(vm.setUtteranceIgnored(role(q, r, ReviewQueueModel::UtteranceIdRole).toString(), true));
        while (stateAt(q, r) != QStringLiteral("pending")) ++r;
        const QString laterId = role(q, r, ReviewQueueModel::UtteranceIdRole).toString();
        QVERIFY(q->jumpTo(r));
        QVERIFY(q->later());

        QVariantMap s = q->roundSummary();
        QCOMPARE(s.value(QStringLiteral("decided")).toInt(), 3);
        QCOMPARE(s.value(QStringLiteral("ignored")).toInt(), 1);
        QCOMPARE(s.value(QStringLiteral("later")).toInt(), 1);
        const QVariantList delta = s.value(QStringLiteral("sampleDelta")).toList();
        QVERIFY2(!delta.isEmpty(), "a megerősítés mintát ad");
        QSet<QString> grown;
        int deltaSum = 0;
        for (const QVariant& d : delta) {
            const int x = d.toMap().value(QStringLiteral("delta")).toInt();
            deltaSum += x;
            if (x > 0) grown.insert(d.toMap().value(QStringLiteral("key")).toString());
        }
        QVERIFY(deltaSum >= 1);
        const QString grownKey = *grown.cbegin();

        // Két új kétes sor: az egyik a mintát kapott beszélőé (következmény), a másik nem.
        QVector<EditorUtterance> utts = vm.utterances();
        QString causedId, otherId;
        for (EditorUtterance& u : utts) {
            if (q->indexOf(u.id) >= 0 || flagged(u) || u.uncertain) continue;
            bool inView = false;
            for (const auto& v : vm.reviewViews()) inView = inView || v.ids.contains(u.id);
            if (inView) continue;
            if (causedId.isEmpty() && u.speakerKey == grownKey) {
                u.uncertain = true;
                u.likelySpeakerKey.clear();
                causedId = u.id;
            } else if (otherId.isEmpty() && !grown.contains(u.speakerKey)) {
                u.uncertain = true;
                u.likelySpeakerKey.clear();
                otherId = u.id;
            }
        }
        QVERIFY(!causedId.isEmpty() && !otherId.isEmpty());
        q->syncFromReview(vm.reviewViews(), utts);
        QCOMPARE(q->incomingCount(), 2);
        s = q->roundSummary();
        QCOMPARE(s.value(QStringLiteral("incoming")).toInt(), 2);
        QCOMPARE(s.value(QStringLiteral("incomingCausedByDecisions")).toInt(), 1);
        QCOMPARE(s.value(QStringLiteral("causedIds")).toStringList(), QStringList{causedId});
        QVERIFY(s.value(QStringLiteral("incomingInReviewed")).toInt() >= 0);

        // Új kör: a nyitott + később + beérkező sorok, időrendben; a következmény jelölve.
        const int expected = countState(q, QStringLiteral("pending")) + 1 + 2;
        q->startNextRound();
        QCOMPARE(q->round(), 2);
        QCOMPARE(q->count(), expected);
        const QStringList ids = idsOf(q);
        for (int i = 1; i < ids.size(); ++i) QVERIFY(vm.utteranceIndexOf(ids[i - 1]) < vm.utteranceIndexOf(ids[i]));
        QVERIFY(role(q, q->indexOf(causedId), ReviewQueueModel::ConsequenceRole).toBool());
        QVERIFY(role(q, q->indexOf(causedId), ReviewQueueModel::AdmittedRole).toBool());
        QVERIFY(!role(q, q->indexOf(otherId), ReviewQueueModel::ConsequenceRole).toBool());
        QCOMPARE(stateAt(q, q->indexOf(laterId)), QStringLiteral("pending"));
        QCOMPARE(q->laterCount(), 0);
        QCOMPARE(q->incomingCount(), 0);
        QCOMPARE(q->currentIndex(), 0);
        // Az új kör alapvonala: még nincs minta-növekmény.
        QVERIFY(q->roundSummary().value(QStringLiteral("sampleDelta")).toList().isEmpty());
    }

    // Mintázat: 4 azonos (ok, jelenlegi, javasolt) áthelyezés az utolsó 6-ból.
    void recentPattern()
    {
        TranscriptEditorViewModel vm;
        QVERIFY(openV3(vm, QString()));
        ReviewQueueModel* q = vm.reviewQueue();
        QVector<EditorUtterance> utts = vm.utterances();
        for (EditorUtterance& u : utts) u.uncertain = false;
        // A legtöbb sorú beszélő 10 sora egy kitalált sáv-ellentmondás csoportban.
        QHash<QString, QStringList> bySpeaker;
        for (const EditorUtterance& u : std::as_const(utts))
            if (!flagged(u)) bySpeaker[u.speakerKey] << u.id;
        QString a;
        for (auto it = bySpeaker.cbegin(); it != bySpeaker.cend(); ++it)
            if (a.isEmpty() || it.value().size() > bySpeaker.value(a).size()) a = it.key();
        const QString b = otherSpeaker(vm, a);
        QVERIFY(bySpeaker.value(a).size() >= 10 && !b.isEmpty());
        TranscriptEditorViewModel::ReviewView v;
        v.id = QStringLiteral("test:side");
        v.kind = QStringLiteral("sideConflict");
        v.ids = bySpeaker.value(a).mid(0, 10);
        v.currentKey = a;
        v.proposedKey = b;
        q->startFrom({v}, utts);
        QCOMPARE(q->count(), 10);
        QVERIFY(q->recentPattern().isEmpty());

        QSignalSpy spy(q, &ReviewQueueModel::patternChanged);
        for (int i = 0; i < 3; ++i) QVERIFY(vm.moveUtteranceToSpeaker(q->currentUtterance(), b));
        QVERIFY(q->recentPattern().isEmpty());
        QVERIFY(spy.count() > 0);
        QVERIFY(vm.moveUtteranceToSpeaker(q->currentUtterance(), b));
        QVariantMap p = q->recentPattern();
        QCOMPARE(p.value(QStringLiteral("hits")).toInt(), 4);
        QCOMPARE(p.value(QStringLiteral("reasonKind")).toString(), QStringLiteral("sideConflict"));
        QCOMPARE(p.value(QStringLiteral("currentKey")).toString(), a);
        QCOMPARE(p.value(QStringLiteral("proposedKey")).toString(), b);
        QCOMPARE(q->currentIndex(), 4);
        QVERIFY(p.value(QStringLiteral("currentMatches")).toBool());
        QCOMPARE(p.value(QStringLiteral("sameAhead")).toStringList(), idsOf(q).mid(5));

        // Két „Jó így" még belefér (4 / 6), a harmadik már nem.
        QVERIFY(vm.confirmUtterance(q->currentUtterance()));
        QVERIFY(vm.confirmUtterance(q->currentUtterance()));
        p = q->recentPattern();
        QCOMPARE(p.value(QStringLiteral("hits")).toInt(), 4);
        QCOMPARE(p.value(QStringLiteral("sameAhead")).toStringList(), idsOf(q).mid(7));
        // „Nem": ez a hármas nem ajánlódik újra.
        q->dismissPattern(p.value(QStringLiteral("key")).toString());
        QVERIFY(q->recentPattern().isEmpty());
    }

    // Nyitó-javaslatok: ≥ 15 sor, erős (hang ≥ 75 % vagy kézi sáv-ellentmondás), javaslattal.
    void openers()
    {
        TranscriptEditorViewModel vm;
        QVERIFY(openV3(vm, QString()));
        ReviewQueueModel* q = vm.reviewQueue();
        QVector<EditorUtterance> utts = vm.utterances();
        for (EditorUtterance& u : utts) u.uncertain = false;
        QStringList free;
        for (const EditorUtterance& u : std::as_const(utts))
            if (!flagged(u)) free << u.id;
        QVERIFY(free.size() >= 40);
        const QString a = utts[vm.utteranceIndexOf(free[0])].speakerKey;
        const QString b = otherSpeaker(vm, a);
        auto view = [&](const QString& id, const QString& kind, const QStringList& ids, const QVariantList& ev) {
            TranscriptEditorViewModel::ReviewView v;
            v.id = id;
            v.kind = kind;
            v.ids = ids;
            v.currentKey = a;
            v.proposedKey = b;
            v.evidence = ev;
            return v;
        };
        const QVariantMap voice82{{QStringLiteral("kind"), QStringLiteral("voice")},
                                  {QStringLiteral("polarity"), QStringLiteral("support")},
                                  {QStringLiteral("value"), 0.82}};
        const QVariantMap voice60{{QStringLiteral("kind"), QStringLiteral("voice")},
                                  {QStringLiteral("polarity"), QStringLiteral("support")},
                                  {QStringLiteral("value"), 0.60}};
        const QVariantMap manualSide{{QStringLiteral("kind"), QStringLiteral("side")},
                                     {QStringLiteral("polarity"), QStringLiteral("contradict")},
                                     {QStringLiteral("manual"), true}};
        QVector<TranscriptEditorViewModel::ReviewView> views{
            view(QStringLiteral("strong"), QStringLiteral("coreMismatch"), free.mid(0, 18), {voice82}),
            view(QStringLiteral("manual"), QStringLiteral("sideConflict"), free.mid(18, 15), {manualSide}),
            view(QStringLiteral("weak"), QStringLiteral("coreMismatch"), free.mid(0, 16), {voice60}),
            view(QStringLiteral("small"), QStringLiteral("coreMismatch"), free.mid(33, 7), {voice82}),
        };
        TranscriptEditorViewModel::ReviewView noProposal = view(QStringLiteral("noProp"), QStringLiteral("coreMismatch"),
                                                                 free.mid(0, 20), {voice82});
        noProposal.proposedKey.clear();
        views << noProposal;
        q->startFrom(views, utts);
        const QVariantList ops = q->openers();
        QCOMPARE(ops.size(), 2);
        const QVariantMap strong = ops[0].toMap();
        QCOMPARE(strong.value(QStringLiteral("groupId")).toString(), QStringLiteral("strong"));
        QCOMPARE(strong.value(QStringLiteral("count")).toInt(), 18);
        QCOMPARE(strong.value(QStringLiteral("pendingCount")).toInt(), 18);
        QCOMPARE(strong.value(QStringLiteral("voicePct")).toInt(), 82);
        QCOMPARE(strong.value(QStringLiteral("proposedKey")).toString(), b);
        QVERIFY(!strong.value(QStringLiteral("proposedName")).toString().isEmpty());
        const QStringList spot = strong.value(QStringLiteral("spotIds")).toStringList();
        QCOMPARE(spot.size(), 3);
        QVERIFY(vm.utteranceIndexOf(spot[0]) < vm.utteranceIndexOf(spot[1]));
        QVERIFY(vm.utteranceIndexOf(spot[1]) < vm.utteranceIndexOf(spot[2]));
        const QVariantMap manual = ops[1].toMap();
        QCOMPARE(manual.value(QStringLiteral("groupId")).toString(), QStringLiteral("manual"));
        QVERIFY(manual.value(QStringLiteral("manualSide")).toBool());
        QCOMPARE(manual.value(QStringLiteral("voicePct")).toInt(), -1);

        // S5 „Egyenként": a csoport sorai a kurzor helyére (itt a lista eleje), időrendben.
        QVERIFY(q->jumpTo(0));
        QCOMPARE(q->bringToFront(manual.value(QStringLiteral("ids")).toStringList()), 15);
        QCOMPARE(idsOf(q).mid(0, 15), free.mid(18, 15));
        QCOMPARE(q->currentIndex(), 0);
        // Egy döntés a csoportból → pendingCount csökken.
        QVERIFY(vm.confirmUtterance(q->currentUtterance()));
        QCOMPARE(q->openers()[1].toMap().value(QStringLiteral("pendingCount")).toInt(), 14);
    }

    // A demó-állapotok (az S4 képernyőképeihez / tesztjeihez).
    void demoStates()
    {
        {
            TranscriptEditorViewModel vm;
            QVERIFY(openV3(vm, QString()));
            vm.applyDemoState(QStringLiteral("reviewQueue"));
            QVERIFY(QTest::qWaitFor([&] { return vm.queueActive(); }, 15000));
            QVERIFY(vm.reviewQueue()->count() > 0);
            QCOMPARE(vm.reviewQueue()->currentIndex(), 0);
        }
        {
            TranscriptEditorViewModel vm;
            QVERIFY(openV3(vm, QString()));
            vm.applyDemoState(QStringLiteral("reviewQueueProgress"));
            QVERIFY(QTest::qWaitFor([&] { return vm.queueActive(); }, 15000));
            ReviewQueueModel* q = vm.reviewQueue();
            QCOMPARE(q->decidedCount() + q->ignoredCount(), 3);
            QCOMPARE(q->ignoredCount(), 1);
            QCOMPARE(q->laterCount(), 1);
            QCOMPARE(q->currentIndex(), 3);
        }
        {
            TranscriptEditorViewModel vm;
            QVERIFY(openV3(vm, QString()));
            vm.applyDemoState(QStringLiteral("reviewQueueSummary"));
            QVERIFY(QTest::qWaitFor([&] { return vm.queueActive(); }, 15000));
            ReviewQueueModel* q = vm.reviewQueue();
            QVERIFY(q->roundComplete());
            QCOMPARE(q->currentIndex(), -1);
            QCOMPARE(q->ignoredCount(), 1);
            QCOMPARE(q->laterCount(), 1);
            QVERIFY(settle(vm));
            const QVariantMap s = q->roundSummary();
            qInfo() << "summary decided" << s.value(QStringLiteral("decided")).toInt() << "incoming"
                    << s.value(QStringLiteral("incoming")).toInt() << "caused"
                    << s.value(QStringLiteral("incomingCausedByDecisions")).toInt() << "inReviewed"
                    << s.value(QStringLiteral("incomingInReviewed")).toInt() << "delta"
                    << s.value(QStringLiteral("sampleDelta")).toList().size();
            QCOMPARE(s.value(QStringLiteral("decided")).toInt() + s.value(QStringLiteral("ignored")).toInt()
                         + s.value(QStringLiteral("later")).toInt() + s.value(QStringLiteral("alreadyDecided")).toInt(),
                     q->count());
        }
    }

    // Új munkamenet → nincs munkalista.
    void stopOnReload()
    {
        TranscriptEditorViewModel vm;
        QVERIFY(openV3(vm));
        vm.reviewQueue()->start();
        QVERIFY(vm.queueActive());
        vm.setDemoVariant(QStringLiteral("many"));
        vm.resolveSession();
        QVERIFY(!vm.queueActive());
        QCOMPARE(vm.reviewQueue()->count(), 0);
        QCOMPARE(vm.reviewQueue()->currentIndex(), -1);
    }
};

QTEST_MAIN(TestReviewQueue)
#include "test_review_queue.moc"
