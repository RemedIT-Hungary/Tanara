#pragma once
//
// RecorderWindowHost — a lebegő QML-felvevő (RecorderWindow.qml) C++ gazdája.
//
// Egy folyamatban EGY példány elég. Kétféleképp használható:
//  - önállóan (`tanara --record …`): a main.cpp létrehozza, request()-tel átadja a
//    parancssori kérést, és a closed() jelre kilép;
//  - a főalkalmazásban: a gui/src ShellRecorderHost ugyanígy létrehozza (ott is saját
//    motorral — a téma az `App` singletonon át közös), a „Felvétel” gombra show()-t, a
//    továbbított `--record` kérésre request()-et hív.
//
// A gazda intézi, amit a QML nem tud: mindig-felül, pozíció megjegyzése képernyőnként,
// a pirula él-illesztése, háttérbe küldés, a recording.lock, és hogy a felvétel-kérdés
// (R06) rejtett / pirula ablaknál előhozza az ablakot és rendszerértesítést kérjen.
// Widgets-függősége nincs (tálca-ikont, értesítést a befoglaló folyamat ad a jelekre).
//
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>

class QQmlEngine;
class QQuickWindow;

namespace tanara {
class AppController;
class RecordingLock;
}

namespace tanara_qml {

class RecorderViewModel;

// Egy felvevő-kérés (parancssor: --record [--title T | --app A] [--context C]
// [--device IDX]… [--no-start] [--stop]).
struct RecorderRequest {
    QString title;              // kifejezett cím (nem „automatikus név”)
    QString appName;            // az észlelt hívás-app → automatikus név: „Teams-hívás · …”
    QString context;            // a meeting kontextus-megjegyzése
    QList<int> deviceIndexes;   // a capture-lista sorszámai; üres = a mentett kijelölés
    bool start = true;          // azonnali indítás (ha üresjáratban van)
    bool stop = false;          // a futó felvétel leállítása (ablak-előhozás nélkül)
};

class RecorderWindowHost : public QObject {
    Q_OBJECT
public:
    // engine: a QML-motor, amelyben az ablak létrejön; nullptr → saját motort készít.
    explicit RecorderWindowHost(tanara::AppController* controller, QQmlEngine* engine = nullptr,
                                QObject* parent = nullptr);
    ~RecorderWindowHost() override;

    // Az ablak megmutatása és előtérbe hozása (első hívásra létre is hozza). false: QML-hiba.
    bool show();
    void hide();
    bool isVisible() const;

    // Egy kérés végrehajtása: megmutat, címet / forrásokat állít, és (start) indít.
    void request(const RecorderRequest& request);

    bool recording() const;                 // fut-e felvétel (vagy épp lezárul)
    QQuickWindow* window() const;           // nullptr, amíg nem jött létre
    RecorderViewModel* viewModel() const;

    // Hamis: a „Háttérbe” gomb csak tálcára minimalizál (nincs, ami visszahozná az ablakot).
    void setHideToTrayEnabled(bool on) { m_hideEnabled = on; }
    // A recording.lock kezelése (felvétel indulásakor felvesz, a végén elenged). Alapból be.
    void setManageLock(bool on) { m_manageLock = on; }

    // Az önálló felvevő-folyamatban a QApplication ELŐTT hívandó: Qt Quick-beállítások
    // (stílus, szövegrajzolás) + a TANARA_RECORDER_X11=1 kapcsoló (Wayland → XWayland).
    static void prepareProcess();

    // A platform engedi-e a kliensnek a mindig-felült és az ablak-pozicionálást
    // (X11, Windows: igen; Wayland: nem — ott az ablakkezelő dönt).
    static bool platformCanPosition();

signals:
    void closed();                               // a felhasználó bezárta (nem fut felvétel)
    void hiddenToTray();                         // háttérbe küldve (a felvétel megy tovább)
    void shown();
    void openMeetingRequested(QString meetingId);   // „Megnyitás az elemzőben”
    void settingsRequested();                    // „Rögzítés beállításai”
    void notificationRequested(QString title, QString text);   // R06 rejtett/pirula ablaknál
    void recordingStarted(QString meetingFolder);
    void recordingFinished(QString meetingId);
    void stateChanged(QString state);            // a nézetmodell állapota (tálca-ikonhoz)

private slots:
    // A RecorderWindow.qml jelei / property-változásai.
    void onHideRequested();
    void onCloseRequested();
    void onUserMoved();
    void onPillChanged();
    void onPrefsChanged();

private:
    bool ensureWindow();
    void restoreGeometry();
    void savePosition();
    void snapPill();
    QString iniPath() const;

    QPointer<tanara::AppController> m_controller;
    QQmlEngine* m_engine = nullptr;
    bool m_ownEngine = false;
    QPointer<QQuickWindow> m_window;
    QPointer<RecorderViewModel> m_vm;
    std::unique_ptr<tanara::RecordingLock> m_lock;
    bool m_hideEnabled = true;
    bool m_manageLock = true;
    bool m_restoring = false;
    bool m_positioned = false;
};

} // namespace tanara_qml
