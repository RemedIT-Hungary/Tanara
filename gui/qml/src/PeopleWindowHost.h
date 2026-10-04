#pragma once
//
// PeopleWindowHost — a QML Személyek-ablak (PeopleWindow.qml) C++ gazdája.
//
// Egy folyamatban EGY példány elég: a főalkalmazásban a QmlShellBridge hozza létre
// (ShellActions.openPeople → open()), a főablak nélküli folyamatokban (önálló felvevő,
// `tanara --settings`) a Beállítások „Személyek kezelése” hivatkozása a
// SettingsWidgetsDialogs-on át. Saját QML-motorral dolgozik (ahogy a Beállítások gazdája), a
// téma az `App` singletonon át közös. Az ablak nem modális: a főablak mellette használható,
// és a műveletek hatása (átnevezés, összevonás …) azonnal látszik a nyitott átiraton.
//
#include <QObject>
#include <QPointer>
#include <QString>

class QQmlEngine;
class QQuickWindow;
class QWindow;

namespace tanara {
class AppController;
}

namespace tanara_qml {

class PeopleViewModel;

class PeopleWindowHost : public QObject {
    Q_OBJECT
public:
    explicit PeopleWindowHost(tanara::AppController* controller, QObject* parent = nullptr);
    ~PeopleWindowHost() override;

    // Az ablak megnyitása / előtérbe hozása; person: ez a személy legyen kijelölve (üres: a
    // korábbi kijelölés marad). Megnyitáskor a lista a lemez friss állapotát mutatja.
    // false: QML-hiba.
    bool open(const QString& person = QString());
    void closeNow();
    bool isVisible() const;
    QQuickWindow* window() const;
    PeopleViewModel* viewModel() const;

    // Az ablak ehhez tartozzon az ablakkezelő szerint.
    void setTransientParent(QWindow* parent);

signals:
    void closed();

private:
    bool ensureWindow();

    QPointer<tanara::AppController> m_controller;
    QQmlEngine* m_engine = nullptr;
    QPointer<QQuickWindow> m_window;
    QPointer<PeopleViewModel> m_vm;
    QPointer<QWindow> m_transientParent;
};

} // namespace tanara_qml
