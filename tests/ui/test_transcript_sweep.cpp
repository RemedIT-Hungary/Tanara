// The Edit view's sweep mode on the built-in MADE-UP meeting (real SpeakerEditSession, fake audio):
// a stop at the end of a doubtful line (+0.1 s), a decision on the target line and then
// continuing; continuous mode's 3 s trailing window; the frozen window (in lines): there the
// background marker change stays pending, outside it shows at once; "N lines doubtful again"
// for the lines already left behind; section-end summary; and in QML: the Sweep switch, keys
// to the target line (the selection does not change), the decision bar, stopping / resuming the
// player, tempo [ / ], the dock's sweep layer. Runs without a display; it touches no user data.
#include "AppContext.h"
#include "QmlApp.h"
#include "TranscriptEditorViewModel.h"
#include "TranscriptListModel.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QtTest>

#include <memory>

using namespace tanara_qml;
using Role = TranscriptListModel::Role;

namespace {

bool waitIdle(const TranscriptEditorViewModel& vm)
{
    return QTest::qWaitFor([&] {
        return !vm.embeddingRunning() && vm.session() && vm.recheck()->embeddingsComplete() && !vm.reviewRunning();
    }, 15000);
}

QVariant cell(const TranscriptEditorViewModel& vm, int row, Role role)
{
    return vm.rows()->data(vm.rows()->index(row), role);
}

bool isStopRow(const TranscriptEditorViewModel& vm, int row)
{
    return cell(vm, row, Role::KindRole).toString() == QLatin1String("utterance")
           && cell(vm, row, Role::UncertainRole).toBool() && !cell(vm, row, Role::IgnoredRole).toBool();
}

// The n-th (from 0) doubtful, not "cannot be identified" line in the list; -1 if none.
int stopRow(const TranscriptEditorViewModel& vm, int nth = 0, int from = 0)
{
    for (int r = from; r < vm.rows()->rowCount(); ++r)
        if (isStopRow(vm, r) && nth-- == 0) return r;
    return -1;
}

// Playback simulation: the position in small steps (like the real player's signals).
void playTo(TranscriptEditorViewModel& vm, int fromMs, int toMs, int step = 200)
{
    for (int ms = fromMs; ms < toMs; ms += step) vm.setPlaybackPosition(ms, true);
    vm.setPlaybackPosition(toMs, true);
}

const char* kHost = R"(
import QtQuick
import QtQuick.Controls
import Tanara
ApplicationWindow {
    width: 1100; height: 720
    color: Theme.bg
    QtObject {
        id: fakePlayer
        objectName: "player"
        property bool available: true
        property bool playing: false
        property int positionMs: 0
        property int durationMs: 0
        property real rate: 1.0
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
        anchors { left: parent.left; right: parent.right; top: parent.top; bottom: dock.top }
        player: fakePlayer
        shell: fakeShell
    }
    MapDock {
        id: dock
        objectName: "mapDock"
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        player: fakePlayer
        lanes: tab.editor.overview
        timelineMs: tab.editor.timelineMs
        sweep: tab.sweepDock
    }
}
)";

} // namespace

