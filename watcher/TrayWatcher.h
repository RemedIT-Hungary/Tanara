#pragma once
//
// TrayWatcher — a rendszertálcára dokkolt meeting-figyelő. Időnként (settings:
// detectorIntervalSec) megnézi a detektor-maggal, folyik-e hívás, és felugró
// értesítéssel felajánlja a rögzítést. Csak figyel — a rögzítéshez a `tanara --record`
// folyamatot indítja (nem tartalmaz AppControllert / audio-capture-t).
//
#include <QObject>
#include <QString>
#include <QStringList>

class QSystemTrayIcon;
class QTimer;

namespace tanara {
class SettingsManager;
class IMeetingDetector;
}

namespace tanara_watcher {

class TrayWatcher : public QObject {
    Q_OBJECT
public:
    explicit TrayWatcher(QObject* parent = nullptr);
    ~TrayWatcher() override;

    // Detektor + tray felállítása. false, ha nincs elérhető detektor vagy nincs tálca.
    bool start();

private slots:
    void poll();                 // időzített detektor-lekérdezés
    void startRecordingNow();    // menü → tanara --record (azonnal rögzít)
    void openRecorder();         // menü/értesítés → tanara --record --no-start (megnyit, nem indít)
    void openAnalyzer();         // menü → a sima tanara (elemző) megnyitása
    // A D-Bus notification akció-gombjai / test-kattintása (csak TANARA_HAVE_DBUS mellett él).
    void onNotifyActionInvoked(uint id, const QString& actionKey);
    void onNotifyClosed(uint id, uint reason);

private:
    // Hívás-notification: Linuxon freedesktop D-Bus akció-gombokkal ("Rögzítés azonnali
    // indítása" / "Rögzítő megnyitása"), különben QSystemTrayIcon::showMessage-fallback.
    void showCallNotification(const QString& appName, const QString& windowTitle);
    QStringList recordArgs(bool immediate) const;  // --record [+ --no-start] + a detektált cím/kontextus
    QString tanaraBinary() const;             // a sibling `tanara` binary feloldása
    QString lockPath() const;                 // ~/.tanara/recording.lock (a settings metaDir-jéből)
    void launch(const QStringList& args) const;
    void applyAutostart(bool on) const;       // ~/.config/autostart/*.desktop (Linux)
    void updateTrayTooltip(bool recording, const QString& detectedApp);

    tanara::SettingsManager*  m_settings = nullptr;
    tanara::IMeetingDetector* m_detector = nullptr;   // owned (delete a dtorban)
    QSystemTrayIcon* m_tray = nullptr;
    QTimer*          m_timer = nullptr;

    // Debounce: meetingenként egyszer ajánlunk. Rising edge-en (inaktív→aktív) vagy új
    // session (más sourceRef) ajánl; inaktívvá válva reset.
    bool    m_wasActive = false;
    QString m_lastOfferedRef;

    // A legutóbb detektált hívás (a „Felvétel indítása" innen veszi a címet/kontextust).
    QString m_detAppName, m_detWindowTitle;

    // Az utolsó saját D-Bus notification id-ja (0 = nincs) — az ActionInvoked ez alapján
    // szűr, és az új értesítés ezt cseréli le (replaces_id).
    quint32 m_notifyId = 0;
};

} // namespace tanara_watcher
