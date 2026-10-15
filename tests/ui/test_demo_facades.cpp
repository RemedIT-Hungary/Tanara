// The fictional library / shell / import / overview facades of tanara_demo (what --demo, --gallery,
// --qml-shot and the tests serve the view models), and AppContext handing them out; plus a real
// controller's set (the transitional adapters of ControllerFacades.h) in an ISOLATED TANARA_HOME.
#include "AppContext.h"
#include "FacadeTestSupport.h"

#include "demo/ImportDemoFacade.h"
#include "demo/LibraryDemoData.h"
#include "demo/LibraryDemoFacade.h"
#include "demo/OverviewDemoFacade.h"
#include "demo/ShellDemoFacade.h"
#include "demo/SettingsDemoFacade.h"
#include "demo/TracksDemoFacade.h"
#include "demo/VoiceModelsDemoFacade.h"

#include "tanara/app/AppController.h"
#include "tanara/app/ISettingsFacade.h"
#include "tanara/library/IMeetingNoteFacade.h"
#include "tanara/library/tracks/ITracksFacade.h"
#include "tanara/processing/voiceid/IVoiceModelsFacade.h"
#include "tanara/recording/IRecorderFacade.h"

#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace tanara_qml;

class TestDemoFacades : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        AppContext::instance()->setController(nullptr);
        AppContext::instance()->setDemo(true);
        tanara_demo::resetTags();
    }

    void contextServesTheFictionalSetWithoutAController()
    {
        AppContext* ctx = AppContext::instance();
        const UiServices& demo = ctx->demoServices();
        QVERIFY(qobject_cast<tanara_demo::LibraryDemoFacade*>(demo.library));
        QVERIFY(qobject_cast<tanara_demo::ShellDemoFacade*>(demo.shell));
        QVERIFY(qobject_cast<tanara_demo::ImportDemoFacade*>(demo.import));
        QCOMPARE(ctx->services().library, demo.library);
        QCOMPARE(ctx->services().shell, demo.shell);
        QVERIFY(!demo.shell->live());
        QVERIFY(!demo.import->live());
        // The overview facade is the model's own: a new one per request, serving its design state.
        QObject owner;
        auto* a = qobject_cast<tanara_demo::OverviewDemoFacade*>(demo.overview(QStringLiteral("overviewDone"), &owner));
        auto* b = qobject_cast<tanara_demo::OverviewDemoFacade*>(demo.overview(QStringLiteral("overviewEmpty"), &owner));
        QVERIFY(a && b && a != b);
        QCOMPARE(a->parent(), &owner);
        QCOMPARE(a->scenario(), QStringLiteral("overviewDone"));
    }

    void libraryFacadeTagsTheSampleLibrary()
    {
        tanara_demo::LibraryDemoFacade lib;
        QCOMPARE(lib.query({}).totalMeetings, int(tanara_demo::meetings().size()));
        QCOMPARE(lib.entry(QStringLiteral("demo-partner")).id, QStringLiteral("demo-partner"));
        QVERIFY(lib.entry(QStringLiteral("nincs-ilyen")).id.isEmpty());
        QCOMPARE(lib.tagName(QStringLiteral("t-q4")), tanara_demo::tagName(QStringLiteral("t-q4")));
        QCOMPARE(lib.pendingItems(1).size(), 1);

        QSignalSpy tags(&lib, &tanara::library::ILibraryFacade::tagsChanged);
        QSignalSpy changed(&lib, &tanara::library::ILibraryFacade::meetingChanged);
        QVERIFY(lib.addTagToMeetings({QStringLiteral("demo-heti"), QStringLiteral("demo-vezetoi")},
                                     QStringLiteral("Pilot 2027"), QStringLiteral("label")));
        QVERIFY(tags.count() >= 1);
        QCOMPARE(changed.count(), 2);
        const QString id = tanara_demo::tagIdByName(QStringLiteral("Pilot 2027"));
        QVERIFY(!id.isEmpty());
        QVERIFY(lib.entry(QStringLiteral("demo-vezetoi")).tagIds.contains(id));
        QVERIFY(lib.removeTagFromMeetings({QStringLiteral("demo-vezetoi")}, id, QStringLiteral("label")));
        QVERIFY(!lib.entry(QStringLiteral("demo-vezetoi")).tagIds.contains(id));
        QVERIFY(lib.entry(QStringLiteral("demo-heti")).tagIds.contains(id));
    }

    void shellFacadeAnswersForTheSampleMeetingsAndDoesNothing()
    {
        tanara_demo::ShellDemoFacade shell;
        const QString id = tanara_demo::defaultMeetingId();
        const tanara::Meeting m = shell.meeting(id);
        QCOMPARE(m.id, id);
        QVERIFY(shell.meetingExists(id));
        QVERIFY(!shell.meetingExists(QStringLiteral("nincs-ilyen")));
        QVERIFY(!shell.meetingExists(QString()));
        QCOMPARE(shell.meetingTitle(id), tanara_demo::find(id)->entry.title);
        QCOMPARE(shell.speakerCount(m), tanara_demo::find(id)->speakers);
        QVERIFY(shell.summaryStale(m).stale);
        QVERIFY(!m.tracks.isEmpty());
        QVERIFY(shell.meeting(QStringLiteral("nincs-ilyen")).id.isEmpty());
        // Nothing starts, nothing is running.
        QVERIFY(!shell.identifyAsync(id));
        QVERIFY(!shell.exportArchive(id, QStringLiteral("/tmp/x.zip")));
        QVERIFY(!shell.importBusy());
        QCOMPARE(shell.recordingState(), tanara::RecordingState::Idle);
        QVERIFY(shell.processingState(id).jobs.isEmpty());
        QCOMPARE(shell.sampleTasks(QString()).size(), 1);
        QCOMPARE(shell.retranscribeImpact(id).manualCorrections(), 23);
    }

    void importFacadeServesTheDesignStates()
    {
        tanara_demo::ImportDemoFacade imp;
        QVERIFY(!imp.canStart(QString()));
        QVERIFY(imp.canStart(QStringLiteral("files")));
        QVERIFY(imp.scenario(QString())->files.isEmpty());
        const auto files = imp.scenario(QStringLiteral("files"));
        QCOMPARE(files->files.size(), 3);
        QCOMPARE(files->tags.size(), 1);
        QVERIFY(files->suggestion.has_value());
        QVERIFY(!files->running);
        const auto progress = imp.scenario(QStringLiteral("progress"));
        QVERIFY(progress->running);
        QCOMPARE(progress->percent, 42);
        QVERIFY(imp.scenario(QStringLiteral("failed"))->error.size() > 0);
        QVERIFY(!imp.scenario(QStringLiteral("probing"))->files.last().probed);
        QVERIFY(!imp.scenario(QStringLiteral("error"))->files.last().info.ok);
        QVERIFY(imp.scenario(QStringLiteral("split"))->files.first().split);
        // Tags: a pool name keeps its id, another name gets a made-up one.
        QCOMPARE(imp.createTag(QStringLiteral("nordvik")).id, QStringLiteral("t-nordvik"));
        QCOMPARE(imp.createTag(QStringLiteral("Új címke")).id, QStringLiteral("new-Új címke"));
        QVERIFY(imp.start({}).isEmpty());
    }

    void overviewFacadeKeepsItsEdits()
    {
        tanara_demo::OverviewDemoFacade done(QStringLiteral("overviewDone"));
        QSignalSpy changed(&done, &tanara::library::IOverviewFacade::changed);
        const tanara::library::OverviewSnapshot s = done.snapshot(QString());
        QVERIFY(s.hasTranscript);
        QVERIFY(s.allDone);
        QCOMPARE(s.stats.size(), 6);
        const int rows = int(s.participants.size());

        const QString id = done.addParticipant(QString(), QStringLiteral("Új Ember"));
        QVERIFY(!id.isEmpty());
        QCOMPARE(done.snapshot(QString()).participants.size(), rows + 1);
        done.removeParticipant(QString(), id);
        QCOMPARE(done.snapshot(QString()).participants.size(), rows);
        done.setContextNote(QString(), QStringLiteral("jegyzet"));
        QCOMPARE(done.snapshot(QString()).contextNote, QStringLiteral("jegyzet"));
        QCOMPARE(changed.count(), 3);
        QVERIFY(done.setSolo(QString()));
        // Another instance is another state.
        tanara_demo::OverviewDemoFacade fresh(QStringLiteral("overviewDone"));
        QVERIFY(fresh.snapshot(QString()).contextNote != QStringLiteral("jegyzet"));
        tanara_demo::OverviewDemoFacade empty(QStringLiteral("overviewEmpty"));
        QCOMPARE(empty.snapshot(QString()).peopleState, QStringLiteral("empty"));
        QVERIFY(empty.snapshot(QString()).analysisRunning);
    }

    // D5: the overview facade's speakers states, the segmentation scenario rows of the voice-model facade.
    void speakersDesignStates()
    {
        using tanara_demo::OverviewDemoFacade;
        auto snap = [](const char* st) { return OverviewDemoFacade(QString::fromLatin1(st)).snapshot(QString()); };
        // The untouched finished state says nothing (screenshots stay as they were).
        QVERIFY(snap("overviewDone").speakers.isEmpty());
        for (const char* st : {"overviewSpeakersLocal", "overviewSpeakersProvider", "overviewSpeakersOff",
                               "overviewSpeakersNoModel", "overviewDiarizing", "overviewDiarizeFailed", "overviewDiarizeAsk"}) {
            const tanara::library::OverviewSnapshot s = snap(st);
            QVERIFY2(s.hasTranscript, st);
            QVERIFY2(!s.speakers.value(QStringLiteral("line")).toString().isEmpty(), st);
            QVERIFY2(!s.allDone, st);
        }
        QVERIFY(snap("overviewDiarizing").transcribing);
        QVERIFY(!snap("overviewSpeakersLocal").transcribing);
        QCOMPARE(snap("overviewSpeakersNoModel").speakers.value("fixTarget").toString(), QStringLiteral("diarization"));

        QObject owner;
        const UiServices& demo = AppContext::instance()->demoServices();
        auto* none = demo.voiceModels(QStringLiteral("B08segmissing"), &owner);
        QCOMPARE(none->scenario()->rows.value(QStringLiteral("pyannote-segmentation-3.0")).state, QStringLiteral("missing"));
        auto* mism = demo.voiceModels(QStringLiteral("B08segmismatch"), &owner);
        QCOMPARE(mism->scenario()->rows.value(QStringLiteral("pyannote-segmentation-3.0")).state, QStringLiteral("mismatch"));
        auto* allMissing = demo.voiceModels(QStringLiteral("B08none"), &owner);
        QCOMPARE(allMissing->scenario()->rows.value(QStringLiteral("pyannote-segmentation-3.0-int8")).state, QStringLiteral("missing"));
    }

    // The summary step's design states (the same pure function as the real facade).
    void summaryStepDesignStates()
    {
        using tanara_demo::OverviewDemoFacade;
        auto stepOf = [](const char* st) {
            for (const QVariant& v : OverviewDemoFacade(QString::fromLatin1(st)).snapshot(QString()).steps)
                if (v.toMap().value(QStringLiteral("key")).toString() == QLatin1String("summary")) return v.toMap();
            return QVariantMap();
        };
        QCOMPARE(stepOf("overviewDone").value("phase").toString(), QStringLiteral("done"));
        QCOMPARE(stepOf("overviewProcessing").value("phase").toString(), QStringLiteral("noTranscript"));
        QCOMPARE(stepOf("overviewSpeakersLocal").value("phase").toString(), QStringLiteral("ready"));
        QCOMPARE(stepOf("overviewSummaryReady").value("phase").toString(), QStringLiteral("ready"));
        QCOMPARE(stepOf("overviewSummaryBlocked").value("phase").toString(), QStringLiteral("blocked"));
        QCOMPARE(stepOf("overviewSummaryBlocked").value("fixTarget").toString(), QStringLiteral("services"));
        QCOMPARE(stepOf("overviewSummaryRunning").value("phase").toString(), QStringLiteral("running"));
        QCOMPARE(stepOf("overviewSummaryRunning").value("progress").toInt(), 40);
        const QVariantMap failed = stepOf("overviewSummaryFailed");
        QCOMPARE(failed.value("phase").toString(), QStringLiteral("failed"));
        QVERIFY(failed.value("note").toString().contains(QStringLiteral("http://localhost:1234")));
        QVERIFY(!failed.value("detail").toString().isEmpty());
        QCOMPARE(stepOf("overviewSummaryStale").value("phase").toString(), QStringLiteral("stale"));
        // Open items keep the steps expanded.
        for (const char* st : {"overviewSummaryReady", "overviewSummaryBlocked", "overviewSummaryRunning",
                               "overviewSummaryFailed", "overviewSummaryStale"})
            QVERIFY2(!OverviewDemoFacade(QString::fromLatin1(st)).snapshot(QString()).allDone, st);
    }

    // P15-6a: the settings facade serves a design state and keeps everything in memory.
    void settingsFacadeServesTheDesignStates()
    {
        QObject owner;
        const UiServices& demo = AppContext::instance()->demoServices();
        tanara::app::ISettingsFacade* b04 = demo.settings(QStringLiteral("B04"), &owner);
        QVERIFY(qobject_cast<tanara_demo::SettingsDemoFacade*>(b04));
        QCOMPARE(b04->parent(), &owner);
        QVERIFY(!b04->live());
        QVERIFY(!b04->hasSecret(QStringLiteral("soniox.apiKey")));            // B04: the key is missing
        QCOMPARE(b04->scenario()->page, QStringLiteral("services"));
        QCOMPARE(b04->scenario()->focusField, QStringLiteral("stt"));
        QVERIFY(b04->registry().has(tanara::providers::CapabilityKind::Transcription, QStringLiteral("soniox")));
        QVERIFY(b04->devices() && !b04->devices()->captureDevices().isEmpty());
        QSignalSpy changed(b04, &tanara::app::ISettingsFacade::settingsChanged);
        tanara::app::AppSettings s = b04->settings();
        s.recording.audioQuality = QStringLiteral("best");
        b04->setSettings(s);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(b04->settings().recording.audioQuality, QStringLiteral("best"));
        // The connection test answers without the network, in the next turn.
        QSignalSpy tested(b04, &tanara::app::ISettingsFacade::connectionTested);
        const tanara::providers::ProviderDescriptor d = b04->registry().descriptor(
            tanara::providers::CapabilityKind::Chat, QStringLiteral("openai-compat"));
        const int id = b04->testConnection(d, {});
        QCOMPARE(tested.count(), 0);
        QVERIFY(tested.wait(1000));
        QCOMPARE(tested.at(0).at(0).toInt(), id);

        tanara::app::ISettingsFacade* onboarding = demo.onboarding(QStringLiteral("watcher"), &owner);
        QCOMPARE(onboarding->scenario()->onboardingStep, QStringLiteral("watcher"));
        QCOMPARE(onboarding->scenario()->onboardingStatus.value(QStringLiteral("folders")), QStringLiteral("skipped"));
        QVERIFY(demo.meetingNotes == nullptr);
        QVERIFY(demo.uiStateFile.isEmpty());
    }

    void voiceModelAndTrackFacadesServeTheDesignStates()
    {
        QObject owner;
        const UiServices& demo = AppContext::instance()->demoServices();
        tanara::processing::IVoiceModelsFacade* partial = demo.voiceModels(QStringLiteral("B08partial"), &owner);
        QVERIFY(!partial->live());
        QCOMPARE(partial->backfillSkips().size(), 3);
        QCOMPARE(partial->scenario()->backfillState, QStringLiteral("partial"));
        tanara::processing::IVoiceModelsFacade* note = demo.voiceModels(QStringLiteral("samplesModels"), &owner);
        QCOMPARE(note->backfillSkips(QStringLiteral("Valaki")).size(), 1);
        QVERIFY(note->backfillSkips().isEmpty());

        tanara::library::ITracksFacade* tracks = demo.tracks(QStringLiteral("default"), &owner);
        const auto fixture = tracks->fixture(QString());
        QVERIFY(fixture && fixture->rows.size() == 4);
        QCOMPARE(fixture->mixdownState, QStringLiteral("running"));
        QSignalSpy changed(tracks, &tanara::library::ITracksFacade::tracksChanged);
        QCOMPARE(tracks->deleteDroppedTracks(QStringLiteral("m")), 1);   // the dropped system audio
        QCOMPARE(changed.count(), 1);
        QCOMPARE(tracks->fixture(QString())->rows.size(), 3);
        QVERIFY(tracks->meeting(QStringLiteral("nincs")).id.isEmpty());
    }

    // A controller's set (isolated TANARA_HOME): real, cached, and gone with the controller.
    void realFacadesAreBuiltPerController()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("TANARA_HOME", home.path().toUtf8());
        qputenv("TANARA_CLOUD", "off");
        AppContext* ctx = AppContext::instance();
        QPointer<QObject> shell;
        {
            tanara::app::AppController c;
            const UiServices& set = ctx->servicesFor(&c);
            QVERIFY(set.library && set.shell && set.import && set.overview);
            QVERIFY(set.shell->live());
            QVERIFY(set.import->live());
            QCOMPARE(set.shell->parent(), static_cast<QObject*>(&c));
            QCOMPARE(set.library->parent(), static_cast<QObject*>(&c));
            QCOMPARE(set.shell->tagController(), static_cast<QObject*>(&c));
            QVERIFY(set.library != ctx->demoServices().library);
            QObject owner;
            QCOMPARE(set.overview(QString(), &owner), set.overview(QStringLiteral("x"), &owner));   // shared
            // An unknown meeting: nothing to show, nothing breaks.
            QVERIFY(!set.shell->meetingExists(QStringLiteral("nincs")));
            QVERIFY(set.shell->meeting(QStringLiteral("nincs")).id.isEmpty());
            QCOMPARE(set.library->query({}).totalMeetings, 0);
            QVERIFY(set.overview(QString(), &owner)->snapshot(QStringLiteral("nincs")).participants.isEmpty());
            // P15-6a: the settings / voice models / tracks / note facades are real and shared.
            QVERIFY(set.settings(QString(), &owner)->live());
            QCOMPARE(set.settings(QString(), &owner), set.onboarding(QStringLiteral("x"), &owner));
            QVERIFY(set.voiceModels(QString(), &owner)->live());
            QVERIFY(set.tracks(QString(), &owner)->live());
            QVERIFY(!set.tracks(QString(), &owner)->fixture(QStringLiteral("nincs")));
            QVERIFY(set.meetingNotes);
            QVERIFY(set.uiStateFile.startsWith(home.path()));
            shell = set.shell;
        }
        QVERIFY(shell.isNull());
    }
};

QTEST_MAIN(TestDemoFacades)
#include "test_demo_facades.moc"
