#include "ShellRecorderHost.h"

#include "RecorderSingleton.h"
#include "RecorderViewModel.h"
#include "RecorderWindowHost.h"

#include "tanara/AppController.h"
#include "tanara/Logging.h"

#include <QQuickWindow>
#include <QTimer>

namespace tanara_gui {

tanara_qml::RecorderRequest toRecorderRequest(const RecorderArgs& ra)
{
    tanara_qml::RecorderRequest r;
    r.title = ra.title;
    r.appName = ra.app;
    r.context = ra.context;
    r.deviceIndexes = ra.deviceIdx;
    r.start = !ra.noStart;
    r.stop = ra.stop;
    return r;
}

ShellRecorderHost::ShellRecorderHost(tanara::AppController* controller, QObject* parent)
    : QObject(parent), m_controller(controller)
{
    // Felvétel vége rejtett felvevő mellett (pl. a tálcáról leállítva): a core a végén
    // újraindítja a szintfigyelést (a felvevő kérte) — ha nincs, aki nézze, elengedjük a
    // mikrofont. Sorba állítva, mert az újraindítás a jel UTÁN történik.
    connect(m_controller, &tanara::AppController::recordingStateChanged, this,
            [this](tanara::RecordingState st) {
                if (st == tanara::RecordingState::Idle)
                    QTimer::singleShot(0, this, &ShellRecorderHost::releaseMonitorIfUnused);
            });
}

ShellRecorderHost::~ShellRecorderHost()
{
    shutdown();
}

void ShellRecorderHost::startListening()
{
    // Singleton felvevő: amíg az elemző fut, MINDEN `tanara --record` kérés ide jön (a
    // figyelő / tálca sosem nyit második felvevőt). Ha a nevet másik élő példány fogja, az
    // marad a felvevő, mi nem figyelünk (és nem vesszük el tőle).
    if (!m_singleton) {
        m_singleton = new RecorderSingleton(this);
        connect(m_singleton, &RecorderSingleton::requestReceived,
                this, &ShellRecorderHost::handleRequest);
    }
    m_singleton->listen();
}

void ShellRecorderHost::retryListening()
{
    if (m_singleton && !m_singleton->isListening())
        m_singleton->listen();
}

void ShellRecorderHost::ensureHost()
{
    if (m_host)
        return;
    // Saját QML-motor (nullptr): a felvevő élettartama ne függjön a főablak motorjától. A
    // recording.lock-ot a gazda kezeli (alapértelmezés) — az elemzőben futó felvételt is
    // látnia kell a figyelőnek. A „Háttérbe” elrejti az ablakot: a főablak „Felvétel
    // folyamatban” gombja (vagy a figyelő tálca-ikonja) hozza vissza.
    m_host = new tanara_qml::RecorderWindowHost(m_controller, nullptr, this);
    m_host->setManageLock(true);
    m_host->setHideToTrayEnabled(true);
    using Host = tanara_qml::RecorderWindowHost;
    connect(m_host, &Host::hiddenToTray, this, &ShellRecorderHost::hidden);
    connect(m_host, &Host::closed, this, [this] {
        // Bezárva (nem fut felvétel): ne fogjuk a mikrofont, amíg a felvevő nem látszik —
        // újranyitáskor a szintfigyelés újraindul.
        releaseMonitorIfUnused();
        emit hidden();
    });
    connect(m_host, &Host::openMeetingRequested, this, &ShellRecorderHost::openMeetingRequested);
    connect(m_host, &Host::settingsRequested, this, &ShellRecorderHost::settingsRequested);
    // notificationRequested (R06 rejtett / pirula ablaknál): a gazda maga előhozza a felvevőt,
    // a kérdés ott jelenik meg — a főablak NEM kérdez még egyszer.
    // recordingFinished: a könyvtár az AppController::recordingFinished jelére frissül és
    // jelöl ki (ShellActions) — itt nincs második kezelés.
}

void ShellRecorderHost::open()
{
    if (m_shutDown)
        return;
    ensureHost();
    // Egy korábbi (rejtett ablak mellett véget ért) felvétel „Elmentve” állapota helyett az
    // újranyitott felvevő új felvételre kész.
    if (!m_host->isVisible() && m_host->viewModel()
        && m_host->viewModel()->state() == QLatin1String("done"))
        m_host->viewModel()->newRecording();
    if (!m_host->show()) {
        qCCritical(tanara::lcApp) << "A felvevő felülete nem tölthető be.";
        return;
    }
    // A felvevő elrejtésekor a szintfigyelést leállítjuk (ne fogjuk a mikrofont, amíg csak
    // visszanézünk) — újranyitáskor, üresjáratban, újraindul.
    if (m_controller->recordingState() == tanara::RecordingState::Idle)
        m_controller->startLevelMonitoring();
}

void ShellRecorderHost::handleRequest(const QStringList& args)
{
    if (m_shutDown)
        return;
    const RecorderArgs ra = parseRecorderArgs(args);
    if (ra.stop) {
        // A tálca-menü „Felvétel leállítása”: ablak-előhozás nélkül.
        if (m_host)
            m_host->request(toRecorderRequest(ra));
        return;
    }
    open();   // elő / előtérbe — mindig UGYANAZ a felvevő
    if (m_host && m_host->isVisible())
        m_host->request(toRecorderRequest(ra));   // cím / app / kontextus / források / indítás
}

void ShellRecorderHost::releaseMonitorIfUnused()
{
    if (m_shutDown || isVisible())
        return;
    if (m_controller->recordingState() == tanara::RecordingState::Idle)
        m_controller->stopLevelMonitoring();
}

void ShellRecorderHost::refreshFromSettings()
{
    // A felvevő eszközlistája tükrözze az új eszköz-policyt (devicesChanged → újraépül).
    if (m_host)
        m_controller->refreshDevices();
}

bool ShellRecorderHost::isVisible() const
{
    return m_host && m_host->isVisible();
}

QObject* ShellRecorderHost::window() const
{
    return m_host ? m_host->window() : nullptr;
}

void ShellRecorderHost::shutdown()
{
    if (m_shutDown)
        return;
    m_shutDown = true;
    if (m_singleton)
        m_singleton->close();
    // A felvevő-ablakot (önálló top-level) kézzel zárjuk, különben a főablak bezárása után is
    // kint maradna. A gazda destruktora a recording.lock-ot is elengedi.
    if (m_host) {
        m_host->disconnect(this);
        delete m_host;
        m_host = nullptr;
    }
    // Biztosítjuk, hogy a capture-eszközök elengedésre kerüljenek kilépéskor.
    m_controller->stopLevelMonitoring();
}

} // namespace tanara_gui
