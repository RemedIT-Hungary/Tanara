// A v3 megbeszélés-nézet héja (Main.qml) kijelző nélkül (offscreen), demó-adattal (App.demo,
// nincs AppController): egysoros fejléc a négy füllel, a nem elérhető fül panelje, a
// „Megbeszélés” menü, az Áttekintés részei (banner + „Később”, lépések, sávok, ADATOK,
// összecsukott sorok, üres kártyák), az Átirat eszköz-sora (Olvasás / Javítás, Ctrl+E) és a
// térkép-dokk (csak az Átirat / összefoglaló füleken; kattintás = ugrás).
#include "AppContext.h"
#include "QmlApp.h"

#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QtTest>

#include <memory>

using namespace tanara_qml;

class TestOverviewTab : public QObject {
    Q_OBJECT

    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow* m_window = nullptr;
    QStringList m_warnings;

    void load(const QString& demoState)
    {
        m_window = nullptr;
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_warnings.clear();
        connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError>& list) {
            for (const QQmlError& e : list) m_warnings << e.toString();
        });
        QVERIFY(loadPage(*m_engine, QStringLiteral("Main"), {{"demoState", demoState}}, QSize(1280, 820)));
        m_window = qobject_cast<QQuickWindow*>(m_engine->rootObjects().value(0));
        if (!m_window) m_window = m_engine->findChild<QQuickWindow*>();
        QVERIFY(m_window);
        m_window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(m_window));
        QTest::qWait(200);
    }
    // A vizuális fában (a Repeater-delegáltak és a felugrók tartalma is) — a látható példány
    // előnyben, ha több is van.
    static void collect(QQuickItem* root, const QString& name, QList<QQuickItem*>& out)
    {
        if (!root) return;
        if (root->objectName() == name) out << root;
        for (QQuickItem* child : root->childItems()) collect(child, name, out);
    }
    QList<QQuickItem*> items(const char* name) const
    {
        QList<QQuickItem*> out;
        if (m_window) collect(m_window->contentItem(), QLatin1String(name), out);
        return out;
    }
    QQuickItem* item(const char* name) const
    {
        const QList<QQuickItem*> all = items(name);
        for (QQuickItem* it : all)
            if (it->isVisible()) return it;
        return all.value(0, nullptr);
    }
    QObject* object(const char* name) const
    {
        return m_window ? m_window->findChild<QObject*>(QLatin1String(name)) : nullptr;
    }
    bool shown(const char* name) const
    {
        QQuickItem* it = item(name);
        return it && it->isVisible();
    }
    // Több azonos nevű elem (a két „Megbeszélés” menü) közül látszik-e valamelyik.
    bool anyShown(const char* name) const
    {
        for (QQuickItem* it : items(name))
            if (it->isVisible()) return true;
        return false;
    }
    void click(QQuickItem* it)
    {
        QVERIFY(it);
        const QPoint p = it->mapToScene(QPointF(it->width() / 2, it->height() / 2)).toPoint();
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, p);
        QTest::qWait(60);
    }
    int tab() const { return m_window->property("currentTab").toInt(); }

