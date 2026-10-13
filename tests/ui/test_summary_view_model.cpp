// SummaryViewModel + TopicListModel: the states of the Summary tab on an isolated TANARA_HOME
// with a fake LLM server (no real provider call).
#include "JobTestSupport.h"
#include "ShellMeetingModel.h"
#include "SummaryProgress.h"
#include "SummaryViewModel.h"
#include "TopicListModel.h"

#include "tanara/editing/session/SpeakerEditor.h"

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

// A quick summary with source markers: the sentences, the decision and the action item point
// to the two utterances (u0: "Hi all, the offer" from Speaker 1; u900: "we will send it
// tomorrow." from Speaker 2); the second sentence has no marker.
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
        // No timestamp, so no reference, the text is untouched (the number is part of the sentence).
        QCOMPARE(SummaryViewModel::splitTimestamp(QStringLiteral("A határidő 12:30-kor jár le."), &rest), -1);
        QCOMPARE(rest, QStringLiteral("A határidő 12:30-kor jár le."));
        QCOMPARE(SummaryViewModel::splitTimestamp(QStringLiteral("3 fővel bővül a csapat."), &rest), -1);
    }

    // Mapping of the job stages (Summary tab + job bar).
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
        // Short meeting: one stage, indeterminate.
        SummaryProgress p = SummaryProgress::from(job({{"single", "Összefoglalás", StageState::Running, -1, {}}}, -1, -1));
        QCOMPARE(p.stage, QStringLiteral("single"));
        QCOMPARE(p.percent, -1);
        QCOMPARE(p.stages.size(), 1);
        QVERIFY(!p.label.isEmpty());

        // Part notes: a real bar from the finished parts, and parts taken over from an earlier run.
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

        // Merge: indeterminate; the note taking is done.
        p = SummaryProgress::from(job({{"notes", "Jegyzetelés részenként", StageState::Done, -1, "5 rész kész"},
                                       {"merge", "Összegzés", StageState::Running, -1, {}}}, 5, 5));
        QCOMPARE(p.stage, QStringLiteral("merge"));
        QCOMPARE(p.percent, -1);
        QCOMPARE(p.reused, 0);
        QCOMPARE(p.stages.at(0).toMap().value("state").toString(), QStringLiteral("done"));
        QCOMPARE(p.stages.at(1).toMap().value("state").toString(), QStringLiteral("running"));

        // A summary without stages (end of the per-topic analysis) and another job: no mapping.
        QVERIFY(!SummaryProgress::from(job({}, -1, -1)).isValid());
        JobProgress other = notes;
        other.kind = JobKind::AnalyzeTopics;
        QVERIFY(!SummaryProgress::from(other).isValid());
        QCOMPARE(SummaryProgress::reusedParts(QStringLiteral("3/5. rész")), 0);
        QCOMPARE(SummaryProgress::reusedParts(QStringLiteral("Part 3/5 · 4 from an earlier run")), 4);

        // The job bar likewise: k / n parts with a real percentage, the merge indeterminate.
        QVariantMap strip = ShellMeetingModel::describeJob(notes);
        QCOMPARE(strip.value("percent").toInt(), 40);
        QVERIFY2(strip.value("detail").toString().contains(QStringLiteral("2 / 5")), qPrintable(strip.value("detail").toString()));
        strip = ShellMeetingModel::describeJob(job({{"notes", "x", StageState::Done, -1, {}},
                                                    {"merge", "y", StageState::Running, -1, {}}}, 5, 5));
        QCOMPARE(strip.value("percent").toInt(), -1);
    }

    // The demo states: executive summary with open questions, memo (many / few sections), old
    // summary, a run with parts, an error with kept parts, topics' open questions.
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

        // Copy: the executive part (without the memo), the memo only, or both.
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
        vm.setSection(QStringLiteral("bármi"));             // unknown: short form
        QCOMPARE(vm.section(), QStringLiteral("exec"));

        // Old (pre-memo) summary: the executive part, an explanation in the memo's place.
        vm.setDemoState(QStringLiteral("oldSummary"));
        QCOMPARE(vm.memoState(), QStringLiteral("missing"));
        QVERIFY(vm.openQuestions().isEmpty());
        QVERIFY(vm.markdownFor(QStringLiteral("memo")).isEmpty());
        QCOMPARE(vm.markdownFor(QStringLiteral("exec")), vm.markdownFor(QStringLiteral("all")));
        vm.setDemoState(QStringLiteral("oldMemo"));
        QCOMPARE(vm.section(), QStringLiteral("memo"));
        QCOMPARE(vm.memoState(), QStringLiteral("missing"));

        // Per-topic summary: no memo view; the topics' open questions are there.
        vm.setDemoState(QStringLiteral("topicsDoc"));
        QCOMPARE(vm.memoState(), QStringLiteral("none"));
        QCOMPARE(vm.topicSections().at(0).toMap().value("openQuestions").toStringList().size(), 1);

        // Run: part notes (3 / 6, of which 2 from earlier), merge, one step.
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

        // Error with kept parts.
        vm.setDemoState(QStringLiteral("emptyErrorKept"));
        QVERIFY(vm.errorKeptParts());
        QVERIFY(!vm.jobRunning());
        vm.clearError();
        QVERIFY(!vm.errorKeptParts());

        // Topic cards: the finished topic's open questions.
        vm.setDemoState(QStringLiteral("topics"));
        QCOMPARE(vm.topics()->data(vm.topics()->index(0), TopicListModel::ResultOpenQuestionsRole)
                     .toStringList().size(), 1);
        QVERIFY(vm.topics()->roleNames().values().contains("resultOpenQuestions"));
    }

    // A long meeting with a real controller and a fake server: the run's stages in the view
    // model, cancel, a failed run with kept parts, resume, then the memo and copying.
    void longMeetingPartsFailResumeAndMemo()
    {
        jobtest::Sandbox sb;
        const Meeting m = sb.transcribedLong("Hosszú megbeszélés", 40);   // 3 parts
        const Meeting other = sb.transcribed("Másik");
        SummaryViewModel vm;
        vm.setController(sb.app.get());
        vm.setMeetingId(m.id);
        QCOMPARE(vm.view(), QStringLiteral("empty"));

        // 1) Part 2 is pending: note taking is at 1 / 3, with a real percentage; cancellable.
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

        // 2) Restart: part 1 comes from the earlier run; part 2 fails now, so the error says the
        //    finished parts were kept.
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

        // 3) Resume: only the missing parts + the merge run; the memo is done.
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
        // Core strips the timestamp from the start of decisions / open questions, so there is no reference.
        QCOMPARE(vm.openQuestions().at(0).toMap().value("ms").toLongLong(), -1);
        QCOMPARE(vm.openQuestions().at(0).toMap().value("text").toString(), QStringLiteral("Nyitott ügy 3"));
        QCOMPARE(vm.decisions().at(0).toMap().value("text").toString(), QStringLiteral("Marad a terv."));

        // Copy variants.
        const QString exec = vm.markdownFor(QStringLiteral("exec"));
        QVERIFY(exec.contains(QStringLiteral("## Nyitott kérdések")));
        QVERIFY(!exec.contains(QStringLiteral("## Memó")));
        const QString memo = vm.markdownFor(QStringLiteral("memo"));
        QVERIFY(memo.startsWith(QStringLiteral("## Memó")));
        QVERIFY(memo.contains(QStringLiteral("### Tárgy 1 (00:00–00:40)")));
        QVERIFY(!memo.contains(QStringLiteral("Vezetői összefoglaló")));
        QVERIFY(vm.markdownFor(QStringLiteral("all")).contains(QStringLiteral("### Tárgy 2")));
        QVERIFY(vm.copyToClipboard(QStringLiteral("exec")));

        // On a meeting switch the memo view goes back to the short form.
        vm.setSection(QStringLiteral("memo"));
        QCOMPARE(vm.section(), QStringLiteral("memo"));
        vm.setMeetingId(other.id);
        QCOMPARE(vm.section(), QStringLiteral("exec"));
        QCOMPARE(vm.memoState(), QStringLiteral("none"));   // it has no summary
    }

    void demoStatesWithoutController()
    {
        SummaryViewModel vm;
        QVERIFY(vm.demo());
        QCOMPARE(vm.view(), QStringLiteral("summary"));          // default: stale
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
        // In demo mode editing works in memory.
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

        // A missing server address: the bar names it, the button leads to the providers.
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

        // Running state: the request is pending, so cancellable; after cancel there is no error.
        sb.http->handler = [](const jobtest::FakeRequest&) { return jobtest::FakeReply{200, "{}", true}; };
        sb.app->summarizeMeeting(m.id);
        QVERIFY(vm.jobRunning());
        QCOMPARE(vm.jobKind(), int(JobKind::Summarize));
        QVERIFY(!vm.jobTitle().isEmpty());
        QVERIFY(sb.app->cancelJob(m.id, JobKind::Summarize));
        QTRY_VERIFY_WITH_TIMEOUT(!vm.jobRunning(), 10000);
        QVERIFY(vm.errorMessage().isEmpty());
        QCOMPARE(vm.view(), QStringLiteral("empty"));

        // Error: the message + the technical line stay; can be dismissed.
        sb.http->handler = [](const jobtest::FakeRequest&) -> jobtest::FakeReply {
            return {500, "{\"error\":{\"message\":\"overloaded\",\"code\":\"overloaded\"}}"};
        };
        sb.app->summarizeMeeting(m.id);
        QTRY_VERIFY_WITH_TIMEOUT(!vm.errorMessage().isEmpty(), 15000);
        QVERIFY(!vm.jobRunning());
        QVERIFY2(vm.errorDetail().contains("HTTP 500"), qPrintable(vm.errorDetail()));
        vm.clearError();
        QVERIFY(vm.errorMessage().isEmpty());

        // Success: the view switches to the summary, with structured content.
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
        QCOMPARE(vm.decisions().at(0).toMap().value("ms").toLongLong(), -1);   // no timestamp in the data
        QCOMPARE(vm.actions().size(), 1);
        // The owner is a speaker of the meeting, so in the speaker's color (colorIndex 1: the second speaker).
        QCOMPARE(vm.actions().at(0).toMap().value("ownerIndex").toInt(), 1);
        QVERIFY(vm.metaLine().contains("saját kulcs"));
        QCOMPARE(vm.modelLine(), QStringLiteral("teszt-modell"));
        // Participants: the two speakers with talk-time share. Core builds the participant list from
        // the transcript's speakers (not from the model's reply), so the merely mentioned "Guest" is not in it.
        QCOMPARE(vm.participants().size(), 2);
        QVERIFY(vm.participants().at(0).toMap().value("percent").toInt() > 0);
        QVERIFY(!vm.stale());

        // A speaker fix after the summary makes it stale, with a real count; "Fine as is" releases it.
        editing::SpeakerEditor* editor = sb.app->speakerEditor(m.id);
        QVERIFY(editor);
        QVERIFY(editor->reassignSpeaker(QStringLiteral("Beszélő 2"), QStringLiteral("Minta Márta")));
        QTRY_VERIFY_WITH_TIMEOUT(vm.stale(), 5000);
        QCOMPARE(vm.staleCount(), 1);
        // The participant list follows the edit.
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
        QCOMPARE(vm.participants().size(), 2);                 // the two speakers, no separate "mentioned"
        // Pre-memo summary: no memo, no open questions; copy is the whole text.
        QCOMPARE(vm.memoState(), QStringLiteral("missing"));
        QVERIFY(vm.memo().isEmpty());
        QVERIFY(vm.openQuestions().isEmpty());
        QVERIFY(vm.markdownFor(QStringLiteral("memo")).isEmpty());
        QCOMPARE(vm.markdownFor(QStringLiteral("exec")), vm.markdownFor(QStringLiteral("all")));
        QVERIFY(!vm.metaLine().isEmpty());                     // the file's time
        QVERIFY(vm.modelLine().isEmpty());                     // old summary: no model data
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

        // A topic suggestion from the model opens the editor.
        sb.http->handler = [](const jobtest::FakeRequest& r) -> jobtest::FakeReply {
            if (!r.path.endsWith("/chat/completions")) return {404, "{}"};
            return {200, jobtest::chat(QStringLiteral(
                "{\"topics\":[{\"title\":\"Árazás\",\"summary\":\"Csomagok\"},{\"title\":\"Pilot\",\"summary\":\"Helyszínek\"}]}"))};
        };
        QSignalSpy topicsReady(sb.app.get(), &app::AppController::topicsReady);
        sb.app->extractMeetingTopics(m.id);
        QVERIFY(vm->jobRunning());
        QCOMPARE(vm->jobKind(), int(JobKind::ExtractTopics));
        QVERIFY(topicsReady.wait(15000));
        QCOMPARE(vm->view(), QStringLiteral("topics"));
        QCOMPARE(topics->count(), 2);
        QCOMPARE(topics->missingCount(), 2);

        // Add / edit / reorder / delete: all go to disk immediately.
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
        topics->removeTopic(2);                                // "Pilot" deleted
        QCOMPARE(topics->count(), 2);
        QCOMPARE(sb.app->meetingTopics(m.id).size(), 2);
        QVERIFY(topics->countsText().contains("2 téma"));

        // Analysis: the first succeeds, the second fails.
        int call = 0;
        sb.http->handler = [&call](const jobtest::FakeRequest& r) -> jobtest::FakeReply {
            if (!r.path.endsWith("/chat/completions")) return {404, "{}"};
            ++call;
            if (call == 2) return {500, "{\"error\":{\"message\":\"overloaded\",\"code\":\"overloaded\"}}"};
            return {200, jobtest::chat(QStringLiteral(
                "Részletes elemzés %1.\n\n## Döntések\n- Döntés %1\n\n## Teendők\n- Teendő %1 — Beszélő 1 (péntek)\n").arg(call))};
        };
        QSignalSpy queueDone(sb.app.get(), &app::AppController::topicAnalysisQueueFinished);
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

        // "Restart": a new view model on the same meeting continues from disk (the workspace is
        // open, the finished and the failed topic states are there).
        vm.reset();
        vm = std::make_unique<SummaryViewModel>();
        vm->setController(sb.app.get());
        vm->setMeetingId(m.id);
        topics = vm->topics();
        QCOMPARE(vm->view(), QStringLiteral("topics"));
        QCOMPARE(topics->count(), 2);
        QCOMPARE(topicState(topics, 0), QStringLiteral("done"));
        QCOMPARE(topicState(topics, 1), QStringLiteral("failed"));

        // The failed topic can be re-run on its own; cancellable while running.
        sb.http->handler = [](const jobtest::FakeRequest&) { return jobtest::FakeReply{200, "{}", true}; };
        const QVector<SummaryTopic> list = sb.app->meetingTopics(m.id);
        sb.app->analyzeTopic(m.id, list[1]);
        QCOMPARE(topicState(topics, 1), QStringLiteral("running"));
        topics->cancelTopic(1);
        QTRY_VERIFY_WITH_TIMEOUT(topicState(topics, 1) != QStringLiteral("running"), 10000);
        QCOMPARE(topicState(topics, 0), QStringLiteral("done"));       // the finished result stayed

        // Analyzing the missing ones with a working provider, then the reduce; the view switches to the summary.
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
        // The topics stayed: the workspace can be reopened.
        vm->setTopicsOpen(true);
        QCOMPARE(vm->view(), QStringLiteral("topics"));
        QCOMPARE(vm->topics()->doneCount(), 2);
    }

    // Context error with LM Studio: the error bar shows the classified message (once, without raw
    // JSON), the fix actions: "Load with a larger context" (tokens), Settings, Retry.
    // The same on the topic card (TopicListModel roles).
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
            if (r.path == "/api/v1/models")   // LM Studio, the model already has a good context, one thread
                return {200, "{\"models\":[{\"type\":\"llm\",\"key\":\"teszt-modell\",\"max_context_length\":131072,\"loaded_instances\":[{\"id\":\"teszt-modell\",\"config\":{\"context_length\":32768,\"parallel\":1}}]}]}"};
            if (r.path.endsWith("/chat/completions")) return {400, overflow};
            return {500, "{}"};   // load / exception not expected
        };
        sb.app->summarizeMeeting(m.id);
        QTRY_VERIFY_WITH_TIMEOUT(!vm.errorMessage().isEmpty(), 10000);
        QVERIFY(!vm.jobRunning());
        QVERIFY2(vm.errorMessage().contains(QStringLiteral("token")), qPrintable(vm.errorMessage()));
        QVERIFY(!vm.errorMessage().contains(QLatin1Char('{')));
        QVERIFY(!vm.errorDetail().contains(QLatin1Char('{')));
        QVERIFY(!vm.errorDetail().contains(QStringLiteral("Engine protocol")));
        QCOMPARE(vm.fixReloadContext(), 49152);                // the step above the loaded 32,768
        QCOMPARE(vm.fixActionLabel(), QStringLiteral("Beállítások"));
        QCOMPARE(vm.fixActionPage(), QStringLiteral("providers"));
        for (const jobtest::FakeRequest& r : sb.http->log)
            QVERIFY2(!r.path.startsWith(QStringLiteral("/api/v1/models/")), qPrintable(r.path));

        // The topic card likewise.
        const QVector<SummaryTopic> list = sb.app->setMeetingTopics(m.id, {{QString(), QStringLiteral("Árazás"), QString()}});
        TopicListModel* topics = vm.topics();
        sb.app->analyzeTopic(m.id, list[0]);
        QTRY_COMPARE_WITH_TIMEOUT(topicState(topics, 0), QStringLiteral("failed"), 10000);
        QCOMPARE(topics->data(topics->index(0), TopicListModel::FixReloadContextRole).toInt(), 49152);
        QCOMPARE(topics->data(topics->index(0), TopicListModel::FixActionPageRole).toString(), QStringLiteral("providers"));
        QVERIFY(!topics->data(topics->index(0), TopicListModel::ErrorRole).toString().contains(QLatin1Char('{')));

        // Another error: no reload button.
        sb.app->jobs()->clearError(m.id, JobKind::Summarize);
        sb.http->handler = [](const jobtest::FakeRequest& r) -> jobtest::FakeReply {
            if (r.path.endsWith("/chat/completions")) return {500, "{\"error\":{\"message\":\"model crashed\"}}"};
            return {404, "{}"};
        };
        sb.app->summarizeMeeting(m.id);
        QTRY_VERIFY_WITH_TIMEOUT(!vm.errorMessage().isEmpty() && !vm.jobRunning(), 10000);
        QCOMPARE(vm.fixReloadContext(), 0);
    }
    // Demo states: claims, chips, map rows, quotes, targeted staleness.
    void sourcesDemoStates()
    {
        SummaryViewModel vm;
        vm.setDemoState(QStringLiteral("sourcesOn"));
        QVERIFY(vm.hasStatements());
        QCOMPARE(vm.view(), QStringLiteral("summary"));
        QCOMPARE(vm.sentences()->count(), 4);
        QCOMPARE(vm.decisionItems()->count(), 3);
        QCOMPARE(vm.todoItems()->count(), 2);
        // Open questions are claims too, with a source chip.
        QCOMPARE(vm.questionItems()->count(), 2);
        QCOMPARE(vm.questionItems()->get(0).value("statementId").toString(), QStringLiteral("q1"));
        QCOMPARE(vm.questionItems()->get(0).value("spans").toList().at(0).toMap().value("stamp").toString(),
                 QStringLiteral("43:30"));
        QCOMPARE(vm.sourceDetails(QStringLiteral("q2")).value("count").toInt(), 1);
        QAbstractItemModelTester tester(vm.sentences(), QAbstractItemModelTester::FailureReportingMode::QtTest);
        const QVariantMap s1 = vm.sentences()->get(0);
        QCOMPARE(s1.value("statementId").toString(), QStringLiteral("s1"));
        QCOMPARE(s1.value("spans").toList().at(0).toMap().value("stamp").toString(), QStringLiteral("00:04"));
        // Action item: the owner in the speaker's color + the due date from actionItems.
        const QVariantMap t1 = vm.todoItems()->get(0);
        QCOMPARE(t1.value("owners").toList().at(0).toMap().value("index").toInt(), 1);
        QCOMPARE(t1.value("due").toString(), QStringLiteral("okt. 15."));
        // The map's "Source" row: all the sources, s1's highlighted.
        QCOMPARE(vm.activeStatementId(), QStringLiteral("s1"));
        int active = 0;
        for (const QVariant& m : vm.sourceMarks()) active += m.toMap().value("active").toBool();
        QCOMPARE(active, 1);
        QVERIFY(vm.sourceMarks().size() >= 9);
        QVERIFY(vm.mapHint().startsWith(QStringLiteral("kék")));
        QVERIFY2(vm.generationLine().contains(QStringLiteral("1298 megszólalásból")), qPrintable(vm.generationLine()));
        // "Where does this come from?": s1 comes from two utterances, in chronological order.
        const QVariantMap det = vm.sourceDetails(QStringLiteral("s1"));
        QCOMPARE(det.value("count").toInt(), 2);
        const QVariantList quotes = det.value("quotes").toList();
        QCOMPARE(quotes.at(0).toMap().value("name").toString(), QStringLiteral("Kovács Lilla"));
        QCOMPARE(quotes.at(1).toMap().value("stamp").toString(), QStringLiteral("01:16"));
        QVERIFY(!quotes.at(0).toMap().value("text").toString().isEmpty());
        QVERIFY(vm.sourceDetails(QStringLiteral("nincs")).isEmpty());
        // Flag (in memory in demo mode).
        QVERIFY(vm.flagStatement(QStringLiteral("d2")));
        QVERIFY(vm.decisionItems()->get(1).value("flagged").toBool());
        QVERIFY(vm.sourceDetails(QStringLiteral("d2")).value("flagged").toBool());
        // Sources off: the map row is empty.
        QSignalSpy marks(&vm, &SummaryViewModel::marksChanged);
        vm.setSourcesVisible(false);
        QVERIFY(marks.count() > 0);
        QVERIFY(vm.sourceMarks().isEmpty());
        vm.setDemoState(QStringLiteral("sourcesOff"));
        QVERIFY(!vm.sourcesVisible());

        // Memo: seven sections as a bar, the 3rd highlighted; the sections' speakers in color.
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
        // Memo points and intros are claims too; section 3's intro is two sentences (with chips),
        // followed by "Arguments made" (3 points) and "Open question" (1).
        QCOMPARE(vm.memoItems()->count(), 16);
        const QVariantList intro = sec.value("introRows").toList();
        QCOMPARE(intro.size(), 2);
        QCOMPARE(intro.at(0).toMap().value("statementId").toString(), QStringLiteral("i3.1"));
        QCOMPARE(intro.at(1).toMap().value("spans").toList().at(0).toMap().value("stamp").toString(),
                 QStringLiteral("33:40"));
        const QVariantList blocks = sec.value("blocks").toList();
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks.at(0).toMap().value("key").toString(), QStringLiteral("arguments"));
        QCOMPARE(blocks.at(0).toMap().value("title").toString(), QStringLiteral("Elhangzott érvek"));
        QCOMPARE(blocks.at(0).toMap().value("rows").toList().size(), 3);
        const QVariantMap m32 = blocks.at(0).toMap().value("rows").toList().at(1).toMap();
        QCOMPARE(m32.value("statementId").toString(), QStringLiteral("m3.2"));
        QCOMPARE(m32.value("spans").toList().size(), 2);
        QCOMPARE(blocks.at(1).toMap().value("title").toString(), QStringLiteral("Nyitott kérdés"));
        QCOMPARE(vm.sourceDetails(QStringLiteral("m3.2")).value("count").toInt(), 2);
        // A point without a claim (section 5): a bare line, no source.
        const QVariantMap plain = vm.memo().at(4).toMap().value("blocks").toList().at(0).toMap()
                                      .value("rows").toList().at(0).toMap();
        QVERIFY(plain.value("statementId").toString().isEmpty());
        QVERIFY(!plain.value("hasSources").toBool());
        // The map's "Source" row in the memo: the memo claims' sources, the highlighted section's is active.
        active = 0;
        for (const QVariant& mk : vm.sourceMarks()) active += mk.toMap().value("active").toBool();
        QCOMPARE(active, 7);
        vm.setActiveStatementId(QStringLiteral("m3.2"));
        active = 0;
        for (const QVariant& mk : vm.sourceMarks()) active += mk.toMap().value("active").toBool();
        QCOMPARE(active, 2);
        vm.setActiveStatementId(QString());
        // Flag on a memo point: the section's row is flagged too.
        QVERIFY(vm.flagStatement(QStringLiteral("m3.1")));
        QVERIFY(vm.memo().at(2).toMap().value("blocks").toList().at(0).toMap().value("rows").toList()
                    .at(0).toMap().value("flagged").toBool());
        vm.setActiveSection(4);
        QVERIFY(vm.sectionBands().at(4).toMap().value("active").toBool());

        // Targeted staleness: the affected claim / source / owner.
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
        // Without a selection the map highlights the affected sources.
        QVERIFY(vm.activeStatementId().isEmpty());
        active = 0;
        for (const QVariant& m : vm.sourceMarks()) active += m.toMap().value("active").toBool();
        QCOMPARE(active, 2);
        QCOMPARE(vm.mapHint(), QStringLiteral("kék: a javítás által érintett források"));
        QVERIFY(vm.generationLine().contains(QStringLiteral("azóta 3 beszélő-javítás")));

        // A summary without sources (old): today's view.
        vm.setDemoState(QStringLiteral("noSources"));
        QVERIFY(!vm.hasStatements());
        QCOMPARE(vm.sentences()->count(), 0);
        QVERIFY(vm.sourceMarks().isEmpty());
        QCOMPARE(vm.memoState(), QStringLiteral("ready"));
        // The old demo states get no claims either.
        vm.setDemoState(QStringLiteral("done"));
        QVERIFY(!vm.hasStatements());
    }

    // With a real controller: a quick summary with markers gives claims bound to the utterances,
    // a quote from the transcript, a flag to disk, a speaker fix gives targeted staleness.
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
        // The markers were removed from the text; sentence 2 points to the "we will send it tomorrow." line.
        QCOMPARE(vm.sentences()->get(0).value("text").toString(), QStringLiteral("Rövid egyeztetés az ajánlatról."));
        const QVariantList spans = vm.sentences()->get(1).value("spans").toList();
        QCOMPARE(spans.size(), 1);
        QCOMPARE(spans.at(0).toMap().value("utteranceIds").toStringList(), QStringList{QStringLiteral("u900")});
        // A sentence without a marker: no source (not an error).
        QVERIFY(!vm.sentences()->get(2).value("hasSources").toBool());
        QCOMPARE(vm.todoItems()->get(0).value("due").toString(), QStringLiteral("holnap"));
        QCOMPARE(vm.todoItems()->get(0).value("owners").toList().at(0).toMap().value("index").toInt(), 1);
        QCOMPARE(vm.sourceMarks().size(), 2);
        // Quote from the speaker editor.
        const QVariantMap det = vm.sourceDetails(rowId(vm.sentences(), 1));
        QCOMPARE(det.value("count").toInt(), 1);
        const QVariantMap q = det.value("quotes").toList().at(0).toMap();
        QCOMPARE(q.value("text").toString(), QStringLiteral("holnap küldjük."));
        QCOMPARE(q.value("name").toString(), QStringLiteral("Beszélő 2"));
        QCOMPARE(q.value("startMs").toLongLong(), 900);
        QVERIFY(!vm.staleTargeted() || vm.affectedStatements() == 0);

        // Flag goes to summary.json; still flagged after a reload.
        const QString did = rowId(vm.decisionItems(), 0);
        QVERIFY(vm.flagStatement(did));
        QTRY_VERIFY(vm.decisionItems()->get(0).value("flagged").toBool());
        vm.refresh();
        QVERIFY(vm.decisionItems()->get(0).value("flagged").toBool());
        QVERIFY(!vm.flagStatement(QStringLiteral("nincs-ilyen")));

        // Speaker fix: the claims pointing to "Speaker 2"'s lines and the action item's owner are affected.
        editing::SpeakerEditor* editor = sb.app->speakerEditor(m.id);
        QVERIFY(editor->reassignSpeaker(QStringLiteral("Beszélő 2"), QStringLiteral("Minta Márta")));
        QTRY_VERIFY_WITH_TIMEOUT(vm.stale(), 5000);
        QVERIFY(vm.staleTargeted());
        QCOMPARE(vm.affectedStatements(), 2);   // sentence 2 + the decision
        QCOMPARE(vm.affectedTodos(), 1);
        QVERIFY(vm.sentences()->get(1).value("affected").toBool());
        QVERIFY(!vm.sentences()->get(0).value("affected").toBool());
        QVERIFY(vm.sentences()->get(1).value("spans").toList().at(0).toMap().value("affected").toBool());
        QVERIFY2(vm.todoItems()->get(0).value("ownerStale").toString().contains(QStringLiteral("Minta Márta")),
                 qPrintable(vm.todoItems()->get(0).value("ownerStale").toString()));
        int active = 0;
        for (const QVariant& mk : vm.sourceMarks()) active += mk.toMap().value("active").toBool();
        QCOMPARE(active, 1);
        // "Fine as is": no more markings.
        vm.dismissStale();
        QTRY_VERIFY_WITH_TIMEOUT(!vm.stale(), 5000);
        QVERIFY(!vm.sentences()->get(1).value("affected").toBool());
    }
};

QTEST_MAIN(TestSummaryViewModel)
#include "test_summary_view_model.moc"
