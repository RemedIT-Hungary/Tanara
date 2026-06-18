// Tanara GUI (Qt Widgets) — belépési pont.
#include "MainWindow.h"
#include "AppIcon.h"

#include "tanara/AppController.h"

#include <QApplication>
#include <QLoggingCategory>
#include <QDebug>

int main(int argc, char** argv) {
    // Konzol-zaj csendesítése (külső libek). Audio-only lejátszás → ne próbáljon
    // videó-hardvergyorsítást (VDPAU/VAAPI/D3D) inicializálni az ffmpeg-backend.
    qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", QByteArray());
    // Qt Multimedia ffmpeg info-banner némítása.
    QLoggingCategory::setFilterRules(QStringLiteral("qt.multimedia.ffmpeg.info=false"));

#if defined(Q_OS_LINUX)
    // PipeWire/libspa enumerációs log szintje le (pl. az optikai/iec958 eszköz
    // formátum-egyeztetési „spaVisitChoice" üzenete) — csak Linuxon releváns.
    if (!qEnvironmentVariableIsSet("PIPEWIRE_DEBUG")) qputenv("PIPEWIRE_DEBUG", "0");
    qInfo().noquote() << "Tanara: a konzolon esetenként ártalmatlan külső-library üzenetek "
                         "jelenhetnek meg (libvdpau – hiányzó NVIDIA VDPAU AMD gépen; libspa/PipeWire – "
                         "optikai eszköz formátum-egyeztetés). Ezek NEM hibák, a működést nem érintik.";
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

    return app.exec();
}
