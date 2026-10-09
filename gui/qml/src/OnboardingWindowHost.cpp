#include "OnboardingWindowHost.h"

#include "OnboardingViewModel.h"
#include "QmlApp.h"
#include "SettingsDialogs.h"

#include "tanara/AppController.h"
#include "tanara/Logging.h"
#include "tanara/SettingsManager.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>

using namespace tanara;

namespace tanara_qml {

OnboardingWindowHost::OnboardingWindowHost(AppController* controller, SettingsDialogs* dialogs,
                                           QObject* parent)
    : QObject(parent), m_controller(controller), m_dialogs(dialogs)
{
}

OnboardingWindowHost::~OnboardingWindowHost()
{
    delete m_window;
    delete m_engine;
}

bool OnboardingWindowHost::ensureWindow()
{
    if (m_window) return true;
    if (!m_engine) {
        m_engine = new QQmlEngine(this);
        setupEngine(*m_engine);
    }
    QQmlComponent comp(m_engine);
    comp.loadFromModule("Tanara", "OnboardingWindow");
    QObject* obj = comp.isError() ? nullptr : comp.createWithInitialProperties({
        {QStringLiteral("hostController"), QVariant::fromValue<QObject*>(m_controller.data())},
        {QStringLiteral("hostDialogs"), QVariant::fromValue<QObject*>(m_dialogs.data())},
    });
    auto* win = qobject_cast<QQuickWindow*>(obj);
    if (!win) {
        for (const QQmlError& e : comp.errors())
            qCCritical(lcApp).noquote() << "Első lépések ablak:" << e.toString();
        delete obj;
        return false;
    }
    QQmlEngine::setObjectOwnership(win, QQmlEngine::CppOwnership);
    m_window = win;
    m_vm = qobject_cast<OnboardingViewModel*>(win->property("vm").value<QObject*>());
    if (m_transientParent) win->setTransientParent(m_transientParent);
    if (m_vm) {
        connect(m_vm, &OnboardingViewModel::openSettingsRequested, this, &OnboardingWindowHost::openSettingsRequested);
        connect(m_vm, &OnboardingViewModel::themeModeSaved, this, &OnboardingWindowHost::themeModeSaved);
        connect(m_vm, &OnboardingViewModel::saved, this, &OnboardingWindowHost::saved);
    }
    connect(win, &QQuickWindow::visibleChanged, this, [this](bool visible) {
        if (visible) return;
        // Bezárás bármelyik úton (Kész, Később, ×): a el nem fogadott lépés elvész, és többé
        // nem nyílik meg magától.
        if (m_vm) {
            m_vm->discardPending();
            m_vm->markDone();
        }
        emit closed();
    });
    return true;
}

void OnboardingWindowHost::centerOverParent()
{
    if (!m_window || !m_transientParent || !m_transientParent->isVisible()) return;
    const QRect p = m_transientParent->frameGeometry();
    const QSize s = m_window->size();
    m_window->setPosition(p.x() + (p.width() - s.width()) / 2, p.y() + (p.height() - s.height()) / 2);
}

bool OnboardingWindowHost::open()
{
    if (!ensureWindow()) return false;
    if (!m_window->isVisible()) {
        if (m_vm) m_vm->reload();
        centerOverParent();
    }
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
    return true;
}

bool OnboardingWindowHost::shouldAutoOpen() const
{
    return m_controller && !m_autoOpened && !m_controller->settings()->settings().onboardingDone;
}

bool OnboardingWindowHost::openIfNeeded()
{
    if (!shouldAutoOpen()) return false;
    m_autoOpened = true;
    return open();
}

void OnboardingWindowHost::closeNow()
{
    if (m_window) m_window->hide();
}

bool OnboardingWindowHost::isVisible() const { return m_window && m_window->isVisible(); }
QQuickWindow* OnboardingWindowHost::window() const { return m_window; }
OnboardingViewModel* OnboardingWindowHost::viewModel() const { return m_vm; }

void OnboardingWindowHost::setTransientParent(QWindow* parent)
{
    m_transientParent = parent;
    if (m_window) m_window->setTransientParent(parent);
}

} // namespace tanara_qml
