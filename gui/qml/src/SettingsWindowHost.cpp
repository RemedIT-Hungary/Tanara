#include "SettingsWindowHost.h"

#include "QmlApp.h"
#include "SettingsDialogs.h"
#include "SettingsViewModel.h"
#include "ShellUiState.h"

#include "tanara/AppController.h"
#include "tanara/Logging.h"
#include "tanara/Paths.h"
#include "tanara/SettingsManager.h"

#include <QDir>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>

using namespace tanara;

namespace tanara_qml {

SettingsWindowHost::SettingsWindowHost(AppController* controller, SettingsDialogs* dialogs,
                                       QObject* parent)
    : QObject(parent), m_controller(controller), m_dialogs(dialogs)
{
}

SettingsWindowHost::~SettingsWindowHost()
{
    delete m_window;
    delete m_engine;
}

bool SettingsWindowHost::ensureWindow()
{
    if (m_window) return true;
    if (!m_engine) {
        m_engine = new QQmlEngine(this);
        setupEngine(*m_engine);
    }
    QQmlComponent comp(m_engine);
    comp.loadFromModule("Tanara", "SettingsWindow");
    QObject* obj = comp.isError() ? nullptr : comp.createWithInitialProperties({
        {QStringLiteral("hostController"), QVariant::fromValue<QObject*>(m_controller.data())},
        {QStringLiteral("hostDialogs"), QVariant::fromValue<QObject*>(m_dialogs.data())},
    });
    auto* win = qobject_cast<QQuickWindow*>(obj);
    if (!win) {
        for (const QQmlError& e : comp.errors())
            qCCritical(lcApp).noquote() << "Beállítások-ablak:" << e.toString();
        delete obj;
        return false;
    }
    QQmlEngine::setObjectOwnership(win, QQmlEngine::CppOwnership);
    m_window = win;
    m_vm = qobject_cast<SettingsViewModel*>(win->property("vm").value<QObject*>());
    if (m_transientParent) win->setTransientParent(m_transientParent);

    if (m_vm) {
        connect(m_vm, &SettingsViewModel::saved, this, &SettingsWindowHost::saved);
        connect(m_vm, &SettingsViewModel::themeModeSaved, this, [this](const QString& mode) {
            if (m_persistTheme && m_controller) {
                // Nincs főablak, amely megjegyezné: a közös ui-state.json-ba írjuk.
                ShellUiState state;
                state.setFilePath(QDir(paths::resolveMetadataDir(
                    m_controller->settings()->settings().metadataDir)).filePath(QStringLiteral("ui-state.json")));
                state.setValue(QStringLiteral("themeMode"), mode);
                state.flush();
            }
            emit themeModeSaved(mode);
        });
        connect(m_vm, &SettingsViewModel::returnRequested, this, [this] {
            if (m_window) m_window->hide();
            emit returnRequested();
        });
    }
    connect(win, &QQuickWindow::visibleChanged, this, [this](bool visible) {
        if (!visible) emit closed();
    });
    return true;
}

bool SettingsWindowHost::open(const QString& page, const QString& focusField)
{
    if (!ensureWindow()) return false;
    const bool wasVisible = m_window->isVisible();
    // Zárt ablak újranyitása: friss állapot a lemezről (közben más is menthetett). Nyitott
    // ablaknál a piszkozat marad — csak a lap vált.
    if (m_vm && !wasVisible && !m_vm->dirty()) m_vm->reload();
    if (m_vm) m_vm->openPage(page.isEmpty() && !wasVisible ? QStringLiteral("general") : page, focusField);
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
    return true;
}

void SettingsWindowHost::closeNow()
{
    if (!m_window) return;
    if (m_vm) m_vm->discard();
    m_window->hide();
}

bool SettingsWindowHost::isVisible() const { return m_window && m_window->isVisible(); }
QQuickWindow* SettingsWindowHost::window() const { return m_window; }
SettingsViewModel* SettingsWindowHost::viewModel() const { return m_vm; }

void SettingsWindowHost::setTransientParent(QWindow* parent)
{
    m_transientParent = parent;
    if (m_window) m_window->setTransientParent(parent);
}

} // namespace tanara_qml
