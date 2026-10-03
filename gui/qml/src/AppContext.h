#pragma once
//
// Tanara QML — az `App` singleton: a QML-felület és a C++ világ közös belépőpontja.
//
//  - QML-ből:  App.dark / App.themeMode / App.demo / App.controller / App.bridge
//  - C++-ból (nézetmodellek):  tanara_qml::AppContext::instance()->controller()
//
// A controller() lehet nullptr: képernyőkép- (--qml-shot), galéria- (--gallery) és demó-
// (--demo) módban SZÁNDÉKOSAN nem jön létre AppController (ne nyúljunk a user adataihoz),
// és a tests/ui tesztekben sem kötelező. A nézetmodelleknek ezt el kell viselniük; ha
// App.demo igaz, beépített (kitalált) mintaadatot mutassanak.
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

    // A ténylegesen érvényes téma (themeMode + a rendszer színsémája). A Theme.dark ezt követi.
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)
    // "system" (alapértelmezés: a rendszer színsémáját követi) | "light" | "dark".
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    // Igaz: nincs AppController, a nézetmodellek kitalált mintaadattal dolgoznak.
    Q_PROPERTY(bool demo READ demo NOTIFY demoChanged)
    // A program verziója (tanara::libraryVersion(), pl. "0.5.0a") — a Beállítások alján látszik.
    Q_PROPERTY(QString version READ version CONSTANT)
    // A core egyetlen UI-felé néző objektuma (tanara::AppController) — QML-ből QObject-ként.
    Q_PROPERTY(QObject* controller READ controllerObject NOTIFY controllerChanged)
    // A Widgets-párbeszédablakokat (Beállítások, Személyek, felvevő, cloud) nyitó híd.
    // A gui/src-ben él (az ismeri a Widgets-osztályokat), a main.cpp állítja be.
    Q_PROPERTY(QObject* bridge READ bridge NOTIFY bridgeChanged)

public:
    // Folyamat-szintű példány (a QML-motor is ezt kapja — lásd create()).
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

    // A "system" | "light" | "dark" érték normalizálása (ismeretlen → "system").
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
