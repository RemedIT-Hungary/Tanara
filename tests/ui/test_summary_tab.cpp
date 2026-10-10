// Az Összefoglaló fül forrás-hivatkozásai (v3, S1–S3) kijelző nélkül (offscreen), a beépített
// KITALÁLT mintaadaton, hamis lejátszóval és shell-lel: idő-chip → „Honnan jön ez?” felugró
// (Meghallgatom → a megszólalás lejátszása, Ugrás az átiratba → shell.seekTo, Jelzem →
// flagStatement + toast), rámutatás → a mondat kijelölése (és a térkép „Forrás” sora), a
// Források kapcsoló, a célzott elavulás jelölései + „Rendben így”, a memó tartalomjegyzéke és
// a szakasz-chip (lejátszás onnan), és a forrás nélküli (régi) összefoglaló régi nézete.
#include "AppContext.h"
#include "QmlApp.h"
#include "SummaryViewModel.h"

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
    width: 1004; height: 680
    color: Theme.bg
    property string variant: ""
    QtObject {
        id: fakePlayer
        objectName: "player"
        property bool available: true
        property bool playing: false
        property int positionMs: 0
        property string log: ""
        function seek(ms) { log += "seek:" + ms + ";"; positionMs = ms }
        function play() { log += "play;"; playing = true }
        function pause() { playing = false }
        function playRange(a, b) { log += "range:" + a + "-" + b + ";" }
    }
    QtObject {
        id: fakeShell
        objectName: "shell"
        property string log: ""
        property string toasts: ""
        function toast(text) { toasts += text + ";" }
        function seekTo(id, ms) { log += "seekTo:" + ms + ";" }
        function startQuickSummary(id) { log += "summary;" }
        function startTopicExtraction(id) { log += "topics;" }
        function revealInFolder(id) { log += "reveal;" }
        function openSettings(page, field) { log += "settings;" }
    }
    SummaryTab {
        objectName: "tab"
        anchors.fill: parent
        meetingId: "demo"
        player: fakePlayer
        shell: fakeShell
        demoState: variant
    }
}
)";

} // namespace

class TestSummaryTab : public QObject {
    Q_OBJECT

    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QStringList m_warnings;
    QQuickItem* m_tab = nullptr;
    QObject* m_player = nullptr;
    QObject* m_shell = nullptr;
    SummaryViewModel* m_vm = nullptr;

    void pump(int ms = 40) { QTest::qWait(ms); }

