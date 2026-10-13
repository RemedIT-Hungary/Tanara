#pragma once
//
// PeopleWindowHost: the C++ host of the QML People window (PeopleWindow.qml).
//
// ONE instance per process is enough: in the main application QmlShellBridge creates it
// (ShellActions.openPeople -> open()), in processes without a main window (standalone
// recorder, `tanara --settings`) Settings' "Manage people" link through
// SettingsWidgetsDialogs. It uses its own QML engine (like Settings' host); the theme is
// shared through the `App` singleton. The window is non-modal: the main window can be used
// beside it, and the effect of actions (rename, merge ...) shows immediately on the open transcript.
//
#include <QObject>
#include <QPointer>
#include <QString>

class QQmlEngine;
class QQuickWindow;
class QWindow;

namespace tanara {
namespace app { class AppController; }
}

namespace tanara_qml {

class PeopleViewModel;

class PeopleWindowHost : public QObject {
    Q_OBJECT
public:
    explicit PeopleWindowHost(tanara::app::AppController* controller, QObject* parent = nullptr);
    ~PeopleWindowHost() override;

    // Opens / raises the window; person: select this person (empty: the earlier selection
    // stays). On opening the list shows the fresh state from disk.
    // section: scroll to this section ("tags"; empty: no request). False: QML error.
    bool open(const QString& person = QString(), const QString& section = QString());
    void closeNow();
    bool isVisible() const;
    QQuickWindow* window() const;
    PeopleViewModel* viewModel() const;

    // The window should belong to this one per the window manager.
    void setTransientParent(QWindow* parent);

signals:
    void closed();

private:
    bool ensureWindow();

    QPointer<tanara::app::AppController> m_controller;
    QQmlEngine* m_engine = nullptr;
    QPointer<QQuickWindow> m_window;
    QPointer<PeopleViewModel> m_vm;
    QPointer<QWindow> m_transientParent;
};

} // namespace tanara_qml
