#include "OnboardingViewModel.h"
#include "tanara/providers/core/ProviderConfig.h"

#include "DemoText.h"

#include "AppContext.h"
#include "SettingsDialogs.h"

#include "tanara/app/AppController.h"
#include "tanara/foundation/i18n/Localization.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/watcher/detect/Autostart.h"
#include "tanara/providers/core/ConnectionTester.h"
#include "tanara/providers/core/ProviderRegistry.h"
#include "tanara/providers/core/ReadinessModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QUrl>

using namespace tanara;

namespace tanara_qml {

namespace {

const QStringList kSteps{QStringLiteral("welcome"), QStringLiteral("you"), QStringLiteral("folders"),
                         QStringLiteral("providers"), QStringLiteral("watcher"), QStringLiteral("done")};

QString resolvedUiLanguage(const QString& setting)
{
    if (setting == QLatin1String("hu") || setting == QLatin1String("en")) return setting;
    return QLocale::system().language() == QLocale::Hungarian ? QStringLiteral("hu")
                                                              : QStringLiteral("en");
}

// Whether two folder paths are the same (~ expanded, without the trailing slash).
bool samePath(const QString& a, const QString& b)
{
    return QDir::cleanPath(paths::expandHome(a.trimmed())) == QDir::cleanPath(paths::expandHome(b.trimmed()));
}

// A step's status line from readiness. The fictional meeting has an audio track and a
// transcript, so only the provider setting counts (like the Settings draft validation).
struct Line { QString text; bool ready = false; };

Line readinessLine(const tanara::app::AppSettings& s, bool stt, const providers::ReadinessModel::SecretProbe& hasSecret)
{
    Meeting m;
    Track t;
    t.active = true;
    m.tracks << t;
    m.hasTranscript = true;
    const providers::ReadinessResult r = providers::ReadinessModel(s.providers, hasSecret).check(stt ? providers::WorkflowStep::Transcribe
                                                                       : providers::WorkflowStep::Summarize, m);
    const QString head = stt ? OnboardingViewModel::tr("Átírás") : OnboardingViewModel::tr("Összefoglaló");
    Line out;
    out.ready = r.runnable;
    if (r.runnable) {
        out.text = head + QStringLiteral(": ") + OnboardingViewModel::providerLabel(s, stt);
        return out;
    }
    if (r.blockerKind == providers::BlockerKind::Auth) {
        out.text = head + QStringLiteral(": ") + OnboardingViewModel::tr("nincs bejelentkezve");
        return out;
    }
    // A missing secret (API key) or another required field?
    bool secret = false;
    const providers::ProviderDescriptor d = stt ? providers::ProviderRegistry::defaultInstance().descriptor(providers::CapabilityKind::Transcription, r.providerId)
                                     : providers::ProviderRegistry::defaultInstance().descriptor(providers::CapabilityKind::Chat, r.providerId);
    for (const providers::ConfigField& f : d.fields)
        if (f.key == r.missingFieldKey) secret = f.isSecret;
    out.text = head + QStringLiteral(": ")
             + (secret ? OnboardingViewModel::tr("nincs kulcs") : OnboardingViewModel::tr("hiányos beállítás"));
    return out;
}

} // namespace

// ---- lifecycle ------------------------------------------------------------------------

OnboardingViewModel::OnboardingViewModel(QObject* parent) : QObject(parent)
{
    m_baseTheme = m_draftTheme = AppContext::instance()->themeMode();
    // App.controller binds in the next event-loop turn, if by then the caller has not given its
    // own and has not asked for a demo state.
    QMetaObject::invokeMethod(this, [this] {
        if (m_controllerSet) return;
        if (app::AppController* c = AppContext::instance()->controller())
            setController(c);
        else
            setDemoState(m_demoState.isEmpty() ? QStringLiteral("welcome") : m_demoState);
    }, Qt::QueuedConnection);
}

OnboardingViewModel::~OnboardingViewModel() = default;

QObject* OnboardingViewModel::controllerObject() const { return m_controller; }
app::AppController* OnboardingViewModel::controller() const { return m_controller; }
QObject* OnboardingViewModel::dialogsObject() const { return m_dialogs; }

void OnboardingViewModel::setControllerObject(QObject* controller)
{
    setController(qobject_cast<app::AppController*>(controller));
}

void OnboardingViewModel::setController(app::AppController* controller)
{
    if (m_controllerSet && m_controller == controller) return;
    m_controller = controller;
    m_controllerSet = true;
    attach();
    emit controllerChanged();
}

void OnboardingViewModel::setDialogsObject(QObject* dialogs)
{
    auto* d = qobject_cast<SettingsDialogs*>(dialogs);
    if (m_dialogs == d) return;
    m_dialogs = d;
    emit dialogsChanged();
}

void OnboardingViewModel::setDemoState(const QString& state)
{
    if (m_demoState != state) {
        m_demoState = state;
        emit demoStateChanged();
    }
    if (m_controller) return;
    m_controllerSet = true;
    loadDemo();
}

void OnboardingViewModel::attach()
{
    for (const QMetaObject::Connection& c : std::as_const(m_connections)) disconnect(c);
    m_connections.clear();
    app::AppController* c = m_controller;
    if (!c) {
        loadDemo();
        return;
    }
    m_connections << connect(c->settings(), &app::SettingsManager::settingsChanged, this, [this] {
        if (m_applying || !m_controller) return;
        // Someone else saved (Settings, cloud window): untouched fields follow, the typed draft stays.
        const tanara::app::AppSettings live = m_controller->settings()->settings();
        auto follow = [](auto& base, auto& draft, const auto& now) {
            if (draft == base) draft = now;
            base = now;
        };
        follow(m_base.recording.userSpeakerName, m_draft.recording.userSpeakerName, live.recording.userSpeakerName);
        follow(m_base.ui.language, m_draft.ui.language, live.ui.language);
        // The two folders follow separately (a typed notes folder must not hold back the recordings).
        QString baseRoot = m_base.paths.primaryRecordingRoot(), draftRoot = m_draft.paths.primaryRecordingRoot();
        follow(baseRoot, draftRoot, live.paths.primaryRecordingRoot());
        m_base.paths.setPrimaryRecordingRoot(baseRoot);
        m_draft.paths.setPrimaryRecordingRoot(draftRoot);
        follow(m_base.paths.notesDir, m_draft.paths.notesDir, live.paths.notesDir);
        follow(m_base.watcher.autostart, m_draft.watcher.autostart, live.watcher.autostart);
        m_base.ui.onboardingDone = live.ui.onboardingDone;
        if (m_done != live.ui.onboardingDone) {
            m_done = live.ui.onboardingDone;
            emit doneChanged();
        }
        emitAllChanged();
        refreshReadiness();
    });
    loadFromCore();
}

void OnboardingViewModel::loadFromCore()
{
    app::AppController* c = m_controller;
    if (!c) return;
    m_base = m_draft = c->settings()->settings();
    m_baseTheme = m_draftTheme = AppContext::instance()->themeMode();
    m_done = m_base.ui.onboardingDone;
    emitAllChanged();
    emit doneChanged();
    refreshReadiness();
}

void OnboardingViewModel::reload()
{
    // If the close did not discard it (e.g. the host did not get the signal), we do it now.
    if (m_draftTheme != m_baseTheme) AppContext::instance()->setThemeMode(m_baseTheme);
    m_status.clear();
    m_step = kSteps.first();
    if (m_controller) loadFromCore();
    else loadDemo();
    emit stepStatusChanged();
    emit stepChanged();
}

void OnboardingViewModel::emitAllChanged()
{
    emit youChanged();
    emit foldersChanged();
    emit watcherChanged();
    emit dirtyChanged();
}

// ---- steps ---------------------------------------------------------------------------

QVariantList OnboardingViewModel::steps() const
{
    auto mk = [](const QString& key, const QString& label, const QString& icon) {
        return QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("label"), label},
                           {QStringLiteral("icon"), icon}};
    };
    return {mk(kSteps[0], tr("Üdvözlés"), QStringLiteral("sparkles")),
            mk(kSteps[1], tr("Te"), QStringLiteral("user")),
            mk(kSteps[2], tr("Mappák"), QStringLiteral("folder-open")),
            mk(kSteps[3], tr("Szolgáltatások"), QStringLiteral("plug")),
            mk(kSteps[4], tr("Hívásfigyelő"), QStringLiteral("radar")),
            mk(kSteps[5], tr("Kész"), QStringLiteral("circle-check"))};
}

