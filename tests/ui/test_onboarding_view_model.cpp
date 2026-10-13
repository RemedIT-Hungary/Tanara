// OnboardingViewModel + OnboardingWindowHost: the "Getting started" window.
//
//  - with a real AppController in an ISOLATED TANARA_HOME (QTemporaryDir): prefilling from the
//    settings (the name taken from the OS account), skipping writes only `onboardingDone`,
//    "Next" saves the name / language / theme / folders / startup per step, the status row
//    text follows readiness, external saves are followed;
//  - the host: opens by itself once on first launch, not after `onboardingDone`, always by
//    hand; on close the flag becomes true; "Set up now" opens Settings > Providers;
//  - demo (without a controller): demoState sets the step.
// Tanara Cloud is off (TANARA_CLOUD=off); an autostart entry is never written with TANARA_HOME
// (the tanara::autostart guard).
#include "AppContext.h"
#include "OnboardingViewModel.h"
#include "OnboardingWindowHost.h"

#include "tanara/app/AppController.h"
#include "tanara/foundation/paths/Paths.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/library/store/JsonSerialization.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace tanara_qml;
using tanara::app::AppController;
using tanara::AppSettings;

class TestOnboardingViewModel : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<AppController> m_app;

    void startApp()
    {
        m_app.reset();
        m_home = std::make_unique<QTemporaryDir>();
        QVERIFY(m_home->isValid());
        qputenv("TANARA_HOME", m_home->path().toUtf8());
        m_app = std::make_unique<AppController>();
        AppContext::instance()->setThemeMode(QStringLiteral("light"));
    }
    AppSettings live() const { return m_app->settings()->settings(); }
    QJsonObject settingsFile() const
    {
        QFile f(m_app->settings()->settingsFilePath());
        return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
    }
    static QVariantMap folder(const OnboardingViewModel& vm, const QString& key)
    {
        for (const QVariant& v : vm.folders())
            if (v.toMap().value(QStringLiteral("key")).toString() == key) return v.toMap();
        return {};
    }

