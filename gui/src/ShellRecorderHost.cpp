#include "ShellRecorderHost.h"

#include "FloatingRecorder.h"
#include "RecordBar.h"
#include "RecorderSingleton.h"

#include "tanara/AppController.h"

namespace tanara_gui {

ShellRecorderHost::ShellRecorderHost(tanara::AppController* controller, QObject* parent)
    : QObject(parent), m_controller(controller)
{
    // A továbbított --context a felvétel végén kerül a megbeszélésre.
    connect(m_controller, &tanara::AppController::recordingFinished, this,
            [this](const tanara::Meeting& m) {
                if (!m_pendingContext.isEmpty()) {
                    m_controller->setMeetingContextNote(m.id, m_pendingContext);
                    m_pendingContext.clear();
                }
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

void ShellRecorderHost::ensureCreated()
{
    if (m_recordBar)
        return;
    // A RecordBar parentless, a FloatingRecorder ctora reparentálja magába. A létrejöttekor
    // (üresjáratban) elindítja az élő szintfigyelést, hogy a VU-sávok mozogjanak.
    m_recordBar = new RecordBar(m_controller, nullptr);
    m_recordBar->setViewMode(RecordBar::ViewMode::Full);
    connect(m_controller, &tanara::AppController::devicesChanged,
            m_recordBar, &RecordBar::onDevicesChanged);
    connect(m_controller, &tanara::AppController::recordingStateChanged,
            m_recordBar, &RecordBar::onRecordingStateChanged);
    connect(m_controller, &tanara::AppController::elapsedChanged,
            m_recordBar, &RecordBar::onElapsedChanged);
    connect(m_controller, &tanara::AppController::levelMeterUpdated,
            m_recordBar, &RecordBar::onLevelMeterUpdated);
    connect(m_controller, &tanara::AppController::deviceLevel,
            m_recordBar, &RecordBar::onDeviceLevel);

    // parent=nullptr → ÖNÁLLÓ top-level ablak (saját tálca-bejegyzés, nem minimalizálódik a
    // főablakkal).
    m_floatingRecorder = new FloatingRecorder(m_controller, m_recordBar, nullptr);
    connect(m_floatingRecorder, &FloatingRecorder::dockRequested, this, &ShellRecorderHost::dock);
}

void ShellRecorderHost::open()
{
    if (m_shutDown)
        return;
    ensureCreated();
    m_recordBar->refreshFromSettings();   // a Beállítások közben változhattak
    // A felvevő elrejtésekor a szintfigyelést leállítjuk (ne fogjuk a mikrofont, amíg csak
    // visszanézünk) — újranyitáskor, üresjáratban, újraindul.
    if (m_controller->recordingState() == tanara::RecordingState::Idle)
        m_controller->startLevelMonitoring();
    m_recordBar->show();
    m_floatingRecorder->show();
    m_floatingRecorder->raise();
    m_floatingRecorder->activateWindow();
}

void ShellRecorderHost::dock()
{
    // „Dokkolás” = a leválasztott felvétel-ablak elrejtése. A RecordBar a FloatingRecorderben
    // marad, így egy futó felvétel / állapot megmarad; az „Új felvétel” újra előhozza.
    if (!m_floatingRecorder)
        return;
    emit aboutToHide();
    m_floatingRecorder->hide();
    if (m_controller->recordingState() == tanara::RecordingState::Idle)
        m_controller->stopLevelMonitoring();
}

void ShellRecorderHost::handleRequest(const QStringList& args)
{
    const RecorderArgs r = parseRecorderArgs(args);
    open();   // elő / előtérbe — mindig UGYANAZ a felvevő
    if (!r.noStart && m_recordBar
        && m_controller->recordingState() == tanara::RecordingState::Idle) {
        m_pendingContext = r.context.trimmed();
        m_recordBar->startWithTitle(r.title);
    }
}

void ShellRecorderHost::refreshFromSettings()
{
    if (m_recordBar)
        m_recordBar->refreshFromSettings();   // a felvevő tükrözze az új eszköz-policyt
}

void ShellRecorderHost::shutdown()
{
    if (m_shutDown)
        return;
    m_shutDown = true;
    if (m_singleton)
        m_singleton->close();
    // Biztosítjuk, hogy a capture-eszközök elengedésre kerüljenek kilépéskor.
    m_controller->stopLevelMonitoring();
    // A különálló (parent nélküli) lebegő felvevőt kézzel zárjuk, különben a főablak
    // bezárása után is kint maradna (és életben tartaná a folyamatot).
    if (m_floatingRecorder) {
        m_floatingRecorder->disconnect(this);
        m_floatingRecorder->close();
        delete m_floatingRecorder;   // a benne lévő RecordBar-t is elviszi
        m_floatingRecorder = nullptr;
        m_recordBar = nullptr;
    }
}

} // namespace tanara_gui
