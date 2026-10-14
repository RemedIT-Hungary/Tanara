// The review queue's focus view (ReviewFocusView in TranscriptTab) without a display
// (offscreen), on the built-in MADE-UP meetings, with a fake player: single-key verdicts on
// the cursor's line (Enter / J / 0 / 1-9 / Shift = not a sample) and the cursor's step;
// Later (to the end of the list); the focus card's y does not change (promotion, an item
// placed under the card, a smaller view); clicking "+K new waiting" moves the lines to the end
// of the list; the round-end buttons; Esc goes to Edit on the same line; undo puts the focus
// on the undone line; auto-play (line + lead-out) and its switch; playing the context line
// (`,`). It touches no user data.
#include "AppContext.h"
#include "tanara/app/AppSettings.h"
#include "QmlApp.h"
#include "ReviewQueueModel.h"
#include "TranscriptEditorViewModel.h"
#include "TranscriptListModel.h"
#include "tanara/library/store/JsonSerialization.h"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

#include <memory>

using namespace tanara_qml;

namespace {

const char* kHost = R"(
import QtQuick
import QtQuick.Controls
import Tanara
ApplicationWindow {
    width: 1100; height: 760
    color: Theme.bg
    property string variant: ""
    property string state_: ""
    QtObject {
        id: fakePlayer
        objectName: "player"
        property bool available: true
        property bool playing: false
        property int positionMs: 0
        property int durationMs: 0
        property string previewPath: ""
        property string log: ""
        function seek(ms) { log += "seek:" + ms + ";"; positionMs = ms }
        function play() { log += "play;"; playing = true }
        function pause() { log += "pause;"; playing = false }
        function toggle() { log += "toggle;"; playing = !playing }
        function playRange(a, b, lo, hi) { log += "range:" + a + "-" + b + ";" }
    }
    QtObject {
        id: fakeShell
        objectName: "shell"
        signal transcriptPositionRequested(int ms)
        property string toasts: ""
        function toast(text) { toasts += text + ";" }
    }
    TranscriptTab {
        id: tab
        objectName: "tab"
        anchors.fill: parent
        player: fakePlayer
        shell: fakeShell
        demoVariant: parent ? variant : ""
        demoState: parent ? state_ : ""
    }
}
)";

} // namespace

class TestReviewFocus : public QObject {
    Q_OBJECT

    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QStringList m_warnings;
    QQuickItem* m_tab = nullptr;
    QObject* m_player = nullptr;
    TranscriptEditorViewModel* m_vm = nullptr;
    ReviewQueueModel* m_queue = nullptr;