int OnboardingViewModel::stepIndex() const { return int(kSteps.indexOf(m_step)); }
int OnboardingViewModel::stepCount() const { return int(kSteps.size()); }

void OnboardingViewModel::setStep(const QString& step)
{
    if (!kSteps.contains(step) || step == m_step) return;
    m_step = step;
    emit stepChanged();
    emit dirtyChanged();
    if (step == QLatin1String("providers")) refreshReadiness();
}

void OnboardingViewModel::setStatus(const QString& step, const QString& mark)
{
    if (m_status.value(step).toString() == mark) return;
    m_status.insert(step, mark);
    emit stepStatusChanged();
}

void OnboardingViewModel::advance(const QString& mark)
{
    setStatus(m_step, mark);
    const int i = stepIndex();
    if (i + 1 < kSteps.size()) setStep(kSteps.at(i + 1));
}

bool OnboardingViewModel::next()
{
    if (m_step == kSteps.last()) {
        finish();
        return true;
    }
    if (!applyStep(m_step)) return false;
    advance(QStringLiteral("done"));
    return true;
}

void OnboardingViewModel::skip()
{
    if (m_step == kSteps.last()) {
        finish();
        return;
    }
    discardStep(m_step);
    advance(QStringLiteral("skipped"));
}

void OnboardingViewModel::back()
{
    const int i = stepIndex();
    if (i > 0) setStep(kSteps.at(i - 1));
}

