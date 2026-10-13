// PreTranscriptViewModel: the states of the pre-transcript view (steps, running, error) on an
// isolated TANARA_HOME with a fake HTTP server (no real provider call).
#include "JobTestSupport.h"
#include "PreTranscriptViewModel.h"

#include "tanara/library/tags/TagService.h"

#include "../unit/tags_fixture.h"

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
        QCOMPARE(vm.state(), QStringLiteral("none"));          // no meeting selected
        vm.setMeetingId(m.id);
        QVERIFY(!vm.demo());
        QCOMPARE(vm.state(), QStringLiteral("steps"));

        // No key: the bar names what is missing, the button leads to the providers page.
        QVERIFY(!vm.canStart());
        const QVariantMap b = vm.blocker();
        QVERIFY(!b.value("title").toString().isEmpty());
        QVERIFY2(b.value("text").toString().contains("Soniox"), qPrintable(b.value("text").toString()));
        QCOMPARE(b.value("actionPage").toString(), QStringLiteral("providers"));
        QVERIFY(!b.value("reason").toString().isEmpty());

        // After a key is entered (when Settings closes the view calls refresh()), it can be started.
        sb.configureStt();
        vm.refresh();
        QVERIFY(vm.canStart());
        QVERIFY(vm.blocker().isEmpty());
        QVERIFY(vm.providerLabel().contains("Soniox"));

        // The context is saved to the meeting.
        QSignalSpy noteSpy(&vm, &PreTranscriptViewModel::contextNoteChanged);
        vm.setContextNote(QStringLiteral("Negyedéves egyeztetés"));
        QCOMPARE(sb.app->store()->load(m.id).contextNote, QStringLiteral("Negyedéves egyeztetés"));
        QVERIFY(noteSpy.count() >= 1);
        QVERIFY(vm.footerLine().contains("Negyedéves"));

        // A draft typed meanwhile is saved into its OWN meeting even if the selection switches to
        // another meeting before the delayed save (e.g. a recording ends).
        const Meeting other = sb.recording("Közben kijelölt", 1);
        const QString otherNote = sb.app->store()->load(other.id).contextNote;
        vm.draftContextNote(QStringLiteral("Félbehagyott megjegyzés"));
        QCOMPARE(sb.app->store()->load(m.id).contextNote, QStringLiteral("Negyedéves egyeztetés"));
        vm.setMeetingId(other.id);
        QCOMPARE(sb.app->store()->load(m.id).contextNote, QStringLiteral("Félbehagyott megjegyzés"));
        QCOMPARE(sb.app->store()->load(other.id).contextNote, otherNote);
        QCOMPARE(vm.contextNote(), otherNote);
        vm.commitContextDraft();                           // no pending draft: writes nothing
        QCOMPARE(sb.app->store()->load(other.id).contextNote, otherNote);
        vm.setMeetingId(m.id);
        QCOMPARE(vm.contextNote(), QStringLiteral("Félbehagyott megjegyzés"));
        vm.draftContextNote(QStringLiteral("Negyedéves egyeztetés"));
        vm.commitContextDraft();
        QCOMPARE(sb.app->store()->load(m.id).contextNote, QStringLiteral("Negyedéves egyeztetés"));

        // Without a voice model the identification switch is unavailable (and cannot be set).
        QVERIFY(!vm.identifyAvailable());
        QVERIFY(!vm.identifyEnabled());

        // Mixdown: not yet.
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
        // The provider keeps the transcription in the "processing" state.
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
        // Mixdown, upload, transcription: the stages show the real state.
        QTRY_COMPARE_WITH_TIMEOUT(stage("transcribe").value("state").toString(), QStringLiteral("running"), 30000);
        QCOMPARE(stage("mixdown").value("state").toString(), QStringLiteral("done"));
        QCOMPARE(stage("upload").value("state").toString(), QStringLiteral("done"));
        // During transcription the provider gives no percentage, so indeterminate (-1).
        QCOMPARE(stage("transcribe").value("percent").toInt(), -1);
        // Without an earlier measured run there is no estimate.
        QVERIFY(vm.etaText().isEmpty());
        QVERIFY(vm.cancellable());

        // Cancel: no error, back to the steps; the recording is intact.
        QVERIFY(sb.app->cancelJob(m.id, JobKind::Transcribe));
        QTRY_COMPARE_WITH_TIMEOUT(vm.state(), QStringLiteral("steps"), 15000);
        QVERIFY(vm.errorMessage().isEmpty());
        QVERIFY(QFile::exists(QDir(m.folder).filePath("track_mic.wav")));
        QCOMPARE(vm.mixdownState(), QStringLiteral("ready"));      // finished during the mixdown
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

        // Another view model (after a restart / switching back) sees the same error.
        PreTranscriptViewModel again;
        again.setController(sb.app.get());
        again.setMeetingId(m.id);
        QCOMPARE(again.state(), QStringLiteral("failed"));

        // Retry with a working provider: the transcript is made, the error disappears.
        sb.http->handler = [](const jobtest::FakeRequest& r) { return jobtest::sonioxOk(r); };
        QSignalSpy ready(sb.app.get(), &app::AppController::transcriptReady);
        sb.app->transcribeMeeting(m.id);
        QCOMPARE(vm.state(), QStringLiteral("running"));
        QVERIFY(ready.wait(30000));
        QTRY_VERIFY_WITH_TIMEOUT(!sb.app->jobs()->isRunning(m.id, JobKind::Transcribe), 15000);
        QVERIFY(sb.app->store()->load(m.id).hasTranscript);
        QVERIFY(vm.errorMessage().isEmpty());
    }

    void demoTagStep()
    {
        PreTranscriptViewModel vm;
        vm.setDemoState(QStringLiteral("note"));
        QVERIFY(vm.demo());
        // One applied tag, the title-based suggestion with the reason.
        QCOMPARE(vm.tags()->tags().size(), 1);
        QCOMPARE(vm.tags()->tags().first().toMap().value("name").toString(), QStringLiteral("Nordvik"));
        QCOMPARE(vm.tagSuggestions().size(), 1);
        QCOMPARE(vm.tagSuggestions().first().toMap().value("name").toString(), QStringLiteral("Ügyféltámogatás"));
        QCOMPARE(vm.tagSuggestionReason(), QStringLiteral("hasonló cím: „Ügyféltámogatás heti”"));
        // The note cards also bring the source meeting's tags.
        QCOMPARE(vm.note()->suggestions().first().toMap().value("tags").toStringList(), QStringList{"Nordvik"});
        QSignalSpy toasts(&vm, &PreTranscriptViewModel::toast);
        vm.acceptTagSuggestion(0);
        QCOMPARE(vm.tags()->tags().size(), 2);
        QVERIFY(vm.tagSuggestions().isEmpty());
        QCOMPARE(toasts.count(), 1);
    }

    void tagStepDraftSuggestions()
    {
        jobtest::Sandbox sb;
        const tagsfixture::Library lib = tagsfixture::buildLibrary(*sb.app->store());
        library::TagService* tags = sb.app->tags();
        const Tag nordvik = tags->addTag(lib.nordvik1, QStringLiteral("Nordvik"));
        tags->addTag(lib.nordvik2, nordvik.id);
        sb.app->setMeetingContextNote(lib.nordvik1, QStringLiteral("Résztvevők: Kovács Anna."));
        const Meeting m = sb.recording("Nordvik heti egyeztetés", 1);

        PreTranscriptViewModel vm;
        vm.setController(sb.app.get());
        vm.setMeetingId(m.id);
        QCOMPARE(vm.state(), QStringLiteral("steps"));
        QVERIFY(vm.tags()->tags().isEmpty());
        // Without a transcript, by the title (draftTagSuggestions), with a reason.
        QCOMPARE(vm.tagSuggestions().size(), 1);
        QCOMPARE(vm.tagSuggestions().first().toMap().value("id").toString(), nordvik.id);
        QVERIFY2(vm.tagSuggestionReason().startsWith(QStringLiteral("hasonló cím: „Nordvik")),
                 qPrintable(vm.tagSuggestionReason()));
        // The tag on the note card of the meeting with a similar title.
        bool cardHasTag = false;
        for (const QVariant& v : vm.note()->suggestions())
            if (v.toMap().value("meetingId").toString() == lib.nordvik1)
                cardHasTag = v.toMap().value("tags").toStringList() == QStringList{QStringLiteral("Nordvik")};
        QVERIFY(cardHasTag);

        // Accept: it is applied, the suggestion disappears; an undoable step (toast).
        QSignalSpy toasts(&vm, &PreTranscriptViewModel::toast);
        vm.acceptTagSuggestion(0);
        QCOMPARE(tags->tagsOf(m.id), QStringList{nordvik.id});
        QCOMPARE(vm.tags()->tagIds(), QStringList{nordvik.id});
        QVERIFY(vm.tagSuggestions().isEmpty());
        QCOMPARE(toasts.count(), 1);
        QCOMPARE(toasts.last().at(0).toString(), QStringLiteral("Javaslat elfogadva: #Nordvik"));
        QVERIFY(toasts.last().at(1).toBool());
        vm.tags()->undo();
        QVERIFY(tags->tagsOf(m.id).isEmpty());
        QCOMPARE(vm.tagSuggestions().size(), 1);

        // Reject: does not come back; after undo it does.
        vm.rejectTagSuggestion(0);
        QVERIFY(tags->isRejected(m.id, nordvik.id));
        QVERIFY(vm.tagSuggestions().isEmpty());
        vm.tags()->undo();
        QCOMPARE(vm.tagSuggestions().size(), 1);

        // By hand from the field: by name (a new tag too).
        vm.tags()->add(QStringLiteral("Logisztika"));
        QCOMPARE(vm.tags()->tags().size(), 1);
        QCOMPARE(tags->tag(tags->tagsOf(m.id).value(0)).name, QStringLiteral("Logisztika"));
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
