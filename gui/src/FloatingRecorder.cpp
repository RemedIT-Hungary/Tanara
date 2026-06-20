#include "FloatingRecorder.h"
#include "RecordBar.h"

#include "tanara/AppController.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QAction>
#include <QPushButton>
#include <QTimer>
#include <QCloseEvent>
#include <QFont>
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

    // --- felső sor: REC-jelzés (villogó pötty) + dokkolás ---
    // A "Mindig felül" a RecordBar ⋮-menüjébe került (a régi külön pin-checkbox helyett).
    auto* top = new QHBoxLayout();
    m_recIndicator = new QLabel(QStringLiteral("●"), this);
    m_recIndicator->setToolTip(QStringLiteral("Felvétel állapotjelző"));
    m_recIndicator->setStyleSheet(QStringLiteral("color: gray;"));
    QFont rf = m_recIndicator->font();
    rf.setPointSizeF(rf.pointSizeF() * 1.3);
    m_recIndicator->setFont(rf);

    auto* dockBtn = new QPushButton(QStringLiteral("Vissza a főablakba"), this);

    top->addWidget(m_recIndicator);
    top->addStretch(1);
    top->addWidget(dockBtn);
    root->addLayout(top);

    // --- a beágyazott vezérlő ---
    if (m_recordBar) {
        m_recordBar->setParent(this);
        root->addWidget(m_recordBar, 1);
        m_recordBar->show();
    }

    connect(dockBtn, &QPushButton::clicked, this, [this]() { emit dockRequested(); });

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

    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setInterval(500);
    connect(m_blinkTimer, &QTimer::timeout, this, &FloatingRecorder::onBlink);

    if (m_controller) {
        connect(m_controller, &tanara::AppController::recordingStateChanged,
                this, &FloatingRecorder::onRecordingStateChanged);
        onRecordingStateChanged(m_controller->recordingState());
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
    setMinimumWidth(300);
    resize(360, 300);   // kompakt alapméret (a RecordBar lebegőben Kompakt módban van)
}

void FloatingRecorder::onAlwaysOnTopToggled(bool on) {
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    // A flag-változás után az ablakot újra meg kell jeleníteni (Qt-követelmény).
    show();
}

void FloatingRecorder::onRecordingStateChanged(tanara::RecordingState state) {
    if (state == tanara::RecordingState::Recording) {
        m_blinkOn = false;
        m_blinkTimer->start();
    } else {
        m_blinkTimer->stop();
        m_recIndicator->setStyleSheet(QStringLiteral("color: gray;"));
    }
}

void FloatingRecorder::onBlink() {
    m_blinkOn = !m_blinkOn;
    m_recIndicator->setStyleSheet(m_blinkOn
        ? QStringLiteral("color: #e00000;")          // élénk piros
        : QStringLiteral("color: rgba(224,0,0,40);")); // halvány (villog)
}

void FloatingRecorder::closeEvent(QCloseEvent* event) {
    // Az X NEM dob el felvételt: visszadokkolunk a főablakba.
    emit dockRequested();
    event->ignore();
}

} // namespace tanara_gui
