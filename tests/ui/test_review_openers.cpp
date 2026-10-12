// Az Átnézendő S5 elemei (ReviewFocusView a TranscriptTab-ban) kijelző nélkül (offscreen), a
// beépített KITALÁLT „v3" meetingen, hamis lejátszóval: a kör eleji NYITÓ-JAVASLAT (Enter = Mind az
// N → Y egy visszavonási lépésben, Shift = nem minta, E = Egyenként a kurzor elé, → = Később, a
// szúrópróba 1–3 / Shift+1–3, ✗ után az Egyenként az alapértelmezés) és a kör közbeni
// MINTÁZAT-AJÁNLAT (4 / 6 azonos döntés után a kártya ALATT; I = Igen egy lépésben, N = Nem és nem
// jön vissza, L / Megmutatom = a sorok listája); a fókusz-kártya y-ja nem változik; a demó-állapotok.
// A felhasználó adataihoz nem nyúl.
#include "AppContext.h"
#include "QmlApp.h"
#include "ReviewQueueModel.h"
#include "TranscriptEditorViewModel.h"
#include "TranscriptListModel.h"
#include "tanara/edit/SpeakerEditor.h"
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

class TestReviewOpeners : public QObject {
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
        comp.setData(kHost, QUrl(QStringLiteral("qrc:/qt/qml/Tanara/ReviewOpenersProbe.qml")));
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
    QString phase() const { return view() ? view()->property("phase").toString() : QString(); }
    QVariantMap opener() const { return view() ? view()->property("opener").toMap() : QVariantMap(); }
    // Az Átnézendő bekapcsolása; a kör eleji nyitó-javaslat bevárása.
    void openOpener()
    {
        QVERIFY(QTest::qWaitFor([this] { return !m_vm->reviewRunning() && m_vm->reviewCount() > 0; }, 10000));
        QMetaObject::invokeMethod(m_tab, "setViewMode", Q_ARG(QVariant, QStringLiteral("review")));
        QTRY_VERIFY_WITH_TIMEOUT(m_vm->queueActive() && phase() == QLatin1String("opener"), 10000);
        QTRY_VERIFY(visual("reviewFocusOpener"));
        m_tab->forceActiveFocus();
        pump(60);
    }
    // A hátralévő nyitók kihagyása (→).
    void skipRest()
    {
        for (int g = 0; g < 20 && phase() == QLatin1String("opener"); ++g) key(Qt::Key_Right);
    }
    // A leggyakoribb nyitott (ok, jelenlegi, javasolt) hármas sorai, listasorrendben.
    QStringList pendingTriple() const
    {
        QHash<QString, QStringList> by;
        QString best;
        for (int i = 0; i < m_queue->count(); ++i) {
            const QVariantMap m = m_queue->get(i);
            const QString likely = m.value(QStringLiteral("likelyKey")).toString();
            if (m.value(QStringLiteral("state")).toString() != QLatin1String("pending") || likely.isEmpty()
                || likely == m.value(QStringLiteral("speakerKey")).toString())
                continue;
            const QString t = m.value(QStringLiteral("reasonKind")).toString() + QLatin1Char('|')
                              + m.value(QStringLiteral("speakerKey")).toString() + QLatin1Char('|') + likely;
            by[t] << m.value(QStringLiteral("utteranceId")).toString();
            if (best.isEmpty() || by.value(t).size() > by.value(best).size()) best = t;
        }
        return by.value(best);
    }

    // Az Átnézendő bekapcsolása, a munkalista bevárása (a háttér-elemzés végén indul).
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

