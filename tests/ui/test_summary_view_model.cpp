// SummaryViewModel + TopicListModel — az Összefoglaló fül (M06 / M07 / M08) állapotai izolált
// TANARA_HOME-on, ál-LLM-szerverrel (valódi szolgáltató-hívás nincs).
#include "JobTestSupport.h"
#include "SummaryViewModel.h"
#include "TopicListModel.h"

#include "tanara/edit/SpeakerEditor.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QtTest>

using namespace tanara;
using tanara_qml::SummaryViewModel;
using tanara_qml::TopicListModel;

namespace {

QString topicState(TopicListModel* model, int row)
{
    return model->data(model->index(row), TopicListModel::StateRole).toString();
}
QString topicTitle(TopicListModel* model, int row)
{
    return model->data(model->index(row), TopicListModel::TitleRole).toString();
}

const char* kQuickJson =
    "{\"execSummary\":\"Rövid egyeztetés az ajánlatról.\","
    "\"decisions\":[\"Az ajánlat holnap megy ki.\"],"
    "\"actionItems\":[{\"text\":\"Ajánlat elküldése\",\"owner\":\"Beszélő 2\",\"due\":\"holnap\"}],"
    "\"participants\":[\"Beszélő 1\",\"Vendég\"]}";

} // namespace

class TestSummaryViewModel : public QObject {
    Q_OBJECT
private slots:
    void splitTimestamp()
    {
        QString rest;
        QCOMPARE(SummaryViewModel::splitTimestamp(QStringLiteral("[12:52] A súgót kiterjesztik."), &rest), 772000);
        QCOMPARE(rest, QStringLiteral("A súgót kiterjesztik."));
        QCOMPARE(SummaryViewModel::splitTimestamp(QStringLiteral("(1:02:10) – Döntés"), &rest), 3730000);
        QCOMPARE(rest, QStringLiteral("Döntés"));
        // Nincs időbélyeg → nincs hivatkozás, a szöveg érintetlen (a szám a mondat része).
        QCOMPARE(SummaryViewModel::splitTimestamp(QStringLiteral("A határidő 12:30-kor jár le."), &rest), -1);
        QCOMPARE(rest, QStringLiteral("A határidő 12:30-kor jár le."));
        QCOMPARE(SummaryViewModel::splitTimestamp(QStringLiteral("3 fővel bővül a csapat."), &rest), -1);
    }

    void demoStatesWithoutController()
    {
        SummaryViewModel vm;
        QVERIFY(vm.demo());
        QCOMPARE(vm.view(), QStringLiteral("summary"));          // alap: M07, elavult
        QVERIFY(vm.stale());
        QCOMPARE(vm.staleCount(), 3);
        QCOMPARE(vm.decisions().size(), 3);
        QCOMPARE(vm.decisions().at(0).toMap().value("ms").toLongLong(), 772000);
        QCOMPARE(vm.actions().size(), 4);
        QCOMPARE(vm.actions().at(1).toMap().value("ownerIndex").toInt(), 2);
        QCOMPARE(vm.participants().size(), 6);

        vm.setDemoState(QStringLiteral("empty"));
        QCOMPARE(vm.view(), QStringLiteral("empty"));
        QVERIFY(vm.canRun());
        vm.setDemoState(QStringLiteral("emptyBlocked"));
        QVERIFY(!vm.canRun());
        QVERIFY(!vm.blocker().value("title").toString().isEmpty());
        vm.setDemoState(QStringLiteral("emptyRunning"));
        QVERIFY(vm.jobRunning());

        vm.setDemoState(QStringLiteral("topics"));
        QCOMPARE(vm.view(), QStringLiteral("topics"));
        QCOMPARE(vm.topics()->count(), 5);
        QCOMPARE(topicState(vm.topics(), 3), QStringLiteral("failed"));
        // Demóban a szerkesztés memóriában működik.
        QVERIFY(vm.topics()->addTopic(QStringLiteral("Új téma"), QString()));
        QCOMPARE(vm.topics()->count(), 6);
        vm.topics()->moveTopic(5, 0);
        QCOMPARE(topicTitle(vm.topics(), 0), QStringLiteral("Új téma"));
        vm.topics()->removeTopic(0);
        QCOMPARE(vm.topics()->count(), 5);
    }

