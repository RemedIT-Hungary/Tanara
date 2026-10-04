// Tanara GUI — belépési pont.
//   tanara                 az elemző/könyvtár a Qt Quick / QML felülettel (gui/qml, Main.qml)
//   tanara --meeting ID    ugyanez, a megadott megbeszélést kijelölve; ha már fut elemző, a
//                          kérést annak adja át és kilép (AnalyzerSingleton.h)
//   tanara --record …      csak a lebegő felvevő (QML), azonnali rögzítéssel (a figyelő indítja)
//                          opciók: --title T | --app A  --context C  --device IDX (ismételhető)
//                                  --no-start  --stop
//   tanara --gallery | --demo | --qml-shot ki.png [--qml-page T] [--theme …] [--size SZxM]
//                          QML-fejlesztői módok AppController NÉLKÜL (lásd gui/qml/README.md)
//   tanara --settings [LAP]  csak a Beállítások ablaka (a tálca-figyelő „Beállítások…” menüpontja
//                          indítja); LAP: general | recording | watcher | providers | cloud | summary.
//                          Ha fut főablak, a kérést annak adja át és kilép.
//   tanara --shell-script f.qml   fejlesztői QA: a főablak végigvezetése szkriptből (ShellQaHook.h)
// A folyamat mindig QApplication: a Tanara Cloud ablakai (bejelentkezés, becslés, hibák,
// ÁSZF, modellválasztó), a natív fájl- / mappaválasztók és a tálca-ikon Qt Widgets, és a
// QML-ablakok mellett nyílnak (App.bridge / SettingsWidgetsDialogs). A főablak, a Beállítások
// (SettingsWindowHost), a Személyek (PeopleWindowHost) és a felvevő (RecorderWindowHost; a
// főablakban a ShellRecorderHost-on át) QML.
#include "AnalyzerSingleton.h"
#include "AppIcon.h"
#include "RecorderSingleton.h"
#include "RecorderTrayIcon.h"
#include "RecorderWindowHost.h"
#include "SettingsWidgetsDialogs.h"
#include "SettingsWindowHost.h"
#include "ShellRecorderHost.h"

#include "tanara/AppController.h"
#include "tanara/Localization.h"
#include "tanara/Logging.h"
#include "tanara/Paths.h"
#include "tanara/SettingsManager.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/detect/RecordingLock.h"   // watcherLockPath()

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
#include <QLockFile>
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

// --record mód: csak a lebegő felvevő (nincs főablak) — az új QML-felvevő
// (gui/qml: RecorderWindow.qml + tanara_qml::RecorderWindowHost). A figyelő / tálca indítja.
//   --title T      kifejezett cím          --app A      észlelt hívás-app (automatikus név)
//   --context C    kontextus-megjegyzés    --device N   forrás a capture-lista sorszámával (ismételhető)
//   --no-start     csak megnyit, nem indít --stop       a FUTÓ felvétel leállítása (nem nyit ablakot)
// A recording.lock (a metaadat-mappában) jelzi a figyelőnek, hogy megy a felvétel. A folyamat
// akkor lép ki, amikor a felhasználó bezárja az ablakot (felvétel közben a bezárás nem állít
// le: a „háttérben fusson” / „leállítás és bezárás” lapot nyitja), vagy ha a felvétel rejtett
// ablak mellett ért véget.
// Fut-e a figyelő (tanara-watcher)? A watcher.lock-ot tartja; ha mi meg tudjuk fogni, nem fut.
static bool watcherRunning()
{
    QLockFile probe(tanara::watcherLockPath());
    probe.setStaleLockTime(0);
    if (probe.tryLock(0)) {
        probe.unlock();
        return false;
    }
    return true;
}

// A `--settings [LAP]` értéke: a lap neve ("" ha nincs megadva / a következő elem kapcsoló).
static QString settingsPageArg(const QStringList& args)
{
    const int i = args.indexOf(QStringLiteral("--settings"));
    if (i < 0 || i + 1 >= args.size() || args.at(i + 1).startsWith(QLatin1String("--")))
        return QString();
    return args.at(i + 1);
}

