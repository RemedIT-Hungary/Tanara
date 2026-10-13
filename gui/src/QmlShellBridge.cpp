#include "QmlShellBridge.h"

#include "OnboardingViewModel.h"
#include "OnboardingWindowHost.h"
#include "PeopleWindowHost.h"
#include "SettingsWidgetsDialogs.h"
#include "SettingsWindowHost.h"
#include "ShellRecorderHost.h"
#include "TagsWindowHost.h"

#include "tanara/app/AppController.h"
#include "tanara/foundation/log/Logging.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/library/MeetingLibrary.h"
#include "tanara/library/store/MeetingStore.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QQuickWindow>
#include <QTimer>
#include <QWindow>

namespace tanara_gui {

namespace {

// Human-readable summary sentence: no name: "N different partners identified";
// with names: "A, B and X unknown partners" (X==0: names only).
QString participantsSummary(QStringList named, int unknownCount, int totalDistinct)
{
    named.removeDuplicates();
    if (named.isEmpty())
        return QCoreApplication::translate("QmlShellBridge", "%n különböző partner azonosítva",
                                           nullptr, totalDistinct);
    QString s = named.join(QStringLiteral(", "));
    if (unknownCount > 0)
        s += QCoreApplication::translate("QmlShellBridge", " és %n ismeretlen partner",
                                         nullptr, unknownCount);
    return s;
}

} // namespace

QmlShellBridge::QmlShellBridge(tanara::app::AppController* controller, QObject* parent)
    : tanara_qml::ShellBridge(parent), m_controller(controller)
{
    qApp->installEventFilter(this);

    // The recorder binding (the new QML recorder + singleton) lives in a separate class.
    m_recorder = new ShellRecorderHost(m_controller, this);
    // In background mode (hidden main window) the recorder is the last visible window; if it
    // disappears too (sent to the background / closed), the main window comes back so there is
    // something to work with.
    connect(m_recorder, &ShellRecorderHost::hidden, this, [this]() {
        if (mainWindowHidden()) emit showWindowRequested();
    });
    // The recorder's "Open in the analyzer" button: selects the meeting + raises the main window.
    connect(m_recorder, &ShellRecorderHost::openMeetingRequested, this, &QmlShellBridge::showMeeting);
    // The recorder's "Recording settings" button.
    connect(m_recorder, &ShellRecorderHost::settingsRequested, this,
            [this]() { openSettings(QStringLiteral("recording")); });
    // Recording ended: (a) "Stop and quit" lets the main window close; (b) in background mode
    // (hidden main window) the main window comes back; (c) retry the singleton listening.
    connect(m_controller, &tanara::app::AppController::recordingStateChanged, this,
            [this](tanara::RecordingState st) {
                if (st != tanara::RecordingState::Idle) return;
                if (m_quitAfterStop) { emit quitRequested(); return; }
                if (mainWindowHidden()) emit showWindowRequested();
                m_recorder->retryListening();
            });

    // The QML Settings window: non-modal; what depends on it is refreshed on the save signal.
    m_settingsDialogs = new SettingsWidgetsDialogs(m_controller, this);
    m_settingsDialogs->setPeopleOpener([this](const QString& person) { openPeopleAt(person); });
    m_people = new tanara_qml::PeopleWindowHost(m_controller, this);
    m_settingsDialogs->setTagsOpener([this](const QString& tagId) { openTagsAt(tagId); });
    // The Tags window: "Open in the library as a filter" filters the library + raises the main
    // window; a meeting link selects it + raises the main window.
    m_tags = new tanara_qml::TagsWindowHost(m_controller, this);
    connect(m_tags, &tanara_qml::TagsWindowHost::openInLibraryRequested, this,
            &QmlShellBridge::tagFilterRequested);
    connect(m_tags, &tanara_qml::TagsWindowHost::meetingRequested, this,
            &QmlShellBridge::showMeetingRequested);
    // A row of a tag's person list opens the People window on that person.
    connect(m_tags, &tanara_qml::TagsWindowHost::personRequested, this,
            [this](const QString& person) { openPeopleAt(person); });
    m_settings = new tanara_qml::SettingsWindowHost(m_controller, m_settingsDialogs, this);
    m_settings->setPersistTheme(false);   // the main window remembers the theme (themeModeSaved)
    connect(m_settings, &tanara_qml::SettingsWindowHost::saved, this, [this] {
        m_recorder->refreshFromSettings();    // the recorder reflects the new device policy / names
        emit readinessChanged();
    });
    connect(m_settings, &tanara_qml::SettingsWindowHost::themeModeSaved, this,
            &QmlShellBridge::themeModeSaved);
    // The missing setting is now in place: back to the meeting (the selection did not change).
    connect(m_settings, &tanara_qml::SettingsWindowHost::returnRequested, this,
            [this] { emit readinessChanged(); emit showMeetingRequested(QString()); });
    connect(m_settings, &tanara_qml::SettingsWindowHost::closed, this, [this] {
        emit readinessChanged();
    });

    // The "Getting started" window: "Set up now" opens Settings > Providers; the accepted theme
    // goes to the main window (it remembers it), and the saved step refreshes whether the views can run.
    m_onboarding = new tanara_qml::OnboardingWindowHost(m_controller, m_settingsDialogs, this);
    m_settingsDialogs->setOnboardingOpener([this] { openOnboarding(); });
    connect(m_onboarding, &tanara_qml::OnboardingWindowHost::openSettingsRequested, this,
            [this](const QString& page) { openSettings(page); });
    connect(m_onboarding, &tanara_qml::OnboardingWindowHost::themeModeSaved, this,
            &QmlShellBridge::themeModeSaved);
    connect(m_onboarding, &tanara_qml::OnboardingWindowHost::saved, this, [this] {
        m_recorder->refreshFromSettings();
        emit readinessChanged();
    });
    // A key / provider saved in Settings also refreshes the "Getting started" status row.
    connect(this, &QmlShellBridge::readinessChanged, this, [this] {
        if (m_onboarding->viewModel()) m_onboarding->viewModel()->refreshReadiness();
    });

}

QmlShellBridge::~QmlShellBridge()
{
    shutdown();
}

void QmlShellBridge::setMainWindow(QWindow* window)
{
    m_window = window;
}

bool QmlShellBridge::mainWindowHidden() const
{
    return m_window && !m_window->isVisible();
}

bool QmlShellBridge::eventFilter(QObject* watched, QEvent* event)
{
    // The QML main window is not a QWidget, so Widgets dialogs have no parent widget. When
    // shown, we tell the window manager they belong to the main window (stay above it, follow
    // it); the recorder (not a QDialog) stays a standalone window.
    if (event->type() == QEvent::Show && m_window && m_window->isVisible()) {
        auto* dialog = qobject_cast<QDialog*>(watched);
        if (dialog && dialog->isWindow() && !dialog->parentWidget()) {
            if (QWindow* handle = dialog->windowHandle())
                if (!handle->transientParent())
                    handle->setTransientParent(m_window);
        }
    }
    return tanara_qml::ShellBridge::eventFilter(watched, event);
}

// ---- Settings / People ----

void QmlShellBridge::openSettings(const QString& page)
{
    openSettingsAt(page, QString());
}

void QmlShellBridge::openSettingsAt(const QString& page, const QString& focusField)
{
    if (m_shutDown)
        return;
    m_settings->setTransientParent(m_window);
    if (!m_settings->open(page, focusField))
        return;
    m_settingsDialogs->setOwnerWindow(m_settings->window());
}

QObject* QmlShellBridge::settingsWindow() const
{
    return m_settings ? m_settings->window() : nullptr;
}

void QmlShellBridge::openPeople()
{
    openPeopleAt(QString());
}

void QmlShellBridge::openPeopleAt(const QString& person)
{
    // Non-modal QML window, so the effect of a rename / merge / delete shows immediately on the
    // open transcript in the background. One instance: a second request raises the first.
    if (m_shutDown)
        return;
    m_people->setTransientParent(m_window);
    m_people->open(person);
}

void QmlShellBridge::openPeopleAtSection(const QString& person, const QString& section)
{
    if (m_shutDown)
        return;
    m_people->setTransientParent(m_window);
    m_people->open(person, section);
}

QObject* QmlShellBridge::peopleWindow() const
{
    return m_people ? m_people->window() : nullptr;
}

void QmlShellBridge::openTags()
{
    openTagsAt(QString());
}

void QmlShellBridge::openTagsAt(const QString& tagId)
{
    // Non-modal QML window (like People), one instance.
    if (m_shutDown)
        return;
    m_tags->setTransientParent(m_window);
    m_tags->open(tagId);
}

QObject* QmlShellBridge::tagsWindow() const
{
    return m_tags ? m_tags->window() : nullptr;
}

void QmlShellBridge::openOnboarding()
{
    // Non-modal QML window, one instance; always opens when requested by hand (regardless of onboardingDone).
    if (m_shutDown)
        return;
    m_onboarding->setTransientParent(m_window);
    m_onboarding->open();
}

QObject* QmlShellBridge::onboardingWindow() const
{
    return m_onboarding ? m_onboarding->window() : nullptr;
}

void QmlShellBridge::maybeOpenOnboarding()
{
    // First launch: once, centered over the main window.
    if (m_shutDown || !m_autoOnboarding || mainWindowHidden())
        return;
    m_onboarding->setTransientParent(m_window);
    if (m_onboarding->openIfNeeded())
        qCInfo(tanara::lcApp) << "Első lépések: első indítás — az ablak megnyílt";
}

QString QmlShellBridge::pickAudioFile()
{
    const QString start = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    return QFileDialog::getOpenFileName(
        nullptr, tr("Hangfájl kiválasztása"), start,
        tr("Hangfájlok (*.ogg *.opus *.flac *.wav *.mp3 *.m4a *.aac);;Minden fájl (*)"));
}

QStringList QmlShellBridge::pickAudioFiles()
{
    // For import: anything ffmpeg decodes, including the audio of videos.
    const QString start = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    return QFileDialog::getOpenFileNames(
        nullptr, tr("Importálandó hangfájlok kiválasztása"), start,
        tr("Hang- és videófájlok (*.wav *.mp3 *.m4a *.aac *.flac *.ogg *.opus *.wma *.aiff *.aif "
           "*.amr *.mp4 *.mov *.mkv *.webm *.avi *.m4v *.3gp);;Minden fájl (*)"));
}

QString QmlShellBridge::pickSaveFile(const QString& title, const QString& proposedPath,
                                     const QString& filter)
{
    return QFileDialog::getSaveFileName(nullptr, title, proposedPath, filter);
}

QString QmlShellBridge::pickArchiveFile()
{
    const QString start = QDir::home().filePath(QStringLiteral("Tanara"));
    return QFileDialog::getOpenFileName(
        nullptr, tr("Megbeszélés-archívum kiválasztása"),
        QFileInfo(start).isDir() ? start : QDir::homePath(),
        tr("Tanara-archívum (*.tanara.zip *.zip);;Minden fájl (*)"));
}

QString QmlShellBridge::pickMeetingFolder()
{
    const QString start = QDir::home().filePath(QStringLiteral("Tanara"));
    return QFileDialog::getExistingDirectory(
        nullptr, tr("Megbeszélés mappájának kiválasztása"),
        QFileInfo(start).isDir() ? start : QDir::homePath(),
        QFileDialog::ShowDirsOnly);
}

// ---- recorder (see ShellRecorderHost) ----

void QmlShellBridge::startRecorderListening()
{
    m_recorder->startListening();
}

void QmlShellBridge::openRecorder()
{
    m_recorder->open();
}

QObject* QmlShellBridge::recorderWindow() const
{
    return m_recorder->window();
}

void QmlShellBridge::showMeeting(const QString& meetingId)
{
    // A meeting created by another process (the standalone recorder) is not yet known to the
    // library: the index is shared (on disk), so we reload the library's cache.
    if (!meetingId.isEmpty() && m_controller->library()
        && m_controller->library()->meeting(meetingId).id.isEmpty())
        m_controller->library()->invalidate();
    emit showMeetingRequested(meetingId);
}

void QmlShellBridge::continueRecordingInBackground()
{
    // The floating recorder is a standalone top-level window, so the app stays alive; QML hides
    // the main window, and it comes back by itself when the recording ends (or if the recorder
    // disappears) (showWindowRequested).
    openRecorder();
}

void QmlShellBridge::stopRecordingAndQuit()
{
    if (m_controller->recordingState() == tanara::RecordingState::Idle) {
        emit quitRequested();
        return;
    }
    m_quitAfterStop = true;
    m_controller->stopRecording();   // async (encoding); quitRequested() when Idle
}

void QmlShellBridge::shutdown()
{
    if (m_shutDown)
        return;
    m_shutDown = true;
    qApp->removeEventFilter(this);
    if (m_people)
        m_people->closeNow();
    if (m_tags)
        m_tags->closeNow();
    if (m_onboarding)
        m_onboarding->closeNow();
    if (m_settings)
        m_settings->closeNow();
    m_recorder->shutdown();
}

// ---- participant guessing before the transcript ----

QString QmlShellBridge::identifyParticipantsPreview(const QString& meetingId, bool* cancelled)
{
    if (cancelled) *cancelled = false;
    // Cancellable progress: the computation runs on the main thread; the callback updates the
    // dialog on every step, keeps the UI alive with processEvents and watches for "Cancel".
    QProgressDialog dlg(tr("Résztvevők azonosítása a hang alapján…"), tr("Megszakítás"), 0, 0, nullptr);
    dlg.setWindowTitle(tr("Résztvevők azonosítása"));
    dlg.setWindowModality(Qt::ApplicationModal);
    dlg.setMinimumDuration(0);
    dlg.setAutoClose(false);
    dlg.setAutoReset(false);
    dlg.setValue(0);
    auto progress = [&dlg](int done, int total) -> bool {
        if (total > 0) { dlg.setMaximum(total); dlg.setValue(done); }
        QCoreApplication::processEvents();
        return !dlg.wasCanceled();
    };
    const auto guesses = m_controller->identifyParticipants(meetingId, progress);
    if (dlg.wasCanceled()) {
        if (cancelled) *cancelled = true;
        return {};
    }
    QStringList named;
    int unknown = 0;
    for (const auto& g : guesses) {
        if (g.name.trimmed().isEmpty()) ++unknown;
        else named << g.name.trimmed();
    }
    dlg.reset();
    return participantsSummary(named, unknown, int(guesses.size()));
}

void QmlShellBridge::windowShown()
{
    if (m_startupDone)
        return;
    m_startupDone = true;
    // The main window is already visible: the "Getting started" window comes in the next event
    // loop round, so it does not delay startup.
    QTimer::singleShot(0, this, [this] { maybeOpenOnboarding(); });
}

} // namespace tanara_gui
