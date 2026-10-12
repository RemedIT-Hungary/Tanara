// Az Átnézendő fókusz-nézete (S4, ReviewFocusView a TranscriptTab-ban) kijelző nélkül (offscreen),
// a beépített KITALÁLT meetingeken, hamis lejátszóval: egybillentyűs ítéletek a kurzor sorára
// (Enter / J / 0 / 1–9 / Shift = nem minta) és a kurzor lépése; → Később (a lista végére); a
// fókusz-kártya y-ja nem változik (beemelés, a kártya alá kerülő elem, kisebb nézet); „+K új vár”
// kattintás → a sorok a lista végére; kör-zárás gombjai; Esc → Javítás ugyanazon a soron;
// visszavonás → a fókusz a visszavont sorra; auto-lejátszás (sor + ráhagyás) és kapcsolója;
// a kontextus-sor lejátszása (`,`). A felhasználó adataihoz nem nyúl.
#include "AppContext.h"
#include "QmlApp.h"
#include "ReviewQueueModel.h"
#include "TranscriptEditorViewModel.h"
#include "TranscriptListModel.h"
#include "tanara/store/JsonSerialization.h"

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
    tanara::EditorUtterance utt(const QString& id) const { return m_vm->utterances().value(m_vm->utteranceIndexOf(id)); }
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
    // Az Átnézendő bekapcsolása, a munkalista bevárása (a háttér-elemzés végén indul).
    QString phase() const { return view() ? view()->property("phase").toString() : QString(); }
    // skipOpenerPhase: a kör eleji nyitó-javaslatok (S5) → Később; a fókusz-kártya jön.
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
    // A v3 demó-állapot bevárása (a munkalista a háttér-elemzés csendes szakaszában készül).
    void loadDemo(const char* state)
    {
        load(QString(), QString::fromLatin1(state));
        QTRY_VERIFY_WITH_TIMEOUT(m_vm->queueActive() && !m_vm->demoStatePending() && !m_vm->reviewRunning(), 15000);
        pump(1500);         // a demó a csend után fut (700 ms), utána még egy elemzés-kör lehet
        QTRY_VERIFY_WITH_TIMEOUT(!m_vm->reviewRunning(), 10000);
        pump(100);
        m_tab->forceActiveFocus();
    }
    // A kurzor egy nyitott, javaslattal bíró sorra (a javasolt beszélő más, mint a mostani).
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
    // A kurzor sorától eltérő beszélő oszlopa (az 1–9 billentyűhöz).
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

    // Enter / J / Shift+J / 0 / 1–9 / Shift+1–9: az ítélet a kurzor sorára, a kurzor továbblép,
    // a sor a helyén marad (a lista nem rendeződik át).
    void verdicts_keysDecideCursorLine()
    {
        load(QStringLiteral("v3"));
        openReview();
        const int count = m_queue->count();

        // Enter: a javaslat („X mondta”).
        QVERIFY(jumpToSuggestion());
        QString id = current();
        const int at = m_queue->indexOf(id);
        const QString likely = cur().value(QStringLiteral("likelyKey")).toString();
        QVERIFY(visual("reviewFocusEnter")->property("text").toString().endsWith(QStringLiteral(" mondta")));
        key(Qt::Key_Return);
        QCOMPARE(utt(id).speakerKey, likely);
        QVERIFY(utt(id).manuallyCorrected);
        QVERIFY(!utt(id).noisy);
        QCOMPARE(stateOf(id), QStringLiteral("decided"));
        QCOMPARE(m_queue->indexOf(id), at);                  // a helyén marad
        QVERIFY(current() != id);
        QCOMPARE(m_queue->count(), count);

        // J: Jó így (minta); Shift+J: Jó így, nem minta.
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

        // 0: nem azonosítható.
        id = current();
        key(Qt::Key_0);
        QVERIFY(utt(id).ignored);
        QCOMPARE(stateOf(id), QStringLiteral("ignored"));
        QVERIFY(current() != id);

        // 1–9: a sín n. beszélője; Shift: nem minta.
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

        // ←: vissza az előző (döntött) sorra; ott újra dönthető.
        key(Qt::Key_Left);
        QCOMPARE(current(), id);
        QCOMPARE(stateOf(id), QStringLiteral("decided"));
        QVERIFY(visual("reviewFocusDecided"));
    }

    // A kártya gombjai és a beszélő-számsor ugyanígy döntenek; lenyomott Shiftnél „· nem minta”.
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

        // Számsor: kattintás egy másik beszélőre = áthelyezés oda (minta).
        id = current();
        const int lane = otherLane();
        QList<QQuickItem*> chips = visuals("reviewFocusLane");
        QVERIFY(chips.size() > lane);
        const QString target = m_vm->lanes()[lane].toMap().value(QStringLiteral("key")).toString();
        click(chips[lane]);
        QCOMPARE(utt(id).speakerKey, target);
        QVERIFY(!utt(id).noisy);

        // „Nem azonosítható” gomb.
        id = current();
        click(visual("reviewFocusIgnore"));
        QVERIFY(utt(id).ignored);

        // „Más…” (és az M billentyű): a sor panelje erre a sorra.
        id = current();
        key(Qt::Key_M);
        QObject* popover = m_window->findChild<QObject*>(QStringLiteral("linePopover"));
        QTRY_VERIFY(popover->property("opened").toBool());
        QCOMPARE(popover->property("utteranceId").toString(), id);
        key(Qt::Key_Escape);
        QTRY_VERIFY(!popover->property("visible").toBool());
    }

    // → Később: a sor a lista végére (◇), a kurzor a következő nyitott sorra; B = következő nyitott.
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

    // A fókusz-kártya y-ja nem változik: „+K új vár” beemeléskor (a sorok a lista VÉGÉRE), a
    // kártya alá kerülő elemnél (S5 ajánlat-hely) és ha a nézet alacsonyabb lesz (pl. az „Imént”
    // sor figyelmeztetése alatta).
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
        QCOMPARE(current(), id);                            // a kurzor nem mozdult
        QCOMPARE(cardY(), y);

        // Egy elem a kártya ALATT (az S5 ajánlat-helye): a kártya marad, az alsó kontextus-sorok mennek.
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

        // Alacsonyabb nézet (a figyelmeztetés / mini-napló a kártya alatt nyúlik): a kártya marad.
        m_window->resize(1100, 600);
        pump(80);
        QCOMPARE(cardY(), y);
        // Döntés után a következő kártya ugyanott.
        key(Qt::Key_J);
        QCOMPARE(cardY(), y);
    }

    // A kártya y-ja minden fókusz-állapotban ugyanaz (a fejléc, az „Imént” sor és a kártya alá
    // kerülő elemek sem tolják el).
    void cardY_sameInEveryFocusState()
    {
        qreal first = -1;
        for (const char* state : {"reviewFocus", "reviewFocusDecided", "reviewFocusIncoming", "reviewFocusAlreadyDecided"}) {
            loadDemo(state);
            QTRY_VERIFY(card() && card()->isVisible());
            const qreal y = cardY();
            qInfo("%s: kártya y = %.0f px (ablak 1100×760), magasság %.0f px", state, y, card()->height());
            if (first < 0) first = y;
            QCOMPARE(y, first);
        }
    }

    // „Már eldőlt” sor: jelzés a kártyán; Enter = megerősítem; → = átugrik.
    void alreadyDecided_enterConfirms()
    {
        loadDemo("reviewFocusAlreadyDecided");
        QTRY_COMPARE(cur().value(QStringLiteral("state")).toString(), QStringLiteral("alreadyDecided"));
        QVERIFY(visual("reviewFocusAlreadyDecided"));
        QCOMPARE(visual("reviewFocusEnter")->property("text").toString(), QStringLiteral("Megerősítem"));
        const QString id = current();
        key(Qt::Key_Return);
        QVERIFY(utt(id).confirmed);
        QVERIFY(current() != id);
    }

    // Kör-zárás: összesítő; Következő kör (beérkezők + később) · Később hagyottak · Kész (Javítás).
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

        // Csak a később hagyottak (→ billentyű).
        loadDemo("reviewFocusSummary");
        QTRY_VERIFY(visual("reviewFocusSummary"));
        const int later2 = m_queue->laterCount();
        key(Qt::Key_Right);
        QCOMPARE(m_queue->round(), 2);
        QCOMPARE(m_queue->count(), later2);

        // Kész: vissza a Javítás nézetbe.
        loadDemo("reviewFocusSummary");
        QTRY_VERIFY(visual("reviewFocusSummary"));
        click(visual("reviewDone"));
        QTRY_COMPARE(viewMode(), QStringLiteral("fix"));
    }

    // Esc: a Javítás nézet, kijelölve ugyanaz a sor.
    void escape_opensFixOnSameLine()
    {
        load();
        openReview();
        key(Qt::Key_Down);
        const QString id = current();
        key(Qt::Key_Escape);
        QTRY_COMPARE(viewMode(), QStringLiteral("fix"));
        QTRY_COMPARE(m_vm->rowUtteranceId(m_vm->currentRow()), id);
        // Vissza: a munkalista megmaradt (nincs új pillanatkép), a kurzor ugyanott.
        QMetaObject::invokeMethod(m_tab, "setViewMode", Q_ARG(QVariant, QStringLiteral("review")));
        pump(60);
        QCOMPARE(current(), id);
    }

    // Ctrl+Z: a döntés visszavonva, a fókusz a visszavont sorra ugrik.
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

    // Auto-lejátszás: a kártyára lépő sor szól (sor + ráhagyás); a kapcsoló kikapcsolja; R újra;
    // `,` az előző kontextus-sor.
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

        // Kapcsoló ki: a következő kártya nem szól magától.
        click(visual("reviewAutoPlay"));
        QVERIFY(!m_vm->reviewAutoPlay());
        clearLog();
        key(Qt::Key_J);
        pump(60);
        QVERIFY2(!playerLog().contains(QStringLiteral("range:")), qPrintable(playerLog()));
        // R: a kártya sora újra.
        key(Qt::Key_R);
        QVERIFY(playerLog().startsWith(QStringLiteral("range:%1-").arg(cur().value(QStringLiteral("startMs")).toInt())));

        // `,`: az előző kontextus-sor (az átirat előző sora, akár nem kétes).
        const QVariantList ctx = m_vm->contextLines(current(), 2, 2);
        int prevStart = -1;
        for (const QVariant& v : ctx)
            if (v.toMap().value(QStringLiteral("offset")).toInt() == -1) prevStart = v.toMap().value(QStringLiteral("startMs")).toInt();
        if (prevStart >= 0) {
            clearLog();
            key(Qt::Key_Comma);
            QVERIFY2(playerLog().startsWith(QStringLiteral("range:%1-").arg(prevStart)), qPrintable(playerLog()));
        }
        // Szóköz: szünet / indítás.
        m_player->setProperty("playing", true);
        clearLog();
        key(Qt::Key_Space);
        QCOMPARE(playerLog(), QStringLiteral("pause;"));
    }

    // A munkalista-csík: cella-kattintás = ugrás arra a sorra.
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

    // A beállítás-kulcs (settings.json "reviewAutoPlay"): alapból be, oda-vissza megmarad.
    void settings_reviewAutoPlayRoundTrip()
    {
        tanara::AppSettings s;
        QVERIFY(s.reviewAutoPlay);
        s.reviewAutoPlay = false;
        QVERIFY(!tanara::appSettingsFromJson(tanara::toJson(s)).reviewAutoPlay);
        QVERIFY(tanara::appSettingsFromJson(QJsonObject()).reviewAutoPlay);
    }
};

QTEST_MAIN(TestReviewFocus)
#include "test_review_focus.moc"
