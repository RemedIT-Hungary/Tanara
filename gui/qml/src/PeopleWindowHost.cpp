#include "PeopleWindowHost.h"

#include "PeopleViewModel.h"
#include "QmlApp.h"

#include "tanara/AppController.h"
#include "tanara/Logging.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>

using namespace tanara;

namespace tanara_qml {

PeopleWindowHost::PeopleWindowHost(AppController* controller, QObject* parent)
    : QObject(parent), m_controller(controller)
{
}

PeopleWindowHost::~PeopleWindowHost()
{
    delete m_window;
    delete m_engine;
}

bool PeopleWindowHost::ensureWindow()
{
    if (m_window) return true;
    if (!m_engine) {
        m_engine = new QQmlEngine(this);
        setupEngine(*m_engine);
    }
    QQmlComponent comp(m_engine);
    comp.loadFromModule("Tanara", "PeopleWindow");
    QObject* obj = comp.isError() ? nullptr : comp.createWithInitialProperties({
        {QStringLiteral("hostController"), QVariant::fromValue<QObject*>(m_controller.data())},
    });
    auto* win = qobject_cast<QQuickWindow*>(obj);
    if (!win) {
        for (const QQmlError& e : comp.errors())
            qCCritical(lcApp).noquote() << "Személyek-ablak:" << e.toString();
        delete obj;
        return false;
    }
    QQmlEngine::setObjectOwnership(win, QQmlEngine::CppOwnership);
    m_window = win;
    m_vm = qobject_cast<PeopleViewModel*>(win->property("vm").value<QObject*>());
    if (m_transientParent) win->setTransientParent(m_transientParent);
    connect(win, &QQuickWindow::visibleChanged, this, [this](bool visible) {
        if (!visible) emit closed();
    });
    return true;
}

bool PeopleWindowHost::open(const QString& person)
{
    if (!ensureWindow()) return false;
    const bool wasVisible = m_window->isVisible();
    // Zárt ablak újranyitása: friss állapot a lemezről + a statisztika frissítése a háttérben.
    if (m_vm && !wasVisible) m_vm->refresh();
    if (m_vm && !person.trimmed().isEmpty()) m_vm->selectPerson(person.trimmed());
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
    return true;
}

void PeopleWindowHost::closeNow()
{
    if (m_window) m_window->hide();
}

bool PeopleWindowHost::isVisible() const { return m_window && m_window->isVisible(); }
QQuickWindow* PeopleWindowHost::window() const { return m_window; }
PeopleViewModel* PeopleWindowHost::viewModel() const { return m_vm; }

void PeopleWindowHost::setTransientParent(QWindow* parent)
{
    m_transientParent = parent;
    if (m_window) m_window->setTransientParent(parent);
}

} // namespace tanara_qml
