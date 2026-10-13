// Az „Átirat" fül (TranscriptTab.qml) működése kijelző nélkül (offscreen), a beépített
// KITALÁLT meetingen: egér- és billentyű-események a QML-ablaknak (nem a felhasználó
// asztalának), hamis lejátszóval és shell-lel. Lefedi: Ctrl+L, kattintás másik oszlopba,
// Ctrl+Z, több sor kijelölése + számbillentyű, húzás, időbélyeg → seek, „+" → személyválasztó,
// lejátszás-követés, a Shell pozíció-kérése, „Meghallgatom"; és a javítás hatóköre: név →
// CSAK az a sor (kijelölésnél a kijelölt sorok), teljes beszélő a sáv-fejlécről / áttekintőről,
// összevonás előtt megerősítés, minden átsorolás után értesítő sáv (Visszavonás / Hasonló N
// sor is / „mind a N sora"), és a „Bizonytalan" szűrő nem ugrik el a javított sor alól.
// Hanglenyomat: jelző a térkép-dokk (MapDock) minden során + saját panel, a blokk a sorról nyitott panel
// teljes-beszélő hatókörében, ajánlat a sávon egy teljes beszélő elnevezése után, és a most
// készült lenyomat visszavonása.
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

    // A sín `lane` oszlopának közepe a `row` sor magasságában (ablak-koordináta).
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
    // Keresés a LÁTHATÓ fában (a Repeater / ListView delegáltjai és a felugró panelek
    // tartalma is megvan így).
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
    // Az Átnézendő kör eleji nyitó-javaslatai (S5) kihagyva (→ Később): a fókusz-kártya jön.
    bool skipOpeners()
    {
        QQuickItem* rv = m_window->findChild<QQuickItem*>(QStringLiteral("reviewView"));
        if (!rv || !QTest::qWaitFor([this] { return m_vm->queueActive(); }, 10000)) return false;
        pump(60);
        for (int g = 0; g < 20 && rv->property("phase").toString() == QLatin1String("opener"); ++g)
            QMetaObject::invokeMethod(rv, "openerLater");
        return rv->property("phase").toString() != QLatin1String("opener");
    }
    // A panel egy választható sora (a meeting beszélője / ismert személy) név szerint.
    QQuickItem* choice(const char* kind, const QString& personName) const
    {
        for (QQuickItem* item : visuals(kind))
            if (item->property("personName").toString() == personName) return item;
        return nullptr;
    }
    // A sor-panel („Kinek a sora ez?", LinePopover) és a teljes beszélő panelje („Miért ő?").
    QObject* popover() const { return m_window->findChild<QObject*>(QStringLiteral("linePopover")); }
    QObject* why() const { return m_window->findChild<QObject*>(QStringLiteral("speakerWhyPopover")); }
    QObject* changeBar() const { return m_window->findChild<QObject*>(QStringLiteral("changeBar")); }
    // Az Átnézendő (fókusz-nézet) bekapcsolása, a háttér-elemzés és a munkalista bevárása.
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
    // A fókusz-kártya sora (a munkalista kurzora).
    QString focusLine() const { return m_vm->reviewQueue()->currentUtterance(); }
    // Az átsorolás értesítése (és ajánlatai) az „Imént" sorban: a sor látszik, és a változás él.
    // (Az „Imént" sor maga Javítás módban mindig látszik: imentShown.)
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
    // A beszélő ujjlenyomat-jele az áttekintőn.
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
    // A személy lenyomatainak száma a (kitalált) meeting lenyomat-tárában.
    int printCount(const QString& person) const
    {
        for (const tanara::PersonInfo& p : m_vm->people())
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

    // A panel teljesen eltűnt (a záró animáció is lefutott): addig a fátyla még elnyeli a
    // következő kattintást.
    bool popupGone(const char* name) const
    {
        QObject* popup = m_window->findChild<QObject*>(QLatin1String(name));
        return popup && !popup->property("visible").toBool();
    }
    // Ctrl+Z a szerkesztőnek (a panel zárása után a fókusz visszatértét megvárva).
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
        QVERIFY(!m_vm->railVisible());                      // alapból rejtve
        QCOMPARE(m_tab->property("textX").toReal(), 0.0);

        QTest::keyClick(m_window.get(), Qt::Key_L, Qt::ControlModifier);
        QVERIFY(m_vm->railVisible());
        QVERIFY(m_tab->property("textX").toReal() > 100.0);
        pump();

        // Kattintás a 2. sor (Fehér Ádám) harmadik oszlopába → a sor átkerül oda.
        QSignalSpy resets(m_vm->rows(), &QAbstractItemModel::modelReset);
        const QString before = speakerOf(2);
        QCOMPARE(before, laneKey(1));
        click(railPoint(2, 2));
        QCOMPARE(speakerOf(2), laneKey(2));
        QVERIFY(cell(2, Role::CorrectedRole).toBool());
        QVERIFY(m_vm->canUndo());
        QCOMPARE(m_vm->selectedCount(), 0);
        // Az értesítő sáv megjelent: mi történt + Visszavonás egy kattintásra.
        QTRY_VERIFY(barShown());
        QCOMPARE(barText(), QStringLiteral("1 sor átkerült ide: %1")
                                .arg(m_vm->lanes().at(2).toMap().value(QStringLiteral("name")).toString()));
        QVERIFY(visual("changeUndo"));

        undoKey();
        QCOMPARE(speakerOf(2), before);
        QVERIFY(!barShown());                               // visszavonás után nincs mit mutatni
        QTest::keyClick(m_window.get(), Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(speakerOf(2), laneKey(2));
        undoKey();
        QCOMPARE(speakerOf(2), before);
        QCOMPARE(resets.count(), 0);

        // Kattintás a sor SAJÁT blokkjára: kijelölés (nem áthelyezés).
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

        // Kattintás a szövegen, majd Shift+kattintás: a 2–4. sor kijelölve (sín nélkül is megy).
        click(textPoint(2));
        QCOMPARE(m_vm->selectedCount(), 1);
        click(textPoint(4), Qt::ShiftModifier);
        QCOMPARE(m_vm->selectedCount(), 3);
        QVERIFY(bar->property("visible").toBool());
        click(textPoint(3), Qt::ControlModifier);           // Ctrl: a 3. ki
        QCOMPARE(m_vm->selectedCount(), 2);
        click(textPoint(3), Qt::ControlModifier);
        QCOMPARE(m_vm->selectedCount(), 3);

        // „2": a kijelölés a második beszélőhöz.
        QTest::keyClick(m_window.get(), Qt::Key_2);
        for (int r : {2, 3, 4}) QCOMPARE(speakerOf(r), laneKey(1));
        QCOMPARE(m_vm->selectedCount(), 0);
        QVERIFY(!bar->property("visible").toBool());
        // Számbillentyű után is ott a sáv (a kijelölés sávja helyén), a darabszámmal.
        QTRY_VERIFY(barShown());
        QCOMPARE(barText(), QStringLiteral("3 sor átkerült ide: Fehér Ádám"));
        undoKey();   // egy lépés
        for (int r : {2, 3, 4}) QCOMPARE(speakerOf(r), laneKey(0));

        // Le / fel: a kijelölés léptetése; Enter: lejátszás a sortól.
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
        // A 2. sor blokkját megfogva, a 4. oszlop fölé és a 3. sorig lehúzva.
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
        // Húzás után is megjelenik a sáv; a „Visszavonás" gombja egy lépésben visszacsinálja.
        QTRY_VERIFY(barShown());
        QVERIFY(barText().startsWith(QStringLiteral("2 sor átkerült ide: ")));
        QVERIFY(visual("changeUndo"));
        click(center(visual("changeUndo")));                // a húzás egy lépés
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

        // A lejátszás halad: a lista követi (a lejátszott sor látható marad).
        const int far = m_vm->rowForTime(600000);
        m_player->setProperty("positionMs", 600000);
        pump(60);
        QCOMPARE(m_vm->playingRow(), far);
        QVERIFY(m_list->property("contentY").toReal() > 0.0);
        QVERIFY(rowItem(far) != nullptr);

        // A felhasználó elgörget: a következő sorra lépés NEM rántja vissza a listát.
        QMetaObject::invokeMethod(m_list, "positionViewAtBeginning");
        pump();
        const qreal top = m_list->property("contentY").toReal();
        QVERIFY(rowItem(0) != nullptr);
        m_player->setProperty("positionMs", m_vm->rowStartMs(far + 1) + 10);
        pump(60);
        QCOMPARE(m_vm->playingRow(), far + 1);
        QCOMPARE(m_list->property("contentY").toReal(), top);
        const qreal before = top;

        // A Shell kérése (pl. összefoglaló-link): odagörget.
        QMetaObject::invokeMethod(m_shell, "transcriptPositionRequested", Q_ARG(int, 1200000));
        pump(60);
        QVERIFY(m_list->property("contentY").toReal() != before);
        QVERIFY(rowItem(m_vm->rowForTime(1200000)) != nullptr);
    }

    // A névre kattintva CSAK AZ A SOR kerül át (ez volt a baj: régen a teljes beszélő ment).
    void nameClick_movesOnlyThatLine()
    {
        load();
        QVERIFY(!m_vm->railVisible());                      // rejtett sín mellett is megy
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
        // A cím és a lábléc kimondja a hatókört és a darabszámot.
        QCOMPARE(visual("popoverTitle")->property("text").toString(), QStringLiteral("Kinek a sora ez?"));
        QCOMPARE(visual("popoverFooter")->property("text").toString(),
                 QStringLiteral("1 sor kerül át · Ctrl+Z visszavonja · a gép tanul belőle"));
        // Soronkénti hatókörben nincs hanglenyomat-jelölő, nincs „összevonás" lista; a meeting
        // többi beszélője áthelyezési célként elöl áll.
        QVERIFY(!visual("fixVoiceprints"));
        QVERIFY(!visual("mergeList"));
        QVERIFY(visual("meetingSpeakers"));
        QVERIFY(choice("speakerChoice", QStringLiteral("Varga Nóra")));
        QVERIFY(!choice("speakerChoice", cell(2, Role::SpeakerNameRole).toString()));
        // A mostani beszélő a jelöltek végén, halványan („most ő").
        QVERIFY(choice("currentChoice", cell(2, Role::SpeakerNameRole).toString()));

        type(QStringLiteral("varga"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popupGone("linePopover"));
        QCOMPARE(speakerOf(2), varga);
        QCOMPARE(linesOf(from), lines - 1);                 // csak EGY sor ment
        QCOMPARE(linesOf(varga), vargaLines + 1);
        QVERIFY(!popupOpen("mergeDialog"));                 // egy sor sosem kérdez
        QTRY_COMPARE(m_tab->property("speakerPopoverRow").toInt(), -1);

        // A sáv: mi történt, Visszavonás; a forrásnak maradt sora → „mind a N sora" is ott van.
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
        QVERIFY(!m_vm->canUndo());                          // egyetlen lépés volt
        QVERIFY(!barShown());

        // Egy ismert (a meetingen még nem szereplő) személyhez: új résztvevő, csak ezzel a sorral.
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

    // Ha a sor egy több soros kijelölés része, a névre kattintva a KIJELÖLT sorok mennek.
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
        QCOMPARE(m_vm->selectedCount(), 3);                 // a névre kattintás nem bontja a kijelölést
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
        QVERIFY(!popupOpen("mergeDialog"));                 // a kijelölés sem kérdez
        QTRY_VERIFY(barShown());
        QCOMPARE(barText(), QStringLiteral("3 sor átkerült ide: Varga Nóra"));

        undoKey();   // egy lépés
        QCOMPARE(QStringList({speakerOf(2), speakerOf(3), speakerOf(4)}), before);
        QVERIFY(!m_vm->canUndo());

        // A kijelölésen kívüli sor nevére kattintva megint csak az az egy sor a hatókör.
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

    // A sorról nyitott panelen a teljes beszélő KIFEJEZETT választás: minden sora megy, és ha
    // a cél már beszélője a meetingnek (összevonás), előbb számokkal megerősítést kér.
    void wholeSpeakerScope_movesAll_andAsksBeforeMerge()
    {
        load();
        const QString key = speakerOf(0);
        const int lines = linesOf(key);
        const QString feher = keyOfName(QStringLiteral("Fehér Ádám"));
        const int feherLines = linesOf(feher);
        const int speakers = m_vm->speakerCount();

        // 1) Átnevezés egy, a meetingen még nem szereplő személyre: nem kérdez, minden sora megy.
        click(center(nameOf(0)));
        QTRY_VERIFY(popupOpen("linePopover"));
        click(center(visual("scopeSpeaker")));
        QCOMPARE(popover()->property("scope").toString(), QStringLiteral("speaker"));
        QCOMPARE(visual("popoverTitle")->property("text").toString(), QStringLiteral("Kovács Lilla valójában…"));
        QCOMPARE(visual("popoverFooter")->property("text").toString(),
                 QStringLiteral("A teljes beszélőre vonatkozik: mind az %1 sor · Ctrl+Z visszavonja").arg(lines));   // 52: „az"
        QVERIFY(visual("fixVoiceprints"));                  // csak itt: a hanglenyomat-jelölő
        QVERIFY(visual("mergeList"));                       // és az összevonás-lista
        type(QStringLiteral("molnar"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popupGone("linePopover"));
        QVERIFY(!popupOpen("mergeDialog"));
        QCOMPARE(cell(0, Role::SpeakerNameRole).toString(), QStringLiteral("Molnár Eszter"));
        QCOMPARE(linesOf(key), lines);
        QTRY_VERIFY(barShown());
        QCOMPARE(barText(), QStringLiteral("Kovács Lilla mind az %1 sora átkerült ide: Molnár Eszter").arg(lines));
        QVERIFY(!visual("changeRest"));                     // teljes beszélő után nincs tömeges folytatás
        click(center(visual("changeUndo")));
        QCOMPARE(cell(0, Role::SpeakerNameRole).toString(), QStringLiteral("Kovács Lilla"));
        QVERIFY(!m_vm->canUndo());

        // 2) A cél már beszélő (Fehér Ádám): megerősítés számokkal; „Mégse" → semmi sem változik.
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

        // 3) Ugyanez az „összevonás" listáról, most megerősítve: egy lépésben összeolvad.
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
        undoKey();   // egy lépés
        QCOMPARE(m_vm->speakerCount(), speakers);
        QCOMPARE(linesOf(key), lines);
        QCOMPARE(linesOf(feher), feherLines);
        QVERIFY(!m_vm->canUndo());
    }

    // A beszélő-szintű helyek (sín-fejléc avatar, térkép név) a TELJES beszélő „Miért ő?"
    // paneljét nyitják (handoff-v3 E3) — hatókör-választó nélkül, „Meghallgatás"-sal, okokkal.
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
        QVERIFY(!visual("scopeLine"));                      // nincs mit választani: a teljes beszélő
        QVERIFY(visual("listenButton"));
        QCOMPARE(visual("popoverTitle")->property("text").toString(), QStringLiteral("Fehér Ádám"));
        QTRY_VERIFY(visual("whyBlock"));                    // MIÉRT Ő? — az okok, javítás-linkkel
        QVERIFY(!visuals("whyReason").isEmpty());
        QVERIFY(visual("notPresentLink"));
        // Az összevonás-lista kérésre nyílik.
        QVERIFY(!visual("mergeList"));
        click(center(visual("mergeLink")));
        QTRY_VERIFY(visual("mergeList"));
        QVERIFY(!visuals("speakerChoice").isEmpty());
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("speakerWhyPopover"));

        // A térkép-dokk neve.
        const QList<QQuickItem*> names = visuals("overviewName");
        QCOMPARE(int(names.size()), int(m_vm->overview().size()));
        const QString key = m_vm->overview().at(2).toMap().value(QStringLiteral("key")).toString();
        const int lines = linesOf(key);
        const QString oldName = m_vm->speakerInfo(key).value(QStringLiteral("name")).toString();
        click(names.at(2)->mapToScene(QPointF(12, names.at(2)->height() / 2)).toPoint());
        QTRY_VERIFY(popupOpen("speakerWhyPopover"));
        QCOMPARE(why()->property("speakerKey").toString(), key);
        QVERIFY(!visual("scopeLine"));
        // „Új személy…": személyválasztó; a teljes beszélő a választott névhez kerül (nem
        // meetingbeli személyhez: megerősítés nélkül).
        click(center(visual("newPersonLink")));
        QTRY_VERIFY(popupOpen("personPicker"));
        QTRY_VERIFY(popupGone("speakerWhyPopover"));
        QTest::mouseMove(m_window.get(), QPoint(2, 2));     // a választó sorai a rámutatásra is kiemelnek
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

    // „Ő nem volt ott" (E3): a beszélő sorai névtelenek lesznek — egy visszavonási lépés.
    void speakerWhy_notPresent_makesLinesAnonymous()
    {
        load();
        m_vm->setRailVisible(true);
        pump();
        const QString key = laneKey(1);
        const int lines = linesOf(key);
        click(center(visuals("laneHead").at(1)));
        QTRY_VERIFY(popupOpen("speakerWhyPopover"));
        QVERIFY(visual("fixVoiceprints"));                  // van lenyomata: a téves felismerés jelölője
        click(center(visual("fixVoiceprints")));            // most ne nyúljunk a lenyomathoz
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

    // „Melyik sávon beszél?" (E3): a kézi sáv-beosztás a panelről; egy lépés, visszavonható.
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
        click(center(chips.at(1)));                         // „Hívás hangja"
        QCOMPARE(m_vm->editor()->trackAssignment(key), QStringList{QStringLiteral("loopback")});
        QTRY_VERIFY(visuals("trackChip").at(1)->property("checked").toBool());
        click(center(visuals("trackChip").at(1)));          // újra: a megkötés törlése
        QVERIFY(m_vm->editor()->trackAssignment(key).isEmpty());
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("speakerWhyPopover"));
        undoKey();
        QCOMPARE(m_vm->editor()->trackAssignment(key), QStringList{QStringLiteral("loopback")});
        undoKey();
        QVERIFY(m_vm->editor()->trackAssignment(key).isEmpty());
    }

    // T3: a sáv-chipek kapcsolók — több sáv is jelölhető, minden kattintás egy visszavonási lépés.
    void speakerWhy_multiTrackToggle()
    {
        load(QStringLiteral("v3"));
        m_vm->setRailVisible(true);
        pump();
        const QString key = laneKey(1);
        click(center(visuals("laneHead").at(1)));
        QTRY_VERIFY(popupOpen("speakerWhyPopover"));
        QTRY_VERIFY(visual("tracksBlock"));
        click(center(visuals("trackChip").at(1)));          // hívás
        QCOMPARE(m_vm->editor()->trackAssignment(key), QStringList{QStringLiteral("loopback")});
        click(center(visuals("trackChip").at(0)));          // + mikrofon: mindkettő
        QCOMPARE(m_vm->editor()->trackAssignment(key),
                 (QStringList{QStringLiteral("mic"), QStringLiteral("loopback")}));
        QTRY_VERIFY(visuals("trackChip").at(0)->property("checked").toBool());
        QVERIFY(visuals("trackChip").at(1)->property("checked").toBool());
        click(center(visuals("trackChip").at(1)));          // − hívás
        QCOMPARE(m_vm->editor()->trackAssignment(key), QStringList{QStringLiteral("mic")});
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("speakerWhyPopover"));
        undoKey();
        QCOMPARE(m_vm->editor()->trackAssignment(key),
                 (QStringList{QStringLiteral("mic"), QStringLiteral("loopback")}));
        undoKey();
        QCOMPARE(m_vm->editor()->trackAssignment(key), QStringList{QStringLiteral("loopback")});
    }

    // T2: gyors „+ #Nordvik" a „Miért ő?" címke-sorában és a jelölt-sorokon: egy kattintás
    // ráteszi, visszavonható toast jön (undoKey "tags"), az ajánlat eltűnik; a jelölt nem választódik.
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
        QVERIFY(visual("fixLink"));                         // a „Címkéi" link marad
        click(center(visual("quickTagLink")));
        QVERIFY(m_vm->editor()->personTagIds(QStringLiteral("Molnár Eszter")).contains(QStringLiteral("nordvik")));
        QVERIFY(m_shell->property("toasts").toString().contains(QStringLiteral("Molnár Eszter címkéje lett: #Nordvik")));
        QTRY_VERIFY(!visual("quickTagLink"));
        QVERIFY(popupOpen("speakerWhyPopover"));            // a panel nyitva marad
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("speakerWhyPopover"));
    }

    // T2 a sor-panelen (E1): a jelölt chipje ráteszi a címkét, a sort NEM helyezi át.
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
        QVERIFY(m_vm->editor()->personTagIds(QStringLiteral("Molnár Eszter")).contains(QStringLiteral("nordvik")));
        QCOMPARE(speakerOf(2), before);
        QVERIFY(popupOpen("linePopover"));
        QVERIFY(m_shell->property("toasts").toString().contains(QStringLiteral("#Nordvik")));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("linePopover"));
    }

    // D3: a sor-panel alján halk „Hanglenyomat-minta ebből a sorból (N mp)" sor; kattintásra minta.
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
        // A teljes beszélő hatókörén a sor nem látszik (ott a hanglenyomat-panel él).
        click(center(visual("scopeSpeaker")));
        QTRY_VERIFY(!visual("lineSampleRow"));
        click(center(visual("scopeLine")));
        QTRY_VERIFY(visual("lineSampleRow"));
        click(center(visual("lineSampleRow")));
        QTRY_VERIFY(popupGone("linePopover"));
        QTRY_COMPARE(printCount(person), before + 1);
        QVERIFY(m_shell->property("toasts").toString().contains(QStringLiteral("Hanglenyomat-minta készült")));
    }

    // T1: az új személyhez hasonló sorok accent jelei a térkép-dokkon (MapDock.similarMarks).
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

    // A sáv „Hasonló N sor is" és „Megmutatom" művelete (rejtett sín mellett, a névről indulva).
    void changeBar_similarLines_oneUndoStep()
    {
        load();
        QVERIFY(!m_vm->railVisible());
        reveal(6);
        // A 6. sor nyersen „Távoli 1", hangra Fehér Ádám: a névre kattintva hozzá kerül.
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

        // „Megmutatom": a sín bekapcsol, a javasolt sorok kiemelve; újra: elrejti.
        click(center(visual("changeShow")));
        QVERIFY(m_vm->suggestionShown());
        QVERIFY(m_vm->railVisible());
        QVERIFY(barShown());
        click(center(visual("changeShow")));
        QVERIFY(!m_vm->suggestionShown());

        // „Hasonló N sor is": pontosan a javasolt sorok mennek, egyetlen további lépésben.
        click(center(visual("changeSimilar")));
        QCOMPARE(linesOf(source), sourceLines - 1 - similar);
        QVERIFY(!m_vm->suggestionActive());
        QTRY_VERIFY(barShown());
        QCOMPARE(barText(), QStringLiteral("%1 sor átkerült ide: Fehér Ádám").arg(similar));
        QVERIFY(!visual("changeSimilar"));
        click(center(visual("changeUndo")));                // csak a hasonló sorok jönnek vissza
        QCOMPARE(linesOf(source), sourceLines - 1);
        QCOMPARE(speakerOf(6), feher);
        undoKey();
        QCOMPARE(linesOf(source), sourceLines);
        QVERIFY(!m_vm->canUndo());
    }

    // A sáv „<Forrás> mind a N sora" folytatása: megerősítést kér, majd a forrás MEGMARADT
    // sorai egyetlen további lépésben a célhoz kerülnek.
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

        click(railPoint(6, cell(2, Role::LaneRole).toInt()));       // kattintás Fehér Ádám oszlopába
        QCOMPARE(speakerOf(6), feher);
        QTRY_VERIFY(barShown());
        QCOMPARE(m_vm->changeRestCount(), sourceLines - 1);
        QQuickItem* rest = visual("changeRest");
        QVERIFY(rest);
        QCOMPARE(rest->property("text").toString(),
                 QStringLiteral("%1 mind a %2 sora").arg(sourceName).arg(sourceLines - 1));

        // „Mégse": semmi sem változik, a sáv marad.
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

        // Egy visszavonás: csak a tömeges lépés; az elsőként javított sor a helyén marad.
        undoKey();
        QCOMPARE(m_vm->speakerCount(), speakers);
        QCOMPARE(linesOf(source), sourceLines - 1);
        QCOMPARE(speakerOf(6), feher);
        undoKey();
        QCOMPARE(speakerOf(6), source);
        QVERIFY(!m_vm->canUndo());
    }

    // Az „Imént" sor (S2) FIX helyen van: nem tűnik el időre; a × csak az ajánlatokat zárja, a
    // következő szerkesztés lecseréli a szövegét — a visszavonhatóság ettől független.
    void changeBar_staysCloseAndReplace()
    {
        load();
        QVERIFY(!imentShown());                             // Olvasás: nincs döntés, nincs sor
        m_tab->setProperty("fixMode", true);
        pump();
        QVERIFY(imentShown());                              // Javítás: mindig ott (üres állapottal)
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
        click(railPoint(1, 1));                             // következő szerkesztés: új szöveg
        QVERIFY(barShown());
        QVERIFY(m_vm->changeSerial() > serial);
        pump(1500);                                         // nincs időzítő: marad
        QVERIFY(barShown());
        QCOMPARE(barBottom(), barY);                        // az alja fix (a figyelmeztetés felfelé nő)

        click(center(visual("changeClose")));
        QVERIFY(!m_vm->changeActive());
        QVERIFY(imentShown());                              // az „Imént" sor marad, az utolsó döntéssel
        QVERIFY(m_vm->lastDecision().value(QStringLiteral("text")).toString().contains(QStringLiteral("átkerült")));
        QCOMPARE(textOf("changeText"), m_vm->lastDecision().value(QStringLiteral("text")).toString());
        QVERIFY(m_vm->canUndo());

        // Résztvevő felvétele (más szerkesztés) megszünteti a régi értesítést; a sor marad.
        click(railPoint(3, 2));
        QVERIFY(barShown());
        m_vm->addParticipant(QStringLiteral("Zita"));
        QVERIFY(!barShown());
        QVERIFY(imentShown());
    }

    // Az Átnézendő nézet (E2) „Egyenként" sorai: a javított sor a helyén marad („javítva"),
    // így a következő kattintás nem egy másik sorra esik; a csoport összecsukásakor tűnik el.
    // „Más…” a fókusz-kártyán: ugyanaz a soronkénti panel, mint a névről; a döntött sor a
    // munkalistán a helyén marad, a kártya a következő nyitott sorra lép.
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
        QTest::keyClick(m_window.get(), Qt::Key_Down);      // az első javasolt jelölt
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popupGone("linePopover"));
        QVERIFY(m_vm->lineInfo(id).value(QStringLiteral("speakerKey")).toString() != from);
        QCOMPARE(linesOf(from), fromLines - 1);             // csak ez az egy sor
        QTRY_VERIFY(barShown());

        // A sor ugyanott van a munkalistán, eldöntve; a kártya már a következőn.
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

        // Új név begépelve → „Új személy" sor → Enter: új (üres) oszlop.
        for (const QChar c : QStringLiteral("Zalan"))
            QTest::keyClick(m_window.get(), c.toLatin1(), c.isUpper() ? Qt::ShiftModifier : Qt::NoModifier);
        pump();
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(!popupOpen("personPicker"));
        QCOMPARE(int(m_vm->lanes().size()), lanes + 1);
        const QVariantMap added = m_vm->lanes().last().toMap();
        QCOMPARE(added.value(QStringLiteral("name")).toString(), QStringLiteral("Zalan"));
        QCOMPARE(added.value(QStringLiteral("utteranceCount")).toInt(), 0);

        // Egy sor áthelyezése az új oszlopba kattintással.
        click(railPoint(1, lanes));
        QCOMPARE(cell(1, Role::SpeakerNameRole).toString(), QStringLiteral("Zalan"));
    }

    void reviewView_listenConfirmAndStep()
    {
        load();
        openReview();
        const QString id = focusLine();
        const QVariantMap info = m_vm->lineInfo(id);
        // A kártya lejátszás-gombja: a sor a határaira (+ 0,3 mp ráhagyás).
        m_player->setProperty("log", QString());
        click(center(visual("reviewFocusPlay")));
        QCOMPARE(m_player->property("log").toString(),
                 QStringLiteral("range:%1-%2;").arg(info.value(QStringLiteral("startMs")).toInt())
                                              .arg(info.value(QStringLiteral("endMs")).toInt() + 200));

        // „Jó így": a sor megerősítve, a kártya továbblép.
        const int before = m_vm->uncertainCount();
        click(center(visual("reviewFocusConfirm")));
        QCOMPARE(m_vm->uncertainCount(), before - 1);
        QVERIFY(m_vm->editor()->utterance(id).confirmed);
        QVERIFY(m_vm->canUndo());
        QVERIFY(focusLine() != id);

        // B: a következő nyitott sor; Shift+B: az előző sor.
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

        // Le-nyíl kiemeli az első személyt → Enter őt választja.
        QTest::keyClick(m_window.get(), Qt::Key_Down);
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popupGone("linePopover"));
        QVERIFY(cell(0, Role::SpeakerNameRole).toString() != QStringLiteral("Kovács Lilla"));
        QVERIFY(m_vm->canUndo());
        m_vm->undo();

        // A teljes beszélő panelje (a sín-fejlécről nyitva): az Enter ott sem csinál semmit.
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

    // Névsor nélküli (folytató) sornál a helyi menü „Más mondta…" pontja nyitja ugyanazt a
    // soronkénti panelt.
    void contextMenu_fixLineWithoutNameRow()
    {
        load();
        QVERIFY(!cell(4, Role::HeadRole).toBool());         // nincs névsora: nincs mire kattintani
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

    // „Meghallgatás": a sorról nyitott panelen maga a sor szól; a beszélő-szintű panelen egy
    // jellemző sor (a panel nyitva marad).
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
        // A szomszéd-határok a lejátszónak (elő-/utó-futás korlátja): az első sor előtt nincs sor,
        // a következő sor kezdete legalább a sor vége.
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

    // Másolás: Ctrl+C a kijelölt sorokat teszi a vágólapra (név + időbélyeg + szöveg); jobb
    // gombbal helyi menü; a kattintás-kijelölés és az időbélyeg-ugrás változatlan.
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

        // Több sor: beszélőváltáskor fejsor, a sorok szövege sorban.
        click(textPoint(3), Qt::ShiftModifier);
        QCOMPARE(m_vm->selectedCount(), 3);
        QTest::keyClick(m_window.get(), Qt::Key_C, Qt::ControlModifier);
        const QString many = QGuiApplication::clipboard()->text();
        for (int r = 1; r <= 3; ++r) QVERIFY(many.contains(cell(r, Role::TextRole).toString()));
        QVERIFY(many.indexOf(cell(1, Role::TextRole).toString()) < many.indexOf(cell(3, Role::TextRole).toString()));
        QCOMPARE(m_vm->selectionText(), many);

        // Jobb gomb: helyi menü; a kijelölés nem változik tőle.
        QTest::mouseClick(m_window.get(), Qt::RightButton, Qt::NoModifier, textPoint(2));
        QTRY_VERIFY(popupOpen("rowMenu"));
        QCOMPARE(m_vm->selectedCount(), 3);
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(!popupOpen("rowMenu"));
        QTRY_VERIFY(m_tab->hasActiveFocus());                // a menü zárása után a fókusz visszajön

        // Ctrl+A: minden sor; a teljes átirat másolható.
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

    // „Következő átnézendő": a B billentyű az Átnézendő nézetre vált, és ott a csoportok sorain
    // lépked (Shift+B visszafelé); szövegmezőbe gépelve a betű nem lép.
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
        QVERIFY(m_vm->railVisible());                        // a Javítás sínje
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

        // A keresőmezőben a „b" betű szöveg, nem ugrás; a Ctrl+Z ott a mezőé.
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

    // A következő / előző SOR (nem rés) a listában.
    int lineAfter(int row, int dir) const
    {
        for (int r = row + dir; r >= 0 && r < m_vm->rows()->rowCount(); r += dir)
            if (!m_vm->rowUtteranceId(r).isEmpty()) return r;
        return -1;
    }
    QString playerLog() const { return m_player->property("log").toString(); }

    // Sor-léptetés állva: a nyilak (←/↑ előző, →/↓ következő) és a térkép-dokk gombjai csak
    // kijelölnek és megmutatnak; nincs tekerés, nincs lejátszás.
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
        // Állva: minden lépés a sor elejére teker (egy kurzor), lejátszás nélkül.
        const QString s2 = QStringLiteral("seek:%1;").arg(m_vm->rowStartMs(2));
        const QString sn = QStringLiteral("seek:%1;").arg(m_vm->rowStartMs(next));
        QCOMPARE(playerLog(), sn + s2 + sn + s2 + sn + s2);
        QVERIFY(!m_player->property("playing").toBool());

        // A lista nem görget magától a nyilakra (a kijelölt sor látható marad).
        const qreal y = m_list->property("contentY").toReal();
        QTest::keyClick(m_window.get(), Qt::Key_Right);
        QTest::keyClick(m_window.get(), Qt::Key_Left);
        pump();
        QCOMPARE(m_list->property("contentY").toReal(), y);
    }

    // Egy kurzor (állva): a sor kiválasztása a sor elejére teker — egyetlen seek, lejátszás
    // nélkül —, és a Szóköz innen indít. Ha a pozíció már a soron belül van, nincs tekerés.
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
        QCOMPARE(m_vm->currentRow(), 3);                     // nincs visszacsatolás

        m_player->setProperty("log", QString());
        click(textPoint(3));                                 // ugyanaz a sor: a pozíció benne van
        QVERIFY(!playerLog().contains(QStringLiteral("seek:")));

        // Szóköz: a kijelölt sor elejéről szól.
        m_player->setProperty("log", QString());
        QTest::keyClick(m_window.get(), Qt::Key_Space);
        QVERIFY(m_player->property("playing").toBool());
        QCOMPARE(m_player->property("positionMs").toInt(), start);
        QVERIFY(!playerLog().contains(QStringLiteral("seek:")));
    }

    // Lejátszás közben a lejátszó alatti sor a kijelölt, ahogy a pozíció halad.
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
        QVERIFY(!playerLog().contains(QStringLiteral("seek:")));   // a követés nem teker
    }

    // A térkép-dokk ugrása a pozíció alatti sort jelöli ki (állva és lejátszás közben is).
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

    // Lejátszás közben egy másik sorra kattintva a lejátszás oda ugrik és szól tovább.
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

    // Lejátszás közben: a lejátszó alatti sortól lép, a cél elejére teker (sima seek, nem
    // playRange), és szól tovább.
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

        // A dokk „Előző sor” gombja: a most szóló sor előtti sor eleje.
        m_player->setProperty("log", QString());
        QQuickItem* prevButton = visual("prevLineButton");
        QVERIFY(prevButton);
        click(center(prevButton));
        QCOMPARE(playerLog(), QStringLiteral("seek:%1;").arg(m_vm->rowStartMs(playingRow)));
        QVERIFY(m_player->property("playing").toBool());
        QCOMPARE(m_vm->currentRow(), playingRow);
        QVERIFY(!playerLog().contains(QStringLiteral("range:")));
    }

    // Az Átnézendő nézetben a nyilak a csoportok sorain lépnek (mint a B).
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
        // Az Átnézendőben ↓ / ↑ a munkalista következő / előző sora (a → itt „Később”).
        QTest::keyClick(m_window.get(), Qt::Key_Down);
        pump();
        QVERIFY(view->property("currentUtterance").toString() != first);
        QTest::keyClick(m_window.get(), Qt::Key_Up);
        pump();
        QCOMPARE(view->property("currentUtterance").toString(), first);
        QCOMPARE(viewMode(), QStringLiteral("review"));
    }

    // Olvasás · Javítás · Átnézendő (N): a szegmens három eleme; az Átnézendő a Javítás része, és
    // kilépve a belépés előtti mód jön vissza; Ctrl+E Olvasás ↔ Javítás (az Átnézendőből Olvasás).
    void viewModes_segmentAndReturn()
    {
        load();
        QVERIFY(QTest::qWaitFor([this] { return !m_vm->reviewRunning() && m_vm->reviewCount() > 0; }, 10000));
        setViewMode("fix");
        QCOMPARE(viewMode(), QStringLiteral("fix"));
        QQuickItem* review = visual("modeReview");
        QVERIFY(review);
        QCOMPARE(textOf("reviewCount"), QString::number(m_vm->reviewCount()));
        QVERIFY(!visual("uncertainChip"));                   // Javításkor csak a szegmens
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
        QVERIFY(visual("uncertainChip"));                    // Olvasáskor a csendes „N átnézendő sor"

        // Kilépés nem a szegmensen át (pl. időbélyeg-ugrás): a belépés előtti mód jön vissza.
        setViewMode("fix");
        m_vm->setUncertainOnly(true);
        pump();
        QCOMPARE(viewMode(), QStringLiteral("review"));
        m_vm->setUncertainOnly(false);
        pump();
        QCOMPARE(viewMode(), QStringLiteral("fix"));
        setViewMode("read");
        m_vm->setUncertainOnly(true);                        // pl. az újraellenőrzés eredménye
        pump();
        QCOMPARE(viewMode(), QStringLiteral("review"));
        m_vm->setUncertainOnly(false);
        pump();
        QCOMPARE(viewMode(), QStringLiteral("read"));

        // Ctrl+E: Olvasás ↔ Javítás; az Átnézendőből az Olvasásba.
        QTest::keyClick(m_window.get(), Qt::Key_E, Qt::ControlModifier);
        QCOMPARE(viewMode(), QStringLiteral("fix"));
        setViewMode("review");
        QCOMPARE(viewMode(), QStringLiteral("review"));
        QTest::keyClick(m_window.get(), Qt::Key_E, Qt::ControlModifier);
        QCOMPARE(viewMode(), QStringLiteral("read"));
        QTest::keyClick(m_window.get(), Qt::Key_E, Qt::ControlModifier);
        QCOMPARE(viewMode(), QStringLiteral("fix"));
    }

    // 0 átnézendő sor: az Átnézendő szegmens 0-t mutat, és a nézet az üres állapot
    // (felirat + „Beszélők újraellenőrzése…"), nem üres lista.
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

    // Szövegmezőbe gépelve a szerkesztő billentyűi (1–9, Szóköz, Ctrl+Y) nem sülnek el.
    void typingInSearchNeverTriggersEditorKeys()
    {
        load();
        m_vm->setRailVisible(true);
        click(textPoint(1));                                 // a kurzor a sorra teker (egy kurzor)
        m_player->setProperty("log", QString());
        const QString speaker = speakerOf(1);
        QMetaObject::invokeMethod(m_tab, "openSearch");
        pump();
        QTest::keyClick(m_window.get(), Qt::Key_2);
        QTest::keyClick(m_window.get(), Qt::Key_Space);
        QTest::keyClick(m_window.get(), Qt::Key_1);
        pump();
        QCOMPARE(m_vm->searchQuery(), QStringLiteral("2 1"));
        QCOMPARE(speakerOf(1), speaker);                     // nem került át másik oszlopba
        QCOMPARE(m_player->property("log").toString(), QString());   // a Szóköz nem indított lejátszást
        QVERIFY(!m_vm->canUndo());
    }

    // Az áttekintő minden során ujjlenyomat-jel: van / nincs / névtelen. Kattintásra a
    // hanglenyomat panelje: állapot, használható anyag, készítés, és a most készült visszavonása.
    void overviewVoiceprint_indicatorAndPopover()
    {
        load();
        const QString lilla = keyOfName(QStringLiteral("Kovács Lilla"));
        const QString anon = keyOfName(QStringLiteral("Távoli 1"));
        QCOMPARE(int(visuals("overviewVoiceprint").size()), int(m_vm->overview().size()));
        QCOMPARE(markState(lilla), QStringLiteral("has"));
        QCOMPARE(markState(anon), QStringLiteral("anonymous"));
        // A jel nem tolja el az idővonalat: a név-oszlopon belül áll.
        QQuickItem* mark = markOf(lilla);
        QVERIFY(mark->x() + mark->width() <= 120);

        // Névtelen beszélőnél nem nyit semmit (a súgó megmondja, miért).
        click(center(markOf(anon)));
        QVERIFY(!popupOpen("voiceprintPopover"));

        // 1) Akinek van: kimondja, és új minta készíthető.
        click(center(mark));
        QTRY_VERIFY(popupOpen("voiceprintPopover"));
        QVERIFY(!popupOpen("speakerWhyPopover"));
        QCOMPARE(textOf("voiceprintName"), QStringLiteral("Kovács Lilla"));
        QCOMPARE(textOf("voiceprintState"), QStringLiteral("Van hanglenyomata"));
        QVERIFY2(textOf("voiceprintDetail").contains(QStringLiteral("hosszabb sora használható fel")),
                 qPrintable(textOf("voiceprintDetail")));
        QCOMPARE(textOf("voiceprintCreate"), QStringLiteral("Új minta készítése"));
        QCOMPARE(printCount(QStringLiteral("Kovács Lilla")), 1);     // megnyitásra semmi sem készül
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("voiceprintPopover"));

        // 2) Elnevezett, még lenyomat nélküli beszélő, elég anyaggal: itt készíthető.
        QVERIFY(m_vm->reassignSpeaker(anon, QStringLiteral("Bálint Péter"), false));
        m_vm->dismissChange();
        QTRY_COMPARE(markState(anon), QStringLiteral("none"));
        pump();     // az áttekintő sorai újraépültek: a helyük a következő elrendezés után végleges
        click(center(markOf(anon)));
        QTRY_VERIFY(popupOpen("voiceprintPopover"));
        QCOMPARE(textOf("voiceprintState"), QStringLiteral("Még nincs hanglenyomata"));
        QCOMPARE(textOf("voiceprintCreate"), QStringLiteral("Hanglenyomat készítése"));
        QVERIFY(!visual("voiceprintUndo"));
        QCOMPARE(printCount(QStringLiteral("Bálint Péter")), 0);
        click(center(visual("voiceprintCreate")));
        QCOMPARE(printCount(QStringLiteral("Bálint Péter")), 1);
        QTRY_COMPARE(markState(anon), QStringLiteral("has"));       // a jelző frissül
        QCOMPARE(textOf("voiceprintState"), QStringLiteral("Van hanglenyomata"));
        QVERIFY2(textOf("voiceprintResult").startsWith(QStringLiteral("Elkészült ")), qPrintable(textOf("voiceprintResult")));
        QVERIFY(textOf("voiceprintResult").contains(QStringLiteral("mp beszédből")));
        QVERIFY(!visual("voiceprintCreate"));
        // …és a most készült visszavonható: pontosan az törlődik.
        QVERIFY(visual("voiceprintUndo"));
        click(center(visual("voiceprintUndo")));
        QCOMPARE(printCount(QStringLiteral("Bálint Péter")), 0);
        QTRY_COMPARE(markState(anon), QStringLiteral("none"));
        QCOMPARE(textOf("voiceprintResult"), QStringLiteral("A most készült hanglenyomat törölve."));
        QCOMPARE(textOf("voiceprintCreate"), QStringLiteral("Hanglenyomat készítése"));
        QCOMPARE(m_vm->speakerInfo(anon).value(QStringLiteral("name")).toString(), QStringLiteral("Bálint Péter"));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("voiceprintPopover"));

        // 3) Kevés anyag: megmondja, mennyi hiányzik és mi segít — gomb nincs.
        const QString id = m_vm->rowInfo(3).value(QStringLiteral("utteranceId")).toString();
        QVERIFY(m_vm->moveUtteranceToPerson(id, QStringLiteral("Ördög Ödön")));
        m_vm->dismissChange();
        const QString odon = keyOfName(QStringLiteral("Ördög Ödön"));
        QTRY_COMPARE(markState(odon), QStringLiteral("none"));
        pump();     // az áttekintő sorai újraépültek: a helyük a következő elrendezés után végleges
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

    // 11 beszélő: az „Egyéb (N)" sornak nincs jele; a névre kattintva kibomlik, és a keveset
    // beszélők is saját jelet (és panelt) kapnak; vissza is csukható.
    void overviewVoiceprint_collapsedSpeakersAfterExpanding()
    {
        load(QStringLiteral("many"));
        QVERIFY(m_vm->collapsedCount() > 0);
        const int shown = int(m_vm->overview().size());
        QCOMPARE(int(visuals("overviewVoiceprint").size()), shown - 1);
        const QString gergo = keyOfName(QStringLiteral("Horváth Gergő"));
        QVERIFY(!markOf(gergo));
        // A sáv-fejléc pöttye ugyanazt az állapotot mutatja (elnevezett beszélőnél látszik).
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

    // A sorról nyitott panelben a hanglenyomat-blokk csak a „<Név> minden sora" hatókörben
    // látszik — ott ugyanúgy készíthető (és visszavonható), mint a teljes beszélő paneljében.
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
        // A blokk és a lábléc az ablakon belül marad (alacsony ablakban a listák rövidebbek).
        QQuickItem* footer = visual("popoverFooter");
        QVERIFY(footer->mapToScene(QPointF(0, footer->height())).y() <= m_window->height());
        QCOMPARE(printCount(QStringLiteral("Kovács Lilla")), 1);
        click(center(visual("voiceprintCreate")));
        QCOMPARE(printCount(QStringLiteral("Kovács Lilla")), 2);
        QVERIFY(popupOpen("linePopover"));               // a panel nyitva marad: visszavonható
        QVERIFY(textOf("voiceprintResult").startsWith(QStringLiteral("Elkészült ")));
        click(center(visual("voiceprintUndo")));
        QCOMPARE(printCount(QStringLiteral("Kovács Lilla")), 1);
        QVERIFY(!m_vm->canUndo());                          // átsorolás nem történt
        // Vissza a soronkénti hatókörre: a blokk eltűnik.
        click(center(visual("scopeLine")));
        QVERIFY(!visual("voiceprintBlock"));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("linePopover"));

        // A teljes beszélő „Miért ő?" paneljéből (térkép név) a „Minták" link a hanglenyomat
        // paneljét nyitja, ugyanezzel a blokkal.
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
        click(railPoint(3, 1));                             // egy sor: nincs ajánlat
        QTRY_VERIFY(barShown());
        QVERIFY(visual("changeUndo"));
        QVERIFY(!visual("changeVoiceprint"));
        undoKey();
        QVERIFY(!barShown());

        // A névtelen beszélő nevet kap az áttekintő nevéről (a személynek nincs lenyomata).
        const QString anon = keyOfName(QStringLiteral("Távoli 1"));
        QQuickItem* anonName = nullptr;
        for (QQuickItem* item : visuals("overviewName"))
            if (item->property("text").toString() == QStringLiteral("Távoli 1")) anonName = item;
        QVERIFY(anonName);
        click(anonName->mapToScene(QPointF(12, anonName->height() / 2)).toPoint());
        QTRY_VERIFY(popupOpen("speakerWhyPopover"));
        click(center(visual("newPersonLink")));             // „Új személy…" → személyválasztó
        QTRY_VERIFY(popupOpen("personPicker"));
        QTest::mouseMove(m_window.get(), QPoint(2, 2));
        type(QStringLiteral("balint"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(!popupOpen("personPicker"));
        QCOMPARE(m_vm->speakerInfo(anon).value(QStringLiteral("name")).toString(), QStringLiteral("Bálint Péter"));
        QTRY_VERIFY(barShown());
        QVERIFY(visual("changeVoiceprint"));
        QCOMPARE(textOf("changeVoiceprint"), QStringLiteral("Hanglenyomat készítése"));
        QCOMPARE(printCount(QStringLiteral("Bálint Péter")), 0);    // magától nem készül
        QTRY_COMPARE(markState(anon), QStringLiteral("none"));

        click(center(visual("changeVoiceprint")));
        QCOMPARE(printCount(QStringLiteral("Bálint Péter")), 1);
        QVERIFY(barShown());
        QVERIFY2(barText().startsWith(QStringLiteral("Hanglenyomat készült: Bálint Péter (")), qPrintable(barText()));
        QCOMPARE(textOf("changeText"), barText());
        QVERIFY(!visual("changeVoiceprint"));
        QTRY_COMPARE(markState(anon), QStringLiteral("has"));

        // Visszavonás a sávon: a lenyomat törlődik, az elnevezés marad (az Ctrl+Z-vel megy).
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

        // Olyan személybe olvasztva, akinek már van lenyomata: nincs ajánlat.
        QVERIFY(m_vm->mergeSpeakers(anon, keyOfName(QStringLiteral("Fehér Ádám"))));
        QTRY_VERIFY(barShown());
        QVERIFY(!visual("changeVoiceprint"));
    }

    // Két hasonló hang: a sáv második sora ajánlja a kettejük átnézését (Átnézés / Most nem);
    // a „Ki mondta?" panel teljes-beszélő hatókörében ugyanez kézzel, a párt választva.
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

        // „Most nem": az ajánlat eltűnik, az átsorolás értesítése marad.
        pump(100);      // a megnőtt sáv elrendezése
        click(center(visual("pairOfferDecline")));
        QVERIFY(!m_vm->pairOfferActive());
        QVERIFY(barShown());
        QTRY_VERIFY(!visual("pairOffer"));
        m_vm->undo();

        // „Átnézés": lefut (egy visszavonási lépés), a sáv helyén értesítés.
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

        // A panel: „Átnézés másik beszélővel…" → a pár kiválasztása → lefut.
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

    // Hangmodell nélkül: nincs halott gomb — a panel egyszer megmondja az okot, a sáv nem ajánl.
    void withoutVoiceModel_noDeadVoiceprintButton()
    {
        load(QStringLiteral("novoice"));
        const QString anon = keyOfName(QStringLiteral("Távoli 1"));
        QVERIFY(m_vm->reassignSpeaker(anon, QStringLiteral("Bálint Péter"), false));
        QTRY_VERIFY(barShown());
        QVERIFY(!visual("changeVoiceprint"));
        m_vm->dismissChange();
        QTRY_COMPARE(markState(anon), QStringLiteral("none"));
        pump();     // az áttekintő sorai újraépültek: a helyük a következő elrendezés után végleges
        click(center(markOf(anon)));
        QTRY_VERIFY(popupOpen("voiceprintPopover"));
        QCOMPARE(textOf("voiceprintState"), QStringLiteral("Még nincs hanglenyomata"));
        QCOMPARE(textOf("voiceprintDetail"),
                 QStringLiteral("Nincs letöltve a hangmodell, ezért itt most nem készíthető hanglenyomat."));
        QVERIFY(!visual("voiceprintCreate"));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("voiceprintPopover"));

        // A „Ki mondta?" panel blokkja ugyanígy.
        click(center(nameOf(0)));
        QTRY_VERIFY(popupOpen("linePopover"));
        click(center(visual("scopeSpeaker")));
        QVERIFY(visual("voiceprintBlock"));
        QVERIFY(textOf("voiceprintDetail").startsWith(QStringLiteral("Nincs letöltve a hangmodell")));
        QVERIFY(!visual("voiceprintCreate"));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("linePopover"));
    }

    // E1 a „v3" meetingen: a sor-panel jelöltjei bizonyítékkal, 1–3 billentyű; „Miért nem X?"
    // okai a javítás helyével (Sáv-beosztás → a beszélő „Miért ő?" panelje a sáv-chipekkel).
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
        // A legjobb jelölt bizonyítéka: hang + ugyanaz a sáv + #Nordvik.
        const QVariantList ev = choice("speakerChoice", QStringLiteral("Fehér Gábor"))->property("evidence").toList();
        QStringList texts;
        for (const QVariant& e : ev) texts << e.toMap().value(QStringLiteral("text")).toString();
        QVERIFY2(texts.contains(QStringLiteral("ugyanaz a sáv")) && texts.contains(QStringLiteral("#Nordvik")),
                 qPrintable(texts.join(QLatin1Char('|'))));
        // „Miért nem Kovács Lilla?": a sáv-ok, javítás-linkkel.
        QVERIFY(visual("whyNotBlock"));
        QVERIFY(!visuals("whyNotReason").isEmpty());
        QQuickItem* tracks = nullptr;
        for (QQuickItem* link : visuals("fixLink"))
            if (link->property("text").toString() == QStringLiteral("Sáv-beosztás")) tracks = link;
        QVERIFY(tracks);

        // 1: a javasolt jelölt.
        QTest::keyClick(m_window.get(), Qt::Key_1);
        QTRY_VERIFY(popupGone("linePopover"));
        QCOMPARE(speakerOf(2), gabor);
        QTRY_VERIFY(barShown());
        undoKey();
        QCOMPARE(speakerOf(2), lilla);

        // Sáv-beosztás → a beszélő „Miért ő?" panelje, a sáv-chipekkel.
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

    // E2 a „v3" meetingen: „Mind a N → X" egy lépés (és visszavonható); a szennyezett mag sávjának
    // „Szétválasztás" gombja is egy lépés.
    // A szennyezett mag sávja a fókusz-kártya ALATT (az alsó kontextus-sorok helyén): Szétválasztás
    // = egy visszavonási lépés. (A csoportos „Mind → X” a régi csoport-nézettel megszűnt; a
    // nagy csoportokat az S5 nyitó-javaslata hozza vissza.)
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
        // A sáv a kártya alatt: alacsony nézetben a fókusz-terület saját görgetésével látszik.
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

    // ---- S2: az Átnézendő-újratervezés közös alapja ----------------------------------------


    // A közös billentyű-olvasó a FIZIKAI billentyűt adja: a magyar kiosztáson a Shift+6 „/”, a
    // Shift+1 „'” — a scancode-ból mégis számjegy; a „0” (bal felső) a kiosztás szerinti Key_0.
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
        QCOMPARE(decode(Qt::Key_0, 0, 49, "linux").value("digit").toInt(), 0);   // HU „0” (bal felső)
        QCOMPARE(decode(Qt::Key_6, int(Qt::KeypadModifier), 0, "").value("digit").toInt(), 6);
        QCOMPARE(decode(Qt::Key_Question, shift, 59, "linux").value("name").toString(), QStringLiteral("Comma"));
        QCOMPARE(decode(Qt::Key_M, 0, 58, "linux").value("name").toString(), QStringLiteral("M"));
        k = decode(Qt::Key_J, int(Qt::ControlModifier), 44, "linux");
        QCOMPARE(k.value("name").toString(), QStringLiteral("J"));
        QVERIFY(!k.value("plain").toBool());
        // Natív kód nélkül (macOS) a Shift+6 nem számjegy (nincs találgatás).
        QCOMPARE(decode(Qt::Key_Slash, shift, 0, "").value("name").toString(), QString());
        keys->setProperty("platform", platform);
    }

    // Shift = nem minta: Shift+sín-kattintás és Shift+számjegy — a sor átkerül, „nem minta”,
    // EGY visszavonási lépésben; lenyomott Shiftnél az előnézeti címke „→ Név · nem minta”.
    void shift_railClickAndDigit_nonSample_oneUndoStep()
    {
        load();
        m_tab->setProperty("fixMode", true);
        pump();
        // Rámutatás + lenyomott Shift: a címke utótagot kap.
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
        QVERIFY(!m_vm->canUndo());                          // egy lépés volt

        // Shift+számjegy a kijelölésre (a tesztben Key_2 + Shift; a scancode-utat a decode teszteli).
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

    // Az Átnézendő nézetben is: Shift lenyomva „Jó így · nem minta”, és a kattintás nem-mintaként dönt.
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

    // 0 = nem azonosítható: a Javításban a kijelölésre (újra 0: vissza), jelölő + halvány sor;
    // a lista elrejtheti; az Átnézendőben a kiválasztott sorra és a sor-gombbal.
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
        // A „nem azonosítható” sorok elrejtése a fejlécből.
        QTRY_VERIFY(visual("ignoredToggle"));
        click(center(visual("ignoredToggle")));
        QVERIFY(m_vm->hideIgnored());
        QCOMPARE(m_vm->rows()->rowOfUtterance(idx2), -1);
        click(center(visual("ignoredToggle")));
        QVERIFY(!m_vm->hideIgnored());
        // Újra 0 a kijelölésen: újra azonosítható.
        click(textPoint(2));
        if (!m_vm->isRowSelected(2)) click(textPoint(2));
        QTest::keyClick(m_window.get(), Qt::Key_0);
        QVERIFY(!cell(2, Role::IgnoredRole).toBool());
        QCOMPARE(m_vm->ignoredCount(), 0);

        // Átnézendő: a fókusz-sor 0-ra (a munkalistán a helyén marad), majd a következő a gombbal.
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
        QTRY_COMPARE(focusLine(), other);                   // a fókusz a visszavont sorra
    }

    // Az „Imént” sor: J után „Jó így” + minta-chip; Shift+J nem minta; mini-napló; Ctrl+Z →
    // „Visszavonva: …”; nem tűnik el.
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

    // Eltérő minta (a v3 kitalált meeting hamis embedderével): az „Imént” sor figyelmeztet, és a
    // „Legyen nem minta” a sort nem-mintává teszi (a figyelmeztetés eltűnik).
    void sampleWarning_deviatingSample()
    {
        load(QStringLiteral("v3"));
        m_tab->setProperty("fixMode", true);
        m_vm->applyDemoState(QStringLiteral("imentWarning"));
        QTRY_VERIFY(!m_vm->sampleDeviation().isEmpty());
        const QVariantMap dev = m_vm->sampleDeviation();
        QVERIFY(dev.value(QStringLiteral("pct")).toInt() < 50);
        QTRY_VERIFY(visual("sampleWarning"));
        pump(150);                                          // a megnőtt sor elrendezése
        QVERIFY(textOf("sampleWarningText").contains(dev.value(QStringLiteral("name")).toString()));
        QVERIFY(m_vm->decisionLog().size() >= 3);
        const QString id = dev.value(QStringLiteral("utteranceId")).toString();
        QVERIFY(!noisyOf(id));
        click(center(visual("sampleWarningNoisy")));
        QVERIFY(noisyOf(id));
        QVERIFY(m_vm->sampleDeviation().isEmpty());
        QTRY_VERIFY(!visual("sampleWarning"));
    }

    // Minta-mérő a dokkban: „N minta +k” a sampleStats szerint; a „+k” kattintásra a Javítás a
    // most lett minta-sorokra szűr (Esc: vissza).
    void sampleMeter_valuesAndFilter()
    {
        load();
        m_tab->setProperty("fixMode", true);
        pump();
        // Két hosszabb sor ugyanattól a beszélőtől: „Jó így”.
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
        QTest::keyClick(m_window.get(), Qt::Key_Escape);    // a szűrő
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
                    << QString::fromLatin1(variant) << QString::fromLatin1(state);        // v3 Javítás mód: üres változattal a „v3" kitalált meetingen futnak.
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
        // A v3 állapotok a hang- és a háttér-elemzés után nyitják a paneljeiket.
        QTest::qWait(variant.isEmpty() && tab->property("v3Demo").toBool() ? 2500 : 180);
        tab.reset();
    }
};

QTEST_MAIN(TestTranscriptTab)
#include "test_transcript_tab.moc"
