#pragma once
//
// TagsWindowHost: the C++ host of the QML Tags window (TagsWindow.qml), modeled on PeopleWindowHost.
//
// ONE instance per process is enough: in the main application QmlShellBridge creates it
// (ShellActions.openTags -> open()). It uses its own QML engine, the theme is shared through
// the `App` singleton. The window is non-modal; "Show in library as a filter" and the meeting
// links go to the main window (openInLibraryRequested / meetingRequested; the bridge ties them to the shell).
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

class TagsViewModel;

class TagsWindowHost : public QObject {
    Q_OBJECT
public:
    explicit TagsWindowHost(tanara::app::AppController* controller, QObject* parent = nullptr);
    ~TagsWindowHost() override;

    // Opens / raises the window; tagId: select this tag (empty: the earlier selection stays).
    // False: QML error.
    bool open(const QString& tagId = QString());
    void closeNow();
    bool isVisible() const;
    QQuickWindow* window() const;
    TagsViewModel* viewModel() const;

    // The window should belong to this one per the window manager.
    void setTransientParent(QWindow* parent);

signals:
    void closed();
    void openInLibraryRequested(const QString& tagId);
    void meetingRequested(const QString& meetingId);
    // A People-list row was clicked: the People window should open on this person.
    void personRequested(const QString& name);

private:
    bool ensureWindow();

    QPointer<tanara::app::AppController> m_controller;
    QQmlEngine* m_engine = nullptr;
    QPointer<QQuickWindow> m_window;
    QPointer<TagsViewModel> m_vm;
    QPointer<QWindow> m_transientParent;
};

} // namespace tanara_qml
