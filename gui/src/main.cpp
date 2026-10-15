// Tanara GUI entry point.
//   tanara                 the analyzer/library with the Qt Quick / QML UI (gui/qml, Main.qml)
//   tanara --meeting ID    the same, with the given meeting selected; if an analyzer is already
//                          running, it hands the request over and exits (AnalyzerSingleton.h)
//   tanara --record ...    only the floating recorder (QML), recording immediately (started by the watcher)
//                          options: --title T | --app A  --context C  --device IDX (repeatable)
//                                   --no-start  --stop
//   tanara --gallery | --demo | --qml-shot out.png [--qml-page T] [--theme ...] [--size WxH]
//                          QML developer modes WITHOUT AppController (see gui/qml/README.md)
//   tanara --settings [PAGE]  only the Settings window (started by the tray watcher's "Settings..."
//                          menu item); PAGE: general | recording | watcher | providers | cloud | summary.
//                          If a main window is running, it hands the request over and exits.
//   tanara --shell-script f.qml   developer QA: drives the main window from a script (ShellQaHook.h)
// The process is always a QApplication: the Tanara Cloud windows (sign-in, estimate, errors,
// terms, model picker), the native file / folder pickers and the tray icon are Qt Widgets,
// opened alongside the QML windows (App.bridge / SettingsWidgetsDialogs). The main window,
// Settings (SettingsWindowHost), People (PeopleWindowHost) and the recorder (RecorderWindowHost;
// in the main window through ShellRecorderHost) are QML.
#include <QElapsedTimer>
#include "AnalyzerSingleton.h"
#include "AppIcon.h"
#include "RecorderSingleton.h"
#include "RecorderTrayIcon.h"
#include "RecorderWindowHost.h"
#include "SettingsWidgetsDialogs.h"
#include "SettingsWindowHost.h"
#include "ShellRecorderHost.h"

#include "tanara/app/AppController.h"
#include "tanara/app/StartupDiagnostics.h"
#include "tanara/foundation/i18n/Localization.h"
#include "tanara/foundation/log/Logging.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/foundation/ProcessLock.h"   // watcherLockPath()

#include "AppContext.h"
#include "MediaPlayerBackend.h"
#include "PlayerController.h"
#include "QmlApp.h"
#include "QmlShellBridge.h"
#include "ShellQaHook.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QTextStream>

using namespace tanara;
using namespace tanara_gui;

// --record mode: only the floating recorder (no main window): the new QML recorder
// (gui/qml: RecorderWindow.qml + tanara_qml::RecorderWindowHost). Started by the watcher / tray.
//   --title T      explicit title          --app A      detected call app (automatic name)
//   --context C    context note            --device N   source by capture-list index (repeatable)
//   --no-start     only opens, no start    --stop       stop the RUNNING recording (opens no window)
// recording.lock (in the metadata folder) tells the watcher a recording is running. The
// process exits when the user closes the window (closing during a recording does not stop it:
// it opens the "keep running in the background" / "stop and close" page), or when the
// recording ended with a hidden window.
// Is the watcher (tanara-watcher) running? The lock probe lives in core (ProcessLock.h); the
// Settings facade uses the same one when it starts the watcher after the switch is turned on.
using tanara::watcherRunning;

// The value of `--settings [PAGE]`: the page name ("" if absent / the next item is a switch).
static QString settingsPageArg(const QStringList& args)
{
    const int i = args.indexOf(QStringLiteral("--settings"));
    if (i < 0 || i + 1 >= args.size() || args.at(i + 1).startsWith(QLatin1String("--")))
        return QString();
    return args.at(i + 1);
}

// In a process without a main window (standalone recorder, --settings): loads the remembered
// theme from the shared ui-state.json; --theme / TANARA_THEME override it.
static void applySavedTheme(app::AppController& controller, const tanara_qml::QmlOptions& opts)
{
    if (!opts.theme.isEmpty() || qEnvironmentVariableIsSet("TANARA_THEME"))
        return;
    QFile f(controller.settings()->paths().stateFile(tanara::paths::files::UiState));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QString mode = QJsonDocument::fromJson(f.readAll()).object()
                             .value(QStringLiteral("themeMode")).toString();
    if (!mode.isEmpty())
        tanara_qml::AppContext::instance()->setThemeMode(mode);
}

