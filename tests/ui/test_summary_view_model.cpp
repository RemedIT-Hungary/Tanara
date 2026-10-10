// SummaryViewModel + TopicListModel — az Összefoglaló fül (M06 / M07 / M08) állapotai izolált
// TANARA_HOME-on, ál-LLM-szerverrel (valódi szolgáltató-hívás nincs).
#include "JobTestSupport.h"
#include "ShellMeetingModel.h"
#include "SummaryProgress.h"
#include "SummaryViewModel.h"
#include "TopicListModel.h"

#include "tanara/edit/SpeakerEditor.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QtTest>

using namespace tanara;
using tanara_qml::ShellMeetingModel;
using tanara_qml::SummaryProgress;
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

// Forrás-jelölős gyors összefoglaló (v3): a mondatok, a döntés és a teendő a két megszólalásra
// mutat (u0: „Sziasztok, az ajánlatot” — Beszélő 1; u900: „holnap küldjük.” — Beszélő 2); a
// második mondatnak nincs jelölője.
const char* kSourcedJson =
    "{\"execSummary\":\"Rövid egyeztetés az ajánlatról. [t=00:00] Holnap küldik. [t=00:01] Jelölő nélküli mondat.\","
    "\"decisions\":[\"Az ajánlat holnap megy ki. [t=00:01]\"],"
    "\"actionItems\":[{\"text\":\"Ajánlat elküldése [t=00:01]\",\"owner\":\"Beszélő 2\",\"due\":\"holnap\"}],"
    "\"participants\":[\"Beszélő 1\",\"Beszélő 2\"]}";

