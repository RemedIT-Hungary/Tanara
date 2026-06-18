// Tanara GUI (Qt Widgets) — belépési pont.
#include "MainWindow.h"

#include "tanara/AppController.h"

#include <QApplication>
#include <QLoggingCategory>

int main(int argc, char** argv) {
    // Konzol-zaj csendesítése (külső libek). Audio-only lejátszás → ne próbáljon
    // videó-hardvergyorsítást (VDPAU/VAAPI) inicializálni az ffmpeg-backend.
    qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", QByteArray());
    // PipeWire/libspa enumerációs log szintje le (pl. az optikai/iec958 eszköz
    // formátum-egyeztetési „spaVisitChoice" üzenete).
    if (!qEnvironmentVariableIsSet("PIPEWIRE_DEBUG")) qputenv("PIPEWIRE_DEBUG", "0");
    // Qt Multimedia ffmpeg info-banner némítása.
    QLoggingCategory::setFilterRules(QStringLiteral("qt.multimedia.ffmpeg.info=false"));

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Tanara"));
    QApplication::setOrganizationName(QStringLiteral("RemedIT"));

    tanara::AppController controller;

    tanara_gui::MainWindow window(&controller);
    window.show();

    // Eszközök felsorolása indításkor (→ devicesChanged → eszközlista feltöltése).
    controller.refreshDevices();

    return app.exec();
}