void OnboardingViewModel::finish()
{
    setStatus(m_step, QStringLiteral("done"));
    markDone();
    emit closeRequested();
}

void OnboardingViewModel::discardPending()
{
    // The draft of every unaccepted step (with Back several can stay open).
    for (const QString& s : kSteps) discardStep(s);
}

void OnboardingViewModel::markDone()
{
    if (!m_controller) {
        if (!m_done) {
            m_done = true;
            emit doneChanged();
        }
        return;
    }
    tanara::app::AppSettings live = m_controller->settings()->settings();
    if (!live.ui.onboardingDone) {
        live.ui.onboardingDone = true;
        m_applying = true;
        m_controller->settings()->setSettings(live);
        m_applying = false;
    }
    m_base.ui.onboardingDone = m_draft.ui.onboardingDone = true;
    if (!m_done) {
        m_done = true;
        emit doneChanged();
    }
}

bool OnboardingViewModel::stepDirty() const
{
    if (m_step == QLatin1String("you"))
        return m_draft.recording.userSpeakerName != m_base.recording.userSpeakerName || m_draft.ui.language != m_base.ui.language
               || m_draftTheme != m_baseTheme;
    if (m_step == QLatin1String("folders"))
        return !(m_draft.paths == m_base.paths);
    if (m_step == QLatin1String("watcher"))
        return m_draft.watcher.autostart != m_base.watcher.autostart;
    return false;
}