    // Enter: a nyitó minden (nyitott) sora a javasolt beszélőhöz, EGY visszavonási lépésben; az
    // „Imént" sor „Mind a N → Y"; utána a következő nyitó jön. Ctrl+Z: mind vissza.
    void opener_enterAppliesAllInOneStep()
    {
        load(QStringLiteral("v3"));
        openOpener();
        const QVariantMap op = opener();
        const QStringList ids = op.value(QStringLiteral("ids")).toStringList();
        const QString from = op.value(QStringLiteral("currentKey")).toString();
        const QString to = op.value(QStringLiteral("proposedKey")).toString();
        const int n = ids.size();
        QVERIFY(n >= 15);
        QVERIFY(!to.isEmpty());
        QVERIFY(visual("reviewOpenerImpact"));
        QVERIFY(visual("reviewOpenerAccept")->property("text").toString().startsWith(QStringLiteral("Mind ")));
        QCOMPARE(visual("reviewOpenerAccept")->property("keyText").toString(), QStringLiteral("Enter"));
        QCOMPARE(visuals("reviewOpenerSpot").size(), 3);
        QVERIFY(!card()->isVisible());
        key(Qt::Key_Return);
        for (const QString& id : ids) {
            QCOMPARE(utt(id).speakerKey, to);
            QVERIFY(utt(id).manuallyCorrected);
            QVERIFY(!utt(id).noisy);
            QCOMPARE(stateOf(id), QStringLiteral("decided"));
        }
        const QString name = m_vm->speakerView(to).name;
        const QString article = (n == 1 || QString::number(n).startsWith(QLatin1Char('5'))) ? QStringLiteral("az") : QStringLiteral("a");
        QCOMPARE(m_vm->lastDecision().value(QStringLiteral("text")).toString(),
                 QStringLiteral("Mind %1 %2 → %3").arg(article).arg(n).arg(name));
        // A következő nyitó (ha van), különben a fókusz.
        QVERIFY(phase() == QLatin1String("focus") || opener().value(QStringLiteral("groupId")) != op.value(QStringLiteral("groupId")));
        // Ctrl+Z: egy lépés, minden sor vissza.
        key(Qt::Key_Z, Qt::ControlModifier);
        for (const QString& id : ids) {
            QCOMPARE(utt(id).speakerKey, from);
            QVERIFY(!utt(id).manuallyCorrected);
        }
        skipRest();
        QCOMPARE(phase(), QStringLiteral("focus"));
        QVERIFY(card()->isVisible());
    }

    // Shift+Enter: ugyanez, nem mintaként (a sorok „zajos" jelzéssel, ugyanabban a lépésben).
    void opener_shiftEnterNonSample()
    {
        load(QStringLiteral("v3"));
        openOpener();
        const QStringList ids = opener().value(QStringLiteral("ids")).toStringList();
        const QString to = opener().value(QStringLiteral("proposedKey")).toString();
        m_tab->setProperty("shiftHeld", true);
        QVERIFY(visual("reviewOpenerAccept")->property("nonSample").toBool());
        QVERIFY(visual("reviewOpenerImpactGain")->property("text").toString().contains(QStringLiteral("nem minta")));
        m_tab->setProperty("shiftHeld", false);
        key(Qt::Key_Return, Qt::ShiftModifier);
        for (const QString& id : ids) {
            QCOMPARE(utt(id).speakerKey, to);
            QVERIFY(utt(id).noisy);
        }
        key(Qt::Key_Z, Qt::ControlModifier);
        for (const QString& id : ids) QVERIFY(!utt(id).noisy);
    }

    // E (Egyenként): a nyitó sorai a kurzor helyére (időrendben), semmi nem mozdul.
    void opener_singleBringsToFront()
    {
        load(QStringLiteral("v3"));
        openOpener();
        const QStringList first = opener().value(QStringLiteral("ids")).toStringList();
        const QString from = opener().value(QStringLiteral("currentKey")).toString();
        const int count = m_queue->count();
        key(Qt::Key_E);
        QCOMPARE(m_queue->count(), count);
        const int at = m_queue->currentIndex();
        for (int i = 0; i < first.size(); ++i) {
            QCOMPARE(m_queue->indexOf(first[i]), at + i);
            QCOMPARE(utt(first[i]).speakerKey, from);
            QCOMPARE(stateOf(first[i]), QStringLiteral("pending"));
        }
        QCOMPARE(current(), first.first());
        // A többi nyitó kihagyva: a kurzor marad, a fókusz-kártya az első soron.
        skipRest();
        QCOMPARE(current(), first.first());
        QVERIFY(card()->isVisible());
    }

    // → / Később: a nyitó kimarad, a lista sorrendje változatlan, a sorai nyitottak maradnak.
    void opener_laterKeepsRows()
    {
        load(QStringLiteral("v3"));
        openOpener();
        const QVariantMap op = opener();
        const QStringList ids = op.value(QStringLiteral("ids")).toStringList();
        QStringList order;
        for (int i = 0; i < m_queue->count(); ++i) order << m_queue->get(i).value(QStringLiteral("utteranceId")).toString();
        const QString cursor = current();
        click(visual("reviewOpenerLater"));
        QVERIFY(phase() == QLatin1String("focus") || opener().value(QStringLiteral("groupId")) != op.value(QStringLiteral("groupId")));
        QStringList after;
        for (int i = 0; i < m_queue->count(); ++i) after << m_queue->get(i).value(QStringLiteral("utteranceId")).toString();
        QCOMPARE(after, order);
        for (const QString& id : ids) QCOMPARE(stateOf(id), QStringLiteral("pending"));
        QCOMPARE(current(), cursor);
    }