    void emptyGatingThenQuickSummary()
    {
        jobtest::Sandbox sb;
        const Meeting m = sb.transcribed("Ajánlat");

        SummaryViewModel vm;
        vm.setController(sb.app.get());
        QCOMPARE(vm.view(), QStringLiteral("none"));
        vm.setMeetingId(m.id);
        QVERIFY(!vm.demo());
        QCOMPARE(vm.view(), QStringLiteral("empty"));
        QVERIFY(vm.canRun());
        QVERIFY2(vm.transcriptLine().contains("2 beszélő"), qPrintable(vm.transcriptLine()));

        // Hiányzó szerver-cím → a sáv megnevezi, a gomb a szolgáltatókhoz visz.
        AppSettings s = sb.app->settings()->settings();
        const QString model = s.llmConfigs[s.llmProviderId].baseUrl;
        s.llmConfigs[s.llmProviderId].baseUrl.clear();
        sb.app->settings()->setSettings(s);
        QVERIFY(!vm.canRun());
        QCOMPARE(vm.blocker().value("actionPage").toString(), QStringLiteral("providers"));
        QVERIFY(!vm.blocker().value("text").toString().isEmpty());
        s.llmConfigs[s.llmProviderId].baseUrl = model;
        sb.app->settings()->setSettings(s);
        QVERIFY(vm.canRun());

        // Futó állapot: a kérés függőben → megszakítható; megszakítás után nincs hiba.
        sb.http->handler = [](const jobtest::FakeRequest&) { return jobtest::FakeReply{200, "{}", true}; };
        sb.app->summarizeMeeting(m.id);
        QVERIFY(vm.jobRunning());
        QCOMPARE(vm.jobKind(), int(JobKind::Summarize));
        QVERIFY(!vm.jobTitle().isEmpty());
        QVERIFY(sb.app->cancelJob(m.id, JobKind::Summarize));
        QTRY_VERIFY_WITH_TIMEOUT(!vm.jobRunning(), 10000);
        QVERIFY(vm.errorMessage().isEmpty());
        QCOMPARE(vm.view(), QStringLiteral("empty"));

        // Hiba: megmarad az üzenet + a technikai sor; elvethető.
        sb.http->handler = [](const jobtest::FakeRequest&) -> jobtest::FakeReply {
            return {500, "{\"error\":{\"message\":\"overloaded\",\"code\":\"overloaded\"}}"};
        };
        sb.app->summarizeMeeting(m.id);
        QTRY_VERIFY_WITH_TIMEOUT(!vm.errorMessage().isEmpty(), 15000);
        QVERIFY(!vm.jobRunning());
        QVERIFY2(vm.errorDetail().contains("HTTP 500"), qPrintable(vm.errorDetail()));
        vm.clearError();
        QVERIFY(vm.errorMessage().isEmpty());

        // Siker: a nézet az összefoglalóra vált, strukturált tartalommal.
        sb.http->handler = [](const jobtest::FakeRequest& r) -> jobtest::FakeReply {
            if (r.path.endsWith("/chat/completions")) return {200, jobtest::chat(QString::fromUtf8(kQuickJson))};
            return {404, "{}"};
        };
        QSignalSpy arrived(&vm, &SummaryViewModel::summaryArrived);
        sb.app->summarizeMeeting(m.id);
        QVERIFY(arrived.wait(15000));
        QCOMPARE(vm.view(), QStringLiteral("summary"));
        QCOMPARE(vm.mode(), QStringLiteral("quick"));
        QCOMPARE(vm.execSummary(), QStringLiteral("Rövid egyeztetés az ajánlatról."));
        QCOMPARE(vm.decisions().size(), 1);
        QCOMPARE(vm.decisions().at(0).toMap().value("ms").toLongLong(), -1);   // nincs időbélyeg az adatban
        QCOMPARE(vm.actions().size(), 1);
        // A felelős a meeting beszélője → a beszélő színében (colorIndex 1: a második megszólaló).
        QCOMPARE(vm.actions().at(0).toMap().value("ownerIndex").toInt(), 1);
        QVERIFY(vm.metaLine().contains("saját kulcs"));
        QCOMPARE(vm.modelLine(), QStringLiteral("teszt-modell"));
        // Résztvevők: a két beszélő beszédidő-aránnyal + az összefoglalóban említett vendég arány nélkül.
        QCOMPARE(vm.participants().size(), 3);
        QVERIFY(vm.participants().at(0).toMap().value("percent").toInt() > 0);
        QCOMPARE(vm.participants().at(2).toMap().value("name").toString(), QStringLiteral("Vendég"));
        QCOMPARE(vm.participants().at(2).toMap().value("percent").toInt(), -1);
        QVERIFY(!vm.stale());

        // Beszélő-javítás az összefoglaló után → elavult, valós számmal; „Rendben így” elengedi.
        SpeakerEditor* editor = sb.app->speakerEditor(m.id);
        QVERIFY(editor);
        QVERIFY(editor->reassignSpeaker(QStringLiteral("Beszélő 2"), QStringLiteral("Minta Márta")));
        QTRY_VERIFY_WITH_TIMEOUT(vm.stale(), 5000);
        QCOMPARE(vm.staleCount(), 1);
        // A résztvevő-lista követi a szerkesztést.
        bool renamed = false;
        for (const QVariant& p : vm.participants())
            renamed |= p.toMap().value("name").toString() == QStringLiteral("Minta Márta");
        QVERIFY(renamed);
        vm.dismissStale();
        QTRY_VERIFY_WITH_TIMEOUT(!vm.stale(), 5000);
    }