class TestTranscriptSweep : public QObject {
    Q_OBJECT

    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QStringList m_warnings;
    QQuickItem* m_tab = nullptr;
    QObject* m_player = nullptr;
    TranscriptEditorViewModel* m_vm = nullptr;

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
    void setPos(int ms)
    {
        m_player->setProperty("positionMs", ms);
        pump(5);
    }
    void load()
    {
        m_window.reset();
        m_warnings.clear();
        QQmlComponent comp(m_engine.get());
        comp.setData(kHost, QUrl(QStringLiteral("qrc:/qt/qml/Tanara/TranscriptSweepProbe.qml")));
        QObject* obj = comp.create();
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
        QVERIFY(QTest::qWaitFor([this] { return !m_vm->embeddingRunning() && m_vm->uncertainCount() > 0; }, 10000));
        pump(60);
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

    // ---- view model ----

    void stopMode_stopsAtUncertainRowEnd_decisionResumes()
    {
        TranscriptEditorViewModel vm;
        vm.setDemoVariant(QStringLiteral("v3"));
        vm.resolveSession();
        QVERIFY(waitIdle(vm));
        const int row = stopRow(vm);
        QVERIFY(row >= 0);
        const int start = vm.rowStartMs(row);
        const int end = vm.rowEndMs(row);
        QSignalSpy stops(&vm, &TranscriptEditorViewModel::sweepStopReached);
        QSignalSpy resumes(&vm, &TranscriptEditorViewModel::sweepResumeRequested);

        // Without sweep there is no stop.
        playTo(vm, start, end + 300);
        QCOMPARE(stops.count(), 0);

        vm.setSweepActive(true);
        QVERIFY(vm.sweepActive());
        QVERIFY(!vm.sweepContinuous());                     // stop mode by default
        vm.setPlaybackPosition(start - 500, true);
        playTo(vm, start, end + 50, 100);
        QCOMPARE(stops.count(), 0);                         // still plays before the 0.1 s lead
        vm.setPlaybackPosition(end + 120, true);
        QCOMPARE(stops.count(), 1);
        QCOMPARE(stops.first().first().toInt(), row);
        QCOMPARE(vm.sweepStopRow(), row);
        QCOMPARE(vm.sweepTargetRow(), row);

        // Enter (default): the suggestion, otherwise Good as is; then playback continues.
        QVERIFY(vm.sweepDecide(row, QStringLiteral("default")));
        QCOMPARE(resumes.count(), 1);
        QCOMPARE(vm.sweepStopRow(), -1);
        QVERIFY(!cell(vm, row, Role::UncertainRole).toBool());
        QVERIFY(cell(vm, row, Role::SweepHandledRole).toBool());
        QVERIFY(!vm.lastDecision().isEmpty());              // the "Just now" row says it

        // Replayed it no longer stops (decided).
        vm.setPlaybackPosition(start - 500, true);
        playTo(vm, start, end + 300, 100);
        QCOMPARE(stops.count(), 1);

        // The next doubtful line: Space = on without deciding (the line stays doubtful, but does not stop again).
        const int next = stopRow(vm, 0, row + 1);
        QVERIFY(next > row);
        vm.setPlaybackPosition(vm.rowStartMs(next) - 300, true);
        playTo(vm, vm.rowStartMs(next), vm.rowEndMs(next) + 150, 100);
        QCOMPARE(vm.sweepStopRow(), next);
        QVERIFY(vm.sweepDecide(next, QStringLiteral("continue")));
        QCOMPARE(resumes.count(), 2);
        QVERIFY(cell(vm, next, Role::UncertainRole).toBool());
        QVERIFY(vm.sweepNextStopRow(row) != next);

        // Off: everything back to the initial state.
        vm.setSweepActive(false);
        QCOMPARE(vm.sweepStopRow(), -1);
        QVERIFY(vm.sweepSections().isEmpty());
    }

    void continuousMode_afterWindowTargetsPreviousRow()
    {
        TranscriptEditorViewModel vm;
        vm.setDemoVariant(QStringLiteral("v3"));
        vm.resolveSession();
        QVERIFY(waitIdle(vm));
        const int row = stopRow(vm);
        QVERIFY(row >= 0);
        const int start = vm.rowStartMs(row);
        const int end = vm.rowEndMs(row);
        QSignalSpy stops(&vm, &TranscriptEditorViewModel::sweepStopReached);
        vm.setSweepActive(true);
        vm.setSweepContinuous(true);
        vm.setPlaybackPosition(start - 500, true);
        playTo(vm, start, end + 150, 100);
        QCOMPARE(stops.count(), 0);                         // does not stop
        QCOMPARE(vm.sweepAfterRow(), row);                  // trailing window: still this target
        QCOMPARE(vm.sweepTargetRow(), row);
        playTo(vm, end + 150, end + 2500, 200);
        QCOMPARE(vm.sweepTargetRow(), row);
        playTo(vm, end + 2500, end + 3100, 200);
        QCOMPARE(vm.sweepAfterRow(), -1);                   // after 3 s the running line
        QCOMPARE(vm.sweepTargetRow(), vm.playingRow());

        // Within the window a decision goes to the target line (J): that line gets it, the "Just now" row shows it.
        vm.setPlaybackPosition(start - 500, true);
        playTo(vm, start, end + 400, 100);
        QCOMPARE(vm.sweepTargetRow(), row);
        QVERIFY(vm.sweepDecide(vm.sweepTargetRow(), QStringLiteral("confirm")));
        QVERIFY(!cell(vm, row, Role::UncertainRole).toBool());
        QCOMPARE(vm.lastDecision().value(QStringLiteral("utteranceId")).toString(),
                 cell(vm, row, Role::UtteranceIdRole).toString());
        QCOMPARE(vm.sweepAfterRow(), -1);
    }

    void frozenWindow_holdsMarkersInRows_releasesOutside()
    {
        TranscriptEditorViewModel vm;
        vm.setDemoVariant(QStringLiteral("v3"));
        vm.resolveSession();
        QVERIFY(waitIdle(vm));
        const int inside = stopRow(vm, 0);
        const int outside = stopRow(vm, 0, inside + 8);
        QVERIFY(inside >= 0 && outside > inside + 8);
        vm.setSweepActive(true);
        vm.setPlaybackPosition(vm.rowStartMs(inside), true);
        // The visible area: inside-1 ... inside+2, so the window +- 2 lines.
        vm.setSweepWindow(std::max(0, inside - 1), inside + 2);
        QVERIFY(vm.sweepFrozenFirstRow() <= inside && vm.sweepFrozenLastRow() >= inside + 4);
        QVERIFY(vm.sweepFrozenLastRow() < outside);

        // "Background change" (not the VM's decision): the editor confirms the two lines directly.
        const QString insideId = cell(vm, inside, Role::UtteranceIdRole).toString();
        const QString outsideId = cell(vm, outside, Role::UtteranceIdRole).toString();
        QSignalSpy markers(&vm, &TranscriptEditorViewModel::sweepMarkersChanged);
        QVERIFY(vm.session()->confirmUtterances({insideId, outsideId}));
        // Within the window the marker does not change (pending), outside it does at once.
        QVERIFY(cell(vm, inside, Role::UncertainRole).toBool());
        QVERIFY(!cell(vm, outside, Role::UncertainRole).toBool());
        QCOMPARE(vm.pendingMarkerIds(), QStringList{insideId});
        QVERIFY(markers.count() > 0);

        // The VM's own decision shows at once within the window.
        const int other = stopRow(vm, 0, inside + 1);
        if (other >= 0 && other <= vm.sweepFrozenLastRow()) {
            QVERIFY(vm.sweepDecide(other, QStringLiteral("confirm")));
            QVERIFY(!cell(vm, other, Role::UncertainRole).toBool());
        }

        // The window moves on (scrolling): the line leaves it, so the pending marker appears.
        vm.setSweepWindow(outside, outside + 3);
        QVERIFY(!cell(vm, inside, Role::UncertainRole).toBool());
        QVERIFY(vm.pendingMarkerIds().isEmpty());
    }

    void rebehind_countsRowsFlaggedBehind()
    {
        TranscriptEditorViewModel vm;
        vm.setDemoVariant(QStringLiteral("v3"));
        vm.resolveSession();
        QVERIFY(waitIdle(vm));
        const int behind = stopRow(vm, 0);
        QVERIFY(behind >= 0);
        const int last = vm.rows()->rowCount() - 1;
        const QString id = cell(vm, behind, Role::UtteranceIdRole).toString();
        // The line is marked OK by the "background", then doubtful again, after playback has passed it.
        QVERIFY(vm.session()->confirmUtterances({id}));
        QVERIFY(!cell(vm, behind, Role::UncertainRole).toBool());
        vm.setSweepActive(true);
        vm.setPlaybackPosition(vm.rowStartMs(last), true);
        vm.setSweepWindow(last - 3, last);
        QCOMPARE(vm.rebehindCount(), 0);
        vm.session()->undo();                                // not the VM's path: looks like a background change
        QVERIFY(cell(vm, behind, Role::UncertainRole).toBool());
        QCOMPARE(vm.rebehindCount(), 1);
        const QVariantList list = vm.rebehindRows();
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().toMap().value(QStringLiteral("utteranceId")).toString(), id);
        QCOMPARE(list.first().toMap().value(QStringLiteral("row")).toInt(), behind);
        // A decision on the line removes it from the list; "Look back" goes to the line's start.
        QVERIFY(vm.sweepReviewBehind() == vm.rowStartMs(behind));
        QVERIFY(vm.sweepDecide(behind, QStringLiteral("confirm")));
        QCOMPARE(vm.rebehindCount(), 0);
    }

