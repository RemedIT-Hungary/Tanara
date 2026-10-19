#include "TagsWindowHost.h"

#include "AppContext.h"
#include "QmlApp.h"
#include "TagsViewModel.h"

#include "tanara/foundation/log/Logging.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>

using namespace tanara;

namespace tanara_qml {

TagsWindowHost::TagsWindowHost(app::AppController* controller, QObject* parent)
    : QObject(parent)
{
    AppContext::instance()->useControllerIfNone(controller);
}

TagsWindowHost::~TagsWindowHost()
{
    delete m_window;
    delete m_engine;
}

bool TagsWindowHost::ensureWindow()
{
    if (m_window) return true;
    if (!m_engine) {
        m_engine = new QQmlEngine(this);
        setupEngine(*m_engine);
    }
    QQmlComponent comp(m_engine);
    comp.loadFromModule("Tanara", "TagsWindow");
    QObject* obj = comp.isError() ? nullptr : comp.create();
    auto* win = qobject_cast<QQuickWindow*>(obj);
    if (!win) {
        for (const QQmlError& e : comp.errors())
            qCCritical(lcApp).noquote() << "Tags window:" << e.toString();
        delete obj;
        return false;
    }
    QQmlEngine::setObjectOwnership(win, QQmlEngine::CppOwnership);
    m_window = win;
    m_vm = qobject_cast<TagsViewModel*>(win->property("vm").value<QObject*>());
    if (m_transientParent) win->setTransientParent(m_transientParent);
    connect(win, &QQuickWindow::visibleChanged, this, [this](bool visible) {
        if (!visible) emit closed();
    });
    // The QML window's signals (string-based: the type lives in QML).
    connect(win, SIGNAL(openInLibraryRequested(QString)), this, SIGNAL(openInLibraryRequested(QString)));
    connect(win, SIGNAL(meetingRequested(QString)), this, SIGNAL(meetingRequested(QString)));
    connect(win, SIGNAL(personRequested(QString)), this, SIGNAL(personRequested(QString)));
    return true;
}

bool TagsWindowHost::open(const QString& tagId)
{
    if (!ensureWindow()) return false;
    if (m_vm && !tagId.trimmed().isEmpty()) m_vm->setSelectedId(tagId.trimmed());
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
    return true;
}

void TagsWindowHost::closeNow()
{
    if (m_window) m_window->hide();
}

bool TagsWindowHost::isVisible() const { return m_window && m_window->isVisible(); }
QQuickWindow* TagsWindowHost::window() const { return m_window; }
TagsViewModel* TagsWindowHost::viewModel() const { return m_vm; }

void TagsWindowHost::setTransientParent(QWindow* parent)
{
    m_transientParent = parent;
    if (m_window) m_window->setTransientParent(parent);
}

} // namespace tanara_qml
