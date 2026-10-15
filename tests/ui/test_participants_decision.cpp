// The decision side of the Overview's "Who was there?" card: ParticipantsViewModel with a real
// AppController in an ISOLATED TANARA_HOME (candidates written into meeting.json, no audio analysis):
// the default checks (a contradicted candidate is unchecked), approve / skip / "They were not there"
// reaching the meeting, a name given by hand after Skip marked as such (the card's "Skipped · N names
// by hand" header counts these rows), and the tag-learning question after an approval (real data; and the bar
// itself, TagLearnPopup, on the made-up demo data).
#include "AppContext.h"
#include "ParticipantsViewModel.h"
#include "QmlApp.h"
#include "FacadeTestSupport.h"

#include "tanara/app/AppController.h"
#include "tanara/app/Services.h"
#include "tanara/editing/participants/ParticipantService.h"
#include "tanara/library/IOverviewFacade.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/tags/TagService.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace tanara_qml;
using namespace tanara;

namespace {

Evidence evidence(EvidenceKind k, Polarity p, const QString& text)
{
    Evidence e;
    e.kind = k;
    e.polarity = p;
    e.text = text;
    return e;
}

Participant participant(const QString& id, const PersonId& person, QVector<Evidence> evs,
                        const QStringList& sides, ParticipantSource source = ParticipantSource::Voice)
{
    Participant p;
    p.id = id;
    p.personId = person;
    p.evidence = std::move(evs);
    p.sides = sides;
    p.source = source;
    return p;
}

const char* kLearnHost = R"(
import QtQuick
import QtQuick.Controls
import Tanara
ApplicationWindow {
    width: 1100; height: 760
    color: Theme.bg
    QtObject {
        id: fakeShell
        objectName: "shell"
        property string toasts: ""
        function toast(text, undoKey) { toasts += text + "|" + (undoKey || "") + ";" }
    }
    ParticipantsViewModel {
        id: decision
        demoState: "learnQuestion"
    }
    TagLearnPopup {
        id: popup
        objectName: "popup"
        vm: decision
        shell: fakeShell
    }
    Component.onCompleted: {
        decision.approve()
        popup.openIfAsked()
    }
}
)";

} // namespace

class TestParticipantsDecision : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<app::AppController> m_app;
    QString m_meetingId;

    // The facades of the test controller (the view model is built on them).
    const UiServices& services() { return AppContext::instance()->servicesFor(m_app.get()); }

    PersonId pid(const QString& name) { return name.isEmpty() ? PersonId() : m_app->people()->ensure(name); }
    QString nameOf(const PersonId& id) const { return m_app->people()->displayName(id); }

    // Lilla (mic + call audio): certain; Gábor (call audio only): doubtful, checked; Bence (call
    // audio + contradicting tag): doubtful, unchecked; Réka (calendar, not heard): invited;
    // anonymous voice: doubtful, cannot be checked until it has a name.
    void seedMeeting(bool transcript)
    {
        Meeting m = m_app->store()->createMeeting(QStringLiteral("Negyedéves partnertalálkozó"));
        m.participants = {
            participant("v1", pid("Kovács Lilla"),
                        {evidence(EvidenceKind::Side, Polarity::Support, "mikrofon"),
                         evidence(EvidenceKind::Voice, Polarity::Support, "hang 96%")}, {"mic"}),
            participant("v2", pid("Fehér Gábor"), {evidence(EvidenceKind::Voice, Polarity::Support, "hang 84%")},
                        {"loopback"}),
            participant("v3", pid("Tóth Bence"),
                        {evidence(EvidenceKind::Voice, Polarity::Support, "hang 58%"),
                         evidence(EvidenceKind::Voice, Polarity::Contradict, "más címkéken szokott lenni")},
                        {"loopback"}),
            participant("c1", pid("Lantos Réka"), {evidence(EvidenceKind::Calendar, Polarity::Neutral, "naptár")},
                        {}, ParticipantSource::Calendar),
            participant("v4", pid(""), {evidence(EvidenceKind::Voice, Polarity::Neutral, "ismeretlen hang")},
                        {"loopback"}),
        };
        m.participants[0].rawSpeakerIds = {"Beszélő 1@mic"};
        m.participants[1].rawSpeakerIds = {"Beszélő 1@loopback"};
        m.participants[2].rawSpeakerIds = {"Beszélő 2"};
        m.participants[4].rawSpeakerIds = {"Beszélő 3"};
        m.hasTranscript = transcript;
        m_app->store()->saveMeeting(m);
        m_meetingId = m.id;
    }

    // ---- QML ----
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QStringList m_warnings;

    void pump(int ms = 40) { QTest::qWait(ms); }
    static void collect(QQuickItem* root, const QString& name, QList<QQuickItem*>& out)
    {
        if (!root || !root->isVisible()) return;
        if (root->objectName() == name) out << root;
        for (QQuickItem* child : root->childItems()) collect(child, name, out);
    }
    QQuickItem* visual(const QString& name) const
    {
        QList<QQuickItem*> out;
        collect(m_window->contentItem(), name, out);
        collect(m_window->contentItem()->parentItem(), name, out);   // the Overlay too
        return out.value(0);
    }
    void click(QQuickItem* item)
    {
        QVERIFY(item);
        const QPoint p = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        QTest::mouseClick(m_window.get(), Qt::LeftButton, Qt::NoModifier, p);
        pump();
    }

