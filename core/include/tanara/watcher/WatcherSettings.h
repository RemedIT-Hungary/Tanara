#pragma once
//
// The "watcher" settings section: the meeting watcher (background detector + tray) and the
// call-end / silence questions during recording.
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace tanara::watcher {

struct WatcherSettings {
    bool detectorEnabled = true;     // active-call detection on/off
    int  detectorIntervalSec = 8;    // poll interval (s); sensible range 5-30
    bool autostart = false;          // whether the watcher starts at login
    // During recording the recorder also watches the call: when it ends (the app quits / releases
    // the microphone), it asks whether to stop. It never stops by itself.
    bool askStopOnCallEnd = true;
    // Silence watch during recording: if ALL tracks stay silent this many minutes, it asks
    // whether to stop (INDEPENDENT of the detector). 0 = off.
    int  silenceAskMinutes = 3;
    QString detectorId;              // empty = MeetingDetectorRegistry::createBest()
    // Known call apps (binary/app-name fragments); the detector signals on these.
    QStringList knownCallApps{
        QStringLiteral("zoom"), QStringLiteral("teams"), QStringLiteral("webex"),
        QStringLiteral("slack"), QStringLiteral("discord"), QStringLiteral("meet"),
        QStringLiteral("skype"), QStringLiteral("chromium"), QStringLiteral("firefox")};

    QJsonObject toJson() const;
    static WatcherSettings fromJson(const QJsonObject& o);
};

} // namespace tanara::watcher