    void sections_endWithSummary()
    {
        TranscriptEditorViewModel vm;
        vm.setDemoVariant(QStringLiteral("v3"));
        vm.resolveSession();
        QVERIFY(waitIdle(vm));
        vm.setSweepActive(true);
        vm.setSweepContinuous(true);                        // the stops must not interrupt
        vm.setSweepSectionLimits(12 * 60 * 1000, 3);         // a new section every 3 doubtful lines
        const QVariantList sections = vm.sweepSections();
        QVERIFY(sections.size() >= 2);
        const QVariantMap first = sections.first().toMap();
        QVERIFY(first.value(QStringLiteral("active")).toBool());
        QVERIFY(first.value(QStringLiteral("uncertain")).toInt() <= 3);
        QVERIFY(!first.value(QStringLiteral("label")).toString().isEmpty());

        // Deciding one doubtful line in the section, then playback runs to the end of the section.
        const int row = stopRow(vm);
        QVERIFY(vm.sweepDecide(row, QStringLiteral("confirm")));
        QSignalSpy ended(&vm, &TranscriptEditorViewModel::sweepSectionEnded);
        const int endMs = int(first.value(QStringLiteral("endMs")).toDouble());
        vm.setPlaybackPosition(endMs - 600, true);
        playTo(vm, endMs - 400, endMs + 300, 100);
        QCOMPARE(ended.count(), 1);
        const QVariantMap summary = vm.sweepSectionSummary();
        QCOMPARE(summary.value(QStringLiteral("index")).toInt(), 0);
        QCOMPARE(summary.value(QStringLiteral("decided")).toInt(), 1);
        QVERIFY(!summary.value(QStringLiteral("nextLabel")).toString().isEmpty());
        QCOMPARE(vm.sweepSectionIndex(), 1);
        QVERIFY(vm.sweepSections().first().toMap().value(QStringLiteral("done")).toBool());
        QVERIFY(vm.sweepDock().value(QStringLiteral("doneCount")).toInt() == 1
                || QTest::qWaitFor([&] { return vm.sweepDock().value(QStringLiteral("doneCount")).toInt() == 1; }, 1000));
        // "Next section": the start of the second section, the summary closes.
        const int next = vm.sweepNextSection();
        QCOMPARE(next, int(sections.at(1).toMap().value(QStringLiteral("startMs")).toDouble()));
        QVERIFY(vm.sweepSectionSummary().isEmpty());
    }

