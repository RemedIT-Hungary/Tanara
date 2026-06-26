#include "FloatingRecorder.h"
#include "RecordBar.h"

#include "tanara/AppController.h"

#include <QVBoxLayout>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QWindow>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>

namespace tanara_gui {

FloatingRecorder::FloatingRecorder(tanara::AppController* controller,
                                   QWidget* recordBar, QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint)
    , m_controller(controller)
    , m_recordBar(recordBar) {

    setWindowTitle(QStringLiteral("Tanara — Felvétel"));
    // Ne semmisüljön meg X-re magától: a MainWindow dokkolja vissza és törli.
    setAttribute(Qt::WA_DeleteOnClose, false);

    m_wayland = QGuiApplication::platformName()
                    .contains(QStringLiteral("wayland"), Qt::CaseInsensitive);

    auto* root = new QVBoxLayout(this);
    // Keret nélküli ablak: a külső layout ne tegyen rá MÉG egy keret-térközt a RecordBar
    // saját margója fölé (különben dupla térköz / „extra hely" a tetején-oldalt).
    root->setContentsMargins(0, 0, 0, 0);

    // A beágyazott vezérlő — NINCS külön felső sáv: a felvétel-állapotot a piros
    // felvétel-gomb mutatja, a tálcára/bezárás a címsor —/✕ gombja, a visszadokkolás
    // pedig az ablak bezárásával (✕ → closeEvent → dockRequested) történik.
    if (m_recordBar) {
        m_recordBar->setParent(this);
        root->addWidget(m_recordBar);
        m_recordBar->show();
    }
    // Az ablak a TARTALOMRA méretez: kompakt induláskor, és automatikusan nő, ha a
    // hangforrás-lista lenyílik (a levelsBox megjelenik).
    root->setSizeConstraint(QLayout::SetMinimumSize);

    // A hangforrás-lista le/felnyitásakor (vagy újraépülésekor) a RecordBar jelez →
    // az ablakot a tartalomra méretezzük. A SetMinimumSize-constraint csak a MINIMUMot
    // követi, az ablakot nem zsugorítja vissza magától → összecsukáskor üres sáv maradna.
    if (auto* rb = qobject_cast<RecordBar*>(m_recordBar)) {
        connect(rb, &RecordBar::requestResizeToFit, this,
                &FloatingRecorder::fitHeightToContent);
        // Keret nélküli ablak → a KDE-keret helyett a RecordBar saját —/✕ vezérlőit
        // mutatjuk. — = tálcára; ✕ = bezárás (= visszadokkolás, a closeEvent útján).
        rb->setWindowControlsVisible(true);
        connect(rb, &RecordBar::minimizeRequested, this, &QWidget::showMinimized);
        connect(rb, &RecordBar::closeRequested, this, [this]() { close(); });
    }

    // MINDIG felül — nincs rá kapcsoló (a felvevőnek nincs értelme háttérbe mennie).
    // A flaget a Window | FramelessWindowHint mellé tesszük; a ctorban közvetlenül,
    // hogy a legelső megjelenítéskor (MainWindow show()) már érvényes legyen.
    // (Wayland alatt a kompozitor felülbírálhatja; ha gond, KWin-ablakszabály kell —
    // de NEM teszünk rá ablak-tooltipet, mert az minden gyermek-widgeten felugrana.)
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    setMinimumWidth(420);
    // Magasságot a SetMinimumSize-constraint adja (tartalomra fitt); csak a szélességet
    // kérjük szélesebbre, hogy a cím-mező + felvétel-gomb kényelmesen elférjen.
    resize(480, sizeHint().height());

    // Utolsó pozíció visszaállítása (ha érvényes és teljesen képernyőn van).
    restoreWindowPosition();
}

void FloatingRecorder::saveWindowPosition() {
    // Wayland: a kliens nem ismeri/állítja az abszolút pozíciót → ne mentsünk hamis
    // értéket (KWin maga jegyezheti meg). Csak látható ablakra mentünk (a pre-show
    // pozíció szemét lehet).
    if (m_wayland || !isVisible())
        return;
    QSettings().setValue(QStringLiteral("recorder/pos"), pos());
}

void FloatingRecorder::restoreWindowPosition() {
    if (m_wayland)
        return;   // a kompozitor kezeli a placementet (lásd KWin-ablakszabály)
    const QVariant v = QSettings().value(QStringLiteral("recorder/pos"));
    if (!v.isValid())
        return;
    const QRect target(v.toPoint(), QSize(width(), sizeHint().height()));
    // Csak akkor állítjuk vissza, ha az ablak TELJESEN elfér valamelyik képernyő
    // elérhető területén. Felbontásváltás / monitorleválás után a régi hely kilóghat
    // (részben vagy egészen) → ilyenkor marad az alapértelmezett (WM-)placement, hogy a
    // felhasználó ne zárhassa ki magát a használatból.
    for (const QScreen* sc : QGuiApplication::screens()) {
        if (sc->availableGeometry().contains(target)) {
            move(target.topLeft());
            return;
        }
    }
}

void FloatingRecorder::fitHeightToContent() {
    if (m_fitting)
        return;   // a lenti resize() újra resizeEvent-et vált ki → ne pörögjön végtelenbe
    m_fitting = true;
    if (layout())
        layout()->activate();                  // friss sizeHint hide/show/wrap után
    const int target = sizeHint().height();    // tartalom-magasság az AKTUÁLIS szélességnél
    if (height() != target)
        resize(width(), target);               // szélesség marad, magasság a tartalomra
    m_fitting = false;
}

void FloatingRecorder::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // Kézi nyújtás (vízszintes VAGY függőleges) után a magasságot visszasimítjuk a
    // tartalomra: szélességet a felhasználó állíthat, de függőleges holt-sáv nem marad.
    fitHeightToContent();
}

void FloatingRecorder::mousePressEvent(QMouseEvent* event) {
    // Keret nélküli ablak mozgatása: a háttéren (a cím-label / gombok NEM nyelik el a
    // megfelelő eseményeket) bal gombbal HÚZVA a kompozitor mozgatja az ablakot. A
    // startSystemMove-ot NEM itt, hanem csak elhúzás-küszöb átlépésekor hívjuk
    // (mouseMoveEvent), különben a cím-label dupla kattintása / a sima kattintás elveszne.
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        m_pressPos = event->globalPosition().toPoint();
        event->accept();   // implicit grab → megkapjuk a mozgás-eseményeket
        return;
    }
    QWidget::mousePressEvent(event);
}

void FloatingRecorder::mouseMoveEvent(QMouseEvent* event) {
    if (m_pressed && (event->buttons() & Qt::LeftButton) && windowHandle()) {
        const int dist = (event->globalPosition().toPoint() - m_pressPos).manhattanLength();
        if (dist >= 8) {              // elhúzás-küszöb (≈ start-drag távolság)
            m_pressed = false;
            windowHandle()->startSystemMove();   // innentől a kompozitor mozgat
        }
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void FloatingRecorder::mouseReleaseEvent(QMouseEvent* event) {
    m_pressed = false;
    QWidget::mouseReleaseEvent(event);
}

void FloatingRecorder::moveEvent(QMoveEvent* event) {
    QWidget::moveEvent(event);
    saveWindowPosition();   // minden mozgatás után megjegyezzük az utolsó pozíciót
}

void FloatingRecorder::closeEvent(QCloseEvent* event) {
    // Az X NEM dob el felvételt: visszadokkolunk a főablakba.
    emit dockRequested();
    event->ignore();
}

} // namespace tanara_gui