    void markdownOnlySummaryRenders()
    {
        jobtest::Sandbox sb;
        Meeting m = sb.transcribed("Régi összefoglaló");
        QFile f(QDir(m.folder).filePath("summary.md"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QStringLiteral(
            "## Vezetői összefoglaló\n\nA csapat átnézte a **kitalált** ütemtervet.\n\n"
            "## Döntések\n\n- A bemutató két héttel csúszik.\n- Új tesztkör indul.\n\n"
            "## Teendők\n\n- [ ] Ütemterv frissítése — Beszélő 1 (péntek)\n- [ ] Tesztkör szervezése\n\n"
            "## Résztvevők\n\n- Beszélő 1\n- Beszélő 2\n").toUtf8());
        f.close();
        m.hasSummary = true;
        sb.app->store()->saveMeeting(m);

        SummaryViewModel vm;
        vm.setController(sb.app.get());
        vm.setMeetingId(m.id);
        QCOMPARE(vm.view(), QStringLiteral("summary"));
        QVERIFY(vm.execSummary().contains("ütemtervet"));
        QCOMPARE(vm.decisions().size(), 2);
        QCOMPARE(vm.actions().size(), 2);
        QCOMPARE(vm.actions().at(0).toMap().value("owner").toString(), QStringLiteral("Beszélő 1"));
        QCOMPARE(vm.actions().at(0).toMap().value("ownerIndex").toInt(), 0);
        QCOMPARE(vm.actions().at(0).toMap().value("due").toString(), QStringLiteral("péntek"));
        QCOMPARE(vm.actions().at(1).toMap().value("owner").toString(), QString());
        QCOMPARE(vm.participants().size(), 2);                 // a két beszélő, nincs külön „említett”
        QVERIFY(!vm.metaLine().isEmpty());                     // a fájl ideje
        QVERIFY(vm.modelLine().isEmpty());                     // régi összefoglaló: nincs modell-adat
        QVERIFY(vm.copyToClipboard());
    }

    void topicsEditRunFailRetryAndResume()
    {
        jobtest::Sandbox sb;
        const Meeting m = sb.transcribed("Témák");

        auto vm = std::make_unique<SummaryViewModel>();
        vm->setController(sb.app.get());
        vm->setMeetingId(m.id);
        TopicListModel* topics = vm->topics();
        QAbstractItemModelTester tester(topics, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(vm->view(), QStringLiteral("empty"));
        QVERIFY(!vm->hasTopics());

        // Téma-javaslat a modelltől → megnyílik a szerkesztő.
        sb.http->handler = [](const jobtest::FakeRequest& r) -> jobtest::FakeReply {
            if (!r.path.endsWith("/chat/completions")) return {404, "{}"};
            return {200, jobtest::chat(QStringLiteral(
                "{\"topics\":[{\"title\":\"Árazás\",\"summary\":\"Csomagok\"},{\"title\":\"Pilot\",\"summary\":\"Helyszínek\"}]}"))};
        };
        QSignalSpy topicsReady(sb.app.get(), &AppController::topicsReady);
        sb.app->extractMeetingTopics(m.id);
        QVERIFY(vm->jobRunning());
        QCOMPARE(vm->jobKind(), int(JobKind::ExtractTopics));
        QVERIFY(topicsReady.wait(15000));
        QCOMPARE(vm->view(), QStringLiteral("topics"));
        QCOMPARE(topics->count(), 2);
        QCOMPARE(topics->missingCount(), 2);

        // Hozzáadás / szerkesztés / átrendezés / törlés — mind azonnal a lemezre kerül.
        QVERIFY(!topics->addTopic(QStringLiteral("   "), QString()));
        QVERIFY(topics->addTopic(QStringLiteral("Határidők"), QStringLiteral("Mikorra")));
        QCOMPARE(topics->count(), 3);
        QVERIFY(topics->updateTopic(0, QStringLiteral("Árazás és csomagok"), QStringLiteral("Csomagok")));
        topics->moveTopic(2, 0);
        QCOMPARE(topicTitle(topics, 0), QStringLiteral("Határidők"));
        QVector<SummaryTopic> onDisk = sb.app->meetingTopics(m.id);
        QCOMPARE(onDisk.size(), 3);
        QCOMPARE(onDisk[0].title, QStringLiteral("Határidők"));
        QCOMPARE(onDisk[1].title, QStringLiteral("Árazás és csomagok"));
        topics->moveTopic(0, 1);
        QCOMPARE(sb.app->meetingTopics(m.id)[1].title, QStringLiteral("Határidők"));
        topics->removeTopic(2);                                // „Pilot” törölve
        QCOMPARE(topics->count(), 2);
        QCOMPARE(sb.app->meetingTopics(m.id).size(), 2);
        QVERIFY(topics->countsText().contains("2 téma"));

        // Elemzés: az első sikerül, a második elbukik.
        int call = 0;
        sb.http->handler = [&call](const jobtest::FakeRequest& r) -> jobtest::FakeReply {
            if (!r.path.endsWith("/chat/completions")) return {404, "{}"};
            ++call;
            if (call == 2) return {500, "{\"error\":{\"message\":\"overloaded\",\"code\":\"overloaded\"}}"};
            return {200, jobtest::chat(QStringLiteral(
                "Részletes elemzés %1.\n\n## Döntések\n- Döntés %1\n\n## Teendők\n- Teendő %1 — Beszélő 1 (péntek)\n").arg(call))};
        };
        QSignalSpy queueDone(sb.app.get(), &AppController::topicAnalysisQueueFinished);
        sb.app->generateComplexSummary(m.id, sb.app->meetingTopics(m.id));
        QCOMPARE(topicState(topics, 0), QStringLiteral("running"));
        QCOMPARE(topicState(topics, 1), QStringLiteral("queued"));
        QVERIFY(vm->analyzing());
        QVERIFY(topics->busy());
        QVERIFY(queueDone.wait(15000));
        QCOMPARE(topicState(topics, 0), QStringLiteral("done"));
        QVERIFY(topics->data(topics->index(0), TopicListModel::ResultTextRole).toString().contains("Részletes elemzés 1"));
        QCOMPARE(topics->data(topics->index(0), TopicListModel::ResultDecisionsRole).toStringList().size(), 1);
        QCOMPARE(topics->data(topics->index(0), TopicListModel::ResultActionsRole).toList().size(), 1);
        QCOMPARE(topicState(topics, 1), QStringLiteral("failed"));
        QVERIFY(!topics->data(topics->index(1), TopicListModel::ErrorRole).toString().isEmpty());
        QVERIFY(topics->data(topics->index(1), TopicListModel::ErrorDetailRole).toString().contains("HTTP 500"));
        QCOMPARE(topics->missingCount(), 1);
        QVERIFY(!vm->analyzing());
        QVERIFY(!vm->hasSummary());

        // „Újraindítás”: új nézetmodell ugyanarra a meetingre → a lemezről folytatódik
        // (a munkaterület nyitva, a kész és a hibás téma állapota megvan).
        vm.reset();
        vm = std::make_unique<SummaryViewModel>();
        vm->setController(sb.app.get());
        vm->setMeetingId(m.id);
        topics = vm->topics();
        QCOMPARE(vm->view(), QStringLiteral("topics"));
        QCOMPARE(topics->count(), 2);
        QCOMPARE(topicState(topics, 0), QStringLiteral("done"));
        QCOMPARE(topicState(topics, 1), QStringLiteral("failed"));

        // A hibás téma önállóan újrafuttatható; futás közben megszakítható.
        sb.http->handler = [](const jobtest::FakeRequest&) { return jobtest::FakeReply{200, "{}", true}; };
        const QVector<SummaryTopic> list = sb.app->meetingTopics(m.id);
        sb.app->analyzeTopic(m.id, list[1]);
        QCOMPARE(topicState(topics, 1), QStringLiteral("running"));
        topics->cancelTopic(1);
        QTRY_VERIFY_WITH_TIMEOUT(topicState(topics, 1) != QStringLiteral("running"), 10000);
        QCOMPARE(topicState(topics, 0), QStringLiteral("done"));       // a kész eredmény megmaradt

        // Hiányzók elemzése működő szolgáltatóval → összegzés → a nézet az összefoglalóra vált.
        sb.http->handler = [](const jobtest::FakeRequest& r) -> jobtest::FakeReply {
            if (!r.path.endsWith("/chat/completions")) return {404, "{}"};
            return {200, jobtest::chat(QStringLiteral(
                "Összegző bekezdés a témákról.\n\n## Döntések\n- Közös döntés\n\n## Teendők\n- Közös teendő — Beszélő 2 (hétfő)\n"))};
        };
        QSignalSpy arrived(vm.get(), &SummaryViewModel::summaryArrived);
        sb.app->generateComplexSummary(m.id, sb.app->meetingTopics(m.id));
        QVERIFY(arrived.wait(20000));
        QCOMPARE(vm->view(), QStringLiteral("summary"));
        QCOMPARE(vm->mode(), QStringLiteral("topics"));
        QCOMPARE(vm->topicSections().size(), 2);
        QVERIFY(!vm->execSummary().isEmpty());
        // A témák megmaradtak: a munkaterület újra megnyitható.
        vm->setTopicsOpen(true);
        QCOMPARE(vm->view(), QStringLiteral("topics"));
        QCOMPARE(vm->topics()->doneCount(), 2);
    }
};

QTEST_MAIN(TestSummaryViewModel)
#include "test_summary_view_model.moc"
