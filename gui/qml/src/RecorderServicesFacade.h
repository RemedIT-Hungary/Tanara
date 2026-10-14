#pragma once
//
// RecorderServicesFacade: the real tanara::recording::IRecorderFacade, composed from the
// recording / library services and the settings (no AppController). One per controller, built by
// AppContext::servicesFor().
//
#include "tanara/recording/IRecorderFacade.h"

#include <QPointer>
#include <QTimer>

namespace tanara {
namespace app { class SettingsManager; }
namespace library { class MeetingRepository; class TagService; }
namespace recording { class RecordingService; }
namespace watcher { class PlaybackRouteMonitor; }
}

namespace tanara_qml {

class RecorderServicesFacade : public tanara::recording::IRecorderFacade {
    Q_OBJECT
public:
    struct Deps {
        tanara::recording::RecordingService* recording = nullptr;
        tanara::library::MeetingRepository* store = nullptr;
        tanara::library::TagService* tags = nullptr;
        tanara::app::SettingsManager* settings = nullptr;
        tanara::library::TagBackend* tagBackend = nullptr;   // the tag input's data (shared)
    };
    explicit RecorderServicesFacade(const Deps& deps, QObject* parent = nullptr);
    ~RecorderServicesFacade() override;

    QVector<tanara::AudioDeviceInfo> captureDevices() const override;
    QString friendlyName(const QString& deviceName) const override;
    QString appForOutput(const QString& deviceName) const override;
    bool autoRecordAllDevices() const override;
    QStringList knownDevices() const override;
    void setKnownDevices(const QStringList& names) override;
    QStringList lastUsedDeviceNames() const override;
    void setLastUsedDeviceNames(const QStringList& names) override;
    void refreshDevices() override;
    void startWatching() override;
    void stopWatching() override;
    void startLevelMonitoring() override;
    void stopLevelMonitoring() override;

    tanara::RecordingState state() const override;
    QString currentFolder() const override;
    QStringList recordingDeviceNames() const override;
    QStringList disconnectedDeviceNames() const override;
    void start(const QString& title, const QVector<tanara::AudioDeviceInfo>& devices) override;
    void stop() override;
    bool addDevice(const tanara::AudioDeviceInfo& device) override;
    bool stopDevice(const QString& deviceName) override;
    void setRecordingTags(const QStringList& tagIds) override;

    void renameMeeting(const QString& meetingId, const QString& title) override;
    void setMeetingContextNote(const QString& meetingId, const QString& note) override;
    void setMeetingDetectedCall(const QString& meetingId, const QString& appName) override;

    tanara::recording::RecorderTag createTag(const QString& name) override;
    std::optional<tanara::recording::RecorderTag> tag(const QString& id) const override;

    QString windowStatePath() const override;
    QString lockFilePath() const override;
    tanara::library::TagBackend* tagBackend() const override { return m_deps.tagBackend; }

private:
    Deps m_deps;
    QPointer<tanara::watcher::PlaybackRouteMonitor> m_routes;
    QTimer m_fallbackScan;   // where there is no route watcher: timed re-enumeration
    int m_watchers = 0;
};

} // namespace tanara_qml