// Főablak nélküli folyamatban (önálló felvevő, --settings) a megjegyzett téma betöltése a
// közös ui-state.json-ból — a --theme / TANARA_THEME erősebb nála.
static void applySavedTheme(AppController& controller, const tanara_qml::QmlOptions& opts)
{
    if (!opts.theme.isEmpty() || qEnvironmentVariableIsSet("TANARA_THEME"))
        return;
    QFile f(tanara::paths::metadataFile(QStringLiteral("ui-state.json"),
                                        controller.settings()->settings().metadataDir));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QString mode = QJsonDocument::fromJson(f.readAll()).object()
                             .value(QStringLiteral("themeMode")).toString();
    if (!mode.isEmpty())
        tanara_qml::AppContext::instance()->setThemeMode(mode);
}

// --settings mód: csak a Beállítások ablaka (a tálca-figyelő „Beállítások…” menüpontja). Akkor
// fut így, ha nincs főablak, amelynek a kérést át lehetett adni; az ablak bezárásakor kilép.
static int runSettingsMode(QApplication& app, AppController& controller,
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

static int runRecorderMode(QApplication& app, AppController& controller, const QStringList& args)
{
    const RecorderArgs ra = parseRecorderArgs(args);
    // SINGLETON: ha már fut felvevő (önálló vagy az elemzőé), a kérést átadjuk neki és
    // kilépünk — nem nyílik második felvevő-ablak.
    if (RecorderSingleton::forwardToExisting(args))
        return 0;
    if (ra.stop)
        return 0;   // nincs futó felvevő → nincs mit leállítani (ablakot sem nyitunk)
    auto* singleton = new RecorderSingleton(&app);
    singleton->listen();

    QApplication::setQuitOnLastWindowClosed(false);   // rejtett ablakkal is fut a felvétel
    const tanara_qml::QmlOptions recorderOpts = tanara_qml::parseQmlOptions(args);
    tanara_qml::applyOptions(recorderOpts);                         // --theme / TANARA_THEME
    tanara_qml::AppContext::instance()->setDemo(false);             // valódi controller van
    applySavedTheme(controller, recorderOpts);                      // különben a megjegyzett téma
    // Ez a folyamat a felvétel után kilép: az automatikus lekeverést NEM indítja el (a
    // félbehagyott ffmpeg csak csonka mixdown.part.mp3-at hagyna). A lekeverést az elemző
    // készíti el, amikor kell.
    controller.setAutoMixdownAfterRecording(false);

    auto* host = new tanara_qml::RecorderWindowHost(&controller, nullptr, &app);

    // Saját tálca-ikon: csak akkor látszik, ha az ablak rejtve van ÉS a figyelő nem fut
    // (különben az ő ikonja hozza vissza a felvevőt), illetve értesítés idejére.
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
        if (watcherRunning()) return;            // a figyelő tálca-ikonja visszahozza
        if (QSystemTrayIcon* t = ensureTray()) { updateTray(); t->show(); }
        else host->show();                       // nincs tálca: ne tűnjön el végleg
    });
    QObject::connect(host, &tanara_qml::RecorderWindowHost::shown, &app, [&] {
        if (tray) tray->hide();
    });
    QObject::connect(host, &tanara_qml::RecorderWindowHost::stateChanged, &app, [&](const QString&) {
        updateTray();
    });
    QObject::connect(host, &tanara_qml::RecorderWindowHost::notificationRequested, &app,
                     [&](const QString& title, const QString& text) {
        // „Vége a megbeszélésnek?” rejtett / pirula ablaknál: rendszerértesítés is megy.
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
        // Rejtett ablak mellett (tálcáról leállítva) nincs kinek „Elmentve”-t mutatni → kilépés.
        if (!host->isVisible()) qApp->quit();
    });
    QObject::connect(host, &tanara_qml::RecorderWindowHost::openMeetingRequested, &app,
                     [](const QString& meetingId) {
        // Az elemző külön folyamat: elindítjuk (a --meeting a megnyitandó meeting azonosítója).
        QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                {QStringLiteral("--meeting"), meetingId});
        qApp->quit();
    });
    // „Rögzítés beállításai” (R10): az új QML Beállítások-ablak a „Rögzítés” lapon, ebben a
    // folyamatban (nem modális — a felvevő mellette használható, a felvételt nem érinti).
    auto* settingsDialogs = new SettingsWidgetsDialogs(&controller, &app);
    auto* settings = new tanara_qml::SettingsWindowHost(&controller, settingsDialogs, &app);
    QObject::connect(host, &tanara_qml::RecorderWindowHost::settingsRequested, &app,
                     [settings, settingsDialogs] {
        if (settings->open(QStringLiteral("recording")))
            settingsDialogs->setOwnerWindow(settings->window());
    });
    QObject::connect(settings, &tanara_qml::SettingsWindowHost::saved, &app,
                     [&controller] { controller.refreshDevices(); });
    // A felvevő bezárásakor a folyamat kilép: a Beállítások ablaka se tartsa életben.
    QObject::connect(host, &tanara_qml::RecorderWindowHost::closed, settings,
                     [settings] { settings->closeNow(); });

    // Továbbított kérések (tálca / figyelő újabb hívásai) → ugyanez az ablak.
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
    // Logolás MIELŐTT bármi más (hogy a korai üzenetek is beessenek). Szint a
    // parancssorból/env-ből: --debug | --log-level <…> | TANARA_LOG_LEVEL.
    QStringList rawArgs;
    rawArgs.reserve(argc);
    for (int i = 0; i < argc; ++i)
        rawArgs << QString::fromLocal8Bit(argv[i]);
    const QStringList cleanArgs = tanara::stripLogArgs(rawArgs);

    // Melyik felület indul? --record: lebegő felvevő; különben a QML-főablak, illetve annak
    // fejlesztői módjai.
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

    // A galéria / demó / képernyőkép-mód nem ír a user ~/.tanara mappájába: logfájl sincs.
    tanara::LogOptions logOpts = tanara::parseLogOptions(rawArgs);
    if (qmlMode && qmlOpts.withoutController())
        logOpts.toFile = false;
    tanara::initLogging(logOpts);

    // Konzol-zaj csendesítése (külső libek). Audio-only lejátszás → ne próbáljon
    // videó-hardvergyorsítást (VDPAU/VAAPI/D3D) inicializálni az ffmpeg-backend.
    qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", QByteArray());

