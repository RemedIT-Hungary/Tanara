#pragma once
//
// OnboardingViewModel — az „Első lépések” ablak (OnboardingWindow.qml, K14) nézetmodellje.
//
// Hat lépés: welcome (Üdvözlés) → you (Te: név, nyelv, téma) → folders (Mappák) → providers
// (Szolgáltatások: állapot + „Beállítás most”) → watcher (Hívásfigyelő: indítás bejelentkezéskor)
// → done (Kész). SEMMI sem kötelező: minden lépésnek van „Kihagyom” gombja, az ablak bármikor
// bezárható, és minden érték később a Beállításokban is módosítható.
//
// Piszkozat-modell, lépésenként: megnyitáskor (reload) a core beállításairól két másolat készül
// — `alap` és `piszkozat`. A „Tovább” (next) az AKTUÁLIS lépés mezőinek KÜLÖNBSÉGÉT vezeti rá a
// core éppen érvényes beállításaira (a többi mezőhöz nem nyúl — a Beállítások-ablak mintája),
// a „Kihagyom” (skip) a lépés piszkozatát eldobja és továbblép. A név a core külön műveletén
// megy (AppController::setUserSpeakerName — a személy-DB-t is átvezeti), a téma a futó
// felületen azonnal látszik (előnézet), és csak a lépés elfogadásakor marad meg
// (themeModeSaved — a főablak jegyzi meg). Bezáráskor a mentetlen lépés elvész (discardPending),
// és az `onboardingDone` igazra áll (markDone) — így az ablak csak egyszer nyílik meg magától.
//
// Controller nélkül (--qml-shot, tesztek) KITALÁLT adatot ad; a `demoState`
// ("welcome" | "you" | "folders" | "providers" | "watcher" | "done") a lépést állítja be.
//
#include "tanara/Types.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace tanara {
class AppController;
}

namespace tanara_qml {

class SettingsDialogs;

class OnboardingViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* controller READ controllerObject WRITE setControllerObject NOTIFY controllerChanged)
    Q_PROPERTY(QObject* dialogs READ dialogsObject WRITE setDialogsObject NOTIFY dialogsChanged)
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)
    Q_PROPERTY(bool demo READ demo NOTIFY controllerChanged)

    // ---- lépések ----
    // [{ key, label, icon }] — a sorrend a varázsló sorrendje.
    Q_PROPERTY(QVariantList steps READ steps CONSTANT)
    Q_PROPERTY(QString step READ step WRITE setStep NOTIFY stepChanged)
    Q_PROPERTY(int stepIndex READ stepIndex NOTIFY stepChanged)
    Q_PROPERTY(int stepCount READ stepCount CONSTANT)
    // Lépésenként: "" (még nem járt ott) | "done" (elfogadta) | "skipped" (kihagyta).
    Q_PROPERTY(QVariantMap stepStatus READ stepStatus NOTIFY stepStatusChanged)

    // ---- Te ----
    Q_PROPERTY(QString userName READ userName WRITE setUserName NOTIFY youChanged)
    Q_PROPERTY(QString userNameError READ userNameError NOTIFY youChanged)
    Q_PROPERTY(QString uiLanguage READ uiLanguage WRITE setUiLanguage NOTIFY youChanged)
    Q_PROPERTY(QVariantList uiLanguageOptions READ uiLanguageOptions CONSTANT)
    Q_PROPERTY(bool languageNeedsRestart READ languageNeedsRestart NOTIFY youChanged)
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY youChanged)

    // ---- Mappák ----
    // [{ key: audio|notes, label, hint, path, isDefault, defaultPath, locked }]
    Q_PROPERTY(QVariantList folders READ folders NOTIFY foldersChanged)

    // ---- Szolgáltatások ----
    Q_PROPERTY(QString sttStatus READ sttStatus NOTIFY readinessChanged)
    Q_PROPERTY(QString llmStatus READ llmStatus NOTIFY readinessChanged)
    Q_PROPERTY(bool sttReady READ sttReady NOTIFY readinessChanged)
    Q_PROPERTY(bool llmReady READ llmReady NOTIFY readinessChanged)
    // Élő (bejelentkezős) Tanara Cloud-mód — csak ekkor választható a Cloud.
    Q_PROPERTY(bool cloudLive READ cloudLive NOTIFY readinessChanged)
    // A felhasználó a Tanara Cloudot választotta (élő módban mindkét lépés a Cloudon fut):
    // a lépés ilyenkor a saját kulcsos magyarázatot nem mutatja.
    Q_PROPERTY(bool cloudChosen READ cloudChosen NOTIFY readinessChanged)

    // ---- Hívásfigyelő ----
    Q_PROPERTY(bool watcherAutostart READ watcherAutostart WRITE setWatcherAutostart NOTIFY watcherChanged)
    Q_PROPERTY(QString autostartNote READ autostartNote NOTIFY watcherChanged)

    // ---- összesítés ----
    // Az aktuális lépésnek van el nem fogadott változása.
    Q_PROPERTY(bool stepDirty READ stepDirty NOTIFY dirtyChanged)
    // Az ablak bezárása után már nem nyílik meg magától.
    Q_PROPERTY(bool done READ isDone NOTIFY doneChanged)

