#pragma once
//
// RecorderDemoFacade: the fictional implementation of tanara::recording::IRecorderFacade for
// --demo, --gallery, --qml-shot and the tests. No audio device, no file, no settings are touched.
//
// Fixture::Recorder serves the recorder's design states ("R01" ... "R10", RecorderPreview.qml):
// six made-up devices with pinned levels and statuses, a made-up tag pool, and a session that
// presets the title, tags, elapsed time, the open "End of the meeting?" question or the finished
// state. start() / stop() / beginNewSession() move between the design states the way the old
// view-model demo did (R01 -> R03 -> R09 -> R01).
//
// Fixture::SettingsList serves the five made-up devices of Settings' "Recording" page.
#include "tanara/recording/IRecorderFacade.h"

#include <QHash>
#include <QStringList>

namespace tanara_demo {

class RecorderDemoFacade : public tanara::recording::IRecorderFacade {
    Q_OBJECT
public:
    enum class Fixture { Recorder, SettingsList };

    // designState empty: no design state (no devices, an empty session).
    explicit RecorderDemoFacade(const QString& designState = {}, Fixture fixture = Fixture::Recorder,
                                QObject* parent = nullptr);

    QString designState() const { return m_design; }
    // Loads a design state; emits sessionChanged() and devicesChanged().
    void setDesignState(const QString& state);

    // ---- IRecorderFacade ----
    QVector<tanara::AudioDeviceInfo> captureDevices() const override;
    QString friendlyName(const QString& deviceName) const override;
    std::optional<tanara::recording::DevicePin> pinnedDisplay(const QString& deviceName) const override;
    QString appForOutput(const QString& deviceName) const override;
    bool autoRecordAllDevices() const override { return false; }
    QStringList knownDevices() const override;
    void setKnownDevices(const QStringList&) override {}
    QStringList lastUsedDeviceNames() const override { return m_lastUsed; }
    void setLastUsedDeviceNames(const QStringList& names) override { m_lastUsed = names; }
    void refreshDevices() override {}
    void startLevelMonitoring() override {}
    void stopLevelMonitoring() override {}

    tanara::RecordingState state() const override { return m_recording ? tanara::RecordingState::Recording
                                                                       : tanara::RecordingState::Idle; }
    QString currentFolder() const override { return {}; }
    QStringList recordingDeviceNames() const override { return m_recorded; }
    QStringList disconnectedDeviceNames() const override { return {}; }
    void start(const QString& title, const QVector<tanara::AudioDeviceInfo>& devices) override;
    void stop() override;
    bool addDevice(const tanara::AudioDeviceInfo& device) override;
    bool stopDevice(const QString& deviceName) override;
    void setRecordingTags(const QStringList&) override {}
    // The protection is in effect in every design state; tests switch it to "off" / "unavailable".
    QString keepAwakeState() const override { return m_keepAwake; }
    void setKeepAwakeState(const QString& state)
    {
        if (m_keepAwake == state) return;
        m_keepAwake = state;
        emit keepAwakeStateChanged();
    }
    void beginNewSession() override;

    void renameMeeting(const QString&, const QString&) override {}
    void setMeetingContextNote(const QString&, const QString&) override {}
    void setMeetingDetectedCall(const QString&, const QString&) override {}

    tanara::recording::RecorderTag createTag(const QString& name) override;
    std::optional<tanara::recording::RecorderTag> tag(const QString& id) const override;

    // The tag input's data: the made-up tag pool (one per facade).
    tanara::library::TagBackend* tagBackend() const override;

    tanara::recording::RecorderSession session() const override { return m_session; }

private:
    struct Device {
        tanara::AudioDeviceInfo info;
        QString friendly;
        QString app;
        tanara::recording::DevicePin pin;
        bool selected = false;
    };
    void loadRecorderState(const QString& st);
    void loadSettingsList();
    const Device* find(const QString& name) const;

    Fixture m_fixture;
    QString m_design;
    QVector<Device> m_devices;
    QStringList m_lastUsed;
    QStringList m_recorded;
    bool m_recording = false;
    QString m_keepAwake{QStringLiteral("held")};
    tanara::recording::RecorderSession m_session;
    QHash<QString, tanara::recording::RecorderTag> m_createdTags;
    mutable tanara::library::TagBackend* m_tagBackend = nullptr;
};

} // namespace tanara_demo
