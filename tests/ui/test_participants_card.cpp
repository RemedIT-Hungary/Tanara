// The Overview's "Who was there?" card (ParticipantsCard, A variant) in the main window without a
// display (offscreen), on the demo data (App.demo, OverviewDemoFacade: the operations apply in
// memory): the pending decision's header / footer and "Approve · N", "Was there" on a doubtful row,
// the naming panel ("This is me" first or last, with the failure reason; naming; "Same voice as..."),
// "+ Participant" -> "Which voice is it?" ("That's them" / "None of them" -> a voiceless row), the
// hidden zero-line speakers, "Only I spoke" hiding the other rows, "Approve" / "Skip" in place (the
// skipped header; "Skipped · N names by hand" once every row is named by hand, also in English), the
// track rule sentence (G10) and the track control (buttons; a drop-down with 3+ tracks), the no
// voice model header.
#include "AppContext.h"
#include "QmlApp.h"
#include "../unit/ts_translator.h"

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

#include <memory>

using namespace tanara_qml;

class TestParticipantsCard : public QObject {
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
        QVERIFY(loadPage(*m_engine, QStringLiteral("Main"), {{"demoState", demoState}}, QSize(1280, 1400)));
        m_window = qobject_cast<QQuickWindow*>(m_engine->rootObjects().value(0));
        if (!m_window) m_window = m_engine->findChild<QQuickWindow*>();
        QVERIFY(m_window);
        m_window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(m_window));
        QTest::qWait(200);
    }
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
    QList<QQuickItem*> visibleItems(const char* name) const
    {
        QList<QQuickItem*> out;
        for (QQuickItem* it : items(name))
            if (it->isVisible()) out << it;
        return out;
    }
    QQuickItem* item(const char* name) const
    {
        const QList<QQuickItem*> shownItems = visibleItems(name);
        return shownItems.isEmpty() ? items(name).value(0, nullptr) : shownItems.first();
    }
    bool shown(const char* name) const { return !visibleItems(name).isEmpty(); }
    QObject* object(const char* name) const
    {
        return m_window ? m_window->findChild<QObject*>(QLatin1String(name)) : nullptr;
    }
    QString text(const char* name) const
    {
        QQuickItem* it = item(name);
        return it ? it->property("text").toString() : QString();
    }
    void click(QQuickItem* it)
    {
        QVERIFY(it);
        const QPoint p = it->mapToScene(QPointF(it->width() / 2, it->height() / 2)).toPoint();
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, p);
        QTest::qWait(80);
    }
    QObject* card() const { return object("participantList"); }
    QObject* vm() const { return object("overviewTab")->property("vm").value<QObject*>(); }
    QVariantMap row(const QString& id) const
    {
        for (const QVariant& v : vm()->property("participants").toList())
            if (v.toMap().value("id").toString() == id) return v.toMap();
        return {};
    }

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

    // The decision pending: blue header with the count of voices, every row, the footer with what is
    // left out and "Approve · N" (sure + named rows); the track rule does not hold yet.
    void pending_headerFooterAndCount()
    {
        load(QStringLiteral("overviewProcessing"));
        QVERIFY(shown("participantsHeader"));
        QVERIFY(text("participantsHeaderTitle").contains(QStringLiteral("5 hangot")));
        QCOMPARE(visibleItems("participantRow").size(), 6);
        QCOMPARE(text("approveButton"), QStringLiteral("Jóváhagyás · 2 résztvevő"));
        QVERIFY(text("participantsLeftOut").contains(QStringLiteral("1 kétes")));
        QVERIFY(text("participantsLeftOut").contains(QStringLiteral("2 névtelen hang")));
        QCOMPARE(visibleItems("participantDoubtPill").size(), 1);
        QVERIFY(text("trackRuleText").startsWith(QStringLiteral("A sávszabály nem él")));
        // Every heard row has a sample button; the voice rows have the track control.
        QCOMPARE(visibleItems("participantSamplePlay").size(), 5);
        QCOMPARE(visibleItems("participantTrackControl").size(), 5);
        QVERIFY(!shown("participantTrackDropDown"));
    }

    // "Was there" on the doubtful row: counted; pressed again: undone.
    void confirm_doubtfulRow()
    {
        load(QStringLiteral("overviewProcessing"));
        click(item("participantConfirmButton"));     // the first: Varga Árpád (doubtful)
        QTRY_COMPARE(text("approveButton"), QStringLiteral("Jóváhagyás · 3 résztvevő"));
        QVERIFY(row(QStringLiteral("p4")).value("approved").toBool());
        QVERIFY(!shown("participantDoubtPill"));
        click(item("participantConfirmButton"));
        QTRY_COMPARE(text("approveButton"), QStringLiteral("Jóváhagyás · 2 résztvevő"));
    }

    // "Name...": the panel (sample on top; the own voice is on the list -> "This is me" at the bottom);
    // a name goes onto the row and counts.
    void naming_panel()
    {
        load(QStringLiteral("overviewProcessing"));
        click(item("participantNameButton"));          // Unknown voice 1
        QObject* panel = object("participantNamePanel");
        QVERIFY(panel);
        QTRY_VERIFY(panel->property("opened").toBool());
        QCOMPARE(panel->property("row").toMap().value("id").toString(), QStringLiteral("v1"));
        QVERIFY(shown("namePanelSample"));
        QVERIFY(shown("namePanelSelfBottom"));
        QVERIFY(!shown("namePanelSelfTop"));
        QVERIFY(shown("namePanelSameVoice"));
        QVERIFY(shown("namePanelRemove"));
        QMetaObject::invokeMethod(panel, "chooseName", Q_ARG(QVariant, QStringLiteral("Tóth Bence")));
        QTRY_VERIFY(!panel->property("opened").toBool());
        QCOMPARE(row(QStringLiteral("v1")).value("name").toString(), QStringLiteral("Tóth Bence"));
        QTRY_COMPARE(text("approveButton"), QStringLiteral("Jóváhagyás · 3 résztvevő"));
        m_engine.reset();

        // Nobody is the own voice yet: "This is me" is the first row.
        load(QStringLiteral("overviewNobody"));
        click(item("participantNameButton"));
        panel = object("participantNamePanel");
        QTRY_VERIFY(panel->property("opened").toBool());
        QVERIFY(shown("namePanelSelfTop"));
        QVERIFY(!shown("namePanelSelfBottom"));
        QMetaObject::invokeMethod(panel, "chooseSelf");
        QTRY_VERIFY(!panel->property("opened").toBool());
        QVERIFY(row(QStringLiteral("S1")).value("self").toBool());
    }

    // "This is me" that fails: the panel stays open and says why (the core's reason).
    void naming_selfReason()
    {
        QQmlEngine engine;
        setupEngine(engine);
        QQmlComponent c(&engine);
        c.setData("import QtQuick\nimport Tanara\n"
                  "Item { width: 700; height: 700\n"
                  "  property alias panel: p\n"
                  "  QtObject { id: fake; property string selfName: \"\"\n"
                  "    function setSelfParticipant(id) { return { ok: false, reason: \"Nincs megadva a saját neved.\" } } }\n"
                  "  ParticipantNamePanel { id: p; vm: fake } }",
                  QUrl());
        std::unique_ptr<QObject> root(c.create());
        QVERIFY2(root, qPrintable(c.errorString()));
        QObject* panel = root->property("panel").value<QObject*>();
        QVERIFY(panel);
        panel->setProperty("row", QVariantMap{{"id", "v1"}, {"anonymous", true}});
        QMetaObject::invokeMethod(panel, "chooseSelf");
        QCOMPARE(panel->property("errorText").toString(), QStringLiteral("Nincs megadva a saját neved."));
    }

    // "Same voice as...": the other rows; one click merges (meeting-local), the row disappears.
    void naming_sameVoice()
    {
        load(QStringLiteral("overviewSameVoice"));
        QObject* panel = object("participantNamePanel");
        QVERIFY(panel);
        QTRY_VERIFY(panel->property("opened").toBool());
        QVERIFY(panel->property("mergeOpen").toBool());
        QTRY_COMPARE(visibleItems("namePanelMergeTarget").size(), 4);
        click(visibleItems("namePanelMergeTarget").first());   // Kovács Lilla (you)
        QTRY_VERIFY(!panel->property("opened").toBool());
        QVERIFY(row(QStringLiteral("v2")).isEmpty());
        QCOMPARE(visibleItems("participantRow").size(), 4);
    }

    // "+ Participant" -> a person -> "Which voice is it?": "That's them" names the voice; "None of
    // them" adds a voiceless row, whose "Which voice?" opens the same panel.
    void addParticipant_whichVoice()
    {
        load(QStringLiteral("overviewProcessing"));
        QMetaObject::invokeMethod(card(), "addPerson", Q_ARG(QVariant, QStringLiteral("Tóth Bence")),
                                  Q_ARG(QVariant, QVariant()));
        QObject* panel = object("whichVoicePanel");
        QVERIFY(panel);
        QTRY_VERIFY(panel->property("opened").toBool());
        QTRY_COMPARE(visibleItems("whichVoiceRow").size(), 2);
        click(visibleItems("whichVoiceThisOne").first());
        QTRY_VERIFY(!panel->property("opened").toBool());
        QCOMPARE(row(QStringLiteral("v1")).value("name").toString(), QStringLiteral("Tóth Bence"));

        QMetaObject::invokeMethod(card(), "addPerson", Q_ARG(QVariant, QStringLiteral("Szabó Bence")),
                                  Q_ARG(QVariant, QVariant()));
        QTRY_VERIFY(panel->property("opened").toBool());
        QTRY_COMPARE(visibleItems("whichVoiceRow").size(), 1);
        click(item("whichVoiceNone"));
        QTRY_VERIFY(!panel->property("opened").toBool());
        bool voiceless = false;
        for (const QVariant& v : vm()->property("participants").toList())
            if (v.toMap().value("name").toString() == QStringLiteral("Szabó Bence"))
                voiceless = v.toMap().value("voiceless").toBool();
        QVERIFY(voiceless);
        QVERIFY(shown("participantWhichVoiceButton"));
        click(item("participantWhichVoiceButton"));
        QTRY_VERIFY(panel->property("opened").toBool());
        QCOMPARE(panel->property("personName").toString(), QStringLiteral("Szabó Bence"));
    }

    // After the transcript: the anonymous speaker without a line is not a row, one sentence says so;
    // the rows carry their line count.
    void done_hiddenZeroLineSpeaker()
    {
        load(QStringLiteral("overviewDone"));
        QVERIFY(shown("hiddenSpeakersNote"));
        QCOMPARE(visibleItems("participantRow").size(), 6);
        bool lines = false;
        for (QQuickItem* sub : visibleItems("participantSub"))
            lines = lines || sub->property("text").toString().contains(QStringLiteral("sor"));
        QVERIFY(lines);
        QVERIFY(text("participantsHeaderTitle").startsWith(QStringLiteral("Jóváhagyva")));
    }

    // "Only I spoke": the other rows are hidden, the header states the decision.
    void solo_hidesOtherRows()
    {
        load(QStringLiteral("overviewProcessing"));
        click(item("soloButton"));
        QTRY_COMPARE(visibleItems("participantRow").size(), 1);
        QCOMPARE(text("participantsHeaderTitle"), QStringLiteral("Csak én beszéltem"));
        QVERIFY(!shown("approveButton"));
        QVERIFY(!shown("trackRuleLine"));
        m_engine.reset();

        load(QStringLiteral("overviewSolo"));
        QCOMPARE(visibleItems("participantRow").size(), 1);
    }

    // "Approve · N" decides in place: the header says it, the rows stay, the footer goes.
    void approve_inPlace()
    {
        load(QStringLiteral("overviewProcessing"));
        click(item("approveButton"));
        QTRY_COMPARE(text("participantsHeaderTitle"), QStringLiteral("Jóváhagyva · 2 résztvevő"));
        QVERIFY(!shown("approveButton"));
        QVERIFY(!shown("participantsFooter"));
        QCOMPARE(visibleItems("participantRow").size(), 6);
    }

    // "Skip" decides in place: the header says the approval was skipped and that hand-given names apply
    // at once; once every row is named by hand it counts them (English from the shipped .ts too).
    void skip_inPlaceThenNamedByHand()
    {
        load(QStringLiteral("overviewProcessing"));
        click(item("skipButton"));
        QTRY_COMPARE(text("participantsHeaderTitle"), QStringLiteral("Jóváhagyás kihagyva"));
        QCOMPARE(text("participantsHeaderText"),
                 QStringLiteral("A jelölteket nem hagytad jóvá; az átirat után a hangfelismerés dönt, "
                                "a kézzel adott nevek azonnal érvényesek."));
        QVERIFY(!shown("participantsFooter"));
        // Naming one row by hand is not enough; naming every row is.
        QStringList ids;
        for (const QVariant& v : vm()->property("participants").toList())
            if (!v.toMap().value("suggested").toBool()) ids << v.toMap().value("id").toString();
        QVERIFY(ids.size() >= 2);
        for (int i = 0; i < ids.size(); ++i) {
            bool ok = false;
            QMetaObject::invokeMethod(vm(), "nameParticipant", Q_RETURN_ARG(bool, ok),
                                      Q_ARG(QString, ids.at(i)), Q_ARG(QString, QStringLiteral("Kézi Név %1").arg(i + 1)));
            QVERIFY(ok);
            if (i + 1 < ids.size())
                QTRY_COMPARE(text("participantsHeaderTitle"), QStringLiteral("Jóváhagyás kihagyva"));
        }
        const int named = int(card()->property("ownRows").toList().size());
        QTRY_COMPARE(text("participantsHeaderTitle"), QStringLiteral("Kihagyva · %1 név kézzel").arg(named));

        // English: the plural form and the new texts from the shipped .ts.
        tstest::TsTranslator tr;
        QVERIFY(tr.loadTs(QStringLiteral(TANARA_EN_TS)));
        QCoreApplication::installTranslator(&tr);
        m_engine->retranslate();
        QTRY_COMPARE(text("participantsHeaderTitle"), QStringLiteral("Skipped · %1 names by hand").arg(named));
        QCOMPARE(text("participantsHeaderText"),
                 QStringLiteral("You did not approve the candidates; after the transcript, voice recognition decides; "
                                "names given by hand apply immediately."));
        QCOMPARE(QCoreApplication::translate("ParticipantsCard", "Kihagyva · %n név kézzel", "", 1),
                 QStringLiteral("Skipped · 1 name by hand"));
        QCOMPARE(QCoreApplication::translate("ParticipantsCard", "Jóváhagyás kihagyva"), QStringLiteral("Approval skipped"));
        QCoreApplication::removeTranslator(&tr);
        m_engine->retranslate();
    }

    // The track rule (G10): holds when every named speaker is bound by hand; one click on a bound
    // track button unbinds it and the rule goes.
    void trackRule_andTrackControl()
    {
        load(QStringLiteral("overviewApproved"));
        QVERIFY(text("trackRuleText").startsWith(QStringLiteral("A sávszabály él")));
        QQuickItem* micButton = item("participantTrackButton_mic");   // the first row: the own mic, bound
        click(micButton);
        QTRY_VERIFY(text("trackRuleText").startsWith(QStringLiteral("A sávszabály nem él")));
        m_engine.reset();

        load(QStringLiteral("overviewThreeTracks"));
        QVERIFY(shown("participantTrackDropDown"));
        QVERIFY(!shown("participantTrackButton_call"));
    }

    // No voice model: a warning header pointing to the PROCESSING step (no second settings button
    // on the card); the manual row has no "Which voice?" (there is no unnamed voice).
    void noVoiceModel_header()
    {
        load(QStringLiteral("overviewNoVoiceModel"));
        QVERIFY(text("participantsHeaderTitle").startsWith(QStringLiteral("Nincs hangmodell")));
        QCOMPARE(visibleItems("participantRow").size(), 1);
        QVERIFY(!shown("participantWhichVoiceButton"));
        QVERIFY(shown("soloButton"));
        QQuickItem* cardItem = qobject_cast<QQuickItem*>(card());
        QVERIFY(cardItem);
        QList<QQuickItem*> fix;
        collect(cardItem, QStringLiteral("voiceFixButton"), fix);
        QVERIFY(fix.isEmpty());
    }
};

QTEST_MAIN(TestParticipantsCard)
#include "test_participants_card.moc"
