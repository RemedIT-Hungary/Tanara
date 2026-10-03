#pragma once
//
// ShellRecorderHost — az új főablak ÖSSZES felvevő-kötése egy helyen.
//
// Az új QML-felvevőt (tanara_qml::RecorderWindowHost: RecorderWindow.qml + RecorderViewModel)
// és a RecorderSingletont (a `tanara --record …` továbbított kérései) burkolja. A
// QmlShellBridge csak ezt a felületet ismeri:
//   open()            — a felvevő elő / előtérbe (ShellActions.openRecorder)
//   startListening()  — a singleton-socket figyelése (ha más élő példány fogja, nem figyel,
//                       és NEM veszi el tőle)
//   shutdown()        — kilépés előtt: felvevő-ablak, singleton, szintfigyelés lezárása
//   hidden()          — a felvevő ablaka eltűnt (háttérbe küldve vagy bezárva); a híd dönt a
//                       rejtett főablak visszahozásáról
// A felvevő SAJÁT QML-motort kap (mint az önálló `tanara --record` folyamatban): így az
// élettartama nem függ a főablak motorjától. A téma közös, mert az `App` singleton
// folyamat-szintű (AppContext::instance()) — mindkét motor Theme-je ugyanazt követi.
//
// Amit a felvevő maga intéz (itt NEM kell még egyszer): a recording.lock (felvétel
// indulásakor felveszi, a végén elengedi), a továbbított --context a megbeszélésre, a
// felvétel közbeni átnevezés, a „Vége a megbeszélésnek?” kérdés (R06) és a bezárás felvétel
// közben (R07). Az automatikus lekeverés az elemzőben BEKAPCSOLVA marad (AppController
// alapértelmezés) — csak az önálló felvevő-folyamat kapcsolja ki.
//
#include <QObject>
#include <QStringList>

namespace tanara { class AppController; }
namespace tanara_qml {
class RecorderWindowHost;
struct RecorderRequest;
}

namespace tanara_gui {

class RecorderSingleton;
struct RecorderArgs;

// A `--record` argumentumok → a QML-felvevő kérése (main.cpp felvevő-mód + továbbított kérés).
tanara_qml::RecorderRequest toRecorderRequest(const RecorderArgs& args);

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

    bool isVisible() const;
    // A felvevő ablaka (RecorderWindow.qml; nullptr, amíg nem nyílt meg) — a QA-szkripteknek.
    QObject* window() const;

signals:
    // A felvevő ablaka eltűnt (háttérbe küldték vagy bezárták).
    void hidden();
    void openMeetingRequested(const QString& meetingId);   // „Megnyitás az elemzőben”
    void settingsRequested();                              // „Rögzítés beállításai”

private:
    void ensureHost();
    void handleRequest(const QStringList& args);
    void releaseMonitorIfUnused();

    tanara::AppController* m_controller = nullptr;
    tanara_qml::RecorderWindowHost* m_host = nullptr;
    RecorderSingleton* m_singleton = nullptr;
    bool m_shutDown = false;
};

} // namespace tanara_gui
