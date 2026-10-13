#pragma once
//
// WindowsCaptureDetector: a WASAPI-based meeting detector for Windows (id: "windows-wasapi").
//
// Primary signal: the ACTIVE audio sessions of the recording (capture) endpoints. We go over the
// sessions of every active capture endpoint (IMMDeviceEnumerator::EnumAudioEndpoints(eCapture, ACTIVE))
// (IAudioSessionManager2 -> IAudioSessionEnumerator); an AudioSessionStateActive session = a process is
// holding the microphone right now. From the PID an image name (QueryFullProcessImageNameW), from that
// a lowercase base name, which we match against the known call apps (detail/WinCaptureRules.h).
//
// Secondary signal (if no session matches, e.g. packaged/UWP apps): the keys of
// HKCU\...\CapabilityAccessManager\ConsentStore\microphone: an app whose LastUsedTimeStart is later than
// LastUsedTimeStop (or Stop == 0) is using the microphone right now.
//
// Self-exclusion: our own PID and every process with an image name "tanara" / "tanara-*" is left out.
//
// The whole content is under Q_OS_WIN (core's CMakeLists uses GLOB, so it is an empty translation unit
// elsewhere). NOT a QObject (synchronous poll, no moc dependency); it follows the LinuxCaptureDetector pattern.
#include <QtGlobal>
#if defined(Q_OS_WIN)

#include "tanara/watcher/detect/IMeetingDetector.h"

#include <QString>
#include <QStringList>

#include <memory>

namespace tanara::watcher {

class WindowsCaptureDetector : public IMeetingDetector {
public:
    WindowsCaptureDetector();
    ~WindowsCaptureDetector() override;

    QString id() const override;
    MeetingSignal poll() override;
    bool isAvailable() const override;
    void configure(const QStringList& knownCallApps, const QString& selfBinary) override;

private:
    struct Impl;                       // COM objects (the header stays free of Windows headers)
    std::unique_ptr<Impl> d;
    QStringList m_knownApps;
    QString     m_selfBinary = QStringLiteral("tanara");
};

} // namespace tanara::watcher
#endif // Q_OS_WIN
