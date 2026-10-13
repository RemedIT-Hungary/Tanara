#pragma once
//
// OnboardingWindowHost: the C++ host of the "Getting started" window (OnboardingWindow.qml),
// modeled on TagsWindowHost.
//
// ONE instance per process is enough: in the main application QmlShellBridge creates it. It opens two ways:
//  - by itself, on first launch (openIfNeeded: only if `onboardingDone` is still false); the
//    bridge calls it AFTER the main window appears (QTimer::singleShot(0)), so it never delays startup;
//  - by hand: File > "Getting started..." and Settings > General's link (open); this does
//    not depend on the flag.
// It uses its own QML engine, the theme is shared through the `App` singleton. The window is
// non-modal; on first opening it is centered over the main window (transient parent). On
// close the unsaved step is lost and `onboardingDone` becomes true.
//
#include <QObject>
#include <QPointer>

class QQmlEngine;
class QQuickWindow;
class QWindow;

namespace tanara {
namespace app { class AppController; }
}

namespace tanara_qml {

class OnboardingViewModel;
class SettingsDialogs;

class OnboardingWindowHost : public QObject {
    Q_OBJECT
public:
    OnboardingWindowHost(tanara::app::AppController* controller, SettingsDialogs* dialogs,
                         QObject* parent = nullptr);
    ~OnboardingWindowHost() override;

    // Opens / raises the window. If it was closed, the steps start over with fresh settings.
    // False: QML error.
    bool open();
    // Automatic opening on first launch: only if there is a controller and `onboardingDone` is
    // false, and it has not yet opened by itself in this process. True = it opened.
    bool openIfNeeded();
    // Whether it would open by itself (the condition above, without opening).
    bool shouldAutoOpen() const;
    void closeNow();
    bool isVisible() const;
    QQuickWindow* window() const;
    OnboardingViewModel* viewModel() const;

    // The window should belong to this one per the window manager (and on first opening is centered over it).
    void setTransientParent(QWindow* parent);

signals:
    void closed();
    // "Set up now": a Settings page ("providers").
    void openSettingsRequested(const QString& page);
    void themeModeSaved(const QString& mode);
    // Accepting a step changed the settings.
    void saved();

private:
    bool ensureWindow();
    void centerOverParent();

    QPointer<tanara::app::AppController> m_controller;
    QPointer<SettingsDialogs> m_dialogs;
    QQmlEngine* m_engine = nullptr;
    QPointer<QQuickWindow> m_window;
    QPointer<OnboardingViewModel> m_vm;
    QPointer<QWindow> m_transientParent;
    bool m_autoOpened = false;
};

} // namespace tanara_qml
