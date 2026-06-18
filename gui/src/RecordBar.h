#pragma once
//
// RecordBar — felvétel-vezérlő sáv: Start/Stop gomb, cím-mező, eltelt idő,
// szintmérők (sávonként), és többszörös eszközválasztó (checkbox-os lista).
//
#include "tanara/Types.h"
#include <QWidget>
#include <QVector>
#include <QHash>

class QLineEdit;
class QPushButton;
class QLabel;
class QListWidget;
class QVBoxLayout;
class QProgressBar;

namespace tanara {
class AppController;
}

namespace tanara_gui {

class RecordBar : public QWidget {
    Q_OBJECT
public:
    explicit RecordBar(tanara::AppController* controller, QWidget* parent = nullptr);

public slots:
    void onDevicesChanged();
    void onRecordingStateChanged(tanara::RecordingState state);
    void onElapsedChanged(qint64 ms);
    void onLevelMeterUpdated(int trackIndex, float rms);

private slots:
    void onStartStopClicked();

private:
    void rebuildDeviceList();
    QVector<tanara::AudioDeviceInfo> selectedDevices() const;
    QProgressBar* meterForTrack(int trackIndex);

    tanara::AppController* m_controller = nullptr;
    tanara::RecordingState m_state = tanara::RecordingState::Idle;

    QLineEdit*   m_titleEdit = nullptr;
    QPushButton* m_startStopBtn = nullptr;
    QLabel*      m_elapsedLabel = nullptr;
    QListWidget* m_deviceList = nullptr;

    QWidget*               m_metersHost = nullptr;
    QVBoxLayout*           m_metersLayout = nullptr;
    QHash<int, QProgressBar*> m_meters;   // trackIndex -> progress bar
};

} // namespace tanara_gui
