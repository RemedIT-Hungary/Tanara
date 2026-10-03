// Tanara GUI — belépési pont.
//   tanara                 az elemző/könyvtár az új Qt Quick / QML felülettel (gui/qml, Main.qml)
//   tanara --classic       ugyanez a régi Qt Widgets főablakkal (MainWindow), változatlanul
//   tanara --record …      csak a lebegő felvevő (QML), azonnali rögzítéssel (a figyelő indítja)
//                          opciók: --title T | --app A  --context C  --device IDX (ismételhető)
//                                  --no-start  --stop;  --classic: a régi Widgets-felvevő
//   tanara --gallery | --demo | --qml-shot ki.png [--qml-page T] [--theme …] [--size SZxM]
//                          QML-fejlesztői módok AppController NÉLKÜL (lásd gui/qml/README.md)
// A folyamat mindig QApplication: a Beállítások / Személyek / felvevő / cloud ablakok
// egyelőre Widgetek maradnak, és a QML-ablak mellett nyílnak (App.bridge).
#include "MainWindow.h"
#include "RecordBar.h"
#include "FloatingRecorder.h"
#include "AppIcon.h"
#include "RecorderSingleton.h"
#include "RecorderTrayIcon.h"
#include "RecorderWindowHost.h"
#include "SettingsDialog.h"
#include "cloud/CloudSnapshots.h"

#include "tanara/AppController.h"
#include "tanara/Localization.h"
#include "tanara/Logging.h"
#include "tanara/Paths.h"
#include "tanara/SettingsManager.h"
#include "tanara/audio/DeviceManager.h"
#include "tanara/detect/RecordingLock.h"

#include "AppContext.h"
#include "QmlApp.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QLockFile>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QQmlApplicationEngine>
#include <QTextStream>

#include <memory>

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
static tanara_qml::RecorderRequest toRequest(const RecorderArgs& ra)
{
    tanara_qml::RecorderRequest r;
    r.title = ra.title;
    r.appName = ra.app;
    r.context = ra.context;
    r.deviceIndexes = ra.deviceIdx;
    r.start = !ra.noStart;
    r.stop = ra.stop;
    return r;
}

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
    tanara_qml::applyOptions(tanara_qml::parseQmlOptions(args));   // --theme / TANARA_THEME
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
    QObject::connect(host, &tanara_qml::RecorderWindowHost::settingsRequested, &app, [&controller] {
        SettingsDialog dlg(&controller);
        dlg.exec();
        controller.refreshDevices();
    });

    // Továbbított kérések (tálca / figyelő újabb hívásai) → ugyanez az ablak.
    QObject::connect(singleton, &RecorderSingleton::requestReceived, host,
                     [host](const QStringList& fwd) { host->request(toRequest(parseRecorderArgs(fwd))); });

    if (!host->show()) {
        QMessageBox::critical(nullptr, QCoreApplication::translate("main", "Tanara — Felvétel"),
                              QCoreApplication::translate("main", "A felvevő felülete nem tölthető be."));
        return 1;
    }
    host->request(toRequest(ra));
    return app.exec();
}

