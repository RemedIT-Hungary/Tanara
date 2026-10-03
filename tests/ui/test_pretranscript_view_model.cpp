// PreTranscriptViewModel — az átirat előtti nézet (M03 lépések, M04 futás, M05 hiba) állapotai
// izolált TANARA_HOME-on, ál-HTTP-szerverrel (valódi szolgáltató-hívás nincs).
#include "JobTestSupport.h"
#include "PreTranscriptViewModel.h"

#include <QSignalSpy>
#include <QtTest>

using namespace tanara;
using tanara_qml::PreTranscriptViewModel;

class TestPreTranscriptViewModel : public QObject {
    Q_OBJECT
private slots:
    void demoStatesWithoutController()
    {
        PreTranscriptViewModel vm;
        QVERIFY(vm.demo());
        QCOMPARE(vm.state(), QStringLiteral("steps"));
        QVERIFY(!vm.canStart());
        QVERIFY(!vm.blocker().value("title").toString().isEmpty());
        QVERIFY(!vm.contextNote().isEmpty());

        vm.setDemoState(QStringLiteral("ready"));
        QVERIFY(vm.canStart());
        QVERIFY(vm.blocker().isEmpty());

        vm.setDemoState(QStringLiteral("running"));
        QCOMPARE(vm.state(), QStringLiteral("running"));
        QVERIFY(vm.stages().size() >= 3);
        QVERIFY(!vm.etaText().isEmpty());

        vm.setDemoState(QStringLiteral("failed"));
        QCOMPARE(vm.state(), QStringLiteral("failed"));
        QVERIFY(vm.errorDetail().contains("HTTP 401"));
        QCOMPARE(vm.fixActionPage(), QStringLiteral("providers"));
    }

    void gatingNamesTheMissingKeyAndContextPersists()
    {
        jobtest::Sandbox sb;
        const Meeting m = sb.recording("Kapuzás", 1);

        PreTranscriptViewModel vm;
        vm.setController(sb.app.get());
        QCOMPARE(vm.state(), QStringLiteral("none"));          // nincs kijelölt meeting
        vm.setMeetingId(m.id);
        QVERIFY(!vm.demo());
        QCOMPARE(vm.state(), QStringLiteral("steps"));

        // Nincs kulcs → a sáv megnevezi a hiányt, a gomb a szolgáltatók oldalára visz.
        QVERIFY(!vm.canStart());
        const QVariantMap b = vm.blocker();
        QVERIFY(!b.value("title").toString().isEmpty());
        QVERIFY2(b.value("text").toString().contains("Soniox"), qPrintable(b.value("text").toString()));
        QCOMPARE(b.value("actionPage").toString(), QStringLiteral("providers"));
        QVERIFY(!b.value("reason").toString().isEmpty());

        // Kulcs megadása után (a Beállítások bezárásakor a nézet refresh()-t hív) indítható.
        sb.configureStt();
        vm.refresh();
        QVERIFY(vm.canStart());
        QVERIFY(vm.blocker().isEmpty());
        QVERIFY(vm.providerLabel().contains("Soniox"));

        // A kontextus a meetinghez mentődik.
        QSignalSpy noteSpy(&vm, &PreTranscriptViewModel::contextNoteChanged);
        vm.setContextNote(QStringLiteral("Negyedéves egyeztetés"));
        QCOMPARE(sb.app->store()->load(m.id).contextNote, QStringLiteral("Negyedéves egyeztetés"));
        QVERIFY(noteSpy.count() >= 1);
        QVERIFY(vm.footerLine().contains("Negyedéves"));

        // A gépelés közbeni piszkozat a SAJÁT megbeszélésébe mentődik akkor is, ha a kijelölés
        // a késleltetett mentés előtt másik megbeszélésre vált (pl. véget ér egy felvétel).
        const Meeting other = sb.recording("Közben kijelölt", 1);
        const QString otherNote = sb.app->store()->load(other.id).contextNote;
        vm.draftContextNote(QStringLiteral("Félbehagyott megjegyzés"));
        QCOMPARE(sb.app->store()->load(m.id).contextNote, QStringLiteral("Negyedéves egyeztetés"));
        vm.setMeetingId(other.id);
        QCOMPARE(sb.app->store()->load(m.id).contextNote, QStringLiteral("Félbehagyott megjegyzés"));
        QCOMPARE(sb.app->store()->load(other.id).contextNote, otherNote);
        QCOMPARE(vm.contextNote(), otherNote);
        vm.commitContextDraft();                           // nincs függő piszkozat → nem ír semmit
        QCOMPARE(sb.app->store()->load(other.id).contextNote, otherNote);
        vm.setMeetingId(m.id);
        QCOMPARE(vm.contextNote(), QStringLiteral("Félbehagyott megjegyzés"));
        vm.draftContextNote(QStringLiteral("Negyedéves egyeztetés"));
        vm.commitContextDraft();
        QCOMPARE(sb.app->store()->load(m.id).contextNote, QStringLiteral("Negyedéves egyeztetés"));

        // Hang-modell nélkül az azonosítás kapcsoló nem elérhető (és nem állítható).
        QVERIFY(!vm.identifyAvailable());
        QVERIFY(!vm.identifyEnabled());

        // Lekeverés: még nincs.
        QCOMPARE(vm.mixdownState(), QStringLiteral("missing"));
    }

    void identifySkipFlagRoundTrip()
    {
        jobtest::Sandbox sb;
        const Meeting m = sb.recording("Azonosítás", 1);
        QVERIFY(sb.app->identifyAfterTranscription(m.id));
        sb.app->setIdentifyAfterTranscription(m.id, false);
        QVERIFY(!sb.app->identifyAfterTranscription(m.id));
        sb.app->setIdentifyAfterTranscription(m.id, true);
        QVERIFY(sb.app->identifyAfterTranscription(m.id));
    }