private slots:
    void initTestCase() { qputenv("TANARA_CLOUD", "off"); }
    void cleanup()
    {
        m_app.reset();
        AppContext::instance()->setController(nullptr);
        AppContext::instance()->setThemeMode(QStringLiteral("light"));
    }
    void cleanupTestCase()
    {
        qunsetenv("TANARA_HOME");
        qunsetenv("TANARA_CLOUD");
    }

    void prefillsFromSettings()
    {
        startApp();
        QVERIFY(m_app->settings()->isFirstRun());
        OnboardingViewModel vm;
        vm.setController(m_app.get());
        QVERIFY(!vm.demo());
        QCOMPARE(vm.step(), QStringLiteral("welcome"));
        QCOMPARE(vm.stepCount(), 6);
        // The name comes from the OS account (SettingsManager::defaults): not empty, and not the old hard-coded one.
        QVERIFY(!vm.userName().trimmed().isEmpty());
        QCOMPARE(vm.userName(), live().userSpeakerName);
        QCOMPARE(vm.uiLanguage(), live().uiLanguage);
        QCOMPARE(vm.themeMode(), QStringLiteral("light"));
        QCOMPARE(folder(vm, "audio").value("path").toString(), live().audioDir);
        QVERIFY(folder(vm, "audio").value("isDefault").toBool());
        QVERIFY(folder(vm, "notes").value("isDefault").toBool());
        QCOMPARE(vm.watcherAutostart(), false);
        QVERIFY(!vm.isDone());
        QVERIFY(!vm.autostartNote().isEmpty());
    }

    void skipLeavesSettingsUntouchedExceptDone()
    {
        startApp();
        const QJsonObject before = settingsFile();
        OnboardingViewModel vm;
        vm.setController(m_app.get());
        QSignalSpy close(&vm, &OnboardingViewModel::closeRequested);
        QSignalSpy theme(&vm, &OnboardingViewModel::themeModeSaved);

        QVERIFY(vm.next());                                   // Welcome to You (nothing to save)
        QCOMPARE(vm.step(), QStringLiteral("you"));
        vm.setUserName(QStringLiteral("Valaki Más"));
        vm.setUiLanguage(QStringLiteral("en"));
        vm.setThemeMode(QStringLiteral("dark"));
        QCOMPARE(AppContext::instance()->themeMode(), QStringLiteral("dark"));   // preview
        QVERIFY(vm.stepDirty());
        vm.skip();
        QCOMPARE(AppContext::instance()->themeMode(), QStringLiteral("light"));  // restored
        QCOMPARE(vm.userName(), live().userSpeakerName);
        QCOMPARE(vm.stepStatus().value("you").toString(), QStringLiteral("skipped"));

        QCOMPARE(vm.step(), QStringLiteral("folders"));
        vm.setFolder(QStringLiteral("audio"), m_home->filePath(QStringLiteral("mashol")));
        QVERIFY(!folder(vm, "audio").value("isDefault").toBool());
        vm.skip();
        QCOMPARE(vm.step(), QStringLiteral("providers"));
        vm.skip();
        vm.setWatcherAutostart(true);
        vm.skip();
        QCOMPARE(vm.step(), QStringLiteral("done"));
        vm.skip();                                            // on the last one = Done
        QCOMPARE(close.count(), 1);
        QCOMPARE(theme.count(), 0);
        QVERIFY(vm.isDone());

        QJsonObject after = settingsFile();
        QCOMPARE(after.value("onboardingDone").toBool(), true);
        after.remove(QStringLiteral("onboardingDone"));
        QJsonObject expected = before;
        expected.remove(QStringLiteral("onboardingDone"));
        QCOMPARE(after, expected);
    }

    void acceptedStepsLandInSettings()
    {
        startApp();
        OnboardingViewModel vm;
        vm.setController(m_app.get());
        QSignalSpy theme(&vm, &OnboardingViewModel::themeModeSaved);
        QSignalSpy saved(&vm, &OnboardingViewModel::saved);
        vm.setStep(QStringLiteral("you"));

        // Empty name: does not advance, nothing is written.
        vm.setUserName(QStringLiteral("   "));
        QVERIFY(!vm.userNameError().isEmpty());
        QVERIFY(!vm.next());
        QCOMPARE(vm.step(), QStringLiteral("you"));

        vm.setUserName(QStringLiteral("  Teszt Elek "));
        vm.setUiLanguage(QStringLiteral("en"));
        vm.setThemeMode(QStringLiteral("dark"));
        QVERIFY(vm.next());
        QCOMPARE(live().userSpeakerName, QStringLiteral("Teszt Elek"));
        QCOMPARE(live().uiLanguage, QStringLiteral("en"));
        QCOMPARE(theme.count(), 1);
        QCOMPARE(theme.first().at(0).toString(), QStringLiteral("dark"));
        QCOMPARE(AppContext::instance()->themeMode(), QStringLiteral("dark"));
        QVERIFY(saved.count() >= 1);
        QCOMPARE(vm.stepStatus().value("you").toString(), QStringLiteral("done"));

        QCOMPARE(vm.step(), QStringLiteral("folders"));
        const QString audio = m_home->filePath(QStringLiteral("felvetelek-uj"));
        const QString notes = m_home->filePath(QStringLiteral("jegyzetek-uj"));
        vm.setFolder(QStringLiteral("audio"), audio);
        vm.setFolder(QStringLiteral("notes"), notes);
        QVERIFY(vm.next());
        QCOMPARE(live().audioDir, audio);
        QCOMPARE(live().notesDir, notes);
        // Back to the default (for the badge): the draft goes back to the default path.
        vm.back();
        vm.resetFolder(QStringLiteral("audio"));
        QVERIFY(folder(vm, "audio").value("isDefault").toBool());
        QVERIFY(vm.next());
        QVERIFY(tanara::paths::expandHome(live().audioDir) == tanara::paths::defaultAudioDir()
                || live().audioDir == tanara::paths::defaultAudioDir());

        QCOMPARE(vm.step(), QStringLiteral("providers"));
        QVERIFY(vm.next());
        QCOMPARE(vm.step(), QStringLiteral("watcher"));
        vm.setWatcherAutostart(true);
        QVERIFY(vm.next());
        QCOMPARE(live().watcherAutostart, true);
        QCOMPARE(vm.step(), QStringLiteral("done"));
        QVERIFY(!live().onboardingDone);                      // the flag only at the end / on close
        vm.next();
        QVERIFY(live().onboardingDone);
    }

    void readinessLineFollowsProviders()
    {
        startApp();
        OnboardingViewModel vm;
        vm.setController(m_app.get());
        // Fresh install: no Soniox key, the local LM Studio with the gemma model is filled in.
        QCOMPARE(vm.sttStatus(), QStringLiteral("Átírás: nincs kulcs"));
        QVERIFY(!vm.sttReady());
        QCOMPARE(vm.llmStatus(), QStringLiteral("Összefoglaló: LM Studio · gemma-4-12b"));
        QVERIFY(vm.llmReady());

        m_app->setSecret(tanara::app::keys::SonioxApiKey, QStringLiteral("teszt-kulcs"));
        vm.refreshReadiness();
        QCOMPARE(vm.sttStatus(), QStringLiteral("Átírás: Soniox"));
        QVERIFY(vm.sttReady());

        // External save (Settings): the row refreshes by itself.
        AppSettings s = live();
        s.llmConfigs[s.llmProviderId].baseUrl.clear();
        m_app->settings()->setSettings(s);
        QCOMPARE(vm.llmStatus(), QStringLiteral("Összefoglaló: hiányos beállítás"));
        QVERIFY(!vm.llmReady());

        s.llmConfigs[s.llmProviderId].baseUrl = QStringLiteral("http://localhost:11434/v1");
        s.llmConfigs[s.llmProviderId].model = QStringLiteral("qwen3:8b");
        QCOMPARE(OnboardingViewModel::providerLabel(s, false), QStringLiteral("Ollama · qwen3:8b"));
    }

    void externalSaveUpdatesUntouchedFields()
    {
        startApp();
        OnboardingViewModel vm;
        vm.setController(m_app.get());
        vm.setStep(QStringLiteral("folders"));
        const QString typed = m_home->filePath(QStringLiteral("begepelt"));
        vm.setFolder(QStringLiteral("notes"), typed);
        AppSettings s = live();
        s.audioDir = m_home->filePath(QStringLiteral("kivulrol"));
        s.notesDir = m_home->filePath(QStringLiteral("kivulrol-jegyzet"));
        m_app->settings()->setSettings(s);
        QCOMPARE(folder(vm, "audio").value("path").toString(), s.audioDir);   // untouched, so it follows
        QCOMPARE(folder(vm, "notes").value("path").toString(), typed);        // what was typed stays
    }

    // ---- the host: automatic / manual opening ----

    void hostOpensOnceOnFirstRun()
    {
        startApp();
        OnboardingWindowHost host(m_app.get(), nullptr);
        QVERIFY(host.shouldAutoOpen());
        QVERIFY(host.openIfNeeded());
        QVERIFY(host.isVisible());
        QVERIFY(host.viewModel());
        QCOMPARE(host.viewModel()->step(), QStringLiteral("welcome"));
        // In the same process it does not open by itself a second time.
        QVERIFY(!host.openIfNeeded());

        // "Set up now" opens Settings > Providers.
        QSignalSpy settings(&host, &OnboardingWindowHost::openSettingsRequested);
        host.viewModel()->openServices();
        QCOMPARE(settings.count(), 1);
        QCOMPARE(settings.first().at(0).toString(), QStringLiteral("providers"));

        // Close (x): a step not accepted is lost, the flag is true.
        host.viewModel()->setStep(QStringLiteral("you"));
        host.viewModel()->setUserName(QStringLiteral("Nem mentett"));
        QSignalSpy closed(&host, &OnboardingWindowHost::closed);
        host.window()->close();
        QTRY_COMPARE(closed.count(), 1);
        QVERIFY(live().onboardingDone);
        QVERIFY(live().userSpeakerName != QStringLiteral("Nem mentett"));

        // New process (new host) on the same data: not by itself, by hand yes, starting from the beginning.
        OnboardingWindowHost again(m_app.get(), nullptr);
        QVERIFY(!again.shouldAutoOpen());
        QVERIFY(!again.openIfNeeded());
        QVERIFY(!again.isVisible());
        QVERIFY(again.open());
        QVERIFY(again.isVisible());
        QCOMPARE(again.viewModel()->step(), QStringLiteral("welcome"));
        again.closeNow();
        QVERIFY(!again.isVisible());
    }

    void hostSkipsWhenDoneAndWithoutController()
    {
        startApp();
        AppSettings s = live();
        s.onboardingDone = true;
        m_app->settings()->setSettings(s);
        OnboardingWindowHost host(m_app.get(), nullptr);
        QVERIFY(!host.openIfNeeded());
        QVERIFY(!host.window());

        OnboardingWindowHost none(nullptr, nullptr);
        QVERIFY(!none.shouldAutoOpen());
    }

    // ---- demo ----

    void demoStatesSelectTheStep()
    {
        for (const char* st : {"welcome", "you", "folders", "providers", "watcher", "done"}) {
            OnboardingViewModel vm;
            vm.setDemoState(QString::fromLatin1(st));
            QVERIFY(vm.demo());
            QCOMPARE(vm.step(), QString::fromLatin1(st));
            QCOMPARE(vm.userName(), QStringLiteral("Kovács Lilla"));
            QVERIFY(!vm.sttStatus().isEmpty());
        }
        OnboardingViewModel vm;
        vm.setDemoState(QStringLiteral("done"));
        QCOMPARE(vm.stepStatus().value("folders").toString(), QStringLiteral("skipped"));
        QCOMPARE(vm.stepStatus().value("you").toString(), QStringLiteral("done"));
        // In demo mode saves stay in memory.
        vm.setStep(QStringLiteral("you"));
        vm.setUserName(QStringLiteral("Nagy Bence"));
        QVERIFY(vm.next());
        QCOMPARE(vm.userName(), QStringLiteral("Nagy Bence"));
    }
};

QTEST_MAIN(TestOnboardingViewModel)
#include "test_onboarding_view_model.moc"
