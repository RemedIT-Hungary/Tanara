#include "FloatingRecorder.h"
#include "RecordBar.h"

#include "tanara/AppController.h"

#include <QVBoxLayout>
#include <QAction>
#include <QCloseEvent>
#include <QSignalBlocker>
#include <QGuiApplication>

namespace tanara_gui {

FloatingRecorder::FloatingRecorder(tanara::AppController* controller,
                                   QWidget* recordBar, QWidget* parent)
    : QWidget(parent, Qt::Window)
    , m_controller(controller)
    , m_recordBar(recordBar) {

    setWindowTitle(QStringLiteral("Tanara — Felvétel"));
    // Ne semmisüljön meg X-re magától: a MainWindow dokkolja vissza és törli.
    setAttribute(Qt::WA_DeleteOnClose, false);

    auto* root = new QVBoxLayout(this);

    // A beágyazott vezérlő — NINCS külön felső sáv: a felvétel-állapotot a piros
    // felvétel-gomb mutatja, a "Mindig felül" a ⋮-menüben van, a visszadokkolás pedig
    // az ablak bezárásával (X → closeEvent → dockRequested) történik.
    if (m_recordBar) {
        m_recordBar->setParent(this);
        root->addWidget(m_recordBar);
        m_recordBar->show();
    }
    // Az ablak a TARTALOMRA méretez: kompakt induláskor, és automatikusan nő, ha a
    // „Szintek"/„Sávok" lenyílik (a levelsBox megjelenik).
    root->setSizeConstraint(QLayout::SetMinimumSize);

    // "Mindig felül" — a RecordBar ⋮-menüjének checkable action-jéhez kötjük.
    if (auto* rb = qobject_cast<RecordBar*>(m_recordBar)) {
        if (QAction* aot = rb->alwaysOnTopAction()) {
            aot->setVisible(true);   // lebegve releváns (dokkoláskor a MainWindow elrejti)
            if (QGuiApplication::platformName().contains(QStringLiteral("wayland"), Qt::CaseInsensitive))
                aot->setToolTip(QStringLiteral(
                    "Wayland alatt a kompozitor felülbírálhatja — ha nem marad felül, "
                    "használj KWin-ablakszabályt erre az ablakra."));
            connect(aot, &QAction::toggled, this, &FloatingRecorder::onAlwaysOnTopToggled);
        }
    }

    // Alapból mindig-felül: a flaget KÖZVETLENÜL állítjuk (ne hívjunk show()-t a
    // ctorban — azt a MainWindow teszi; a flag így a legelső megjelenítéskor érvényes).
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    if (auto* rb = qobject_cast<RecordBar*>(m_recordBar)) {
        if (QAction* aot = rb->alwaysOnTopAction()) {
            QSignalBlocker block(aot);
            aot->setChecked(true);
        }
    }
    setMinimumWidth(420);
    // Magasságot a SetMinimumSize-constraint adja (tartalomra fitt); csak a szélességet
    // kérjük szélesebbre, hogy a cím-mező + felvétel-gomb kényelmesen elférjen.
    resize(480, sizeHint().height());
}

void FloatingRecorder::onAlwaysOnTopToggled(bool on) {
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    // A flag-változás után az ablakot újra meg kell jeleníteni (Qt-követelmény).
    show();
}

void FloatingRecorder::closeEvent(QCloseEvent* event) {
    // Az X NEM dob el felvételt: visszadokkolunk a főablakba.
    emit dockRequested();
    event->ignore();
}

} // namespace tanara_gui
