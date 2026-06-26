#pragma once
//
// FloatingRecorder — a felvétel-vezérlő (RecordBar) leválasztható, önálló
// felső-szintű ablaka. Tálcázható (Qt::Window), opcionálisan mindig-felül.
// Nincs külön felső sáv: a felvétel-állapotot a piros felvétel-gomb mutatja, a
// visszadokkolás az ablak bezárásával történik. A RecordBar-t NEM birtokolja:
// dokkoláskor a MainWindow visszahelyezi a fő elrendezésbe.
//
#include "tanara/Types.h"
#include <QWidget>
#include <QPoint>

class QCloseEvent;
class QResizeEvent;
class QMouseEvent;
class QMoveEvent;

namespace tanara {
class AppController;
}

namespace tanara_gui {

class FloatingRecorder : public QWidget {
    Q_OBJECT
public:
    // recordBar: a beágyazandó vezérlő (a ctor reparentálja magába). Az ablak
    // keret nélküli (frameless) és MINDIG felül van (nincs rá kapcsoló — nincs rá
    // szükség, hogy háttérbe menjen); a saját —/✕ vezérlők a RecordBar címsorában.
    FloatingRecorder(tanara::AppController* controller, QWidget* recordBar,
                     QWidget* parent = nullptr);

signals:
    void dockRequested();   // "Vissza a főablakba" vagy ablak-bezárás

protected:
    void closeEvent(QCloseEvent* event) override;
    // A magasságot MINDEN átméretezéskor a tartalomra simítjuk (a szélesség szabad),
    // hogy kézi nyújtás után se maradjon kitölthetetlen holt-sáv alul.
    void resizeEvent(QResizeEvent* event) override;
    // Keret nélküli ablak: a háttér (nem-interaktív terület) húzásával mozgatható,
    // a kompozitorra bízva (startSystemMove — Wayland/X11 alatt is működik). A mozgatást
    // csak ELHÚZÁS-küszöb után indítjuk (nem puszta lenyomásra), hogy a cím-label dupla
    // kattintása (szerkesztés) és a sima kattintások ne vesszenek el.
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    // Pozíció megjegyzése: minden mozgatáskor elmentjük, induláskor visszaállítjuk —
    // de CSAK ha a mentett hely teljesen elfér egy képernyőn (off-screen → default).
    void moveEvent(QMoveEvent* event) override;

private:
    void fitHeightToContent();   // magasság = tartalom sizeHint-je (szélesség marad)
    void saveWindowPosition();   // QSettings: recorder/pos (Wayland alatt no-op)
    void restoreWindowPosition();// induláskor, off-screen-védelemmel

    tanara::AppController* m_controller = nullptr;
    QWidget*   m_recordBar = nullptr;     // not owned
    bool       m_fitting = false;         // rekurzió-gát a resizeEvent → resize hurokra
    bool       m_wayland = false;         // Wayland: a kompozitor kezeli a pozíciót → nem mentünk/állítunk
    bool       m_pressed = false;         // bal gomb lenyomva (elhúzás-küszöb figyeléséhez)
    QPoint     m_pressPos;                // a lenyomás globális pozíciója (küszöb-méréshez)
};

} // namespace tanara_gui
