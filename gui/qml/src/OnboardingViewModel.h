#pragma once
//
// OnboardingViewModel: view model of the "Getting started" window (OnboardingWindow.qml).
//
// Six steps: welcome -> you (name, language, theme) -> folders -> providers (status + "Set up
// now") -> watcher (call watcher: start at sign-in) -> done. NOTHING is mandatory: every
// step has a "Skip" button, the window can be closed any time, and every value can be changed
// later in Settings.
//
// Draft model, per step: on opening (reload) two copies of the core's settings are made:
// `base` and `draft`. "Next" applies the DIFFERENCE of the CURRENT step's fields onto the
// core's currently effective settings (it touches no other field, like the Settings
// window); "Skip" discards the step's draft and moves on. The name goes through the core's
// separate operation (ISettingsFacade::setUserSpeakerName, which updates the person DB too);
// the theme shows immediately in the running UI (preview) and is kept only when the step is
// accepted (themeModeSaved: the main window remembers it). On close the unsaved step is lost
// (discardPending) and `onboardingDone` becomes true (markDone), so the window opens by
// itself only once.
//
// It works on a tanara::app::ISettingsFacade: the QML default constructor takes AppContext::
// services()'s; on the fictional one (tanara_demo: --qml-shot, tests) `demoState` ("welcome" |
// "you" | "folders" | "providers" | "watcher" | "done") picks the step.
//
#include "tanara/app/AppSettings.h"
#include "tanara/app/ISettingsFacade.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace tanara {
namespace providers { class ProviderRegistry; }
}

namespace tanara_qml {

class SettingsDialogs;

class OnboardingViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* dialogs READ dialogsObject WRITE setDialogsObject NOTIFY dialogsChanged)
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)
    Q_PROPERTY(bool demo READ demo NOTIFY facadeChanged)

    // ---- steps ----
    // [{ key, label, icon }]: the order is the wizard's order.
    Q_PROPERTY(QVariantList steps READ steps CONSTANT)
    Q_PROPERTY(QString step READ step WRITE setStep NOTIFY stepChanged)
    Q_PROPERTY(int stepIndex READ stepIndex NOTIFY stepChanged)
    Q_PROPERTY(int stepCount READ stepCount CONSTANT)
    // Per step: "" (not visited yet) | "done" (accepted) | "skipped" (skipped).
    Q_PROPERTY(QVariantMap stepStatus READ stepStatus NOTIFY stepStatusChanged)

    // ---- Te ----
    Q_PROPERTY(QString userName READ userName WRITE setUserName NOTIFY youChanged)
    Q_PROPERTY(QString userNameError READ userNameError NOTIFY youChanged)
    Q_PROPERTY(QString uiLanguage READ uiLanguage WRITE setUiLanguage NOTIFY youChanged)
    Q_PROPERTY(QVariantList uiLanguageOptions READ uiLanguageOptions CONSTANT)
    Q_PROPERTY(bool languageNeedsRestart READ languageNeedsRestart NOTIFY youChanged)
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY youChanged)

    // ---- Folders ----
    // [{ key: audio|notes, label, hint, path, isDefault, defaultPath, locked }]
    Q_PROPERTY(QVariantList folders READ folders NOTIFY foldersChanged)

    // ---- Services ----
    Q_PROPERTY(QString sttStatus READ sttStatus NOTIFY readinessChanged)
    Q_PROPERTY(QString llmStatus READ llmStatus NOTIFY readinessChanged)
    Q_PROPERTY(bool sttReady READ sttReady NOTIFY readinessChanged)
    Q_PROPERTY(bool llmReady READ llmReady NOTIFY readinessChanged)

    // ---- Call watcher ----
    Q_PROPERTY(bool watcherAutostart READ watcherAutostart WRITE setWatcherAutostart NOTIFY watcherChanged)
    Q_PROPERTY(QString autostartNote READ autostartNote NOTIFY watcherChanged)

    // ---- summary ----
    // The current step has an unaccepted change.
    Q_PROPERTY(bool stepDirty READ stepDirty NOTIFY dirtyChanged)
    // After the window closes it no longer opens by itself.
    Q_PROPERTY(bool done READ isDone NOTIFY doneChanged)

