// The meeting view shell (Main.qml) without a display (offscreen), with demo data (App.demo, no
// AppController): the one-line header with the four tabs, the panel of an unavailable tab, the
// "Meeting" menu, the parts of the Overview (the "Who was there?" card's decision, steps, tracks,
// DATA, collapsed rows, empty cards), the Transcript tool row (Read / Edit, Ctrl+E) and the map dock (only on
// the Transcript / summary tabs; click = jump). After a recording, before transcription, the
// Overview shows too, with "Start transcription" in the PROCESSING step.
// Also covered: the first transcription's stages and "Cancel" in the step, note template and
// detected call on the source card, title-based tag suggestion, clickable badges and naming an
// anonymous line, "Re-analyze" and the menu item's new name, the empty tool row.
#include "AppContext.h"
#include "QmlApp.h"

#include "tanara/domain/Job.h"

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QtTest>

#include <memory>

using namespace tanara_qml;

// Fake shell for the Overview's start buttons (the needed part of the ShellActions contract surface).
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
    // In the visual tree (also the Repeater delegates and the popups' content); the visible
    // instance is preferred if there are several.
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
    // Whether any of several same-named items (the two "Meeting" menus) is visible.
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

    // The Overview is the first tab; the card's "Skip" decides in place; the unavailable summary
    // tab opens a panel on click (and does not switch), a running transcription shows on the tab.
    void processing_bannerTabsAndPanel()
    {
        load(QStringLiteral("overviewProcessing"));
        QCOMPARE(tab(), 0);
        QVERIFY(shown("overviewTab"));
        QVERIFY(shown("pipelineSteps"));
        QVERIFY(shown("trackList"));
        QVERIFY(shown("participantList"));
        QVERIFY(!shown("statTiles"));
        QVERIFY(!shown("mapDock"));                       // the plain player on the Overview
        QVERIFY(shown("seekSlider"));
        // The decision is the card's header + footer ("Who was there?" A variant; no banner, no modal).
        QVERIFY(shown("participantsHeader"));
        QVERIFY(shown("approveButton"));
        QVERIFY(shown("skipButton"));
        QMetaObject::invokeMethod(object("participantList"), "skip");   // below the fold at 820 px
        QTRY_VERIFY(!shown("skipButton"));
        QCOMPARE(item("participantsHeaderTitle")->property("text").toString(), QStringLiteral("Kihagyva"));

        QObject* tip = object("tabTooltip");
        QVERIFY(tip);
        QVERIFY(!tip->property("opened").toBool());
        click(item("tab_summary"));
        QTRY_VERIFY(tip->property("opened").toBool());
        QCOMPARE(tab(), 0);                                // does not switch
        // Without a transcript there is nothing to start: the panel only says what is missing.
        QCOMPARE(tip->property("tipTitle").toString(), QStringLiteral("Még nincs vezetői összefoglaló"));
        QVERIFY(tip->property("tipText").toString().contains(QStringLiteral("előbb az átírásnak")));
        QVERIFY(!shown("tabTipAction"));
        QTest::keyClick(m_window, Qt::Key_Escape);
        QTRY_VERIFY(!tip->property("opened").toBool());

        // The Transcript can be opened during transcription too.
        click(item("tab_transcript"));
        QCOMPARE(tab(), 1);
    }

    // A finished meeting: DATA, collapsed steps / tracks, details when expanded.
    void done_statsAndCollapsedRows()
    {
        load(QStringLiteral("overviewDone"));
        QVERIFY(shown("statTiles"));
        QVERIFY(!shown("pipelineSteps"));
        QVERIFY(!shown("trackList"));
        QVERIFY(!shown("approveButton"));
        click(item("stepsCollapsed"));
        QTRY_VERIFY(shown("pipelineSteps"));
        click(item("tracksCollapsed"));
        QTRY_VERIFY(shown("trackList"));
        // The Executive summary and the Memo are available here: the tabs switch.
        click(item("tab_memo"));
        QCOMPARE(tab(), 3);
        click(item("tab_summary"));
        QCOMPARE(tab(), 2);
    }

    // The Overview's change bar (Main calls it on the shared stack's signal): message, "Undo
    // Ctrl+Z", x; after an undo "Undone: ..." with no action. In demo mode (without the shared
    // stack) the bar announces the undo itself.
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
        // An invited participant who was not heard appears dimmed after the speakers.
        bool dimmedLast = false;
        for (QQuickItem* row : items("participantRow"))
            if (row->isVisible()) dimmedLast = row->opacity() < 1.0;
        QVERIFY(dimmedLast);
    }

    // Empty parts are dashed cards with the actions.
    void emptyAndNobodyCards()
    {
        // The analysis runs: the card's grey header, placeholders; adding by hand and "Only I spoke" work.
        load(QStringLiteral("overviewEmpty"));
        QVERIFY(!shown("peopleEmpty"));
        QVERIFY(shown("participantsHeader"));
        QVERIFY(shown("soloButton"));
        QVERIFY(shown("participantsAction"));
        QVERIFY(!shown("approveButton"));
        QVERIFY(shown("sourceDescribe"));
        QVERIFY(!shown("participantRow"));
        // The description can be entered in place.
        click(item("sourceDescribe"));
        QTRY_VERIFY(shown("sourceEditor"));
        for (const QChar c : QStringLiteral("Q4 review")) QTest::keyClick(m_window, c.toLatin1());
        click(item("sourceSave"));
        QTRY_VERIFY(shown("sourceCard"));
        QCOMPARE(m_window->property("overview").value<QObject*>()->property("vm").value<QObject*>()
                     ->property("contextNote").toString(), QStringLiteral("Q4 review"));
        m_engine.reset();

        // Everyone anonymous after the transcript: the speakers are rows, nameable on the card.
        load(QStringLiteral("overviewNobody"));
        QVERIFY(shown("participantsHeader"));
        int nameable = 0;
        for (QQuickItem* it : items("participantNameButton")) nameable += it->isVisible() ? 1 : 0;
        QCOMPARE(nameable, 3);
        QVERIFY(!shown("peopleEmpty"));
    }

    // Transcript: tool row (Read / Edit, Ctrl+E) and the map dock; the dock shows only here (and
    // on the summary tabs); a click on the map = jump.
    void reading_toolsRowAndMapDock()
    {
        load(QStringLiteral("readingMap"));
        QTRY_COMPARE(tab(), 1);
        QVERIFY(shown("modeSegment"));
        QVERIFY(shown("modeReview"));                 // Read, Edit, To review (N)
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
        QVERIFY2(!popoverOpen(), "Olvasás után");   // the rail header does not cover the tool row
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

        // Back to the Overview: the player takes the dock's place.
        QObject* shell = m_window->property("shell").value<QObject*>();
        QSignalSpy spy(shell, SIGNAL(currentTabChanged()));
        QTest::keyClick(m_window, Qt::Key_1, Qt::ControlModifier);
        QCOMPARE(tab(), 0);
        QCOMPARE(spy.count(), 1);
        QTRY_VERIFY(!shown("mapDock"));
        QVERIFY(shown("seekSlider"));
    }

    // After a recording, before transcription, the Overview shows: an empty participant card,
    // tracks, steps; in the Transcript step "Start transcription", which calls the shell's
    // startTranscription action. The Transcript tab is dimmed, and its panel offers the start too.
    void recorded_overviewBeforeTranscription()
    {
        load(QStringLiteral("overviewRecorded"));
        QCOMPARE(tab(), 0);
        QVERIFY(shown("overviewTab"));
        QVERIFY(shown("pipelineSteps"));
        QVERIFY(shown("step_transcript"));
        QVERIFY(shown("trackList"));
        int switches = 0;                                  // the tracks can be switched (the visible switches)
        for (QQuickItem* it : items("trackSwitch")) switches += it->isVisible() ? 1 : 0;
        QCOMPARE(switches, 2);
        int strips = 0;                                    // and listened to with the speech strip
        for (QQuickItem* it : items("trackActivityStrip")) strips += it->isVisible() ? 1 : 0;
        QVERIFY(strips >= 2);
        QVERIFY(shown("peopleEmpty"));
        QVERIFY(shown("soloButton"));
        QVERIFY(shown("emptyAddParticipant"));
        QVERIFY(shown("sourceDescribe"));
        QVERIFY(shown("overviewTags"));
        QVERIFY(!shown("approveButton"));
        QVERIFY(!shown("statTiles"));
        QVERIFY(!shown("transcribeError"));
        QVERIFY(!shown("transcribeBlocker"));
        QTRY_VERIFY(shown("identifySwitch"));             // the steps load after the model
        QQuickItem* start = item("startTranscriptionButton");
        QVERIFY(start && start->isVisible());
        QVERIFY(start->isEnabled());

        QObject* overview = object("overviewTab");
        QVERIFY(overview);
        StartSpy spy;
        overview->setProperty("shell", QVariant::fromValue<QObject*>(&spy));
        click(start);
        QCOMPARE(spy.calls, QStringList({QStringLiteral("startTranscription:")}));   // empty id in demo mode

        // The Transcript tab does not switch before transcription; the panel says what is missing and starts.
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

    // A transcription error in the step: an attention circle, an error box, "Retry" and the fix action.
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

    // During / after transcription the steps have no start.
    void processing_noStartButton()
    {
        load(QStringLiteral("overviewProcessing"));
        QVERIFY(shown("pipelineSteps"));
        QVERIFY(!shown("startTranscriptionButton"));
        QVERIFY(!shown("transcribeError"));
        QVERIFY(!shown("identifySwitch"));
    }

    // The "Meeting" menu is the same in the menu bar and on the header's "..." button.
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

    // "Who was there?" from the Transcript tab (menu entry): no dialog; the Overview tab comes up and the
    // card's frame flashes (ShellActions.openParticipants -> participantsFocusRequested -> focusParticipants).
    void participantsEntryFlashesTheCard()
    {
        load(QStringLiteral("overviewDone"));
        QTest::keyClick(m_window, Qt::Key_2, Qt::ControlModifier);
        QTRY_COMPARE(tab(), 1);
        click(item("meetingMenuButton"));
        QTRY_VERIFY(anyShown("meetingParticipantsItem"));
        click(item("meetingParticipantsItem"));
        QTRY_COMPARE(tab(), 0);
        QObject* card = object("participantList");
        QVERIFY(card);
        QTRY_VERIFY(card->property("flashing").toBool());
        QTRY_VERIFY(item("participantsFlash") && item("participantsFlash")->opacity() > 0.1);
        QVERIFY(!object("participantsDialog"));
        QTRY_VERIFY(!card->property("flashing").toBool());   // two blinks, then it settles
    }

    // First transcription running: in the "Transcript" step the stages, the "Feel free to keep
    // working meanwhile" line and "Cancel" (the shell's cancelJob(..., Transcribe)); no start.
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

        // Can also be canceled from the step (with candidates, while transcribing).
        load(QStringLiteral("overviewProcessing"));
        QTRY_VERIFY(shown("cancelTranscriptionButton"));
    }

    // For a new recording: the templates and the detected call under the source card; the template
    // goes into the editor (saving is the user's). The tag row suggests based on the title.
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
        QVERIFY(shown("sourceTemplates"));                 // can be chosen while editing too
        QTest::keyClick(m_window, Qt::Key_Escape);
        QTRY_VERIFY(shown("sourceDescribe"));
    }

    // After the transcript the badge asks the "Why this one?" panel for that speaker (from the
    // shell); before the transcript it is not clickable (tooltip only). The "Name..." button of an
    // anonymous line opens the card's naming panel; the chosen name goes onto the line (in place in demo mode).
    void done_badgesAndNaming()
    {
        load(QStringLiteral("overviewDone"));
        QObject* overview = object("overviewTab");
        StartSpy spy;
        overview->setProperty("shell", QVariant::fromValue<QObject*>(&spy));
        // The first line's only badge was the side ("microphone"): the track chips replace it, so the
        // first clickable badge is the second one.
        click(item("participantBadge"));
        QCOMPARE(spy.calls, QStringList({QStringLiteral("openSpeakerWhy::S2")}));
        QVERIFY(shown("participantTrackControl"));

        QCOMPARE(items("participantNameButton").size(), 6);   // + the invited participant who was not heard
        int visibleName = 0;
        for (QQuickItem* it : items("participantNameButton")) visibleName += it->isVisible() ? 1 : 0;
        QCOMPARE(visibleName, 1);                         // only the anonymous "Remote 1"
        click(item("participantNameButton"));
        QObject* picker = object("participantNamePanel");
        QVERIFY(picker);
        QTRY_VERIFY(picker->property("opened").toBool());
        QCOMPARE(picker->property("row").toMap().value("speakerKey").toString(), QStringLiteral("S5"));
        QMetaObject::invokeMethod(picker, "chooseName", Q_ARG(QVariant, QStringLiteral("Szabó Bence")));
        QTRY_VERIFY(!picker->property("opened").toBool());
        QObject* vm = overview->property("vm").value<QObject*>();
        bool named = false;
        for (const QVariant& v : vm->property("participants").toList())
            named = named || v.toMap().value("name").toString() == QStringLiteral("Szabó Bence");
        QVERIFY(named);
        m_engine.reset();

        load(QStringLiteral("overviewProcessing"));
        overview = object("overviewTab");
        overview->setProperty("shell", QVariant::fromValue<QObject*>(&spy));
        spy.calls.clear();
        click(item("participantBadge"));
        QVERIFY(spy.calls.isEmpty());                    // before the transcript only a tooltip
    }

    // The voiceprint step's "Re-analyze" (after the transcript = the menu's "Re-run voiceprint analysis").
    void done_reanalyzeAndMenuName()
    {
        load(QStringLiteral("overviewDone"));
        click(item("stepsCollapsed"));
        QTRY_VERIFY(shown("reanalyzeButton"));
        QObject* overview = object("overviewTab");
        StartSpy spy;
        overview->setProperty("shell", QVariant::fromValue<QObject*>(&spy));
        // Scrolling the right column to the bottom of the step (expanded steps extend below the player).
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

    // The tool row with a hidden child has height 0 (no empty 46 px remains), with a visible child 46.
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