    // Szúrópróba: 1–3 lejátssza (playLine), a sor „meghallgatva"; Shift+n ✓ / ✗; ✗ után az Enter
    // az Egyenként (a gomb-kiemelés is átvált), és semmi nem mozdul.
    void opener_spotCheckDoubtDefaultsToSingle()
    {
        load(QStringLiteral("v3"));
        openOpener();
        const QVariantMap op = opener();
        const QStringList spot = op.value(QStringLiteral("spotIds")).toStringList();
        QCOMPARE(spot.size(), 3);
        clearLog();
        key(Qt::Key_1);
        const int start = m_vm->lineInfo(spot[0]).value(QStringLiteral("startMs")).toInt();
        QVERIFY2(playerLog().contains(QStringLiteral("range:%1-").arg(start)), qPrintable(playerLog()));
        QVERIFY(visual("reviewOpenerSpotStatus")->property("text").toString().startsWith(QStringLiteral("1/3 meghallgatva")));
        QVERIFY(visual("reviewOpenerSpotStatus")->property("text").toString().contains(QStringLiteral("nem garancia")));
        QCOMPARE(visuals("reviewOpenerVerdict").size(), 1);
        QVERIFY(!view()->property("openerDoubt").toBool());
        // Kattintás a ✓-re: ✗.
        click(visual("reviewOpenerVerdict"));
        QVERIFY(view()->property("openerDoubt").toBool());
        QCOMPARE(visual("reviewOpenerSingle")->property("variant").toString(), QStringLiteral("primary"));
        QCOMPARE(visual("reviewOpenerAccept")->property("variant").toString(), QStringLiteral("secondary"));
        // Shift+1: vissza ✓; Shift+1 újra: ✗.
        key(Qt::Key_1, Qt::ShiftModifier);
        QVERIFY(!view()->property("openerDoubt").toBool());
        key(Qt::Key_1, Qt::ShiftModifier);
        QVERIFY(view()->property("openerDoubt").toBool());
        const QStringList ids = op.value(QStringLiteral("ids")).toStringList();
        const QString from = op.value(QStringLiteral("currentKey")).toString();
        key(Qt::Key_Return);
        for (const QString& id : ids) QCOMPARE(utt(id).speakerKey, from);
        QCOMPARE(current(), ids.first());
        QVERIFY(phase() == QLatin1String("focus") || opener().value(QStringLiteral("groupId")) != op.value(QStringLiteral("groupId")));
        // A következő nyitónál a szúrópróba tiszta lappal indul.
        QVERIFY(!view()->property("openerDoubt").toBool());
    }

    // Mintázat-ajánlat: 4 azonos döntés (ok, jelenlegi, javasolt) után a kártya ALATT jelenik meg
    // (3 után még nem); a kártya y-ja nem mozdul; Megmutatom = a sorok listája; Igen = a hátralévő
    // azonos sorok EGY lépésben; Ctrl+Z vissza.
    void offer_afterFourOfSix_yesOneStep()
    {
        load(QStringLiteral("v3"));
        openReview();
        const qreal y = cardY();
        const QStringList triple = pendingTriple();
        QVERIFY(triple.size() >= 8);
        const QString to = m_queue->get(m_queue->indexOf(triple[0])).value(QStringLiteral("likelyKey")).toString();
        for (int k = 0; k < 4; ++k) {
            QVERIFY(!visual("reviewFocusOffer"));
            m_queue->jumpTo(m_queue->indexOf(triple[k]));
            pump();
            key(Qt::Key_Return);
            QCOMPARE(utt(triple[k]).speakerKey, to);
        }
        QTRY_VERIFY(visual("reviewFocusOffer"));
        QCOMPARE(cardY(), y);
        // A kártya alatt (az alsó kontextus-sorok helyén).
        QVERIFY(visual("reviewFocusOffer")->mapToScene(QPointF(0, 0)).y() > y + card()->height());
        const QStringList ahead = view()->property("offerData").toMap().value(QStringLiteral("sameAhead")).toStringList();
        QVERIFY(!ahead.isEmpty());
        QVERIFY(!ahead.contains(current()));
        QVERIFY(visual("reviewOfferText")->property("text").toString().contains(QStringLiteral("%1 sor ugyanilyen").arg(ahead.size())));

        // Megmutatom (L): a lista a sáv alatt; a kártya nem mozdul; kattintás = lejátszás.
        key(Qt::Key_L);
        QTRY_VERIFY(visual("reviewOfferList"));
        QVERIFY(!visuals("reviewOfferLine").isEmpty());
        QCOMPARE(cardY(), y);
        clearLog();
        click(visuals("reviewOfferLine").first());
        QVERIFY2(playerLog().contains(QStringLiteral("range:")), qPrintable(playerLog()));
        m_tab->forceActiveFocus();

        // Igen: egy lépés.
        const int undoBefore = int(m_vm->editor()->undoSerials().size());
        key(Qt::Key_I);
        for (const QString& id : ahead) {
            QCOMPARE(utt(id).speakerKey, to);
            QVERIFY(!utt(id).noisy);
        }
        QCOMPARE(int(m_vm->editor()->undoSerials().size()), undoBefore + 1);
        QVERIFY(m_vm->lastDecision().value(QStringLiteral("text")).toString().startsWith(QStringLiteral("Mind ")));
        QTRY_VERIFY(!visual("reviewFocusOffer"));
        QCOMPARE(cardY(), y);
        key(Qt::Key_Z, Qt::ControlModifier);
        for (const QString& id : ahead) QVERIFY(utt(id).speakerKey != to);
    }

