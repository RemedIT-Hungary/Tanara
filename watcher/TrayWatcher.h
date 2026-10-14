#pragma once
// TrayWatcher: the meeting watcher docked in the system tray. At intervals (settings:
// detectorIntervalSec) it asks the detector core whether a call is in progress and offers
// to record via a pop-up notification. It only watches: to record it launches the
// `tanara --record` process (it contains no AppController / audio capture).
//
// The tray icon has three states: Watching, Call detected, Recording. The balloon shows the
// app, or the elapsed time; during a recording the menu also gets a header ("Recording ·
// 00:12:47") and a stop item. "Quit..." asks for confirmation during a recording.
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>

class QAction;
class QSystemTrayIcon;
class QTimer;

namespace tanara {
namespace app { class SettingsManager; }
namespace watcher { class IMeetingDetector; }
}

namespace tanara_watcher {

class TrayWatcher : public QObject {
    Q_OBJECT
public:
    explicit TrayWatcher(QObject* parent = nullptr);
    ~TrayWatcher() override;

    // Sets up the detector + tray. false if no detector is available or there is no tray.
    bool start();

    // The UI language from settings.json (uiLanguage: "auto"|"hu"|"en"), for installAppTranslator().
    static QString configuredUiLanguage();

    // "HH:MM:SS" from elapsed seconds (for the balloon and the menu header).
    static QString formatElapsed(qint64 seconds);
    // The call notification text; outputName: the output the app is playing to ("" = unknown).
    static QString callNotificationBody(const QString& appName, const QString& outputName);

private slots:
    void poll();                 // timed detector poll
    void refreshState();         // icon + balloon + menu from the lock and the last signal (1 s)
    void startRecordingNow();    // menu/notification: tanara --record (records immediately)
    void openRecorder();         // menu/notification: tanara --record --no-start (opens / raises)
    void stopRecording();        // menu: tanara --record --stop (forwarded to the running recorder)
    void openAnalyzer();         // menu: opens plain tanara (the analyzer)
    void openSettings();         // menu: tanara --settings watcher (the "Call watcher" page of Settings)
    void quitRequested();        // "Quit..." asks for confirmation during a recording
    // D-Bus notification action buttons / body click (only active with TANARA_HAVE_DBUS).
    void onNotifyActionInvoked(uint id, const QString& actionKey);
    void onNotifyClosed(uint id, uint reason);

private:
    // Call notification: on Linux via freedesktop D-Bus with action buttons ("Start recording" /
    // "Open recorder" / "Not now"), otherwise the QSystemTrayIcon::showMessage fallback.
    void showCallNotification(const QString& appName);
    QStringList recordArgs(bool immediate) const;  // --record [+ --no-start] + the detected app/context
    QString tanaraBinary() const;             // resolves the sibling `tanara` binary
    QString lockPath() const;                 // <state folder>/recording.lock
    void launch(const QStringList& args) const;
    void applyAutostart(bool on) const;       // Linux: autostart .desktop; Windows: HKCU Run
    // settings.json changed (Settings saved): reload and reconfigure the watcher without a
    // restart (on/off, interval, watched apps, autostart).
    void reloadSettingsIfChanged();
    void applySettings();

    tanara::app::SettingsManager*  m_settings = nullptr;
    tanara::watcher::IMeetingDetector* m_detector = nullptr;   // owned (deleted in the dtor)
    QSystemTrayIcon* m_tray = nullptr;
    QTimer*          m_timer = nullptr;
    QTimer*          m_stateTimer = nullptr;   // 1 s: elapsed time + watches the lock
    QDateTime        m_settingsStamp;          // last seen modification time of settings.json
    bool             m_autostartApplied = false;   // the last applied autostart state

    QAction* m_headerAction = nullptr;    // "Recording · 00:12:47" (only while recording)
    QAction* m_showAction = nullptr;      // Show recorder
    QAction* m_startAction = nullptr;     // Start recording (if not running)
    QAction* m_stopAction = nullptr;      // Stop recording (if running)
    int      m_iconState = -1;            // the last icon state set

    // Debounce: one offer per meeting. Offers on a rising edge (inactive to active) or a new
    // session (different sourceRef); resets when it goes inactive.
    bool    m_wasActive = false;
    QString m_lastOfferedRef;

    // The most recently detected call ("Start recording" takes the app/context from here).
    QString m_detAppName, m_detWindowTitle;
    bool    m_callActive = false;

    // Id of our last own D-Bus notification (0 = none); ActionInvoked filters on it, and a new
    // notification replaces it (replaces_id).
    quint32 m_notifyId = 0;
};

} // namespace tanara_watcher
