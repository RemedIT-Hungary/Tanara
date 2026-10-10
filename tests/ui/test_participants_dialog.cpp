// „Ki volt ott?" (handoff-v3 V2): a ParticipantsViewModel valódi AppController-rel, IZOLÁLT
// TANARA_HOME-ban (a jelöltek a meeting.json-ba írva, hangelemzés nélkül): csoportok,
// alapjelölés (az ellentmondó jelölt kikapcsolva), a munkapéldány (jelölés, hozzáadás, elnevezés)
// csak a „Tovább"-ra kerül a meetingre, „Kihagyás", „Ő nem volt ott"; a ShellActions
// openParticipants / maybeOfferParticipants jele; és a ParticipantsDialog.qml kijelző nélkül
// (offscreen) a beépített KITALÁLT mintaadaton: jelölés, címke-javaslat, Tovább / Kihagyás.
#include "AppContext.h"
#include "ParticipantsViewModel.h"
#include "QmlApp.h"
#include "ShellActions.h"

#include "tanara/AppController.h"
#include "tanara/store/MeetingStore.h"

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

Participant participant(const QString& id, const QString& name, QVector<Evidence> evs,
                        const QStringList& sides, ParticipantSource source = ParticipantSource::Voice)
{
    Participant p;
    p.id = id;
    p.personName = name;
    p.evidence = std::move(evs);
    p.sides = sides;
    p.source = source;
    return p;
}

QVariantMap rowOf(const ParticipantsViewModel& vm, const QString& id)
{
    for (const QVariant& g : vm.groups())
        for (const QVariant& r : g.toMap().value(QStringLiteral("rows")).toList())
            if (r.toMap().value(QStringLiteral("id")).toString() == id) return r.toMap();
    return {};
}

QString groupOf(const ParticipantsViewModel& vm, const QString& id)
{
    for (const QVariant& g : vm.groups())
        for (const QVariant& r : g.toMap().value(QStringLiteral("rows")).toList())
            if (r.toMap().value(QStringLiteral("id")).toString() == id)
                return g.toMap().value(QStringLiteral("key")).toString();
    return {};
}

const char* kHost = R"(
import QtQuick
import QtQuick.Controls
import Tanara
ApplicationWindow {
    width: 1100; height: 760
    color: Theme.bg
    QtObject {
        id: fakeShell
        objectName: "shell"
        signal participantsDialogRequested(string meetingId)
        property string toasts: ""
        function toast(text) { toasts += text + ";" }
    }
    ParticipantsDialog {
        objectName: "host"
        anchors.fill: parent
        shell: fakeShell
    }
}
)";

} // namespace