    void pump(int ms = 30) { QTest::qWait(ms); }
    static void collect(QQuickItem* root, const QString& name, QList<QQuickItem*>& out)
    {
        if (!root || !root->isVisible()) return;
        if (root->objectName() == name) out << root;
        for (QQuickItem* child : root->childItems()) collect(child, name, out);
    }
    QList<QQuickItem*> visuals(const char* name) const
    {
        QList<QQuickItem*> out;
        collect(m_window->contentItem(), QLatin1String(name), out);
        return out;
    }
    QQuickItem* visual(const char* name) const { return visuals(name).value(0); }
    QQuickItem* view() const { return m_window->findChild<QQuickItem*>(QStringLiteral("reviewView")); }
    QQuickItem* card() const { return m_window->findChild<QQuickItem*>(QStringLiteral("reviewFocusCard")); }
    QPoint center(QQuickItem* item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); }
    void click(QQuickItem* item, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        QVERIFY(item);
        QTest::mouseClick(m_window.get(), Qt::LeftButton, mods, center(item));
        pump();
    }
    void key(int k, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        QTest::keyClick(m_window.get(), Qt::Key(k), mods);
        pump();
    }
    qreal cardY() const { return card() ? card()->mapToScene(QPointF(0, 0)).y() : -1; }
    QString current() const { return m_queue->currentUtterance(); }
    QVariantMap cur() const { return m_queue->current(); }
    QString stateOf(const QString& id) const { return m_queue->get(m_queue->indexOf(id)).value(QStringLiteral("state")).toString(); }
    tanara::editing::EditorUtterance utt(const QString& id) const { return m_vm->utterances().value(m_vm->utteranceIndexOf(id)); }
    QString viewMode() const { return m_tab->property("viewMode").toString(); }
    QString playerLog() const { return m_player->property("log").toString(); }
    void clearLog() { m_player->setProperty("log", QString()); }

    void load(const QString& variant = QString(), const QString& state = QString())
    {
        m_window.reset();
        m_warnings.clear();
        QQmlComponent comp(m_engine.get());
        comp.setData(kHost, QUrl(QStringLiteral("qrc:/qt/qml/Tanara/ReviewFocusProbe.qml")));
        QObject* obj = comp.createWithInitialProperties({{QStringLiteral("variant"), variant},
                                                         {QStringLiteral("state_"), state}});
        QVERIFY2(obj, qPrintable(comp.errorString()));
        m_window.reset(qobject_cast<QQuickWindow*>(obj));
        QVERIFY(m_window);
        m_window->show();
        m_window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
        m_tab = m_window->findChild<QQuickItem*>(QStringLiteral("tab"));
        m_player = m_window->findChild<QObject*>(QStringLiteral("player"));
        m_vm = m_window->findChild<TranscriptEditorViewModel*>();
        QVERIFY(m_tab && m_player && m_vm);
        m_queue = m_vm->reviewQueue();
        QVERIFY(m_queue);
        if (state.isEmpty() && m_vm->hasTranscript() && m_vm->voiceAvailable())
            QVERIFY(QTest::qWaitFor([this] { return !m_vm->embeddingRunning() && m_vm->uncertainCount() > 0; }, 10000));
        pump(60);
    }
    // Switching on To review, waiting for the work list (it starts at the end of the background analysis).
    QString phase() const { return view() ? view()->property("phase").toString() : QString(); }
    // skipOpenerPhase: the round's opening suggestions go to Later; the focus card comes.
    void openReview(bool skipOpenerPhase = true)
    {
        QVERIFY(QTest::qWaitFor([this] { return !m_vm->reviewRunning() && m_vm->reviewCount() > 0; }, 10000));
        QMetaObject::invokeMethod(m_tab, "setViewMode", Q_ARG(QVariant, QStringLiteral("review")));
        QTRY_VERIFY_WITH_TIMEOUT(m_vm->queueActive() && !current().isEmpty(), 10000);
        if (skipOpenerPhase) {
            pump(60);
            m_tab->forceActiveFocus();
            for (int g = 0; g < 20 && phase() == QLatin1String("opener"); ++g) key(Qt::Key_Right);
        }
        QTRY_VERIFY(card() && card()->isVisible());
        m_tab->forceActiveFocus();
        pump(60);
    }
    // Waiting for the demo state (the work list is made in the background analysis's quiet phase).
    void loadDemo(const char* state)
    {
        load(QString(), QString::fromLatin1(state));
        QTRY_VERIFY_WITH_TIMEOUT(m_vm->queueActive() && !m_vm->demoStatePending() && !m_vm->reviewRunning(), 15000);
        pump(1500);         // the demo runs after the silence (700 ms), and one more analysis round may follow
        QTRY_VERIFY_WITH_TIMEOUT(!m_vm->reviewRunning(), 10000);
        pump(100);
        m_tab->forceActiveFocus();
    }
    // The cursor on an open line that has a suggestion (the suggested speaker differs from the current one).
    bool jumpToSuggestion()
    {
        for (int i = 0; i < m_queue->count(); ++i) {
            const QVariantMap m = m_queue->get(i);
            if (m.value(QStringLiteral("state")).toString() == QLatin1String("pending")
                && !m.value(QStringLiteral("likelyKey")).toString().isEmpty()
                && m.value(QStringLiteral("likelyKey")) != m.value(QStringLiteral("speakerKey"))) {
                m_queue->jumpTo(i);
                pump();
                return true;
            }
        }
        return false;
    }
    int laneOf(const QString& key) const
    {
        const QVariantList lanes = m_vm->lanes();
        for (int i = 0; i < lanes.size(); ++i)
            if (lanes[i].toMap().value(QStringLiteral("key")).toString() == key) return i;
        return -1;
    }
    // The column of a speaker other than the cursor line's (for the 1-9 keys).
    int otherLane() const
    {
        const QString key = cur().value(QStringLiteral("speakerKey")).toString();
        const int n = std::min<int>(9, int(m_vm->lanes().size()));
        for (int i = 0; i < n; ++i)
            if (m_vm->lanes()[i].toMap().value(QStringLiteral("key")).toString() != key) return i;
        return -1;
    }

