// The "Transcript" tab (TranscriptTab.qml) without a display (offscreen), on the built-in
// MADE-UP meeting: mouse and keyboard events to the QML window (not the user's desktop), with
// a fake player and shell. Covers: Ctrl+L, clicking into another column, Ctrl+Z, selecting
// several lines + a number key, drag, timestamp seek, "+" opens the person picker, playback
// following, the Shell's position request, "Listen"; and the scope of a fix: name = ONLY that
// line (with a selection, the selected lines), a whole speaker from the track header /
// overview, confirmation before a merge, a notice bar after every reassignment (Undo /
// "Similar N lines too" / "all N lines"), and the "Uncertain" filter does not jump away from
// under the fixed line.
// Voiceprint: an indicator on every row of the map dock (MapDock) + its own panel, the block in
// the whole-speaker scope of the panel opened from a line, the offer on the bar after naming a
// whole speaker, and undoing the just-created voiceprint.
#include "AppContext.h"
#include "QmlApp.h"
#include "TranscriptEditorViewModel.h"
#include "ReviewQueueModel.h"
#include "TranscriptListModel.h"

#include <QClipboard>
#include <QJSValue>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

#include <memory>

using namespace tanara_qml;
using Role = TranscriptListModel::Role;

namespace {

const char* kHost = R"(
import QtQuick
import QtQuick.Controls
import Tanara
ApplicationWindow {
    width: 1004; height: 640
    color: Theme.bg
    property string variant: ""
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
        property var bounds: []
        function playRange(a, b, lo, hi) { log += "range:" + a + "-" + b + ";"; bounds = [lo, hi] }
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
        demoVariant: parent ? variant : ""
    }
    // A beszélőnkénti idővonal a lejátszó fölötti térkép-dokkban él (a Main.qml-hez hasonlóan bekötve).
    MapDock {
        id: dock
        objectName: "mapDock"
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        player: fakePlayer
        lanes: tab.editor.overview
        viewportStart: tab.viewportStart
        viewportSize: tab.viewportSize
        collapsedCount: tab.editor.collapsedCount
        lanesExpanded: tab.editor.lanesExpanded
        timelineMs: tab.editor.timelineMs
        onSeekRequested: fraction => tab.seekToFraction(fraction)
        onSpeakerClicked: (key, anchor) => tab.openSpeakerPopover(key, anchor)
        onVoiceprintClicked: (key, anchor) => tab.openVoiceprintPopover(key, anchor)
        onExpandRequested: tab.editor.lanesExpanded = true
        onCollapseRequested: tab.editor.lanesExpanded = false
        similarMarks: tab.similarMarks
        onLineStepRequested: delta => tab.stepLine(delta)
        sampleStats: tab.viewMode !== "read" ? tab.editor.sampleStats : []
        onSamplesClicked: key => tab.showSessionSamples(key)
    }
}
)";

} // namespace

class TestTranscriptTab : public QObject {
    Q_OBJECT

    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QStringList m_warnings;
    QQuickItem* m_tab = nullptr;
    QQuickItem* m_list = nullptr;
    QObject* m_player = nullptr;
    QObject* m_shell = nullptr;
    TranscriptEditorViewModel* m_vm = nullptr;

    void pump(int ms = 30) { QTest::qWait(ms); }

