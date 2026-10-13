#pragma once
//
// SettingsDeviceModel: the device list of Settings' "Recording" page: the same three groups
// and rows as in the recorder (toggle, friendly + raw name, "default", 14-segment level
// meter with peak hold), plus renaming the device.
//
// The toggles set the "default sources" draft (selection shared with the recorder,
// state.json), the names the draft's `deviceNames` table; both take effect when the
// SettingsViewModel saves. The level comes from the core's level monitoring
// (AppController::deviceLevelPeak), the scale is the recorder's (RecorderViewModel::levelSegments).
//
#include "tanara/domain/Types.h"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class SettingsViewModel;

class SettingsDeviceModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
public:
    enum Role {
        KeyRole = Qt::UserRole + 1,   // the raw OS device name (stable key)
        NameRole,                     // display name (the user's or the abbreviated one)
        RawNameRole,
        DefaultNameRole,              // the abbreviated factory name (to undo a rename)
        RenamedRole,                  // the user gave it a name
        GroupRole,                    // 0 microphone, 1 audio output, 2 other input
        GroupFirstRole,
        SelectedRole,                 // default source
        DefaultRole,                  // the system's default device
        LevelRole,                    // 0..14 segments
        PeakRole,                     // peak-hold segment index (-1 = none)
    };

    explicit SettingsDeviceModel(SettingsViewModel* vm);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Re-reads the devices from the core (or the demo list), with the draft's names.
    void rebuild();
    // The names / selection changed from the draft (discard, external save).
    void refreshFromDraft();
    void toggle(int row);
    void rename(int row, const QString& name);
    void onLevel(const QString& deviceName, float rms, float peak);
    void setTicking(bool on);
    // The raw names of the present devices (saving overwrites the selection with these).
    QStringList presentNames() const;

private:
    struct Row {
        tanara::AudioDeviceInfo info;
        int group = 0;
        int level = 0;
        int peakSeg = -1;
        qint64 peakAt = 0;
        bool demoFixed = false;
    };
    void tick();
    void loadDemo();
    int rowOf(const QString& name) const;

    SettingsViewModel* m_vm;
    QVector<Row> m_rows;
    QTimer m_tick;
    QElapsedTimer m_clock;
};

} // namespace tanara_qml
