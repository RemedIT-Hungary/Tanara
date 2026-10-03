// ShellActions + ShellMeetingModel + ShellUiState — a héj művelet- és állapot-rétege.
// Valódi AppController, IZOLÁLT TANARA_HOME (QTemporaryDir), ál-híddal (a Widgets-ablakok
// helyén): a kapuzás a régi MainWindow útját járja — futtathatóság → cloud-akadály →
// Beállítások a megfelelő lapon → indítás. Valódi szolgáltató-hívás NINCS (a feladatot az
// indulás után rögtön megszakítjuk; a szolgáltató címe egy zárt helyi port).
#include "AppContext.h"
#include "PlayerController.h"
#include "ShellActions.h"
#include "ShellBridge.h"
#include "ShellMeetingModel.h"
#include "ShellUiState.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/jobs/MeetingJobTracker.h"
#include "tanara/store/MeetingStore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <memory>

using namespace tanara_qml;

namespace {

class FakeBridge : public ShellBridge {
public:
    using ShellBridge::ShellBridge;

    QStringList calls;
    bool handleBlockers = false;      // true: a cloud-akadályt „kezeli”
    bool estimateAnswer = true;
    QString previewSummary = QStringLiteral("Kovács Lilla és 1 ismeretlen partner");
    bool previewCancelled = false;

    bool cloudChipVisible() const override { return false; }
    QString cloudChipText() const override { return {}; }
    QString cloudChipTone() const override { return QStringLiteral("normal"); }
    QString cloudChipToolTip() const override { return {}; }
    QVariantList cloudBanners() const override { return {}; }

    void openSettings(const QString& page) override { calls << QStringLiteral("settings:") + page; }
    void openPeople() override { calls << QStringLiteral("people"); }
    void openRecorder() override { calls << QStringLiteral("recorder"); }
    QString pickAudioFile() override { calls << QStringLiteral("pick"); return QStringLiteral("/tmp/x.ogg"); }
    bool handleCloudBlocker(const tanara::ReadinessResult& r) override
    {
        calls << QStringLiteral("blocker:%1:%2").arg(int(r.blockerKind)).arg(r.providerId);
        return handleBlockers;
    }
    bool confirmCloudEstimate(const QString&, const QString& task, const QString& mode) override
    {
        calls << QStringLiteral("estimate:%1:%2").arg(task, mode);
        return estimateAnswer;
    }
    QString identifyParticipantsPreview(const QString&, bool* cancelled) override
    {
        calls << QStringLiteral("preview");
        if (cancelled) *cancelled = previewCancelled;
        return previewCancelled ? QString() : previewSummary;
    }
    void cloudBannerAction(const QString&) override {}
    void cloudBannerDismiss(const QString&) override {}
    void continueRecordingInBackground() override {}
    void stopRecordingAndQuit() override {}
    void shutdown() override {}
    void windowShown() override {}
    void windowActivated() override {}
    void openUsageLog() override {}
};

} // namespace

