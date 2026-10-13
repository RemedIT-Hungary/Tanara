#pragma once
//
// Tanara QML: the `App` singleton: the shared entry point of the QML UI and the C++ world.
//
//  - from QML:  App.dark / App.themeMode / App.demo / App.controller / App.bridge
//  - from C++ (view models):  tanara_qml::AppContext::instance()->controller()
//
// controller() may be nullptr: in screenshot (--qml-shot), gallery (--gallery) and demo
// (--demo) modes an AppController is INTENTIONALLY not created (so the user's data is not
// touched), and in the tests/ui tests it is not mandatory either. View models must cope with
// this; if App.demo is true, they should show built-in (fictional) sample data.
//
#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace tanara {
class AppController;
}

namespace tanara_qml {

class AppContext : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(App)
    QML_SINGLETON

    // The theme actually in effect (themeMode + the system's color scheme). Theme.dark follows it.
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)
    // "system" (default: follows the system's color scheme) | "light" | "dark".
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    // True: there is no AppController, the view models work with fictional sample data.
    Q_PROPERTY(bool demo READ demo NOTIFY demoChanged)
    // The program's version (tanara::libraryVersion(), e.g. "0.5.0a"): shown at the bottom of Settings.
    Q_PROPERTY(QString version READ version CONSTANT)
    // The core's single UI-facing object (tanara::AppController): from QML as a QObject.
    Q_PROPERTY(QObject* controller READ controllerObject NOTIFY controllerChanged)
    // The bridge that opens the separate windows (Settings, People, recorder) and the cloud
    // Widgets dialogs. It lives in gui/src (it knows the Widgets classes); main.cpp sets it.
    Q_PROPERTY(QObject* bridge READ bridge NOTIFY bridgeChanged)

public:
    // Process-level instance (the QML engine gets this one too; see create()).
    static AppContext* instance();
    static AppContext* create(QQmlEngine*, QJSEngine*);

    bool dark() const { return m_dark; }
    QString themeMode() const { return m_themeMode; }
    void setThemeMode(const QString& mode);

    bool demo() const { return m_demo; }
    QString version() const;
    void setDemo(bool demo);

    tanara::AppController* controller() const;
    QObject* controllerObject() const;
    void setController(tanara::AppController* controller);

    QObject* bridge() const { return m_bridge; }
    void setBridge(QObject* bridge);

    // Normalizes the "system" | "light" | "dark" value (unknown -> "system").
    static QString normalizedThemeMode(const QString& mode);

signals:
    void darkChanged();
    void themeModeChanged();
    void demoChanged();
    void controllerChanged();
    void bridgeChanged();

private:
    explicit AppContext(QObject* parent = nullptr);
    void updateDark();

    QString m_themeMode = QStringLiteral("system");
    bool m_dark = false;
    bool m_demo = false;
    QPointer<QObject> m_controller;
    QPointer<QObject> m_bridge;
};

} // namespace tanara_qml
