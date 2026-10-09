#pragma once
//
// OnboardingWindowHost — az „Első lépések” ablak (OnboardingWindow.qml, K14) C++ gazdája
// (a TagsWindowHost mintájára).
//
// Egy folyamatban EGY példány elég: a főalkalmazásban a QmlShellBridge hozza létre. Két úton nyílik:
//  - magától, az első indításkor (openIfNeeded: csak ha az `onboardingDone` még hamis) — a híd a
//    főablak megjelenése UTÁN hívja (QTimer::singleShot(0)), így az indulást sosem tartja fel;
//  - kézzel: Fájl › „Első lépések…” és a Beállítások › Általános hivatkozása (open) — ez nem
//    függ a jelzőtől.
// Saját QML-motorral dolgozik, a téma az `App` singletonon át közös. Az ablak nem modális; az
// első megnyitáskor a főablak (transient parent) fölé középre kerül. Bezáráskor a mentetlen lépés
// elvész, és az `onboardingDone` igazra áll.
//
#include <QObject>
#include <QPointer>

class QQmlEngine;
class QQuickWindow;
class QWindow;

namespace tanara {
class AppController;
}

namespace tanara_qml {

class OnboardingViewModel;
class SettingsDialogs;

class OnboardingWindowHost : public QObject {
    Q_OBJECT
public:
    OnboardingWindowHost(tanara::AppController* controller, SettingsDialogs* dialogs,
                         QObject* parent = nullptr);
    ~OnboardingWindowHost() override;

    // Az ablak megnyitása / előtérbe hozása. Zárt ablaknál a lépések elölről, friss
    // beállításokkal indulnak. false: QML-hiba.
    bool open();
    // Az első indítás automatikus megnyitása: csak ha van controller és az `onboardingDone`
    // hamis, és ebben a folyamatban még nem nyílt meg magától. true = megnyílt.
    bool openIfNeeded();
    // Megnyílna-e magától (a fenti feltétel, megnyitás nélkül).
    bool shouldAutoOpen() const;
    void closeNow();
    bool isVisible() const;
    QQuickWindow* window() const;
    OnboardingViewModel* viewModel() const;

    // Az ablak ehhez tartozzon az ablakkezelő szerint (és első megnyitáskor fölé középre kerül).
    void setTransientParent(QWindow* parent);

signals:
    void closed();
    // „Beállítás most” — a Beállítások egy lapja ("providers").
    void openSettingsRequested(const QString& page);
    void themeModeSaved(const QString& mode);
    // Egy lépés elfogadása a beállításokat módosította.
    void saved();

private:
    bool ensureWindow();
    void centerOverParent();

    QPointer<tanara::AppController> m_controller;
    QPointer<SettingsDialogs> m_dialogs;
    QQmlEngine* m_engine = nullptr;
    QPointer<QQuickWindow> m_window;
    QPointer<OnboardingViewModel> m_vm;
    QPointer<QWindow> m_transientParent;
    bool m_autoOpened = false;
};

} // namespace tanara_qml