private slots:
    void initTestCase()
    {
        AppContext::instance()->setDemo(true);
        AppContext::instance()->setThemeMode(QStringLiteral("light"));
        m_engine = std::make_unique<QQmlEngine>();
        setupEngine(*m_engine);
        connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError>& list) {
            for (const QQmlError& e : list) m_warnings << e.toString();
        });
    }
    void cleanupTestCase()
    {
        m_window.reset();
        m_engine.reset();
    }
    void cleanup()
    {
        QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QLatin1Char('\n'))));
    }

    // Enter / J / Shift+J / 0 / 1-9 / Shift+1-9: the verdict goes on the cursor's line, the
    // cursor moves on, the line stays in place (the list does not reorder).
    void verdicts_keysDecideCursorLine()
    {
        load(QStringLiteral("v3"));
        openReview();
        const int count = m_queue->count();

        // Enter: the suggestion (the suggested NAME on the button).
        QVERIFY(jumpToSuggestion());
        QString id = current();
        const int at = m_queue->indexOf(id);
        const QString likely = cur().value(QStringLiteral("likelyKey")).toString();
        QCOMPARE(visual("reviewFocusEnter")->property("text").toString(), cur().value(QStringLiteral("likelyName")).toString());
        key(Qt::Key_Return);
        QCOMPARE(utt(id).speakerKey, likely);
        QVERIFY(utt(id).manuallyCorrected);
        QVERIFY(!utt(id).noisy);
        QCOMPARE(stateOf(id), QStringLiteral("decided"));
        QCOMPARE(m_queue->indexOf(id), at);                  // stays in place
        QVERIFY(current() != id);
        QCOMPARE(m_queue->count(), count);

        // J: Good as is (sample); Shift+J: Good as is, not a sample.
        id = current();
        key(Qt::Key_J);
        QVERIFY(utt(id).confirmed);
        QVERIFY(!utt(id).noisy);
        QVERIFY(current() != id);
        id = current();
        key(Qt::Key_J, Qt::ShiftModifier);
        QVERIFY(utt(id).confirmed);
        QVERIFY(utt(id).noisy);
        QVERIFY(current() != id);

        // 0: cannot be identified.
        id = current();
        key(Qt::Key_0);
        QVERIFY(utt(id).ignored);
        QCOMPARE(stateOf(id), QStringLiteral("ignored"));
        QVERIFY(current() != id);

        // 1-9: the rail's n-th speaker; Shift: not a sample.
        id = current();
        int lane = otherLane();
        QVERIFY(lane >= 0);
        QString target = m_vm->lanes()[lane].toMap().value(QStringLiteral("key")).toString();
        key(Qt::Key_1 + lane);
        QCOMPARE(utt(id).speakerKey, target);
        QVERIFY(!utt(id).noisy);
        QVERIFY(current() != id);
        id = current();
        lane = otherLane();
        target = m_vm->lanes()[lane].toMap().value(QStringLiteral("key")).toString();
        key(Qt::Key_1 + lane, Qt::ShiftModifier);
        QCOMPARE(utt(id).speakerKey, target);
        QVERIFY(utt(id).noisy);
        QCOMPARE(m_queue->decidedCount() + m_queue->ignoredCount(), 6);
        QCOMPARE(visual("reviewFocusProgress")->property("text").toString(),
                 QStringLiteral("6 / %1").arg(m_queue->count()));

        // Left: back to the previous (decided) line; it can be decided again there.
        key(Qt::Key_Left);
        QCOMPARE(current(), id);
        QCOMPARE(stateOf(id), QStringLiteral("decided"));
        QVERIFY(visual("reviewFocusDecided"));
    }

    // The card's buttons and the speaker number row decide the same way; with Shift held, "- not a sample".
    void verdicts_buttonsAndNumberRow()
    {
        load();
        openReview();
        QQuickItem* confirm = visual("reviewFocusConfirm");
        QVERIFY(confirm);
        QVERIFY(!confirm->property("label").toString().endsWith(QStringLiteral(" · nem minta")));
        m_tab->setProperty("shiftHeld", true);
        QVERIFY(confirm->property("nonSample").toBool());
        QString id = current();
        click(confirm);
        QVERIFY(utt(id).confirmed);
        QVERIFY(utt(id).noisy);
        m_tab->setProperty("shiftHeld", false);

        // Number row: clicking another speaker = moving there (sample).
        id = current();
        const int lane = otherLane();
        QList<QQuickItem*> chips = visuals("reviewFocusLane");
        QVERIFY(chips.size() > lane);
        const QString target = m_vm->lanes()[lane].toMap().value(QStringLiteral("key")).toString();
        click(chips[lane]);
        QCOMPARE(utt(id).speakerKey, target);
        QVERIFY(!utt(id).noisy);

        // "Cannot be identified" button.
        id = current();
        click(visual("reviewFocusIgnore"));
        QVERIFY(utt(id).ignored);

        // "Other..." (and the M key): the line's panel for this line.
        id = current();
        key(Qt::Key_M);
        QObject* popover = m_window->findChild<QObject*>(QStringLiteral("linePopover"));
        QTRY_VERIFY(popover->property("opened").toBool());
        QCOMPARE(popover->property("utteranceId").toString(), id);
        key(Qt::Key_Escape);
        QTRY_VERIFY(!popover->property("visible").toBool());
    }

    // Later: the line goes to the end of the list, the cursor to the next open line; B = next open one.
    void later_movesToEnd()
    {
        load();
        openReview();
        const QString id = current();
        const int count = m_queue->count();
        key(Qt::Key_Right);
        QCOMPARE(stateOf(id), QStringLiteral("later"));
        QCOMPARE(m_queue->indexOf(id), count - 1);
        QCOMPARE(m_queue->laterCount(), 1);
        QVERIFY(current() != id);
        const QString next = current();
        key(Qt::Key_B);
        QVERIFY(current() != next);
        QCOMPARE(viewMode(), QStringLiteral("review"));
    }

    // The focus card's y does not change: on "+K new waiting" promotion (the lines go to the END
    // of the list), with an item placed under the card (the openers' offer place), and when the
    // view becomes lower (e.g. under the "Just now" line's warning).
    void cardY_fixed_andIncomingToEnd()
    {
        loadDemo("reviewFocusIncoming");
        QTRY_VERIFY(card() && card()->isVisible());
        QVERIFY(m_queue->incomingCount() > 0);
        const qreal y = cardY();
        QVERIFY(y > 0);
        const QStringList incoming = m_queue->incomingIds();
        const int before = m_queue->count();
        const QString id = current();
        QCOMPARE(visual("reviewFocusIncomingText")->property("text").toString(),
                 QStringLiteral("+%1 új vár").arg(incoming.size()));
        click(visual("reviewFocusIncoming"));
        QCOMPARE(m_queue->incomingCount(), 0);
        QCOMPARE(m_queue->count(), before + int(incoming.size()));
        for (const QString& in : incoming) QVERIFY(m_queue->indexOf(in) >= before);
        QCOMPARE(current(), id);                            // the cursor did not move
        QCOMPARE(cardY(), y);

        // An item UNDER the card (the offer place of the openers): the card stays, the lower context lines go.
        QQuickItem* v = view();
        QQmlComponent tall(m_engine.get());
        tall.setData("import QtQuick\nRectangle { height: 220; color: \"red\" }", QUrl());
        v->setProperty("offerComponent", QVariant::fromValue(&tall));
        pump(60);
        QTRY_VERIFY(visual("reviewFocusOfferSlot"));
        QCOMPARE(cardY(), y);
        int lower = 0;
        for (QQuickItem* row : visuals("reviewFocusContext"))
            if (row->mapToScene(QPointF(0, 0)).y() > y) ++lower;
        QCOMPARE(lower, 0);
        v->setProperty("offerComponent", QVariant::fromValue<QObject*>(nullptr));
        pump(60);

        // A lower view (the warning / mini log extends under the card): the card stays.
        m_window->resize(1100, 600);
        pump(80);
        QCOMPARE(cardY(), y);
        // After a decision the next card is in the same place.
        key(Qt::Key_J);
        QCOMPARE(cardY(), y);
    }

    // The card's y is the same in every focus state (the header, the "Just now" line and the items
    // under the card do not shift it either).
    void cardY_sameInEveryFocusState()
    {
        qreal first = -1;
        for (const char* state : {"reviewFocus", "reviewFocusDecided", "reviewFocusIncoming", "reviewFocusAlreadyDecided",
                                  "reviewFocusSkipped"}) {
            loadDemo(state);
            QTRY_VERIFY(card() && card()->isVisible());
            const qreal y = cardY();
            qInfo("%s: kártya y = %.0f px (ablak 1100×760), magasság %.0f px", state, y, card()->height());
            if (first < 0) first = y;
            QCOMPARE(y, first);
        }
    }

    // A line that decided itself, when the cursor steps back onto it: the card shows it normally
    // (check mark), Enter writes the current name ("keep"), J is dimmed; Enter = confirmation, the
    // cursor goes to the next OPEN one.
    void alreadyDecided_enterKeeps()
    {
        loadDemo("reviewFocusAlreadyDecided");
        QTRY_COMPARE(cur().value(QStringLiteral("state")).toString(), QStringLiteral("alreadyDecided"));
        QVERIFY(visual("reviewFocusSettled"));
        QCOMPARE(visual("reviewFocusEnter")->property("text").toString(), cur().value(QStringLiteral("speakerName")).toString());
        QCOMPARE(visual("reviewFocusEnter")->property("iconName").toString(), QStringLiteral("check"));
        QVERIFY(visual("reviewFocusConfirm")->property("dimmed").toBool());
        const QString id = current();
        key(Qt::Key_Return);
        QVERIFY(utt(id).confirmed);
        QVERIFY(current() != id);
        QCOMPARE(cur().value(QStringLiteral("state")).toString(), QStringLiteral("pending"));
    }

    // Auto-decided lines are skipped by the cursor; Up steps back onto them; "Confirm all"
    // (Ctrl+Enter / the header button) confirms those >= 1.5 s long in ONE undo step.
    void settled_skippedAndConfirmAll_oneUndoStep()
    {
        loadDemo("reviewFocusSkipped");
        QTRY_VERIFY(card() && card()->isVisible());
        QStringList settled;
        int firstSettled = -1, lastSettled = -1;
        for (int i = 0; i < m_queue->count(); ++i) {
            if (m_queue->get(i).value(QStringLiteral("state")).toString() != QLatin1String("alreadyDecided")) continue;
            settled << m_queue->get(i).value(QStringLiteral("utteranceId")).toString();
            if (firstSettled < 0) firstSettled = i;
            lastSettled = i;
        }
        QVERIFY(settled.size() >= 3);
        QCOMPARE(m_queue->alreadyDecidedCount(), int(settled.size()));
        QVERIFY(m_queue->openCount() < m_queue->count() - int(settled.size()) + 1);
        QVERIFY(visual("reviewFocusSettledBar"));
        QVERIFY(visual("reviewFocusSettledCount")->property("text").toString().startsWith(QString::number(settled.size())));

        // (The demo sets the background analysis's result; after a decision the real analysis would
        // reset it, so stepping comes first and the decision last. The skipping after a decision is
        // measured by test_review_queue.)
        // B steps to the next open one (across the auto-decided ones).
        QVERIFY(firstSettled > 0);
        m_queue->jumpTo(firstSettled - 1);
        pump();
        QCOMPARE(cur().value(QStringLiteral("state")).toString(), QStringLiteral("pending"));
        key(Qt::Key_B);
        QVERIFY(m_queue->currentIndex() > lastSettled || m_queue->currentIndex() < firstSettled);
        QCOMPARE(cur().value(QStringLiteral("state")).toString(), QStringLiteral("pending"));
        // Shift+B goes back to the previous open one (skipping them again).
        key(Qt::Key_B, Qt::ShiftModifier);
        QCOMPARE(m_queue->currentIndex(), firstSettled - 1);
        // Up / Down: the strip's items, in any state (the auto-decided too).
        m_queue->jumpTo(firstSettled - 1);
        pump();
        key(Qt::Key_Down);
        QCOMPARE(m_queue->currentIndex(), firstSettled);
        QCOMPARE(cur().value(QStringLiteral("state")).toString(), QStringLiteral("alreadyDecided"));
        key(Qt::Key_Up);
        QCOMPARE(m_queue->currentIndex(), firstSettled - 1);
        key(Qt::Key_Down);
        QCOMPARE(m_queue->currentIndex(), firstSettled);

        // Confirm all (Ctrl+Enter): the long ones confirmed, the short ones not; one step.
        QStringList longOnes, shortOnes;
        for (const QString& id : std::as_const(settled))
            (utt(id).endMs - utt(id).startMs >= 1500 ? longOnes : shortOnes) << id;
        QVERIFY(!longOnes.isEmpty());
        QCOMPARE(m_queue->confirmableCount(), int(longOnes.size()));
        key(Qt::Key_Return, Qt::ControlModifier);
        for (const QString& id : std::as_const(longOnes)) {
            QVERIFY(utt(id).confirmed);
            QVERIFY(!utt(id).noisy);
            QCOMPARE(stateOf(id), QStringLiteral("decided"));
        }
        for (const QString& id : std::as_const(shortOnes)) QVERIFY(!utt(id).confirmed);
        QCOMPARE(m_vm->lastDecision().value(QStringLiteral("text")).toString(),
                 QStringLiteral("%1 sor megerősítve").arg(longOnes.size()));
        QCOMPARE(m_vm->lastDecision().value(QStringLiteral("verb")).toString(), QStringLiteral("keep"));
        // (After the confirmation the background analysis may auto-decide more lines; the current ones are not among them.)
        for (const QString& id : std::as_const(longOnes)) QVERIFY(!m_queue->confirmableIds().contains(id));

        // One Ctrl+Z undoes them all.
        key(Qt::Key_Z, Qt::ControlModifier);
        for (const QString& id : std::as_const(longOnes)) {
            QVERIFY(!utt(id).confirmed);
            QVERIFY(stateOf(id) != QLatin1String("decided"));
        }
        QVERIFY(!m_vm->canUndo() || m_vm->lastDecision().value(QStringLiteral("undone")).toBool());

        // The header button does the same.
        loadDemo("reviewFocusSkipped");
        QTRY_VERIFY(visual("reviewFocusConfirmSettled"));
        const QStringList ids = m_queue->confirmableIds();
        QVERIFY(!ids.isEmpty());
        click(visual("reviewFocusConfirmSettled"));
        for (const QString& id : ids) QVERIFY(utt(id).confirmed);
    }

    // The button labels in the three states (suggestion / no suggestion / new speaker): names on
    // the buttons, the action's icon; J is dimmed when there is no suggestion; the buttons' places are fixed.
    void buttons_namesInThreeStates()
    {
        loadDemo("reviewFocus");
        QTRY_VERIFY(card() && card()->isVisible());
        QQuickItem* enter = visual("reviewFocusEnter");
        QQuickItem* keep = visual("reviewFocusConfirm");
        QVERIFY(enter && keep);
        auto geometry = [&] {
            QList<QRectF> out;
            for (const char* n : {"reviewFocusEnter", "reviewFocusConfirm", "reviewFocusIgnore", "reviewFocusOther", "reviewFocusLater"}) {
                QQuickItem* b = visual(n);
                out << QRectF(b->mapToScene(QPointF(0, 0)), QSizeF(b->width(), b->height()));
            }
            return out;
        };
        // A) there is a suggestion (the demo puts the cursor on a line with a suggestion).
        QVERIFY(card()->property("hasSuggestion").toBool());
        QCOMPARE(enter->property("text").toString(), cur().value(QStringLiteral("likelyName")).toString());
        QCOMPARE(enter->property("iconName").toString(), QStringLiteral("arrow-right"));
        QVERIFY(enter->property("subText").toString().startsWith(QStringLiteral("javaslat")));
        QCOMPARE(keep->property("text").toString(), cur().value(QStringLiteral("speakerName")).toString());
        QCOMPARE(keep->property("subText").toString(), QStringLiteral("marad"));
        QCOMPARE(keep->property("iconName").toString(), QStringLiteral("check"));
        QVERIFY(!keep->property("dimmed").toBool());
        QCOMPARE(visual("reviewFocusIgnore")->property("text").toString(), QStringLiteral("Nem azonosítható"));
        QCOMPARE(visual("reviewFocusOther")->property("text").toString(), QStringLiteral("Más…"));
        QCOMPARE(visual("reviewFocusLater")->property("text").toString(), QStringLiteral("Később"));
        const QList<QRectF> geo = geometry();

        // B) no suggestion: Enter writes the current name ("keep"), J is dimmed.
        QVariantMap item = cur();
        item.insert(QStringLiteral("likelyKey"), QString());
        item.insert(QStringLiteral("likelyName"), QString());
        item.insert(QStringLiteral("likelyNewSide"), QString());
        card()->setProperty("item", item);
        pump();
        QCOMPARE(enter->property("text").toString(), item.value(QStringLiteral("speakerName")).toString());
        QCOMPARE(enter->property("iconName").toString(), QStringLiteral("check"));
        QVERIFY(enter->property("subText").toString().startsWith(QStringLiteral("marad")));
        QVERIFY(keep->property("dimmed").toBool());
        QCOMPARE(geometry(), geo);

        // C) the suggestion is a new speaker: "New speaker" + the user-plus icon; the "+" chip is suggested.
        item.insert(QStringLiteral("likelyNewSide"), QStringLiteral("remote"));
        card()->setProperty("item", item);
        pump();
        QCOMPARE(enter->property("text").toString(), QStringLiteral("Új beszélő"));
        QCOMPARE(enter->property("iconName").toString(), QStringLiteral("user-plus"));
        QVERIFY(!keep->property("dimmed").toBool());
        QVERIFY(visual("reviewFocusLaneNew")->property("suggested").toBool());
        // A long name: elided, the buttons do not move.
        item.insert(QStringLiteral("likelyNewSide"), QString());
        item.insert(QStringLiteral("likelyName"), QStringLiteral("Nagyon Hosszú Nevű Személy Akinek A Neve Nem Fér Ki"));
        item.insert(QStringLiteral("likelyKey"), QStringLiteral("x-external"));
        card()->setProperty("item", item);
        pump();
        QCOMPARE(geometry(), geo);
    }

    // The number row with full names; the "current" and "suggested" marks; the chips' places do
    // not change per line (the line does not reorder).
    void numberRow_nowAndSuggested()
    {
        loadDemo("reviewFocus");
        QTRY_VERIFY(card() && card()->isVisible());
        QVERIFY(card()->property("hasSuggestion").toBool());
        const QList<QQuickItem*> chips = visuals("reviewFocusLane");
        QVERIFY(chips.size() >= 2);
        const QString nowKey = cur().value(QStringLiteral("speakerKey")).toString();
        const QString sugKey = cur().value(QStringLiteral("likelyKey")).toString();
        const QVariantList lanes = m_vm->lanes();
        auto positions = [&] {
            QList<QPointF> out;
            for (QQuickItem* c : visuals("reviewFocusLane")) out << c->mapToScene(QPointF(0, 0));
            return out;
        };
        const QList<QPointF> pos = positions();
        int nows = 0, sugs = 0;
        const QList<QQuickItem*> marks = visuals("reviewFocusLaneMark");
        const QList<QQuickItem*> names = visuals("reviewFocusLaneName");
        QCOMPARE(marks.size(), chips.size());
        for (int i = 0; i < chips.size(); ++i) {
            const QString key = lanes[i].toMap().value(QStringLiteral("key")).toString();
            QCOMPARE(names[i]->property("text").toString(), lanes[i].toMap().value(QStringLiteral("name")).toString());
            const QString mark = marks[i]->property("text").toString();
            if (key == nowKey) { QCOMPARE(mark, QStringLiteral("most")); ++nows; }
            else if (key == sugKey) { QCOMPARE(mark, QStringLiteral("javasolt")); ++sugs; }
            else QVERIFY(mark.isEmpty());
            QVERIFY(chips[i]->property("current").toBool() == (key == nowKey));
            QVERIFY(chips[i]->property("suggested").toBool() == (key == sugKey));
        }
        QCOMPARE(nows, 1);
        QCOMPARE(sugs, laneOf(sugKey) >= 0 && laneOf(sugKey) < 9 ? 1 : 0);
        // Stepping to the next line, the chips' places are the same.
        key(Qt::Key_Down);
        QCOMPARE(positions(), pos);
        key(Qt::Key_Down);
        QCOMPARE(positions(), pos);
    }

    // 3 + 3 context lines (the transcript's neighbors), without a label, per line with the time jump.
    void context_threeAndThreeWithGaps()
    {
        loadDemo("reviewFocus");
        QTRY_VERIFY(card() && card()->isVisible());
        const qreal y = cardY();
        const QVariantList ctx = m_vm->contextLines(current(), 3, 3);
        int above = 0, below = 0, gaps = 0;
        for (QQuickItem* row : visuals("reviewFocusContext")) {
            const qreal ry = row->mapToScene(QPointF(0, 0)).y();
            (ry < y ? above : below)++;
            if (!row->property("valid").toBool()) continue;
            const QString gap = row->property("gapText").toString();
            QVERIFY2(gap.startsWith(QLatin1Char('+')) && gap.endsWith(QStringLiteral(" s")), qPrintable(gap));
            ++gaps;
        }
        QCOMPARE(above, 3);
        QCOMPARE(below, 3);
        QCOMPARE(gaps, int(ctx.size()));
        // The previous line's time jump = the silence up to the card's line.
        for (const QVariant& v : ctx) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("offset")).toInt() != -1) continue;
            const int expect = std::max(0, cur().value(QStringLiteral("startMs")).toInt() - m.value(QStringLiteral("endMs")).toInt());
            QVariant gap;
            QVERIFY(QMetaObject::invokeMethod(view(), "gapFor", Q_RETURN_ARG(QVariant, gap), Q_ARG(QVariant, -1)));
            QCOMPARE(gap.toInt(), expect);
        }
        // No "Before / After" label.
        QVERIFY(visuals("reviewFocusContextLabel").isEmpty());
    }

    // Above the strip: the cursor's position and the time of the next doubtful line.
    void worklist_positionCaption()
    {
        loadDemo("reviewFocus");
        QTRY_VERIFY(card() && card()->isVisible());
        const QString text = visual("reviewFocusPosition")->property("text").toString();
        QVERIFY2(text.startsWith(QStringLiteral("%1 / %2").arg(m_queue->currentIndex() + 1).arg(m_queue->count())), qPrintable(text));
        const int next = m_queue->nextOpenIndex();
        if (next >= 0)
            QVERIFY2(text.endsWith(QStringLiteral("következő kétes %1").arg(m_queue->get(next).value(QStringLiteral("timeLabel")).toString())),
                     qPrintable(text));
    }

    // Round end: summary; Next round (arrivals + later) / Left for later / Done (Edit).
    void roundClose_buttons()
    {
        loadDemo("reviewFocusSummary");
        QTRY_VERIFY(visual("reviewFocusSummary"));
        QVERIFY(m_queue->roundComplete());
        QVERIFY(!card()->isVisible());
        const int incoming = m_queue->incomingCount();
        const int later = m_queue->laterCount();
        QVERIFY(later > 0);
        QVERIFY(visual("reviewSummaryCounts")->property("text").toString().contains(QStringLiteral("eldöntve")));
        click(visual("reviewNextRound"));
        QCOMPARE(m_queue->round(), 2);
        QVERIFY(m_queue->count() >= incoming + later);
        QTRY_VERIFY(card()->isVisible());

        // Only those left for later (Right key).
        loadDemo("reviewFocusSummary");
        QTRY_VERIFY(visual("reviewFocusSummary"));
        const int later2 = m_queue->laterCount();
        key(Qt::Key_Right);
        QCOMPARE(m_queue->round(), 2);
        QCOMPARE(m_queue->count(), later2);

        // Done: back to the Edit view.
        loadDemo("reviewFocusSummary");
        QTRY_VERIFY(visual("reviewFocusSummary"));
        click(visual("reviewDone"));
        QTRY_COMPARE(viewMode(), QStringLiteral("fix"));
    }

    // Esc: the Edit view, the same line selected.
    void escape_opensFixOnSameLine()
    {
        load();
        openReview();
        key(Qt::Key_Down);
        const QString id = current();
        key(Qt::Key_Escape);
        QTRY_COMPARE(viewMode(), QStringLiteral("fix"));
        QTRY_COMPARE(m_vm->rowUtteranceId(m_vm->currentRow()), id);
        // Back: the work list stayed (no new snapshot), the cursor in the same place.
        QMetaObject::invokeMethod(m_tab, "setViewMode", Q_ARG(QVariant, QStringLiteral("review")));
        pump(60);
        QCOMPARE(current(), id);
    }

    // Ctrl+Z: the decision is undone, the focus jumps to the undone line.
    void undo_focusReturnsToLine()
    {
        load();
        openReview();
        const QString id = current();
        key(Qt::Key_J);
        QVERIFY(utt(id).confirmed);
        key(Qt::Key_Down);
        QVERIFY(current() != id);
        key(Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(!utt(id).confirmed);
        QTRY_COMPARE(current(), id);
        QCOMPARE(stateOf(id), QStringLiteral("pending"));
    }

    // Auto-play: the line that steps onto the card plays (line + lead-out); the switch turns it
    // off; R replays; `,` the previous context line.
    void autoPlay_andContextKeys()
    {
        load();
        clearLog();
        openReview();
        QVERIFY(m_vm->reviewAutoPlay());
        const QVariantMap c = cur();
        const QString expected = QStringLiteral("range:%1-%2;").arg(c.value(QStringLiteral("startMs")).toInt())
                                     .arg(c.value(QStringLiteral("endMs")).toInt() + 200);
        QTRY_VERIFY2(playerLog().contains(expected), qPrintable(playerLog()));

        // Switch off: the next card does not play by itself.
        click(visual("reviewAutoPlay"));
        QVERIFY(!m_vm->reviewAutoPlay());
        clearLog();
        key(Qt::Key_J);
        pump(60);
        QVERIFY2(!playerLog().contains(QStringLiteral("range:")), qPrintable(playerLog()));
        // R: the card's line again.
        key(Qt::Key_R);
        QVERIFY(playerLog().startsWith(QStringLiteral("range:%1-").arg(cur().value(QStringLiteral("startMs")).toInt())));

        // `,`: the previous context line (the transcript's previous line, even if not doubtful).
        const QVariantList ctx = m_vm->contextLines(current(), 2, 2);
        int prevStart = -1;
        for (const QVariant& v : ctx)
            if (v.toMap().value(QStringLiteral("offset")).toInt() == -1) prevStart = v.toMap().value(QStringLiteral("startMs")).toInt();
        if (prevStart >= 0) {
            clearLog();
            key(Qt::Key_Comma);
            QVERIFY2(playerLog().startsWith(QStringLiteral("range:%1-").arg(prevStart)), qPrintable(playerLog()));
        }
        // Space: pause / start.
        m_player->setProperty("playing", true);
        clearLog();
        key(Qt::Key_Space);
        QCOMPARE(playerLog(), QStringLiteral("pause;"));
    }

    // The work-list strip: clicking a cell = jump to that line.
    void worklist_cellJumps()
    {
        load();
        openReview();
        QList<QQuickItem*> cells = visuals("reviewFocusCell");
        QCOMPARE(cells.size(), m_queue->count());
        QVERIFY(cells.size() > 3);
        click(cells[3]);
        QCOMPARE(m_queue->currentIndex(), 3);
    }

    // The settings key (settings.json "reviewAutoPlay"): on by default, persists both ways.
    void settings_reviewAutoPlayRoundTrip()
    {
        tanara::app::AppSettings s;
        QVERIFY(s.ui.reviewAutoPlay);
        s.ui.reviewAutoPlay = false;
        QVERIFY(!tanara::app::AppSettings::fromJson(s.toJson()).ui.reviewAutoPlay);
        QVERIFY(tanara::app::AppSettings::fromJson(QJsonObject()).ui.reviewAutoPlay);
    }
};

QTEST_MAIN(TestReviewFocus)
#include "test_review_focus.moc"
