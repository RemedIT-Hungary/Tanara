// A v3 megbeszélés-nézet héja (Main.qml) kijelző nélkül (offscreen), demó-adattal (App.demo,
// nincs AppController): egysoros fejléc a négy füllel, a nem elérhető fül panelje, a
// „Megbeszélés” menü, az Áttekintés részei (banner + „Később”, lépések, sávok, ADATOK,
// összecsukott sorok, üres kártyák), az Átirat eszköz-sora (Olvasás / Javítás, Ctrl+E) és a
// térkép-dokk (csak az Átirat / összefoglaló füleken; kattintás = ugrás). U6: felvétel után,
// átírás előtt is az Áttekintés jelenik meg, az „Átírás indítása” a FELDOLGOZÁS lépésében.
// Javító kör (G1): az első átírás szakaszai és „Megszakítás” a lépésben (A1), jegyzet-sablon és
// észlelt hívás a forrás-kártyán, cím alapú címke-javaslat (A2, A3), kattintható jelvények és a
// névtelen sor elnevezése (A4, A5), „Újraelemzés” és a menüpont új neve (D4), az üres eszköz-sor (S3).
#include "AppContext.h"
#include "QmlApp.h"

#include "tanara/jobs/JobTypes.h"

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QtTest>

#include <memory>

using namespace tanara_qml;