public:
    explicit OnboardingViewModel(QObject* parent = nullptr);
    ~OnboardingViewModel() override;

    QObject* controllerObject() const;
    void setControllerObject(QObject* controller);
    tanara::AppController* controller() const;
    void setController(tanara::AppController* controller);
    QObject* dialogsObject() const;
    void setDialogsObject(QObject* dialogs);
    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);
    bool demo() const { return m_controller.isNull(); }

    QVariantList steps() const;
    QString step() const { return m_step; }
    void setStep(const QString& step);
    int stepIndex() const;
    int stepCount() const;
    QVariantMap stepStatus() const { return m_status; }

    QString userName() const { return m_draft.userSpeakerName; }
    void setUserName(const QString& name);
    QString userNameError() const;
    QString uiLanguage() const { return m_draft.uiLanguage; }
    void setUiLanguage(const QString& lang);
    QVariantList uiLanguageOptions() const;
    bool languageNeedsRestart() const;
    QString themeMode() const { return m_draftTheme; }
    void setThemeMode(const QString& mode);

    QVariantList folders() const;

    QString sttStatus() const { return m_sttStatus; }
    QString llmStatus() const { return m_llmStatus; }
    bool sttReady() const { return m_sttReady; }
    bool llmReady() const { return m_llmReady; }
    bool cloudLive() const { return m_cloudLive; }
    bool cloudChosen() const { return m_cloudChosen; }

    bool watcherAutostart() const { return m_draft.watcherAutostart; }
    void setWatcherAutostart(bool on);
    QString autostartNote() const;

    bool stepDirty() const;
    bool isDone() const { return m_done; }

    // ---- műveletek ----
    // Az aktuális lépés elfogadása (különbség-mentés) és tovább. false: érvénytelen mező (üres
    // név) — ilyenkor semmi nem íródik, a lépés marad. Az utolsó lépésen = finish().
    Q_INVOKABLE bool next();
    // Az aktuális lépés piszkozatának eldobása és tovább (az utolsó lépésen = finish()).
    Q_INVOKABLE void skip();
    // Vissza az előző lépésre (a piszkozat megmarad).
    Q_INVOKABLE void back();
    // Kész: a jelző mentése és az ablak bezárása (closeRequested).
    Q_INVOKABLE void finish();
    // Az ablak bezárásakor: a el nem fogadott lépés eldobása (a téma-előnézet is visszaáll).
    Q_INVOKABLE void discardPending();
    // `onboardingDone` = true a core beállításaiban (ha még nem az). Más mezőhöz nem nyúl.
    Q_INVOKABLE void markDone();
    // Újratöltés a core-ból (megnyitáskor): piszkozat = alap, az első lépés, állapot-jelek törölve.
    Q_INVOKABLE void reload();

    Q_INVOKABLE void browseFolder(const QString& key);
    Q_INVOKABLE void setFolder(const QString& key, const QString& path);
    Q_INVOKABLE void resetFolder(const QString& key);
    // „Beállítás most”: a Beállítások › Szolgáltatások lap (openSettingsRequested("providers")).
    Q_INVOKABLE void openServices();
    // A szolgáltatók állapot-sorának újraszámolása (a Beállítások mentése után a gazda hívja).
    Q_INVOKABLE void refreshReadiness();

    // Tesztekhez / a gazdának: a szolgáltató rövid neve („LM Studio · gemma-4-12b”).
    static QString providerLabel(const tanara::AppSettings& s, bool stt);

signals:
    void controllerChanged();
    void dialogsChanged();
    void demoStateChanged();
    void stepChanged();
    void stepStatusChanged();
    void youChanged();
    void foldersChanged();
    void readinessChanged();
    void watcherChanged();
    void dirtyChanged();
    void doneChanged();

    // Az ablak bezárandó (Kész / az utolsó lépés).
    void closeRequested();
    // A Beállítások egy lapja nyitandó ("providers").
    void openSettingsRequested(const QString& page);
    // Elfogadott téma ("system" | "light" | "dark") — a befoglaló folyamat jegyzi meg.
    void themeModeSaved(const QString& mode);
    // A beállítások egy lépés elfogadásával változtak (a gazda a hídnak továbbadja).
    void saved();

private:
    void attach();
    void loadFromCore();
    void loadDemo();
    bool applyStep(const QString& step);
    void discardStep(const QString& step);
    void advance(const QString& mark);
    void setStatus(const QString& step, const QString& mark);
    void emitAllChanged();
    QString defaultFolder(const QString& key) const;
    QString folderPath(const QString& key) const;

    QPointer<tanara::AppController> m_controller;
    bool m_controllerSet = false;
    QPointer<SettingsDialogs> m_dialogs;
    QString m_demoState;
    QList<QMetaObject::Connection> m_connections;

    tanara::AppSettings m_base, m_draft;
    QString m_baseTheme, m_draftTheme;
    QString m_step = QStringLiteral("welcome");
    QVariantMap m_status;
    bool m_done = false;
    bool m_applying = false;

    QString m_sttStatus, m_llmStatus;
    bool m_sttReady = false, m_llmReady = false;
    bool m_cloudLive = false, m_cloudChosen = false;
};

} // namespace tanara_qml
