#pragma once
//
// PlaybackRouting: "which application is playing to which output right now" + the change of the
// audio devices set (a hot-plug signal). The recorder uses it to print e.g. "> Microsoft Teams"
// under a device row, and to learn cheaply whether the devices must be re-enumerated.
//
// Linux: PipeWire (`pw-dump`): Stream/Output/Audio node -> Link -> Audio/Sink node. This graph also
// lives under the PulseAudio-compatible layer (pipewire-pulse).
// Windows / macOS: not implemented (WASAPI audio sessions would be per endpoint): available() is
// false, the snapshot is empty; the caller then simply does not print an app.
//
// Headless (core); does NOT link Qt Gui/Widgets.
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

namespace tanara {

struct PlaybackRoute {
    QString appName;           // human-readable ("Microsoft Teams", "Firefox")
    QString appBinary;         // application.process.binary (lowercase)
    QString sinkName;          // the output's node.name (e.g. alsa_output.usb-...)
    QString sinkDescription;   // description of the output; the monitor device's name is "Monitor of <this>"
    bool    running = false;   // the stream is playing now (not paused)
};

struct AudioGraphSnapshot {
    QVector<PlaybackRoute> routes;
    // Sorted key list of the audio devices (Audio/Sink + Audio/Source nodes): if it changes, a device
    // was plugged / unplugged and must be re-enumerated.
    QStringList deviceKeys;

    // Name of the application currently playing to the given capture device (an output's monitor /
    // loopback, by the miniaudio name, e.g. "Monitor of Kanto YU4 - ..."); among several, the one actually
    // sounding comes first. Empty if nobody plays there (or the device is not an output monitor).
    QString appForOutput(const QString& captureDeviceName) const;
    // Conversely: which output the given app (a name or binary fragment) plays to (description).
    QString outputForApp(const QString& appNameOrBinary) const;
};

namespace detail {
// PURE function (unit-testable): the playback routes from the JSON of `pw-dump`.
AudioGraphSnapshot parsePwDumpGraph(const QByteArray& json);
} // namespace detail

// One-time, SYNCHRONOUS query (for the watcher's notification). Empty on an unavailable platform.
AudioGraphSnapshot queryAudioGraph(int timeoutMs = 1500);

// Timed, NON-blocking watcher (pw-dump runs as an asynchronous QProcess).
class PlaybackRouteMonitor : public QObject {
    Q_OBJECT
public:
    explicit PlaybackRouteMonitor(QObject* parent = nullptr);
    ~PlaybackRouteMonitor() override;

    // Whether there is an implementation on this host (Linux + pw-dump on the PATH).
    static bool available();

    AudioGraphSnapshot snapshot() const;

public slots:
    void start(int intervalMs = 2000);
    void stop();

signals:
    void routesChanged();      // another app plays to another output
    void deviceSetChanged();   // a device appeared / disappeared

private:
    void poll();
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace tanara