#if defined(Q_OS_LINUX)
    // PipeWire/libspa enumerációs log szintje le (pl. az optikai/iec958 eszköz
    // formátum-egyeztetési „spaVisitChoice" üzenete) — csak Linuxon releváns.
    if (!qEnvironmentVariableIsSet("PIPEWIRE_DEBUG")) qputenv("PIPEWIRE_DEBUG", "0");
    qCInfo(tanara::lcApp).noquote()
        << "Megjegyzés: a konzolon esetenként ártalmatlan külső-library üzenetek "
           "jelenhetnek meg (libvdpau – hiányzó NVIDIA VDPAU AMD gépen; libspa/PipeWire – "
           "optikai eszköz formátum-egyeztetés). Ezek NEM hibák.";
#endif

    // Qt Quick-beállítások (stílus, szövegrajzolás; képernyőkép-módban offscreen platform) —
    // a QApplication ELŐTT.
    if (qmlMode)
        tanara_qml::prepareProcess(qmlOpts);
    else
        tanara_qml::RecorderWindowHost::prepareProcess();   // a QML-felvevő

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Tanara"));
    QApplication::setOrganizationName(QStringLiteral("RemedIT"));
    QApplication::setWindowIcon(tanara_gui::makeTanaraIcon());   // minden ablakra + tálcára

    // UI-nyelv (settings.json uiLanguage) — minden widget megkonstruálása ELŐTT.
    tanara::installAppTranslator();

    // QML-fejlesztői módok (képernyőkép / galéria / demó): AppController NÉLKÜL — a user
    // valódi adataihoz (~/.tanara, ~/Tanara) nem nyúlunk, a nézetmodellek mintaadatot adnak.
    if (qmlMode && qmlOpts.withoutController()) {
        tanara_qml::applyOptions(qmlOpts);
        if (qmlOpts.shotMode())
            return tanara_qml::runShot(qmlOpts);
        QQmlApplicationEngine engine;
        if (!tanara_qml::loadPage(engine, qmlOpts.page, qmlOpts.props))
            return 1;
        return app.exec();
    }

    // Fejlesztői QA (--shell-script): a lokális socketeken (felvevő / elemző) csak akkor
    // veszünk részt, ha TANARA_HOME homokozó van beállítva — ott a nevek a mappa hash-ével
    // elkülönülnek, így a futó valódi példányt nem zavarjuk.
    const bool qaScript = cleanArgs.contains(QStringLiteral("--shell-script"));
    const bool useSockets = !qaScript || !tanara::instanceScopeSuffix().isEmpty();

    // `tanara --meeting <id>` (az önálló felvevő „Megnyitás az elemzőben” gombja): ha már fut
    // az új főablak, a kérést annak adjuk át és kilépünk — nem nyílik második elemző
    // ugyanazon az adaton.
    const QString startMeetingId = analyzer_singleton::meetingArg(cleanArgs);
    if (qmlMode && !qaScript && !startMeetingId.isEmpty()
        && analyzer_singleton::forwardToExisting({QStringLiteral("--meeting"), startMeetingId}))
        return 0;

    // `tanara --settings [lap]` (a tálca-figyelő menüpontja): ha fut az új főablak, az nyitja meg
    // a Beállításokat a kért lapon, és ez a folyamat kilép.
    if (settingsMode && !qaScript
        && analyzer_singleton::forwardToExisting({QStringLiteral("--settings"), settingsPageArg(cleanArgs)}))
        return 0;

    tanara::AppController controller;

    if (settingsMode)
        return runSettingsMode(app, controller, qmlOpts, settingsPageArg(cleanArgs));

    // --record mód: csak a lebegő felvevő (a figyelő indítja); nincs főablak.
    if (recordMode)
        return runRecorderMode(app, controller, cleanArgs);

    // A QML-főablak. A nézetmodellek (gui/qml/src) az App-singletonon át érik el a
    // controllert: tanara_qml::AppContext::instance()->controller().
    tanara_qml::applyOptions(qmlOpts);
    tanara_qml::AppContext::instance()->setController(&controller);
    // A lejátszó valódi hang-motorja (a QML-modul nem linkel Qt Multimediát).
    tanara_qml::PlayerController::setBackendFactory(
        [](QObject* parent) -> tanara_qml::PlayerBackend* {
            return new tanara_gui::MediaPlayerBackend(parent);
        });
    // A híd (App.bridge): Beállítások / Személyek / felvevő ablakok, natív fájlválasztók és a
    // Tanara Cloud Widgets-ablakai.
    tanara_gui::QmlShellBridge bridge(&controller);
    tanara_qml::AppContext::instance()->setBridge(&bridge);

    // QA-szkript mód: friss homokozóban az index üres → a lemezről újraépítjük.
    if (qaScript)
        controller.store()->rebuildIndexFromDisk();

    QQmlApplicationEngine engine;   // a controller és a híd UTÁN deklarálva → előbb szűnik meg
    if (!tanara_qml::loadPage(engine, QStringLiteral("Main")))
        return 1;
    auto* mainWindow = engine.findChild<QQuickWindow*>();
    bridge.setMainWindow(mainWindow);
    if (useSockets) {
        // A `tanara --record …` továbbított kérései → a főablak felvevője.
        bridge.startRecorderListening();
        // A `tanara --meeting <id>` átadott kérései → kijelölés + a főablak előre.
        analyzer_singleton::listen(&bridge, [&bridge](const QStringList& fwd) {
            // `tanara --settings [lap]`: a Beállítások a kért lapon; különben megbeszélés-kijelölés.
            if (const int i = fwd.indexOf(QStringLiteral("--settings")); i >= 0)
                bridge.openSettings(fwd.value(i + 1));
            else
                bridge.showMeeting(analyzer_singleton::meetingArg(fwd));
        });
    }
    // Induláskor kért megbeszélés (a megjegyzett kijelölés helyett).
    if (!startMeetingId.isEmpty())
        bridge.showMeeting(startMeetingId);

    // Fejlesztői QA: a főablak végigvezetése egy QML-szkripttel (lásd ShellQaHook.h).
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
    tanara::logStartupDiagnostics(controller);

    return app.exec();
}
