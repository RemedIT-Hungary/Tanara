#include "RecorderWindowHost.h"

#include "QmlApp.h"
#include "RecorderViewModel.h"

#include "tanara/AppController.h"
#include "tanara/foundation/log/Logging.h"
#include "tanara/foundation/paths/Paths.h"
#include "tanara/SettingsManager.h"
#include "tanara/foundation/ProcessLock.h"

#include <QDir>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>

using namespace tanara;

namespace tanara_qml {

RecorderWindowHost::RecorderWindowHost(AppController* controller, QQmlEngine* engine, QObject* parent)
    : QObject(parent), m_controller(controller), m_engine(engine)
{
    if (!m_engine) {
        m_engine = new QQmlEngine(this);
        m_ownEngine = true;
    }
    setupEngine(*m_engine);
    if (controller) {
        const QString metaDir = controller->settings()
            ? controller->settings()->settings().metadataDir : QString();
        m_lock = std::make_unique<RecordingLock>(recordingLockPath(metaDir));
    }
}

RecorderWindowHost::~RecorderWindowHost()
{
    if (m_lock) m_lock->release();
    if (m_window) delete m_window.data();
}

void RecorderWindowHost::prepareProcess()
{
    // Under Wayland a client cannot ask for always-on-top nor position its window. Whoever still
    // wants that for the recorder can run the standalone recorder on XWayland (xcb) with the
    // TANARA_RECORDER_X11=1 environment variable, where both work.
    if (qEnvironmentVariableIntValue("TANARA_RECORDER_X11") == 1
        && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM")
        && qEnvironmentVariableIsSet("DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "xcb");
    tanara_qml::prepareProcess(QmlOptions{});
}

bool RecorderWindowHost::platformCanPosition()
{
    const QString p = QGuiApplication::platformName();
    return !p.contains(QLatin1String("wayland"), Qt::CaseInsensitive);
}

QString RecorderWindowHost::iniPath() const
{
    // Without a controller (test / demo) there is nothing to remember, and we do not touch the user's folder either.
    if (!m_controller)
        return {};
    const QString dir = paths::resolveMetadataDir(
        m_controller && m_controller->settings() ? m_controller->settings()->settings().metadataDir
                                                 : QString());
    return QDir(dir).filePath(QStringLiteral("recorder.ini"));
}

QQuickWindow* RecorderWindowHost::window() const { return m_window; }
RecorderViewModel* RecorderWindowHost::viewModel() const { return m_vm; }
bool RecorderWindowHost::isVisible() const { return m_window && m_window->isVisible(); }

bool RecorderWindowHost::recording() const
{
    return m_controller && m_controller->recordingState() != RecordingState::Idle;
}

bool RecorderWindowHost::ensureWindow()
{
    if (m_window) return true;
    QQmlComponent comp(m_engine);
    comp.loadFromModule("Tanara", "RecorderWindow");
    QObject* obj = comp.isError() ? nullptr : comp.createWithInitialProperties(
        {{QStringLiteral("pinSupported"), platformCanPosition()}});
    auto* win = qobject_cast<QQuickWindow*>(obj);
    if (!win) {
        for (const QQmlError& e : comp.errors())
            qCCritical(lcApp).noquote() << "Felvevő-ablak:" << e.toString();
        delete obj;
        return false;
    }
    QQmlEngine::setObjectOwnership(win, QQmlEngine::CppOwnership);
    m_window = win;
    m_vm = qobject_cast<RecorderViewModel*>(win->property("vm").value<QObject*>());
    if (m_vm && m_controller)
        m_vm->setController(m_controller);

    // QML signals (old-style connect: the QML type declares the signals).
    connect(win, SIGNAL(hideRequested()), this, SLOT(onHideRequested()));
    connect(win, SIGNAL(closeRequested()), this, SLOT(onCloseRequested()));
    connect(win, SIGNAL(userMoved()), this, SLOT(onUserMoved()));
    connect(win, SIGNAL(pillChanged()), this, SLOT(onPillChanged()));
    connect(win, SIGNAL(pinnedChanged()), this, SLOT(onPrefsChanged()));
    connect(win, SIGNAL(expandedChanged()), this, SLOT(onPrefsChanged()));

    if (m_vm) {
        connect(m_vm, &RecorderViewModel::askRaised, this, [this](const QString& title, const QString& text) {
            // For a hidden / pill / background window: bring the window up + system notification.
            const bool wasAway = !m_window || !m_window->isVisible() || !m_window->isActive()
                                 || m_window->property("pill").toBool();
            if (m_window) {
                m_window->setProperty("pill", false);
                m_window->show();
                m_window->raise();
            }
            if (wasAway) emit notificationRequested(title, text);
        });
        connect(m_vm, &RecorderViewModel::openAnalyzerRequested, this, &RecorderWindowHost::openMeetingRequested);
        connect(m_vm, &RecorderViewModel::settingsRequested, this, &RecorderWindowHost::settingsRequested);
        connect(m_vm, &RecorderViewModel::stateChanged, this, [this] { emit stateChanged(m_vm->state()); });
        connect(m_vm, &RecorderViewModel::recordingStarted, this, [this](const QString& folder) {
            if (m_manageLock && m_lock) m_lock->acquire(folder);
            emit recordingStarted(folder);
        });
        connect(m_vm, &RecorderViewModel::recordingFinished, this, [this](const QString& id) {
            if (m_lock) m_lock->release();
            emit recordingFinished(id);
        });
        // Failed / interrupted recording: the lock must not stay.
        connect(m_vm, &RecorderViewModel::stateChanged, this, [this] {
            if (m_lock && !recording()) m_lock->release();
        });
        // If the recording is already running (the host was created later), the lock is taken now.
        if (m_manageLock && m_lock && m_controller
            && m_controller->recordingState() == RecordingState::Recording)
            m_lock->acquire(m_controller->currentMeetingFolder());
    }
    restoreGeometry();
    return true;
}

void RecorderWindowHost::onHideRequested()
{
    if (!m_window) return;
    if (m_hideEnabled) {
        m_window->hide();
        emit hiddenToTray();
    } else {
        m_window->showMinimized();   // no tray: at least restorable from the taskbar
    }
}

void RecorderWindowHost::onCloseRequested()
{
    if (recording()) return;         // during recording the view opens the page, we do not get here
    if (m_window) m_window->hide();
    emit closed();
}

void RecorderWindowHost::onUserMoved()
{
    if (m_restoring || !m_window || !m_window->isVisible()) return;
    snapPill();
    savePosition();
}

void RecorderWindowHost::onPillChanged()
{
    if (m_window && m_window->isVisible())
        QMetaObject::invokeMethod(this, [this] { snapPill(); }, Qt::QueuedConnection);
}

void RecorderWindowHost::onPrefsChanged()
{
    if (!m_window || m_restoring) return;
    if (iniPath().isEmpty()) return;
    QSettings ini(iniPath(), QSettings::IniFormat);
    ini.setValue(QStringLiteral("window/pinned"), m_window->property("pinned"));
    ini.setValue(QStringLiteral("window/expanded"), m_window->property("expanded"));
}

void RecorderWindowHost::restoreGeometry()
{
    if (!m_window) return;
    if (iniPath().isEmpty()) return;
    m_restoring = true;
    QSettings ini(iniPath(), QSettings::IniFormat);
    if (platformCanPosition())
        m_window->setProperty("pinned", ini.value(QStringLiteral("window/pinned"), true));
    m_window->setProperty("expanded", ini.value(QStringLiteral("window/expanded"), false));
    m_restoring = false;
}

void RecorderWindowHost::savePosition()
{
    // Wayland: the client does not know its absolute position -> nothing to save.
    if (!m_window || !platformCanPosition() || !m_window->screen()) return;
    if (iniPath().isEmpty()) return;
    QSettings ini(iniPath(), QSettings::IniFormat);
    const QString screen = m_window->screen()->name();
    ini.setValue(QStringLiteral("window/screen"), screen);
    ini.setValue(QStringLiteral("window/pos/") + screen, m_window->position());
}

void RecorderWindowHost::snapPill()
{
    // The pill snaps to the screen edge if released closer than 24 px. Only where the client
    // can move its window (X11, Windows).
    if (!m_window || !platformCanPosition() || !m_window->property("pill").toBool()) return;
    const QScreen* sc = m_window->screen();
    if (!sc) return;
    const QRect a = sc->availableGeometry();
    QRect g = m_window->geometry();
    const int snap = 24;
    if (qAbs(g.left() - a.left()) < snap) g.moveLeft(a.left());
    else if (qAbs(a.right() - g.right()) < snap) g.moveRight(a.right());
    if (qAbs(g.top() - a.top()) < snap) g.moveTop(a.top());
    else if (qAbs(a.bottom() - g.bottom()) < snap) g.moveBottom(a.bottom());
    if (g.topLeft() != m_window->position()) {
        m_restoring = true;
        m_window->setPosition(g.topLeft());
        m_restoring = false;
    }
}

bool RecorderWindowHost::show()
{
    if (!ensureWindow()) return false;
    if (!m_positioned && platformCanPosition() && !iniPath().isEmpty()) {
        // Last position per screen, only if the window fits entirely on that screen (after a
        // resolution change / disconnected monitor the window manager's placement stays).
        m_positioned = true;
        QSettings ini(iniPath(), QSettings::IniFormat);
        const QString want = ini.value(QStringLiteral("window/screen")).toString();
        for (QScreen* sc : QGuiApplication::screens()) {
            if (sc->name() != want) continue;
            const QVariant v = ini.value(QStringLiteral("window/pos/") + sc->name());
            if (!v.isValid()) break;
            const QRect target(v.toPoint(), QSize(m_window->width(), qMax(m_window->height(), 120)));
            if (sc->availableGeometry().contains(target)) {
                m_restoring = true;
                m_window->setScreen(sc);
                m_window->setPosition(target.topLeft());
                m_restoring = false;
            }
            break;
        }
    }
    if (m_window->visibility() == QWindow::Minimized) m_window->showNormal();
    else m_window->show();
    m_window->raise();
    m_window->requestActivate();
    emit shown();
    return true;
}

void RecorderWindowHost::hide()
{
    if (m_window) m_window->hide();
}

void RecorderWindowHost::request(const RecorderRequest& r)
{
    if (r.stop) {
        // Explicit user request from the tray menu: stop (without a window too).
        if (m_vm && m_vm->state() == QLatin1String("recording")) m_vm->stop();
        return;
    }
    if (!show() || !m_vm) return;
    // Start request in the previous recording's "Saved" state: we first switch to a new
    // recording (otherwise the request would be lost without a start).
    if (r.start && m_vm->state() == QLatin1String("done"))
        m_vm->newRecording();
    m_vm->applyRequest(r.title, r.appName, r.context, r.deviceIndexes);
    if (r.start && m_vm->state() == QLatin1String("idle"))
        m_vm->start();
}

} // namespace tanara_qml