QString rowId(tanara_qml::StatementListModel* model, int row)
{
    return model->data(model->index(row), tanara_qml::StatementListModel::StatementIdRole).toString();
}

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

    // A feladat-szakaszok leképezése (Összefoglaló fül + feladat-sáv).
    void summaryProgressMapping()
    {
        auto job = [](const QVector<JobStage>& stages, int done, int total) {
            JobProgress j;
            j.meetingId = QStringLiteral("m");
            j.kind = JobKind::Summarize;
            j.title = QStringLiteral("Összefoglaló készítése");
            j.stages = stages;
            j.done = done;
            j.total = total;
            return j;
        };
        // Rövid megbeszélés: egy szakasz, határozatlan.
        SummaryProgress p = SummaryProgress::from(job({{"single", "Összefoglalás", StageState::Running, -1, {}}}, -1, -1));
        QCOMPARE(p.stage, QStringLiteral("single"));
        QCOMPARE(p.percent, -1);
        QCOMPARE(p.stages.size(), 1);
        QVERIFY(!p.label.isEmpty());

        // Részenkénti jegyzet: valós csík a kész részekből, és a korábbi futásból átvett részek.
        const JobProgress notes = job({{"notes", "Jegyzetelés részenként", StageState::Running, -1,
                                        "3/5. rész · 2 korábbi futásból"},
                                       {"merge", "Összegzés", StageState::Waiting, -1, {}}}, 2, 5);
        p = SummaryProgress::from(notes);
        QCOMPARE(p.stage, QStringLiteral("notes"));
        QCOMPARE(p.percent, 40);
        QCOMPARE(p.done, 2);
        QCOMPARE(p.total, 5);
        QVERIFY2(p.label.contains(QStringLiteral("2 / 5")), qPrintable(p.label));
        QCOMPARE(p.reused, 2);
        QVERIFY(!p.reusedNote.isEmpty());
        QCOMPARE(p.stages.size(), 2);
        QCOMPARE(p.stages.at(0).toMap().value("state").toString(), QStringLiteral("running"));
        QCOMPARE(p.stages.at(0).toMap().value("percent").toInt(), 40);
        QCOMPARE(p.stages.at(1).toMap().value("state").toString(), QStringLiteral("waiting"));

        // Összefésülés: határozatlan; a jegyzetelés kész.
        p = SummaryProgress::from(job({{"notes", "Jegyzetelés részenként", StageState::Done, -1, "5 rész kész"},
                                       {"merge", "Összegzés", StageState::Running, -1, {}}}, 5, 5));
        QCOMPARE(p.stage, QStringLiteral("merge"));
        QCOMPARE(p.percent, -1);
        QCOMPARE(p.reused, 0);
        QCOMPARE(p.stages.at(0).toMap().value("state").toString(), QStringLiteral("done"));
        QCOMPARE(p.stages.at(1).toMap().value("state").toString(), QStringLiteral("running"));

        // Szakaszok nélküli összegzés (témánkénti elemzés vége) és más feladat: nincs leképezés.
        QVERIFY(!SummaryProgress::from(job({}, -1, -1)).isValid());
        JobProgress other = notes;
        other.kind = JobKind::AnalyzeTopics;
        QVERIFY(!SummaryProgress::from(other).isValid());
        QCOMPARE(SummaryProgress::reusedParts(QStringLiteral("3/5. rész")), 0);
        QCOMPARE(SummaryProgress::reusedParts(QStringLiteral("Part 3/5 · 4 from an earlier run")), 4);

        // A feladat-sáv ugyanígy: k / n rész valós százalékkal, az összefésülés határozatlan.
        QVariantMap strip = ShellMeetingModel::describeJob(notes);
        QCOMPARE(strip.value("percent").toInt(), 40);
        QVERIFY2(strip.value("detail").toString().contains(QStringLiteral("2 / 5")), qPrintable(strip.value("detail").toString()));
        strip = ShellMeetingModel::describeJob(job({{"notes", "x", StageState::Done, -1, {}},
                                                    {"merge", "y", StageState::Running, -1, {}}}, 5, 5));
        QCOMPARE(strip.value("percent").toInt(), -1);
    }

    // A demó-állapotok: vezetői összefoglaló nyitott kérdésekkel, memó (sok / kevés szakasz),
    // régi összefoglaló, futás részekkel, hiba megtartott részekkel, témák nyitott kérdései.
    void demoMemoProgressAndOldSummaries()
    {
        SummaryViewModel vm;
        QCOMPARE(vm.section(), QStringLiteral("exec"));
        QCOMPARE(vm.memoState(), QStringLiteral("ready"));
        QCOMPARE(vm.memo().size(), 24);
        QCOMPARE(vm.openQuestions().size(), 3);
        QCOMPARE(vm.openQuestions().at(0).toMap().value("ms").toLongLong(), (43 * 60 + 30) * 1000);
        QCOMPARE(vm.openQuestions().at(2).toMap().value("ms").toLongLong(), -1);
        const QVariantMap first = vm.memo().at(0).toMap();
        QCOMPARE(first.value("range").toString(), QStringLiteral("00:00–02:40"));
        QCOMPARE(first.value("startMs").toLongLong(), 0);
        QCOMPARE(first.value("points").toStringList().size(), 2);
        QCOMPARE(vm.memo().at(23).toMap().value("stamp").toString(), QStringLiteral("1:15:30"));

        // Másolás: vezetői rész (memó nélkül), csak a memó, vagy mindkettő.
        const QString exec = vm.markdownFor(QStringLiteral("exec"));
        QVERIFY(exec.contains(QStringLiteral("## Vezetői összefoglaló")));
        QVERIFY(exec.contains(QStringLiteral("## Nyitott kérdések")));
        QVERIFY(!exec.contains(QStringLiteral("## Memó")));
        const QString memo = vm.markdownFor(QStringLiteral("memo"));
        QVERIFY(memo.startsWith(QStringLiteral("## Memó")));
        QVERIFY(memo.contains(QStringLiteral("### Nyitás, napirend (00:00–02:40)")));
        QVERIFY(!memo.contains(QStringLiteral("## Döntések")));
        const QString all = vm.markdownFor(QStringLiteral("all"));
        QVERIFY(all.contains(QStringLiteral("## Nyitott kérdések")) && all.contains(QStringLiteral("## Memó")));
        QCOMPARE(vm.markdownFor(QString()), all);
        QVERIFY(vm.copyToClipboard(QStringLiteral("memo")));

        QSignalSpy sectionSpy(&vm, &SummaryViewModel::sectionChanged);
        vm.setDemoState(QStringLiteral("memoShort"));
        QCOMPARE(vm.section(), QStringLiteral("memo"));
        QCOMPARE(sectionSpy.size(), 1);
        QCOMPARE(vm.memo().size(), 4);
        vm.setSection(QStringLiteral("bármi"));             // ismeretlen → rövid forma
        QCOMPARE(vm.section(), QStringLiteral("exec"));

        // Régi (memó előtti) összefoglaló: a vezetői rész, a memó helyén magyarázat.
        vm.setDemoState(QStringLiteral("oldSummary"));
        QCOMPARE(vm.memoState(), QStringLiteral("missing"));
        QVERIFY(vm.openQuestions().isEmpty());
        QVERIFY(vm.markdownFor(QStringLiteral("memo")).isEmpty());
        QCOMPARE(vm.markdownFor(QStringLiteral("exec")), vm.markdownFor(QStringLiteral("all")));
        vm.setDemoState(QStringLiteral("oldMemo"));
        QCOMPARE(vm.section(), QStringLiteral("memo"));
        QCOMPARE(vm.memoState(), QStringLiteral("missing"));

        // Témánkénti összefoglaló: nincs memó-nézet; a témák nyitott kérdései megvannak.
        vm.setDemoState(QStringLiteral("topicsDoc"));
        QCOMPARE(vm.memoState(), QStringLiteral("none"));
        QCOMPARE(vm.topicSections().at(0).toMap().value("openQuestions").toStringList().size(), 1);

        // Futás: részenkénti jegyzet (3 / 6, ebből 2 korábbról), összefésülés, egy lépés.
        vm.setDemoState(QStringLiteral("emptyRunningParts"));
        QVERIFY(vm.jobRunning());
        QCOMPARE(vm.jobStage(), QStringLiteral("notes"));
        QCOMPARE(vm.jobPercent(), 50);
        QCOMPARE(vm.jobReusedParts(), 2);
        QVERIFY(!vm.jobReusedNote().isEmpty());
        QCOMPARE(vm.jobStages().size(), 2);
        vm.setDemoState(QStringLiteral("emptyRunningMerge"));
        QCOMPARE(vm.jobStage(), QStringLiteral("merge"));
        QCOMPARE(vm.jobPercent(), -1);
        vm.setDemoState(QStringLiteral("emptyRunning"));
        QCOMPARE(vm.jobStage(), QStringLiteral("single"));
        QCOMPARE(vm.jobStages().size(), 1);
        vm.setDemoState(QStringLiteral("running"));
        QCOMPARE(vm.view(), QStringLiteral("summary"));
        QVERIFY(vm.jobRunning());
        QCOMPARE(vm.jobStage(), QStringLiteral("notes"));

        // Hiba megtartott részekkel.
        vm.setDemoState(QStringLiteral("emptyErrorKept"));
        QVERIFY(vm.errorKeptParts());
        QVERIFY(!vm.jobRunning());
        vm.clearError();
        QVERIFY(!vm.errorKeptParts());

        // Témakártyák: a kész téma nyitott kérdései.
        vm.setDemoState(QStringLiteral("topics"));
        QCOMPARE(vm.topics()->data(vm.topics()->index(0), TopicListModel::ResultOpenQuestionsRole)
                     .toStringList().size(), 1);
        QVERIFY(vm.topics()->roleNames().values().contains("resultOpenQuestions"));
    }

    // Hosszú megbeszélés valódi controllerrel, ál-szerverrel: a futás szakaszai a nézetmodellben,
    // megszakítás, elbukott futás megtartott részekkel, folytatás, majd a memó és a másolás.
    void longMeetingPartsFailResumeAndMemo()
    {
        jobtest::Sandbox sb;
        const Meeting m = sb.transcribedLong("Hosszú megbeszélés", 40);   // 3 rész
        const Meeting other = sb.transcribed("Másik");
        SummaryViewModel vm;
        vm.setController(sb.app.get());
        vm.setMeetingId(m.id);
        QCOMPARE(vm.view(), QStringLiteral("empty"));

        // 1) A 2. rész függőben: a jegyzetelés 1 / 3-nál tart, valós százalékkal; megszakítható.
        sb.http->handler = [](const jobtest::FakeRequest& r) -> jobtest::FakeReply {
            if (!r.path.endsWith("/chat/completions")) return {404, "{}"};
            const int k = jobtest::notesPart(r);
            if (k == 2) { jobtest::FakeReply h; h.hold = true; return h; }
            return {200, k > 0 ? jobtest::chat(jobtest::partNotes(k)) : jobtest::mergeReply()};
        };
        sb.app->summarizeMeeting(m.id);
        QVERIFY(vm.jobRunning());
        QCOMPARE(vm.jobStage(), QStringLiteral("notes"));
        QCOMPARE(vm.jobStages().size(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(vm.jobPercent(), 33, 10000);
        QVERIFY2(vm.jobStageLabel().contains(QStringLiteral("1 / 3")), qPrintable(vm.jobStageLabel()));
        QCOMPARE(vm.jobReusedParts(), 0);
        QVERIFY(sb.app->cancelJob(m.id, JobKind::Summarize));
        QTRY_VERIFY_WITH_TIMEOUT(!vm.jobRunning(), 10000);
        QVERIFY(vm.errorMessage().isEmpty());
        QVERIFY(!vm.errorKeptParts());
        QVERIFY(QDir(m.folder).exists(QStringLiteral("summary.notes.json")));

        // 2) Újraindítás: az 1. rész a korábbi futásból jön; a 2. rész most elbukik → a hiba
        //    jelzi, hogy a kész részek megmaradtak.
        sb.http->handler = [](const jobtest::FakeRequest& r) -> jobtest::FakeReply {
            if (!r.path.endsWith("/chat/completions")) return {404, "{}"};
            const int k = jobtest::notesPart(r);
            if (k == 2) return {500, "{\"error\":{\"message\":\"overloaded\",\"code\":\"overloaded\"}}"};
            return {200, k > 0 ? jobtest::chat(jobtest::partNotes(k)) : jobtest::mergeReply()};
        };
        sb.app->summarizeMeeting(m.id);
        QCOMPARE(vm.jobReusedParts(), 1);
        QVERIFY(!vm.jobReusedNote().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!vm.errorMessage().isEmpty(), 15000);
        QVERIFY(!vm.jobRunning());
        QVERIFY(vm.errorKeptParts());
        QCOMPARE(vm.view(), QStringLiteral("empty"));

        // 3) Folytatás: csak a hiányzó részek + az összefésülés fut; kész a memó.
        sb.http->handler = [](const jobtest::FakeRequest& r) -> jobtest::FakeReply {
            if (!r.path.endsWith("/chat/completions")) return {404, "{}"};
            const int k = jobtest::notesPart(r);
            return {200, k > 0 ? jobtest::chat(jobtest::partNotes(k)) : jobtest::mergeReply()};
        };
        QSignalSpy arrived(&vm, &SummaryViewModel::summaryArrived);
        sb.app->summarizeMeeting(m.id);
        QCOMPARE(vm.jobReusedParts(), 1);
        QVERIFY(arrived.wait(15000));
        QVERIFY(!vm.jobRunning());
        QVERIFY(vm.errorMessage().isEmpty());
        QVERIFY(!vm.errorKeptParts());
        QCOMPARE(vm.view(), QStringLiteral("summary"));
        QCOMPARE(vm.section(), QStringLiteral("exec"));
        QCOMPARE(vm.memoState(), QStringLiteral("ready"));
        QCOMPARE(vm.memo().size(), 3);
        const QVariantMap third = vm.memo().at(2).toMap();
        QCOMPARE(third.value("title").toString(), QStringLiteral("Tárgy 3"));
        QCOMPARE(third.value("startMs").toLongLong(), 30 * 60000);
        QCOMPARE(third.value("range").toString(), QStringLiteral("30:00–30:40"));
        QCOMPARE(third.value("points").toStringList(), QStringList({"Egy pont a(z) 3. részből."}));
        QCOMPARE(vm.openQuestions().size(), 2);
        // A core a döntések / nyitott kérdések elejéről leveszi az időbélyeget → nincs hivatkozás.
        QCOMPARE(vm.openQuestions().at(0).toMap().value("ms").toLongLong(), -1);
        QCOMPARE(vm.openQuestions().at(0).toMap().value("text").toString(), QStringLiteral("Nyitott ügy 3"));
        QCOMPARE(vm.decisions().at(0).toMap().value("text").toString(), QStringLiteral("Marad a terv."));

        // Másolás-változatok.
        const QString exec = vm.markdownFor(QStringLiteral("exec"));
        QVERIFY(exec.contains(QStringLiteral("## Nyitott kérdések")));
        QVERIFY(!exec.contains(QStringLiteral("## Memó")));
        const QString memo = vm.markdownFor(QStringLiteral("memo"));
        QVERIFY(memo.startsWith(QStringLiteral("## Memó")));
        QVERIFY(memo.contains(QStringLiteral("### Tárgy 1 (00:00–00:40)")));
        QVERIFY(!memo.contains(QStringLiteral("Vezetői összefoglaló")));
        QVERIFY(vm.markdownFor(QStringLiteral("all")).contains(QStringLiteral("### Tárgy 2")));
        QVERIFY(vm.copyToClipboard(QStringLiteral("exec")));

        // A memó-nézet meetingváltáskor visszaáll a rövid formára.
        vm.setSection(QStringLiteral("memo"));
        QCOMPARE(vm.section(), QStringLiteral("memo"));
        vm.setMeetingId(other.id);
        QCOMPARE(vm.section(), QStringLiteral("exec"));
        QCOMPARE(vm.memoState(), QStringLiteral("none"));   // nincs összefoglalója
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
        // Résztvevők: a két beszélő beszédidő-aránnyal. A résztvevő-listát a core az átirat
        // beszélőiből állítja össze (nem a modell válaszából) → a csak említett „Vendég” nincs benne.
        QCOMPARE(vm.participants().size(), 2);
        QVERIFY(vm.participants().at(0).toMap().value("percent").toInt() > 0);
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
        // Memó előtti összefoglaló: nincs memó, nincsenek nyitott kérdések; a másolás a teljes szöveg.
        QCOMPARE(vm.memoState(), QStringLiteral("missing"));
        QVERIFY(vm.memo().isEmpty());
        QVERIFY(vm.openQuestions().isEmpty());
        QVERIFY(vm.markdownFor(QStringLiteral("memo")).isEmpty());
        QCOMPARE(vm.markdownFor(QStringLiteral("exec")), vm.markdownFor(QStringLiteral("all")));
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

    // Kontextus-hiba LM Studióval: a hibasáv a besorolt üzenetet mutatja (egyszer, nyers JSON
    // nélkül), a javító műveletek: „Betöltés nagyobb kontextussal” (token), Beállítások, Újra.
    // Ugyanez a téma-kártyán (TopicListModel szerepek).
    void contextOverflowFixActions()
    {
        jobtest::Sandbox sb;
        const Meeting m = sb.transcribed("Kontextus");
        SummaryViewModel vm;
        vm.setController(sb.app.get());
        vm.setMeetingId(m.id);
        QCOMPARE(vm.fixReloadContext(), 0);

        const QByteArray overflow =
            "{\"error\":\"Engine protocol predict request returned 400: {\\\"error\\\":{\\\"code\\\":400,\\\"message\\\":\\\"request (40000 tokens) exceeds the available context size (32768 tokens), try increasing it\\\",\\\"type\\\":\\\"exceed_context_size_error\\\",\\\"n_prompt_tokens\\\":40000,\\\"n_ctx\\\":32768}}\"}";
        sb.http->handler = [overflow](const jobtest::FakeRequest& r) -> jobtest::FakeReply {
            if (r.path == "/api/v1/models")   // LM Studio, a modell már jó kontextussal, egy szálon
                return {200, "{\"models\":[{\"type\":\"llm\",\"key\":\"teszt-modell\",\"max_context_length\":131072,\"loaded_instances\":[{\"id\":\"teszt-modell\",\"config\":{\"context_length\":32768,\"parallel\":1}}]}]}"};
            if (r.path.endsWith("/chat/completions")) return {400, overflow};
            return {500, "{}"};   // betöltés / kivétel nem várt
        };
        sb.app->summarizeMeeting(m.id);
        QTRY_VERIFY_WITH_TIMEOUT(!vm.errorMessage().isEmpty(), 10000);
        QVERIFY(!vm.jobRunning());
        QVERIFY2(vm.errorMessage().contains(QStringLiteral("token")), qPrintable(vm.errorMessage()));
        QVERIFY(!vm.errorMessage().contains(QLatin1Char('{')));
        QVERIFY(!vm.errorDetail().contains(QLatin1Char('{')));
        QVERIFY(!vm.errorDetail().contains(QStringLiteral("Engine protocol")));
        QCOMPARE(vm.fixReloadContext(), 49152);                // a betöltött 32 768 fölötti lépcső
        QCOMPARE(vm.fixActionLabel(), QStringLiteral("Beállítások"));
        QCOMPARE(vm.fixActionPage(), QStringLiteral("providers"));
        for (const jobtest::FakeRequest& r : sb.http->log)
            QVERIFY2(!r.path.startsWith(QStringLiteral("/api/v1/models/")), qPrintable(r.path));

        // A téma-kártya ugyanígy.
        const QVector<SummaryTopic> list = sb.app->setMeetingTopics(m.id, {{QString(), QStringLiteral("Árazás"), QString()}});
        TopicListModel* topics = vm.topics();
        sb.app->analyzeTopic(m.id, list[0]);
        QTRY_COMPARE_WITH_TIMEOUT(topicState(topics, 0), QStringLiteral("failed"), 10000);
        QCOMPARE(topics->data(topics->index(0), TopicListModel::FixReloadContextRole).toInt(), 49152);
        QCOMPARE(topics->data(topics->index(0), TopicListModel::FixActionPageRole).toString(), QStringLiteral("providers"));
        QVERIFY(!topics->data(topics->index(0), TopicListModel::ErrorRole).toString().contains(QLatin1Char('{')));

        // Más hiba: nincs újratöltés-gomb.
        sb.app->jobs()->clearError(m.id, JobKind::Summarize);
        sb.http->handler = [](const jobtest::FakeRequest& r) -> jobtest::FakeReply {
            if (r.path.endsWith("/chat/completions")) return {500, "{\"error\":{\"message\":\"model crashed\"}}"};
            return {404, "{}"};
        };
        sb.app->summarizeMeeting(m.id);
        QTRY_VERIFY_WITH_TIMEOUT(!vm.errorMessage().isEmpty() && !vm.jobRunning(), 10000);
        QCOMPARE(vm.fixReloadContext(), 0);
    }
    // v3 demó-állapotok (S1–S3): állítások, chipek, térkép-sorok, idézetek, célzott elavulás.
    void sourcesDemoStates()
    {
        SummaryViewModel vm;
        vm.setDemoState(QStringLiteral("sourcesOn"));
        QVERIFY(vm.hasStatements());
        QCOMPARE(vm.view(), QStringLiteral("summary"));
        QCOMPARE(vm.sentences()->count(), 4);
        QCOMPARE(vm.decisionItems()->count(), 3);
        QCOMPARE(vm.todoItems()->count(), 2);
        QAbstractItemModelTester tester(vm.sentences(), QAbstractItemModelTester::FailureReportingMode::QtTest);
        const QVariantMap s1 = vm.sentences()->get(0);
        QCOMPARE(s1.value("statementId").toString(), QStringLiteral("s1"));
        QCOMPARE(s1.value("spans").toList().at(0).toMap().value("stamp").toString(), QStringLiteral("00:04"));
        // Teendő: felelős a beszélő színében + határidő az actionItems-ből.
        const QVariantMap t1 = vm.todoItems()->get(0);
        QCOMPARE(t1.value("owners").toList().at(0).toMap().value("index").toInt(), 1);
        QCOMPARE(t1.value("due").toString(), QStringLiteral("okt. 15."));
        // A térkép „Forrás” sora: az összes forrás, az s1-é kiemelve.
        QCOMPARE(vm.activeStatementId(), QStringLiteral("s1"));
        int active = 0;
        for (const QVariant& m : vm.sourceMarks()) active += m.toMap().value("active").toBool();
        QCOMPARE(active, 1);
        QVERIFY(vm.sourceMarks().size() >= 9);
        QVERIFY(vm.mapHint().startsWith(QStringLiteral("kék")));
        QVERIFY2(vm.generationLine().contains(QStringLiteral("1298 megszólalásból")), qPrintable(vm.generationLine()));
        // „Honnan jön ez?”: az s1 két megszólalásból jön, időrendben.
        const QVariantMap det = vm.sourceDetails(QStringLiteral("s1"));
        QCOMPARE(det.value("count").toInt(), 2);
        const QVariantList quotes = det.value("quotes").toList();
        QCOMPARE(quotes.at(0).toMap().value("name").toString(), QStringLiteral("Kovács Lilla"));
        QCOMPARE(quotes.at(1).toMap().value("stamp").toString(), QStringLiteral("01:16"));
        QVERIFY(!quotes.at(0).toMap().value("text").toString().isEmpty());
        QVERIFY(vm.sourceDetails(QStringLiteral("nincs")).isEmpty());
        // Jelzés (demóban memóriában).
        QVERIFY(vm.flagStatement(QStringLiteral("d2")));
        QVERIFY(vm.decisionItems()->get(1).value("flagged").toBool());
        QVERIFY(vm.sourceDetails(QStringLiteral("d2")).value("flagged").toBool());
        // Források ki: a térkép-sor üres.
        QSignalSpy marks(&vm, &SummaryViewModel::marksChanged);
        vm.setSourcesVisible(false);
        QVERIFY(marks.count() > 0);
        QVERIFY(vm.sourceMarks().isEmpty());
        vm.setDemoState(QStringLiteral("sourcesOff"));
        QVERIFY(!vm.sourcesVisible());

        // Memó: hét szakasz sávként, a 3. kiemelve; a szakaszok beszélői színnel.
        vm.setDemoState(QStringLiteral("memoSections"));
        QCOMPARE(vm.section(), QStringLiteral("memo"));
        QCOMPARE(vm.sectionBands().size(), 7);
        QVERIFY(vm.sectionBands().at(2).toMap().value("active").toBool());
        QCOMPARE(vm.mapHint(), QStringLiteral("a kiemelt szakasz: 31:10–44:05"));
        const QVariantMap sec = vm.memo().at(2).toMap();
        QCOMPARE(sec.value("sourceRange").toString(), QStringLiteral("31:10–44:05"));
        QCOMPARE(sec.value("speakers").toList().size(), 3);
        QCOMPARE(sec.value("speakers").toList().at(1).toMap().value("colorIndex").toInt(), 1);
        QCOMPARE(vm.memo().at(6).toMap().value("sourceRange").toString(), QStringLiteral("115:50–131:04"));
        QVERIFY(vm.generationLine().startsWith(QStringLiteral("Memó · 7 szakasz")));
        vm.setActiveSection(4);
        QVERIFY(vm.sectionBands().at(4).toMap().value("active").toBool());

        // Célzott elavulás: az érintett állítás / forrás / felelős.
        vm.setDemoState(QStringLiteral("staleTargeted"));
        QVERIFY(vm.stale());
        QVERIFY(vm.staleTargeted());
        QCOMPARE(vm.affectedStatements(), 1);
        QCOMPARE(vm.affectedTodos(), 1);
        const QVariantMap s3 = vm.sentences()->get(2);
        QVERIFY(s3.value("affected").toBool());
        QVERIFY(s3.value("spans").toList().at(0).toMap().value("affected").toBool());
        QVERIFY(!s3.value("spans").toList().at(1).toMap().value("affected").toBool());
        const QVariantMap staleTodo = vm.todoItems()->get(0);
        QCOMPARE(staleTodo.value("ownerStale").toString(), QStringLiteral("Fehér Gábor → Varga Árpád?"));
        QCOMPARE(staleTodo.value("ownerStaleIndex").toInt(), 2);
        // Kijelölés nélkül a térkép az érintett forrásokat emeli ki.
        QVERIFY(vm.activeStatementId().isEmpty());
        active = 0;
        for (const QVariant& m : vm.sourceMarks()) active += m.toMap().value("active").toBool();
        QCOMPARE(active, 2);
        QCOMPARE(vm.mapHint(), QStringLiteral("kék: a javítás által érintett források"));
        QVERIFY(vm.generationLine().contains(QStringLiteral("azóta 3 beszélő-javítás")));

        // Forrás nélküli (régi) összefoglaló: a mai nézet.
        vm.setDemoState(QStringLiteral("noSources"));
        QVERIFY(!vm.hasStatements());
        QCOMPARE(vm.sentences()->count(), 0);
        QVERIFY(vm.sourceMarks().isEmpty());
        QCOMPARE(vm.memoState(), QStringLiteral("ready"));
        // A régi demó-állapotok sem kapnak állításokat.
        vm.setDemoState(QStringLiteral("done"));
        QVERIFY(!vm.hasStatements());
    }

    // Valódi controllerrel: jelölős gyors összefoglaló → állítások a megszólalásokra kötve,
    // idézet az átiratból, jelzés a lemezre, beszélő-javítás → célzott elavulás.
    void sourcesFromController()
    {
        jobtest::Sandbox sb;
        const Meeting m = sb.transcribed("Forrásos");
        SummaryViewModel vm;
        vm.setController(sb.app.get());
        vm.setMeetingId(m.id);
        sb.http->handler = [](const jobtest::FakeRequest& r) -> jobtest::FakeReply {
            if (r.path.endsWith("/chat/completions")) return {200, jobtest::chat(QString::fromUtf8(kSourcedJson))};
            return {404, "{}"};
        };
        QSignalSpy arrived(&vm, &SummaryViewModel::summaryArrived);
        sb.app->summarizeMeeting(m.id);
        QVERIFY(arrived.wait(15000));
        QVERIFY(vm.hasStatements());
        QCOMPARE(vm.sentences()->count(), 3);
        QCOMPARE(vm.decisionItems()->count(), 1);
        QCOMPARE(vm.todoItems()->count(), 1);
        // A jelölők a szövegből lekerültek; a 2. mondat a „holnap küldjük.” sorra mutat.
        QCOMPARE(vm.sentences()->get(0).value("text").toString(), QStringLiteral("Rövid egyeztetés az ajánlatról."));
        const QVariantList spans = vm.sentences()->get(1).value("spans").toList();
        QCOMPARE(spans.size(), 1);
        QCOMPARE(spans.at(0).toMap().value("utteranceIds").toStringList(), QStringList{QStringLiteral("u900")});
        // Jelölő nélküli mondat: nincs forrás (nem hiba).
        QVERIFY(!vm.sentences()->get(2).value("hasSources").toBool());
        QCOMPARE(vm.todoItems()->get(0).value("due").toString(), QStringLiteral("holnap"));
        QCOMPARE(vm.todoItems()->get(0).value("owners").toList().at(0).toMap().value("index").toInt(), 1);
        QCOMPARE(vm.sourceMarks().size(), 2);
        // Idézet a beszélő-szerkesztőből.
        const QVariantMap det = vm.sourceDetails(rowId(vm.sentences(), 1));
        QCOMPARE(det.value("count").toInt(), 1);
        const QVariantMap q = det.value("quotes").toList().at(0).toMap();
        QCOMPARE(q.value("text").toString(), QStringLiteral("holnap küldjük."));
        QCOMPARE(q.value("name").toString(), QStringLiteral("Beszélő 2"));
        QCOMPARE(q.value("startMs").toLongLong(), 900);
        QVERIFY(!vm.staleTargeted() || vm.affectedStatements() == 0);

        // Jelzés → summary.json; újratöltés után is jelzett.
        const QString did = rowId(vm.decisionItems(), 0);
        QVERIFY(vm.flagStatement(did));
        QTRY_VERIFY(vm.decisionItems()->get(0).value("flagged").toBool());
        vm.refresh();
        QVERIFY(vm.decisionItems()->get(0).value("flagged").toBool());
        QVERIFY(!vm.flagStatement(QStringLiteral("nincs-ilyen")));

        // Beszélő-javítás: a „Beszélő 2” sorára mutató állítások és a teendő felelőse érintett.
        SpeakerEditor* editor = sb.app->speakerEditor(m.id);
        QVERIFY(editor->reassignSpeaker(QStringLiteral("Beszélő 2"), QStringLiteral("Minta Márta")));
        QTRY_VERIFY_WITH_TIMEOUT(vm.stale(), 5000);
        QVERIFY(vm.staleTargeted());
        QCOMPARE(vm.affectedStatements(), 2);   // a 2. mondat + a döntés
        QCOMPARE(vm.affectedTodos(), 1);
        QVERIFY(vm.sentences()->get(1).value("affected").toBool());
        QVERIFY(!vm.sentences()->get(0).value("affected").toBool());
        QVERIFY(vm.sentences()->get(1).value("spans").toList().at(0).toMap().value("affected").toBool());
        QVERIFY2(vm.todoItems()->get(0).value("ownerStale").toString().contains(QStringLiteral("Minta Márta")),
                 qPrintable(vm.todoItems()->get(0).value("ownerStale").toString()));
        int active = 0;
        for (const QVariant& mk : vm.sourceMarks()) active += mk.toMap().value("active").toBool();
        QCOMPARE(active, 1);
        // „Rendben így”: nincs több jelölés.
        vm.dismissStale();
        QTRY_VERIFY_WITH_TIMEOUT(!vm.stale(), 5000);
        QVERIFY(!vm.sentences()->get(1).value("affected").toBool());
    }
};

QTEST_MAIN(TestSummaryViewModel)
#include "test_summary_view_model.moc"