class TestShellActions : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<tanara::AppController> m_app;
    std::unique_ptr<FakeBridge> m_bridge;
    std::unique_ptr<ShellActions> m_shell;
    QStringList m_toasts;

    tanara::Meeting recording(const QString& title)
    {
        tanara::Meeting m = m_app->store()->createMeeting(title);
        tanara::Track t;
        t.id = QStringLiteral("mic");
        t.kind = tanara::TrackKind::Mic;
        t.file = QStringLiteral("track_mic.wav");
        t.active = true;
        m.tracks = {t};
        m.durationMs = 4564000;   // 1:16:04
        QFile f(QDir(m.folder).filePath(t.file));
        if (f.open(QIODevice::WriteOnly)) f.write("nem valódi hang");
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
        // A feldolgozás a tokenekből dolgozik (az összefoglaló üres token-listára nem indul).
        QJsonArray toks;
        const QStringList words{QStringLiteral("Sziasztok."), QStringLiteral(" Szia."), QStringLiteral(" Kezdjük.")};
        for (int i = 0; i < words.size(); ++i)
            toks.append(QJsonObject{{"text", words[i]}, {"speaker", i == 1 ? "Beszélő 2" : "Beszélő 1"},
                                    {"startMs", i * 1000}, {"endMs", i * 1000 + 900},
                                    {"confidence", 0.9}, {"trackId", "mixdown"}});
        QFile tok(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
        if (tok.open(QIODevice::WriteOnly))
            tok.write(QJsonDocument(QJsonObject{{"language", "hu"}, {"tokens", toks}}).toJson());
        m.hasTranscript = true;
        m_app->store()->saveMeeting(m);
        return m;
    }
    void configureProviders(bool withSttKey)
    {
        tanara::AppSettings s = m_app->settings()->settings();
        s.sttProviderId = QStringLiteral("soniox");
        s.sttConfigs[s.sttProviderId].baseUrl = QStringLiteral("http://127.0.0.1:9/v1");   // zárt port
        s.llmProviderId = QStringLiteral("openai-compat");
        s.llmConfigs[s.llmProviderId].baseUrl = QStringLiteral("http://127.0.0.1:9/v1");
        s.llmConfigs[s.llmProviderId].model = QStringLiteral("teszt-modell");
        m_app->settings()->setSettings(s);
        if (withSttKey)
            m_app->setSecret(tanara::keys::SonioxApiKey, QStringLiteral("teszt-kulcs"));
    }

private slots:
    void initTestCase() { qputenv("TANARA_CLOUD", "off"); }

    void init()
    {
        m_home = std::make_unique<QTemporaryDir>();
        QVERIFY(m_home->isValid());
        qputenv("TANARA_HOME", m_home->path().toUtf8());
        AppContext::instance()->setDemo(false);
        m_app = std::make_unique<tanara::AppController>();
        QVERIFY(m_app->store()->audioDir().startsWith(m_home->path()));
        m_bridge = std::make_unique<FakeBridge>();
        m_shell = std::make_unique<ShellActions>();
        m_shell->setController(m_app.get());
        m_shell->setBridge(m_bridge.get());
        m_toasts.clear();
        connect(m_shell.get(), &ShellActions::toastRequested, this,
                [this](const QString& text) { m_toasts << text; });
    }

    void cleanup()
    {
        m_shell.reset();
        m_bridge.reset();
        m_app.reset();
        m_home.reset();
        qunsetenv("TANARA_HOME");
    }

    void navigation()
    {
        const tanara::Meeting a = transcribed(QStringLiteral("A"));
        QSignalSpy current(m_shell.get(), &ShellActions::currentMeetingIdChanged);
        QSignalSpy positions(m_shell.get(), &ShellActions::transcriptPositionRequested);
        PlayerController player;
        player.setController(m_app.get());
        m_shell->setPlayer(&player);

        m_shell->showTab(2);
        QCOMPARE(m_shell->currentTab(), 2);
        m_shell->showTab(9);
        QCOMPARE(m_shell->currentTab(), 2);      // határon belül marad

        // seekTo: kijelöl, az Átirat fülre vált, a lejátszót tekeri, az Editornak jelez.
        m_shell->seekTo(a.id, 1500);
        QCOMPARE(m_shell->currentMeetingId(), a.id);
        QCOMPARE(current.count(), 1);
        QCOMPARE(m_shell->currentTab(), 0);
        QCOMPARE(player.meetingId(), a.id);
        QCOMPARE(player.positionMs(), 1500);
        QCOMPARE(positions.count(), 1);
        QCOMPARE(positions.at(0).at(0).toInt(), 1500);

        QVERIFY(m_shell->meetingExists(a.id));
        QVERIFY(!m_shell->meetingExists(QStringLiteral("nincs-ilyen")));
        m_shell->setPlayer(nullptr);
    }

    void widgetsActionsGoThroughTheBridge()
    {
        m_shell->openSettings(QStringLiteral("watcher"));
        m_shell->openPeople();
        m_shell->openRecorder();
        QCOMPARE(m_shell->pickAudioFile(), QStringLiteral("/tmp/x.ogg"));
        QCOMPARE(m_bridge->calls, (QStringList{QStringLiteral("settings:watcher"), QStringLiteral("people"),
                                               QStringLiteral("recorder"), QStringLiteral("pick")}));
        // Híd nélkül (demó) nem omlik össze, értesít.
        m_shell->setBridge(nullptr);
        m_shell->openSettings(QString());
        m_shell->openRecorder();
        QCOMPARE(m_shell->pickAudioFile(), QString());
        QCOMPARE(m_toasts.size(), 2);
    }

    void transcriptionIsGatedByReadiness()
    {
        configureProviders(/*withSttKey*/ false);
        const tanara::Meeting m = recording(QStringLiteral("Kulcs nélkül"));
        QSignalSpy started(m_app->jobs(), &tanara::MeetingJobTracker::jobStarted);
        QSignalSpy revisions(m_shell.get(), &ShellActions::readinessRevisionChanged);

        // Hiányzó API-kulcs → nem cloud-akadály → Beállítások a „Külső szolgáltatások” lapon.
        m_shell->startTranscription(m.id);
        QCOMPARE(started.count(), 0);
        QCOMPARE(m_bridge->calls.size(), 2);
        QVERIFY(m_bridge->calls.at(0).startsWith(QStringLiteral("blocker:")));
        QCOMPARE(m_bridge->calls.at(1), QStringLiteral("settings:providers"));
        QVERIFY(revisions.count() >= 1);

        // Hangsáv nélküli megbeszélés → nincs mit beállítani, csak értesítés.
        m_bridge->calls.clear();
        const tanara::Meeting empty = m_app->store()->createMeeting(QStringLiteral("Üres"));
        m_shell->startTranscription(empty.id);
        QCOMPARE(started.count(), 0);
        QCOMPARE(m_bridge->calls.size(), 1);          // csak a cloud-akadály kérdés
        QCOMPARE(m_toasts.size(), 1);
        QVERIFY(m_toasts.last().contains(QStringLiteral("Nem indítható")));

        // Beállított szolgáltatóval indul (saját kulcs → nincs becslés-ablak).
        m_bridge->calls.clear();
        m_app->setSecret(tanara::keys::SonioxApiKey, QStringLiteral("teszt-kulcs"));
        m_shell->startTranscription(m.id);
        QCOMPARE(started.count(), 1);
        QCOMPARE(started.at(0).at(0).toString(), m.id);
        QVERIFY(m_bridge->calls.isEmpty());
        m_shell->cancelJob(m.id, int(tanara::JobKind::Transcribe));
        m_app->cancelAllJobs(m.id);
        QTRY_VERIFY(!m_app->jobs()->isBusy(m.id));
    }

    void summaryActionsAreGated()
    {
        configureProviders(true);
        const tanara::Meeting noTranscript = recording(QStringLiteral("Átirat nélkül"));
        QSignalSpy started(m_app->jobs(), &tanara::MeetingJobTracker::jobStarted);

        m_shell->startQuickSummary(noTranscript.id);
        m_shell->startTopicExtraction(noTranscript.id);
        m_shell->startTopicAnalysis(noTranscript.id);
        m_shell->analyzeTopic(noTranscript.id, QStringLiteral("t1"));
        QCOMPARE(started.count(), 0);
        QCOMPARE(m_toasts.size(), 4);                 // „Nem indítható: nincs átirat…”

        // Átirattal: a témánkénti elemzés téma-lista nélkül nem indul.
        const tanara::Meeting m = transcribed(QStringLiteral("Átirattal"));
        m_toasts.clear();
        m_shell->startTopicAnalysis(m.id);
        QCOMPARE(started.count(), 0);
        QCOMPARE(m_toasts.size(), 1);
        m_shell->analyzeTopic(m.id, QStringLiteral("nincs-ilyen-téma"));
        QCOMPARE(m_toasts.size(), 2);

        // Gyors összefoglaló indul (és rögtön megszakítjuk — a cím egy zárt port).
        m_toasts.clear();
        m_shell->startQuickSummary(m.id);
        QVERIFY2(started.count() == 1, qPrintable(m_toasts.join(QLatin1Char('|'))));
        QCOMPARE(started.at(0).at(1).value<tanara::JobKind>(), tanara::JobKind::Summarize);
        m_app->cancelAllJobs(m.id);
        QTRY_VERIFY(!m_app->jobs()->isBusy(m.id));
    }

    void cloudBlockerIsHandledByTheBridge()
    {
        // Élő cloud-mód, Tanara Cloud szolgáltató, bejelentkezés nélkül → Auth-akadály: a híd
        // kezeli (bejelentkezés), a héj NEM nyit Beállításokat és NEM indít semmit.
        m_shell.reset();
        m_app.reset();
        qputenv("TANARA_CLOUD", "live");
        qputenv("TANARA_CLOUD_URL", "http://127.0.0.1:9");
        m_app = std::make_unique<tanara::AppController>();
        qputenv("TANARA_CLOUD", "off");
        qunsetenv("TANARA_CLOUD_URL");
        if (!m_app->cloudLive())
            QSKIP("A cloud-mód nincs befordítva ebbe a buildbe.");
        m_shell = std::make_unique<ShellActions>();
        m_shell->setController(m_app.get());
        m_shell->setBridge(m_bridge.get());
        tanara::AppSettings s = m_app->settings()->settings();
        s.sttProviderId = tanara::cloud::ProviderId;
        s.sttConfigs[tanara::cloud::ProviderId].type = tanara::cloud::ProviderId;
        m_app->settings()->setSettings(s);
        const tanara::Meeting m = recording(QStringLiteral("Cloud"));
        QVERIFY(m_app->usesCloud(tanara::WorkflowStep::Transcribe));
        QSignalSpy started(m_app->jobs(), &tanara::MeetingJobTracker::jobStarted);

        m_bridge->handleBlockers = true;
        m_shell->startTranscription(m.id);
        QCOMPARE(started.count(), 0);
        QCOMPARE(m_bridge->calls.size(), 1);
        QVERIFY(m_bridge->calls.at(0).startsWith(QStringLiteral("blocker:")));
        QVERIFY(m_bridge->calls.at(0).endsWith(tanara::cloud::ProviderId));

        // Az újra-átírás megerősítése ugyanazon a kapun megy át.
        m_bridge->calls.clear();
        m_shell->confirmRetranscribe(m.id, true);
        QCOMPARE(started.count(), 0);
        QCOMPARE(m_bridge->calls.size(), 1);
    }

    void retranscribeAsksFirstAndReportsImpact()
    {
        configureProviders(true);
        const tanara::Meeting m = transcribed(QStringLiteral("Újra-átírandó"));
        QSignalSpy asked(m_shell.get(), &ShellActions::retranscribeDialogRequested);
        QSignalSpy started(m_app->jobs(), &tanara::MeetingJobTracker::jobStarted);

        m_shell->retranscribe(m.id);
        QCOMPARE(asked.count(), 1);
        QCOMPARE(started.count(), 0);                 // megerősítés nélkül semmi nem indul

        const QVariantMap impact = m_shell->retranscribeImpact(m.id);
        QCOMPARE(impact.value("corrections").toInt(), 0);
        QVERIFY(impact.value("text").toString().contains(QStringLiteral("hanglenyomat")));

        m_shell->confirmRetranscribe(m.id, /*keepBackup*/ true);
        QCOMPARE(started.count(), 1);
        m_app->cancelAllJobs(m.id);
        QTRY_VERIFY(!m_app->jobs()->isBusy(m.id));
    }

    void identifyBeforeTranscriptUsesThePreview()
    {
        const tanara::Meeting m = recording(QStringLiteral("Előnézet"));
        QSignalSpy guessed(m_shell.get(), &ShellActions::participantsGuessed);
        m_shell->identifyParticipants(m.id);
        QCOMPARE(m_bridge->calls, QStringList{QStringLiteral("preview")});
        QCOMPARE(guessed.count(), 1);
        QCOMPARE(m_shell->participantsGuess(m.id), m_bridge->previewSummary);
        QVERIFY(m_toasts.last().contains(QStringLiteral("Kovács Lilla")));

        // Megszakítva: nem marad eredmény, értesítés jön.
        const tanara::Meeting other = recording(QStringLiteral("Megszakított"));
        m_bridge->previewCancelled = true;
        m_shell->identifyParticipants(other.id);
        QCOMPARE(guessed.count(), 1);
        QCOMPARE(m_shell->participantsGuess(other.id), QString());
    }

    void confirmWaitsForTheDialog()
    {
        // Nincs, aki megjelenítse → azonnal hamis (nem akad be).
        QVERIFY(!m_shell->confirm(QStringLiteral("Cím"), QStringLiteral("Szöveg"), QStringLiteral("Igen"), true));

        QStringList shown;
        bool answer = true;
        connect(m_shell.get(), &ShellActions::confirmRequested, this,
                [&](const QString& title, const QString&, const QString& label, bool danger) {
            shown << title + QLatin1Char('|') + label + (danger ? QStringLiteral("|danger") : QString());
            // A QML-ablak később válaszol (a felhasználó kattint).
            QTimer::singleShot(20, m_shell.get(), [&] { m_shell->resolveConfirm(answer); });
        });
        QVERIFY(m_shell->confirm(QStringLiteral("Törlöd?"), QStringLiteral("…"), QStringLiteral("Törlés"), true));
        answer = false;
        QVERIFY(!m_shell->confirm(QStringLiteral("Biztos?"), QStringLiteral("…"), QStringLiteral("Igen"), false));
        QCOMPARE(shown, (QStringList{QStringLiteral("Törlöd?|Törlés|danger"), QStringLiteral("Biztos?|Igen")}));
    }

    void renameDeleteAndRecordingFinished()
    {
        const tanara::Meeting a = recording(QStringLiteral("Régi cím"));
        const tanara::Meeting b = recording(QStringLiteral("Másik"));
        m_shell->showMeeting(a.id);

        m_shell->renameMeeting(a.id, QStringLiteral("  Új cím  "));
        QCOMPARE(m_app->store()->load(a.id).title, QStringLiteral("Új cím"));
        m_shell->renameMeeting(a.id, QStringLiteral("   "));        // üres → marad
        QCOMPARE(m_app->store()->load(a.id).title, QStringLiteral("Új cím"));

        // Törlés: előbb megerősítést kér (a címmel), a megerősítés után töröl és a kijelölés megszűnik.
        QSignalSpy asked(m_shell.get(), &ShellActions::deleteDialogRequested);
        m_shell->requestDelete(a.id);
        QCOMPARE(asked.count(), 1);
        QCOMPARE(asked.at(0).at(1).toString(), QStringLiteral("Új cím"));
        QVERIFY(m_shell->meetingExists(a.id));
        const QString folder = m_app->store()->load(a.id).folder;
        m_shell->deleteMeeting(a.id);
        QVERIFY(!m_shell->meetingExists(a.id));
        QVERIFY(!QDir(folder).exists());
        QCOMPARE(m_shell->currentMeetingId(), QString());
        QVERIFY(m_shell->meetingExists(b.id));

        // Új felvétel elkészült → kijelölődik és értesítés jön.
        m_toasts.clear();
        emit m_app->recordingFinished(m_app->store()->load(b.id));
        QCOMPARE(m_shell->currentMeetingId(), b.id);
        QCOMPARE(m_toasts.size(), 1);

        // A felvevő „Megnyitás az elemzőben” gombja / `tanara --meeting <id>` (a híd jele):
        // kijelölés + a főablak előre; ismeretlen azonosítónál a kijelölés marad.
        m_shell->setCurrentMeetingId(QString());
        QSignalSpy activated(m_shell.get(), &ShellActions::windowActivationRequested);
        emit m_bridge->showMeetingRequested(b.id);
        QCOMPARE(m_shell->currentMeetingId(), b.id);
        QCOMPARE(activated.count(), 1);
        emit m_bridge->showMeetingRequested(QStringLiteral("nincs-ilyen"));
        QCOMPARE(m_shell->currentMeetingId(), b.id);
        QCOMPARE(activated.count(), 2);

        // A hibák nem modális ablakban, hanem értesítésként jelennek meg.
        QSignalSpy toasts(m_shell.get(), &ShellActions::toastRequested);
        emit m_app->errorOccurred(QStringLiteral("Nincs rögzíthető hangeszköz."));
        QCOMPARE(toasts.count(), 1);
        QCOMPARE(toasts.at(0).at(1).toString(), QStringLiteral("danger"));
        QVERIFY(!m_shell->recording());
    }

    void meetingModelDescribesHeaderAndTasks()
    {
        configureProviders(true);
        const tanara::Meeting plain = recording(QStringLiteral("Csak felvétel"));
        const tanara::Meeting done = transcribed(QStringLiteral("Átírt megbeszélés"));

        ShellMeetingModel model;
        model.setController(m_app.get());
        QVERIFY(!model.exists());
        model.setMeetingId(plain.id);
        QVERIFY(model.exists());
        QVERIFY(!model.hasTranscript());
        QVERIFY(model.canIdentify());
        QCOMPARE(model.title(), QStringLiteral("Csak felvétel"));
        QVERIFY2(model.metaText().endsWith(QStringLiteral("· 1:16:04 · 1 sáv")), qPrintable(model.metaText()));

        model.setMeetingId(done.id);
        QVERIFY(model.hasTranscript());
        QVERIFY2(model.metaText().endsWith(QStringLiteral("· 1:16:04 · 2 beszélő")), qPrintable(model.metaText()));
        QVERIFY(!model.summaryStale());
        QVERIFY(model.tasks().isEmpty());

        // Átnevezés → a fejléc követi.
        QSignalSpy changed(&model, &ShellMeetingModel::changed);
        m_app->renameMeeting(done.id, QStringLiteral("Új név"));
        QTRY_COMPARE(model.title(), QStringLiteral("Új név"));
        QVERIFY(changed.count() >= 1);

        // Futó feladat → megjelenik a sávban; az ELSŐ átírás nem (azt az átirat előtti nézet mutatja).
        m_app->jobs()->begin(done.id, tanara::JobKind::Mixdown, QStringLiteral("Lekeverés"));
        m_app->jobs()->setPercent(done.id, tanara::JobKind::Mixdown, 64);
        QCOMPARE(model.tasks().size(), 1);
        const QVariantMap task = model.tasks().at(0).toMap();
        QCOMPARE(task.value("kind").toInt(), int(tanara::JobKind::Mixdown));
        QCOMPARE(task.value("percent").toInt(), 64);
        QCOMPARE(task.value("title").toString(), QStringLiteral("Lekeverés…"));
        m_app->jobs()->finish(done.id, tanara::JobKind::Mixdown);
        QVERIFY(model.tasks().isEmpty());

        model.setMeetingId(plain.id);
        m_app->jobs()->begin(plain.id, tanara::JobKind::Transcribe, QStringLiteral("Átírás folyamatban"));
        QVERIFY(model.tasks().isEmpty());
        m_app->jobs()->cancelled(plain.id, tanara::JobKind::Transcribe);

        // Törölt megbeszélés → nem létezik.
        m_app->deleteMeeting(plain.id);
        QTRY_VERIFY(!model.exists());
    }

    void describeJobFormatsProgress()
    {
        tanara::JobProgress identify;
        identify.meetingId = QStringLiteral("m");
        identify.kind = tanara::JobKind::Identify;
        identify.title = QStringLiteral("Résztvevők azonosítása");
        identify.done = 3;
        identify.total = 5;
        QVariantMap t = ShellMeetingModel::describeJob(identify);
        QCOMPARE(t.value("detail").toString(), QStringLiteral("3 / 5 beszélő"));
        QCOMPARE(t.value("percent").toInt(), 60);
        QCOMPARE(t.value("iconName").toString(), QStringLiteral("fingerprint"));
        QVERIFY(t.value("cancellable").toBool());

        // Százalék nélküli szakasz → határozatlan sáv (percent −1), a futó szakasz nevével.
        tanara::JobProgress transcribe;
        transcribe.meetingId = QStringLiteral("m");
        transcribe.kind = tanara::JobKind::Transcribe;
        tanara::JobStage upload{QStringLiteral("upload"), QStringLiteral("Feltöltés"), tanara::StageState::Done, 100, {}};
        tanara::JobStage run{QStringLiteral("transcribe"), QStringLiteral("Átírás"), tanara::StageState::Running, -1, {}};
        transcribe.stages = {upload, run};
        transcribe.cancelling = true;
        t = ShellMeetingModel::describeJob(transcribe);
        QCOMPARE(t.value("percent").toInt(), -1);
        QCOMPARE(t.value("detail").toString(), QStringLiteral("Átírás"));
        QCOMPARE(t.value("title").toString(), QStringLiteral("Átírás…"));
        QVERIFY(t.value("cancelling").toBool());
        QCOMPARE(t.value("eta").toString(), QString());   // becslés nélkül nincs kitalált idő
    }

    void uiStatePersistsOnlyToItsFile()
    {
        const QString path = m_home->filePath(QStringLiteral("ui-state.json"));
        {
            ShellUiState state;
            state.setFilePath(path);
            QCOMPARE(state.value(QStringLiteral("selectedMeetingId"), QStringLiteral("x")).toString(),
                     QStringLiteral("x"));
            state.setValue(QStringLiteral("selectedMeetingId"), QStringLiteral("abc"));
            state.setValue(QStringLiteral("window"),
                           QVariantMap{{QStringLiteral("w"), 1400}, {QStringLiteral("maximized"), true}});
            QVERIFY(!QFile::exists(path));      // késleltetve ír
            state.flush();
            QVERIFY(QFile::exists(path));
        }
        ShellUiState again;
        again.setFilePath(path);
        QCOMPARE(again.value(QStringLiteral("selectedMeetingId")).toString(), QStringLiteral("abc"));
        QCOMPARE(again.value(QStringLiteral("window")).toMap().value(QStringLiteral("w")).toInt(), 1400);

        // Útvonal nélkül (demó) csak memóriában él.
        ShellUiState memory;
        memory.setValue(QStringLiteral("k"), 1);
        memory.flush();
        QCOMPARE(memory.value(QStringLiteral("k")).toInt(), 1);
        QVERIFY(memory.filePath().isEmpty());
    }
};

QTEST_MAIN(TestShellActions)
#include "test_shell_actions.moc"