// --settings mode: only the Settings window (the tray watcher's "Settings..." menu item). It
// runs this way when there is no main window the request could be handed to; exits when the
// window closes.
static int runSettingsMode(QApplication& app, app::AppController& controller,
                           const tanara_qml::QmlOptions& opts, const QString& page)
{
    tanara_qml::applyOptions(opts);
    tanara_qml::AppContext::instance()->setDemo(false);
    tanara_qml::AppContext::instance()->setController(&controller);
    applySavedTheme(controller, opts);
    controller.refreshDevices();

    auto* dialogs = new SettingsWidgetsDialogs(&controller, &app);
    auto* settings = new tanara_qml::SettingsWindowHost(&controller, dialogs, &app);
    QObject::connect(settings, &tanara_qml::SettingsWindowHost::closed, &app, [] { qApp->quit(); });
    if (!settings->open(page.isEmpty() ? QStringLiteral("general") : page)) {
        QMessageBox::critical(nullptr, QCoreApplication::translate("main", "Tanara — Beállítások"),
                              QCoreApplication::translate("main", "A Beállítások felülete nem tölthető be."));
        return 1;
    }
    dialogs->setOwnerWindow(settings->window());
    return app.exec();
}

static int runRecorderMode(QApplication& app, app::AppController& controller, const QStringList& args)
{
    const RecorderArgs ra = parseRecorderArgs(args);
    // SINGLETON: if a recorder is already running (standalone or the analyzer's), we hand the
    // request over and exit; no second recorder window opens.
    if (RecorderSingleton::forwardToExisting(args))
        return 0;
    if (ra.stop)
        return 0;   // no running recorder, so nothing to stop (and no window opens)
    auto* singleton = new RecorderSingleton(&app);
    singleton->listen();

    QApplication::setQuitOnLastWindowClosed(false);   // the recording runs even with a hidden window
    const tanara_qml::QmlOptions recorderOpts = tanara_qml::parseQmlOptions(args);
    tanara_qml::applyOptions(recorderOpts);                         // --theme / TANARA_THEME
    tanara_qml::AppContext::instance()->setDemo(false);             // a real controller exists
    applySavedTheme(controller, recorderOpts);                      // otherwise the remembered theme
    // This process exits after the recording: it does NOT start the automatic mixdown (an
    // interrupted ffmpeg would leave only a truncated mixdown.part.mp3). The analyzer builds the
    // mixdown when needed.
    controller.setAutoMixdownAfterRecording(false);

    auto* host = new tanara_qml::RecorderWindowHost(
        tanara_qml::AppContext::instance()->servicesFor(&controller).recorder, nullptr, &app);

    // Own tray icon: shown only when the window is hidden AND the watcher is not running
    // (otherwise the watcher's icon brings the recorder back), and for the duration of a notification.
    QSystemTrayIcon* tray = nullptr;
    auto ensureTray = [&app, &tray, host, &controller]() -> QSystemTrayIcon* {
        if (tray || !QSystemTrayIcon::isSystemTrayAvailable())
            return tray;
        tray = new QSystemTrayIcon(&app);
        auto* menu = new QMenu();
        QObject::connect(tray, &QObject::destroyed, menu, &QObject::deleteLater);
        menu->addAction(QCoreApplication::translate("main", "Felvevő megjelenítése"), host,
                        [host] { host->show(); });
        menu->addAction(QCoreApplication::translate("main", "Felvétel leállítása"), host, [host, &controller] {
            if (controller.recordingState() == RecordingState::Recording) controller.stopRecording();
        });
        tray->setContextMenu(menu);
        QObject::connect(tray, &QSystemTrayIcon::activated, host,
                         [host](QSystemTrayIcon::ActivationReason r) {
                             if (r == QSystemTrayIcon::Trigger) host->show();
                         });
        QObject::connect(tray, &QSystemTrayIcon::messageClicked, host, [host] { host->show(); });
        return tray;
    };
    auto updateTray = [&tray, host] {
        if (!tray) return;
        const bool rec = host->recording();
        tray->setIcon(makeTrayIcon(rec ? TrayState::Recording : TrayState::Watching));
        tray->setToolTip(rec ? QCoreApplication::translate("main", "Tanara — felvétel fut")
                             : QCoreApplication::translate("main", "Tanara felvevő"));
    };

    host->setHideToTrayEnabled(QSystemTrayIcon::isSystemTrayAvailable() || watcherRunning());
    QObject::connect(host, &tanara_qml::RecorderWindowHost::hiddenToTray, &app, [&] {
        if (watcherRunning()) return;            // the watcher's tray icon brings it back
        if (QSystemTrayIcon* t = ensureTray()) { updateTray(); t->show(); }
        else host->show();                       // no tray: do not vanish for good
    });
    QObject::connect(host, &tanara_qml::RecorderWindowHost::shown, &app, [&] {
        if (tray) tray->hide();
    });
    QObject::connect(host, &tanara_qml::RecorderWindowHost::stateChanged, &app, [&](const QString&) {
        updateTray();
    });
    QObject::connect(host, &tanara_qml::RecorderWindowHost::notificationRequested, &app,
                     [&](const QString& title, const QString& text) {
        // "End of the meeting?" with a hidden / pill window: a system notification goes out too.
        if (QSystemTrayIcon* t = ensureTray()) {
            updateTray();
            const bool wasVisible = t->isVisible();
            t->show();
            t->showMessage(title, text, QSystemTrayIcon::Information, 10000);
            if (!wasVisible)
                QTimer::singleShot(12000, t, [t, host] { if (host->isVisible()) t->hide(); });
        }
    });
    QObject::connect(host, &tanara_qml::RecorderWindowHost::closed, &app, [] { qApp->quit(); });
    QObject::connect(host, &tanara_qml::RecorderWindowHost::recordingFinished, &app, [host](const QString&) {
        // With a hidden window (stopped from the tray) nobody can be shown "Saved", so quit.
        if (!host->isVisible()) qApp->quit();
    });
    QObject::connect(host, &tanara_qml::RecorderWindowHost::openMeetingRequested, &app,
                     [](const QString& meetingId) {
        // The analyzer is a separate process: we start it (--meeting is the id of the meeting to open).
        QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                {QStringLiteral("--meeting"), meetingId});
        qApp->quit();
    });
    // "Recording settings": the new QML Settings window on the "Recording" page, in this process
    // (non-modal; the recorder stays usable next to it, and the recording is unaffected).
    auto* settingsDialogs = new SettingsWidgetsDialogs(&controller, &app);
    auto* settings = new tanara_qml::SettingsWindowHost(&controller, settingsDialogs, &app);
    QObject::connect(host, &tanara_qml::RecorderWindowHost::settingsRequested, &app,
                     [settings, settingsDialogs] {
        if (settings->open(QStringLiteral("recording")))
            settingsDialogs->setOwnerWindow(settings->window());
    });
    QObject::connect(settings, &tanara_qml::SettingsWindowHost::saved, &app,
                     [&controller] { controller.refreshDevices(); });
    // When the recorder closes, the process exits: the Settings window must not keep it alive.
    QObject::connect(host, &tanara_qml::RecorderWindowHost::closed, settings,
                     [settings] { settings->closeNow(); });

    // Forwarded requests (newer tray / watcher calls) go to the same window.
    QObject::connect(singleton, &RecorderSingleton::requestReceived, host,
                     [host](const QStringList& fwd) { host->request(toRecorderRequest(parseRecorderArgs(fwd))); });

    if (!host->show()) {
        QMessageBox::critical(nullptr, QCoreApplication::translate("main", "Tanara — Felvétel"),
                              QCoreApplication::translate("main", "A felvevő felülete nem tölthető be."));
        return 1;
    }
    host->request(toRecorderRequest(ra));
    return app.exec();
}

