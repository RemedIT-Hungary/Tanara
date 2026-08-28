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

    // Keret nélküli (frameless) lebegő ablakban a KDE-keret helyett a saját
    // —/✕ vezérlőinket mutatjuk. Dokkolt/beágyazott használatkor rejtve maradnak.
    void setWindowControlsVisible(bool on);

    // A Beállításokban megadott rögzítés-policy tükrözése a checkboxokra: auto-mód
    // bekapcsolva → minden auto-rögzítendő (nem line-in) forrás bepipálva; kikapcsolva
    // → a mentett lastUsedDeviceNames halmaz. A felvevő megnyitásakor hívjuk, hogy a
    // Beállítások és a felvevő ne csússzon szét.
    void refreshFromSettings();

signals:
    void viewModeChanged(ViewMode mode);
    void minimizeRequested();   // — gomb: tálcára (a FloatingRecorder showMinimized-et hív)
    void closeRequested();      // ✕ gomb: bezárás → visszadokkolás (a closeEvent-tel azonos út)
    // A tartalom függőleges mérete megváltozott (hangforrás-lista le/felnyitva vagy
    // újraépült) → a befoglaló lebegő ablak méretezze magát a tartalomra (ne maradjon
    // üres sáv összecsukáskor, és ne kelljen kézzel nagyítani+görgetni lenyitáskor).
    void requestResizeToFit();

public slots:
    void onDevicesChanged();
    void onRecordingStateChanged(tanara::RecordingState state);
    void onElapsedChanged(qint64 ms);
    void onLevelMeterUpdated(int trackIndex, float rms);
    void onDeviceLevel(QString deviceName, float rms);

protected:
    // A cím-label dupla kattintását figyeljük (→ szerkesztés-mód).
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    void onStartStopClicked();

private:
    void enterTitleEdit();      // label → input (fókusz + selectAll)
    void commitTitleEdit();     // input → label (a szöveg a titleEdit-ben marad)
    void updateTitleDisplay();  // a label szövegét a titleEdit aktuális szövegéből frissíti
    void rebuildDeviceList();
    void adjustDeviceListHeight();   // a hangforrás-listát a látható soraira méretezi (felső korláttal)
    void saveSelection();   // a bepipált eszközöket azonnal perzisztálja
    QVector<tanara::AudioDeviceInfo> selectedDevices() const;
    void resetDeviceLevelBars();
    void applyViewMode();   // a m_mode szerint mutat/rejt elemeket
    void updateRecordButton();   // a kétállapotú (piros/semleges, kétsoros) gomb frissítése
    void setLevelsVisible(bool on);   // a hangforrás-doboz mutatása/rejtése a toggle-hoz
    void updateVoicesLabel();   // "N hangforrás kiválasztva" / "Rögzítés — N hangforrás" frissítése

    Ui::RecordBar* ui = nullptr;
    tanara::AppController* m_controller = nullptr;
    tanara::RecordingState m_state = tanara::RecordingState::Idle;
    bool m_askingStop = false;   // a hívás-vég kérdés épp nyitva (ne duplázzunk)
    ViewMode m_mode = ViewMode::Full;

    QLabel*      m_titleLabel = nullptr;    // a cím alapból label (egyben húzó-fogantyú)
    QToolButton* m_editBtn = nullptr;       // ✏ cím-szerkesztés
    QLineEdit*   m_titleEdit = nullptr;     // a cím szerkesztő mezője (alapból rejtve)
    QPushButton* m_recordBtn = nullptr;     // egyesített, kétállapotú felvétel-gomb
    QToolButton* m_minBtn = nullptr;        // — tálcára (frameless ablak-vezérlő)
    QToolButton* m_closeBtn = nullptr;      // ✕ bezárás/visszadokk (frameless ablak-vezérlő)
    QToolButton* m_tracksToggle = nullptr;  // ▸ Rögzítendő hangforrások módosítása (VU-doboz mutat/rejt)
    QLabel*      m_voicesLabel = nullptr;
    QGroupBox*   m_levelsBox = nullptr;      // a VU-doboz (alapból rejtve)
    QLabel*      m_devHint = nullptr;
    QListWidget* m_deviceList = nullptr;

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