// Ál-héj az Áttekintés indító gombjaihoz (a ShellActions szerződéses felületéből a kellő rész).
class StartSpy : public QObject {
    Q_OBJECT
public:
    QStringList calls;
    Q_INVOKABLE void startTranscription(const QString& id) { calls << "startTranscription:" + id; }
    Q_INVOKABLE void openSettings(const QString& page, const QString& field = QString())
    {
        calls << "openSettings:" + page + (field.isEmpty() ? QString() : ":" + field);
    }
    Q_INVOKABLE void toast(const QString&, const QString& = QString()) { calls << "toast"; }
    Q_INVOKABLE void cancelJob(const QString& id, int kind) { calls << "cancelJob:" + id + ":" + QString::number(kind); }
    Q_INVOKABLE void openSpeakerWhy(const QString& id, const QString& key) { calls << "openSpeakerWhy:" + id + ":" + key; }
    Q_INVOKABLE void identifyParticipants(const QString& id) { calls << "identifyParticipants:" + id; }
};

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

    // G7b: az Áttekintés változás-sávja (a Main a közös verem jelére hívja): üzenet ·
    // „Visszavonás · Ctrl+Z” · ×; visszavonás után „Visszavonva: …” teendő nélkül. Demóban (közös
    // verem nélkül) a sáv maga mondja ki a visszavonást.
    void done_changeBar()
    {
        load(QStringLiteral("overviewDone"));
        QObject* overview = object("overviewTab");
        QVERIFY(!shown("overviewChangeBar"));
        QMetaObject::invokeMethod(overview, "showChange", Q_ARG(QVariant, QStringLiteral("Résztvevő eltávolítva: Lantos Réka")));
        QTRY_VERIFY(shown("overviewChangeBar"));
        QCOMPARE(item("overviewChangeText")->property("text").toString(), QStringLiteral("Résztvevő eltávolítva: Lantos Réka"));
        QVERIFY(shown("overviewChangeUndo"));
        click(item("overviewChangeUndo"));
        QTRY_COMPARE(item("overviewChangeText")->property("text").toString(),
                     QStringLiteral("Visszavonva: Résztvevő eltávolítva: Lantos Réka"));
        QVERIFY(!shown("overviewChangeUndo"));
        click(item("overviewChangeClose"));
        QTRY_VERIFY(!shown("overviewChangeBar"));
        // A nem hallott meghívott (A7) halványan a beszélők után.
        bool dimmedLast = false;
        for (QQuickItem* row : items("participantRow"))
            if (row->isVisible()) dimmedLast = row->opacity() < 1.0;
        QVERIFY(dimmedLast);
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
        QVERIFY(shown("modeReview"));                 // Olvasás · Javítás · Átnézendő (N)
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

    // U6 (V5): felvétel után, átírás előtt az Áttekintés látszik — üres résztvevő-kártya,
    // sávok, lépések; az „Átirat” lépésben az „Átírás indítása”, ami a héj startTranscription
    // akcióját hívja. Az Átirat fül halvány, a panelje is felajánlja az indítást.
    void recorded_overviewBeforeTranscription()
    {
        load(QStringLiteral("overviewRecorded"));
        QCOMPARE(tab(), 0);
        QVERIFY(shown("overviewTab"));
        QVERIFY(shown("pipelineSteps"));
        QVERIFY(shown("step_transcript"));
        QVERIFY(shown("trackList"));
        int switches = 0;                                  // a sávok kapcsolhatók (a látható kapcsolók)
        for (QQuickItem* it : items("trackSwitch")) switches += it->isVisible() ? 1 : 0;
        QCOMPARE(switches, 2);
        int strips = 0;                                    // és beszéd-csíkkal meghallgathatók
        for (QQuickItem* it : items("trackActivityStrip")) strips += it->isVisible() ? 1 : 0;
        QVERIFY(strips >= 2);
        QVERIFY(shown("peopleEmpty"));
        QVERIFY(shown("soloButton"));
        QVERIFY(shown("emptyAddParticipant"));
        QVERIFY(shown("sourceDescribe"));
        QVERIFY(shown("overviewTags"));
        QVERIFY(!shown("approvalBanner"));
        QVERIFY(!shown("statTiles"));
        QVERIFY(!shown("transcribeError"));
        QVERIFY(!shown("transcribeBlocker"));
        QTRY_VERIFY(shown("identifySwitch"));             // a lépések a modell után töltődnek
        QQuickItem* start = item("startTranscriptionButton");
        QVERIFY(start && start->isVisible());
        QVERIFY(start->isEnabled());

        QObject* overview = object("overviewTab");
        QVERIFY(overview);
        StartSpy spy;
        overview->setProperty("shell", QVariant::fromValue<QObject*>(&spy));
        click(start);
        QCOMPARE(spy.calls, QStringList({QStringLiteral("startTranscription:")}));   // demóban üres id

        // Az Átirat fül átírás előtt nem vált, a panel megmondja, mi hiányzik, és indít.
        QObject* tip = object("tabTooltip");
        QVERIFY(tip);
        click(item("tab_transcript"));
        QTRY_VERIFY(tip->property("opened").toBool());
        QCOMPARE(tab(), 0);
        QCOMPARE(tip->property("tipTitle").toString(), QStringLiteral("Még nincs átirat"));
        QTRY_VERIFY(shown("tabTipAction"));
        QTest::keyClick(m_window, Qt::Key_Escape);
        QTRY_VERIFY(!tip->property("opened").toBool());
    }

    // U6: az átírás hibája a lépésben — figyelmet kérő kör, hiba-doboz, „Újra” és a javító művelet.
    void recordedError_retryAndFix()
    {
        load(QStringLiteral("overviewRecordedError"));
        QCOMPARE(tab(), 0);
        QTRY_VERIFY(shown("transcribeError"));
        QVERIFY(!shown("startTranscriptionButton"));
        QVERIFY(shown("transcribeFixButton"));
        QObject* overview = object("overviewTab");
        QVERIFY(overview);
        StartSpy spy;
        overview->setProperty("shell", QVariant::fromValue<QObject*>(&spy));
        click(item("transcribeRetryButton"));
        click(item("transcribeFixButton"));
        QCOMPARE(spy.calls, QStringList({QStringLiteral("startTranscription:"),
                                         QStringLiteral("openSettings:providers:stt")}));
    }

    // Átírás közben / után nincs indító a lépésekben.
    void processing_noStartButton()
    {
        load(QStringLiteral("overviewProcessing"));
        QVERIFY(shown("pipelineSteps"));
        QVERIFY(!shown("startTranscriptionButton"));
        QVERIFY(!shown("transcribeError"));
        QVERIFY(!shown("identifySwitch"));
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

    // A1: az első átírás futása közben az „Átirat” lépésben a szakaszok, a „Közben nyugodtan
    // dolgozz tovább” sor és a „Megszakítás” (a héj cancelJob(…, Transcribe)-ja); nincs indító.
    void transcribing_stagesAndCancel()
    {
        load(QStringLiteral("overviewTranscribing"));
        QCOMPARE(tab(), 0);
        QTRY_VERIFY(shown("transcribeRunning"));
        QVERIFY(items("transcribeStage").size() >= 3);
        QVERIFY(shown("cancelTranscriptionButton"));
        QVERIFY(!shown("startTranscriptionButton"));
        QVERIFY(!shown("transcribeBlocker"));
        QVERIFY(shown("peopleEmpty"));
        QObject* overview = object("overviewTab");
        QVERIFY(overview);
        StartSpy spy;
        overview->setProperty("shell", QVariant::fromValue<QObject*>(&spy));
        click(item("cancelTranscriptionButton"));
        QCOMPARE(spy.calls, QStringList({QStringLiteral("cancelJob::%1").arg(int(tanara::JobKind::Transcribe))}));
        m_engine.reset();

        // V1 (átírás közben, jelöltekkel) is megszakítható a lépésből.
        load(QStringLiteral("overviewProcessing"));
        QTRY_VERIFY(shown("cancelTranscriptionButton"));
    }

    // A3 + A2: új felvételnél a forrás-kártya alatt a sablonok és az észlelt hívás; a sablon a
    // szerkesztőbe kerül (a mentés a felhasználóé). A címkesor a cím alapján javasol.
    void recorded_templatesAndTitleTags()
    {
        load(QStringLiteral("overviewRecorded"));
        QTRY_VERIFY(shown("sourceTemplates"));
        QVERIFY(items("sourceTemplateChip").size() >= 1);
        QVERIFY(shown("sourceDetectedCall"));
        QObject* tags = object("overviewTab")->property("effectiveTags").value<QObject*>();
        QVERIFY(tags);
        QTRY_COMPARE(tags->property("suggestionSource").toString(), QStringLiteral("title"));
        QCOMPARE(tags->property("suggestionLabel").toString(), QStringLiteral("A cím alapján"));
        click(item("sourceTemplateChip"));
        QTRY_VERIFY(shown("sourceEditor"));
        QVERIFY(!item("sourceEditor")->property("text").toString().isEmpty());
        QVERIFY(shown("sourceTemplates"));                 // szerkesztés közben is választható
        QTest::keyClick(m_window, Qt::Key_Escape);
        QTRY_VERIFY(shown("sourceDescribe"));
    }

    // A4: átirat után a jelvény a „Miért ő?” panelt kéri arra a beszélőre (a héjtól); átirat
    // előtt nem kattintható (csak tooltip). A5: a névtelen sor „Elnevezés…” gombja a
    // személyválasztót nyitja; a választott név a sorra kerül (demóban helyben).
    void done_badgesAndNaming()
    {
        load(QStringLiteral("overviewDone"));
        QObject* overview = object("overviewTab");
        StartSpy spy;
        overview->setProperty("shell", QVariant::fromValue<QObject*>(&spy));
        // Az első sor (S1) egyetlen jelvénye az oldal volt („mikrofon”): azt a sáv-chipek váltják
        // (G6), így az első kattintható jelvény a másodiké (S2, #Nordvik).
        click(item("participantBadge"));
        QCOMPARE(spy.calls, QStringList({QStringLiteral("openSpeakerWhy::S2")}));
        QVERIFY(shown("participantTrackChips"));

        QCOMPARE(items("participantNameButton").size(), 6);   // + a nem hallott meghívott (A7)
        int visibleName = 0;
        for (QQuickItem* it : items("participantNameButton")) visibleName += it->isVisible() ? 1 : 0;
        QCOMPARE(visibleName, 1);                         // csak a névtelen „Távoli 1”
        click(item("participantNameButton"));
        QObject* picker = object("overviewNamePicker");
        QVERIFY(picker);
        QTRY_VERIFY(picker->property("opened").toBool());
        QCOMPARE(picker->property("speakerKey").toString(), QStringLiteral("S5"));
        QMetaObject::invokeMethod(picker, "personChosen", Q_ARG(QString, QStringLiteral("Szabó Bence")));
        QObject* vm = overview->property("vm").value<QObject*>();
        bool named = false;
        for (const QVariant& v : vm->property("participants").toList())
            named = named || v.toMap().value("name").toString() == QStringLiteral("Szabó Bence");
        QVERIFY(named);
        QTest::keyClick(m_window, Qt::Key_Escape);
        QTRY_VERIFY(!picker->property("opened").toBool());
        m_engine.reset();

        load(QStringLiteral("overviewProcessing"));
        overview = object("overviewTab");
        overview->setProperty("shell", QVariant::fromValue<QObject*>(&spy));
        spy.calls.clear();
        click(item("participantBadge"));
        QVERIFY(spy.calls.isEmpty());                    // átirat előtt csak tooltip
    }

    // D4: a hanglenyomat-lépés „Újraelemzés”-e (átirat után = a menü „Hanglenyomat-elemzés újra”).
    void done_reanalyzeAndMenuName()
    {
        load(QStringLiteral("overviewDone"));
        click(item("stepsCollapsed"));
        QTRY_VERIFY(shown("reanalyzeButton"));
        QObject* overview = object("overviewTab");
        StartSpy spy;
        overview->setProperty("shell", QVariant::fromValue<QObject*>(&spy));
        // A jobb oszlop görgetése a lépés aljáig (a kinyitott lépések a lejátszó alá nyúlnak).
        QObject* right = object("overviewRight");
        QVERIFY(right);
        QTRY_VERIFY(right->property("contentHeight").toReal() > right->property("height").toReal());
        right->setProperty("contentY", right->property("contentHeight").toReal() - right->property("height").toReal());
        QTest::qWait(100);
        click(item("reanalyzeButton"));
        QCOMPARE(spy.calls, QStringList({QStringLiteral("identifyParticipants:")}));
        click(item("meetingMoreButton"));
        QTRY_VERIFY(anyShown("identifyItem"));
        QQuickItem* identify = nullptr;
        for (QQuickItem* it : items("identifyItem")) if (it->isVisible()) identify = it;
        QVERIFY(identify);
        QCOMPARE(identify->property("text").toString(), QStringLiteral("Hanglenyomat-elemzés újra"));
        QTest::keyClick(m_window, Qt::Key_Escape);
        QTest::qWait(150);
    }

    // S3: az eszköz-sor rejtett gyerekkel 0 magas (nem marad üres 46 px), látható gyerekkel 46.
    void toolsRow_hiddenChildMeansEmpty()
    {
        QQmlEngine engine;
        setupEngine(engine);
        QQmlComponent c(&engine);
        c.setData("import QtQuick\nimport Tanara\nMeetingToolsRow { width: 400; Item { objectName: \"kid\"; visible: false } }",
                  QUrl());
        std::unique_ptr<QObject> row(c.create());
        QVERIFY2(row, qPrintable(c.errorString()));
        auto* item = qobject_cast<QQuickItem*>(row.get());
        QVERIFY(!item->property("hasContent").toBool());
        QCOMPARE(item->implicitHeight(), 0.0);
        QObject* kid = row->findChild<QObject*>(QStringLiteral("kid"));
        QVERIFY(kid);
        kid->setProperty("visible", true);
        QTRY_VERIFY(item->property("hasContent").toBool());
        QCOMPARE(item->implicitHeight(), 46.0);
        kid->setProperty("visible", false);
        QTRY_VERIFY(!item->property("hasContent").toBool());
    }
};

QTEST_MAIN(TestOverviewTab)
#include "test_overview_tab.moc"
