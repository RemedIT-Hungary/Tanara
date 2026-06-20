// Tanara GUI (Qt Widgets) — belépési pont.
#include "MainWindow.h"
#include "AppIcon.h"

#include "tanara/AppController.h"
#include "tanara/Logging.h"

#include <QApplication>

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

    tanara::AppController controller;

    tanara_gui::MainWindow window(&controller);
    window.show();

    // Eszközök felsorolása indításkor (→ devicesChanged → eszközlista feltöltése).
    controller.refreshDevices();

    // Induló diagnosztika (fejléc info, részletek debug szinten) — a refresh UTÁN,
    // hogy a látott audio-eszközök is benne legyenek.
    tanara::logStartupDiagnostics(controller);

    return app.exec();
}