    // ---- QML: switch, keys, decision bar, player, tempo, dock ----

    void qml_toggleKeysDecisionBarTempo()
    {
        load();
        QMetaObject::invokeMethod(m_tab, "setViewMode", Q_ARG(QVariant, QStringLiteral("fix")));
        pump(60);
        QQuickItem* toggle = visual("sweepToggle");
        QVERIFY(toggle);
        QTest::mouseClick(m_window.get(), Qt::LeftButton, Qt::NoModifier,
                          toggle->mapToScene(QPointF(10, toggle->height() / 2)).toPoint());
        pump(60);
        QVERIFY(m_vm->sweepActive());
        QVERIFY(visual("sweepStrip"));
        QTRY_VERIFY(!m_vm->sweepDock().isEmpty());
        QTRY_VERIFY(!visuals("mapSweepDot").isEmpty());     // the dock's doubtful dots
        QVERIFY(!visuals("mapSweepSection").isEmpty());

        // Tempo: ] / [ (1.0-2.0x).
        QTest::keyClick(m_window.get(), Qt::Key_BracketRight);
        QCOMPARE(m_player->property("rate").toReal(), 1.25);
        QTest::keyClick(m_window.get(), Qt::Key_BracketRight);
        QTest::keyClick(m_window.get(), Qt::Key_BracketRight);
        QTest::keyClick(m_window.get(), Qt::Key_BracketRight);
        QTest::keyClick(m_window.get(), Qt::Key_BracketRight);
        QCOMPARE(m_player->property("rate").toReal(), 2.0);
        for (int i = 0; i < 6; ++i) QTest::keyClick(m_window.get(), Qt::Key_BracketLeft);
        QCOMPARE(m_player->property("rate").toReal(), 1.0);

        // The key goes to the running line (without a selection), the selection does not change.
        const int row = stopRow(*m_vm);
        QVERIFY(row >= 1);
        const int selected = row == 1 ? 0 : 1;
        m_vm->selectOnly(selected);
        setPos(m_vm->rowStartMs(row) + 20);
        QCOMPARE(m_vm->playingRow(), row);
        QCOMPARE(m_vm->sweepTargetRow(), row);
        QTest::keyClick(m_window.get(), Qt::Key_J);
        pump();
        QVERIFY(!cell(*m_vm, row, Role::UncertainRole).toBool());
        QVERIFY(cell(*m_vm, row, Role::ConfirmedRole).toBool() || cell(*m_vm, row, Role::CorrectedRole).toBool());
        QCOMPARE(m_vm->currentRow(), selected);
        QCOMPARE(m_vm->selectedCount(), 1);

        // Stop: during playback the player stops at the end of the doubtful line, the decision bar under it.
        const int next = stopRow(*m_vm, 0, row + 1);
        QVERIFY(next > row);
        m_player->setProperty("log", QString());
        m_player->setProperty("playing", true);
        setPos(m_vm->rowStartMs(next) - 300);
        for (int ms = m_vm->rowStartMs(next); ms < m_vm->rowEndMs(next) + 150; ms += 100) setPos(ms);
        setPos(m_vm->rowEndMs(next) + 150);
        QCOMPARE(m_vm->sweepStopRow(), next);
        QVERIFY(m_player->property("log").toString().contains(QLatin1String("pause;")));
        QVERIFY(!m_player->property("playing").toBool());
        QTRY_VERIFY(visual("sweepDecisionBar"));
        QVERIFY(visual("sweepDefault"));
        QVERIFY(visual("sweepIgnore"));
        QVERIFY(!visuals("sweepLane").isEmpty());

        // Shift: the labels get "- not a sample".
        QTest::keyPress(m_window.get(), Qt::Key_Shift, Qt::ShiftModifier);
        pump();
        QVERIFY(visual("sweepDefault")->property("text").toString().endsWith(QStringLiteral(" · nem minta")));
        QTest::keyRelease(m_window.get(), Qt::Key_Shift);
        pump();

        // 0 = cannot be identified on the stop line; then playback continues, the bar disappears.
        QTest::keyClick(m_window.get(), Qt::Key_0);
        pump();
        QVERIFY(cell(*m_vm, next, Role::IgnoredRole).toBool());
        QCOMPARE(m_vm->sweepStopRow(), -1);
        QVERIFY(m_player->property("playing").toBool());
        QTRY_VERIFY(!visual("sweepDecisionBar"));

        // Stop, then Enter (the bar's button on click is the same).
        const int third = stopRow(*m_vm, 0, next + 1);
        if (third > next) {
            setPos(m_vm->rowStartMs(third) - 300);
            for (int ms = m_vm->rowStartMs(third); ms < m_vm->rowEndMs(third) + 150; ms += 100) setPos(ms);
            setPos(m_vm->rowEndMs(third) + 150);
            QCOMPARE(m_vm->sweepStopRow(), third);
            QTRY_VERIFY(visual("sweepDefault"));
            QTest::keyClick(m_window.get(), Qt::Key_Return);
            pump();
            QCOMPARE(m_vm->sweepStopRow(), -1);
            QVERIFY(!cell(*m_vm, third, Role::UncertainRole).toBool());
            QVERIFY(m_player->property("playing").toBool());
        }

        // Sweep lives only in Edit: switching to Read turns it off.
        QMetaObject::invokeMethod(m_tab, "setViewMode", Q_ARG(QVariant, QStringLiteral("read")));
        pump(60);
        QVERIFY(!m_vm->sweepActive());
        QVERIFY(!visual("sweepStrip"));
    }
};

QTEST_MAIN(TestTranscriptSweep)
#include "test_transcript_sweep.moc"
