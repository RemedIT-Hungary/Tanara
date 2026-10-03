// Az „Átirat" fül (TranscriptTab.qml) működése kijelző nélkül (offscreen), a beépített
// KITALÁLT meetingen: egér- és billentyű-események a QML-ablaknak (nem a felhasználó
// asztalának), hamis lejátszóval és shell-lel. Lefedi: Ctrl+L, kattintás másik oszlopba,
// Ctrl+Z, több sor kijelölése + számbillentyű, húzás, időbélyeg → seek, név → teljes-beszélő
// popover, „+" → személyválasztó, lejátszás-követés, a Shell pozíció-kérése, „Meghallgatom".
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

        QTest::keyClick(m_window.get(), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(speakerOf(2), before);
        QTest::keyClick(m_window.get(), Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(speakerOf(2), laneKey(2));
        QTest::keyClick(m_window.get(), Qt::Key_Z, Qt::ControlModifier);
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
        QTest::keyClick(m_window.get(), Qt::Key_Z, Qt::ControlModifier);   // egy lépés
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
        m_vm->undo();                                       // a húzás egy lépés
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

    void speakerPopover_reassignWholeSpeaker()
    {
        load();
        QQuickItem* name = rowItem(0)->findChild<QQuickItem*>(QStringLiteral("speakerName"));
        QVERIFY(name);
        const QString key = speakerOf(0);
        const int lines = m_vm->speakerInfo(key).value(QStringLiteral("utteranceCount")).toInt();
        QVERIFY(lines > 10);

        click(center(name));
        QTRY_VERIFY(popupOpen("speakerPopover"));
        QCOMPARE(m_tab->property("speakerPopoverRow").toInt(), 0);

        // A keresőbe gépelve, majd Enter: az első találathoz kerül a teljes beszélő.
        for (const QChar c : QStringLiteral("molnar"))
            QTest::keyClick(m_window.get(), c.toLatin1());
        pump();
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(!popupOpen("speakerPopover"));
        QCOMPARE(cell(0, Role::SpeakerNameRole).toString(), QStringLiteral("Molnár Eszter"));
        QCOMPARE(m_vm->speakerInfo(key).value(QStringLiteral("utteranceCount")).toInt(), lines);
        QTRY_COMPARE(m_tab->property("speakerPopoverRow").toInt(), -1);

        QTest::keyClick(m_window.get(), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(cell(0, Role::SpeakerNameRole).toString(), QStringLiteral("Kovács Lilla"));
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

    // Egy véletlen Enter az üres keresőben NEM rendelheti át a teljes beszélőt (és a
    // hanglenyomatát) a lista első emberéhez; nyíllal kiemelve viszont választ.
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
        QTRY_VERIFY(!popupOpen("speakerPopover"));
        QVERIFY(cell(0, Role::SpeakerNameRole).toString() != QStringLiteral("Kovács Lilla"));
        QVERIFY(m_vm->canUndo());
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

    // „Meghallgatás" a beszélő-panelen: egy jellemző sor szól (a panel nyitva marad).
    void speakerPopover_listenPlaysSample()
    {
        load();
        QQuickItem* name = rowItem(0)->findChild<QQuickItem*>(QStringLiteral("speakerName"));
        click(center(name));
        QTRY_VERIFY(popupOpen("speakerPopover"));
        QQuickItem* listen = m_window->findChild<QQuickItem*>(QStringLiteral("listenButton"));
        QVERIFY(listen);
        const QVariantMap sample = m_vm->speakerSample(speakerOf(0));
        QVERIFY(sample.value(QStringLiteral("ok")).toBool());
        const int start = sample.value(QStringLiteral("startMs")).toInt();
        const int end = sample.value(QStringLiteral("endMs")).toInt();
        QVERIFY(end > start && end - start <= 12000);
        QCOMPARE(m_vm->utterances().at(m_vm->utteranceForTime(start)).speakerKey, speakerOf(0));
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

    void statesLoadWithoutWarnings_data()
    {
        QTest::addColumn<QString>("variant");
        QTest::addColumn<QString>("state");
        const char* states[] = {"", "rail", "playing", "selection", "suggestion", "suggestionShown", "filter",
                                "search", "searchEmpty", "speakerPopover", "personPicker", "drag", "expanded"};
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
