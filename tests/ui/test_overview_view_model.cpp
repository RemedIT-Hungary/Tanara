// The Overview tab's view model (OverviewViewModel) with a real AppController in an ISOLATED
// TANARA_HOME (QTemporaryDir; no audio, no model, no network): adding and removing participants
// by hand, "only I spoke", the manual description, the processing steps before and after the
// transcript, the DATA tiles, the "nobody" state (only anonymous speakers), the tracks' speech
// share and including them in the mixdown (TrackListModel). Demo data: the four screen states.
#include "AppContext.h"
#include "UiServices.h"
#include "FacadeTestSupport.h"
#include "OverviewViewModel.h"
#include "ParticipantsViewModel.h"
#include "ShellMeetingModel.h"
#include "TrackListModel.h"

#include "tanara/app/AppController.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/editing/undo/MeetingUndo.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/library/tags/TagService.h"
#include "tanara/processing/analysis/ParticipantAnalysis.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace tanara_qml;

namespace {

// S5: a facade double with the split button's routes (the stored route, the cloud mode and the estimate are
// set by the test; the estimate's answer waits until the test releases it).
class RouteFakeFacade : public tanara::library::IOverviewFacade {
public:
    using Route = tanara::providers::Route;
    Route stored = Route::Local;
    bool cloudLive = false;
    QStringList calls;
    std::function<void(const tanara::library::CloudRunEstimate&)> pendingEstimate;

    tanara::library::OverviewSnapshot snapshot(const QString&) override
    {
        tanara::library::OverviewSnapshot s;
        s.steps = {QVariantMap{{"key", "transcript"}, {"title", "Átirat"}},
                   QVariantMap{{"key", "summary"}, {"title", "Összefoglaló"}}};
        return s;
    }
    QString addParticipant(const QString&, const QString&) override { return {}; }
    void removeParticipant(const QString&, const QString&) override {}
    bool setSolo(const QString&) override { return false; }
    bool reanalyze(const QString&) override { return false; }
    void setContextNote(const QString&, const QString&) override {}

    tanara::library::RouteChoices routeChoices(tanara::providers::Capability cap, const QString&) const override
    {
        tanara::library::RouteChoices c;
        c.available = true;
        c.stored = stored;
        c.cloudLive = cloudLive;
        tanara::library::RouteChoiceRow local{Route::Local, QStringLiteral("Whisper"), {}, {}, 120};
        c.rows << local;
        if (cap == tanara::providers::Capability::Stt)
            c.rows << tanara::library::RouteChoiceRow{Route::External, QStringLiteral("Soniox"), {}, {}, -1};
        tanara::library::RouteChoiceRow cloud{Route::Cloud, QStringLiteral("Pontos"),
                                              cloudLive ? QString() : QStringLiteral("cloud_unavailable"), {}, -1};
        c.rows << cloud;
        return c;
    }
    void estimateCloud(tanara::providers::WorkflowStep step, const QString& meetingId,
                       std::function<void(const tanara::library::CloudRunEstimate&)> done) override
    {
        calls << QStringLiteral("estimate:%1:%2").arg(step == tanara::providers::WorkflowStep::Transcribe ? "stt" : "llm", meetingId);
        pendingEstimate = std::move(done);
    }
    bool setDefaultRoute(tanara::providers::Capability cap, Route route) override
    {
        calls << QStringLiteral("default:%1:%2").arg(tanara::providers::capabilityKey(cap), tanara::providers::routeKey(route));
        stored = route;
        return true;
    }
};

QVariantMap optionOf(const QVariantMap& step, const QString& route)
{
    for (const QVariant& v : step.value(QStringLiteral("routeOptions")).toList())
        if (v.toMap().value(QStringLiteral("route")).toString() == route) return v.toMap();
    return {};
}

} // namespace

class TestOverviewViewModel : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<tanara::app::AppController> m_app;

    tanara::Meeting recording(const QString& title)
    {
        tanara::Meeting m = m_app->store()->createMeeting(title);
        tanara::Track mic;
        mic.id = QStringLiteral("mic");
        mic.kind = tanara::TrackKind::Mic;
        mic.file = QStringLiteral("track_mic.wav");
        mic.speechRatio = 0.38;
        tanara::Track sys;
        sys.id = QStringLiteral("system");
        sys.kind = tanara::TrackKind::Loopback;
        sys.file = QStringLiteral("track_system.wav");
        sys.active = false;
        sys.speechRatio = 0.0;
        sys.excludedReason = QStringLiteral("noSpeech");
        m.tracks = {mic, sys};
        m.durationMs = 4564000;   // 1:16:04
        for (const tanara::Track& t : m.tracks) {
            QFile f(QDir(m.folder).filePath(t.file));
            if (f.open(QIODevice::WriteOnly)) f.write("nem valódi hang");
        }
        m_app->store()->saveMeeting(m);
        return m;
    }
    tanara::Meeting transcribed(const QString& title)
    {
        tanara::Meeting m = recording(title);
        QFile seg(QDir(m.folder).filePath(QStringLiteral("transcript.segments.json")));
        if (seg.open(QIODevice::WriteOnly))
            seg.write(QJsonDocument(QJsonArray{
                QJsonObject{{"startMs", 0}, {"endMs", 900}, {"speaker", "Beszélő 1"}, {"text", "Sziasztok."}},
                QJsonObject{{"startMs", 1000}, {"endMs", 1900}, {"speaker", "Beszélő 2"}, {"text", "Szia."}},
                QJsonObject{{"startMs", 2000}, {"endMs", 2900}, {"speaker", "Beszélő 1"}, {"text", "Kezdjük."}}}).toJson());
        m.hasTranscript = true;
        m_app->store()->saveMeeting(m);
        return m;
    }
    static QVariantMap stepOf(const OverviewViewModel& vm, const QString& key)
    {
        for (const QVariant& v : vm.steps())
            if (v.toMap().value(QStringLiteral("key")).toString() == key) return v.toMap();
        return {};
    }
    static QStringList names(const OverviewViewModel& vm)
    {
        QStringList out;
        for (const QVariant& v : vm.participants()) out << v.toMap().value(QStringLiteral("name")).toString();
        return out;
    }

