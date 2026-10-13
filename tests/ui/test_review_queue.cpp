// The review queue's work list (ReviewQueueModel) on the built-in MADE-UP "v3" meeting (real
// SpeakerEditor, fake audio): frozen chronological snapshot, cursor, Later, the arrivals set,
// promotion to the end, alreadyDecided, suggestion refresh, round end + consequence marking,
// pattern counter, opener suggestions, demo states. Runs without a display; it touches no
// user data. The "hand-assembled" group cases (syncFromReview / startFrom with explicit input)
// run WITHOUT spinning the event loop, so the background analysis does not overwrite the input.
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

// A speaker that is not `except`.
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
    // The snapshot is chronological; after a decision the line STAYS IN PLACE, the cursor moves to
    // the next, and neither the order nor the number of lines changes after the background analysis.
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

        // "Good as is": decided in place, cursor to 1.
        QVERIFY(vm.confirmUtterance(order[0]));
        QCOMPARE(stateAt(q, 0), QStringLiteral("decided"));
        QCOMPARE(role(q, 0, ReviewQueueModel::DecidedKindRole).toString(), QStringLiteral("confirmed"));
        QCOMPARE(q->currentIndex(), 1);
        // Move (to the suggestion, if any): decided / corrected, cursor to 2.
        const QString cur1 = role(q, 1, ReviewQueueModel::SpeakerKeyRole).toString();
        QString target = role(q, 1, ReviewQueueModel::LikelyKeyRole).toString();
        if (target.isEmpty() || target == cur1) target = otherSpeaker(vm, cur1);
        QVERIFY(vm.moveUtteranceToSpeaker(order[1], target));
        QCOMPARE(stateAt(q, 1), QStringLiteral("decided"));
        QCOMPARE(role(q, 1, ReviewQueueModel::DecidedKindRole).toString(), QStringLiteral("corrected"));
        QCOMPARE(role(q, 1, ReviewQueueModel::OriginalKeyRole).toString(), cur1);
        QCOMPARE(q->currentIndex(), 2);
        // Cannot be identified: ignored, cursor to 3.
        QVERIFY(vm.setUtteranceIgnored(order[2], true));
        QCOMPARE(stateAt(q, 2), QStringLiteral("ignored"));
        QCOMPARE(q->currentIndex(), 3);
        QCOMPARE(q->decidedCount(), 2);
        QCOMPARE(q->ignoredCount(), 1);

        // The background analysis recomputes: the work list does not move.
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

        // Later: the line goes to the END, `later`; the cursor to the next open line (now at position 0).
        QVERIFY(q->later());
        QCOMPARE(q->count(), n);
        QCOMPARE(idsOf(q).last(), order[0]);
        QCOMPARE(stateAt(q, n - 1), QStringLiteral("later"));
        QCOMPARE(q->laterCount(), 1);
        QCOMPARE(q->currentIndex(), 0);
        QCOMPARE(q->currentUtterance(), order[1]);
        QCOMPARE(role(q, n - 1, ReviewQueueModel::OrderRole).toInt(), n - 1);

        // Decision, then next; back to the decided one, re-deciding there goes to the next open one.
        QVERIFY(vm.confirmUtterance(order[1]));
        QCOMPARE(q->currentUtterance(), order[2]);
        QVERIFY(q->previous());
        QCOMPARE(q->currentUtterance(), order[1]);
        QCOMPARE(stateAt(q, 0), QStringLiteral("decided"));
        const QString cur = role(q, 0, ReviewQueueModel::SpeakerKeyRole).toString();
        QVERIFY(vm.moveUtteranceToSpeaker(order[1], otherSpeaker(vm, cur)));
        QCOMPARE(stateAt(q, 0), QStringLiteral("decided"));
        QCOMPARE(q->currentUtterance(), order[2]);

        // Undo: the latest "Good as is" is undone, the line is pending again, the cursor jumps to it.
        QVERIFY(vm.confirmUtterance(order[2]));
        QCOMPARE(q->currentUtterance(), order[3]);
        vm.undo();
        QCOMPARE(stateAt(q, 1), QStringLiteral("pending"));
        QCOMPARE(q->currentUtterance(), order[2]);
        QCOMPARE(idsOf(q).mid(0, 3), (QStringList{order[1], order[2], order[3]}));

        // A later line can be jumped to and decided there.
        QVERIFY(q->jumpTo(n - 1));
        QVERIFY(vm.confirmUtterance(order[0]));
        QCOMPARE(stateAt(q, n - 1), QStringLiteral("decided"));
        QCOMPARE(q->laterCount(), 0);
    }

    // Arrivals set (+N new waiting), promotion to the END, alreadyDecided, suggestion refresh.
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
        // A new doubtful line (outside the work list).
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
        // Line 3 is no longer doubtful in the background.
        const QString alreadyId = order[3];
        for (auto& v : views) v.ids.removeAll(alreadyId);
        utts[vm.utteranceIndexOf(alreadyId)].uncertain = false;
        // Line 2's suggestion changes.
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

        // Promotion: to the END, the cursor does not move.
        QCOMPARE(q->admitIncoming(), 1);
        QCOMPARE(q->count(), n + 1);
        QCOMPARE(idsOf(q).mid(0, n), order);
        QCOMPARE(idsOf(q).last(), newId);
        QVERIFY(role(q, n, ReviewQueueModel::AdmittedRole).toBool());
        QCOMPARE(stateAt(q, n), QStringLiteral("pending"));
        QCOMPARE(q->currentIndex(), 1);
        QCOMPARE(q->incomingCount(), 0);

        // alreadyDecided: not open, so the cursor skips it; when it lands on it, Later moves on (the
        // line stays in place).
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
        // A decision before the auto-decided line: the cursor skips it.
        QVERIFY(q->jumpTo(2));
        QVERIFY(vm.confirmUtterance(order[2]));
        QCOMPARE(q->currentIndex(), 4);
        // "Enter: keep the name": the confirmation makes it decided.
        QVERIFY(q->jumpTo(3));
        QVERIFY(vm.confirmUtterance(alreadyId));
        QCOMPARE(stateAt(q, 3), QStringLiteral("decided"));
    }

    // Round end: numbers, sample increment, consequence marking; a new round from the arrivals + later.
    void roundSummary_consequence_nextRound()
    {
        TranscriptEditorViewModel vm;
        QVERIFY(openV3(vm));
        ReviewQueueModel* q = vm.reviewQueue();
        q->start();
        // Three longer lines "Good as is" (become samples), one cannot be identified, one Later.
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

        // Two new doubtful lines: one belongs to the speaker that got a sample (consequence), the other does not.
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

        // New round: the open + later + arrival lines, chronologically; the consequence marked.
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
        // The new round's baseline: no sample increment yet.
        QVERIFY(q->roundSummary().value(QStringLiteral("sampleDelta")).toList().isEmpty());
    }

    // Pattern: 4 identical (reason, current, suggested) moves out of the last 6.
    void recentPattern()
    {
        TranscriptEditorViewModel vm;
        QVERIFY(openV3(vm, QString()));
        ReviewQueueModel* q = vm.reviewQueue();
        QVector<EditorUtterance> utts = vm.utterances();
        for (EditorUtterance& u : utts) u.uncertain = false;
        // The 10 lines of the speaker with the most lines, in a made-up track-contradiction group.
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

        // Two more "Good as is" still fit (4 / 6), the third does not.
        QVERIFY(vm.confirmUtterance(q->currentUtterance()));
        QVERIFY(vm.confirmUtterance(q->currentUtterance()));
        p = q->recentPattern();
        QCOMPARE(p.value(QStringLiteral("hits")).toInt(), 4);
        QCOMPARE(p.value(QStringLiteral("sameAhead")).toStringList(), idsOf(q).mid(7));
        // "No": this triple is not offered again.
        q->dismissPattern(p.value(QStringLiteral("key")).toString());
        QVERIFY(q->recentPattern().isEmpty());
    }

    // Opener suggestions: >= 15 lines, strong (voice >= 75% or a manual track contradiction), with a suggestion.
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

        // "One by one": the group's lines go to the cursor's place (here the start of the list), chronologically.
        QVERIFY(q->jumpTo(0));
        QCOMPARE(q->bringToFront(manual.value(QStringLiteral("ids")).toStringList()), 15);
        QCOMPARE(idsOf(q).mid(0, 15), free.mid(18, 15));
        QCOMPARE(q->currentIndex(), 0);
        // A decision from the group lowers pendingCount.
        QVERIFY(vm.confirmUtterance(q->currentUtterance()));
        QCOMPARE(q->openers()[1].toMap().value(QStringLiteral("pendingCount")).toInt(), 14);
    }

    // The demo states (for the focus view's screenshots / tests).
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

    // New session: no work list.
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
