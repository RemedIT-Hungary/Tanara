#pragma once
//
// RecordBar — felvétel-vezérlő sáv: Start/Stop gomb, cím-mező, eltelt idő,
// szintmérők (sávonként), és többszörös eszközválasztó élő VU-sávval.
//
#include "tanara/Types.h"
#include <QWidget>
#include <QVector>
#include <QHash>

class QLineEdit;
class QPushButton;
class QToolButton;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QVBoxLayout;
class QGroupBox;
class QProgressBar;
class QCheckBox;
class QMenu;
class QAction;

namespace tanara {
class AppController;
}

QT_BEGIN_NAMESPACE
namespace Ui { class RecordBar; }
QT_END_NAMESPACE

namespace tanara_gui {

class RecordBar : public QWidget {
    Q_OBJECT
public:
    // Teljes: minden eszköz, checkbox, beállítás. Kompakt: csak a kiválasztott
    // eszközök (név + szint), a részletek elrejtve (sarokba illő lebegő vezérlő).
    enum class ViewMode { Full, Compact };

    explicit RecordBar(tanara::AppController* controller, QWidget* parent = nullptr);
    ~RecordBar() override;

    ViewMode viewMode() const { return m_mode; }
    void setViewMode(ViewMode mode);

    // Az ablak-menü (⋮) "Mindig felül" kapcsolója. A FloatingRecorder ezen
    // keresztül köti be magát (a tényleges WindowStaysOnTopHint kezelése ott marad),
    // így a régi külön pin-checkbox helyett a menüből vezérelhető. Dokkolt (Full)
    // nézetben a menüpont rejtett (nincs lebegő ablak, amire vonatkozna).
    QAction* alwaysOnTopAction() const { return m_alwaysOnTopAct; }

signals:
    void viewModeChanged(ViewMode mode);

public slots:
    void onDevicesChanged();
    void onRecordingStateChanged(tanara::RecordingState state);
    void onElapsedChanged(qint64 ms);
    void onLevelMeterUpdated(int trackIndex, float rms);
    void onDeviceLevel(QString deviceName, float rms);

private slots:
    void onStartStopClicked();

private:
    void rebuildDeviceList();
    void saveSelection();   // a bepipált eszközöket azonnal perzisztálja
    QVector<tanara::AudioDeviceInfo> selectedDevices() const;
    void resetDeviceLevelBars();
    void applyViewMode();   // a m_mode szerint mutat/rejt elemeket
    void updateRecordButton();   // a kétállapotú (piros/semleges, kétsoros) gomb frissítése
    void setLevelsVisible(bool on);   // a VU-doboz mutatása/rejtése a "▸ Szintek" togglehoz
    void updateVoicesLabel();   // "● N hangot hallok" frissítése a kiválasztott eszközszámból

    Ui::RecordBar* ui = nullptr;
    tanara::AppController* m_controller = nullptr;
    tanara::RecordingState m_state = tanara::RecordingState::Idle;
    ViewMode m_mode = ViewMode::Full;

    QLineEdit*   m_titleEdit = nullptr;
    QPushButton* m_recordBtn = nullptr;     // egyesített, kétállapotú felvétel-gomb
    QToolButton* m_menuBtn = nullptr;       // ⋮ ablak-menü
    QToolButton* m_levelsToggle = nullptr;  // ▸ Szintek (VU-doboz mutat/rejt)
    QToolButton* m_tracksToggle = nullptr;  // ▸ Rögzítendő sávok…
    QLabel*      m_voicesLabel = nullptr;
    QGroupBox*   m_levelsBox = nullptr;      // a VU-doboz (alapból rejtve)
    QLabel*      m_devHint = nullptr;
    QListWidget* m_deviceList = nullptr;

    QMenu*   m_windowMenu = nullptr;         // a ⋮-gomb menüje
    QAction* m_alwaysOnTopAct = nullptr;     // "Mindig felül" (a FloatingRecorder köti be)

    qint64 m_elapsedMs = 0;                  // utolsó eltelt idő (a gomb 2. sorához)
    bool   m_levelsVisible = false;          // a VU-doboz aktuális láthatósága

    // Egy-egy eszköz-sor vezérlői, eszköznév szerint kulcsolva (a deviceLevel és
    // a lastUsedDeviceNames is NÉV alapú).
    struct DeviceRow {
        QCheckBox*       check = nullptr;
        QProgressBar*    level = nullptr;
        QListWidgetItem* item = nullptr;   // a sor (kompakt módban rejthető)
    };
    QHash<QString, DeviceRow> m_deviceRows;     // deviceName -> sor
    QVector<QListWidgetItem*> m_headerItems;    // csoportfejek (kompaktban rejtve)

    // Felvétel közben a sáv-index → eszköznév leképezés (a per-sáv szintet a
    // megfelelő eszköz VU-sávjába vezetjük, így nincs külön „Szintek" doboz).
    QVector<QString> m_recordingDeviceNames;
};

} // namespace tanara_gui