// The given step's fields' difference -> onto the core's currently effective settings.
bool OnboardingViewModel::applyStep(const QString& step)
{
    const bool you = step == QLatin1String("you");
    const bool folders = step == QLatin1String("folders");
    const bool watcher = step == QLatin1String("watcher");
    if (!you && !folders && !watcher) return true;
    if (you && !userNameError().isEmpty()) return false;

    const QString newName = m_draft.recording.userSpeakerName.trimmed();
    const bool nameChanged = you && newName != m_base.recording.userSpeakerName.trimmed();
    const bool themeChanged = you && m_draftTheme != m_baseTheme;
    const bool autostartChanged = watcher && m_draft.watcher.autostart != m_base.watcher.autostart;

    if (app::AppController* c = m_controller) {
        m_applying = true;
        tanara::app::AppSettings live = c->settings()->settings();
        const tanara::app::AppSettings before = live;
        if (you && m_draft.ui.language != m_base.ui.language) live.ui.language = m_draft.ui.language;
        if (folders && m_draft.paths.primaryRecordingRoot() != m_base.paths.primaryRecordingRoot())
            live.paths.setPrimaryRecordingRoot(m_draft.paths.primaryRecordingRoot().trimmed());
        if (folders && m_draft.paths.notesDir != m_base.paths.notesDir) live.paths.notesDir = m_draft.paths.notesDir.trimmed();
        if (autostartChanged) live.watcher.autostart = m_draft.watcher.autostart;
        const bool settingsChanged = live.ui.language != before.ui.language || !(live.paths == before.paths)
                                     || live.watcher.autostart != before.watcher.autostart;
        if (settingsChanged) c->settings()->setSettings(live);
        // The own name: the core carries it through the person DB, the meetings and the voiceprints.
        if (nameChanged) c->setUserSpeakerName(newName);
        // Starting the watcher at sign-in: effective immediately (never in a sandbox / test).
        if (autostartChanged)
            watcher::autostart::applyWatcher(m_draft.watcher.autostart,
                                    watcher::autostart::findWatcherExecutable(QCoreApplication::applicationDirPath()));
        m_applying = false;
        const tanara::app::AppSettings now = c->settings()->settings();
        if (you) {
            m_base.recording.userSpeakerName = m_draft.recording.userSpeakerName = now.recording.userSpeakerName;
            m_base.ui.language = m_draft.ui.language = now.ui.language;
        }
        if (folders) {
            m_base.paths = m_draft.paths = now.paths;
        }
        if (watcher) m_base.watcher.autostart = m_draft.watcher.autostart = now.watcher.autostart;
        if (settingsChanged || nameChanged) emit saved();
    } else {
        // Demo: nothing to write to disk; the draft becomes the base.
        m_draft.recording.userSpeakerName = newName.isEmpty() ? m_draft.recording.userSpeakerName : newName;
        if (you) {
            m_base.recording.userSpeakerName = m_draft.recording.userSpeakerName;
            m_base.ui.language = m_draft.ui.language;
        }
        if (folders) {
            m_base.paths = m_draft.paths;
        }
        if (watcher) m_base.watcher.autostart = m_draft.watcher.autostart;
    }
    if (themeChanged) {
        m_baseTheme = m_draftTheme;
        AppContext::instance()->setThemeMode(m_draftTheme);
        emit themeModeSaved(m_draftTheme);
    }
    emitAllChanged();
    return true;
}

void OnboardingViewModel::discardStep(const QString& step)
{
    if (step == QLatin1String("you")) {
        m_draft.recording.userSpeakerName = m_base.recording.userSpeakerName;
        m_draft.ui.language = m_base.ui.language;
        if (m_draftTheme != m_baseTheme) {
            m_draftTheme = m_baseTheme;
            AppContext::instance()->setThemeMode(m_baseTheme);   // the preview reverts
        }
        emit youChanged();
    } else if (step == QLatin1String("folders")) {
        m_draft.paths = m_base.paths;
        emit foldersChanged();
    } else if (step == QLatin1String("watcher")) {
        m_draft.watcher.autostart = m_base.watcher.autostart;
        emit watcherChanged();
    }
    emit dirtyChanged();
}

// ---- Te ----------------------------------------------------------------------------------

void OnboardingViewModel::setUserName(const QString& name)
{
    if (m_draft.recording.userSpeakerName == name) return;
    m_draft.recording.userSpeakerName = name;
    emit youChanged();
    emit dirtyChanged();
}

QString OnboardingViewModel::userNameError() const
{
    return m_draft.recording.userSpeakerName.trimmed().isEmpty()
        ? tr("Adj meg egy nevet — ezen a néven szerepelsz az átiratokban.") : QString();
}

void OnboardingViewModel::setUiLanguage(const QString& lang)
{
    if (m_draft.ui.language == lang) return;
    m_draft.ui.language = lang;
    emit youChanged();
    emit dirtyChanged();
}

QVariantList OnboardingViewModel::uiLanguageOptions() const
{
    // The same three choices as on the Settings > General page.
    auto mk = [](const QString& v, const QString& l) {
        return QVariantMap{{QStringLiteral("value"), v}, {QStringLiteral("label"), l}};
    };
    const bool sysHu = QLocale::system().language() == QLocale::Hungarian;
    return {mk(QStringLiteral("auto"), sysHu ? tr("Rendszer nyelve (magyar)") : tr("Rendszer nyelve (English)")),
            mk(QStringLiteral("hu"), QStringLiteral("Magyar")),
            mk(QStringLiteral("en"), QStringLiteral("English"))};
}

bool OnboardingViewModel::languageNeedsRestart() const
{
    if (!m_controller) return m_draft.ui.language != m_base.ui.language;
    const QString active = activeUiLanguage();
    return !active.isEmpty() && resolvedUiLanguage(m_draft.ui.language) != active;
}