private slots:
    void initTestCase() { qputenv("TANARA_CLOUD", "off"); }

    void init()
    {
        m_home = std::make_unique<QTemporaryDir>();
        QVERIFY(m_home->isValid());
        qputenv("TANARA_HOME", m_home->path().toUtf8());
        AppContext::instance()->setDemo(false);
        m_app = std::make_unique<tanara::app::AppController>();
        QVERIFY(m_app->store()->audioDir().startsWith(m_home->path()));
    }
    void cleanup()
    {
        m_app.reset();
        m_home.reset();
        qunsetenv("TANARA_HOME");
    }

    void monogram()
    {
        QCOMPARE(OverviewViewModel::monogramOf(QStringLiteral("Varga Árpád")), QStringLiteral("VÁ"));
        QCOMPARE(OverviewViewModel::monogramOf(QStringLiteral("Távoli 1")), QStringLiteral("T1"));
        QCOMPARE(OverviewViewModel::monogramOf(QStringLiteral("Kovács Lilla (te)")), QStringLiteral("KL"));
        QCOMPARE(OverviewViewModel::monogramOf(QString()), QStringLiteral("?"));
    }

    // Before the transcript: steps (recording done, transcript not started), an empty participant
    // list, a manual participant gives the list + source "manual", x removes it; the manual description is saved.
    void beforeTranscript_participantsAndSteps()
    {
        const tanara::Meeting m = recording(QStringLiteral("Előtte"));
        OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
        vm.setMeetingId(m.id);
        QVERIFY(!vm.hasTranscript());
        QVERIFY(!vm.transcribing());
        QCOMPARE(vm.peopleState(), QStringLiteral("empty"));
        QCOMPARE(stepOf(vm, QStringLiteral("recording")).value("state").toString(), QStringLiteral("done"));
        QCOMPARE(stepOf(vm, QStringLiteral("recording")).value("sub").toString(), QStringLiteral("1:16:04 · 2 sáv"));
        QCOMPARE(stepOf(vm, QStringLiteral("tracks")).value("sub").toString(),
                 QStringLiteral("1 aktív · 1 kimaradt, nincs rajta beszéd"));
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("state").toString(), QStringLiteral("waiting"));
        QVERIFY(stepOf(vm, QStringLiteral("transcript")).value("parallel").toBool());
        // Without a voice model the voice step is blocked like the transcript step: warning + the Settings page to fix it.
        QCOMPARE(stepOf(vm, QStringLiteral("voice")).value("state").toString(), QStringLiteral("attention"));
        QCOMPARE(stepOf(vm, QStringLiteral("voice")).value("fixTarget").toString(), QStringLiteral("voiceModels"));
        QCOMPARE(stepOf(vm, QStringLiteral("voice")).value("sub").toString(),
                 QStringLiteral("Nincs hangmodell. Töltsd le a Beállításokban (Beszélők)."));
        QCOMPARE(stepOf(vm, QStringLiteral("summary")).value("sub").toString(), QStringLiteral("az átirat után"));
        QVERIFY(!vm.allDone());
        QVERIFY(vm.stats().isEmpty());

        const QString id = vm.addParticipant(QStringLiteral("Fehér Gábor"));
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE(vm.peopleState(), QStringLiteral("list"));
        QCOMPARE(names(vm), QStringList{QStringLiteral("Fehér Gábor")});
        const QVariantMap row = vm.participants().first().toMap();
        QCOMPARE(row.value("monogram").toString(), QStringLiteral("FG"));
        QCOMPARE(row.value("talkShare").toDouble(), -1.0);
        const QVariantList sources = row.value("sources").toList();
        QVERIFY(!sources.isEmpty());
        QCOMPARE(sources.first().toMap().value("text").toString(), QStringLiteral("kézi"));
        // The badge's tooltip (evidence explanation); before the transcript there is no speaker key.
        QVERIFY(!sources.first().toMap().value("detail").toString().isEmpty());
        QVERIFY(row.value("speakerKey").toString().isEmpty());
        QVERIFY(!row.value("anonymous").toBool());
        QCOMPARE(vm.title(), QStringLiteral("Előtte"));
        // Without a voice model there is no "Re-analyze", and core does not start one either.
        QVERIFY(!vm.canReanalyze());
        QVERIFY(!vm.reanalyze());

        // The x is persistent (meeting.json) and can be undone from the shared stack.
        vm.removeParticipant(id);
        QTRY_COMPARE(vm.peopleState(), QStringLiteral("empty"));
        QVERIFY(m_app->participants(m.id).isEmpty());
        {
            OverviewViewModel fresh(facadetest::overviewOf(m_app.get()));
            fresh.setMeetingId(m.id);
            QCOMPARE(fresh.peopleState(), QStringLiteral("empty"));
        }
        tanara::editing::MeetingUndoService* undo = m_app->meetingUndo(m.id);
        QVERIFY(undo);
        QCOMPARE(undo->undoText(), QStringLiteral("Résztvevő eltávolítva: Fehér Gábor"));
        QVERIFY(undo->undo());
        QTRY_COMPARE(names(vm), QStringList{QStringLiteral("Fehér Gábor")});
        vm.removeParticipant(id);
        QTRY_COMPARE(vm.peopleState(), QStringLiteral("empty"));

        vm.setContextNote(QStringLiteral("  Q4 partner review  "));
        QTRY_COMPARE(vm.contextNote(), QStringLiteral("Q4 partner review"));
        QCOMPARE(m_app->store()->load(m.id).contextNote, QStringLiteral("Q4 partner review"));
    }

    // The processing panel's summary step: no transcript -> waiting (disabled start); no language model set up ->
    // blocked (attention, fixTarget "services"); ready -> the start button; running; failed (the message + the
    // technical line, fixable in the Settings); done. The button itself calls the Summary tab's own path.
    void summaryStepStates()
    {
        const tanara::Meeting rec = recording(QStringLiteral("Csak felvétel"));
        {
            OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
            vm.setMeetingId(rec.id);
            const QVariantMap st = stepOf(vm, QStringLiteral("summary"));
            QCOMPARE(st.value("phase").toString(), QStringLiteral("noTranscript"));
            QCOMPARE(st.value("state").toString(), QStringLiteral("waiting"));
            QCOMPARE(st.value("sub").toString(), QStringLiteral("az átirat után"));
            QVERIFY(!st.value("canStart").toBool());
        }

        const tanara::Meeting m = transcribed(QStringLiteral("Átírt"));
        // No language model address: blocked, the Settings services page fixes it.
        tanara::app::AppSettings s = m_app->settings()->settings();
        s.providers.llm.selectedId = QStringLiteral("openai-compat");
        s.providers.llm.configs[s.providers.llm.selectedId].baseUrl = QString();
        s.providers.llm.configs[s.providers.llm.selectedId].model = QString();
        m_app->settings()->setSettings(s);
        OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
        vm.setMeetingId(m.id);
        QVariantMap st = stepOf(vm, QStringLiteral("summary"));
        QCOMPARE(st.value("phase").toString(), QStringLiteral("blocked"));
        QCOMPARE(st.value("state").toString(), QStringLiteral("attention"));
        QCOMPARE(st.value("fixTarget").toString(), QStringLiteral("services"));
        QVERIFY(!st.value("canStart").toBool());
        QVERIFY(!st.value("note").toString().isEmpty());

        // Set up: the start button.
        s.providers.llm.configs[s.providers.llm.selectedId].baseUrl = QStringLiteral("http://127.0.0.1:9/v1");
        s.providers.llm.configs[s.providers.llm.selectedId].model = QStringLiteral("teszt-modell");
        m_app->settings()->setSettings(s);
        QTRY_COMPARE(stepOf(vm, QStringLiteral("summary")).value("phase").toString(), QStringLiteral("ready"));
        st = stepOf(vm, QStringLiteral("summary"));
        QCOMPARE(st.value("state").toString(), QStringLiteral("waiting"));
        QCOMPARE(st.value("sub").toString(), QStringLiteral("még nem készült"));
        QVERIFY(st.value("canStart").toBool());
        QCOMPARE(st.value("fixTarget").toString(), QString());

        // Running: progress + the cancel button.
        m_app->jobs()->begin(m.id, tanara::JobKind::Summarize, QStringLiteral("Összefoglaló"));
        QTRY_COMPARE(stepOf(vm, QStringLiteral("summary")).value("phase").toString(), QStringLiteral("running"));
        QCOMPARE(stepOf(vm, QStringLiteral("summary")).value("state").toString(), QStringLiteral("running"));
        QVERIFY(stepOf(vm, QStringLiteral("summary")).value("progress").toInt() != -1);

        // Failed (the local server is down): attention, the message, the technical line, retry + Settings.
        tanara::JobError je;
        je.message = QStringLiteral("A helyi LLM nem érhető el: http://localhost:1234 — fut az LM Studio?");
        je.detail = QStringLiteral("kapcsolat · openai-compat · http://localhost:1234/v1 · Connection refused");
        je.fixActionHint = QStringLiteral("settings:llm-unreachable");
        m_app->jobs()->fail(m.id, tanara::JobKind::Summarize, je);
        QTRY_COMPARE(stepOf(vm, QStringLiteral("summary")).value("phase").toString(), QStringLiteral("failed"));
        st = stepOf(vm, QStringLiteral("summary"));
        QCOMPARE(st.value("state").toString(), QStringLiteral("attention"));
        QCOMPARE(st.value("sub").toString(), QStringLiteral("nem sikerült"));
        QCOMPARE(st.value("note").toString(), je.message);
        QCOMPARE(st.value("detail").toString(), je.detail);
        QCOMPARE(st.value("fixTarget").toString(), QStringLiteral("services"));
        QVERIFY(st.value("canStart").toBool());

        // Cleared ("Rendben" on the Summary tab): back to ready.
        m_app->jobs()->clearError(m.id, tanara::JobKind::Summarize);
        QTRY_COMPARE(stepOf(vm, QStringLiteral("summary")).value("phase").toString(), QStringLiteral("ready"));

        // Done: "Újra" + the Summary tab link.
        tanara::Meeting done = m_app->store()->load(m.id);
        done.hasSummary = true;
        m_app->store()->saveMeeting(done);
        QTRY_COMPARE(stepOf(vm, QStringLiteral("summary")).value("phase").toString(), QStringLiteral("done"));
        st = stepOf(vm, QStringLiteral("summary"));
        QCOMPARE(st.value("state").toString(), QStringLiteral("done"));
        QCOMPARE(st.value("sub").toString(), QStringLiteral("kész"));
        QVERIFY(st.value("canStart").toBool());
    }

    // S5: the split button's rows per stored route (Local / External / Cloud while not live / Ask), the main
    // part on Ask opens the menu instead of starting, a disabled route is refused with its reason, the Cloud
    // row waits for its estimate, "Ez legyen az alapértelmezett" writes the route.
    void routeOptions_perStoredRoute()
    {
        RouteFakeFacade facade;
        OverviewViewModel vm(&facade);
        vm.setMeetingId(QStringLiteral("m1"));
        QVariantMap st = stepOf(vm, QStringLiteral("transcript"));
        QCOMPARE(st.value("route").toString(), QStringLiteral("local"));
        QCOMPARE(st.value("routeOptions").toList().size(), 3);
        QVariantMap local = optionOf(st, "local");
        QCOMPARE(local.value("label").toString(), QStringLiteral("Helyben · Whisper"));
        QCOMPARE(local.value("detail").toString(), QStringLiteral("ingyenes · kb. 2 perc · a gépeden"));
        QVERIFY(local.value("enabled").toBool());
        QVERIFY(local.value("isDefault").toBool());
        const QVariantMap external = optionOf(st, "external");
        QCOMPARE(external.value("label").toString(), QStringLiteral("Külső · Soniox · saját kulcs"));
        QVERIFY(!external.value("isDefault").toBool());
        QVariantMap cloud = optionOf(st, "cloud");
        QCOMPARE(cloud.value("label").toString(), QStringLiteral("Tanara Cloud · hamarosan"));
        QVERIFY(!cloud.value("enabled").toBool());
        QVERIFY(cloud.value("isCloud").toBool());
        QCOMPARE(cloud.value("reason").toString(), QStringLiteral("A Tanara Cloud még nem érhető el."));
        // The summary has no external route here.
        QCOMPARE(stepOf(vm, QStringLiteral("summary")).value("routeOptions").toList().size(), 2);

        QSignalSpy menu(&vm, &OverviewViewModel::menuRequested);
        QSignalSpy starts(&vm, &OverviewViewModel::startRequested);
        QSignalSpy rejected(&vm, &OverviewViewModel::routeRejected);
        // Local: the main part starts the default path.
        QVERIFY(vm.startDefault(QStringLiteral("transcript")));
        QCOMPARE(starts.count(), 1);
        QCOMPARE(starts.at(0).at(1).toString(), QString());
        // A non-live Cloud is refused with the reason.
        QVERIFY(!vm.start(QStringLiteral("transcript"), QStringLiteral("cloud")));
        QCOMPARE(rejected.count(), 1);
        QCOMPARE(rejected.at(0).at(1).toString(), QStringLiteral("A Tanara Cloud még nem érhető el."));
        QCOMPARE(starts.count(), 1);
        // A menu row: the explicit route.
        QVERIFY(vm.start(QStringLiteral("transcript"), QStringLiteral("external")));
        QCOMPARE(starts.count(), 2);
        QCOMPARE(starts.at(1).at(1).toString(), QStringLiteral("external"));
        QVERIFY(facade.calls.isEmpty());   // no estimate while the cloud is not live

        // Stored External: the default mark moves.
        facade.stored = tanara::providers::Route::External;
        vm.refresh();
        st = stepOf(vm, QStringLiteral("transcript"));
        QCOMPARE(st.value("route").toString(), QStringLiteral("external"));
        QVERIFY(optionOf(st, "external").value("isDefault").toBool());
        QVERIFY(!optionOf(st, "local").value("isDefault").toBool());

        // Stored Cloud while the cloud is not live: the main part runs the default (the core falls back).
        facade.stored = tanara::providers::Route::Cloud;
        vm.refresh();
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("route").toString(), QStringLiteral("cloud"));
        QVERIFY(vm.startDefault(QStringLiteral("transcript")));
        QCOMPARE(menu.count(), 0);

        // Ask (the cloud is live): the main part opens the menu, nothing starts; the Cloud row waits for its
        // estimate ("…", not choosable), then shows the price.
        facade.stored = tanara::providers::Route::Ask;
        facade.cloudLive = true;
        vm.refresh();
        st = stepOf(vm, QStringLiteral("transcript"));
        QCOMPARE(st.value("route").toString(), QStringLiteral("ask"));
        const int startsBefore = starts.count();
        QVERIFY(!vm.startDefault(QStringLiteral("transcript")));
        QCOMPARE(menu.count(), 1);
        QCOMPARE(menu.at(0).at(0).toString(), QStringLiteral("transcript"));
        QCOMPARE(starts.count(), startsBefore);
        QCOMPARE(facade.calls, QStringList{QStringLiteral("estimate:stt:m1")});
        cloud = optionOf(stepOf(vm, QStringLiteral("transcript")), "cloud");
        QCOMPARE(cloud.value("label").toString(), QStringLiteral("Tanara Cloud · Pontos"));
        QCOMPARE(cloud.value("detail").toString(), QStringLiteral("…"));
        QVERIFY(!cloud.value("enabled").toBool());
        QVERIFY(!vm.start(QStringLiteral("transcript"), QStringLiteral("cloud")));
        vm.prepareRoutes(QStringLiteral("transcript"));   // asked once
        QCOMPARE(facade.calls.size(), 1);
        tanara::library::CloudRunEstimate est;
        est.ok = true;
        est.cost = QStringLiteral("0,12 €");
        est.balance = QStringLiteral("8,40 €");
        est.level = QStringLiteral("enough");
        est.etaSeconds = 60;
        QSignalSpy changed(&vm, &OverviewViewModel::changed);
        facade.pendingEstimate(est);
        QCOMPARE(changed.count(), 1);
        cloud = optionOf(stepOf(vm, QStringLiteral("transcript")), "cloud");
        QVERIFY(cloud.value("enabled").toBool());
        QCOMPARE(cloud.value("detail").toString(), QStringLiteral("kb. 0,12 € · kb. 1 perc · a keretből"));
        QVERIFY(vm.start(QStringLiteral("transcript"), QStringLiteral("cloud")));
        QCOMPARE(starts.last().at(1).toString(), QStringLiteral("cloud"));

        // Not enough balance: the row is refused with the level's text.
        est.level = QStringLiteral("notEnough");
        vm.setMeetingId(QStringLiteral("m2"));   // a new meeting asks again
        vm.prepareRoutes(QStringLiteral("transcript"));
        facade.pendingEstimate(est);
        cloud = optionOf(stepOf(vm, QStringLiteral("transcript")), "cloud");
        QVERIFY(!cloud.value("enabled").toBool());
        QCOMPARE(cloud.value("reason").toString(), QStringLiteral("Nincs elég keret: kb. 0,12 € kell, a keret 8,40 €."));
        // Offline: "no internet; the local route works".
        tanara::library::CloudRunEstimate offline;
        offline.offline = true;
        vm.setMeetingId(QStringLiteral("m3"));
        vm.prepareRoutes(QStringLiteral("transcript"));
        facade.pendingEstimate(offline);
        QCOMPARE(optionOf(stepOf(vm, QStringLiteral("transcript")), "cloud").value("reason").toString(),
                 QStringLiteral("Nincs internet; a helyi út működik."));

        // "Ez legyen az alapértelmezett".
        QVERIFY(vm.setDefaultRoute(QStringLiteral("stt"), QStringLiteral("local")));
        QCOMPARE(facade.calls.last(), QStringLiteral("default:stt:local"));
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("route").toString(), QStringLiteral("local"));
        QVERIFY(vm.setDefaultRoute(QStringLiteral("summary"), QStringLiteral("cloud")));
        QCOMPARE(facade.calls.last(), QStringLiteral("default:llm:cloud"));
        QVERIFY(!vm.setDefaultRoute(QStringLiteral("stt"), QStringLiteral("nincs-ilyen")));
    }

    // S5 with the real facade (cloud off): the transcription's rows come from the registry and the settings;
    // Soniox without a key is not runnable (its reason), Tanara Cloud is "coming soon".
    void routeOptions_realFacade()
    {
        const tanara::Meeting m = recording(QStringLiteral("Útvonalak"));
        OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
        vm.setMeetingId(m.id);
        const QVariantMap st = stepOf(vm, QStringLiteral("transcript"));
        QVERIFY(!st.value("route").toString().isEmpty());
        // No local transcriber was ever set up: the row names Whisper but cannot run.
        const QVariantMap local = optionOf(st, "local");
        QCOMPARE(local.value("label").toString(), QStringLiteral("Helyben · Whisper"));
        QVERIFY(!local.value("enabled").toBool());
        QCOMPARE(local.value("reason").toString(), QStringLiteral("Még nincs beállítva a Beállításokban."));
        const QVariantMap external = optionOf(st, "external");
        QCOMPARE(external.value("label").toString(), QStringLiteral("Külső · Soniox · saját kulcs"));
        QVERIFY(!external.value("enabled").toBool());
        QVERIFY(external.value("reason").toString().contains(QStringLiteral("Soniox")));
        const QVariantMap cloud = optionOf(st, "cloud");
        QCOMPARE(cloud.value("label").toString(), QStringLiteral("Tanara Cloud · hamarosan"));
        QVERIFY(!cloud.value("enabled").toBool());
        QSignalSpy rejected(&vm, &OverviewViewModel::routeRejected);
        QVERIFY(!vm.start(QStringLiteral("transcript"), QStringLiteral("cloud")));
        QCOMPARE(rejected.count(), 1);

        // "Ez legyen az alapértelmezett" writes the routes setting (Local here).
        QVERIFY(vm.setDefaultRoute(QStringLiteral("stt"), QStringLiteral("local")));
        QCOMPARE(m_app->settings()->settings().routes.route(tanara::providers::Capability::Stt),
                 tanara::providers::Route::Local);
        QVERIFY(!vm.setDefaultRoute(QStringLiteral("stt"), QStringLiteral("cloud")));   // not live: refused
    }

    // "Only I spoke": does not work without an own name; with a configured name the decision is saved.
    void soloMeeting()
    {
        const tanara::Meeting m = recording(QStringLiteral("Egyedül"));
        OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
        vm.setMeetingId(m.id);
        tanara::app::AppSettings s = m_app->settings()->settings();
        s.recording.userSpeakerName = QString();
        m_app->settings()->setSettings(s);
        QVERIFY(!vm.setSolo());
        s.recording.userSpeakerName = QStringLiteral("Kovács Lilla");
        m_app->settings()->setSettings(s);
        QVERIFY(vm.setSolo());
        const tanara::Meeting after = m_app->store()->load(m.id);
        QVERIFY(after.approval.has_value());
        QVERIFY(after.approval->solo);
        QTRY_COMPARE(vm.peopleHint(), QStringLiteral("csak én beszéltem"));
        QCOMPARE(stepOf(vm, QStringLiteral("voice")).value("sub").toString(), QStringLiteral("csak én beszéltem"));
    }

    // The row carries the voice sample to listen to (sampleTrackId / sampleStartMs / sampleEndMs, meeting
    // time): before the transcript the participant's cluster sample (parsed from sampleRef), a manual
    // participant has none; after the transcript the speaker's longest line on the cluster's track.
    void rowsCarryTheVoiceSample()
    {
        const tanara::Meeting m = recording(QStringLiteral("Minta"));
        tanara::Participant voice;
        voice.id = QStringLiteral("p-voice");
        voice.source = tanara::ParticipantSource::Voice;
        voice.sides = {QStringLiteral("mic")};
        tanara::Participant manual;
        manual.id = QStringLiteral("p-manual");
        manual.source = tanara::ParticipantSource::Manual;
        tanara::Meeting saved = m_app->store()->load(m.id);
        saved.participants = {voice, manual};
        m_app->store()->saveMeeting(saved);
        tanara::processing::ParticipantAnalysisFile file;
        file.at = QStringLiteral("2026-10-14T10:00:00Z");
        tanara::VoiceCluster small, big, other;
        small.trackId = QStringLiteral("system");
        small.participantId = QStringLiteral("p-voice");
        small.windows = {{1000, 4000}};
        small.sampleRef = QStringLiteral("track_system.wav#1000-4000");
        big.trackId = QStringLiteral("mic");
        big.participantId = QStringLiteral("p-voice");
        big.windows = {{10000, 16000}, {20000, 26000}};
        big.sampleRef = QStringLiteral("track_mic.wav#12500-18500");
        other.trackId = QStringLiteral("mic");
        other.participantId = QStringLiteral("p-else");
        other.windows = {{0, 9000}};
        other.sampleRef = QStringLiteral("track_mic.wav#0-6000");
        file.clusters = {small, big, other};
        QVERIFY(file.save(m.folder));

        OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
        vm.setMeetingId(m.id);
        QCOMPARE(vm.participants().size(), 2);
        const QVariantMap v = vm.participants().at(0).toMap();
        QCOMPARE(v.value("id").toString(), QStringLiteral("p-voice"));
        QCOMPARE(v.value("sampleTrackId").toString(), QStringLiteral("mic"));   // the larger cluster
        QCOMPARE(v.value("sampleStartMs").toLongLong(), 12500);
        QCOMPARE(v.value("sampleEndMs").toLongLong(), 18500);
        const QVariantMap mn = vm.participants().at(1).toMap();
        QCOMPARE(mn.value("sampleTrackId").toString(), QString());
        QCOMPARE(mn.value("sampleStartMs").toLongLong(), -1);
        QCOMPARE(mn.value("sampleEndMs").toLongLong(), -1);
    }

    void rowsCarryTheLongestLineAfterTheTranscript()
    {
        tanara::Meeting m = transcribed(QStringLiteral("Hosszú sor"));
        QFile seg(QDir(m.folder).filePath(QStringLiteral("transcript.segments.json")));
        QVERIFY(seg.open(QIODevice::WriteOnly | QIODevice::Truncate));
        seg.write(QJsonDocument(QJsonArray{
            QJsonObject{{"startMs", 0}, {"endMs", 900}, {"speaker", "Beszélő 1"}, {"text", "Rövid."}},
            QJsonObject{{"startMs", 5000}, {"endMs", 8000}, {"speaker", "Beszélő 1"}, {"text", "Ez a hosszabb."}},
            QJsonObject{{"startMs", 30000}, {"endMs", 60000}, {"speaker", "Beszélő 2"}, {"text", "Nagyon hosszú."}}}).toJson());
        seg.close();
        tanara::Participant p;
        p.id = QStringLiteral("p-one");
        p.source = tanara::ParticipantSource::Voice;
        p.rawSpeakerIds = {QStringLiteral("Beszélő 1")};
        m = m_app->store()->load(m.id);
        m.participants = {p};
        m_app->store()->saveMeeting(m);
        tanara::processing::ParticipantAnalysisFile file;
        file.at = QStringLiteral("2026-10-14T10:00:00Z");
        tanara::VoiceCluster mic, sys;
        mic.trackId = QStringLiteral("mic");
        mic.participantId = QStringLiteral("p-one");
        mic.windows = {{0, 900}};
        mic.sampleRef = QStringLiteral("track_mic.wav#0-900");
        sys.trackId = QStringLiteral("system");
        sys.participantId = QStringLiteral("p-one");
        sys.windows = {{4500, 8500}};
        sys.sampleRef = QStringLiteral("track_system.wav#4500-8500");
        file.clusters = {mic, sys};
        QVERIFY(file.save(m.folder));

        OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
        vm.setMeetingId(m.id);
        QVERIFY(vm.hasTranscript());
        bool found = false;
        for (const QVariant& v : vm.participants()) {
            const QVariantMap row = v.toMap();
            if (row.value("id").toString() != QLatin1String("p-one")) continue;
            found = true;
            QCOMPARE(row.value("sampleTrackId").toString(), QStringLiteral("system"));   // overlaps the line most
            QCOMPARE(row.value("sampleStartMs").toLongLong(), 5000);
            QCOMPARE(row.value("sampleEndMs").toLongLong(), 8000);
        }
        QVERIFY(found);
        // A speaker with no participant record (no cluster): nothing to play.
        for (const QVariant& v : vm.participants())
            if (v.toMap().value("id").toString() != QLatin1String("p-one"))
                QCOMPARE(v.toMap().value("sampleTrackId").toString(), QString());
    }

    // After the transcript, with only anonymous speakers: "nobody" + the raw speakers with their share; DATA tiles.
    void afterTranscript_nobodyAndStats()
    {
        const tanara::Meeting m = transcribed(QStringLiteral("Utána"));
        OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
        vm.setMeetingId(m.id);
        QVERIFY(vm.hasTranscript());
        QCOMPARE(vm.peopleState(), QStringLiteral("nobody"));
        // The rows by speaker key, as anonymous (nameable).
        QCOMPARE(vm.participants().size(), 2);
        for (const QVariant& v : vm.participants()) {
            QVERIFY(!v.toMap().value("speakerKey").toString().isEmpty());
            QVERIFY(v.toMap().value("anonymous").toBool());
        }
        const QVariantMap first = vm.participants().first().toMap();
        QCOMPARE(first.value("name").toString(), QStringLiteral("Beszélő 1"));
        QVERIFY(first.value("talkShare").toDouble() > 0.6);
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("state").toString(), QStringLiteral("done"));
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("sub").toString(),
                 QStringLiteral("3 megszólalás · 2 beszélő"));
        QCOMPARE(vm.stats().size(), 6);
        QCOMPARE(vm.stats().at(0).toMap().value("value").toString(), QStringLiteral("1:16:04"));
        QCOMPARE(vm.stats().at(2).toMap().value("value").toString(), QStringLiteral("2"));
        QCOMPARE(vm.stats().at(2).toMap().value("sub").toString(), QStringLiteral("2 névtelen"));
        QCOMPARE(vm.stats().at(3).toMap().value("value").toString(), QStringLiteral("3"));
        QCOMPARE(vm.stats().at(5).toMap().value("value").toString(), QStringLiteral("nincs"));
    }

    // D5: the transcript step says where the speakers come from; the real facade reads it from the transcript, the
    // selected provider and the models.
    void speakersLine_followsTheTranscriptAndTheProvider()
    {
        auto writeTokens = [](const tanara::Meeting& m, const QStringList& speakers, const QString& source) {
            tanara::MergedTranscript t;
            qint64 at = 0;
            for (const QString& sp : speakers) {
                tanara::TranscriptToken tok;
                tok.text = QStringLiteral(" szó");
                tok.startMs = at;
                tok.endMs = at + 300;
                tok.speaker = sp;
                t.tokens << tok;
                at += 400;
            }
            t.speakerSource = source;
            QVERIFY(tanara::library::TranscriptRepository::writeTokens(m.folder, t));
        };
        auto useWhisper = [this]() {
            tanara::app::AppSettings s = m_app->settings()->settings();
            s.providers.stt.selectedId = QStringLiteral("whisper-compat");
            m_app->settings()->setSettings(s);
        };
        const QString provider = QStringLiteral("Beszélők: a szolgáltatótól");

        // Soniox (the default) separates the speakers itself.
        const tanara::Meeting a = transcribed(QStringLiteral("Szolgáltatótól"));
        {
            OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
            vm.setMeetingId(a.id);
            QCOMPARE(vm.speakers().value("source").toString(), QStringLiteral("provider"));
            QCOMPARE(vm.speakers().value("line").toString(), provider);
            const QVariantMap step = stepOf(vm, QStringLiteral("transcript"));
            QCOMPARE(step.value("state").toString(), QStringLiteral("done"));
            QCOMPARE(step.value("note").toString(), provider);
            QVERIFY(step.value("fixTarget").toString().isEmpty());
            QVERIFY(!vm.speakers().value("canDiarize").toBool());   // no usable model in the sandbox
        }

        // Whisper: no speakers from the provider, no models in the sandbox -> the model link + attention.
        useWhisper();
        const tanara::Meeting b = transcribed(QStringLiteral("Whisper, modell nélkül"));
        writeTokens(b, {QStringLiteral("Beszélő 1"), QStringLiteral("Beszélő 1"), QStringLiteral("Beszélő 1")}, QString());
        {
            OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
            vm.setMeetingId(b.id);
            QCOMPARE(vm.speakers().value("source").toString(), QStringLiteral("none"));
            QVERIFY(vm.speakers().value("line").toString().contains(QStringLiteral("tölts le modellt")));
            const QVariantMap step = stepOf(vm, QStringLiteral("transcript"));
            QCOMPARE(step.value("state").toString(), QStringLiteral("attention"));
            QCOMPARE(step.value("fixTarget").toString(), QStringLiteral("diarization"));
            QVERIFY(!vm.allDone());
            QVERIFY(!vm.speakers().value("canDiarize").toBool());
            // The manual action is refused without a model (nothing starts).
            QVERIFY(!vm.diarizeSpeakers(2));
        }

        // The local diarization's result (speakerSource) says so, even with the provider that cannot.
        const tanara::Meeting c = transcribed(QStringLiteral("Helyi"));
        writeTokens(c, {QStringLiteral("Beszélő 1"), QStringLiteral("Beszélő 2"), QStringLiteral("Beszélő 1")},
                    QStringLiteral("local-diarization"));
        {
            OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
            vm.setMeetingId(c.id);
            QCOMPARE(vm.speakers().value("source").toString(), QStringLiteral("local"));
            QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("note").toString(), QStringLiteral("Beszélők: helyi elkülönítés"));
            QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("state").toString(), QStringLiteral("done"));
        }

        // Several speakers without a source: the provider separated them (an earlier Soniox transcript).
        const tanara::Meeting d = transcribed(QStringLiteral("Korábbi Soniox"));
        writeTokens(d, {QStringLiteral("Beszélő 1"), QStringLiteral("Beszélő 2")}, QString());
        {
            OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
            vm.setMeetingId(d.id);
            QCOMPARE(vm.speakers().value("source").toString(), QStringLiteral("provider"));
        }
    }

    // After the transcript, a participant not bound to a speaker (added by hand, never spoke)
    // shows dimmed after the speakers, with a "not heard" badge; it can be deleted permanently
    // and undone.
    void afterTranscript_unheardParticipant()
    {
        const tanara::Meeting m = transcribed(QStringLiteral("Nem hallott"));
        OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
        vm.setMeetingId(m.id);
        QCOMPARE(vm.peopleState(), QStringLiteral("nobody"));
        const QString id = vm.addParticipant(QStringLiteral("Lantos Réka"));
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE(vm.participants().size(), 3);
        QCOMPARE(vm.peopleState(), QStringLiteral("list"));
        const QVariantMap row = vm.participants().last().toMap();
        QCOMPARE(row.value("id").toString(), id);
        QCOMPARE(row.value("name").toString(), QStringLiteral("Lantos Réka"));
        QVERIFY(row.value("unheard").toBool());
        QVERIFY(row.value("dimmed").toBool());
        QVERIFY(row.value("removable").toBool());
        QVERIFY(row.value("speakerKey").toString().isEmpty());
        const QVariantMap badge = row.value("sources").toList().first().toMap();
        QCOMPARE(badge.value("text").toString(), QStringLiteral("nem hallottuk"));
        QVERIFY(badge.value("negative").toBool());
        // The speakers' rows come first, not dimmed.
        QVERIFY(!vm.participants().first().toMap().value("unheard").toBool());

        vm.removeParticipant(id);
        QTRY_COMPARE(vm.participants().size(), 2);
        QCOMPARE(vm.peopleState(), QStringLiteral("nobody"));
        QVERIFY(m_app->participants(m.id).isEmpty());
        QVERIFY(m_app->meetingUndo(m.id)->undo());
        QTRY_COMPARE(vm.participants().size(), 3);
    }

    // Tag-based person suggestion: at most 3; the x is a persistent rejection for this meeting
    // (TagService::rejectPersonForMeeting), undoable from the shared stack.
    void tagSuggestions_limitAndDurableReject()
    {
        tanara::library::TagService* tags = m_app->tags();
        const tanara::Tag nord = tags->create(QStringLiteral("Nordvik"));
        const QStringList crew{QStringLiteral("Fehér Gábor"), QStringLiteral("Varga Árpád"),
                               QStringLiteral("Molnár Eszter"), QStringLiteral("Szabó Bence")};
        for (int i = 1; i <= 3; ++i) {
            tanara::Meeting t = m_app->store()->createMeeting(QStringLiteral("Nordvik %1").arg(i));
            t.startedAt = QDateTime(QDate(2026, 9, i), QTime(10, 0));
            for (int k = 0; k < crew.size(); ++k)
                t.speakerMap.insert(QStringLiteral("Beszélő %1").arg(k + 1), m_app->people()->ensure(crew.at(k)));
            t.tagIds = {nord.id};
            m_app->store()->saveMeeting(t);
        }
        tanara::Meeting m = recording(QStringLiteral("Új Nordvik"));
        m.tagIds = {nord.id};
        m_app->store()->saveMeeting(m);
        tags->refreshPersonStatsNow();

        OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
        vm.setMeetingId(m.id);
        auto suggested = [&vm] {
            QStringList out;
            for (const QVariant& v : vm.participants())
                if (v.toMap().value("suggested").toBool()) out << v.toMap().value("name").toString();
            return out;
        };
        QCOMPARE(suggested().size(), 3);
        QCOMPARE(vm.peopleState(), QStringLiteral("list"));
        const QString rejected = suggested().first();
        const tanara::PersonId rejectedId = m_app->people()->idForName(rejected);
        QVERIFY(!rejectedId.isNull());
        vm.removeParticipant(QStringLiteral("suggest:") + rejectedId.toString());
        QTRY_VERIFY(!suggested().contains(rejected));
        QCOMPARE(suggested().size(), 3);   // the fourth takes its place
        QVERIFY(tags->isPersonRejectedForMeeting(rejectedId, m.id));
        {
            OverviewViewModel fresh(facadetest::overviewOf(m_app.get()));
            fresh.setMeetingId(m.id);
            for (const QVariant& v : fresh.participants())
                QVERIFY(v.toMap().value("name").toString() != rejected);
        }
        QVERIFY(m_app->meetingUndo(m.id)->undo());
        QVERIFY(!tags->isPersonRejectedForMeeting(rejectedId, m.id));
        vm.refresh();
        QVERIFY(suggested().contains(rejected));

        // "Add": becomes a participant and drops out of the suggestions.
        QVERIFY(!vm.addParticipant(rejected).isEmpty());
        QTRY_VERIFY(!suggested().contains(rejected));
        QVERIFY(names(vm).contains(rejected));
    }

    // The tracks' speech share, the reason for exclusion and "Include" (TrackListModel -> AppController).
    void tracks_speechAndInclude()
    {
        const tanara::Meeting m = recording(QStringLiteral("Sávok"));
        TrackListModel tracks;
        tracks.setFacade(AppContext::instance()->servicesFor(m_app.get()).tracks(QString(), nullptr));
        tracks.setMeetingId(m.id);
        QCOMPARE(tracks.count(), 2);
        QCOMPARE(tracks.includedCount(), 1);
        QCOMPARE(tracks.excludedCount(), 1);
        int sys = -1;
        for (int i = 0; i < tracks.count(); ++i)
            if (tracks.trackIdAt(i) == QLatin1String("system")) sys = i;
        QVERIFY(sys >= 0);
        const QModelIndex idx = tracks.index(sys);
        QCOMPARE(tracks.data(idx, TrackListModel::ExcludedReasonRole).toString(), QStringLiteral("noSpeech"));
        QCOMPARE(tracks.data(idx, TrackListModel::SpeechRatioRole).toDouble(), 0.0);
        QCOMPARE(tracks.data(idx, TrackListModel::KindRole).toString(), QStringLiteral("loopback"));
        QVERIFY(!tracks.data(idx, TrackListModel::IncludedRole).toBool());

        tracks.setIncluded(sys, true);
        QTRY_COMPARE(tracks.includedCount(), 2);
        const tanara::Meeting after = m_app->store()->load(m.id);
        for (const tanara::Track& t : after.tracks)
            QVERIFY2(t.included(), qPrintable(t.id));
        tracks.setIncluded(0, false);
        QTRY_COMPARE(tracks.includedCount(), 1);
        QCOMPARE(m_app->store()->load(m.id).tracks.at(0).excludedReason, QStringLiteral("manual"));
    }

    // The tab state for the shell: whether there is a summary, whether a transcription is running.
    void shellMeetingTabState()
    {
        const tanara::Meeting m = transcribed(QStringLiteral("Fülek"));
        ShellMeetingModel model(facadetest::shellOf(m_app.get()));
        model.setMeetingId(m.id);
        QVERIFY(model.hasTranscript());
        QVERIFY(!model.hasSummary());
        QCOMPARE(model.transcribePercent(), -1);
        QCOMPARE(model.summarizePercent(), -1);
    }

    static QVariantMap rowOf(const OverviewViewModel& vm, const QString& id)
    {
        for (const QVariant& v : vm.participants())
            if (v.toMap().value(QStringLiteral("id")).toString() == id) return v.toMap();
        return {};
    }
    static tanara::Evidence evidence(tanara::EvidenceKind kind, tanara::Polarity polarity)
    {
        tanara::Evidence e;
        e.kind = kind;
        e.polarity = polarity;
        e.value = 0.8;
        return e;
    }

    // The "Who was there?" card's keys on real data: the decision ("pending" while there are voice
    // candidates and no decision), "Approve · N" (= what ParticipantsViewModel approves), doubtful /
    // counted / voiceless rows, and the row operations through the view model (confirm, "This is me"
    // with its reason, naming merging the manual voiceless row, "same voice" merge).
    void card_decisionKeysAndRowOperations()
    {
        using namespace tanara;
        Meeting m = recording(QStringLiteral("Kártya"));
        Participant sure;
        sure.id = QStringLiteral("p-sure");
        sure.personId = m_app->people()->ensure(QStringLiteral("Fehér Gábor"));
        sure.sides = {QStringLiteral("loopback")};
        sure.evidence = {evidence(EvidenceKind::Voice, Polarity::Support), evidence(EvidenceKind::Tag, Polarity::Support)};
        Participant doubt;
        doubt.id = QStringLiteral("p-doubt");
        doubt.personId = m_app->people()->ensure(QStringLiteral("Varga Árpád"));
        doubt.sides = {QStringLiteral("loopback")};
        doubt.evidence = {evidence(EvidenceKind::Voice, Polarity::Support), evidence(EvidenceKind::Tag, Polarity::Contradict)};
        Participant anon;
        anon.id = QStringLiteral("p-anon");
        anon.sides = {QStringLiteral("mic")};
        Participant manual;
        manual.id = QStringLiteral("p-man");
        manual.personId = m_app->people()->ensure(QStringLiteral("Molnár Eszter"));
        manual.source = ParticipantSource::Manual;
        Meeting saved = m_app->store()->load(m.id);
        saved.participants = {sure, doubt, anon, manual};
        m_app->store()->saveMeeting(saved);
        app::AppSettings settings = m_app->settings()->settings();
        settings.recording.userSpeakerName = QString();
        m_app->settings()->setSettings(settings);

        OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
        vm.setMeetingId(m.id);
        QCOMPARE(vm.decision(), QStringLiteral("pending"));
        QVERIFY(vm.approvalPending());
        QVERIFY(rowOf(vm, "p-sure").value("counted").toBool());
        QVERIFY(!rowOf(vm, "p-sure").value("doubtful").toBool());
        QVERIFY(!rowOf(vm, "p-sure").value("voiceless").toBool());
        QVERIFY(!rowOf(vm, "p-doubt").value("counted").toBool());
        QVERIFY(rowOf(vm, "p-doubt").value("doubtful").toBool());
        QVERIFY(!rowOf(vm, "p-anon").value("counted").toBool());
        QVERIFY(!rowOf(vm, "p-anon").value("doubtful").toBool());
        QVERIFY(rowOf(vm, "p-anon").value("anonymous").toBool());
        QVERIFY(rowOf(vm, "p-man").value("voiceless").toBool());
        QVERIFY(rowOf(vm, "p-man").value("counted").toBool());
        QCOMPARE(vm.approveCount(), 2);

        // "Was there" on the doubtful row: counted, one undo step; an anonymous row cannot be confirmed.
        QVERIFY(vm.confirmParticipant(QStringLiteral("p-doubt"), true));
        QTRY_COMPARE(vm.approveCount(), 3);
        QVERIFY(rowOf(vm, "p-doubt").value("approved").toBool());
        QVERIFY(!vm.confirmParticipant(QStringLiteral("p-anon"), true));
        QVERIFY(m_app->meetingUndo(m.id)->undo());
        QTRY_COMPARE(vm.approveCount(), 2);
        QVERIFY(vm.confirmParticipant(QStringLiteral("p-doubt"), true));
        QTRY_COMPARE(vm.approveCount(), 3);

        // "This is me" without an own name: fails with a reason for the panel.
        const QVariantMap self = vm.setSelfParticipant(QStringLiteral("p-anon"));
        QVERIFY(!self.value("ok").toBool());
        QVERIFY(!self.value("reason").toString().isEmpty());
        QVERIFY(vm.selfName().isEmpty());

        // Naming the unknown voice after the person's manual voiceless row: the two rows become one.
        QVERIFY(vm.nameParticipant(QStringLiteral("p-anon"), QStringLiteral("Molnár Eszter")));
        QTRY_VERIFY(rowOf(vm, "p-man").isEmpty());
        QCOMPARE(rowOf(vm, "p-anon").value("name").toString(), QStringLiteral("Molnár Eszter"));
        QVERIFY(!rowOf(vm, "p-anon").value("voiceless").toBool());
        QVERIFY(!rowOf(vm, "p-anon").value("anonymous").toBool());

        // "Same voice as ...": the doubtful row joins the sure one (meeting-local).
        QVERIFY(vm.mergeParticipantInto(QStringLiteral("p-doubt"), QStringLiteral("p-sure")));
        QTRY_VERIFY(rowOf(vm, "p-doubt").isEmpty());
        QVERIFY(!rowOf(vm, "p-sure").isEmpty());

        // "Approve · N" is what the participants view model approves (the card's footer).
        ParticipantsViewModel decision(AppContext::instance()->servicesFor(m_app.get()));
        decision.setMeetingId(m.id);
        decision.reload();
        QCOMPARE(decision.checkedCount(), vm.approveCount());
        const int n = vm.approveCount();
        QVERIFY(decision.approve());
        QTRY_COMPARE(vm.decision(), QStringLiteral("approved"));
        QCOMPARE(vm.approveCount(), n);
        QVERIFY(!vm.approvalPending());
        // A demo-only decision does nothing on real data.
        QVERIFY(!vm.decideInDemo(QStringLiteral("skipped")));

        // With an own name: "This is me" works and the panel knows the name; "Only I spoke" -> "solo".
        m_app->setUserSpeakerName(QStringLiteral("Kovács Lilla"));
        const tanara::Meeting other = recording(QStringLiteral("Egyedül a kártyán"));
        Meeting otherSaved = m_app->store()->load(other.id);
        Participant voice;
        voice.id = QStringLiteral("p-voice");
        voice.sides = {QStringLiteral("mic")};
        otherSaved.participants = {voice};
        m_app->store()->saveMeeting(otherSaved);
        OverviewViewModel second(facadetest::overviewOf(m_app.get()));
        second.setMeetingId(other.id);
        QCOMPARE(second.selfName(), QStringLiteral("Kovács Lilla"));
        QVERIFY(second.setSelfParticipant(QStringLiteral("p-voice")).value("ok").toBool());
        QTRY_VERIFY(rowOf(second, "p-voice").value("self").toBool());
        QVERIFY(second.setSolo());
        QTRY_COMPARE(second.decision(), QStringLiteral("solo"));
    }

    // The card's demo states (OverviewDemoFacade): decision, "Approve · N", and the row operations in
    // memory (naming, "Was there", "same voice", "This is me", the decisions).
    void card_demoStatesAndOperations()
    {
        m_app.reset();
        AppContext::instance()->setDemo(true);
        OverviewViewModel vm;
        vm.setDemoState(QStringLiteral("overviewProcessing"));
        QCOMPARE(vm.decision(), QStringLiteral("pending"));
        QCOMPARE(vm.approveCount(), 2);
        QVERIFY(rowOf(vm, "p4").value("doubtful").toBool());
        QVERIFY(rowOf(vm, "v1").value("anonymous").toBool());
        QVERIFY(!vm.selfName().isEmpty());
        QVERIFY(vm.confirmParticipant(QStringLiteral("p4"), true));
        QCOMPARE(vm.approveCount(), 3);
        QVERIFY(!vm.confirmParticipant(QStringLiteral("v1"), true));     // a name is needed
        QVERIFY(vm.nameParticipant(QStringLiteral("v1"), QStringLiteral("Molnár Eszter")));
        QCOMPARE(vm.approveCount(), 4);
        // The suggestion with that name dissolved into the named voice.
        QVERIFY(rowOf(vm, QStringLiteral("suggest:Molnár Eszter")).isEmpty());
        QVERIFY(vm.mergeParticipantInto(QStringLiteral("v2"), QStringLiteral("p1")));
        QVERIFY(rowOf(vm, "v2").isEmpty());
        QVERIFY(vm.decideInDemo(QStringLiteral("approved")));
        QCOMPARE(vm.decision(), QStringLiteral("approved"));
        QVERIFY(!vm.approvalPending());
        QCOMPARE(vm.approveCount(), 4);

        vm.setDemoState(QStringLiteral("overviewVoiceless"));
        QVERIFY(rowOf(vm, "t1").value("voiceless").toBool());
        QVERIFY(vm.setSelfParticipant(QStringLiteral("v1")).value("ok").toBool());
        QVERIFY(rowOf(vm, "p1").isEmpty());                                // the earlier own row gave way
        QVERIFY(rowOf(vm, "v1").value("self").toBool());

        vm.setDemoState(QStringLiteral("overviewApproved"));
        QCOMPARE(vm.decision(), QStringLiteral("approved"));
        QCOMPARE(vm.approveCount(), 4);
        vm.setDemoState(QStringLiteral("overviewSkipped"));
        QCOMPARE(vm.decision(), QStringLiteral("skipped"));
        vm.setDemoState(QStringLiteral("overviewSolo"));
        QCOMPARE(vm.decision(), QStringLiteral("solo"));
        vm.setDemoState(QStringLiteral("overviewNoVoiceModel"));
        QCOMPARE(stepOf(vm, QStringLiteral("voice")).value("fixTarget").toString(), QStringLiteral("voiceModels"));
        QVERIFY(rowOf(vm, "m1").value("voiceless").toBool());
        vm.setDemoState(QStringLiteral("overviewDone"));
        QCOMPARE(vm.hiddenSpeakers(), 1);
        QVERIFY(rowOf(vm, "p2").value("lineCount").toInt() > 0);
        vm.setDemoState(QStringLiteral("overviewNobody"));
        QCOMPARE(vm.participants().size(), 3);
        QVERIFY(vm.nameParticipant(QStringLiteral("S1"), QStringLiteral("Szabó Bence")));
        QCOMPARE(rowOf(vm, "S1").value("name").toString(), QStringLiteral("Szabó Bence"));
        AppContext::instance()->setDemo(false);
    }

    // Demo (screenshot): the made-up data of the four states.
    void demoStates()
    {
        m_app.reset();
        AppContext::instance()->setDemo(true);
        OverviewViewModel vm;
        vm.setDemoState(QStringLiteral("overviewProcessing"));
        QVERIFY(vm.approvalPending());
        QVERIFY(vm.transcribing());
        QCOMPARE(vm.candidateCount(), 5);
        QCOMPARE(vm.participants().size(), 6);
        QVERIFY(vm.participants().last().toMap().value("suggested").toBool());
        QCOMPARE(stepOf(vm, QStringLiteral("voice")).value("state").toString(), QStringLiteral("attention"));
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("progress").toInt(), 62);
        // "Add" / x on the suggestion.
        vm.removeParticipant(QStringLiteral("suggest:Molnár Eszter"));
        QCOMPARE(vm.participants().size(), 5);

        vm.setDemoState(QStringLiteral("overviewDone"));
        QVERIFY(vm.allDone());
        QCOMPARE(vm.stats().size(), 6);
        QVERIFY(vm.approved());

        vm.setDemoState(QStringLiteral("overviewEmpty"));
        QCOMPARE(vm.peopleState(), QStringLiteral("empty"));
        QVERIFY(vm.contextNote().isEmpty());

        // An anonymous line can be named in place in demo mode; the finished rows have a speaker key.
        vm.setDemoState(QStringLiteral("overviewDone"));
        bool found = false;
        int unheard = 0;
        for (const QVariant& v : vm.participants()) {
            const QVariantMap row = v.toMap();
            // The invited participant who was not heard is dimmed, with no speaker key.
            if (row.value("unheard").toBool()) {
                ++unheard;
                QVERIFY(row.value("dimmed").toBool());
                QVERIFY(row.value("speakerKey").toString().isEmpty());
                continue;
            }
            QVERIFY(!row.value("speakerKey").toString().isEmpty());
            if (!row.value("anonymous").toBool()) continue;
            found = true;
            QVERIFY(vm.nameParticipant(row.value("id").toString(), QStringLiteral("Szabó Bence")));
        }
        QVERIFY(found);
        QCOMPARE(unheard, 1);
        // The heard rows carry a fictional sample; the dim (not heard) row does not.
        int withSample = 0;
        for (const QVariant& v : vm.participants()) {
            const QVariantMap row = v.toMap();
            const bool has = !row.value("sampleTrackId").toString().isEmpty();
            QCOMPARE(has, !row.value("unheard").toBool());
            if (has) {
                ++withSample;
                QCOMPARE(row.value("sampleEndMs").toLongLong() - row.value("sampleStartMs").toLongLong(), 6000);
            }
        }
        QVERIFY(withSample >= 4);
        QVERIFY(names(vm).contains(QStringLiteral("Szabó Bence")));
        QVERIFY(vm.canReanalyze());

        // The first transcription is running: two tracks, nobody yet, the transcript step is running.
        vm.setDemoState(QStringLiteral("overviewTranscribing"));
        QVERIFY(vm.transcribing());
        QVERIFY(!vm.hasTranscript());
        QCOMPARE(vm.peopleState(), QStringLiteral("empty"));
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("state").toString(), QStringLiteral("running"));
        QVERIFY(!vm.canReanalyze());

        vm.setDemoState(QStringLiteral("overviewNobody"));
        QCOMPARE(vm.peopleState(), QStringLiteral("nobody"));
        QCOMPARE(vm.participants().size(), 3);
        AppContext::instance()->setDemo(false);
    }

    // D5: the demo states of the transcript step (the speakers' source, the manual action).
    void demoStates_speakers()
    {
        m_app.reset();
        AppContext::instance()->setDemo(true);
        OverviewViewModel vm;
        // The untouched finished state says nothing about speakers.
        vm.setDemoState(QStringLiteral("overviewDone"));
        QVERIFY(vm.speakers().isEmpty());
        QVERIFY(stepOf(vm, QStringLiteral("transcript")).value("note").toString().isEmpty());
        QVERIFY(vm.allDone());

        vm.setDemoState(QStringLiteral("overviewSpeakersLocal"));
        QVERIFY(vm.hasTranscript());
        QVERIFY(!vm.allDone());
        QCOMPARE(vm.speakers().value("source").toString(), QStringLiteral("local"));
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("note").toString(), QStringLiteral("Beszélők: helyi elkülönítés"));
        QVERIFY(vm.speakers().value("canDiarize").toBool());

        vm.setDemoState(QStringLiteral("overviewSpeakersProvider"));
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("note").toString(), QStringLiteral("Beszélők: a szolgáltatótól"));
        QVERIFY(!vm.speakers().value("canDiarize").toBool());

        vm.setDemoState(QStringLiteral("overviewSpeakersOff"));
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("fixTarget").toString(), QStringLiteral("diarization"));
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("state").toString(), QStringLiteral("done"));
        QVERIFY(vm.speakers().value("line").toString().contains(QStringLiteral("kapcsold be")));
        QVERIFY(vm.speakers().value("canDiarize").toBool());

        vm.setDemoState(QStringLiteral("overviewSpeakersNoModel"));
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("fixTarget").toString(), QStringLiteral("diarization"));
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("state").toString(), QStringLiteral("attention"));
        QVERIFY(!vm.speakers().value("canDiarize").toBool());

        // The diarize stage runs: percent + detail, a cancel instead of the action.
        vm.setDemoState(QStringLiteral("overviewDiarizing"));
        QVERIFY(vm.transcribing());
        QVERIFY(vm.speakers().value("running").toBool());
        const QVariantMap run = stepOf(vm, QStringLiteral("transcript"));
        QCOMPARE(run.value("state").toString(), QStringLiteral("running"));
        QCOMPARE(run.value("progress").toInt(), 62);
        QVERIFY(run.value("sub").toString().contains(QStringLiteral("Beszélők szétválasztása")));
        QVERIFY(run.value("sub").toString().contains(QStringLiteral("62%")));

        // A failed run: attention with the reason, and the action again.
        vm.setDemoState(QStringLiteral("overviewDiarizeFailed"));
        QVERIFY(vm.speakers().value("failed").toBool());
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("state").toString(), QStringLiteral("attention"));
        QVERIFY(stepOf(vm, QStringLiteral("transcript")).value("note").toString().contains(QStringLiteral("nem sikerült")));
        QVERIFY(vm.speakers().value("canDiarize").toBool());

        // The in-place demo action turns the speakers "local".
        QVERIFY(vm.diarizeSpeakers(2));
        QCOMPARE(vm.speakers().value("source").toString(), QStringLiteral("local"));
        QVERIFY(!vm.speakers().value("failed").toBool());

        vm.setDemoState(QStringLiteral("overviewDiarizeAsk"));
        QVERIFY(vm.speakers().value("hasNames").toBool());
        QCOMPARE(vm.speakers().value("speakerCount").toInt(), 2);
        AppContext::instance()->setDemo(false);
    }
};

QTEST_MAIN(TestOverviewViewModel)
#include "test_overview_view_model.moc"