// --record --classic: a RÉGI (Qt Widgets) lebegő felvevő önállóan, változatlan viselkedéssel
// (azonnal indul, a felvétel végén kilép). Az alapértelmezett --record az új QML-felvevő.
static int runClassicRecorderMode(QApplication& app, AppController& controller, const QStringList& args)
{
    // SINGLETON: ha már fut felvevő (önálló vagy az elemzőé), a kérést átadjuk neki és
    // kilépünk — nem nyílik második felvevő-ablak.
    if (RecorderSingleton::forwardToExisting(args))
        return 0;
    auto* singleton = new RecorderSingleton(&app);
    singleton->listen();

    const RecorderArgs ra = parseRecorderArgs(args);
    QString title = ra.title, context = ra.context;
    const QList<int> deviceIdx = ra.deviceIdx;
    const bool noStart = ra.noStart;
    if (title.trimmed().isEmpty())
        title = QCoreApplication::translate("main", "Felvétel %1")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));

    controller.refreshDevices();
    const QVector<AudioDeviceInfo> all = controller.devices()->captureDevices();
    QVector<AudioDeviceInfo> sel;
    if (deviceIdx.isEmpty())
        sel = controller.devices()->autoRecordDevices();   // line-in/AUX kimarad
    else
        for (int idx : deviceIdx)
            if (idx >= 0 && idx < all.size()) sel << all[idx];
    if (sel.isEmpty()) {
        QMessageBox::critical(nullptr,
                              QCoreApplication::translate("main", "Tanara — Felvétel"),
                              QCoreApplication::translate("main", "Nincs rögzíthető hangeszköz."));
        return 1;
    }

    // Lock-fájl a metaDir-ben (~/.tanara). A settings nyers ~-t adhat → kifejtjük.
    const QString metaDir =
        tanara::paths::resolveMetadataDir(controller.settings()->settings().metadataDir);
    auto lock = std::make_shared<RecordingLock>(QDir(metaDir).filePath(QStringLiteral("recording.lock")));
    // A folyamat a felvétel végén kilép → ne indítson lekeverést, amit félbehagyna.
    controller.setAutoMixdownAfterRecording(false);

    // Lebegő felvevő (a RecordBar-t a FloatingRecorder reparentálja magába).
    auto* recordBar = new RecordBar(&controller, nullptr);
    recordBar->setViewMode(RecordBar::ViewMode::Full);

    // A RecordBar-t a controller jeleire kötjük (állapot/szintek/idő) — ugyanúgy, ahogy a
    // MainWindow teszi; e nélkül a felvevő nem váltana Stop-módra és a szintek se mozognának.
    QObject::connect(&controller, &AppController::devicesChanged,
                     recordBar, &RecordBar::onDevicesChanged);
    QObject::connect(&controller, &AppController::recordingStateChanged,
                     recordBar, &RecordBar::onRecordingStateChanged);
    QObject::connect(&controller, &AppController::elapsedChanged,
                     recordBar, &RecordBar::onElapsedChanged);
    QObject::connect(&controller, &AppController::levelMeterUpdated,
                     recordBar, &RecordBar::onLevelMeterUpdated);
    QObject::connect(&controller, &AppController::deviceLevel,
                     recordBar, &RecordBar::onDeviceLevel);

    auto* recorder = new FloatingRecorder(&controller, recordBar, nullptr);
    recordBar->refreshFromSettings();
    // FONTOS: a top-level ablakot (a FloatingRecordert) kell megmutatni — a beágyazott
    // RecordBar önmagában nem hoz fel ablakot. E nélkül nincs Stop-gomb → nincs leállítás.
    recorder->show();
    recorder->raise();
    recorder->activateWindow();

    // Felvétel-indulás → lock felvétele a friss meeting-mappával.
    QObject::connect(&controller, &AppController::recordingStateChanged, &app,
                     [&controller, lock](RecordingState st) {
                         if (st == RecordingState::Recording)
                             lock->acquire(controller.currentMeetingFolder());
                     });
    // Felvétel vége → a detektált kontextus mentése + lock elengedése + kilépés (frugális).
    QObject::connect(&controller, &AppController::recordingFinished, &app,
                     [&controller, lock, context](Meeting m) {
                         if (!context.trimmed().isEmpty())
                             controller.setMeetingContextNote(m.id, context.trimmed());
                         lock->release();
                         qApp->quit();
                     });
    QObject::connect(&controller, &AppController::errorOccurred, &app,
                     [lock](const QString& e) {
                         lock->release();
                         QMessageBox::critical(
                             nullptr, QCoreApplication::translate("main", "Tanara — Felvétel"), e);
                         qApp->exit(1);
                     });
    // A lebegő ablak bezárása: ha megy felvétel, állítsuk le (a finished kiléptet), különben kilépés.
    QObject::connect(recorder, &FloatingRecorder::dockRequested, &app, [&controller]() {
        if (controller.recordingState() == RecordingState::Recording)
            controller.stopRecording();
        else
            qApp->quit();
    });

    // Azonnali indítás (a figyelő „Rögzítés azonnali indítása" útja), VAGY --no-start
    // esetén csak megnyitjuk a felvevőt: a user elkeresztel + a felvevő Start-gombjával indít.
    // Továbbított kérések (tálca/figyelő újabb hívásai) → ugyanez az ablak elő; azonnali
    // kérésnél indítás is, ha üresjáratban vagyunk.
    QObject::connect(singleton, &RecorderSingleton::requestReceived, recorder,
                     [recorder, recordBar, &controller](const QStringList& fwd) {
                         const RecorderArgs r = parseRecorderArgs(fwd);
                         recorder->show(); recorder->raise(); recorder->activateWindow();
                         if (!r.noStart && controller.recordingState() == RecordingState::Idle)
                             recordBar->startWithTitle(r.title);
                     });

    if (!noStart)
        controller.startRecording(title, sel);
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

    // Melyik felület indul? --record: lebegő felvevő; --classic (vagy a Widgets-képernyőkép
    // QA): a régi MainWindow; különben az új QML-főablak, illetve annak fejlesztői módjai.
    const bool recordMode = cleanArgs.contains(QStringLiteral("--record"));
    const bool classicMode = cleanArgs.contains(QStringLiteral("--classic"))
                             || cleanArgs.contains(QStringLiteral("--ui-snapshots"));
    const bool qmlMode = !recordMode && !classicMode;
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
    // a QApplication ELŐTT. Klasszikus és felvevő-módban semmi nem változik.
    if (qmlMode)
        tanara_qml::prepareProcess(qmlOpts);
    else if (recordMode && !classicMode)
        tanara_qml::RecorderWindowHost::prepareProcess();   // az új QML-felvevő

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

    tanara::AppController controller;

    // --record mód: csak a lebegő felvevő (a figyelő indítja); nincs főablak.
    if (recordMode)
        return classicMode ? runClassicRecorderMode(app, controller, cleanArgs)
                           : runRecorderMode(app, controller, cleanArgs);

    if (classicMode) {
        tanara_gui::MainWindow window(&controller);

        // Fejlesztői QA: a Tanara Cloud képernyők PNG-be mentése, majd kilépés.
        if (const int i = cleanArgs.indexOf(QStringLiteral("--ui-snapshots")); i >= 0 && i + 1 < cleanArgs.size())
            return tanara_gui::runCloudSnapshots(controller, window, cleanArgs.at(i + 1));

        window.show();

        // Eszközök felsorolása indításkor (→ devicesChanged → eszközlista feltöltése).
        controller.refreshDevices();

        // Induló diagnosztika (fejléc info, részletek debug szinten) — a refresh UTÁN,
        // hogy a látott audio-eszközök is benne legyenek.
        tanara::logStartupDiagnostics(controller);

        return app.exec();
    }

    // Az új QML-főablak. A nézetmodellek (gui/qml/src) az App-singletonon át érik el a
    // controllert: tanara_qml::AppContext::instance()->controller().
    tanara_qml::applyOptions(qmlOpts);
    tanara_qml::AppContext::instance()->setController(&controller);
    QQmlApplicationEngine engine;   // a controller UTÁN deklarálva → előbb szűnik meg
    if (!tanara_qml::loadPage(engine, QStringLiteral("Main")))
        return 1;

    controller.refreshDevices();
    tanara::logStartupDiagnostics(controller);

    return app.exec();
}
