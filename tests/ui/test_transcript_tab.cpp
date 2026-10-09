// Az „Átirat" fül (TranscriptTab.qml) működése kijelző nélkül (offscreen), a beépített
// KITALÁLT meetingen: egér- és billentyű-események a QML-ablaknak (nem a felhasználó
// asztalának), hamis lejátszóval és shell-lel. Lefedi: Ctrl+L, kattintás másik oszlopba,
// Ctrl+Z, több sor kijelölése + számbillentyű, húzás, időbélyeg → seek, „+" → személyválasztó,
// lejátszás-követés, a Shell pozíció-kérése, „Meghallgatom"; és a javítás hatóköre: név →
// CSAK az a sor (kijelölésnél a kijelölt sorok), teljes beszélő a sáv-fejlécről / áttekintőről,
// összevonás előtt megerősítés, minden átsorolás után értesítő sáv (Visszavonás / Hasonló N
// sor is / „mind a N sora"), és a „Bizonytalan" szűrő nem ugrik el a javított sor alól.
// Hanglenyomat: jelző az áttekintő minden során + saját panel, a blokk a sorról nyitott panel
// teljes-beszélő hatókörében, ajánlat a sávon egy teljes beszélő elnevezése után, és a most
// készült lenyomat visszavonása.
#include "AppContext.h"
#include "QmlApp.h"
#include "TranscriptEditorViewModel.h"
#include "TranscriptListModel.h"