private slots:
    void initTestCase()
    {
        AppContext::instance()->setDemo(true);
        AppContext::instance()->setThemeMode(QStringLiteral("light"));
    }
    void cleanup()
    {
        QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QLatin1Char('\n'))));
        m_engine.reset();
        m_window = nullptr;
    }

    // V1: az Áttekintés az első fül; a banner „Később”-re eltűnik; a nem elérhető összefoglaló
    // fül kattintásra panelt nyit (és nem vált), a futó átírás a fülön.
    void processing_bannerTabsAndPanel()
    {
        load(QStringLiteral("overviewProcessing"));
        QCOMPARE(tab(), 0);
        QVERIFY(shown("overviewTab"));
        QVERIFY(shown("pipelineSteps"));
        QVERIFY(shown("trackList"));
        QVERIFY(shown("participantList"));
        QVERIFY(!shown("statTiles"));
        QVERIFY(!shown("mapDock"));                       // Áttekintésen a sima lejátszó
        QVERIFY(shown("seekSlider"));
        QVERIFY(shown("approvalBanner"));
        click(item("laterButton"));
        QTRY_VERIFY(!shown("approvalBanner"));

        QObject* tip = object("tabTooltip");
        QVERIFY(tip);
        QVERIFY(!tip->property("opened").toBool());
        click(item("tab_summary"));
        QTRY_VERIFY(tip->property("opened").toBool());
        QCOMPARE(tab(), 0);                                // nem vált
        // Átirat nélkül nincs mit indítani: a panel csak megmondja, mi hiányzik.
        QCOMPARE(tip->property("tipTitle").toString(), QStringLiteral("Még nincs vezetői összefoglaló"));
        QVERIFY(tip->property("tipText").toString().contains(QStringLiteral("előbb az átírásnak")));
        QVERIFY(!shown("tabTipAction"));
        QTest::keyClick(m_window, Qt::Key_Escape);
        QTRY_VERIFY(!tip->property("opened").toBool());

        // Az Átirat az átírás közben is megnyitható.
        click(item("tab_transcript"));
        QCOMPARE(tab(), 1);
    }

    // V3: kész megbeszélés — ADATOK, összecsukott lépések / sávok, kinyitva a részletek.
    void done_statsAndCollapsedRows()
    {
        load(QStringLiteral("overviewDone"));
        QVERIFY(shown("statTiles"));
        QVERIFY(!shown("pipelineSteps"));
        QVERIFY(!shown("trackList"));
        QVERIFY(!shown("approvalBanner"));
        click(item("stepsCollapsed"));
        QTRY_VERIFY(shown("pipelineSteps"));
        click(item("tracksCollapsed"));
        QTRY_VERIFY(shown("trackList"));
        // A Vezetői összefoglaló és a Memó itt elérhető: a fülek váltanak.
        click(item("tab_memo"));
        QCOMPARE(tab(), 3);
        click(item("tab_summary"));
        QCOMPARE(tab(), 2);
    }

    // V5 / V6: az üres részek szaggatott kártyák a műveletekkel.
    void emptyAndNobodyCards()
    {
        load(QStringLiteral("overviewEmpty"));
        QVERIFY(shown("peopleEmpty"));
        QVERIFY(shown("soloButton"));
        QVERIFY(shown("emptyAddParticipant"));
        QVERIFY(shown("sourceDescribe"));
        QVERIFY(!shown("participantList"));
        // A leírás helyben adható meg.
        click(item("sourceDescribe"));
        QTRY_VERIFY(shown("sourceEditor"));
        for (const QChar c : QStringLiteral("Q4 review")) QTest::keyClick(m_window, c.toLatin1());
        click(item("sourceSave"));
        QTRY_VERIFY(shown("sourceCard"));
        QCOMPARE(m_window->property("overview").value<QObject*>()->property("vm").value<QObject*>()
                     ->property("contextNote").toString(), QStringLiteral("Q4 review"));
        m_engine.reset();

        load(QStringLiteral("overviewNobody"));
        QVERIFY(shown("peopleNobody"));
        QVERIFY(shown("nameThemButton"));
        QVERIFY(!shown("peopleEmpty"));
    }

    // V4: Átirat — eszköz-sor (Olvasás / Javítás, Ctrl+E) és térkép-dokk; a dokk csak itt (és
    // az összefoglaló füleken) látszik; kattintás a térképen = ugrás.
    void reading_toolsRowAndMapDock()
    {
        load(QStringLiteral("readingMap"));
        QTRY_COMPARE(tab(), 1);
        QVERIFY(shown("modeSegment"));
        QVERIFY(shown("whoWasThereButton"));
        QTRY_VERIFY(shown("mapDock"));
        QVERIFY(!shown("seekSlider"));
        QObject* transcript = object("transcriptTab");
        QVERIFY(transcript);
        QVERIFY(!transcript->property("fixMode").toBool());
        QTest::keyClick(m_window, Qt::Key_1, Qt::ControlModifier);
        QCOMPARE(tab(), 0);
        QTest::keyClick(m_window, Qt::Key_2, Qt::ControlModifier);
        QCOMPARE(tab(), 1);
        QTest::keyClick(m_window, Qt::Key_E, Qt::ControlModifier);
        QTRY_VERIFY(transcript->property("fixMode").toBool());
        auto popoverOpen = [this] {
            QObject* pop = m_window->findChild<QObject*>(QStringLiteral("speakerPopover"));
            return pop && pop->property("visible").toBool();
        };
        QVERIFY2(!popoverOpen(), "Ctrl+E után");
        click(item("modeRead"));
        QVERIFY2(!popoverOpen(), "Olvasás után");   // a sín-fejléc nem takarja az eszköz-sort
        QTRY_VERIFY(!transcript->property("fixMode").toBool());

        QQuickItem* seek = item("mapSeekArea");
        QVERIFY(seek && seek->isVisible());
        QObject* player = m_window->property("player").value<QObject*>();
        QVERIFY(player);
        const QPoint p = seek->mapToScene(QPointF(seek->width() * 0.5, seek->height() / 2)).toPoint();
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, p);
        QTest::qWait(60);
        QVERIFY(!popoverOpen());
        QCOMPARE(tab(), 1);

        // Vissza az Áttekintésre: a dokk helyén a lejátszó.
        QObject* shell = m_window->property("shell").value<QObject*>();
        QSignalSpy spy(shell, SIGNAL(currentTabChanged()));
        QTest::keyClick(m_window, Qt::Key_1, Qt::ControlModifier);
        QCOMPARE(tab(), 0);
        QCOMPARE(spy.count(), 1);
        QTRY_VERIFY(!shown("mapDock"));
        QVERIFY(shown("seekSlider"));
    }

    // A „Megbeszélés” menü a menüsorban és a fejléc „…” gombján ugyanaz.
    void meetingMenu()
    {
        load(QStringLiteral("overviewDone"));
        click(item("meetingMenuButton"));
        QTRY_VERIFY(anyShown("meetingParticipantsItem"));
        QVERIFY(anyShown("recheckSpeakersItem"));
        QVERIFY(anyShown("exportArchiveItem"));
        QVERIFY(anyShown("deleteMeetingItem"));
        QTest::keyClick(m_window, Qt::Key_Escape);
        QTest::qWait(150);
        click(item("meetingMoreButton"));
        QTRY_VERIFY(anyShown("meetingRenameItem"));
        QTest::keyClick(m_window, Qt::Key_Escape);
        QTest::qWait(150);
    }
};

QTEST_MAIN(TestOverviewTab)
#include "test_overview_tab.moc"
