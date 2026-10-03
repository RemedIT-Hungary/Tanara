#pragma once
//
// ShellRecorderHost — az új főablak ÖSSZES felvevő-kötése egy helyen (cserélhető réteg).
//
// A meglévő Widgets-felvevőt (RecordBar + FloatingRecorder) és a RecorderSingletont
// (a `tanara --record …` továbbított kérései) burkolja, változtatás nélkül. A QmlShellBridge
// csak ezt a felületet ismeri, így az új QML-felvevő (tanara_qml::RecorderWindowHost)
// beolvasztásakor elég ezt az osztályt lecserélni:
//   open()            — a felvevő elő / előtérbe (ShellActions.openRecorder)
//   startListening()  — a singleton-socket figyelése (ha más élő példány fogja, nem figyel,
//                       és NEM veszi el tőle)
//   shutdown()        — kilépés előtt: felvevő-ablak, singleton, szintfigyelés lezárása
//   hidden()          — a felvevő ablaka elrejtőzött (a híd dönt a főablak visszahozásáról)
// A felvétel közbeni „Vége a meetingnek?” kérdést egyelőre a régi RecordBar teszi fel a
// saját üzenetablakában (a felvevő fölött).
//
#include <QObject>
#include <QStringList>

namespace tanara { class AppController; }

namespace tanara_gui {

class RecordBar;
class FloatingRecorder;
class RecorderSingleton;

class ShellRecorderHost : public QObject {
    Q_OBJECT
public:
    explicit ShellRecorderHost(tanara::AppController* controller, QObject* parent = nullptr);
    ~ShellRecorderHost() override;

    void open();
    void startListening();
    void retryListening();          // felvétel vége után: ha eddig más fogta a nevet
    void refreshFromSettings();     // a Beállítások elfogadása után
    void shutdown();

signals:
    // A felvevő-ablak bezárása ELŐTT (még látszik): a híd ilyenkor hozhatja vissza a rejtett
    // főablakot, hogy ne tűnjön el az utolsó látható ablak felvétel közben.
    void aboutToHide();

private:
    void ensureCreated();
    void dock();
    void handleRequest(const QStringList& args);

    tanara::AppController* m_controller = nullptr;
    RecordBar* m_recordBar = nullptr;
    FloatingRecorder* m_floatingRecorder = nullptr;
    RecorderSingleton* m_singleton = nullptr;
    QString m_pendingContext;   // továbbított --context → recordingFinished-nél a meetingre
    bool m_shutDown = false;
};

} // namespace tanara_gui
