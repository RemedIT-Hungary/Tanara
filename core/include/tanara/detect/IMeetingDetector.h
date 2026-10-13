#pragma once
//
// IMeetingDetector: the headless port of meeting/call detection.
// The reliable signal is NOT a running process but that a call app is CURRENTLY holding the
// microphone (an active capture stream). The running app / window title is SECONDARY: only for the
// meeting's name/context. A separate backend per platform (Linux: pw-dump;
// Windows: WASAPI; macOS: CoreAudio). Headless (core); does NOT link Qt Widgets.
//
// Poll-based (NOT an async job): poll() is cheap and synchronous; the caller (the watcher)
// drives it with a QTimer at the desired interval.
#include <QString>
#include <QStringList>

namespace tanara {

// One detection snapshot. active=false = no (recognized) meeting.
struct MeetingSignal {
    bool    active = false;   // a known call app is actively holding the microphone (not ours)
    QString appId;            // normalized key, e.g. "zoom", "teams", "discord"
    QString appName;          // human-readable name for the meeting title, e.g. "Zoom"
    QString windowTitle;      // best-effort window title (secondary: meeting name/context)
    QString sourceRef;        // diagnostics: which stream/session matched
};

class IMeetingDetector {
public:
    virtual ~IMeetingDetector() = default;

    // Stable id (e.g. "linux-capture", "windows-wasapi", "macos-coreaudio").
    virtual QString id() const = 0;

    // Synchronous, cheap (typically a few ms). No meeting -> {active:false}.
    // MANDATORY: our own binary's ('tanara') capture (while recording) must NEVER be taken as a
    // meeting (self-exclusion), otherwise the recorder would detect itself.
    virtual MeetingSignal poll() = 0;

    // Whether this detector can run on this host now (the needed tool is available, the endpoint can be
    // discovered). The registry uses this for createBest().
    virtual bool isAvailable() const = 0;

    // Optional customization (the watcher calls it from the settings): which apps count as call apps,
    // and what our own binary is (for self-exclusion). Default: a no-op; the platform detectors override it
    // if they support it. They also work without it, with built-in defaults.
    virtual void configure(const QStringList& knownCallApps, const QString& selfBinary)
    {
        Q_UNUSED(knownCallApps);
        Q_UNUSED(selfBinary);
    }
};

} // namespace tanara
