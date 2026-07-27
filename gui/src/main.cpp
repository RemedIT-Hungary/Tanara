// Tanara GUI (Qt Widgets) — belépési pont.
//   tanara                 az elemző/könyvtár (MainWindow)
//   tanara --record …      csak a lebegő felvevő, azonnali rögzítéssel (a figyelő indítja)
//                          opciók: --title T  --context C  --device IDX (ismételhető)
#include "MainWindow.h"
#include "RecordBar.h"
#include "FloatingRecorder.h"
#include "AppIcon.h"

#include "tanara/AppController.h"
#include "tanara/Localization.h"
#include "tanara/Logging.h"
#include "tanara/SettingsManager.h"
#include "tanara/audio/DeviceManager.h"
#include "tanara/detect/RecordingLock.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QMessageBox>

#include <memory>

using namespace tanara;
using namespace tanara_gui;

// --record mód: csak a lebegő felvevő (nincs MainWindow), és azonnal indul a rögzítés
// a kapott címmel/kontextussal/eszközökkel. A figyelő ezt indítja, ha a user rábólint.
// A ~/.tanara/recording.lock jelzi a figyelőnek, hogy megy a felvétel.
static int runRecorderMode(QApplication& app, AppController& controller, const QStringList& args)
{
    QString title, context;
    QList<int> deviceIdx;
    bool noStart = false;   // --no-start → csak megnyitja a felvevőt (nem indít azonnal)
    for (int i = 0; i < args.size(); ++i) {
        if (args[i] == QStringLiteral("--title") && i + 1 < args.size()) title = args[++i];
        else if (args[i] == QStringLiteral("--context") && i + 1 < args.size()) context = args[++i];
        else if (args[i] == QStringLiteral("--device") && i + 1 < args.size()) deviceIdx << args[++i].toInt();
        else if (args[i] == QStringLiteral("--no-start")) noStart = true;
    }
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
    QString metaDir = controller.settings()->settings().metadataDir;
    if (metaDir.startsWith(QLatin1Char('~')))
        metaDir = QDir::homePath() + metaDir.mid(1);
    auto lock = std::make_shared<RecordingLock>(QDir(metaDir).filePath(QStringLiteral("recording.lock")));

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
    tanara::initLogging(tanara::parseLogOptions(rawArgs));

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

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Tanara"));
    QApplication::setOrganizationName(QStringLiteral("RemedIT"));
    QApplication::setWindowIcon(tanara_gui::makeTanaraIcon());   // minden ablakra + tálcára

    // UI-nyelv (settings.json uiLanguage) — minden widget megkonstruálása ELŐTT.
    tanara::installAppTranslator();

    tanara::AppController controller;

    // --record mód: csak a lebegő felvevő (a figyelő indítja); nincs MainWindow.
    const QStringList cleanArgs = tanara::stripLogArgs(rawArgs);
    if (cleanArgs.contains(QStringLiteral("--record")))
        return runRecorderMode(app, controller, cleanArgs);

    tanara_gui::MainWindow window(&controller);
    window.show();

    // Eszközök felsorolása indításkor (→ devicesChanged → eszközlista feltöltése).
    controller.refreshDevices();

    // Induló diagnosztika (fejléc info, részletek debug szinten) — a refresh UTÁN,
    // hogy a látott audio-eszközök is benne legyenek.
    tanara::logStartupDiagnostics(controller);

    return app.exec();
}