int main(int argc, char** argv) {
    // Set up logging BEFORE anything else (so early messages are captured too). Level from the
    // command line / env: --debug | --log-level <...> | TANARA_LOG_LEVEL.
    QStringList rawArgs;
    rawArgs.reserve(argc);
    for (int i = 0; i < argc; ++i)
        rawArgs << QString::fromLocal8Bit(argv[i]);
    const QStringList cleanArgs = tanara::stripLogArgs(rawArgs);

    // Which UI starts? --record: the floating recorder; otherwise the QML main window and its
    // developer modes.
    const bool recordMode = cleanArgs.contains(QStringLiteral("--record"));
    const bool qmlMode = !recordMode;


    const bool settingsMode = qmlMode && cleanArgs.contains(QStringLiteral("--settings"));
    tanara_qml::QmlOptions qmlOpts;
    if (qmlMode) {
        qmlOpts = tanara_qml::parseQmlOptions(cleanArgs);
        if (!qmlOpts.error.isEmpty()) {
            QTextStream(stderr) << "tanara: " << qmlOpts.error << Qt::endl;
            return 2;
        }
    }

    // Gallery / demo / screenshot modes do not write to the user's ~/.tanara folder: no log file either.
    tanara::LogOptions logOpts = tanara::parseLogOptions(rawArgs);
    if (qmlMode && qmlOpts.withoutController())
        logOpts.toFile = false;
    tanara::initLogging(logOpts);

    // Quiet the console noise (external libs). Audio-only playback must not try to initialize
    // video hardware acceleration (VDPAU/VAAPI/D3D) in the ffmpeg backend.
    qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", QByteArray());

#if defined(Q_OS_LINUX)
    // Lower the PipeWire/libspa enumeration log level (e.g. the format negotiation
    // "spaVisitChoice" message of the optical/iec958 device); relevant on Linux only.
    if (!qEnvironmentVariableIsSet("PIPEWIRE_DEBUG")) qputenv("PIPEWIRE_DEBUG", "0");
    qCInfo(tanara::lcApp).noquote()
        << "Megjegyzés: a konzolon esetenként ártalmatlan külső-library üzenetek "
           "jelenhetnek meg (libvdpau – hiányzó NVIDIA VDPAU AMD gépen; libspa/PipeWire – "
           "optikai eszköz formátum-egyeztetés). Ezek NEM hibák.";
#endif

    // Qt Quick settings (style; offscreen platform in screenshot mode), BEFORE QApplication.
    if (qmlMode)
        tanara_qml::prepareProcess(qmlOpts);
    else
        tanara_qml::RecorderWindowHost::prepareProcess();   // the QML recorder

    QApplication app(argc, argv);
    // Main-thread stall watcher (only when tanara.perf debug logging is on:
    // --log-rules "tanara.perf.debug=true"): a 16 ms timer; if more than 80 ms pass between two
    // firings, the main thread did not reach the event loop for that long. The log timestamps
    // can be matched with the other (loading) entries to see what blocked the UI.
    if (tanara::lcPerf().isDebugEnabled()) {
        auto* stall = new QTimer(&app);
        auto* last = new QElapsedTimer();
        last->start();
        QObject::connect(stall, &QTimer::timeout, &app, [last] {
            const qint64 gap = last->restart();
            if (gap > 80)
                qCDebug(tanara::lcPerf).noquote()
                    << QStringLiteral("fő szál megakadás: %1 ms (nem jutott az eseményhurokhoz)").arg(gap);
        });
        stall->start(16);
    }
    QApplication::setApplicationName(QStringLiteral("Tanara"));
    QApplication::setOrganizationName(QStringLiteral("Tanara"));   // folders: foundation/paths/AppPaths.h
    QApplication::setWindowIcon(tanara_gui::makeTanaraIcon());   // every window + the tray

    // UI language (settings.json uiLanguage), BEFORE any widget is constructed.
    tanara::installAppTranslator(tanara::app::SettingsManager().settings().ui.language);

    // QML developer modes (screenshot / gallery / demo): WITHOUT AppController; the user's real
    // data (~/.tanara, ~/Tanara) is not touched, and the view models supply sample data.
    if (qmlMode && qmlOpts.withoutController()) {
        tanara_qml::applyOptions(qmlOpts);
        if (qmlOpts.shotMode())
            return tanara_qml::runShot(qmlOpts);
        QQmlApplicationEngine engine;
        if (!tanara_qml::loadPage(engine, qmlOpts.page, qmlOpts.props))
            return 1;
        return app.exec();
    }

    // Developer QA (--shell-script): we take part in the local sockets (recorder / analyzer)
    // only if a TANARA_HOME sandbox is set; there the names are separated by the folder hash, so a
    // running real instance is not disturbed.
    const bool qaScript = cleanArgs.contains(QStringLiteral("--shell-script"));
    const bool useSockets = !qaScript || !tanara::instanceScopeSuffix().isEmpty();

    // `tanara --meeting <id>` (the standalone recorder's "Open in the analyzer" button): if the
    // new main window is already running, we hand the request over and exit; no second analyzer
    // opens on the same data.
    const QString startMeetingId = analyzer_singleton::meetingArg(cleanArgs);
    if (qmlMode && !qaScript && !startMeetingId.isEmpty()
        && analyzer_singleton::forwardToExisting({QStringLiteral("--meeting"), startMeetingId}))
        return 0;

    // `tanara --settings [page]` (the tray watcher's menu item): if the new main window is
    // running, it opens Settings on the requested page, and this process exits.
    if (settingsMode && !qaScript
        && analyzer_singleton::forwardToExisting({QStringLiteral("--settings"), settingsPageArg(cleanArgs)}))
        return 0;

    tanara::app::AppController controller;

    if (settingsMode)
        return runSettingsMode(app, controller, qmlOpts, settingsPageArg(cleanArgs));

    // --record mode: only the floating recorder (started by the watcher); no main window.
    if (recordMode)
        return runRecorderMode(app, controller, cleanArgs);

    // The QML main window. The view models (gui/qml/src) reach the controller through the App
    // singleton: tanara_qml::AppContext::instance()->controller().
    tanara_qml::applyOptions(qmlOpts);
    tanara_qml::AppContext::instance()->setController(&controller);
    // The player's real audio engine (the QML module does not link Qt Multimedia).
    tanara_qml::PlayerController::setBackendFactory(
        [](QObject* parent) -> tanara_qml::PlayerBackend* {
            return new tanara_gui::MediaPlayerBackend(parent);
        });
    // The bridge (App.bridge): Settings / People / recorder windows, native file pickers and the
    // Tanara Cloud Widgets windows.
    tanara_gui::QmlShellBridge bridge(&controller);
    tanara_qml::AppContext::instance()->setBridge(&bridge);
    // In QA script mode "Getting started" does not open by itself (the script drives the main window).
    bridge.setAutoOnboarding(!qaScript);

    // QA script mode: in a fresh sandbox the index is empty, so we rebuild it from disk.
    if (qaScript)
        controller.store()->rebuildIndexFromDisk();

    QQmlApplicationEngine engine;   // declared AFTER the controller and the bridge, so it is destroyed first
    if (!tanara_qml::loadPage(engine, QStringLiteral("Main")))
        return 1;
    auto* mainWindow = engine.findChild<QQuickWindow*>();
    bridge.setMainWindow(mainWindow);
    if (useSockets) {
        // Forwarded `tanara --record ...` requests go to the main window's recorder.
        bridge.startRecorderListening();
        // Passed-over `tanara --meeting <id>` requests: select + raise the main window.
        analyzer_singleton::listen(&bridge, [&bridge](const QStringList& fwd) {
            // `tanara --settings [page]`: Settings on the requested page; otherwise a meeting selection.
            if (const int i = fwd.indexOf(QStringLiteral("--settings")); i >= 0)
                bridge.openSettings(fwd.value(i + 1));
            else
                bridge.showMeeting(analyzer_singleton::meetingArg(fwd));
        });
    }
    // The meeting requested at startup (instead of the remembered selection).
    if (!startMeetingId.isEmpty())
        bridge.showMeeting(startMeetingId);

    // Developer QA: drives the main window with a QML script (see ShellQaHook.h).
    if (const int i = cleanArgs.indexOf(QStringLiteral("--shell-script"));
        i >= 0 && i + 1 < cleanArgs.size()) {
        auto* hook = new tanara_gui::ShellQaHook(mainWindow, &engine);
        QQmlComponent script(&engine, QUrl::fromLocalFile(cleanArgs.at(i + 1)));
        QObject* obj = script.createWithInitialProperties(
            {{QStringLiteral("window"), QVariant::fromValue<QObject*>(mainWindow)},
             {QStringLiteral("hook"), QVariant::fromValue<QObject*>(hook)}});
        if (!obj) {
            QTextStream(stderr) << "tanara: --shell-script: " << script.errorString() << Qt::endl;
            return 2;
        }
        obj->setParent(&engine);
    }

    controller.refreshDevices();
    tanara::app::logStartupDiagnostics(controller);

    return app.exec();
}