public:
    // QML: the facade of AppContext::services() (the fictional one follows demoState).
    explicit OnboardingViewModel(QObject* parent = nullptr);
    // An explicit facade (tests, hosts).
    explicit OnboardingViewModel(tanara::app::ISettingsFacade* facade, QObject* parent = nullptr);
    ~OnboardingViewModel() override;

    tanara::app::ISettingsFacade* facade() const { return m_facade; }
    QObject* dialogsObject() const;
    void setDialogsObject(QObject* dialogs);
    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);
    bool demo() const { return !m_live; }

    QVariantList steps() const;
    QString step() const { return m_step; }
    void setStep(const QString& step);
    int stepIndex() const;
    int stepCount() const;
    QVariantMap stepStatus() const { return m_status; }

    QString userName() const { return m_draft.recording.userSpeakerName; }
    void setUserName(const QString& name);
    QString userNameError() const;
    QString uiLanguage() const { return m_draft.ui.language; }
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

    bool watcherAutostart() const { return m_draft.watcher.autostart; }
    void setWatcherAutostart(bool on);
    QString autostartNote() const;

    bool stepDirty() const;
    bool isDone() const { return m_done; }

    // ---- actions ----
    // Accepts the current step (difference save) and moves on. False: invalid field (empty
    // name): nothing is written, the step stays. On the last step = finish().
    Q_INVOKABLE bool next();
    // Discards the current step's draft and moves on (on the last step = finish()).
    Q_INVOKABLE void skip();
    // Back to the previous step (the draft is kept).
    Q_INVOKABLE void back();
    // Done: saves the flag and closes the window (closeRequested).
    Q_INVOKABLE void finish();
    // When the window closes: discards the unaccepted step (the theme preview reverts too).
    Q_INVOKABLE void discardPending();
    // `onboardingDone` = true in the core's settings (if not yet). Touches no other field.
    Q_INVOKABLE void markDone();
    // Reload from the core (on opening): draft = base, the first step, state flags cleared.
    Q_INVOKABLE void reload();

    Q_INVOKABLE void browseFolder(const QString& key);
    Q_INVOKABLE void setFolder(const QString& key, const QString& path);
    Q_INVOKABLE void resetFolder(const QString& key);
    // "Set up now": the Settings > Services page (openSettingsRequested("providers")).
    Q_INVOKABLE void openServices();
    // Recomputes the providers' status rows (the host calls it after Settings saves).
    Q_INVOKABLE void refreshReadiness();

    // For tests / the host: the provider's short name ("LM Studio · gemma-4-12b").
    static QString providerLabel(const tanara::app::AppSettings& s, bool stt,
                                 const tanara::providers::ProviderRegistry& registry);

signals:
    void facadeChanged();
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

    // The window should close (Done / the last step).
    void closeRequested();
    // A Settings page should open ("providers").
    void openSettingsRequested(const QString& page);
    // Accepted theme ("system" | "light" | "dark"): the enclosing process remembers it.
    void themeModeSaved(const QString& mode);
    // The settings changed by accepting a step (the host passes it on to the bridge).
    void saved();

private:
    void resolveServices();
    void useFacade(tanara::app::ISettingsFacade* facade);
    void loadFromCore();
    bool applyStep(const QString& step);
    void discardStep(const QString& step);
    void advance(const QString& mark);
    void setStatus(const QString& step, const QString& mark);
    void emitAllChanged();
    QString defaultFolder(const QString& key) const;
    QString folderPath(const QString& key) const;

    QPointer<tanara::app::ISettingsFacade> m_facade;
    bool m_explicitFacade = false;
    bool m_live = false;
    QPointer<SettingsDialogs> m_dialogs;
    QString m_demoState;
    QList<QMetaObject::Connection> m_connections;

    tanara::app::AppSettings m_base, m_draft;
    QString m_baseTheme, m_draftTheme;
    QString m_step = QStringLiteral("welcome");
    QVariantMap m_status;
    bool m_done = false;
    bool m_applying = false;

    QString m_sttStatus, m_llmStatus;
    bool m_sttReady = false, m_llmReady = false;
};

} // namespace tanara_qml