    // Shift+I: Igen, nem mintaként. N: eltűnik, és ugyanerre a mintára nem jön vissza.
    void offer_shiftYesNonSample_noDismisses()
    {
        loadDemo("reviewFocusOffer");
        QTRY_VERIFY(visual("reviewFocusOffer"));
        QStringList ahead = view()->property("offerData").toMap().value(QStringLiteral("sameAhead")).toStringList();
        const QString to = view()->property("offerData").toMap().value(QStringLiteral("proposedKey")).toString();
        QVERIFY(ahead.size() >= 2);
        key(Qt::Key_I, Qt::ShiftModifier);
        for (const QString& id : ahead) {
            QCOMPARE(utt(id).speakerKey, to);
            QVERIFY(utt(id).noisy);
        }
        key(Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY(visual("reviewFocusOffer"));

        // Nem: eltűnik; egy újabb azonos döntés után sem jön vissza.
        click(visual("reviewOfferNo"));
        QVERIFY(!visual("reviewFocusOffer"));
        const QString key_ = view()->property("offerData").toMap().value(QStringLiteral("key")).toString();
        QVERIFY(key_.isEmpty());
        ahead = m_queue->recentPattern().value(QStringLiteral("sameAhead")).toStringList();
        QVERIFY(ahead.isEmpty());
        const QStringList triple = pendingTriple();
        QVERIFY(!triple.isEmpty());
        m_queue->jumpTo(m_queue->indexOf(triple[0]));
        pump();
        m_tab->forceActiveFocus();
        key(Qt::Key_Return);
        pump(60);
        QVERIFY(!visual("reviewFocusOffer"));
    }

    // A fókusz-kártya y-ja az ajánlat-sávval (és kinyitott listával) ugyanaz, mint nélküle.
    void cardY_sameWithOffer()
    {
        loadDemo("reviewFocus");
        QTRY_VERIFY(card() && card()->isVisible());
        const qreal y = cardY();
        for (const char* state : {"reviewFocusOffer", "reviewFocusOfferList"}) {
            loadDemo(state);
            QTRY_VERIFY(visual("reviewFocusOffer"));
            qInfo("%s: kártya y = %.0f px (reviewFocus: %.0f px; ablak 1100×760)", state, cardY(), y);
            QCOMPARE(cardY(), y);
        }
        QVERIFY(visual("reviewOfferList"));
    }

    // A demó-állapotok: nyitó (1 meghallgatva), nyitó ✗-szel (Egyenként kiemelve).
    void demoStates_opener()
    {
        loadDemo("reviewFocusOpener");
        QTRY_COMPARE(phase(), QStringLiteral("opener"));
        QVERIFY(visual("reviewFocusOpener"));
        QCOMPARE(visuals("reviewOpenerVerdict").size(), 1);
        QVERIFY(!view()->property("openerDoubt").toBool());
        loadDemo("reviewFocusOpenerDoubt");
        QTRY_COMPARE(phase(), QStringLiteral("opener"));
        QVERIFY(view()->property("openerDoubt").toBool());
        QCOMPARE(visual("reviewOpenerSingle")->property("variant").toString(), QStringLiteral("primary"));
        // A fókusz-demókban nincs nyitó (a kártya látszik).
        loadDemo("reviewFocus");
        QTRY_VERIFY(card() && card()->isVisible());
        QCOMPARE(phase(), QStringLiteral("focus"));
    }
};

QTEST_MAIN(TestReviewOpeners)
#include "test_review_openers.moc"