void OnboardingViewModel::setThemeMode(const QString& mode)
{
    const QString m = AppContext::normalizedThemeMode(mode);
    if (m == m_draftTheme) return;
    m_draftTheme = m;
    AppContext::instance()->setThemeMode(m);   // live preview
    emit youChanged();
    emit dirtyChanged();
}

// ---- Folders ------------------------------------------------------------------------------

QString OnboardingViewModel::folderPath(const QString& key) const
{
    // The configured ("paths" section) value, else the default folder.
    QString configured;
    if (key == QLatin1String("audio")) configured = m_draft.paths.primaryRecordingRoot();
    else if (key == QLatin1String("notes")) configured = m_draft.paths.notesDir;
    else return QString();
    return configured.isEmpty() ? defaultFolder(key) : configured;
}

QString OnboardingViewModel::defaultFolder(const QString& key) const
{
    if (!m_controller) {
        // Demo: the fictional user's default folders.
        return key == QLatin1String("audio") ? demoText("/home/lilla/Tanara/felvetelek", "/home/emma/Tanara/recordings")
                                             : demoText("/home/lilla/Tanara/jegyzetek", "/home/emma/Tanara/notes");
    }
    // The effective folder without a "paths" setting (with TANARA_HOME: the sandbox's).
    return paths::AppPaths::system(paths::PathSettings())
        .dir(key == QLatin1String("audio") ? paths::Role::Recordings : paths::Role::Notes);
}

QVariantList OnboardingViewModel::folders() const
{
    auto mk = [this](const QString& key, const QString& label, const QString& hint) {
        const QString path = folderPath(key);
        const QString def = defaultFolder(key);
        return QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("label"), label},
                           {QStringLiteral("hint"), hint}, {QStringLiteral("path"), path},
                           {QStringLiteral("defaultPath"), def},
                           {QStringLiteral("isDefault"), samePath(path, def)}};
    };
    return {mk(QStringLiteral("audio"), tr("Felvételek"),
               tr("Hangsávok, megbeszélésenként egy mappa. Az új felvételek ide kerülnek.")),
            mk(QStringLiteral("notes"), tr("Jegyzetek"),
               tr("Az összefoglalók másolata sima Markdown-fájlként (pl. a jegyzettáradba)."))};
}

void OnboardingViewModel::setFolder(const QString& key, const QString& path)
{
    const QString p = path.trimmed();
    if (p.isEmpty() || p == folderPath(key)) return;
    if (key != QLatin1String("audio") && key != QLatin1String("notes")) return;
    // The default folder is stored as "no setting" (later default changes then apply).
    const QString stored = m_controller && samePath(p, defaultFolder(key)) ? QString() : p;
    if (key == QLatin1String("audio")) m_draft.paths.setPrimaryRecordingRoot(stored);
    else m_draft.paths.notesDir = stored;
    emit foldersChanged();
    emit dirtyChanged();
}

void OnboardingViewModel::resetFolder(const QString& key)
{
    setFolder(key, defaultFolder(key));
}

void OnboardingViewModel::browseFolder(const QString& key)
{
    if (!m_dialogs) return;
    const QString title = key == QLatin1String("audio") ? tr("Felvételek mappája") : tr("Jegyzetek mappája");
    QString start = paths::expandHome(folderPath(key).trimmed());
    if (start.isEmpty() || !QDir(start).exists()) start = QDir::homePath();
    const QString dir = m_dialogs->pickFolder(title, start);
    if (!dir.isEmpty()) setFolder(key, dir);
}

// ---- Services ----------------------------------------------------------------------------

QString OnboardingViewModel::providerLabel(const tanara::app::AppSettings& s, bool stt)
{
    const QString id = stt ? s.providers.stt.selectedId : s.providers.llm.selectedId;
    const providers::ProviderDescriptor d = stt ? providers::ProviderRegistry::defaultInstance().descriptor(providers::CapabilityKind::Transcription, id)
                                     : providers::ProviderRegistry::defaultInstance().descriptor(providers::CapabilityKind::Chat, id);
    if (d.id.isEmpty()) return id;
    const providers::ProviderConfig cfg = stt ? s.providers.stt.selected() : s.providers.llm.selected();
    QString label = d.displayName;
    // From the local server's well-known port we show its name (the descriptor's name is
    // generic), the same as in Settings' collapsed provider card.
    if (d.authMode != providers::AuthMode::Login && providers::isLocalEndpoint(cfg.baseUrl)) {
        const QUrl url(cfg.baseUrl.trimmed());
        if (url.port() == 1234) label = QStringLiteral("LM Studio");
        else if (url.port() == 11434) label = QStringLiteral("Ollama");
    }
    if (stt || d.authMode == providers::AuthMode::Login) return label;
    QString model = cfg.model.trimmed();
    model = model.mid(model.lastIndexOf(QLatin1Char('/')) + 1);
    return model.isEmpty() ? label : label + QStringLiteral(" · ") + model;
}