    QQuickItem* rowItem(int row)
    {
        QQuickItem* item = nullptr;
        QMetaObject::invokeMethod(m_list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, item), Q_ARG(int, row));
        return item;
    }
    QVariant cell(int row, Role role) const { return m_vm->rows()->data(m_vm->rows()->index(row), role); }
    QString speakerOf(int row) const { return cell(row, Role::SpeakerKeyRole).toString(); }
    QString laneKey(int lane) const { return m_vm->lanes().at(lane).toMap().value(QStringLiteral("key")).toString(); }

    // The center of the rail's `lane` column at the height of row `row` (window coordinates).
    QPoint railPoint(int row, int lane)
    {
        QQuickItem* item = rowItem(row);
        if (!item) return {};
        const qreal x = m_tab->property("railX").toReal() + 4 + lane * 24 + 12;
        const QPointF p = item->mapToScene(QPointF(0, item->height() / 2));
        return QPoint(int(m_tab->mapToScene(QPointF(x, 0)).x()), int(p.y()));
    }
    QPoint textPoint(int row)
    {
        QQuickItem* item = rowItem(row);
        if (!item) return {};
        return item->mapToScene(QPointF(item->width() - 60, item->height() - 12)).toPoint();
    }
    QPoint center(QQuickItem* item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); }
    void click(const QPoint& p, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        QTest::mouseClick(m_window.get(), Qt::LeftButton, mods, p);
        pump();
    }
    static QQuickItem* findByText(QQuickItem* root, const QString& text)
    {
        if (!root) return nullptr;
        if (root->property("text").toString() == text && root->inherits("QQuickAbstractButton")) return root;
        for (QQuickItem* child : root->childItems())
            if (QQuickItem* hit = findByText(child, text)) return hit;
        return nullptr;
    }
    // Search in the VISIBLE tree (this also covers the Repeater / ListView delegates and the
    // content of popup panels).
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
    // The To review round-start opener suggestions skipped (Later): the focus card comes.
    bool skipOpeners()
    {
        QQuickItem* rv = m_window->findChild<QQuickItem*>(QStringLiteral("reviewView"));
        if (!rv || !QTest::qWaitFor([this] { return m_vm->queueActive(); }, 10000)) return false;
        pump(60);
        for (int g = 0; g < 20 && rv->property("phase").toString() == QLatin1String("opener"); ++g)
            QMetaObject::invokeMethod(rv, "openerLater");
        return rv->property("phase").toString() != QLatin1String("opener");
    }
    // A selectable row of the panel (a speaker of the meeting / a known person) by name.
    QQuickItem* choice(const char* kind, const QString& personName) const
    {
        for (QQuickItem* item : visuals(kind))
            if (item->property("personName").toString() == personName) return item;
        return nullptr;
    }
    // The line panel ("Whose line is this?", LinePopover) and the whole speaker's panel ("Why them?").
    QObject* popover() const { return m_window->findChild<QObject*>(QStringLiteral("linePopover")); }
    QObject* why() const { return m_window->findChild<QObject*>(QStringLiteral("speakerWhyPopover")); }
    QObject* changeBar() const { return m_window->findChild<QObject*>(QStringLiteral("changeBar")); }
    // Switching on To review (the focus view), waiting for the background analysis and the work list.
    void openReview()
    {
        QVERIFY(QTest::qWaitFor([this] { return !m_vm->reviewRunning() && m_vm->reviewCount() > 0; }, 10000));
        m_vm->setUncertainOnly(true);
        pump(60);
        QTRY_VERIFY(visual("reviewView"));
        QTRY_VERIFY_WITH_TIMEOUT(m_vm->queueActive() && !focusLine().isEmpty(), 10000);
        QTRY_VERIFY(visual("reviewFocusCard"));
        m_tab->forceActiveFocus();
    }
    // The focus card's line (the work list's cursor).
    QString focusLine() const { return m_vm->reviewQueue()->currentUtterance(); }
    // The reassignment notice (and its offers) in the "Just now" row: the row is visible and the change is live.
    // (The "Just now" row itself is always visible in Edit mode: imentShown.)
    bool barShown() const { return imentShown() && m_vm->changeActive(); }
    bool imentShown() const { return changeBar() && changeBar()->property("visible").toBool(); }
    QString barText() const { return m_vm->changeText(); }
    void type(const QString& text)
    {
        for (const QChar c : text) QTest::keyClick(m_window.get(), c.toLatin1());
        pump();
    }
    QString keyOfName(const QString& name) const
    {
        for (const QVariant& s : m_vm->speakers())
            if (s.toMap().value(QStringLiteral("name")).toString() == name)
                return s.toMap().value(QStringLiteral("key")).toString();
        return {};
    }
    // A speaker's voiceprint indicator on the overview.
    QQuickItem* markOf(const QString& key) const
    {
        for (QQuickItem* item : visuals("overviewVoiceprint"))
            if (item->property("speakerKey").toString() == key) return item;
        return nullptr;
    }
    QString markState(const QString& key) const
    {
        QQuickItem* mark = markOf(key);
        return mark ? mark->property("voiceprint").toString() : QStringLiteral("?");
    }
    QString textOf(const char* name) const
    {
        QQuickItem* item = visual(name);
        return item ? item->property("text").toString() : QStringLiteral("<nincs>");
    }
    // The number of a person's voiceprints in the (made-up) meeting's voiceprint store.
    int printCount(const QString& person) const
    {
        for (const tanara::editing::PersonInfo& p : m_vm->people())
            if (p.name == person) return p.voiceprintCount;
        return 0;
    }
    int linesOf(const QString& key) const
    {
        return m_vm->speakerInfo(key).value(QStringLiteral("utteranceCount")).toInt();
    }
    void reveal(int row)
    {
        QMetaObject::invokeMethod(m_tab, "revealRow", Q_ARG(QVariant, row), Q_ARG(QVariant, QVariant()));
        pump(60);
    }
    QQuickItem* nameOf(int row)
    {
        QQuickItem* item = rowItem(row);
        return item ? item->findChild<QQuickItem*>(QStringLiteral("speakerName")) : nullptr;
    }

    // The panel has fully disappeared (the closing animation has run too): until then its veil
    // still swallows the next click.
    bool popupGone(const char* name) const
    {
        QObject* popup = m_window->findChild<QObject*>(QLatin1String(name));
        return popup && !popup->property("visible").toBool();
    }
    // Ctrl+Z to the editor (after the panel closes, waiting for the focus to return).
    void undoKey()
    {
        QTRY_VERIFY(m_tab->hasActiveFocus());
        QTest::keyClick(m_window.get(), Qt::Key_Z, Qt::ControlModifier);
    }
    bool noisyOf(const QString& id) const
    {
        const int i = m_vm->utteranceIndexOf(id);
        return i >= 0 && m_vm->utterances()[i].noisy;
    }
    QString rowId(int row) const { return cell(row, Role::UtteranceIdRole).toString(); }
    bool popupOpen(const char* name) const
    {
        QObject* popup = m_window->findChild<QObject*>(QLatin1String(name));
        return popup && popup->property("opened").toBool();
    }

    void load(const QString& variant = QString())
    {
        m_window.reset();
        m_warnings.clear();
        QQmlComponent comp(m_engine.get());
        comp.setData(kHost, QUrl(QStringLiteral("qrc:/qt/qml/Tanara/TranscriptTabProbe.qml")));
        QObject* obj = comp.createWithInitialProperties({{QStringLiteral("variant"), variant}});
        QVERIFY2(obj, qPrintable(comp.errorString()));
        m_window.reset(qobject_cast<QQuickWindow*>(obj));
        QVERIFY(m_window);
        m_window->show();
        m_window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(m_window.get()));

        m_tab = m_window->findChild<QQuickItem*>(QStringLiteral("tab"));
        m_list = m_window->findChild<QQuickItem*>(QStringLiteral("transcriptList"));
        m_player = m_window->findChild<QObject*>(QStringLiteral("player"));
        m_shell = m_window->findChild<QObject*>(QStringLiteral("shell"));
        m_vm = m_window->findChild<TranscriptEditorViewModel*>();
        QVERIFY(m_tab && m_list && m_player && m_shell && m_vm);
        if (m_vm->hasTranscript() && m_vm->voiceAvailable())
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

    void railToggle_clickToMove_undo()
    {
        load();
        QVERIFY(m_vm->demo());
        QVERIFY(!m_vm->railVisible());                      // hidden by default
        QCOMPARE(m_tab->property("textX").toReal(), 0.0);

        QTest::keyClick(m_window.get(), Qt::Key_L, Qt::ControlModifier);
        QVERIFY(m_vm->railVisible());
        QVERIFY(m_tab->property("textX").toReal() > 100.0);
        pump();

        // A click into the third column of line 2 (Fehér Ádám): the line moves there.
        QSignalSpy resets(m_vm->rows(), &QAbstractItemModel::modelReset);
        const QString before = speakerOf(2);
        QCOMPARE(before, laneKey(1));
        click(railPoint(2, 2));
        QCOMPARE(speakerOf(2), laneKey(2));
        QVERIFY(cell(2, Role::CorrectedRole).toBool());
        QVERIFY(m_vm->canUndo());
        QCOMPARE(m_vm->selectedCount(), 0);
        // The notice bar appeared: what happened + Undo in one click.
        QTRY_VERIFY(barShown());
        QCOMPARE(barText(), QStringLiteral("1 sor átkerült ide: %1")
                                .arg(m_vm->lanes().at(2).toMap().value(QStringLiteral("name")).toString()));
        QVERIFY(visual("changeUndo"));

        undoKey();
        QCOMPARE(speakerOf(2), before);
        QVERIFY(!barShown());                               // after undo there is nothing to show
        QTest::keyClick(m_window.get(), Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(speakerOf(2), laneKey(2));
        undoKey();
        QCOMPARE(speakerOf(2), before);
        QCOMPARE(resets.count(), 0);

        // A click on the line's OWN block: selection (not a move).
        click(railPoint(2, 1));
        QCOMPARE(m_vm->selectedCount(), 1);
        QCOMPARE(speakerOf(2), before);
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QCOMPARE(m_vm->selectedCount(), 0);
    }

    void multiSelect_numberKey_selectionBar()
    {
        load(QStringLiteral("two"));
        QObject* bar = m_window->findChild<QObject*>(QStringLiteral("selectionBar"));
        QVERIFY(bar);
        QVERIFY(!bar->property("visible").toBool());

        // A click on the text, then Shift+click: lines 2-4 selected (works without the rail too).
        click(textPoint(2));
        QCOMPARE(m_vm->selectedCount(), 1);
        click(textPoint(4), Qt::ShiftModifier);
        QCOMPARE(m_vm->selectedCount(), 3);
        QVERIFY(bar->property("visible").toBool());
        click(textPoint(3), Qt::ControlModifier);           // Ctrl: line 3 out
        QCOMPARE(m_vm->selectedCount(), 2);
        click(textPoint(3), Qt::ControlModifier);
        QCOMPARE(m_vm->selectedCount(), 3);

        // "2": the selection to the second speaker.
        QTest::keyClick(m_window.get(), Qt::Key_2);
        for (int r : {2, 3, 4}) QCOMPARE(speakerOf(r), laneKey(1));
        QCOMPARE(m_vm->selectedCount(), 0);
        QVERIFY(!bar->property("visible").toBool());
        // After a number key the bar is there too (in place of the selection's bar), with the count.
        QTRY_VERIFY(barShown());
        QCOMPARE(barText(), QStringLiteral("3 sor átkerült ide: Fehér Ádám"));
        undoKey();   // one step
        for (int r : {2, 3, 4}) QCOMPARE(speakerOf(r), laneKey(0));

        // Down / up: steps the selection; Enter: plays from the line.
        click(textPoint(1));
        QTest::keyClick(m_window.get(), Qt::Key_Down);
        QCOMPARE(m_vm->currentRow(), 2);
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QVERIFY(m_player->property("log").toString().contains(QStringLiteral("seek:21000;play;")));
    }

    void drag_movesTouchedRows()
    {
        load();
        m_vm->setRailVisible(true);
        pump();
        // Grabbing line 2's block, dragged over column 4 and down to line 3.
        const QPoint from = railPoint(2, 1);
        const QPoint to = railPoint(3, 3);
        QTest::mousePress(m_window.get(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(m_window.get(), from + QPoint(6, 6));
        QTest::mouseMove(m_window.get(), QPoint(to.x(), from.y() + 10));
        QTest::mouseMove(m_window.get(), to);
        pump();
        QVERIFY(m_tab->property("dragActive").toBool());
        QCOMPARE(m_tab->property("dragLane").toInt(), 3);
        QCOMPARE(m_tab->property("dragToRow").toInt(), 3);
        QTest::mouseRelease(m_window.get(), Qt::LeftButton, Qt::NoModifier, to);
        pump();
        QVERIFY(!m_tab->property("dragActive").toBool());
        QCOMPARE(speakerOf(2), laneKey(3));
        QCOMPARE(speakerOf(3), laneKey(3));
        // After a drag the bar appears too; its "Undo" button reverts it in one step.
        QTRY_VERIFY(barShown());
        QVERIFY(barText().startsWith(QStringLiteral("2 sor átkerült ide: ")));
        QVERIFY(visual("changeUndo"));
        click(center(visual("changeUndo")));                // the drag is one step
        QVERIFY(!barShown());
        QCOMPARE(speakerOf(2), laneKey(1));
        QCOMPARE(speakerOf(3), laneKey(0));
        QVERIFY(!m_vm->canUndo());
    }

    void timestamp_seeks_andPlaybackIsFollowed()
    {
        load();
        QQuickItem* stamp = rowItem(2)->findChild<QQuickItem*>(QStringLiteral("stamp"));
        QVERIFY(stamp);
        click(center(stamp));
        QCOMPARE(m_player->property("log").toString(), QStringLiteral("seek:72000;play;"));
        QCOMPARE(m_vm->playingRow(), 2);

        // Playback advances: the list follows (the playing line stays visible).
        const int far = m_vm->rowForTime(600000);
        m_player->setProperty("positionMs", 600000);
        pump(60);
        QCOMPARE(m_vm->playingRow(), far);
        QVERIFY(m_list->property("contentY").toReal() > 0.0);
        QVERIFY(rowItem(far) != nullptr);

        // The user scrolls away: stepping to the next line does NOT pull the list back.
        QMetaObject::invokeMethod(m_list, "positionViewAtBeginning");
        pump();
        const qreal top = m_list->property("contentY").toReal();
        QVERIFY(rowItem(0) != nullptr);
        m_player->setProperty("positionMs", m_vm->rowStartMs(far + 1) + 10);
        pump(60);
        QCOMPARE(m_vm->playingRow(), far + 1);
        QCOMPARE(m_list->property("contentY").toReal(), top);
        const qreal before = top;

        // The Shell's request (e.g. a summary link): scrolls there.
        QMetaObject::invokeMethod(m_shell, "transcriptPositionRequested", Q_ARG(int, 1200000));
        pump(60);
        QVERIFY(m_list->property("contentY").toReal() != before);
        QVERIFY(rowItem(m_vm->rowForTime(1200000)) != nullptr);
    }

    // Clicking a name moves ONLY THAT LINE (that was the bug: it used to move the whole speaker).
    void nameClick_movesOnlyThatLine()
    {
        load();
        QVERIFY(!m_vm->railVisible());                      // works with a hidden rail too
        QQuickItem* name = nameOf(2);
        QVERIFY(name);
        const QString from = speakerOf(2);
        const int lines = linesOf(from);
        QVERIFY(lines > 10);
        const QString varga = keyOfName(QStringLiteral("Varga Nóra"));
        const int vargaLines = linesOf(varga);

        click(center(name));
        QTRY_VERIFY(popupOpen("linePopover"));
        QCOMPARE(m_tab->property("speakerPopoverRow").toInt(), 2);
        QCOMPARE(popover()->property("scope").toString(), QStringLiteral("line"));
        // The title and the footer state the scope and the count.
        QCOMPARE(visual("popoverTitle")->property("text").toString(), QStringLiteral("Kinek a sora ez?"));
        QCOMPARE(visual("popoverFooter")->property("text").toString(),
                 QStringLiteral("1 sor kerül át · Ctrl+Z visszavonja · a gép tanul belőle"));
        // In the per-line scope there is no voiceprint marker and no "merge" list; the meeting's
        // other speakers are first as move targets.
        QVERIFY(!visual("fixVoiceprints"));
        QVERIFY(!visual("mergeList"));
        QVERIFY(visual("meetingSpeakers"));
        QVERIFY(choice("speakerChoice", QStringLiteral("Varga Nóra")));
        QVERIFY(!choice("speakerChoice", cell(2, Role::SpeakerNameRole).toString()));
        // The current speaker is at the end of the candidates, dimmed ("current").
        QVERIFY(choice("currentChoice", cell(2, Role::SpeakerNameRole).toString()));

        type(QStringLiteral("varga"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popupGone("linePopover"));
        QCOMPARE(speakerOf(2), varga);
        QCOMPARE(linesOf(from), lines - 1);                 // only ONE line moved
        QCOMPARE(linesOf(varga), vargaLines + 1);
        QVERIFY(!popupOpen("mergeDialog"));                 // a single line never asks
        QTRY_COMPARE(m_tab->property("speakerPopoverRow").toInt(), -1);

        // The bar: what happened, Undo; the source still has lines, so "all N lines" is there too.
        QTRY_VERIFY(barShown());
        QCOMPARE(barText(), QStringLiteral("1 sor átkerült ide: Varga Nóra"));
        QCOMPARE(m_vm->changeRestCount(), lines - 1);
        QVERIFY(visual("changeRest"));
        QVERIFY(rowItem(2) != nullptr);
        const QPointF rowBottom = rowItem(2)->mapToScene(QPointF(0, rowItem(2)->height()));
        QVERIFY(rowBottom.y() <= static_cast<QQuickItem*>(changeBar())->mapToScene(QPointF(0, 0)).y());

        click(center(visual("changeUndo")));
        QCOMPARE(speakerOf(2), from);
        QCOMPARE(linesOf(from), lines);
        QVERIFY(!m_vm->canUndo());                          // it was a single step
        QVERIFY(!barShown());

        // To a known person (not yet in the meeting): a new participant, with just this line.
        click(center(nameOf(2)));
        QTRY_VERIFY(popupOpen("linePopover"));
        type(QStringLiteral("molnar"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popupGone("linePopover"));
        QCOMPARE(cell(2, Role::SpeakerNameRole).toString(), QStringLiteral("Molnár Eszter"));
        QCOMPARE(linesOf(from), lines - 1);
        QCOMPARE(linesOf(speakerOf(2)), 1);
        undoKey();
        QCOMPARE(linesOf(from), lines);
    }

    // If the line is part of a multi-line selection, clicking a name moves the SELECTED lines.
    void nameClick_withMultiSelection_movesSelection()
    {
        load();
        click(textPoint(2));
        click(textPoint(4), Qt::ShiftModifier);
        QCOMPARE(m_vm->selectedCount(), 3);
        const QStringList before{speakerOf(2), speakerOf(3), speakerOf(4)};
        const QString varga = keyOfName(QStringLiteral("Varga Nóra"));
        const int vargaLines = linesOf(varga);

        click(center(nameOf(2)));
        QTRY_VERIFY(popupOpen("linePopover"));
        QCOMPARE(m_vm->selectedCount(), 3);                 // clicking a name does not break the selection
        QCOMPARE(popover()->property("scope").toString(), QStringLiteral("selection"));
        QVERIFY(visual("scopeSelection"));
        QCOMPARE(visual("popoverTitle")->property("text").toString(), QStringLiteral("Kié a kijelölt 3 sor?"));
        QCOMPARE(visual("popoverFooter")->property("text").toString(),
                 QStringLiteral("3 sor kerül át · Ctrl+Z visszavonja · a gép tanul belőle"));
        click(center(choice("speakerChoice", QStringLiteral("Varga Nóra"))));
        QTRY_VERIFY(popupGone("linePopover"));
        for (int r : {2, 3, 4}) QCOMPARE(speakerOf(r), varga);
        QCOMPARE(linesOf(varga), vargaLines + 3);
        QCOMPARE(m_vm->selectedCount(), 0);
        QVERIFY(!popupOpen("mergeDialog"));                 // a selection does not ask either
        QTRY_VERIFY(barShown());
        QCOMPARE(barText(), QStringLiteral("3 sor átkerült ide: Varga Nóra"));

        undoKey();   // one step
        QCOMPARE(QStringList({speakerOf(2), speakerOf(3), speakerOf(4)}), before);
        QVERIFY(!m_vm->canUndo());

        // Clicking the name for a line outside the selection again makes that one line the scope.
        click(textPoint(2));
        click(textPoint(4), Qt::ShiftModifier);
        click(center(nameOf(0)));
        QTRY_VERIFY(popupOpen("linePopover"));
        QCOMPARE(popover()->property("scope").toString(), QStringLiteral("line"));
        QVERIFY(!visual("scopeSelection"));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("linePopover"));
        QVERIFY(!m_vm->canUndo());
    }

    // On the panel opened from a line the whole speaker is an EXPLICIT choice: all its lines go,
    // and if the target is already a speaker of the meeting (a merge), it first asks for confirmation with the numbers.
    void wholeSpeakerScope_movesAll_andAsksBeforeMerge()
    {
        load();
        const QString key = speakerOf(0);
        const int lines = linesOf(key);
        const QString feher = keyOfName(QStringLiteral("Fehér Ádám"));
        const int feherLines = linesOf(feher);
        const int speakers = m_vm->speakerCount();

        // 1) Renaming to a person not yet in the meeting: no question, all its lines go.
        click(center(nameOf(0)));
        QTRY_VERIFY(popupOpen("linePopover"));
        click(center(visual("scopeSpeaker")));
        QCOMPARE(popover()->property("scope").toString(), QStringLiteral("speaker"));
        QCOMPARE(visual("popoverTitle")->property("text").toString(), QStringLiteral("Kovács Lilla valójában…"));
        QCOMPARE(visual("popoverFooter")->property("text").toString(),
                 QStringLiteral("A teljes beszélőre vonatkozik: mind az %1 sor · Ctrl+Z visszavonja").arg(lines));   // 52: "the"
        QVERIFY(visual("fixVoiceprints"));                  // only here: the voiceprint marker
        QVERIFY(visual("mergeList"));                       // and the merge list
        type(QStringLiteral("molnar"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popupGone("linePopover"));
        QVERIFY(!popupOpen("mergeDialog"));
        QCOMPARE(cell(0, Role::SpeakerNameRole).toString(), QStringLiteral("Molnár Eszter"));
        QCOMPARE(linesOf(key), lines);
        QTRY_VERIFY(barShown());
        QCOMPARE(barText(), QStringLiteral("Kovács Lilla mind az %1 sora átkerült ide: Molnár Eszter").arg(lines));
        QVERIFY(!visual("changeRest"));                     // no bulk continuation after a whole speaker
        click(center(visual("changeUndo")));
        QCOMPARE(cell(0, Role::SpeakerNameRole).toString(), QStringLiteral("Kovács Lilla"));
        QVERIFY(!m_vm->canUndo());

        // 2) The target is already a speaker (Fehér Ádám): confirmation with numbers; "Cancel": nothing changes.
        click(center(nameOf(0)));
        QTRY_VERIFY(popupOpen("linePopover"));
        click(center(visual("scopeSpeaker")));
        type(QStringLiteral("feher"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popupOpen("mergeDialog"));
        QTRY_VERIFY(popupGone("linePopover"));
        QVERIFY(!m_vm->canUndo());
        QCOMPARE(linesOf(key), lines);
        const QString question = visual("mergeText")->property("text").toString();
        QCOMPARE(question, QStringLiteral("Kovács Lilla %1 sora összeolvad ezzel: Fehér Ádám (%2 sor). "
                                          "Visszavonható: Ctrl+Z.").arg(lines).arg(feherLines));
        click(center(visual("mergeCancel")));
        QTRY_VERIFY(popupGone("mergeDialog"));
        QVERIFY(!m_vm->canUndo());
        QCOMPARE(m_vm->speakerCount(), speakers);
        QTRY_VERIFY(m_tab->hasActiveFocus());

        // 3) The same from the "merge" list, now confirmed: merges in one step.
        click(center(nameOf(0)));
        QTRY_VERIFY(popupOpen("linePopover"));
        click(center(visual("scopeSpeaker")));
        QVERIFY(choice("speakerChoice", QStringLiteral("Fehér Ádám")));
        click(center(choice("speakerChoice", QStringLiteral("Fehér Ádám"))));
        QTRY_VERIFY(popupOpen("mergeDialog"));
        QVERIFY(!m_vm->canUndo());
        click(center(visual("mergeAccept")));
        QTRY_VERIFY(popupGone("mergeDialog"));
        QCOMPARE(m_vm->speakerCount(), speakers - 1);
        QCOMPARE(speakerOf(0), feher);
        QCOMPARE(linesOf(feher), feherLines + lines);
        QTRY_VERIFY(barShown());
        QCOMPARE(barText(), QStringLiteral("Kovács Lilla mind az %1 sora átkerült ide: Fehér Ádám").arg(lines));
        undoKey();   // one step
        QCOMPARE(m_vm->speakerCount(), speakers);
        QCOMPARE(linesOf(key), lines);
        QCOMPARE(linesOf(feher), feherLines);
        QVERIFY(!m_vm->canUndo());
    }

    // The speaker-level places (rail header avatar, map name) open the WHOLE speaker's "Why
    // them?" panel with no scope picker, with "Listen" and reasons.
    void railAvatar_and_overviewName_openWholeSpeaker()
    {
        load();
        m_vm->setRailVisible(true);
        pump();
        const QList<QQuickItem*> heads = visuals("laneHead");
        QCOMPARE(int(heads.size()), int(m_vm->lanes().size()));
        click(center(heads.at(1)));
        QTRY_VERIFY(popupOpen("speakerWhyPopover"));
        QCOMPARE(why()->property("speakerKey").toString(), laneKey(1));
        QCOMPARE(m_tab->property("speakerPopoverRow").toInt(), -1);
        QVERIFY(!visual("scopeLine"));                      // nothing to choose: the whole speaker
        QVERIFY(visual("listenButton"));
        QCOMPARE(visual("popoverTitle")->property("text").toString(), QStringLiteral("Fehér Ádám"));
        QTRY_VERIFY(visual("whyBlock"));                    // WHY THEM? the reasons, with a fix link
        QVERIFY(!visuals("whyReason").isEmpty());
        QVERIFY(visual("notPresentLink"));
        // The merge list opens on request.
        QVERIFY(!visual("mergeList"));
        click(center(visual("mergeLink")));
        QTRY_VERIFY(visual("mergeList"));
        QVERIFY(!visuals("speakerChoice").isEmpty());
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("speakerWhyPopover"));

        // The map dock's name.
        const QList<QQuickItem*> names = visuals("overviewName");
        QCOMPARE(int(names.size()), int(m_vm->overview().size()));
        const QString key = m_vm->overview().at(2).toMap().value(QStringLiteral("key")).toString();
        const int lines = linesOf(key);
        const QString oldName = m_vm->speakerInfo(key).value(QStringLiteral("name")).toString();
        click(names.at(2)->mapToScene(QPointF(12, names.at(2)->height() / 2)).toPoint());
        QTRY_VERIFY(popupOpen("speakerWhyPopover"));
        QCOMPARE(why()->property("speakerKey").toString(), key);
        QVERIFY(!visual("scopeLine"));
        // "New person...": the person picker; the whole speaker goes to the chosen name (not a
        // person in the meeting: without confirmation).
        click(center(visual("newPersonLink")));
        QTRY_VERIFY(popupOpen("personPicker"));
        QTRY_VERIFY(popupGone("speakerWhyPopover"));
        QTest::mouseMove(m_window.get(), QPoint(2, 2));     // the picker rows highlight on hover too
        type(QStringLiteral("balogh"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(!popupOpen("personPicker"));
        QVERIFY(!popupOpen("mergeDialog"));
        QCOMPARE(m_vm->speakerInfo(key).value(QStringLiteral("name")).toString(), QStringLiteral("Balogh Kata"));
        QCOMPARE(linesOf(key), lines);
        QTRY_VERIFY(barShown());
        QVERIFY(barText().startsWith(oldName + QStringLiteral(" mind a")));
        undoKey();
        QCOMPARE(m_vm->speakerInfo(key).value(QStringLiteral("name")).toString(), oldName);
    }

    // "Was not there": the speaker's lines become anonymous, one undo step.
    void speakerWhy_notPresent_makesLinesAnonymous()
    {
        load();
        m_vm->setRailVisible(true);
        pump();
        const QString key = laneKey(1);
        const int lines = linesOf(key);
        click(center(visuals("laneHead").at(1)));
        QTRY_VERIFY(popupOpen("speakerWhyPopover"));
        QVERIFY(visual("fixVoiceprints"));                  // it has a voiceprint: the wrong-recognition marker
        click(center(visual("fixVoiceprints")));            // do not touch the voiceprint now
        click(center(visual("notPresentLink")));
        QTRY_VERIFY(popupGone("speakerWhyPopover"));
        QVERIFY(m_vm->speakerInfo(key).value(QStringLiteral("anonymous")).toBool());
        QCOMPARE(linesOf(key), lines);
        QCOMPARE(printCount(QStringLiteral("Fehér Ádám")), 1);
        QTRY_VERIFY(barShown());
        undoKey();
        QCOMPARE(m_vm->speakerInfo(key).value(QStringLiteral("name")).toString(), QStringLiteral("Fehér Ádám"));
        QVERIFY(!m_vm->canUndo());
    }

    // "Which track does it speak on?": manual track assignment from the panel; one step, undoable.
    void speakerWhy_trackAssignment()
    {
        load(QStringLiteral("v3"));
        m_vm->setRailVisible(true);
        pump();
        const QString key = laneKey(1);
        QCOMPARE(m_vm->speakerInfo(key).value(QStringLiteral("name")).toString(), QStringLiteral("Fehér Gábor"));
        click(center(visuals("laneHead").at(1)));
        QTRY_VERIFY(popupOpen("speakerWhyPopover"));
        QTRY_VERIFY(visual("tracksBlock"));
        const QList<QQuickItem*> chips = visuals("trackChip");
        QCOMPARE(int(chips.size()), 2);
        QVERIFY(m_vm->editor()->trackAssignment(key).isEmpty());
        click(center(chips.at(1)));                         // "Call audio"
        QCOMPARE(m_vm->editor()->trackAssignment(key), QStringList{QStringLiteral("loopback")});
        QTRY_VERIFY(visuals("trackChip").at(1)->property("checked").toBool());
        click(center(visuals("trackChip").at(1)));          // again: clears the constraint
        QVERIFY(m_vm->editor()->trackAssignment(key).isEmpty());
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("speakerWhyPopover"));
        undoKey();
        QCOMPARE(m_vm->editor()->trackAssignment(key), QStringList{QStringLiteral("loopback")});
        undoKey();
        QVERIFY(m_vm->editor()->trackAssignment(key).isEmpty());
    }

    // The track chips are switches: several tracks can be marked, each click is one undo step.
    void speakerWhy_multiTrackToggle()
    {
        load(QStringLiteral("v3"));
        m_vm->setRailVisible(true);
        pump();
        const QString key = laneKey(1);
        click(center(visuals("laneHead").at(1)));
        QTRY_VERIFY(popupOpen("speakerWhyPopover"));
        QTRY_VERIFY(visual("tracksBlock"));
        click(center(visuals("trackChip").at(1)));          // call
        QCOMPARE(m_vm->editor()->trackAssignment(key), QStringList{QStringLiteral("loopback")});
        click(center(visuals("trackChip").at(0)));          // + microphone: both
        QCOMPARE(m_vm->editor()->trackAssignment(key),
                 (QStringList{QStringLiteral("mic"), QStringLiteral("loopback")}));
        QTRY_VERIFY(visuals("trackChip").at(0)->property("checked").toBool());
        QVERIFY(visuals("trackChip").at(1)->property("checked").toBool());
        click(center(visuals("trackChip").at(1)));          // - call
        QCOMPARE(m_vm->editor()->trackAssignment(key), QStringList{QStringLiteral("mic")});
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("speakerWhyPopover"));
        undoKey();
        QCOMPARE(m_vm->editor()->trackAssignment(key),
                 (QStringList{QStringLiteral("mic"), QStringLiteral("loopback")}));
        undoKey();
        QCOMPARE(m_vm->editor()->trackAssignment(key), QStringList{QStringLiteral("loopback")});
    }

    // Quick "+ #Nordvik" in the "Why them?" panel's tag row and on the candidate rows: one click
    // applies it, an undoable toast comes (undoKey "tags"), the offer disappears; the candidate is not chosen.
    void speakerWhy_quickTag()
    {
        load(QStringLiteral("v3"));
        m_vm->setRailVisible(true);
        pump();
        const QString eszter = keyOfName(QStringLiteral("Molnár Eszter"));
        QVERIFY(!eszter.isEmpty());
        QMetaObject::invokeMethod(m_tab, "openSpeakerPopover", Q_ARG(QVariant, eszter),
                                  Q_ARG(QVariant, QVariant::fromValue<QObject*>(m_tab)), Q_ARG(QVariant, false));
        QTRY_VERIFY(popupOpen("speakerWhyPopover"));
        QTRY_VERIFY(visual("quickTagLink"));
        QCOMPARE(textOf("quickTagLink"), QStringLiteral("+ #Nordvik ráteszem"));
        QVERIFY(visual("fixLink"));                         // the "Tags" link stays
        click(center(visual("quickTagLink")));
        QVERIFY(m_vm->editor()->personTagIds(m_vm->editor()->personChoice(QStringLiteral("Molnár Eszter")).id).contains(QStringLiteral("nordvik")));
        QVERIFY(m_shell->property("toasts").toString().contains(QStringLiteral("Molnár Eszter címkéje lett: #Nordvik")));
        QTRY_VERIFY(!visual("quickTagLink"));
        QVERIFY(popupOpen("speakerWhyPopover"));            // the panel stays open
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("speakerWhyPopover"));
    }

    // The tag quick-add on the line panel: the candidate's chip applies the tag, it does NOT move the line.
    void linePopover_quickTagChip()
    {
        load(QStringLiteral("v3"));
        reveal(2);
        const QString before = speakerOf(2);
        click(center(nameOf(2)));
        QTRY_VERIFY(popupOpen("linePopover"));
        type(QStringLiteral("moln"));
        QQuickItem* row = nullptr;
        QTRY_VERIFY((row = choice("speakerChoice", QStringLiteral("Molnár Eszter"))));
        QQuickItem* chip = row->findChild<QQuickItem*>(QStringLiteral("quickTagChip"));
        QVERIFY(chip && chip->isVisible());
        QCOMPARE(chip->property("width").toReal() > 0, true);
        click(center(chip));
        QVERIFY(m_vm->editor()->personTagIds(m_vm->editor()->personChoice(QStringLiteral("Molnár Eszter")).id).contains(QStringLiteral("nordvik")));
        QCOMPARE(speakerOf(2), before);
        QVERIFY(popupOpen("linePopover"));
        QVERIFY(m_shell->property("toasts").toString().contains(QStringLiteral("#Nordvik")));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("linePopover"));
    }

    // At the bottom of the line panel a quiet "Voiceprint sample from this line (N s)" row; a click makes a sample.
    void linePopover_sampleRow()
    {
        load(QStringLiteral("v3"));
        reveal(0);
        QQuickItem* name = nameOf(0);
        QVERIFY(name);
        click(center(name));
        QTRY_VERIFY(popupOpen("linePopover"));
        QTRY_VERIFY(visual("lineSampleRow"));
        const QString text = textOf("lineSampleLink");
        QVERIFY2(text.contains(QStringLiteral("anglenyomat-minta ebből a sorból (")), qPrintable(text));
        const QString person = m_vm->speakerInfo(speakerOf(0)).value(QStringLiteral("personName")).toString();
        const int before = printCount(person);
        // In the whole speaker's scope the line is not shown (the voiceprint panel lives there).
        click(center(visual("scopeSpeaker")));
        QTRY_VERIFY(!visual("lineSampleRow"));
        click(center(visual("scopeLine")));
        QTRY_VERIFY(visual("lineSampleRow"));
        click(center(visual("lineSampleRow")));
        QTRY_VERIFY(popupGone("linePopover"));
        QTRY_COMPARE(printCount(person), before + 1);
        QVERIFY(m_shell->property("toasts").toString().contains(QStringLiteral("Hanglenyomat-minta készült")));
    }

    // The accent marks of the lines similar to the new person on the map dock (MapDock.similarMarks).
    void mapDock_similarMarks()
    {
        load(QStringLiteral("v3"));
        QVERIFY(visuals("mapSimilarMark").isEmpty());
        m_vm->applyDemoState(QStringLiteral("newPersonSimilar"));
        QTRY_VERIFY_WITH_TIMEOUT(!m_vm->similarMarks().isEmpty(), 15000);
        QTRY_COMPARE(int(visuals("mapSimilarMark").size()), int(m_vm->similarMarks().size() / 2));
        m_vm->setSimilarShown(false);
        QTRY_VERIFY(visuals("mapSimilarMark").isEmpty());
    }

    // The bar's "Similar N lines too" and "Show me" actions (with the rail hidden, starting from the name).
    void changeBar_similarLines_oneUndoStep()
    {
        load();
        QVERIFY(!m_vm->railVisible());
        reveal(6);
        // Line 6 is raw "Remote 1", by voice Fehér Ádám: clicking the name moves it to them.
        const QString source = speakerOf(6);
        const QString feher = keyOfName(QStringLiteral("Fehér Ádám"));
        const int sourceLines = linesOf(source);
        click(center(nameOf(6)));
        QTRY_VERIFY(popupOpen("linePopover"));
        type(QStringLiteral("feher"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popupGone("linePopover"));
        QCOMPARE(speakerOf(6), feher);
        QCOMPARE(linesOf(source), sourceLines - 1);

        QTRY_VERIFY(barShown());
        QVERIFY(m_vm->suggestionActive());
        const int similar = m_vm->suggestionCount();
        QVERIFY(similar > 0);
        QVERIFY(visual("changeSimilar"));
        QVERIFY(visual("changeShow"));
        QVERIFY(visual("changeRest"));

        // "Show me": the rail turns on, the suggested lines are highlighted; again: hides it.
        click(center(visual("changeShow")));
        QVERIFY(m_vm->suggestionShown());
        QVERIFY(m_vm->railVisible());
        QVERIFY(barShown());
        click(center(visual("changeShow")));
        QVERIFY(!m_vm->suggestionShown());

        // "Similar N lines too": exactly the suggested lines go, in a single further step.
        click(center(visual("changeSimilar")));
        QCOMPARE(linesOf(source), sourceLines - 1 - similar);
        QVERIFY(!m_vm->suggestionActive());
        QTRY_VERIFY(barShown());
        QCOMPARE(barText(), QStringLiteral("%1 sor átkerült ide: Fehér Ádám").arg(similar));
        QVERIFY(!visual("changeSimilar"));
        click(center(visual("changeUndo")));                // only the similar lines come back
        QCOMPARE(linesOf(source), sourceLines - 1);
        QCOMPARE(speakerOf(6), feher);
        undoKey();
        QCOMPARE(linesOf(source), sourceLines);
        QVERIFY(!m_vm->canUndo());
    }

    // The bar's "<Source> all N lines" continuation: asks for confirmation, then the source's
    // REMAINING lines go to the target in a single further step.
    void changeBar_restOfSource_confirmsAndIsOneStep()
    {
        load();
        m_vm->setRailVisible(true);
        pump();
        reveal(6);
        const QString source = speakerOf(6);
        const QString sourceName = cell(6, Role::SpeakerNameRole).toString();
        const QString feher = keyOfName(QStringLiteral("Fehér Ádám"));
        const int sourceLines = linesOf(source);
        const int feherLines = linesOf(feher);
        const int speakers = m_vm->speakerCount();

        click(railPoint(6, cell(2, Role::LaneRole).toInt()));       // a click into Fehér Ádám's column
        QCOMPARE(speakerOf(6), feher);
        QTRY_VERIFY(barShown());
        QCOMPARE(m_vm->changeRestCount(), sourceLines - 1);
        QQuickItem* rest = visual("changeRest");
        QVERIFY(rest);
        QCOMPARE(rest->property("text").toString(),
                 QStringLiteral("%1 mind a %2 sora").arg(sourceName).arg(sourceLines - 1));

        // "Cancel": nothing changes, the bar stays.
        click(center(rest));
        QTRY_VERIFY(popupOpen("mergeDialog"));
        QCOMPARE(visual("mergeText")->property("text").toString(),
                 QStringLiteral("%1 %2 sora összeolvad ezzel: Fehér Ádám (%3 sor). Visszavonható: Ctrl+Z.")
                     .arg(sourceName).arg(sourceLines - 1).arg(feherLines + 1));
        click(center(visual("mergeCancel")));
        QTRY_VERIFY(popupGone("mergeDialog"));
        QCOMPARE(linesOf(source), sourceLines - 1);
        QVERIFY(barShown());

        click(center(visual("changeRest")));
        QTRY_VERIFY(popupOpen("mergeDialog"));
        click(center(visual("mergeAccept")));
        QTRY_VERIFY(popupGone("mergeDialog"));
        QCOMPARE(m_vm->speakerCount(), speakers - 1);
        QCOMPARE(linesOf(feher), feherLines + sourceLines);
        QTRY_VERIFY(barShown());
        QCOMPARE(barText(), QStringLiteral("%1 mind a %2 sora átkerült ide: Fehér Ádám")
                                .arg(sourceName).arg(sourceLines - 1));
        QVERIFY(!visual("changeRest"));

        // One undo: only the bulk step; the line fixed first stays in place.
        undoKey();
        QCOMPARE(m_vm->speakerCount(), speakers);
        QCOMPARE(linesOf(source), sourceLines - 1);
        QCOMPARE(speakerOf(6), feher);
        undoKey();
        QCOMPARE(speakerOf(6), source);
        QVERIFY(!m_vm->canUndo());
    }

    // The "Just now" row sits in a FIXED place: it does not disappear over time; the x closes
    // only the offers, the next edit replaces its text, and undoability is independent of that.
    void changeBar_staysCloseAndReplace()
    {
        load();
        QVERIFY(!imentShown());                             // Read: no decision, no row
        m_tab->setProperty("fixMode", true);
        pump();
        QVERIFY(imentShown());                              // Edit: always there (with an empty state)
        QVERIFY(m_vm->lastDecision().isEmpty());
        QVERIFY(!changeBar()->property("timeoutMs").isValid());
        auto barBottom = [this] {
            auto* bar = static_cast<QQuickItem*>(changeBar());
            return bar->mapToScene(QPointF(0, bar->height())).y();
        };
        const qreal barY = barBottom();
        click(railPoint(3, 1));
        QTRY_VERIFY(barShown());
        QVERIFY(!m_vm->suggestionActive());
        QCOMPARE(textOf("changeText"), barText());
        const int serial = m_vm->changeSerial();
        click(railPoint(1, 1));                             // the next edit: new text
        QVERIFY(barShown());
        QVERIFY(m_vm->changeSerial() > serial);
        pump(1500);                                         // no timer: it stays
        QVERIFY(barShown());
        QCOMPARE(barBottom(), barY);                        // its bottom is fixed (a warning grows upward)

        click(center(visual("changeClose")));
        QVERIFY(!m_vm->changeActive());
        QVERIFY(imentShown());                              // the "Just now" row stays, with the last decision
        QVERIFY(m_vm->lastDecision().value(QStringLiteral("text")).toString().contains(QStringLiteral("átkerült")));
        QCOMPARE(textOf("changeText"), m_vm->lastDecision().value(QStringLiteral("text")).toString());
        QVERIFY(m_vm->canUndo());

        // Adding a participant (another edit) ends the old notice; the row stays.
        click(railPoint(3, 2));
        QVERIFY(barShown());
        m_vm->addParticipant(QStringLiteral("Zita"));
        QVERIFY(!barShown());
        QVERIFY(imentShown());
    }

    // The To review view's "One by one" rows: the fixed line stays in place ("fixed"), so the
    // next click does not land on another line; it goes away when the group is collapsed.
    // "Other..." on the focus card: the same per-line panel as from the name; the decided line
    // stays in place on the work list, and the card steps to the next open line.
    void reviewView_decidedLineStays()
    {
        load();
        openReview();
        const QString id = focusLine();
        QVERIFY(!id.isEmpty());
        ReviewQueueModel* q = m_vm->reviewQueue();
        const int row = q->indexOf(id);
        const int count = q->count();
        const QString from = m_vm->lineInfo(id).value(QStringLiteral("speakerKey")).toString();
        const int fromLines = linesOf(from);

        click(center(visual("reviewFocusOther")));
        QTRY_VERIFY(popupOpen("linePopover"));
        QCOMPARE(popover()->property("scope").toString(), QStringLiteral("line"));
        QCOMPARE(popover()->property("utteranceId").toString(), id);
        QTest::keyClick(m_window.get(), Qt::Key_Down);      // the first suggested candidate
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popupGone("linePopover"));
        QVERIFY(m_vm->lineInfo(id).value(QStringLiteral("speakerKey")).toString() != from);
        QCOMPARE(linesOf(from), fromLines - 1);             // only this one line
        QTRY_VERIFY(barShown());

        // The line is in the same place on the work list, decided; the card is already on the next one.
        QTRY_VERIFY(!m_vm->reviewRunning());
        pump(80);
        QCOMPARE(q->indexOf(id), row);
        QVERIFY(q->count() >= count);
        QCOMPARE(q->get(row).value(QStringLiteral("state")).toString(), QStringLiteral("decided"));
        QVERIFY(focusLine() != id);
    }

    void addParticipant_viaPicker()
    {
        load();
        m_vm->setRailVisible(true);
        pump();
        QQuickItem* add = m_window->findChild<QQuickItem*>(QStringLiteral("addParticipant"));
        QVERIFY(add);
        const int lanes = int(m_vm->lanes().size());
        click(center(add));
        QTRY_VERIFY(popupOpen("personPicker"));

        // A new name typed, then "New person" row, then Enter: a new (empty) column.
        for (const QChar c : QStringLiteral("Zalan"))
            QTest::keyClick(m_window.get(), c.toLatin1(), c.isUpper() ? Qt::ShiftModifier : Qt::NoModifier);
        pump();
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(!popupOpen("personPicker"));
        QCOMPARE(int(m_vm->lanes().size()), lanes + 1);
        const QVariantMap added = m_vm->lanes().last().toMap();
        QCOMPARE(added.value(QStringLiteral("name")).toString(), QStringLiteral("Zalan"));
        QCOMPARE(added.value(QStringLiteral("utteranceCount")).toInt(), 0);

        // Moving one line into the new column by a click.
        click(railPoint(1, lanes));
        QCOMPARE(cell(1, Role::SpeakerNameRole).toString(), QStringLiteral("Zalan"));
    }

    void reviewView_listenConfirmAndStep()
    {
        load();
        openReview();
        const QString id = focusLine();
        const QVariantMap info = m_vm->lineInfo(id);
        // The card's play button: the line to its boundaries (+ 0.3 s lead-out). The focus view's
        // pending auto-play (Qt.callLater on opening) lands first, so the log has only the click.
        pump();
        m_player->setProperty("log", QString());
        click(center(visual("reviewFocusPlay")));
        QCOMPARE(m_player->property("log").toString(),
                 QStringLiteral("range:%1-%2;").arg(info.value(QStringLiteral("startMs")).toInt())
                                              .arg(info.value(QStringLiteral("endMs")).toInt() + 200));

        // "Good as is": the line is confirmed, the card moves on.
        const int before = m_vm->uncertainCount();
        click(center(visual("reviewFocusConfirm")));
        QCOMPARE(m_vm->uncertainCount(), before - 1);
        QVERIFY(m_vm->editor()->utterance(id).confirmed);
        QVERIFY(m_vm->canUndo());
        QVERIFY(focusLine() != id);

        // B: the next open line; Shift+B: the previous line.
        QQuickItem* view = visual("reviewView");
        QVERIFY(view);
        const QString current = view->property("currentUtterance").toString();
        QTest::keyClick(m_window.get(), Qt::Key_B);
        pump();
        const QString next = view->property("currentUtterance").toString();
        QVERIFY(!next.isEmpty());
        QVERIFY(next != current);
        QTest::keyClick(m_window.get(), Qt::Key_B, Qt::ShiftModifier);
        pump();
        QVERIFY(view->property("currentUtterance").toString() != next);
    }

    void speakerPopover_strayEnterDoesNothing()
    {
        load();
        QQuickItem* name = rowItem(0)->findChild<QQuickItem*>(QStringLiteral("speakerName"));
        QVERIFY(name);
        click(center(name));
        QTRY_VERIFY(popupOpen("linePopover"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTest::keyClick(m_window.get(), Qt::Key_Enter);
        pump();
        QVERIFY(popupOpen("linePopover"));
        QCOMPARE(cell(0, Role::SpeakerNameRole).toString(), QStringLiteral("Kovács Lilla"));
        QVERIFY(!m_vm->canUndo());

        // Down arrow highlights the first person, then Enter chooses them.
        QTest::keyClick(m_window.get(), Qt::Key_Down);
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popupGone("linePopover"));
        QVERIFY(cell(0, Role::SpeakerNameRole).toString() != QStringLiteral("Kovács Lilla"));
        QVERIFY(m_vm->canUndo());
        m_vm->undo();

        // The whole speaker's panel (opened from the rail header): Enter does nothing there either.
        m_vm->setRailVisible(true);
        pump();
        click(center(visuals("laneHead").at(0)));
        QTRY_VERIFY(popupOpen("speakerWhyPopover"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTest::keyClick(m_window.get(), Qt::Key_Enter);
        pump();
        QVERIFY(popupOpen("speakerWhyPopover"));
        QVERIFY(!popupOpen("mergeDialog"));
        QVERIFY(!m_vm->canUndo());
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("speakerWhyPopover"));
    }

    void personPicker_strayEnterDoesNothing()
    {
        load();
        m_vm->setRailVisible(true);
        pump();
        QQuickItem* add = m_window->findChild<QQuickItem*>(QStringLiteral("addParticipant"));
        QVERIFY(add);
        const int lanes = int(m_vm->lanes().size());
        click(center(add));
        QTRY_VERIFY(popupOpen("personPicker"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        pump();
        QVERIFY(popupOpen("personPicker"));
        QCOMPARE(int(m_vm->lanes().size()), lanes);
        QVERIFY(!m_vm->canUndo());
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(!popupOpen("personPicker"));
    }

    // For a line without a name row (a continuation), the context menu's "Said by someone else..."
    // item opens the same per-line panel.
    void contextMenu_fixLineWithoutNameRow()
    {
        load();
        QVERIFY(!cell(4, Role::HeadRole).toBool());         // it has no name row: nothing to click
        const QString from = speakerOf(4);
        const int lines = linesOf(from);
        QTest::mouseClick(m_window.get(), Qt::RightButton, Qt::NoModifier, textPoint(4));
        QTRY_VERIFY(popupOpen("rowMenu"));
        QQuickItem* entry = findByText(m_window->contentItem(), QStringLiteral("Más mondta… (ez a sor)"));
        QVERIFY(entry);
        click(center(entry));
        QTRY_VERIFY(popupOpen("linePopover"));
        QCOMPARE(popover()->property("scope").toString(), QStringLiteral("line"));
        QCOMPARE(popover()->property("utteranceId").toString(),
                 m_vm->rowInfo(4).value(QStringLiteral("utteranceId")).toString());
        type(QStringLiteral("varga"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popupGone("linePopover"));
        QCOMPARE(cell(4, Role::SpeakerNameRole).toString(), QStringLiteral("Varga Nóra"));
        QCOMPARE(linesOf(from), lines - 1);
        QVERIFY(speakerOf(3) == from && speakerOf(5) != speakerOf(4));
        undoKey();
        QCOMPARE(speakerOf(4), from);
    }

    // "Listen": on the panel opened from a line the line itself plays; on the speaker-level panel
    // a typical line (the panel stays open).
    void speakerPopover_listenPlaysLineOrSample()
    {
        load();
        click(center(nameOf(0)));
        QTRY_VERIFY(popupOpen("linePopover"));
        QQuickItem* listen = visual("listenButton");
        QVERIFY(listen);
        click(center(listen));
        QCOMPARE(m_player->property("log").toString(),
                 QStringLiteral("range:%1-%2;").arg(m_vm->rowStartMs(0)).arg(m_vm->rowEndMs(0)));
        // The neighbor boundaries for the player (the lead-in / tail limit): there is no line before
        // the first line, the next line's start is at least the line's end.
        {
            const QVariantList bounds = m_player->property("bounds").toList();
            QCOMPARE(bounds.size(), 2);
            const QVariantMap expected = m_vm->playBounds(m_vm->rowStartMs(0), m_vm->rowEndMs(0));
            QCOMPARE(bounds.at(0).toInt(), expected.value(QStringLiteral("minStartMs")).toInt());
            QCOMPARE(bounds.at(1).toInt(), expected.value(QStringLiteral("maxEndMs")).toInt());
            QCOMPARE(bounds.at(0).toInt(), -1);
            QVERIFY(bounds.at(1).toInt() >= m_vm->rowEndMs(0));
        }
        QVERIFY(popupOpen("linePopover"));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("linePopover"));
        m_player->setProperty("log", QString());

        m_vm->setRailVisible(true);
        pump();
        click(center(visuals("laneHead").at(0)));
        QTRY_VERIFY(popupOpen("speakerWhyPopover"));
        listen = visual("listenButton");
        QVERIFY(listen);
        const QVariantMap sample = m_vm->speakerSample(laneKey(0));
        QVERIFY(sample.value(QStringLiteral("ok")).toBool());
        const int start = sample.value(QStringLiteral("startMs")).toInt();
        const int end = sample.value(QStringLiteral("endMs")).toInt();
        QVERIFY(end > start && end - start <= 12000);
        QCOMPARE(m_vm->utterances().at(m_vm->utteranceForTime(start)).speakerKey, laneKey(0));
        click(center(listen));
        QCOMPARE(m_player->property("log").toString(), QStringLiteral("range:%1-%2;").arg(start).arg(end));
        QVERIFY(popupOpen("speakerWhyPopover"));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
    }

    // Copy: Ctrl+C puts the selected lines on the clipboard (name + timestamp + text); right
    // click gives a context menu; click selection and timestamp jump are unchanged.
    void copy_selectionAndContextMenu()
    {
        load();
        QGuiApplication::clipboard()->clear();
        click(textPoint(1));
        QCOMPARE(m_vm->selectedCount(), 1);
        QTest::keyClick(m_window.get(), Qt::Key_C, Qt::ControlModifier);
        const QString one = QGuiApplication::clipboard()->text();
        QVERIFY(one.contains(cell(1, Role::TextRole).toString()));
        QVERIFY(one.startsWith(cell(1, Role::SpeakerNameRole).toString()));
        QVERIFY(one.contains(cell(1, Role::TimeLabelRole).toString()));

        // Several lines: a header line at a speaker change, the lines' text in order.
        click(textPoint(3), Qt::ShiftModifier);
        QCOMPARE(m_vm->selectedCount(), 3);
        QTest::keyClick(m_window.get(), Qt::Key_C, Qt::ControlModifier);
        const QString many = QGuiApplication::clipboard()->text();
        for (int r = 1; r <= 3; ++r) QVERIFY(many.contains(cell(r, Role::TextRole).toString()));
        QVERIFY(many.indexOf(cell(1, Role::TextRole).toString()) < many.indexOf(cell(3, Role::TextRole).toString()));
        QCOMPARE(m_vm->selectionText(), many);

        // Right click: context menu; the selection does not change because of it.
        QTest::mouseClick(m_window.get(), Qt::RightButton, Qt::NoModifier, textPoint(2));
        QTRY_VERIFY(popupOpen("rowMenu"));
        QCOMPARE(m_vm->selectedCount(), 3);
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(!popupOpen("rowMenu"));
        QTRY_VERIFY(m_tab->hasActiveFocus());                // focus returns after the menu closes

        // Ctrl+A: every line; the whole transcript can be copied.
        QTest::keyClick(m_window.get(), Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(m_vm->selectedCount(), m_vm->utteranceCount());
        QCOMPARE(m_vm->copyAll(), m_vm->utteranceCount());
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QCOMPARE(m_vm->selectedCount(), 0);
    }

    QString viewMode() const { return m_tab->property("viewMode").toString(); }
    void setViewMode(const char* mode)
    {
        QMetaObject::invokeMethod(m_tab, "setViewMode", Q_ARG(QVariant, QString::fromLatin1(mode)));
        pump();
    }

    // "Next to review": the B key switches to the To review view and steps through the groups'
    // lines there (Shift+B backwards); typing into a text field, the letter does not step.
    void nextUncertain_switchesToReview()
    {
        load();
        QVERIFY(QTest::qWaitFor([this] { return !m_vm->reviewRunning() && m_vm->reviewCount() > 0; }, 10000));
        setViewMode("read");
        QCOMPARE(viewMode(), QStringLiteral("read"));
        QTest::keyClick(m_window.get(), Qt::Key_B);
        pump(60);
        QCOMPARE(viewMode(), QStringLiteral("review"));
        QVERIFY(m_vm->uncertainOnly());
        QVERIFY(m_vm->railVisible());                        // the Edit rail
        QQuickItem* view = visual("reviewView");
        QVERIFY(view);
        QTRY_VERIFY(!view->property("currentUtterance").toString().isEmpty());
        const QString first = view->property("currentUtterance").toString();
        QTest::keyClick(m_window.get(), Qt::Key_B);
        pump();
        const QString next = view->property("currentUtterance").toString();
        QVERIFY(next != first);
        QTest::keyClick(m_window.get(), Qt::Key_B, Qt::ShiftModifier);
        pump();
        QCOMPARE(view->property("currentUtterance").toString(), first);

        // In the search field the letter "b" is text, not a jump; Ctrl+Z there belongs to the field.
        const int before = m_vm->currentRow();
        QMetaObject::invokeMethod(m_tab, "openSearch");
        pump();
        QTest::keyClick(m_window.get(), Qt::Key_B);
        QTest::keyClick(m_window.get(), Qt::Key_1);
        pump();
        QCOMPARE(m_vm->searchQuery(), QStringLiteral("b1"));
        QCOMPARE(m_vm->currentRow(), before);
        QVERIFY(!m_vm->canUndo());
    }

    // The next / previous LINE (not gap) in the list.
    int lineAfter(int row, int dir) const
    {
        for (int r = row + dir; r >= 0 && r < m_vm->rows()->rowCount(); r += dir)
            if (!m_vm->rowUtteranceId(r).isEmpty()) return r;
        return -1;
    }
    QString playerLog() const { return m_player->property("log").toString(); }

    // Line stepping while stopped: the arrows (Left/Up previous, Right/Down next) and the map
    // dock's buttons only select and show; no seeking, no playback.
    void lineStep_notPlaying()
    {
        load();
        click(textPoint(2));
        QCOMPARE(m_vm->currentRow(), 2);
        m_player->setProperty("log", QString());
        const int next = lineAfter(2, 1);
        QTest::keyClick(m_window.get(), Qt::Key_Right);
        QCOMPARE(m_vm->currentRow(), next);
        QTest::keyClick(m_window.get(), Qt::Key_Left);
        QCOMPARE(m_vm->currentRow(), 2);
        QTest::keyClick(m_window.get(), Qt::Key_Down);
        QCOMPARE(m_vm->currentRow(), next);
        QTest::keyClick(m_window.get(), Qt::Key_Up);
        QCOMPARE(m_vm->currentRow(), 2);
        QCOMPARE(m_vm->selectedCount(), 1);

        QQuickItem* nextButton = visual("nextLineButton");
        QQuickItem* prevButton = visual("prevLineButton");
        QVERIFY(nextButton && prevButton);
        click(center(nextButton));
        QCOMPARE(m_vm->currentRow(), next);
        QVERIFY(m_tab->hasActiveFocus() || !nextButton->hasActiveFocus());
        click(center(prevButton));
        QCOMPARE(m_vm->currentRow(), 2);
        // Stopped: every step seeks to the line's start (one cursor), without playing.
        const QString s2 = QStringLiteral("seek:%1;").arg(m_vm->rowStartMs(2));
        const QString sn = QStringLiteral("seek:%1;").arg(m_vm->rowStartMs(next));
        QCOMPARE(playerLog(), sn + s2 + sn + s2 + sn + s2);
        QVERIFY(!m_player->property("playing").toBool());

        // The list does not scroll by itself on the arrows (the selected line stays visible).
        const qreal y = m_list->property("contentY").toReal();
        QTest::keyClick(m_window.get(), Qt::Key_Right);
        QTest::keyClick(m_window.get(), Qt::Key_Left);
        pump();
        QCOMPARE(m_list->property("contentY").toReal(), y);
    }

    // One cursor (stopped): selecting a line seeks to its start (a single seek) without playing,
    // and Space starts from there. If the position is already inside the line, there is no seek.
    void cursor_selectWhilePausedMovesPosition()
    {
        load();
        m_player->setProperty("log", QString());
        click(textPoint(3));
        QCOMPARE(m_vm->currentRow(), 3);
        const int start = m_vm->rowStartMs(3);
        QCOMPARE(playerLog(), QStringLiteral("seek:%1;").arg(start));
        QCOMPARE(m_player->property("positionMs").toInt(), start);
        QVERIFY(!m_player->property("playing").toBool());
        QCOMPARE(m_vm->currentRow(), 3);                     // no feedback loop

        m_player->setProperty("log", QString());
        click(textPoint(3));                                 // the same line: the position is inside it
        QVERIFY(!playerLog().contains(QStringLiteral("seek:")));

        // Space: plays from the selected line's start.
        m_player->setProperty("log", QString());
        QTest::keyClick(m_window.get(), Qt::Key_Space);
        QVERIFY(m_player->property("playing").toBool());
        QCOMPARE(m_player->property("positionMs").toInt(), start);
        QVERIFY(!playerLog().contains(QStringLiteral("seek:")));
    }

    // During playback the line under the player is the selected one as the position advances.
    void cursor_playingUpdatesCurrentRow()
    {
        load();
        m_player->setProperty("playing", true);
        const int a = lineAfter(0, 1);
        const int b = lineAfter(a, 1);
        m_player->setProperty("positionMs", m_vm->rowStartMs(a) + 5);
        QTRY_COMPARE(m_vm->currentRow(), a);
        m_player->setProperty("positionMs", m_vm->rowStartMs(b) + 5);
        QTRY_COMPARE(m_vm->currentRow(), b);
        QCOMPARE(m_vm->selectedCount(), 1);
        QVERIFY(!playerLog().contains(QStringLiteral("seek:")));   // following does not seek
    }

    // The map dock's jump selects the line under the position (stopped and during playback too).
    void cursor_dockSeekSelectsRow()
    {
        load();
        const int row = lineAfter(lineAfter(lineAfter(0, 1), 1), 1);
        const double fraction = m_vm->rowStartFraction(row) + 0.0001;
        QMetaObject::invokeMethod(m_tab, "seekToFraction", Q_ARG(QVariant, fraction));
        pump();
        QCOMPARE(m_vm->currentRow(), m_vm->rowForTime(m_player->property("positionMs").toInt()));
        QVERIFY(!m_player->property("playing").toBool());
        const int first = m_vm->currentRow();
        QVERIFY(first >= 0);

        m_player->setProperty("playing", true);
        const int later = lineAfter(lineAfter(first, 1), 1);
        QMetaObject::invokeMethod(m_tab, "seekToFraction", Q_ARG(QVariant, m_vm->rowStartFraction(later) + 0.0001));
        pump();
        QCOMPARE(m_vm->currentRow(), m_vm->rowForTime(m_player->property("positionMs").toInt()));
        QVERIFY(m_vm->currentRow() != first);
        QVERIFY(m_player->property("playing").toBool());
    }

    // Clicking another line during playback: playback jumps there and keeps playing.
    void cursor_rowClickWhilePlayingSeeks()
    {
        load();
        m_player->setProperty("playing", true);
        m_player->setProperty("positionMs", m_vm->rowStartMs(1) + 5);
        QTRY_COMPARE(m_vm->currentRow(), 1);
        m_player->setProperty("log", QString());
        click(textPoint(3));
        QCOMPARE(m_vm->currentRow(), 3);
        QCOMPARE(playerLog(), QStringLiteral("seek:%1;").arg(m_vm->rowStartMs(3)));
        QVERIFY(m_player->property("playing").toBool());
        QTRY_COMPARE(m_vm->playingRow(), 3);
        QCOMPARE(m_vm->currentRow(), 3);
    }

    // During playback: steps from the line under the player, seeks to the target's start (a
    // plain seek, not playRange), and keeps playing.
    void lineStep_playing()
    {
        load();
        click(textPoint(1));
        QCOMPARE(m_vm->currentRow(), 1);
        const int playingRow = lineAfter(lineAfter(1, 1), 1);
        QVERIFY(playingRow > 1);
        m_player->setProperty("playing", true);
        m_player->setProperty("positionMs", m_vm->rowStartMs(playingRow) + 10);
        QTRY_COMPARE(m_vm->playingRow(), playingRow);
        m_player->setProperty("log", QString());

        const int next = lineAfter(playingRow, 1);
        QTest::keyClick(m_window.get(), Qt::Key_Right);
        QCOMPARE(playerLog(), QStringLiteral("seek:%1;").arg(m_vm->rowStartMs(next)));
        QVERIFY(m_player->property("playing").toBool());
        QCOMPARE(m_vm->currentRow(), next);
        QTRY_COMPARE(m_vm->playingRow(), next);

        // The dock's "Previous line" button: the start of the line before the one now playing.
        m_player->setProperty("log", QString());
        QQuickItem* prevButton = visual("prevLineButton");
        QVERIFY(prevButton);
        click(center(prevButton));
        QCOMPARE(playerLog(), QStringLiteral("seek:%1;").arg(m_vm->rowStartMs(playingRow)));
        QVERIFY(m_player->property("playing").toBool());
        QCOMPARE(m_vm->currentRow(), playingRow);
        QVERIFY(!playerLog().contains(QStringLiteral("range:")));
    }

    // In the To review view the arrows step through the groups' lines (like B).
    void lineStep_reviewView()
    {
        load();
        QVERIFY(QTest::qWaitFor([this] { return !m_vm->reviewRunning() && m_vm->reviewCount() > 1; }, 10000));
        setViewMode("review");
        m_tab->forceActiveFocus();
        QQuickItem* view = visual("reviewView");
        QVERIFY(view);
        QTRY_VERIFY(!view->property("currentUtterance").toString().isEmpty());
        const QString first = view->property("currentUtterance").toString();
        // In To review Down / Up are the work list's next / previous line (Right is "Later" here).
        QTest::keyClick(m_window.get(), Qt::Key_Down);
        pump();
        QVERIFY(view->property("currentUtterance").toString() != first);
        QTest::keyClick(m_window.get(), Qt::Key_Up);
        pump();
        QCOMPARE(view->property("currentUtterance").toString(), first);
        QCOMPARE(viewMode(), QStringLiteral("review"));
    }

    // Read, Edit, To review (N): the segment's three items; To review is part of Edit, and on
    // leaving it the mode before entering returns; Ctrl+E toggles Read <-> Edit (from To review: Read).
    void viewModes_segmentAndReturn()
    {
        load();
        QVERIFY(QTest::qWaitFor([this] { return !m_vm->reviewRunning() && m_vm->reviewCount() > 0; }, 10000));
        setViewMode("fix");
        QCOMPARE(viewMode(), QStringLiteral("fix"));
        QQuickItem* review = visual("modeReview");
        QVERIFY(review);
        QCOMPARE(textOf("reviewCount"), QString::number(m_vm->reviewCount()));
        QVERIFY(!visual("uncertainChip"));                   // in Edit only the segment
        click(center(review));
        QTRY_COMPARE(viewMode(), QStringLiteral("review"));
        QVERIFY(m_vm->uncertainOnly());
        QVERIFY(m_tab->property("fixMode").toBool());
        QTRY_VERIFY(visual("reviewView"));
        QVERIFY(!visual("uncertainChip"));
        click(center(visual("modeRead")));
        QTRY_COMPARE(viewMode(), QStringLiteral("read"));
        QVERIFY(!m_vm->uncertainOnly());
        QVERIFY(!m_vm->railVisible());
        QVERIFY(visual("uncertainChip"));                    // in Read the quiet "N lines to review"

        // Leaving not through the segment (e.g. a timestamp jump): the mode before entering returns.
        setViewMode("fix");
        m_vm->setUncertainOnly(true);
        pump();
        QCOMPARE(viewMode(), QStringLiteral("review"));
        m_vm->setUncertainOnly(false);
        pump();
        QCOMPARE(viewMode(), QStringLiteral("fix"));
        setViewMode("read");
        m_vm->setUncertainOnly(true);                        // e.g. the result of a recheck
        pump();
        QCOMPARE(viewMode(), QStringLiteral("review"));
        m_vm->setUncertainOnly(false);
        pump();
        QCOMPARE(viewMode(), QStringLiteral("read"));

        // Ctrl+E: Read <-> Edit; from To review into Read.
        QTest::keyClick(m_window.get(), Qt::Key_E, Qt::ControlModifier);
        QCOMPARE(viewMode(), QStringLiteral("fix"));
        setViewMode("review");
        QCOMPARE(viewMode(), QStringLiteral("review"));
        QTest::keyClick(m_window.get(), Qt::Key_E, Qt::ControlModifier);
        QCOMPARE(viewMode(), QStringLiteral("read"));
        QTest::keyClick(m_window.get(), Qt::Key_E, Qt::ControlModifier);
        QCOMPARE(viewMode(), QStringLiteral("fix"));
    }

    // 0 lines to review: the To review segment shows 0, and the view is the empty state
    // (caption + "Recheck speakers..."), not an empty list.
    void viewModes_reviewEmptyState()
    {
        load();
        QVERIFY(QTest::qWaitFor([this] { return !m_vm->reviewRunning() && m_vm->reviewCount() > 0; }, 10000));
        for (int round = 0; round < 5 && m_vm->reviewCount() > 0; ++round) {
            QStringList ids;
            for (const auto& v : m_vm->reviewViews()) ids += v.ids;
            for (const QString& id : std::as_const(ids)) m_vm->confirmUtterance(id);
            QTest::qWaitFor([this] { return !m_vm->reviewRunning(); }, 10000);
            pump(60);
        }
        QStringList kinds;
        for (const auto& v : m_vm->reviewViews()) kinds << QStringLiteral("%1:%2").arg(v.kind).arg(v.ids.size());
        QVERIFY2(m_vm->reviewCount() == 0, qPrintable(QStringLiteral("%1 maradt: %2").arg(m_vm->reviewCount()).arg(kinds.join(u' '))));
        pump();
        QCOMPARE(textOf("reviewCount"), QStringLiteral("0"));
        setViewMode("review");
        QCOMPARE(viewMode(), QStringLiteral("review"));
        QTRY_VERIFY(visual("reviewView"));
        QVERIFY(visual("reviewEmpty"));
        QVERIFY(findByText(visual("reviewEmpty"), QStringLiteral("Beszélők újraellenőrzése…")));
        QVERIFY(visual("emptyFilterRecheck"));
    }

    // Typing into a text field, the editor's keys (1-9, Space, Ctrl+Y) do not fire.
    void typingInSearchNeverTriggersEditorKeys()
    {
        load();
        m_vm->setRailVisible(true);
        click(textPoint(1));                                 // the cursor seeks to the line (one cursor)
        m_player->setProperty("log", QString());
        const QString speaker = speakerOf(1);
        QMetaObject::invokeMethod(m_tab, "openSearch");
        pump();
        QTest::keyClick(m_window.get(), Qt::Key_2);
        QTest::keyClick(m_window.get(), Qt::Key_Space);
        QTest::keyClick(m_window.get(), Qt::Key_1);
        pump();
        QCOMPARE(m_vm->searchQuery(), QStringLiteral("2 1"));
        QCOMPARE(speakerOf(1), speaker);                     // it did not move to another column
        QCOMPARE(m_player->property("log").toString(), QString());   // Space did not start playback
        QVERIFY(!m_vm->canUndo());
    }

    // Every row of the overview has a voiceprint marker: has / none / anonymous. A click opens the
    // voiceprint panel: state, usable material, creation, and undoing the just-created one.
    void overviewVoiceprint_indicatorAndPopover()
    {
        load();
        const QString lilla = keyOfName(QStringLiteral("Kovács Lilla"));
        const QString anon = keyOfName(QStringLiteral("Távoli 1"));
        QCOMPARE(int(visuals("overviewVoiceprint").size()), int(m_vm->overview().size()));
        QCOMPARE(markState(lilla), QStringLiteral("has"));
        QCOMPARE(markState(anon), QStringLiteral("anonymous"));
        // The marker does not shift the timeline: it sits inside the name column.
        QQuickItem* mark = markOf(lilla);
        QVERIFY(mark->x() + mark->width() <= 120);

        // For an anonymous speaker it opens nothing (the tooltip says why).
        click(center(markOf(anon)));
        QVERIFY(!popupOpen("voiceprintPopover"));

        // 1) One who has one: it says so, and a new sample can be made.
        click(center(mark));
        QTRY_VERIFY(popupOpen("voiceprintPopover"));
        QVERIFY(!popupOpen("speakerWhyPopover"));
        QCOMPARE(textOf("voiceprintName"), QStringLiteral("Kovács Lilla"));
        QCOMPARE(textOf("voiceprintState"), QStringLiteral("Van hanglenyomata"));
        QVERIFY2(textOf("voiceprintDetail").contains(QStringLiteral("hosszabb sora használható fel")),
                 qPrintable(textOf("voiceprintDetail")));
        QCOMPARE(textOf("voiceprintCreate"), QStringLiteral("Új minta készítése"));
        QCOMPARE(printCount(QStringLiteral("Kovács Lilla")), 1);     // nothing is made on open
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("voiceprintPopover"));

        // 2) A named speaker with no voiceprint yet, with enough material: can be created here.
        QVERIFY(m_vm->reassignSpeaker(anon, QStringLiteral("Bálint Péter"), false));
        m_vm->dismissChange();
        QTRY_COMPARE(markState(anon), QStringLiteral("none"));
        pump();     // the overview rows were rebuilt: their place is final after the next layout
        click(center(markOf(anon)));
        QTRY_VERIFY(popupOpen("voiceprintPopover"));
        QCOMPARE(textOf("voiceprintState"), QStringLiteral("Még nincs hanglenyomata"));
        QCOMPARE(textOf("voiceprintCreate"), QStringLiteral("Hanglenyomat készítése"));
        QVERIFY(!visual("voiceprintUndo"));
        QCOMPARE(printCount(QStringLiteral("Bálint Péter")), 0);
        click(center(visual("voiceprintCreate")));
        QCOMPARE(printCount(QStringLiteral("Bálint Péter")), 1);
        QTRY_COMPARE(markState(anon), QStringLiteral("has"));       // the indicator refreshes
        QCOMPARE(textOf("voiceprintState"), QStringLiteral("Van hanglenyomata"));
        QVERIFY2(textOf("voiceprintResult").startsWith(QStringLiteral("Elkészült ")), qPrintable(textOf("voiceprintResult")));
        QVERIFY(textOf("voiceprintResult").contains(QStringLiteral("mp beszédből")));
        QVERIFY(!visual("voiceprintCreate"));
        // ... and the just-created one is undoable: exactly that is deleted.
        QVERIFY(visual("voiceprintUndo"));
        click(center(visual("voiceprintUndo")));
        QCOMPARE(printCount(QStringLiteral("Bálint Péter")), 0);
        QTRY_COMPARE(markState(anon), QStringLiteral("none"));
        QCOMPARE(textOf("voiceprintResult"), QStringLiteral("A most készült hanglenyomat törölve."));
        QCOMPARE(textOf("voiceprintCreate"), QStringLiteral("Hanglenyomat készítése"));
        QCOMPARE(m_vm->speakerInfo(anon).value(QStringLiteral("name")).toString(), QStringLiteral("Bálint Péter"));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("voiceprintPopover"));

        // 3) Too little material: it says how much is missing and what helps; there is no button.
        const QString id = m_vm->rowInfo(3).value(QStringLiteral("utteranceId")).toString();
        QVERIFY(m_vm->moveUtteranceToPerson(id, QStringLiteral("Ördög Ödön")));
        m_vm->dismissChange();
        const QString odon = keyOfName(QStringLiteral("Ördög Ödön"));
        QTRY_COMPARE(markState(odon), QStringLiteral("none"));
        pump();     // the overview rows were rebuilt: their place is final after the next layout
        click(center(markOf(odon)));
        QTRY_VERIFY(popupOpen("voiceprintPopover"));
        QCOMPARE(textOf("voiceprintState"), QStringLiteral("Még nincs hanglenyomata"));
        QVERIFY2(textOf("voiceprintDetail").contains(QStringLiteral("Még kb. ")), qPrintable(textOf("voiceprintDetail")));
        QVERIFY(textOf("voiceprintDetail").contains(QStringLiteral("legalább 3 másodperces sorát rendeled hozzá")));
        QVERIFY(!visual("voiceprintCreate"));
        QVERIFY(!visual("voiceprintUndo"));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("voiceprintPopover"));
        QTRY_VERIFY(m_tab->hasActiveFocus());
    }

    // 11 speakers: the "Other (N)" row has no marker; clicking the name expands it, and the
    // low-talk speakers get their own marker (and panel) too; it can be collapsed again.
    void overviewVoiceprint_collapsedSpeakersAfterExpanding()
    {
        load(QStringLiteral("many"));
        QVERIFY(m_vm->collapsedCount() > 0);
        const int shown = int(m_vm->overview().size());
        QCOMPARE(int(visuals("overviewVoiceprint").size()), shown - 1);
        const QString gergo = keyOfName(QStringLiteral("Horváth Gergő"));
        QVERIFY(!markOf(gergo));
        // The track header's dot shows the same state (visible for a named speaker).
        m_vm->setRailVisible(true);
        pump();
        for (QQuickItem* dot : visuals("laneVoiceprint"))
            QVERIFY(dot->property("voiceprint").toString() != QLatin1String("anonymous"));
        m_vm->setRailVisible(false);
        pump();

        QQuickItem* other = visuals("overviewName").constLast();
        click(other->mapToScene(QPointF(12, other->height() / 2)).toPoint());
        QVERIFY(m_vm->lanesExpanded());
        QVERIFY(!popupOpen("speakerWhyPopover"));
        QTRY_COMPARE(int(visuals("overviewVoiceprint").size()), m_vm->speakerCount());
        pump();
        QCOMPARE(markState(gergo), QStringLiteral("none"));
        QCOMPARE(markState(keyOfName(QStringLiteral("Németh Dávid"))), QStringLiteral("has"));
        QCOMPARE(markState(keyOfName(QStringLiteral("Távoli 3"))), QStringLiteral("anonymous"));
        click(center(markOf(gergo)));
        QTRY_VERIFY(popupOpen("voiceprintPopover"));
        QCOMPARE(textOf("voiceprintName"), QStringLiteral("Horváth Gergő"));
        QCOMPARE(textOf("voiceprintState"), QStringLiteral("Még nincs hanglenyomata"));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("voiceprintPopover"));

        QVERIFY(visual("overviewCollapse"));
        click(center(visual("overviewCollapse")));
        QVERIFY(!m_vm->lanesExpanded());
        QTRY_COMPARE(int(visuals("overviewVoiceprint").size()), shown - 1);
    }

    // In the panel opened from a line the voiceprint block shows only in the "<Name>'s every
    // line" scope; it can be created there (and undone) the same way as in the whole speaker's panel.
    void linePopover_wholeSpeakerScope_hasVoiceprintBlock()
    {
        load();
        click(center(nameOf(0)));
        QTRY_VERIFY(popupOpen("linePopover"));
        QCOMPARE(popover()->property("scope").toString(), QStringLiteral("line"));
        QVERIFY(!visual("voiceprintBlock"));
        QVERIFY(!visual("voiceprintCreate"));
        click(center(visual("scopeSpeaker")));
        QVERIFY(visual("voiceprintBlock"));
        QCOMPARE(textOf("voiceprintState"), QStringLiteral("Van hanglenyomata"));
        QVERIFY(textOf("voiceprintDetail").contains(QStringLiteral("hosszabb sora használható fel")));
        QVERIFY(visual("popoverFooter"));
        // The block and the footer stay inside the window (in a low window the lists are shorter).
        QQuickItem* footer = visual("popoverFooter");
        QVERIFY(footer->mapToScene(QPointF(0, footer->height())).y() <= m_window->height());
        QCOMPARE(printCount(QStringLiteral("Kovács Lilla")), 1);
        click(center(visual("voiceprintCreate")));
        QCOMPARE(printCount(QStringLiteral("Kovács Lilla")), 2);
        QVERIFY(popupOpen("linePopover"));               // the panel stays open: undoable
        QVERIFY(textOf("voiceprintResult").startsWith(QStringLiteral("Elkészült ")));
        click(center(visual("voiceprintUndo")));
        QCOMPARE(printCount(QStringLiteral("Kovács Lilla")), 1);
        QVERIFY(!m_vm->canUndo());                          // no reassignment happened
        // Back to the per-line scope: the block disappears.
        click(center(visual("scopeLine")));
        QVERIFY(!visual("voiceprintBlock"));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("linePopover"));

        // From the whole speaker's "Why them?" panel (map name) the "Samples" link opens the
        // voiceprint panel, with the same block.
        const QList<QQuickItem*> names = visuals("overviewName");
        click(names.at(0)->mapToScene(QPointF(12, names.at(0)->height() / 2)).toPoint());
        QTRY_VERIFY(popupOpen("speakerWhyPopover"));
        QQuickItem* samples = nullptr;
        QTRY_VERIFY(!visuals("fixLink").isEmpty());
        for (QQuickItem* link : visuals("fixLink"))
            if (link->property("text").toString() == QStringLiteral("Minták")) samples = link;
        QVERIFY(samples);
        click(center(samples));
        QTRY_VERIFY(popupGone("speakerWhyPopover"));
        QTRY_VERIFY(popupOpen("voiceprintPopover"));
        QVERIFY(visual("voiceprintCreate"));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("voiceprintPopover"));
    }

    void changeBar_offersVoiceprint_afterNamingWholeSpeaker()
    {
        load();
        m_vm->setRailVisible(true);
        pump();
        click(railPoint(3, 1));                             // one line: no offer
        QTRY_VERIFY(barShown());
        QVERIFY(visual("changeUndo"));
        QVERIFY(!visual("changeVoiceprint"));
        undoKey();
        QVERIFY(!barShown());

        // The anonymous speaker gets a name from the overview's name (the person has no voiceprint).
        const QString anon = keyOfName(QStringLiteral("Távoli 1"));
        QQuickItem* anonName = nullptr;
        for (QQuickItem* item : visuals("overviewName"))
            if (item->property("text").toString() == QStringLiteral("Távoli 1")) anonName = item;
        QVERIFY(anonName);
        click(anonName->mapToScene(QPointF(12, anonName->height() / 2)).toPoint());
        QTRY_VERIFY(popupOpen("speakerWhyPopover"));
        click(center(visual("newPersonLink")));             // "New person..." opens the person picker
        QTRY_VERIFY(popupOpen("personPicker"));
        QTest::mouseMove(m_window.get(), QPoint(2, 2));
        type(QStringLiteral("balint"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(!popupOpen("personPicker"));
        QCOMPARE(m_vm->speakerInfo(anon).value(QStringLiteral("name")).toString(), QStringLiteral("Bálint Péter"));
        QTRY_VERIFY(barShown());
        QVERIFY(visual("changeVoiceprint"));
        QCOMPARE(textOf("changeVoiceprint"), QStringLiteral("Hanglenyomat készítése"));
        QCOMPARE(printCount(QStringLiteral("Bálint Péter")), 0);    // not created by itself
        QTRY_COMPARE(markState(anon), QStringLiteral("none"));

        click(center(visual("changeVoiceprint")));
        QCOMPARE(printCount(QStringLiteral("Bálint Péter")), 1);
        QVERIFY(barShown());
        QVERIFY2(barText().startsWith(QStringLiteral("Hanglenyomat készült: Bálint Péter (")), qPrintable(barText()));
        QCOMPARE(textOf("changeText"), barText());
        QVERIFY(!visual("changeVoiceprint"));
        QTRY_COMPARE(markState(anon), QStringLiteral("has"));

        // Undo on the bar: the voiceprint is deleted, the naming stays (that goes with Ctrl+Z).
        click(center(visual("changeUndo")));
        QCOMPARE(printCount(QStringLiteral("Bálint Péter")), 0);
        QCOMPARE(m_vm->speakerInfo(anon).value(QStringLiteral("name")).toString(), QStringLiteral("Bálint Péter"));
        QVERIFY(barShown());
        QCOMPARE(barText(), QStringLiteral("A most készült hanglenyomat törölve: Bálint Péter"));
        QVERIFY(!visual("changeUndo"));
        QVERIFY(!visual("changeVoiceprint"));
        QTRY_COMPARE(markState(anon), QStringLiteral("none"));
        QVERIFY(m_vm->canUndo());
        undoKey();
        QCOMPARE(m_vm->speakerInfo(anon).value(QStringLiteral("name")).toString(), QStringLiteral("Távoli 1"));

        // Merged into a person who already has a voiceprint: no offer.
        QVERIFY(m_vm->mergeSpeakers(anon, keyOfName(QStringLiteral("Fehér Ádám"))));
        QTRY_VERIFY(barShown());
        QVERIFY(!visual("changeVoiceprint"));
    }

    // Two similar voices: the bar's second row offers reviewing the two (Review / Not now); in
    // the whole-speaker scope of the "Who said it?" panel the same by hand, choosing the pair.
    void pairRecheck_changeBarOffer_andPopover()
    {
        load();
        m_vm->applyDemoState(QStringLiteral("changePairOffer"));
        QTRY_VERIFY(barShown());
        QVERIFY(m_vm->pairOfferActive());
        QTRY_VERIFY(visual("pairOffer"));
        QCOMPARE(textOf("pairOfferText"), m_vm->pairOfferText());
        QVERIFY(m_vm->pairOfferText().contains(QStringLiteral(" hangja hasonló. Nézzem át kettejük sorait")));
        QTRY_VERIFY(changeBar()->property("height").toReal() > 70);
        QVERIFY(!visual("changeSimilar"));

        // "Not now": the offer disappears, the reassignment notice stays.
        pump(100);      // layout of the grown bar
        click(center(visual("pairOfferDecline")));
        QVERIFY(!m_vm->pairOfferActive());
        QVERIFY(barShown());
        QTRY_VERIFY(!visual("pairOffer"));
        m_vm->undo();

        // "Review": it runs (one undo step), a notice in place of the bar.
        m_vm->applyDemoState(QStringLiteral("changePairOffer"));
        QTRY_VERIFY(visual("pairOfferAccept"));
        pump(100);
        m_shell->setProperty("toasts", QString());
        click(center(visual("pairOfferAccept")));
        QVERIFY(!m_vm->pairOfferActive());
        QTRY_VERIFY(!barShown());
        const QString toast = m_shell->property("toasts").toString();
        QVERIFY2(toast.contains(QStringLiteral(" között")), qPrintable(toast));
        QVERIFY2(m_vm->undoText().startsWith(QStringLiteral("Átnézés: ")) || toast.startsWith(QStringLiteral("A megerősített")),
                 qPrintable(m_vm->undoText()));

        // The panel: "Review with another speaker..." then choosing the pair, then it runs.
        m_vm->setRailVisible(true);
        pump();
        click(center(visuals("laneHead").at(1)));
        QTRY_VERIFY(popupOpen("speakerWhyPopover"));
        QVERIFY(visual("pairRecheckButton"));
        QVERIFY(!visual("pairRecheckList"));
        click(center(visual("pairRecheckButton")));
        QTRY_VERIFY(visual("pairRecheckList"));
        const QList<QQuickItem*> choices = visuals("pairChoice");
        QVERIFY(!choices.isEmpty());
        const QString other = choices.first()->property("personName").toString();
        m_shell->setProperty("toasts", QString());
        click(center(choices.first()));
        QTRY_VERIFY(popupGone("speakerWhyPopover"));
        const QString toast2 = m_shell->property("toasts").toString();
        QVERIFY2(toast2.contains(QStringLiteral("Fehér Ádám és ") + other), qPrintable(toast2));
        QVERIFY(m_warnings.isEmpty());
    }

    // Without a voice model: no dead button; the panel says the reason once, the bar offers nothing.
    void withoutVoiceModel_noDeadVoiceprintButton()
    {
        load(QStringLiteral("novoice"));
        const QString anon = keyOfName(QStringLiteral("Távoli 1"));
        QVERIFY(m_vm->reassignSpeaker(anon, QStringLiteral("Bálint Péter"), false));
        QTRY_VERIFY(barShown());
        QVERIFY(!visual("changeVoiceprint"));
        m_vm->dismissChange();
        QTRY_COMPARE(markState(anon), QStringLiteral("none"));
        pump();     // the overview rows were rebuilt: their place is final after the next layout
        click(center(markOf(anon)));
        QTRY_VERIFY(popupOpen("voiceprintPopover"));
        QCOMPARE(textOf("voiceprintState"), QStringLiteral("Még nincs hanglenyomata"));
        QCOMPARE(textOf("voiceprintDetail"),
                 QStringLiteral("Nincs letöltve a hangmodell, ezért itt most nem készíthető hanglenyomat."));
        QVERIFY(!visual("voiceprintCreate"));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("voiceprintPopover"));

        // The "Who said it?" panel's block likewise.
        click(center(nameOf(0)));
        QTRY_VERIFY(popupOpen("linePopover"));
        click(center(visual("scopeSpeaker")));
        QVERIFY(visual("voiceprintBlock"));
        QVERIFY(textOf("voiceprintDetail").startsWith(QStringLiteral("Nincs letöltve a hangmodell")));
        QVERIFY(!visual("voiceprintCreate"));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("linePopover"));
    }

    // Review on the "v3" meeting: the line panel's candidates with evidence, keys 1-3; the reasons
    // of "Why not X?" with the place of the fix (track assignment leads to the speaker's "Why them?" panel with the track chips).
    void linePopover_candidatesKeysAndWhyNot()
    {
        load(QStringLiteral("v3"));
        QVERIFY(QTest::qWaitFor([this] { return !m_vm->reviewRunning() && m_vm->reviewCount() > 0; }, 10000));
        QCOMPARE(cell(2, Role::UncertainReasonRole).toString(), QStringLiteral("side"));
        const QString lilla = speakerOf(2);
        const QString gabor = keyOfName(QStringLiteral("Fehér Gábor"));
        click(center(nameOf(2)));
        QTRY_VERIFY(popupOpen("linePopover"));
        QTRY_VERIFY(choice("speakerChoice", QStringLiteral("Fehér Gábor")));
        QCOMPARE(choice("speakerChoice", QStringLiteral("Fehér Gábor"))->property("keyHint").toString(),
                 QStringLiteral("1"));
        QVERIFY(choice("currentChoice", QStringLiteral("Kovács Lilla")));
        QVERIFY(choice("currentChoice", QStringLiteral("Kovács Lilla"))->property("dimmed").toBool());
        // The best candidate's evidence: voice + the same track + #Nordvik.
        const QVariantList ev = choice("speakerChoice", QStringLiteral("Fehér Gábor"))->property("evidence").toList();
        QStringList texts;
        for (const QVariant& e : ev) texts << e.toMap().value(QStringLiteral("text")).toString();
        QVERIFY2(texts.contains(QStringLiteral("ugyanaz a sáv")) && texts.contains(QStringLiteral("#Nordvik")),
                 qPrintable(texts.join(QLatin1Char('|'))));
        // "Why not Kovács Lilla?": the track reasons, with a fix link.
        QVERIFY(visual("whyNotBlock"));
        QVERIFY(!visuals("whyNotReason").isEmpty());
        QQuickItem* tracks = nullptr;
        for (QQuickItem* link : visuals("fixLink"))
            if (link->property("text").toString() == QStringLiteral("Sáv-beosztás")) tracks = link;
        QVERIFY(tracks);

        // 1: the suggested candidate.
        QTest::keyClick(m_window.get(), Qt::Key_1);
        QTRY_VERIFY(popupGone("linePopover"));
        QCOMPARE(speakerOf(2), gabor);
        QTRY_VERIFY(barShown());
        undoKey();
        QCOMPARE(speakerOf(2), lilla);

        // Track assignment leads to the speaker's "Why them?" panel, with the track chips.
        click(center(nameOf(2)));
        QTRY_VERIFY(popupOpen("linePopover"));
        tracks = nullptr;
        for (QQuickItem* link : visuals("fixLink"))
            if (link->property("text").toString() == QStringLiteral("Sáv-beosztás")) tracks = link;
        QVERIFY(tracks);
        click(center(tracks));
        QTRY_VERIFY(popupOpen("speakerWhyPopover"));
        QCOMPARE(why()->property("speakerKey").toString(), lilla);
        QTRY_VERIFY(visual("tracksBlock"));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("speakerWhyPopover"));
        QVERIFY(m_warnings.isEmpty());
    }

    // Review on the "v3" meeting: "All N to X" is one step (and undoable); the contaminated
    // core's track "Split" button is also one step.
    // The contaminated core's track under the focus card (in place of the lower context lines):
    // Split = one undo step. (The group-level "All to X" went away with the old group view; the
    // openers' suggestion of the S5 slice brings the big groups back.)
    void reviewView_contaminatedSplit()
    {
        load(QStringLiteral("v3"));
        m_vm->applyDemoState(QStringLiteral("reviewGroups"));
        QVERIFY(QTest::qWaitFor([this] {
            return !m_vm->reviewRunning() && !m_vm->demoStatePending() && !m_vm->contaminated().isEmpty(); }, 10000));
        QTRY_VERIFY(visual("reviewView"));
        QVERIFY(skipOpeners());
        QTRY_VERIFY_WITH_TIMEOUT(visual("contaminatedBanner"), 10000);
        const QString gabor = keyOfName(QStringLiteral("Fehér Gábor"));
        QQuickItem* card = visual("reviewFocusCard");
        QVERIFY(card);
        QVERIFY(visual("contaminatedBanner")->mapToScene(QPointF(0, 0)).y() > card->mapToScene(QPointF(0, card->height())).y());

        QTRY_VERIFY(!m_vm->reviewRunning());
        QTRY_VERIFY(visual("splitSpeaker"));
        // The bar under the card: in a low view the focus area is visible with its own scrolling.
        QQuickItem* flick = visual("reviewFocusFlick");
        QVERIFY(flick);
        flick->setProperty("contentY", std::max(0.0, flick->property("contentHeight").toReal() - flick->height()));
        pump(60);
        const int gaborLines = linesOf(gabor);
        click(center(visual("splitSpeaker")));
        QVERIFY(linesOf(gabor) < gaborLines);
        QTRY_VERIFY(barShown());
        undoKey();
        QCOMPARE(linesOf(gabor), gaborLines);
        QVERIFY(m_warnings.isEmpty());
    }

    // ---- shared base of the To review redesign ----


    // The shared key reader returns the PHYSICAL key: on the Hungarian layout Shift+6 is "/",
    // Shift+1 is "'", but from the scancode it is still a digit; "0" (top left) is Key_0 per layout.
    void keys_physicalDecode()
    {
        QObject* keys = m_engine->singletonInstance<QObject*>("Tanara", "TranscriptKeys");
        QVERIFY(keys);
        const QString platform = keys->property("platform").toString();
        auto decode = [&](int key, int mods, int scan, const char* os) {
            keys->setProperty("platform", QString::fromLatin1(os));
            QVariant r;
            QMetaObject::invokeMethod(keys, "decodeRaw", Q_RETURN_ARG(QVariant, r), Q_ARG(QVariant, key),
                                      Q_ARG(QVariant, mods), Q_ARG(QVariant, scan));
            if (r.metaType() == QMetaType::fromType<QJSValue>()) r = r.value<QJSValue>().toVariant();
            return r.toMap();
        };
        const int shift = int(Qt::ShiftModifier);
        QVariantMap k = decode(Qt::Key_Slash, shift, 15, "linux");             // HU: Shift+6
        QCOMPARE(k.value("name").toString(), QStringLiteral("digit"));
        QCOMPARE(k.value("digit").toInt(), 6);
        QVERIFY(k.value("shift").toBool());
        QVERIFY(k.value("plain").toBool());
        QCOMPARE(decode(Qt::Key_Apostrophe, shift, 10, "linux").value("digit").toInt(), 1);
        QCOMPARE(decode(Qt::Key_Slash, shift, 7, "windows").value("digit").toInt(), 6);
        QCOMPARE(decode(Qt::Key_0, 0, 49, "linux").value("digit").toInt(), 0);   // HU "0" (top left)
        QCOMPARE(decode(Qt::Key_6, int(Qt::KeypadModifier), 0, "").value("digit").toInt(), 6);
        QCOMPARE(decode(Qt::Key_Question, shift, 59, "linux").value("name").toString(), QStringLiteral("Comma"));
        QCOMPARE(decode(Qt::Key_M, 0, 58, "linux").value("name").toString(), QStringLiteral("M"));
        k = decode(Qt::Key_J, int(Qt::ControlModifier), 44, "linux");
        QCOMPARE(k.value("name").toString(), QStringLiteral("J"));
        QVERIFY(!k.value("plain").toBool());
        // Without a native code (macOS) Shift+6 is not a digit (no guessing).
        QCOMPARE(decode(Qt::Key_Slash, shift, 0, "").value("name").toString(), QString());
        keys->setProperty("platform", platform);
    }

    // Shift = not a sample: Shift+rail click and Shift+digit; the line moves, "not a sample", in
    // ONE undo step; with Shift held the preview label is "-> Name, not a sample".
    void shift_railClickAndDigit_nonSample_oneUndoStep()
    {
        load();
        m_tab->setProperty("fixMode", true);
        pump();
        // Hover + Shift held: the label gets a suffix.
        QTest::mouseMove(m_window.get(), railPoint(3, 2) + QPoint(-30, 0));
        QTest::mouseMove(m_window.get(), railPoint(3, 2));
        QTRY_VERIFY(visual("dragChipLabel"));
        QVERIFY(!textOf("dragChipLabel").endsWith(QStringLiteral(" · nem minta")));
        QTest::keyPress(m_window.get(), Qt::Key_Shift, Qt::ShiftModifier);
        QVERIFY(m_tab->property("shiftHeld").toBool());
        QVERIFY(textOf("dragChipLabel").endsWith(QStringLiteral(" · nem minta")));
        QTest::keyRelease(m_window.get(), Qt::Key_Shift, Qt::NoModifier);
        QVERIFY(!m_tab->property("shiftHeld").toBool());

        const QString id = rowId(3);
        const QString before = speakerOf(3);
        QVERIFY(before != laneKey(2));
        QVERIFY(!noisyOf(id));
        click(railPoint(3, 2), Qt::ShiftModifier);
        QCOMPARE(speakerOf(3), laneKey(2));
        QVERIFY(noisyOf(id));
        QCOMPARE(cell(3, Role::SampleStateRole).toString(), QStringLiteral("noSample"));
        QTRY_VERIFY(barShown());
        undoKey();
        QCOMPARE(speakerOf(3), before);
        QVERIFY(!noisyOf(id));
        QVERIFY(!m_vm->canUndo());                          // it was one step

        // Shift+digit on the selection (in the test Key_2 + Shift; the scancode path is tested by decode).
        reveal(5);
        click(textPoint(5));
        QVERIFY(m_vm->isRowSelected(5));
        const QString id5 = rowId(5);
        const QString target = speakerOf(5) == laneKey(0) ? laneKey(1) : laneKey(0);
        QTest::keyClick(m_window.get(), target == laneKey(0) ? Qt::Key_1 : Qt::Key_2, Qt::ShiftModifier);
        pump();
        QCOMPARE(m_vm->lineInfo(id5).value(QStringLiteral("speakerKey")).toString(), target);
        QVERIFY(noisyOf(id5));
        undoKey();
        QVERIFY(!noisyOf(id5));
        QVERIFY(!m_vm->canUndo());
    }

    // In the To review view too: with Shift held "Good as is, not a sample", and a click decides as not a sample.
    void shift_reviewButtons_nonSample()
    {
        load();
        openReview();
        QQuickItem* confirm = visual("reviewFocusConfirm");
        QVERIFY(confirm);
        const QString id = focusLine();
        QVERIFY(!confirm->property("nonSample").toBool());
        m_tab->setProperty("shiftHeld", true);
        QVERIFY(confirm->property("nonSample").toBool());
        QVERIFY(visual("reviewFocusEnter")->property("nonSample").toBool());
        click(center(confirm));
        QVERIFY(noisyOf(id));
        QCOMPARE(m_vm->lastDecision().value(QStringLiteral("chips")).toList().first().toMap()
                     .value(QStringLiteral("kind")).toString(), QStringLiteral("noSample"));
        m_tab->setProperty("shiftHeld", false);
    }

    // 0 = cannot be identified: in Edit on the selection (again 0: back), marker + dimmed line;
    // the list can hide it; in To review on the chosen line and with the line button.
    void zeroKey_ignored_markerFilterAndReview()
    {
        load();
        m_tab->setProperty("fixMode", true);
        pump();
        click(textPoint(2));
        QVERIFY(m_vm->isRowSelected(2));
        const int idx2 = cell(2, Role::UtteranceIndexRole).toInt();
        QTest::keyClick(m_window.get(), Qt::Key_0);
        pump();
        QVERIFY(cell(2, Role::IgnoredRole).toBool());
        QCOMPARE(m_vm->ignoredCount(), 1);
        QTRY_VERIFY(rowItem(2) && rowItem(2)->findChild<QQuickItem*>(QStringLiteral("ignoredMarker"))->isVisible());
        QCOMPARE(m_vm->lastDecision().value(QStringLiteral("chips")).toList().first().toMap()
                     .value(QStringLiteral("kind")).toString(), QStringLiteral("ignored"));
        // Hiding the "cannot be identified" lines from the header.
        QTRY_VERIFY(visual("ignoredToggle"));
        click(center(visual("ignoredToggle")));
        QVERIFY(m_vm->hideIgnored());
        QCOMPARE(m_vm->rows()->rowOfUtterance(idx2), -1);
        click(center(visual("ignoredToggle")));
        QVERIFY(!m_vm->hideIgnored());
        // 0 again on the selection: identifiable again.
        click(textPoint(2));
        if (!m_vm->isRowSelected(2)) click(textPoint(2));
        QTest::keyClick(m_window.get(), Qt::Key_0);
        QVERIFY(!cell(2, Role::IgnoredRole).toBool());
        QCOMPARE(m_vm->ignoredCount(), 0);

        // To review: the focus line to 0 (it stays in place on the work list), then the next with the button.
        openReview();
        ReviewQueueModel* q = m_vm->reviewQueue();
        const QString id = focusLine();
        const int row = q->indexOf(id);
        QTest::keyClick(m_window.get(), Qt::Key_0);
        QVERIFY(m_vm->lineInfo(id).value(QStringLiteral("ignored")).toBool());
        QCOMPARE(q->indexOf(id), row);
        QCOMPARE(q->get(row).value(QStringLiteral("state")).toString(), QStringLiteral("ignored"));
        const QString other = focusLine();
        QVERIFY(other != id);
        click(center(visual("reviewFocusIgnore")));
        QVERIFY(m_vm->lineInfo(other).value(QStringLiteral("ignored")).toBool());
        undoKey();
        QVERIFY(!m_vm->lineInfo(other).value(QStringLiteral("ignored")).toBool());
        QTRY_COMPARE(focusLine(), other);                   // the focus on the undone line
    }

    // The "Just now" row: after J "Good as is" + a sample chip; Shift+J not a sample; mini-log;
    // Ctrl+Z gives "Undone: ..."; it does not disappear.
    void imentRow_confirmChipsLogUndo()
    {
        load();
        m_tab->setProperty("fixMode", true);
        pump();
        QVERIFY(imentShown());
        QVERIFY(m_vm->lastDecision().isEmpty());
        reveal(4);
        click(textPoint(4));
        QTest::keyClick(m_window.get(), Qt::Key_J);
        pump();
        QVariantMap last = m_vm->lastDecision();
        QVERIFY2(last.value(QStringLiteral("text")).toString().startsWith(QStringLiteral("Jó így: ")),
                 qPrintable(QStringLiteral("%1 sel=%2 cur=%3").arg(last.value(QStringLiteral("text")).toString())
                                .arg(m_vm->selectedCount()).arg(m_vm->currentRow())));
        QCOMPARE(textOf("changeText"), last.value(QStringLiteral("text")).toString());
        QVERIFY(!last.value(QStringLiteral("chips")).toList().isEmpty());
        QTRY_VERIFY(visual("imentChip"));
        reveal(6);
        click(textPoint(6));
        if (!m_vm->isRowSelected(6)) click(textPoint(6));
        QTest::keyClick(m_window.get(), Qt::Key_J, Qt::ShiftModifier);
        pump();
        last = m_vm->lastDecision();
        QCOMPARE(last.value(QStringLiteral("chips")).toList().first().toMap().value(QStringLiteral("kind")).toString(),
                 QStringLiteral("noSample"));
        QCOMPARE(m_vm->decisionLog().size(), 1);
        click(center(visual("decisionLogToggle")));
        QTRY_VERIFY(popupOpen("decisionLog"));
        QCOMPARE(visuals("decisionLogEntry").size(), 1);
        click(center(visual("decisionLogToggle")));
        QTRY_VERIFY(popupGone("decisionLog"));
        undoKey();
        last = m_vm->lastDecision();
        QVERIFY(last.value(QStringLiteral("undone")).toBool());
        QVERIFY(last.value(QStringLiteral("text")).toString().startsWith(QStringLiteral("Visszavonva: ")));
        pump(1200);
        QVERIFY(imentShown());
        QCOMPARE(textOf("changeText"), last.value(QStringLiteral("text")).toString());
    }

    // Mismatched sample (with the v3 made-up meeting's fake embedder): the "Just now" row warns,
    // and "Make it not a sample" makes the line not a sample (the warning goes away).
    void sampleWarning_deviatingSample()
    {
        load(QStringLiteral("v3"));
        m_tab->setProperty("fixMode", true);
        m_vm->applyDemoState(QStringLiteral("imentWarning"));
        QTRY_VERIFY(!m_vm->sampleDeviation().isEmpty());
        const QVariantMap dev = m_vm->sampleDeviation();
        QVERIFY(dev.value(QStringLiteral("pct")).toInt() < 50);
        QTRY_VERIFY(visual("sampleWarning"));
        pump(150);                                          // layout of the grown row
        QVERIFY(textOf("sampleWarningText").contains(dev.value(QStringLiteral("name")).toString()));
        QVERIFY(m_vm->decisionLog().size() >= 3);
        const QString id = dev.value(QStringLiteral("utteranceId")).toString();
        QVERIFY(!noisyOf(id));
        click(center(visual("sampleWarningNoisy")));
        QVERIFY(noisyOf(id));
        QVERIFY(m_vm->sampleDeviation().isEmpty());
        QTRY_VERIFY(!visual("sampleWarning"));
    }

    // Sample meter in the dock: "N samples +k" per sampleStats; a click on "+k" filters Edit to
    // the lines that just became samples (Esc: back).
    void sampleMeter_valuesAndFilter()
    {
        load();
        m_tab->setProperty("fixMode", true);
        pump();
        // Two longer lines from the same speaker: "Good as is".
        QString key;
        QStringList confirmed;
        for (int r = 0; r < m_vm->rows()->rowCount() && confirmed.size() < 2; ++r) {
            if (cell(r, Role::UncertainRole).toBool() || cell(r, Role::NoisyRole).toBool()) continue;
            if (cell(r, Role::EndMsRole).toInt() - cell(r, Role::StartMsRole).toInt() < 4000) continue;
            if (!key.isEmpty() && speakerOf(r) != key) continue;
            key = speakerOf(r);
            QVERIFY(m_vm->confirmRow(r));
            confirmed << rowId(r);
        }
        QCOMPARE(confirmed.size(), 2);
        QVariantMap stat;
        QTRY_VERIFY([&] {
            for (const QVariant& v : m_vm->sampleStats())
                if (v.toMap().value(QStringLiteral("key")).toString() == key) stat = v.toMap();
            return stat.value(QStringLiteral("sessionAdded")).toInt() >= 2;
        }());
        QQuickItem* meter = nullptr;
        QTRY_VERIFY([&] {
            for (QQuickItem* m : visuals("sampleMeter"))
                if (m->property("speakerKey").toString() == key) meter = m;
            return meter != nullptr;
        }());
        QCOMPARE(meter->property("samples").toInt(), stat.value(QStringLiteral("sampleLines")).toInt());
        QCOMPARE(meter->property("added").toInt(), stat.value(QStringLiteral("sessionAdded")).toInt());
        QQuickItem* added = meter->findChild<QQuickItem*>(QStringLiteral("sampleMeterAdded"));
        QVERIFY(added && added->isVisible());
        pump(150);
        QCOMPARE(added->property("text").toString(), QStringLiteral("+%1").arg(stat.value(QStringLiteral("sessionAdded")).toInt()));
        click(center(added));
        QCOMPARE(m_vm->listFilter(), QStringLiteral("samples"));
        QCOMPARE(m_vm->sampleFilterKey(), key);
        const QStringList ids = stat.value(QStringLiteral("sessionAddedIds")).toStringList();
        for (int r = 0; r < m_vm->rows()->rowCount(); ++r)
            if (cell(r, Role::KindRole).toString() == QLatin1String("utterance")) QVERIFY(ids.contains(rowId(r)));
        QTRY_VERIFY(visual("sampleFilterNote"));
        m_vm->clearSelection();
        m_tab->forceActiveFocus();
        QTest::keyClick(m_window.get(), Qt::Key_Escape);    // the filter
        QCOMPARE(m_vm->listFilter(), QString());
    }

    void statesLoadWithoutWarnings_data()
    {
        QTest::addColumn<QString>("variant");
        QTest::addColumn<QString>("state");
        const char* states[] = {"", "rail", "playing", "selection", "suggestion", "suggestionShown", "filter",
                                "search", "searchEmpty", "speakerPopover", "personPicker", "drag", "expanded",
                                "linePopover", "selectionPopover", "lineToSpeakerPopover", "changeLine",
                                "changeSelection", "changeSpeaker", "changeFilter", "mergeConfirm",
                                "changeVoiceprint", "changeVoiceprintDone", "voiceprintHas", "voiceprintNone",
                                "voiceprintDone", "voiceprintShort", "changePairOffer",
                                "speakerPopoverPair"};
        for (const char* variant : {"", "two", "many", "long", "novoice", "none"})
            for (const char* state : states)
                QTest::addRow("%s-%s", *variant ? variant : "default", *state ? state : "plain")
                    << QString::fromLatin1(variant) << QString::fromLatin1(state);        // v3 Edit mode: with an empty variant they run on the "v3" made-up meeting.
        for (const char* state : {"markers", "fixLinePopover", "reviewGroups", "reviewExpanded",
                                  "contaminatedCore", "speakerWhy", "newPersonSimilar", "newPersonTag",
                                  "imentWarning", "imentLog", "shiftPreview", "ignoredRows", "sampleFilter",
                                  "reviewFocus", "reviewFocusDecided", "reviewFocusIncoming", "reviewFocusSummary",
                                  "reviewFocusAlreadyDecided", "reviewFocusSkipped"})
            QTest::addRow("v3-%s", state) << QString() << QString::fromLatin1(state);
    }
    void statesLoadWithoutWarnings()
    {
        QFETCH(QString, variant);
        QFETCH(QString, state);
        m_window.reset();
        m_warnings.clear();
        QQmlComponent comp(m_engine.get());
        comp.loadFromModule("Tanara", "TranscriptTab");
        std::unique_ptr<QQuickItem> tab(qobject_cast<QQuickItem*>(comp.createWithInitialProperties(
            {{QStringLiteral("demoVariant"), variant}, {QStringLiteral("demoState"), state},
             {QStringLiteral("width"), 1004}, {QStringLiteral("height"), 640}})));
        QVERIFY2(tab, qPrintable(comp.errorString()));
        QQuickWindow window;
        window.resize(1004, 640);
        tab->setParentItem(window.contentItem());
        window.show();
        // The v3 states open their panels after the audio and the background analysis.
        QTest::qWait(variant.isEmpty() && tab->property("v3Demo").toBool() ? 2500 : 180);
        tab.reset();
    }
};

QTEST_MAIN(TestTranscriptTab)
#include "test_transcript_tab.moc"