#include <QClipboard>
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
        function playRange(a, b) { log += "range:" + a + "-" + b + ";" }
    }
    QtObject {
        id: fakeShell
        objectName: "shell"
        signal transcriptPositionRequested(int ms)
        property string toasts: ""
        function toast(text) { toasts += text + ";" }
    }
    TranscriptTab {
        objectName: "tab"
        anchors.fill: parent
        player: fakePlayer
        shell: fakeShell
        demoVariant: parent ? variant : ""
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
    // A panel egy választható sora (a meeting beszélője / ismert személy) név szerint.
    QQuickItem* choice(const char* kind, const QString& personName) const
    {
        for (QQuickItem* item : visuals(kind))
            if (item->property("personName").toString() == personName) return item;
        return nullptr;
    }
    QObject* popover() const { return m_window->findChild<QObject*>(QStringLiteral("speakerPopover")); }
    QObject* changeBar() const { return m_window->findChild<QObject*>(QStringLiteral("changeBar")); }
    bool barShown() const { return changeBar() && changeBar()->property("visible").toBool(); }
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
        QTRY_VERIFY(popupOpen("speakerPopover"));
        QCOMPARE(m_tab->property("speakerPopoverRow").toInt(), 2);
        QCOMPARE(popover()->property("scope").toString(), QStringLiteral("line"));
        // A cím és a lábléc kimondja a hatókört és a darabszámot.
        QCOMPARE(visual("popoverTitle")->property("text").toString(), QStringLiteral("Kinek a sora ez?"));
        QCOMPARE(visual("popoverFooter")->property("text").toString(),
                 QStringLiteral("1 sor kerül át · visszavonható: Ctrl+Z"));
        // Soronkénti hatókörben nincs hanglenyomat-jelölő, nincs „összevonás" lista; a meeting
        // többi beszélője áthelyezési célként elöl áll.
        QVERIFY(!visual("fixVoiceprints"));
        QVERIFY(!visual("mergeList"));
        QVERIFY(visual("meetingSpeakers"));
        QVERIFY(choice("speakerChoice", QStringLiteral("Varga Nóra")));
        QVERIFY(!choice("speakerChoice", cell(2, Role::SpeakerNameRole).toString()));

        type(QStringLiteral("varga"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popupGone("speakerPopover"));
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
        QTRY_VERIFY(popupOpen("speakerPopover"));
        type(QStringLiteral("molnar"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popupGone("speakerPopover"));
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
        QTRY_VERIFY(popupOpen("speakerPopover"));
        QCOMPARE(m_vm->selectedCount(), 3);                 // a névre kattintás nem bontja a kijelölést
        QCOMPARE(popover()->property("scope").toString(), QStringLiteral("selection"));
        QVERIFY(visual("scopeSelection"));
        QCOMPARE(visual("popoverTitle")->property("text").toString(), QStringLiteral("Kié a kijelölt 3 sor?"));
        QCOMPARE(visual("popoverFooter")->property("text").toString(),
                 QStringLiteral("3 sor kerül át · visszavonható: Ctrl+Z"));
        click(center(choice("speakerChoice", QStringLiteral("Varga Nóra"))));
        QTRY_VERIFY(popupGone("speakerPopover"));
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
        QTRY_VERIFY(popupOpen("speakerPopover"));
        QCOMPARE(popover()->property("scope").toString(), QStringLiteral("line"));
        QVERIFY(!visual("scopeSelection"));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("speakerPopover"));
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
        QTRY_VERIFY(popupOpen("speakerPopover"));
        click(center(visual("scopeSpeaker")));
        QCOMPARE(popover()->property("scope").toString(), QStringLiteral("speaker"));
        QCOMPARE(visual("popoverTitle")->property("text").toString(), QStringLiteral("Kovács Lilla valójában…"));
        QCOMPARE(visual("popoverFooter")->property("text").toString(),
                 QStringLiteral("Mind az %1 sor átkerül · visszavonható: Ctrl+Z").arg(lines));   // 52: „az"
        QVERIFY(visual("fixVoiceprints"));                  // csak itt: a hanglenyomat-jelölő
        QVERIFY(visual("mergeList"));                       // és az összevonás-lista
        type(QStringLiteral("molnar"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popupGone("speakerPopover"));
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
        QTRY_VERIFY(popupOpen("speakerPopover"));
        click(center(visual("scopeSpeaker")));
        type(QStringLiteral("feher"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popupOpen("mergeDialog"));
        QTRY_VERIFY(popupGone("speakerPopover"));
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
        QTRY_VERIFY(popupOpen("speakerPopover"));
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

    // A beszélő-szintű helyek (sáv-fejléc avatar, áttekintő név) egyből a TELJES beszélő
    // paneljét nyitják — hatókör-választó nélkül, „Meghallgatás"-sal és hanglenyomattal.
    void railAvatar_and_overviewName_openWholeSpeaker()
    {
        load();
        m_vm->setRailVisible(true);
        pump();
        const QList<QQuickItem*> heads = visuals("laneHead");
        QCOMPARE(int(heads.size()), int(m_vm->lanes().size()));
        click(center(heads.at(1)));
        QTRY_VERIFY(popupOpen("speakerPopover"));
        QCOMPARE(popover()->property("scope").toString(), QStringLiteral("speaker"));
        QCOMPARE(popover()->property("speakerKey").toString(), laneKey(1));
        QCOMPARE(popover()->property("utteranceId").toString(), QString());
        QCOMPARE(m_tab->property("speakerPopoverRow").toInt(), -1);
        QVERIFY(!visual("scopeLine"));                      // nincs mit választani: a teljes beszélő
        QVERIFY(visual("listenButton"));
        QVERIFY(visual("mergeList"));
        QCOMPARE(visual("popoverTitle")->property("text").toString(), QStringLiteral("Fehér Ádám valójában…"));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("speakerPopover"));

        // Az áttekintő (idővonal) neve.
        const QList<QQuickItem*> names = visuals("overviewName");
        QCOMPARE(int(names.size()), int(m_vm->overview().size()));
        const QString key = m_vm->overview().at(2).toMap().value(QStringLiteral("key")).toString();
        const int lines = linesOf(key);
        const QString oldName = m_vm->speakerInfo(key).value(QStringLiteral("name")).toString();
        click(names.at(2)->mapToScene(QPointF(12, names.at(2)->height() / 2)).toPoint());
        QTRY_VERIFY(popupOpen("speakerPopover"));
        QCOMPARE(popover()->property("scope").toString(), QStringLiteral("speaker"));
        QCOMPARE(popover()->property("speakerKey").toString(), key);
        QVERIFY(!visual("scopeLine"));
        // Innen a teljes beszélő megy (nem meetingbeli személyhez: megerősítés nélkül).
        type(QStringLiteral("balogh"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popupGone("speakerPopover"));
        QVERIFY(!popupOpen("mergeDialog"));
        QCOMPARE(m_vm->speakerInfo(key).value(QStringLiteral("name")).toString(), QStringLiteral("Balogh Kata"));
        QCOMPARE(linesOf(key), lines);
        QTRY_VERIFY(barShown());
        QVERIFY(barText().startsWith(oldName + QStringLiteral(" mind a")));
        undoKey();
        QCOMPARE(m_vm->speakerInfo(key).value(QStringLiteral("name")).toString(), oldName);
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
        QTRY_VERIFY(popupOpen("speakerPopover"));
        type(QStringLiteral("feher"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popupGone("speakerPopover"));
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

    // A sáv magától eltűnik (~12 mp; itt rövidre állítva), bezárható, és a következő
    // szerkesztés lecseréli — a visszavonhatóság ettől független.
    void changeBar_timeoutCloseAndReplace()
    {
        load();
        m_vm->setRailVisible(true);
        pump();
        QCOMPARE(changeBar()->property("timeoutMs").toInt(), 12000);
        click(railPoint(3, 1));
        QTRY_VERIFY(barShown());
        QVERIFY(!m_vm->suggestionActive());
        const int serial = m_vm->changeSerial();
        click(railPoint(1, 1));                             // következő szerkesztés: új sáv
        QVERIFY(barShown());
        QVERIFY(m_vm->changeSerial() > serial);

        click(center(visual("changeClose")));
        QVERIFY(!barShown());
        QVERIFY(m_vm->canUndo());

        changeBar()->setProperty("timeoutMs", 150);
        click(railPoint(4, 1));
        QVERIFY(barShown());
        QTRY_VERIFY_WITH_TIMEOUT(!barShown(), 3000);
        QVERIFY(m_vm->canUndo());

        // „Jó így" / résztvevő felvétele (más szerkesztés) is megszünteti a régi értesítést.
        changeBar()->setProperty("timeoutMs", 12000);
        click(railPoint(3, 2));
        QVERIFY(barShown());
        m_vm->addParticipant(QStringLiteral("Zita"));
        QVERIFY(!barShown());
    }

    // A „Bizonytalan" szűrőben a javított sor a helyén marad („javítva"), így a következő
    // kattintás nem egy másik sorra esik; a szűrő újbóli alkalmazásakor tűnik el.
    void uncertainFilter_correctedLineStays()
    {
        load();
        m_vm->setUncertainOnly(true);
        pump(60);
        int row = -1;
        for (int r = 0; r < m_vm->rows()->rowCount() && row < 0; ++r)
            if (cell(r, Role::KindRole).toString() == QLatin1String("utterance")) row = r;
        QVERIFY(row >= 0);
        const int rowCount = m_vm->rows()->rowCount();
        const int utterance = cell(row, Role::UtteranceIndexRole).toInt();
        const int nextUtterance = cell(row + 1, Role::UtteranceIndexRole).toInt();
        const QString nextKind = cell(row + 1, Role::KindRole).toString();
        const QString from = speakerOf(row);
        const int fromLines = linesOf(from);
        const int uncertain = m_vm->uncertainCount();
        const qreal y = rowItem(row)->mapToScene(QPointF(0, 0)).y();

        // „Más mondta…": ugyanaz a soronkénti panel, mint a névről.
        QQuickItem* fix = findByText(rowItem(row), QStringLiteral("Más mondta…"));
        QVERIFY(fix);
        click(center(fix));
        QTRY_VERIFY(popupOpen("speakerPopover"));
        QCOMPARE(popover()->property("scope").toString(), QStringLiteral("line"));
        QTest::keyClick(m_window.get(), Qt::Key_Down);      // az első másik beszélő
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popupGone("speakerPopover"));
        QVERIFY(speakerOf(row) != from);
        QCOMPARE(linesOf(from), fromLines - 1);             // csak ez az egy sor
        QCOMPARE(m_vm->uncertainCount(), uncertain - 1);

        // A sor ugyanott van, „javítva" jelöléssel; alatta ugyanaz következik.
        QCOMPARE(m_vm->rows()->rowCount(), rowCount);
        QCOMPARE(cell(row, Role::UtteranceIndexRole).toInt(), utterance);
        QVERIFY(cell(row, Role::CorrectedRole).toBool());
        QVERIFY(!cell(row, Role::UncertainRole).toBool());
        QCOMPARE(cell(row + 1, Role::KindRole).toString(), nextKind);
        QCOMPARE(cell(row + 1, Role::UtteranceIndexRole).toInt(), nextUtterance);
        QTRY_VERIFY(barShown());
        pump(80);
        QVERIFY(rowItem(row) != nullptr);
        QCOMPARE(rowItem(row)->mapToScene(QPointF(0, 0)).y(), y);   // nem ugrott el a kurzor alól
        QVERIFY(!findByText(rowItem(row), QStringLiteral("Jó így")));   // már nincs mit megerősíteni

        // A név ugyanitt újra kattintható (soronként), és a visszavonás is helyben történik.
        click(center(nameOf(row)));
        QTRY_VERIFY(popupOpen("speakerPopover"));
        QCOMPARE(popover()->property("scope").toString(), QStringLiteral("line"));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("speakerPopover"));

        // A szűrő újbóli alkalmazása után a javított sor már nem szerepel.
        m_vm->setUncertainOnly(false);
        m_vm->setUncertainOnly(true);
        pump(60);
        for (int r = 0; r < m_vm->rows()->rowCount(); ++r)
            QVERIFY(cell(r, Role::UtteranceIndexRole).toInt() != utterance);
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

    void uncertainFilter_listenPlaysOneLine()
    {
        load();
        m_vm->setUncertainOnly(true);
        pump(60);
        int row = -1;
        for (int r = 0; r < m_vm->rows()->rowCount() && row < 0; ++r)
            if (cell(r, Role::KindRole).toString() == QLatin1String("utterance")) row = r;
        QVERIFY(row >= 0);
        QQuickItem* listen = findByText(rowItem(row), QStringLiteral("Meghallgatom"));
        QVERIFY(listen);
        click(center(listen));
        const QString expected = QStringLiteral("range:%1-%2;").arg(m_vm->rowStartMs(row)).arg(m_vm->rowEndMs(row));
        QCOMPARE(m_player->property("log").toString(), expected);

        const int before = m_vm->uncertainCount();
        QQuickItem* ok = findByText(rowItem(row), QStringLiteral("Jó így"));
        QVERIFY(ok);
        click(center(ok));
        QCOMPARE(m_vm->uncertainCount(), before - 1);
    }

    // Egy véletlen Enter az üres keresőben SEMMIT sem rendel át a lista első emberéhez (sem
    // a sort, sem — a teljes beszélő hatókörében — a beszélőt); nyíllal kiemelve viszont választ.
    void speakerPopover_strayEnterDoesNothing()
    {
        load();
        QQuickItem* name = rowItem(0)->findChild<QQuickItem*>(QStringLiteral("speakerName"));
        QVERIFY(name);
        click(center(name));
        QTRY_VERIFY(popupOpen("speakerPopover"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTest::keyClick(m_window.get(), Qt::Key_Enter);
        pump();
        QVERIFY(popupOpen("speakerPopover"));
        QCOMPARE(cell(0, Role::SpeakerNameRole).toString(), QStringLiteral("Kovács Lilla"));
        QVERIFY(!m_vm->canUndo());

        // Le-nyíl kiemeli az első személyt → Enter őt választja.
        QTest::keyClick(m_window.get(), Qt::Key_Down);
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popupGone("speakerPopover"));
        QVERIFY(cell(0, Role::SpeakerNameRole).toString() != QStringLiteral("Kovács Lilla"));
        QVERIFY(m_vm->canUndo());
        m_vm->undo();

        // Ugyanez a teljes beszélő hatókörében (a sáv-fejlécről nyitva).
        m_vm->setRailVisible(true);
        pump();
        click(center(visuals("laneHead").at(0)));
        QTRY_VERIFY(popupOpen("speakerPopover"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTest::keyClick(m_window.get(), Qt::Key_Enter);
        pump();
        QVERIFY(popupOpen("speakerPopover"));
        QVERIFY(!popupOpen("mergeDialog"));
        QVERIFY(!m_vm->canUndo());
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("speakerPopover"));
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
        QTRY_VERIFY(popupOpen("speakerPopover"));
        QCOMPARE(popover()->property("scope").toString(), QStringLiteral("line"));
        QCOMPARE(popover()->property("utteranceId").toString(),
                 m_vm->rowInfo(4).value(QStringLiteral("utteranceId")).toString());
        type(QStringLiteral("varga"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popupGone("speakerPopover"));
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
        QTRY_VERIFY(popupOpen("speakerPopover"));
        QQuickItem* listen = visual("listenButton");
        QVERIFY(listen);
        click(center(listen));
        QCOMPARE(m_player->property("log").toString(),
                 QStringLiteral("range:%1-%2;").arg(m_vm->rowStartMs(0)).arg(m_vm->rowEndMs(0)));
        QVERIFY(popupOpen("speakerPopover"));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("speakerPopover"));
        m_player->setProperty("log", QString());

        m_vm->setRailVisible(true);
        pump();
        click(center(visuals("laneHead").at(0)));
        QTRY_VERIFY(popupOpen("speakerPopover"));
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
        QVERIFY(popupOpen("speakerPopover"));
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

    // „Következő bizonytalan": a B billentyű (és az eszköztár gombja) a következő bizonytalan
    // sorra lép, körbefordulva; szövegmezőbe gépelve a betű nem lép.
    void nextUncertain_jumps()
    {
        load();
        QVERIFY(m_vm->uncertainCount() > 0);
        click(textPoint(0));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTest::keyClick(m_window.get(), Qt::Key_B);
        const int first = m_vm->currentRow();
        QVERIFY(first >= 0);
        QVERIFY(cell(first, Role::UncertainRole).toBool());
        QVERIFY(rowItem(first) != nullptr);
        QSet<int> seen{first};
        for (int i = 1; i < m_vm->uncertainCount(); ++i) {
            QTest::keyClick(m_window.get(), Qt::Key_B);
            QVERIFY(cell(m_vm->currentRow(), Role::UncertainRole).toBool());
            seen.insert(m_vm->currentRow());
        }
        QCOMPARE(int(seen.size()), m_vm->uncertainCount());
        QTest::keyClick(m_window.get(), Qt::Key_B);
        QCOMPARE(m_vm->currentRow(), first);                 // körbefordult
        QTest::keyClick(m_window.get(), Qt::Key_B, Qt::ShiftModifier);
        QVERIFY(m_vm->currentRow() != first);                // visszafelé

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

    // Szövegmezőbe gépelve a szerkesztő billentyűi (1–9, Szóköz, Ctrl+Y) nem sülnek el.
    void typingInSearchNeverTriggersEditorKeys()
    {
        load();
        m_vm->setRailVisible(true);
        click(textPoint(1));
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
        QVERIFY(!popupOpen("speakerPopover"));
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
        QVERIFY(!popupOpen("speakerPopover"));
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
        QTRY_VERIFY(popupOpen("speakerPopover"));
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
        QVERIFY(popupOpen("speakerPopover"));               // a panel nyitva marad: visszavonható
        QVERIFY(textOf("voiceprintResult").startsWith(QStringLiteral("Elkészült ")));
        click(center(visual("voiceprintUndo")));
        QCOMPARE(printCount(QStringLiteral("Kovács Lilla")), 1);
        QVERIFY(!m_vm->canUndo());                          // átsorolás nem történt
        // Vissza a soronkénti hatókörre: a blokk eltűnik.
        click(center(visual("scopeLine")));
        QVERIFY(!visual("voiceprintBlock"));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("speakerPopover"));

        // A teljes beszélő paneljében (áttekintő név) ugyanez a blokk áll.
        const QList<QQuickItem*> names = visuals("overviewName");
        click(names.at(0)->mapToScene(QPointF(12, names.at(0)->height() / 2)).toPoint());
        QTRY_VERIFY(popupOpen("speakerPopover"));
        QVERIFY(visual("voiceprintBlock"));
        QVERIFY(visual("voiceprintCreate"));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("speakerPopover"));
    }

    // Egy teljes beszélő elnevezése után a sáv felajánlja a hanglenyomatot; elkészülte után ezt
    // mondja ki, és a „Visszavonás" pontosan azt a lenyomatot törli. Sor után nincs ajánlat.
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
        QTRY_VERIFY(popupOpen("speakerPopover"));
        type(QStringLiteral("balint"));
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popupGone("speakerPopover"));
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
        QVERIFY(changeBar()->property("height").toReal() > 70);
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
        QTRY_VERIFY(popupOpen("speakerPopover"));
        QVERIFY(visual("pairRecheckButton"));
        QVERIFY(!visual("pairRecheckList"));
        click(center(visual("pairRecheckButton")));
        QTRY_VERIFY(visual("pairRecheckList"));
        const QList<QQuickItem*> choices = visuals("pairChoice");
        QVERIFY(!choices.isEmpty());
        const QString other = choices.first()->property("personName").toString();
        m_shell->setProperty("toasts", QString());
        click(center(choices.first()));
        QTRY_VERIFY(popupGone("speakerPopover"));
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
        QTRY_VERIFY(popupOpen("speakerPopover"));
        click(center(visual("scopeSpeaker")));
        QVERIFY(visual("voiceprintBlock"));
        QVERIFY(textOf("voiceprintDetail").startsWith(QStringLiteral("Nincs letöltve a hangmodell")));
        QVERIFY(!visual("voiceprintCreate"));
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(popupGone("speakerPopover"));
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
                    << QString::fromLatin1(variant) << QString::fromLatin1(state);
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
        QTest::qWait(180);
        tab.reset();
    }
};

QTEST_MAIN(TestTranscriptTab)
#include "test_transcript_tab.moc"
