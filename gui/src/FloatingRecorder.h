#pragma once
//
// FloatingRecorder — a felvétel-vezérlő (RecordBar) leválasztható, önálló
// felső-szintű ablaka. Tálcázható (Qt::Window), opcionálisan mindig-felül,
// és felvétel közben villogó REC-jelzést mutat. A RecordBar-t NEM birtokolja:
// dokkoláskor a MainWindow visszahelyezi a fő elrendezésbe.
//
#include "tanara/Types.h"
#include <QWidget>

class QLabel;
class QTimer;
class QCloseEvent;

namespace tanara {
class AppController;
}

namespace tanara_gui {

class FloatingRecorder : public QWidget {
    Q_OBJECT
public:
    // recordBar: a beágyazandó vezérlő (a ctor reparentálja magába). A "Mindig felül"
    // kapcsoló a RecordBar ⋮-menüjéből jön (alwaysOnTopAction) — itt kötjük be a
    // tényleges WindowStaysOnTopHint kezeléssel; a régi külön pin-checkbox megszűnt.
    FloatingRecorder(tanara::AppController* controller, QWidget* recordBar,
                     QWidget* parent = nullptr);

signals:
    void dockRequested();   // "Vissza a főablakba" vagy ablak-bezárás

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onAlwaysOnTopToggled(bool on);
    void onRecordingStateChanged(tanara::RecordingState state);
    void onBlink();

private:
    tanara::AppController* m_controller = nullptr;
    QWidget*   m_recordBar = nullptr;     // not owned
    QLabel*    m_recIndicator = nullptr;
    QTimer*    m_blinkTimer = nullptr;
    bool       m_blinkOn = false;
};

} // namespace tanara_gui
