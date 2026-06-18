// Tanara GUI (Qt Widgets) — belépési pont.
#include "MainWindow.h"

#include "tanara/AppController.h"

#include <QApplication>

int main(int argc, char** argv) {
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