private slots:
    void initTestCase() { qputenv("TANARA_CLOUD", "off"); }

    void init()
    {
        m_home = std::make_unique<QTemporaryDir>();
        QVERIFY(m_home->isValid());
        qputenv("TANARA_HOME", m_home->path().toUtf8());
        m_app = std::make_unique<app::AppController>();
        QVERIFY(m_app->store()->metadataDir().startsWith(m_home->path()));
        m_app->setUserSpeakerName(QStringLiteral("Kovács Lilla"));
    }
    void cleanup()
    {
        m_window.reset();
        QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QLatin1Char('\n'))));
        m_app.reset();
        m_home.reset();
    }
    void cleanupTestCase() { m_engine.reset(); }

    // Default checks: a named candidate with no contradiction (Lilla, Gábor); a contradicted one
    // (Bence), the anonymous voice and the invited, not heard one are not.
    void defaultsAndCounts()
    {
        seedMeeting(true);
        ParticipantsViewModel vm(services());
        vm.setMeetingId(m_meetingId);
        QVERIFY(!vm.demo());
        QVERIFY(vm.approvalPending());
        QVERIFY(vm.hasTranscript());
        QCOMPARE(vm.checkedCount(), 2);
        QCOMPARE(vm.candidateCount(), 5);
        QVERIFY(vm.metaText().isEmpty());                      // no analysis file in the sandbox
        QCOMPARE(vm.participantIdOf(QStringLiteral("kovács lilla")), QStringLiteral("v1"));
        QCOMPARE(vm.participantIdOf(QStringLiteral("Tóth Bence")), QStringLiteral("v3"));
        QVERIFY(vm.participantIdOf(QStringLiteral("Senki Sándor")).isEmpty());
        QVERIFY(vm.participantIdOf(QString()).isEmpty());
    }

    void approveTakesTheCheckedRows()
    {
        seedMeeting(false);
        ParticipantsViewModel vm(services());
        vm.setMeetingId(m_meetingId);
        QVERIFY(vm.approve());
        const Meeting m = m_app->store()->load(m_meetingId);
        QVERIFY(m.approval.has_value());
        QVERIFY(!m.approval->skipped);
        QStringList approved;
        for (const Participant& p : m.participants)
            if (p.approved) approved << nameOf(p.personId);
        approved.sort();
        QCOMPARE(approved, (QStringList{"Fehér Gábor", "Kovács Lilla"}));
        QVERIFY(!m_app->participantApprovalPending(m_meetingId));
        // Reopened: the decision made then is the default check.
        vm.reload();
        QVERIFY(!vm.approvalPending());
        QCOMPARE(vm.checkedCount(), 2);
        // An external change (participantsChanged) keeps the working copy.
        emit m_app->participantsChanged(m_meetingId);
        QCOMPARE(vm.checkedCount(), 2);
    }

    void skipAndUnbind()
    {
        seedMeeting(false);
        ParticipantsViewModel vm(services());
        vm.setMeetingId(m_meetingId);
        QVERIFY(vm.skip());
        Meeting m = m_app->store()->load(m_meetingId);
        QVERIFY(m.approval && m.approval->skipped);
        vm.reload();
        QVERIFY(!vm.approvalPending());
        // After Skip the default check stays (not "all off").
        QCOMPARE(vm.checkedCount(), 2);

        QVERIFY(vm.approve());
        QVERIFY(vm.unbind(QStringLiteral("v1")));
        m = m_app->store()->load(m_meetingId);
        for (const Participant& p : m.participants)
            if (p.id == QLatin1String("v1")) QVERIFY(!p.approved);
        QCOMPARE(vm.checkedCount(), 1);
    }

    // After Skip a name given by hand is marked (stored, and the Overview row's "manual"); the voice
    // recognition's names are not; a manually added participant counts as named by hand.
    void skipThenNameByHand()
    {
        seedMeeting(false);
        ParticipantsViewModel vm(services());
        vm.setMeetingId(m_meetingId);
        QVERIFY(vm.skip());
        editing::ParticipantService& ps = m_app->services()->participants();
        QVERIFY(ps.nameParticipant(m_meetingId, QStringLiteral("v4"), PersonChoice::create(QStringLiteral("Szabó Bence"))));
        const Meeting m = m_app->store()->load(m_meetingId);
        for (const Participant& p : m.participants) {
            if (p.id == QLatin1String("v4")) QVERIFY(p.namedByHand && p.byHand());
            if (p.id == QLatin1String("v2")) QVERIFY(!p.namedByHand && !p.byHand());
        }
        library::IOverviewFacade* ov = services().overview(QString(), this);
        QVERIFY(ov);
        QHash<QString, bool> manual;
        for (const QVariant& v : ov->snapshot(m_meetingId).participants)
            manual.insert(v.toMap().value("id").toString(), v.toMap().value("manual").toBool());
        QVERIFY(manual.value(QStringLiteral("v4")));
        QVERIFY(!manual.value(QStringLiteral("v2")));
        QVERIFY(!manual.value(QStringLiteral("v1")));
        // A manually added participant is named by hand too.
        Participant added;
        added.personId = pid(QStringLiteral("Lantos Réka"));
        added.source = ParticipantSource::Manual;
        QVERIFY(added.byHand());
        added.personId = PersonId();
        QVERIFY(!added.byHand());                              // anonymous: no name at all
    }

    // After "Approve" it asks once: Gábor is at a #Nordvik meeting for the second time, Szabó Bence
    // is a new person; "Yes" is one undoable step; no question on a repeated approval.
    void learnQuestionAfterApproval()
    {
        library::TagService* tags = m_app->tags();
        const QString nord = tags->create(QStringLiteral("Nordvik")).id;
        Meeting before = m_app->store()->createMeeting(QStringLiteral("Előző egyeztetés"));
        before.speakerMap.insert(QStringLiteral("Beszélő 2"), pid(QStringLiteral("Fehér Gábor")));
        before.tagIds = {nord};
        m_app->store()->saveMeeting(before);
        seedMeeting(false);
        tags->addTag(m_meetingId, nord);
        tags->refreshPersonStatsNow();   // computed on a background thread in the app; here, right now

        ParticipantsViewModel vm(services());
        vm.setMeetingId(m_meetingId);
        QVERIFY(vm.learnQuestion().isEmpty());
        QVERIFY(!m_app->addParticipant(m_meetingId, m_app->people()->choiceFor(QStringLiteral("Szabó Bence"))).isEmpty());
        vm.reload();
        QSignalSpy learn(&vm, &ParticipantsViewModel::learnChanged);
        QVERIFY(vm.approve());
        QVERIFY(!learn.isEmpty());
        QVariantMap q = vm.learnQuestion();
        QCOMPARE(q.value("text").toString(),
                 QStringLiteral("Fehér Gábor másodszor van #Nordvik megbeszélésen. Ráteszem a címkét?"));
        const QVariantList pairs = q.value("pairs").toList();
        QCOMPARE(pairs.size(), 2);                                    // + Szabó Bence (new person)
        QCOMPARE(pairs.at(1).toMap().value("name").toString(), QStringLiteral("Szabó Bence"));

        // One pair from "One by one...": Bence "No" (persistent rejection); the question stays for Gábor.
        QVERIFY(vm.answerLearnPair(1, false).contains(QStringLiteral("#Nordvik")));
        QVERIFY(tags->isRejectedForPerson(pid(QStringLiteral("Szabó Bence")), nord));
        QVERIFY(vm.learnQuestion().value("pairs").toList().isEmpty());
        // "Yes": the tag on Gábor, one step.
        QVERIFY(vm.answerLearn(true).contains(QStringLiteral("Fehér Gábor")));
        QCOMPARE(tags->tagsOfPerson(pid(QStringLiteral("Fehér Gábor"))), QStringList{nord});
        QVERIFY(vm.learnQuestion().isEmpty());
        tags->undo();
        QVERIFY(tags->tagsOfPerson(pid(QStringLiteral("Fehér Gábor"))).isEmpty());

        // Asks once: a repeated approval brings no question.
        vm.reload();
        QVERIFY(vm.approve());
        QVERIFY(vm.learnQuestion().isEmpty());
    }

    void learnBarDemoInteractions()
    {
        AppContext::instance()->setDemo(true);
        AppContext::instance()->setThemeMode(QStringLiteral("light"));
        m_engine = std::make_unique<QQmlEngine>();
        setupEngine(*m_engine);
        connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError>& list) {
            for (const QQmlError& e : list) m_warnings << e.toString();
        });
        QQmlComponent comp(m_engine.get());
        comp.setData(kLearnHost, QUrl(QStringLiteral("qrc:/qt/qml/Tanara/TagLearnProbe.qml")));
        QObject* obj = comp.create();
        QVERIFY2(obj, qPrintable(comp.errorString()));
        m_window.reset(qobject_cast<QQuickWindow*>(obj));
        QVERIFY(m_window);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
        QObject* shell = m_window->findChild<QObject*>(QStringLiteral("shell"));
        QVERIFY(shell);
        QTRY_VERIFY(visual("learnText"));
        QCOMPARE(visual("learnText")->property("text").toString(),
                 QStringLiteral("Fehér Gábor és Varga Árpád másodszor van #Nordvik megbeszélésen. "
                                "Rájuk teszem a címkét?"));
        // "One by one..." gives three pairs; one pair "Yes" gives a toast with "Undo" (tag stack).
        click(visual("learnEach"));
        QTRY_VERIFY(visual("learnPairs"));
        QVERIFY(visual("learnPair_2"));
        click(visual("learnPairYes_2"));
        QVERIFY(shell->property("toasts").toString().contains(QStringLiteral("#Partnerek · Fehér Gábor|tags")));
        QVERIFY(visual("learnText"));                                // the other pairs are still waiting
        click(visual("learnYes"));
        QTRY_VERIFY(!visual("learnText"));
        QVERIFY(shell->property("toasts").toString().contains(QStringLiteral("#Nordvik · Fehér Gábor és Varga Árpád|tags")));
        AppContext::instance()->setDemo(false);
    }
};

QTEST_MAIN(TestParticipantsDecision)
#include "test_participants_decision.moc"
