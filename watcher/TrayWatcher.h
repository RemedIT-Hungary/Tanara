#pragma once
//
// TrayWatcher — a rendszertálcára dokkolt meeting-figyelő. Időnként (settings:
// detectorIntervalSec) megnézi a detektor-maggal, folyik-e hívás, és felugró
// értesítéssel felajánlja a rögzítést. Csak figyel — a rögzítéshez a `tanara --record`
// folyamatot indítja (nem tartalmaz AppControllert / audio-capture-t).
//
// A tálca-ikon három állapota (design/handoff-recorder R11): Figyel · Hívás észlelve ·
// Felvétel fut. A buborék az appot, ill. az eltelt időt mutatja; a menü felvétel közben
// fejlécet („Felvétel · 00:12:47”) és leállítást is ad. A „Kilépés…” felvétel közben rákérdez.
//
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>

class QAction;
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

    // „HH:MM:SS” az eltelt másodpercekből (a buborékhoz és a menü-fejléchez).
    static QString formatElapsed(qint64 seconds);
    // A hívás-értesítés szövege; outputName: a kimenet, amelyre az app épp szól ("" = nem tudjuk).
    static QString callNotificationBody(const QString& appName, const QString& outputName);

private slots:
    void poll();                 // időzített detektor-lekérdezés
    void refreshState();         // ikon + buborék + menü a lock és az utolsó jel alapján (1 mp)
    void startRecordingNow();    // menü/értesítés → tanara --record (azonnal rögzít)
    void openRecorder();         // menü/értesítés → tanara --record --no-start (megnyit / előhoz)
    void stopRecording();        // menü → tanara --record --stop (a futó felvevőnek továbbítva)
    void openAnalyzer();         // menü → a sima tanara (elemző) megnyitása
    void openSettings();         // menü → tanara --settings watcher (a Beállítások „Hívásfigyelő” lapja)
    void quitRequested();        // „Kilépés…” — felvétel közben rákérdez
    // A D-Bus notification akció-gombjai / test-kattintása (csak TANARA_HAVE_DBUS mellett él).
    void onNotifyActionInvoked(uint id, const QString& actionKey);
    void onNotifyClosed(uint id, uint reason);

private:
    // Hívás-notification: Linuxon freedesktop D-Bus akció-gombokkal („Felvétel indítása” /
    // „Felvevő megnyitása” / „Nem most”), különben QSystemTrayIcon::showMessage-fallback.
    void showCallNotification(const QString& appName);
    QStringList recordArgs(bool immediate) const;  // --record [+ --no-start] + az észlelt app/kontextus
    QString tanaraBinary() const;             // a sibling `tanara` binary feloldása
    QString lockPath() const;                 // <metaadat-mappa>/recording.lock
    void launch(const QStringList& args) const;
    void applyAutostart(bool on) const;       // Linux: autostart .desktop; Windows: HKCU Run
    // A settings.json megváltozott (a Beállítások mentett): újratöltés + a figyelő
    // átállítása újraindítás nélkül (be/ki, gyakoriság, figyelt appok, autostart).
    void reloadSettingsIfChanged();
    void applySettings();

    tanara::SettingsManager*  m_settings = nullptr;
    tanara::IMeetingDetector* m_detector = nullptr;   // owned (delete a dtorban)
    QSystemTrayIcon* m_tray = nullptr;
    QTimer*          m_timer = nullptr;
    QTimer*          m_stateTimer = nullptr;   // 1 mp: eltelt idő + a lock figyelése
    QDateTime        m_settingsStamp;          // a settings.json utolsó látott módosítási ideje
    bool             m_autostartApplied = false;   // az utoljára alkalmazott autostart-állapot

    QAction* m_headerAction = nullptr;    // „Felvétel · 00:12:47” (csak felvétel közben)
    QAction* m_showAction = nullptr;      // Felvevő megjelenítése
    QAction* m_startAction = nullptr;     // Felvétel indítása (ha nem fut)
    QAction* m_stopAction = nullptr;      // Felvétel leállítása (ha fut)
    int      m_iconState = -1;            // az utoljára beállított ikon-állapot

    // Debounce: meetingenként egyszer ajánlunk. Rising edge-en (inaktív→aktív) vagy új
    // session (más sourceRef) ajánl; inaktívvá válva reset.
    bool    m_wasActive = false;
    QString m_lastOfferedRef;

    // A legutóbb detektált hívás (a „Felvétel indítása" innen veszi az appot/kontextust).
    QString m_detAppName, m_detWindowTitle;
    bool    m_callActive = false;

    // Az utolsó saját D-Bus notification id-ja (0 = nincs) — az ActionInvoked ez alapján
    // szűr, és az új értesítés ezt cseréli le (replaces_id).
    quint32 m_notifyId = 0;
};

} // namespace tanara_watcher
