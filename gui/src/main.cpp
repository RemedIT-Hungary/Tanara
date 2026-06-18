// Tanara GUI (Qt Widgets). MVP-ig placeholder ablak.
#include "tanara/Types.h"
#include <QApplication>
#include <QLabel>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QLabel w(QStringLiteral("Tanara %1 — scaffold").arg(tanara::libraryVersion()));
    w.setMinimumSize(420, 120);
    w.setAlignment(Qt::AlignCenter);
    w.show();
    return app.exec();
}