void OnboardingViewModel::refreshReadiness()
{
    QString stt, llm;
    bool sttOk = false, llmOk = false;
    if (app::AppController* c = m_controller) {
        const tanara::app::AppSettings s = c->settings()->settings();
        const providers::ReadinessModel::SecretProbe probe = [c](const QString& key) { return c->hasSecret(key); };
        const Line a = readinessLine(s, true, probe);
        const Line b = readinessLine(s, false, probe);
        stt = a.text;
        llm = b.text;
        sttOk = a.ready;
        llmOk = b.ready;
    } else {
        // Demo: a fresh install's typical state: no key yet, the local LLM entered.
        stt = tr("Átírás") + QStringLiteral(": ") + tr("nincs kulcs");
        llm = tr("Összefoglaló") + QStringLiteral(": ") + QStringLiteral("LM Studio · gemma-4-12b");
        llmOk = true;
    }
    if (stt == m_sttStatus && llm == m_llmStatus && sttOk == m_sttReady && llmOk == m_llmReady)
        return;
    m_sttStatus = stt;
    m_llmStatus = llm;
    m_sttReady = sttOk;
    m_llmReady = llmOk;
    emit readinessChanged();
}

void OnboardingViewModel::openServices()
{
    emit openSettingsRequested(QStringLiteral("providers"));
}

// ---- Call watcher ------------------------------------------------------------------------

void OnboardingViewModel::setWatcherAutostart(bool on)
{
    if (m_draft.watcher.autostart == on) return;
    m_draft.watcher.autostart = on;
    emit watcherChanged();
    emit dirtyChanged();
}

QString OnboardingViewModel::autostartNote() const
{
    QString note;
#if defined(Q_OS_LINUX)
    note = tr("Linuxon egy indítófájl kerül a ~/.config/autostart mappába; kikapcsoláskor törlődik.");
#elif defined(Q_OS_WIN)
    note = tr("Windowson egy bejegyzés kerül a felhasználó indítási listájába (Run); kikapcsoláskor törlődik.");
#else
    note = tr("Ezen a rendszeren az automatikus indítás még nem érhető el; a figyelőt kézzel indíthatod.");
#endif
    // In a sandbox (TANARA_HOME) and in tests the entry is not written; we say so too.
    if (m_controller && !watcher::autostart::managed() && !watcher::autostart::watcherEntryPath().isEmpty())
        note += QLatin1Char(' ') + tr("Most nem íródik bejegyzés (elkülönített adatmappával fut a Tanara).");
    return note;
}

// ---- demo --------------------------------------------------------------------------------

void OnboardingViewModel::loadDemo()
{
    tanara::app::AppSettings s;
    s.recording.userSpeakerName = demoName("Kovács Lilla");
    s.paths.setPrimaryRecordingRoot(demoText("/home/lilla/Tanara/felvetelek", "/home/emma/Tanara/recordings"));
    s.paths.notesDir = demoText("/home/lilla/Obsidian/Megbeszélések", "/home/emma/Obsidian/Meetings");
    s.paths.dataDir = demoText("/home/lilla/.tanara", "/home/emma/.tanara");
    s.ui.language = QStringLiteral("auto");
    s.watcher.autostart = true;
    m_base = m_draft = s;
    m_baseTheme = m_draftTheme = AppContext::instance()->themeMode();
    m_done = false;

    m_status.clear();
    const QString st = kSteps.contains(m_demoState) ? m_demoState : kSteps.first();
    // The earlier steps: one skipped (Folders), the rest accepted, so the navigation shows both marks.
    for (const QString& k : kSteps) {
        if (k == st) break;
        m_status.insert(k, k == QLatin1String("folders") ? QStringLiteral("skipped") : QStringLiteral("done"));
    }
    m_step = st;
    emit stepStatusChanged();
    emit stepChanged();
    emitAllChanged();
    emit doneChanged();
    refreshReadiness();
}

} // namespace tanara_qml
