#pragma once
//
// SettingsWindowHost — a QML Beállítások-ablak (SettingsWindow.qml) C++ gazdája.
//
// Egy folyamatban EGY példány elég; három helyen él:
//  - a főalkalmazásban a QmlShellBridge hozza létre (ShellActions.openSettings → open());
//  - az önálló felvevőben (`tanara --record`, R10 „Rögzítés beállításai”);
//  - a `tanara --settings [lap]` módban (a tálca-figyelő „Beállítások…” menüpontja), ha nem
//    fut főablak, amelynek a kérést át lehetne adni.
// Saját QML-motorral dolgozik (ahogy a felvevő gazdája), a téma az `App` singletonon át közös.
// Widgets-függősége nincs: a natív mappaválasztót és a Tanara Cloud Widgets-ablakait a
// SettingsDialogs felületen át kéri (gui/src: SettingsWidgetsDialogs).
//
// Az ablak nem modális: a főablak mellette használható. A bezárás mentetlen változásnál
// rákérdez (Mentés / Elvetés / Mégse) — ezt a QML intézi.
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

class SettingsDialogs;
class SettingsViewModel;

class SettingsWindowHost : public QObject {
    Q_OBJECT
public:
    SettingsWindowHost(tanara::AppController* controller, SettingsDialogs* dialogs,
                       QObject* parent = nullptr);
    ~SettingsWindowHost() override;

    // Az ablak megnyitása / előtérbe hozása a megadott lapon. page: "" | "general" |
    // "recording" | "watcher" | "providers" | "cloud" | "summary"; focusField: "" | "stt" |
    // "llm" (B04). Ha az ablak zárva volt, a beállításokat újratölti. false: QML-hiba.
    bool open(const QString& page = QString(), const QString& focusField = QString());
    // Bezárás kérdés nélkül (a folyamat leállásakor); a mentetlen piszkozat elvész.
    void closeNow();
    bool isVisible() const;
    QQuickWindow* window() const;
    SettingsViewModel* viewModel() const;

    // Az ablak ehhez tartozzon az ablakkezelő szerint (fölötte marad, hozzá igazodik).
    void setTransientParent(QWindow* parent);
    // Hamis: a téma mentését a hívó intézi a themeModeSaved jelre (a főablak ui-state-je);
    // igaz (alapértelmezés): a gazda maga írja a <metaadat-mappa>/ui-state.json-ba.
    void setPersistTheme(bool on) { m_persistTheme = on; }

signals:
    void saved();                           // a beállítások mentődtek
    void themeModeSaved(const QString& mode);
    void returnRequested();                 // B04: vissza a megbeszéléshez (az ablak már bezárult)
    void closed();                          // az ablak bezárult (mentéssel vagy anélkül)

private:
    bool ensureWindow();

    QPointer<tanara::AppController> m_controller;
    QPointer<SettingsDialogs> m_dialogs;
    QQmlEngine* m_engine = nullptr;
    QPointer<QQuickWindow> m_window;
    QPointer<SettingsViewModel> m_vm;
    QPointer<QWindow> m_transientParent;
    bool m_persistTheme = true;
};

} // namespace tanara_qml