    void runningStagesThenCancel()
    {
        jobtest::Sandbox sb;
        if (!sb.ffmpeg) QSKIP("ffmpeg nem található");
        sb.configureStt();
        // A szolgáltató „feldolgozás alatt” állapotban tartja az átírást.
        sb.http->handler = [](const jobtest::FakeRequest& r) { return jobtest::sonioxOk(r, "processing"); };
        const Meeting m = sb.recording("Futó átírás", 2);

        PreTranscriptViewModel vm;
        vm.setController(sb.app.get());
        vm.setMeetingId(m.id);
        QCOMPARE(vm.state(), QStringLiteral("steps"));

        sb.app->transcribeMeeting(m.id);
        QCOMPARE(vm.state(), QStringLiteral("running"));
        QVERIFY(!vm.jobTitle().isEmpty());

        auto stage = [&vm](const QString& id) {
            for (const QVariant& v : vm.stages())
                if (v.toMap().value("id").toString() == id) return v.toMap();
            return QVariantMap();
        };
        // Lekeverés → feltöltés → átírás: a szakaszok valós állapotot mutatnak.
        QTRY_COMPARE_WITH_TIMEOUT(stage("transcribe").value("state").toString(), QStringLiteral("running"), 30000);
        QCOMPARE(stage("mixdown").value("state").toString(), QStringLiteral("done"));
        QCOMPARE(stage("upload").value("state").toString(), QStringLiteral("done"));
        // Az átírás alatt a szolgáltató nem ad százalékot → határozatlan (-1).
        QCOMPARE(stage("transcribe").value("percent").toInt(), -1);
        // Korábbi mért futás nélkül nincs becslés.
        QVERIFY(vm.etaText().isEmpty());
        QVERIFY(vm.cancellable());

        // Megszakítás: nincs hiba, vissza a lépésekhez; a felvétel megvan.
        QVERIFY(sb.app->cancelJob(m.id, JobKind::Transcribe));
        QTRY_COMPARE_WITH_TIMEOUT(vm.state(), QStringLiteral("steps"), 15000);
        QVERIFY(vm.errorMessage().isEmpty());
        QVERIFY(QFile::exists(QDir(m.folder).filePath("track_mic.wav")));
        QCOMPARE(vm.mixdownState(), QStringLiteral("ready"));      // a lekeverés közben elkészült
    }

    void failureCardWithFixActionThenRetrySucceeds()
    {
        jobtest::Sandbox sb;
        if (!sb.ffmpeg) QSKIP("ffmpeg nem található");
        sb.configureStt();
        sb.http->handler = [](const jobtest::FakeRequest& r) -> jobtest::FakeReply {
            if (r.method == "POST" && r.path.endsWith("/files"))
                return {401, "{\"status_code\":401,\"error_type\":\"invalid_api_key\",\"message\":\"Invalid API key.\"}"};
            return {200, "{}"};
        };
        const Meeting m = sb.recording("Bukó átírás", 2);

        PreTranscriptViewModel vm;
        vm.setController(sb.app.get());
        vm.setMeetingId(m.id);
        sb.app->transcribeMeeting(m.id);
        QTRY_COMPARE_WITH_TIMEOUT(vm.state(), QStringLiteral("failed"), 30000);
        QVERIFY(!vm.errorMessage().isEmpty());
        QVERIFY2(vm.errorDetail().startsWith("HTTP 401 · invalid_api_key"), qPrintable(vm.errorDetail()));
        QCOMPARE(vm.fixActionLabel(), QStringLiteral("Kulcs módosítása"));
        QCOMPARE(vm.fixActionPage(), QStringLiteral("providers"));

        // Másik nézetmodell (újraindítás / visszaváltás után) ugyanazt a hibát látja.
        PreTranscriptViewModel again;
        again.setController(sb.app.get());
        again.setMeetingId(m.id);
        QCOMPARE(again.state(), QStringLiteral("failed"));

        // Újrapróbálás működő szolgáltatóval → az átirat elkészül, a hiba eltűnik.
        sb.http->handler = [](const jobtest::FakeRequest& r) { return jobtest::sonioxOk(r); };
        QSignalSpy ready(sb.app.get(), &AppController::transcriptReady);
        sb.app->transcribeMeeting(m.id);
        QCOMPARE(vm.state(), QStringLiteral("running"));
        QVERIFY(ready.wait(30000));
        QTRY_VERIFY_WITH_TIMEOUT(!sb.app->jobs()->isRunning(m.id, JobKind::Transcribe), 15000);
        QVERIFY(sb.app->store()->load(m.id).hasTranscript);
        QVERIFY(vm.errorMessage().isEmpty());
    }

    void clearErrorReturnsToSteps()
    {
        jobtest::Sandbox sb;
        if (!sb.ffmpeg) QSKIP("ffmpeg nem található");
        sb.configureStt();
        sb.http->handler = [](const jobtest::FakeRequest& r) -> jobtest::FakeReply {
            if (r.method == "POST" && r.path.endsWith("/files")) return {500, "{}"};
            return {200, "{}"};
        };
        const Meeting m = sb.recording("Hiba elvetése", 2);
        PreTranscriptViewModel vm;
        vm.setController(sb.app.get());
        vm.setMeetingId(m.id);
        sb.app->transcribeMeeting(m.id);
        QTRY_COMPARE_WITH_TIMEOUT(vm.state(), QStringLiteral("failed"), 30000);
        vm.clearError();
        QCOMPARE(vm.state(), QStringLiteral("steps"));
        QVERIFY(vm.canStart());
    }
};

QTEST_MAIN(TestPreTranscriptViewModel)
#include "test_pretranscript_view_model.moc"