class TestParticipantsDialog : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<AppController> m_app;
    QString m_meetingId;

    // Lilla (mikrofon + hang): biztos; Gábor (csak hang): kétséges, bejelölve; Bence (hang +
    // ellentmondó címke): kétséges, kikapcsolva; Réka (naptár, nem hallottuk): meghívott;
    // névtelen hang: kétséges, nem jelölhető, amíg nincs neve.
    void seedMeeting(bool transcript)
    {
        Meeting m = m_app->store()->createMeeting(QStringLiteral("Negyedéves partnertalálkozó"));
        m.participants = {
            participant("v1", "Kovács Lilla",
                        {evidence(EvidenceKind::Side, Polarity::Support, "mikrofon"),
                         evidence(EvidenceKind::Voice, Polarity::Support, "hang 96%")}, {"mic"}),
            participant("v2", "Fehér Gábor", {evidence(EvidenceKind::Voice, Polarity::Support, "hang 84%")},
                        {"loopback"}),
            participant("v3", "Tóth Bence",
                        {evidence(EvidenceKind::Voice, Polarity::Support, "hang 58%"),
                         evidence(EvidenceKind::Voice, Polarity::Contradict, "más címkéken szokott lenni")},
                        {"loopback"}),
            participant("c1", "Lantos Réka", {evidence(EvidenceKind::Calendar, Polarity::Neutral, "naptár")},
                        {}, ParticipantSource::Calendar),
            participant("v4", "", {evidence(EvidenceKind::Voice, Polarity::Neutral, "ismeretlen hang")},
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
        collect(m_window->contentItem()->parentItem(), name, out);   // az Overlay is
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
        m_app = std::make_unique<AppController>();
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

    void groupsAndDefaults()
    {
        seedMeeting(true);
        ParticipantsViewModel vm;
        vm.setController(m_app.get());
        vm.setMeetingId(m_meetingId);
        QVERIFY(!vm.demo());
        QVERIFY(vm.approvalPending());
        QVERIFY(vm.hasTranscript());

        QCOMPARE(groupOf(vm, "v1"), QStringLiteral("sure"));
        QCOMPARE(groupOf(vm, "v2"), QStringLiteral("doubt"));
        QCOMPARE(groupOf(vm, "v3"), QStringLiteral("doubt"));
        QCOMPARE(groupOf(vm, "c1"), QStringLiteral("invited"));
        QVERIFY(rowOf(vm, "v1").value("checked").toBool());
        QVERIFY(rowOf(vm, "v2").value("checked").toBool());
        QVERIFY(!rowOf(vm, "v3").value("checked").toBool());          // ellentmondó → kikapcsolva
        QVERIFY(!rowOf(vm, "v4").value("checkable").toBool());        // névtelen
        QCOMPARE(rowOf(vm, "v1").value("displayName").toString(), QStringLiteral("Kovács Lilla (te)"));
        QCOMPARE(rowOf(vm, "v1").value("mapping").toString(), QStringLiteral("Beszélő 1 · mikrofon"));
        QCOMPARE(rowOf(vm, "v2").value("mapping").toString(), QStringLiteral("Beszélő 1 · hívás"));
        QCOMPARE(rowOf(vm, "c1").value("mapping").toString(), QString());
        // A side bizonyíték rövid formája.
        const QVariantList evs = rowOf(vm, "v1").value("evidence").toList();
        QCOMPARE(evs.first().toMap().value("text").toString(), QStringLiteral("mikrofon-sáv"));
        QCOMPARE(evs.first().toMap().value("side").toString(), QStringLiteral("mic"));
        QCOMPARE(vm.checkedCount(), 2);
        // Nincs átirat-fájl a homokozóban → nincs nyers beszélő-lista, nincs info.
        QCOMPARE(vm.infoText(), QString());
    }

    void noTranscriptMapping()
    {
        seedMeeting(false);
        ParticipantsViewModel vm;
        vm.setController(m_app.get());
        vm.setMeetingId(m_meetingId);
        QCOMPARE(rowOf(vm, "v1").value("mapping").toString(), QStringLiteral("átirat után"));
        QVERIFY(rowOf(vm, "v1").value("mappingPending").toBool());
        QCOMPARE(rowOf(vm, "c1").value("mapping").toString(), QString());   // nem hallottuk
        QVERIFY(vm.infoText().contains(QStringLiteral("átirat")));
    }

    void workingCopyThenApprove()
    {
        seedMeeting(false);
        ParticipantsViewModel vm;
        vm.setController(m_app.get());
        vm.setMeetingId(m_meetingId);

        vm.setChecked("v2", false);
        vm.setChecked("v3", true);
        vm.setChecked("v4", true);                       // névtelen: nem jelölhető
        QVERIFY(!rowOf(vm, "v4").value("checked").toBool());
        vm.nameRow("v4", "Varga Árpád");
        QVERIFY(rowOf(vm, "v4").value("checked").toBool());
        const QString added = vm.addPerson("Szabó Bence");
        QCOMPARE(groupOf(vm, added), QStringLiteral("manual"));
        QCOMPARE(vm.addPerson("szabó bence"), added);    // ugyanaz a név → ugyanaz a sor
        QCOMPARE(vm.checkedCount(), 4);                  // Lilla, Bence (T), Árpád, Szabó Bence

        // Semmi sem került még a meetingre.
        for (const Participant& p : m_app->participants(m_meetingId)) {
            QVERIFY(!p.approved);
            QVERIFY(p.personName != QLatin1String("Szabó Bence"));
        }

        // Külső változás (participantsChanged): a munkapéldány megmarad.
        emit m_app->participantsChanged(m_meetingId);
        QVERIFY(!rowOf(vm, "v2").value("checked").toBool());
        QVERIFY(!rowOf(vm, added).isEmpty());

        QVERIFY(vm.approve());
        const Meeting m = m_app->store()->load(m_meetingId);
        QVERIFY(m.approval.has_value());
        QVERIFY(!m.approval->skipped);
        QStringList approved;
        for (const Participant& p : m.participants)
            if (p.approved) approved << p.personName;
        approved.sort();
        QCOMPARE(approved, (QStringList{"Kovács Lilla", "Szabó Bence", "Tóth Bence", "Varga Árpád"}));
        QVERIFY(!m_app->participantApprovalPending(m_meetingId));

        // Újranyitás („Módosítás"): az akkori döntés az alapjelölés.
        vm.reload();
        QVERIFY(!rowOf(vm, "v2").value("checked").toBool());
        QVERIFY(rowOf(vm, "v3").value("checked").toBool());
        QCOMPARE(vm.checkedCount(), 4);
    }

    void skipAndUnbind()
    {
        seedMeeting(false);
        ParticipantsViewModel vm;
        vm.setController(m_app.get());
        vm.setMeetingId(m_meetingId);
        QVERIFY(vm.skip());
        Meeting m = m_app->store()->load(m_meetingId);
        QVERIFY(m.approval && m.approval->skipped);
        vm.reload();
        QVERIFY(!vm.approvalPending());
        // Kihagyás után az alapjelölés marad (nem a „mind ki").
        QVERIFY(rowOf(vm, "v1").value("checked").toBool());

        QVERIFY(vm.approve());
        QVERIFY(vm.unbind("v1"));
        m = m_app->store()->load(m_meetingId);
        for (const Participant& p : m.participants)
            if (p.id == QLatin1String("v1")) QVERIFY(!p.approved);
        QVERIFY(!rowOf(vm, "v1").value("checked").toBool());
    }

    void tagSuggestionExcludesPresent()
    {
        seedMeeting(false);
        ParticipantsViewModel vm;
        vm.setController(m_app.get());
        vm.setMeetingId(m_meetingId);
        // Címke nélkül nincs javaslat (a TagService-számítást a core tesztjei fedik).
        QVERIFY(vm.tagSuggestions().isEmpty());
        QCOMPARE(vm.tagSuggestionLabel(), QString());
    }

    void shellOffersOnce()
    {
        seedMeeting(true);
        ShellActions shell;
        shell.setController(m_app.get());
        QSignalSpy spy(&shell, &ShellActions::participantsDialogRequested);
        QVERIFY(shell.maybeOfferParticipants(m_meetingId));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), m_meetingId);
        QVERIFY(!shell.maybeOfferParticipants(m_meetingId));   // munkamenetenként egyszer
        shell.openParticipants(m_meetingId);                   // a kézi megnyitás mindig megy
        QCOMPARE(spy.count(), 2);

        // Döntés után nem ajánljuk fel.
        seedMeeting(true);
        QVERIFY(m_app->skipApproval(m_meetingId));
        QVERIFY(!shell.maybeOfferParticipants(m_meetingId));
        QVERIFY(!shell.maybeOfferParticipants(QString()));
        QCOMPARE(spy.count(), 2);
    }

    void dialogDemoInteractions()
    {
        AppContext::instance()->setDemo(true);
        AppContext::instance()->setThemeMode(QStringLiteral("light"));
        m_engine = std::make_unique<QQmlEngine>();
        setupEngine(*m_engine);
        connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError>& list) {
            for (const QQmlError& e : list) m_warnings << e.toString();
        });
        QQmlComponent comp(m_engine.get());
        comp.setData(kHost, QUrl(QStringLiteral("qrc:/qt/qml/Tanara/ParticipantsDialogProbe.qml")));
        QObject* obj = comp.create();
        QVERIFY2(obj, qPrintable(comp.errorString()));
        m_window.reset(qobject_cast<QQuickWindow*>(obj));
        QVERIFY(m_window);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));

        QObject* host = m_window->findChild<QObject*>(QStringLiteral("host"));
        QObject* shell = m_window->findChild<QObject*>(QStringLiteral("shell"));
        QVERIFY(host && shell);
        auto* vm = host->property("viewModel").value<ParticipantsViewModel*>();
        QVERIFY(vm);
        QVERIFY(!host->property("opened").toBool());

        // A shell jele nyitja (az U1 / a menü ezen át).
        QMetaObject::invokeMethod(shell, "participantsDialogRequested", Q_ARG(QString, QStringLiteral("demo")));
        QTRY_VERIFY(host->property("opened").toBool());
        pump(150);
        QVERIFY(vm->demo());
        QCOMPARE(vm->checkedCount(), 4);
        QQuickItem* approve = visual("participantsApprove");
        QVERIFY(approve);
        QCOMPARE(approve->property("text").toString(), QStringLiteral("Tovább · 4 résztvevő"));
        QVERIFY(visual("participantGroup_sure") && visual("participantGroup_doubt"));
        QVERIFY(!visual("participantGroup_invited"));     // üres csoport rejtve

        // Jelölés kattintással: Tóth Bence (ellentmondó, alapból ki) be.
        click(visual("participantCheck_v-bence"));
        QCOMPARE(vm->checkedCount(), 5);
        QTRY_COMPARE(approve->property("text").toString(), QStringLiteral("Tovább · 5 résztvevő"));

        // Címke-alapú javaslat → „Kézzel felvéve" csoport.
        click(visual("participantsTagSuggestion_0"));
        QTRY_VERIFY(visual("participantGroup_manual"));
        QCOMPARE(vm->checkedCount(), 6);
        QVERIFY(vm->tagSuggestions().isEmpty());

        // A hozzáadás-mező a személyválasztót nyitja.
        click(visual("participantsAdd"));
        QObject* picker = m_window->findChild<QObject*>(QStringLiteral("participantsAddPicker"));
        QVERIFY(picker);
        QTRY_VERIFY(picker->property("opened").toBool());
        QTest::keyClick(m_window.get(), Qt::Key_Escape);
        QTRY_VERIFY(!picker->property("opened").toBool());

        // Tovább → zárás + toast.
        click(visual("participantsApprove"));
        QTRY_VERIFY(!host->property("opened").toBool());
        QVERIFY(shell->property("toasts").toString().contains(QStringLiteral("6")));

        // Újranyitás → friss munkapéldány; Kihagyás zár.
        QMetaObject::invokeMethod(host, "openFor", Q_ARG(QVariant, QStringLiteral("demo")));
        QTRY_VERIFY(host->property("opened").toBool());
        QCOMPARE(vm->checkedCount(), 4);
        pump(150);
        click(visual("participantsSkip"));
        QTRY_VERIFY(!host->property("opened").toBool());
        AppContext::instance()->setDemo(false);
    }
};

QTEST_MAIN(TestParticipantsDialog)
#include "test_participants_dialog.moc"