    void load(const QString& variant)
    {
        m_window.reset();
        m_warnings.clear();
        QQmlComponent comp(m_engine.get());
        comp.setData(kHost, QUrl(QStringLiteral("qrc:/qt/qml/Tanara/SummaryTabProbe.qml")));
        QObject* obj = comp.createWithInitialProperties({{QStringLiteral("variant"), variant}});
        QVERIFY2(obj, qPrintable(comp.errorString()));
        m_window.reset(qobject_cast<QQuickWindow*>(obj));
        QVERIFY(m_window);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
        m_tab = m_window->findChild<QQuickItem*>(QStringLiteral("tab"));
        m_player = m_window->findChild<QObject*>(QStringLiteral("player"));
        m_shell = m_window->findChild<QObject*>(QStringLiteral("shell"));
        m_vm = qobject_cast<SummaryViewModel*>(m_tab->property("vm").value<QObject*>());
        QVERIFY(m_vm);
        pump(120);
    }

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
    QPoint center(QQuickItem* item) const
    {
        return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    }
    void click(QQuickItem* item)
    {
        QVERIFY(item);
        QTest::mouseClick(m_window.get(), Qt::LeftButton, Qt::NoModifier, center(item));
        pump();
    }
    QObject* popover() const { return m_window->findChild<QObject*>(QStringLiteral("sourcePopover")); }
    bool popoverOpen() const { return popover() && popover()->property("opened").toBool(); }
    QString playerLog() const { return m_player->property("log").toString(); }
    QString shellLog() const { return m_shell->property("log").toString(); }
    // A bekezdés első szava, amely az adott mondathoz tartozik.
    QQuickItem* wordOf(const QString& sid) const
    {
        QQuickItem* flow = visual("execParagraph");
        if (!flow) return nullptr;
        for (QQuickItem* child : flow->childItems()) {
            const QVariantMap m = child->property("modelData").toMap();
            if (m.value(QStringLiteral("kind")).toString() == QLatin1String("word")
                && m.value(QStringLiteral("sid")).toString() == sid)
                return child;
        }
        return nullptr;
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

    void chipOpensSourcesPopover()
    {
        load(QStringLiteral("sourcesOn"));
        QVERIFY(visual("summaryToolsRow"));                  // ideiglenesen a fül tetején
        QVERIFY(visual("execParagraph"));
        const QList<QQuickItem*> chips = visuals("timeChip");
        QVERIFY2(chips.size() >= 9, qPrintable(QString::number(chips.size())));
        QVERIFY(visual("sourceMarks"));

        click(chips.first());                                 // az s1 chipje (00:04)
        QTRY_VERIFY(popoverOpen());
        QCOMPARE(popover()->property("statementId").toString(), QStringLiteral("s1"));
        QCOMPARE(m_vm->activeStatementId(), QStringLiteral("s1"));
        QCOMPARE(visuals("sourceListen").size(), 2);          // két idézet

        click(visuals("sourceListen").first());
        QVERIFY2(playerLog().contains(QStringLiteral("range:4000-13000")), qPrintable(playerLog()));

        click(visual("sourceFlag"));
        QVERIFY(m_shell->property("toasts").toString().contains(QStringLiteral("Jelezted")));
        QVERIFY(m_vm->sentences()->get(0).value(QStringLiteral("flagged")).toBool());
        QVERIFY(!visual("sourceFlag")->isEnabled());           // „Jelezve”

        click(visual("sourceJump"));
        QVERIFY2(shellLog().contains(QStringLiteral("seekTo:4000")), qPrintable(shellLog()));
        QTRY_VERIFY(!popoverOpen());

        // Billentyűzet: a chip fókuszálható, Enter nyitja.
        QQuickItem* chip = visuals("timeChip").at(2);
        chip->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(m_window.get(), Qt::Key_Return);
        QTRY_VERIFY(popoverOpen());
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(!popoverOpen());
    }

    void hoverSelectsSentence_toggleHidesChips()
    {
        load(QStringLiteral("sourcesOn"));
        QQuickItem* word = wordOf(QStringLiteral("s2"));
        QVERIFY(word);
        QTest::mouseMove(m_window.get(), center(word));
        QTRY_COMPARE(m_vm->activeStatementId(), QStringLiteral("s2"));
        // A térkép „Forrás” sorában az s2 forrása (egy tartomány) a kiemelt.
        int active = 0;
        for (const QVariant& m : m_vm->sourceMarks()) active += m.toMap().value(QStringLiteral("active")).toBool();
        QCOMPARE(active, 1);
        QTest::mouseMove(m_window.get(), QPoint(300, 660));    // üres terület
        QTRY_COMPARE(m_vm->activeStatementId(), QString());

        click(visual("sourcesToggle"));
        QVERIFY(!m_vm->sourcesVisible());
        QTRY_COMPARE(visuals("timeChip").size(), 0);
        QVERIFY(!visual("sourceMarks"));
        click(visual("sourcesToggle"));
        QTRY_VERIFY(visuals("timeChip").size() >= 9);
    }

    void staleTargetedMarks()
    {
        load(QStringLiteral("staleTargeted"));
        QQuickItem* banner = visual("staleBanner");
        QVERIFY(banner);
        const QString text = visual("staleText")->property("text").toString();
        QVERIFY2(text.contains(QStringLiteral("1 állítás és 1 teendő")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("3 beszélőt")));
        QVERIFY(visual("ownerStaleChip"));
        QVERIFY(!visuals("staleUnderline").isEmpty());
        int warnChips = 0;
        for (QQuickItem* c : visuals("timeChip")) warnChips += c->property("warn").toBool();
        QCOMPARE(warnChips, 2);                                // 12:41 és 13:01

        click(visual("staleRefresh"));
        QVERIFY(shellLog().contains(QStringLiteral("summary;")));
        click(visual("staleDismiss"));
        QTRY_VERIFY(!visual("staleBanner"));
    }

    void memoTocAndSectionChip()
    {
        load(QStringLiteral("memoSections"));
        QVERIFY(visual("memoToc"));
        QVERIFY(visual("sectionBands"));
        QCOMPARE(m_vm->activeSection(), 2);
        const QList<QQuickItem*> entries = visuals("memoTocEntry");
        QCOMPARE(entries.size(), 7);
        click(entries.at(5));
        QTRY_COMPARE(m_vm->activeSection(), 5);
        QQuickItem* chip = nullptr;
        for (QQuickItem* c : visuals("sectionRangeChip"))
            if (c->property("text").toString().startsWith(QStringLiteral("92:10"))) chip = c;
        QVERIFY(chip);
        click(chip);
        QVERIFY2(playerLog().contains(QStringLiteral("seek:5530000;play;")), qPrintable(playerLog()));
    }

    void noSourcesKeepsOldView()
    {
        load(QStringLiteral("noSources"));
        QVERIFY(!m_vm->hasStatements());
        QVERIFY(!visual("execParagraph"));
        QVERIFY(!visual("summaryToolsRow"));
        QVERIFY(visuals("timeChip").isEmpty());
    }
};

QTEST_MAIN(TestSummaryTab)
#include "test_summary_tab.moc"
