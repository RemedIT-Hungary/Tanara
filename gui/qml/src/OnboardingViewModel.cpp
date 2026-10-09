#include "OnboardingViewModel.h"

#include "AppContext.h"
#include "SettingsDialogs.h"

#include "tanara/AppController.h"
#include "tanara/Localization.h"
#include "tanara/Paths.h"
#include "tanara/SettingsManager.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/detect/Autostart.h"
#include "tanara/provider/ConnectionTester.h"
#include "tanara/provider/ProviderRegistry.h"
#include "tanara/provider/ReadinessModel.h"

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

// Két mappa-út ugyanaz-e (a ~ kibontva, a záró perjel nélkül).
bool samePath(const QString& a, const QString& b)
{
    return QDir::cleanPath(paths::expandHome(a.trimmed())) == QDir::cleanPath(paths::expandHome(b.trimmed()));
}

// Egy lépés állapot-sora a readiness alapján. A kitalált megbeszélésben van hangsáv és
// átirat, így csak a szolgáltató beállítása számít (mint a Beállítások piszkozat-ellenőrzése).
struct Line { QString text; bool ready = false; };

Line readinessLine(const AppSettings& s, bool stt, const ReadinessModel::SecretProbe& hasSecret)
{
    Meeting m;
    Track t;
    t.active = true;
    m.tracks << t;
    m.hasTranscript = true;
    const ReadinessResult r = ReadinessModel(s, hasSecret).check(stt ? WorkflowStep::Transcribe
                                                                       : WorkflowStep::Summarize, m);
    const QString head = stt ? OnboardingViewModel::tr("Átírás") : OnboardingViewModel::tr("Összefoglaló");
    Line out;
    out.ready = r.runnable;
    if (r.runnable) {
        out.text = head + QStringLiteral(": ") + OnboardingViewModel::providerLabel(s, stt);
        return out;
    }
    if (r.blockerKind == BlockerKind::Auth) {
        out.text = head + QStringLiteral(": ") + OnboardingViewModel::tr("nincs bejelentkezve");
        return out;
    }
    // Hiányzó titok (API-kulcs) vagy más kötelező mező?
    bool secret = false;
    const ProviderDescriptor d = stt ? SttProviderRegistry::instance().descriptor(r.providerId)
                                     : LlmProviderRegistry::instance().descriptor(r.providerId);
    for (const ConfigField& f : d.fields)
        if (f.key == r.missingFieldKey) secret = f.isSecret;
    out.text = head + QStringLiteral(": ")
             + (secret ? OnboardingViewModel::tr("nincs kulcs") : OnboardingViewModel::tr("hiányos beállítás"));
    return out;
}

} // namespace

// ---- életciklus ------------------------------------------------------------------------

OnboardingViewModel::OnboardingViewModel(QObject* parent) : QObject(parent)
{
    registerBuiltinProviders();   // idempotens; controller nélkül is kell a szolgáltató-nevekhez
    m_baseTheme = m_draftTheme = AppContext::instance()->themeMode();
    // Az App.controller a következő eseményhurok-körben kötődik be, ha addig a hívó nem
    // adott sajátot és nem kért demó-állapotot.
    QMetaObject::invokeMethod(this, [this] {
        if (m_controllerSet) return;
        if (AppController* c = AppContext::instance()->controller())
            setController(c);
        else
            setDemoState(m_demoState.isEmpty() ? QStringLiteral("welcome") : m_demoState);
    }, Qt::QueuedConnection);
}

OnboardingViewModel::~OnboardingViewModel() = default;

QObject* OnboardingViewModel::controllerObject() const { return m_controller; }
AppController* OnboardingViewModel::controller() const { return m_controller; }
QObject* OnboardingViewModel::dialogsObject() const { return m_dialogs; }

void OnboardingViewModel::setControllerObject(QObject* controller)
{
    setController(qobject_cast<AppController*>(controller));
}

void OnboardingViewModel::setController(AppController* controller)
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
    AppController* c = m_controller;
    if (!c) {
        loadDemo();
        return;
    }
    m_connections << connect(c->settings(), &SettingsManager::settingsChanged, this, [this] {
        if (m_applying || !m_controller) return;
        // Valaki más mentett (Beállítások, cloud-ablak): a meg nem érintett mezők követik, a
        // begépelt piszkozat marad.
        const AppSettings live = m_controller->settings()->settings();
        auto follow = [](auto& base, auto& draft, const auto& now) {
            if (draft == base) draft = now;
            base = now;
        };
        follow(m_base.userSpeakerName, m_draft.userSpeakerName, live.userSpeakerName);
        follow(m_base.uiLanguage, m_draft.uiLanguage, live.uiLanguage);
        follow(m_base.audioDir, m_draft.audioDir, live.audioDir);
        follow(m_base.notesDir, m_draft.notesDir, live.notesDir);
        follow(m_base.watcherAutostart, m_draft.watcherAutostart, live.watcherAutostart);
        m_base.onboardingDone = live.onboardingDone;
        if (m_done != live.onboardingDone) {
            m_done = live.onboardingDone;
            emit doneChanged();
        }
        emitAllChanged();
        refreshReadiness();
    });
    loadFromCore();
}

void OnboardingViewModel::loadFromCore()
{
    AppController* c = m_controller;
    if (!c) return;
    m_base = m_draft = c->settings()->settings();
    m_baseTheme = m_draftTheme = AppContext::instance()->themeMode();
    m_done = m_base.onboardingDone;
    emitAllChanged();
    emit doneChanged();
    refreshReadiness();
}

void OnboardingViewModel::reload()
{
    // Ha a bezárás nem dobta el (pl. a gazda nem kapta meg a jelet), most tesszük.
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

// ---- lépések ---------------------------------------------------------------------------

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
    // Minden el nem fogadott lépés piszkozata (a Vissza gombbal több is nyitva maradhat).
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
    AppSettings live = m_controller->settings()->settings();
    if (!live.onboardingDone) {
        live.onboardingDone = true;
        m_applying = true;
        m_controller->settings()->setSettings(live);
        m_applying = false;
    }
    m_base.onboardingDone = m_draft.onboardingDone = true;
    if (!m_done) {
        m_done = true;
        emit doneChanged();
    }
}

bool OnboardingViewModel::stepDirty() const
{
    if (m_step == QLatin1String("you"))
        return m_draft.userSpeakerName != m_base.userSpeakerName || m_draft.uiLanguage != m_base.uiLanguage
               || m_draftTheme != m_baseTheme;
    if (m_step == QLatin1String("folders"))
        return m_draft.audioDir != m_base.audioDir || m_draft.notesDir != m_base.notesDir;
    if (m_step == QLatin1String("watcher"))
        return m_draft.watcherAutostart != m_base.watcherAutostart;
    return false;
}

// Az adott lépés mezőinek különbsége → a core éppen érvényes beállításaira.
bool OnboardingViewModel::applyStep(const QString& step)
{
    const bool you = step == QLatin1String("you");
    const bool folders = step == QLatin1String("folders");
    const bool watcher = step == QLatin1String("watcher");
    if (!you && !folders && !watcher) return true;
    if (you && !userNameError().isEmpty()) return false;

    const QString newName = m_draft.userSpeakerName.trimmed();
    const bool nameChanged = you && newName != m_base.userSpeakerName.trimmed();
    const bool themeChanged = you && m_draftTheme != m_baseTheme;
    const bool autostartChanged = watcher && m_draft.watcherAutostart != m_base.watcherAutostart;

    if (AppController* c = m_controller) {
        m_applying = true;
        AppSettings live = c->settings()->settings();
        const AppSettings before = live;
        if (you && m_draft.uiLanguage != m_base.uiLanguage) live.uiLanguage = m_draft.uiLanguage;
        if (folders && m_draft.audioDir != m_base.audioDir) live.audioDir = m_draft.audioDir.trimmed();
        if (folders && m_draft.notesDir != m_base.notesDir) live.notesDir = m_draft.notesDir.trimmed();
        if (autostartChanged) live.watcherAutostart = m_draft.watcherAutostart;
        const bool settingsChanged = live.uiLanguage != before.uiLanguage || live.audioDir != before.audioDir
                                     || live.notesDir != before.notesDir
                                     || live.watcherAutostart != before.watcherAutostart;
        if (settingsChanged) c->settings()->setSettings(live);
        // A saját név: a core átvezeti a személy-DB-n, a megbeszéléseken és a hanglenyomatokon.
        if (nameChanged) c->setUserSpeakerName(newName);
        // A figyelő indítása bejelentkezéskor: azonnal érvényes (homokozóban / tesztben soha).
        if (autostartChanged)
            autostart::applyWatcher(m_draft.watcherAutostart,
                                    autostart::findWatcherExecutable(QCoreApplication::applicationDirPath()));
        m_applying = false;
        const AppSettings now = c->settings()->settings();
        if (you) {
            m_base.userSpeakerName = m_draft.userSpeakerName = now.userSpeakerName;
            m_base.uiLanguage = m_draft.uiLanguage = now.uiLanguage;
        }
        if (folders) {
            m_base.audioDir = m_draft.audioDir = now.audioDir;
            m_base.notesDir = m_draft.notesDir = now.notesDir;
        }
        if (watcher) m_base.watcherAutostart = m_draft.watcherAutostart = now.watcherAutostart;
        if (settingsChanged || nameChanged) emit saved();
    } else {
        // Demó: nincs mit lemezre írni — a piszkozat lesz az alap.
        m_draft.userSpeakerName = newName.isEmpty() ? m_draft.userSpeakerName : newName;
        if (you) {
            m_base.userSpeakerName = m_draft.userSpeakerName;
            m_base.uiLanguage = m_draft.uiLanguage;
        }
        if (folders) {
            m_base.audioDir = m_draft.audioDir;
            m_base.notesDir = m_draft.notesDir;
        }
        if (watcher) m_base.watcherAutostart = m_draft.watcherAutostart;
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
        m_draft.userSpeakerName = m_base.userSpeakerName;
        m_draft.uiLanguage = m_base.uiLanguage;
        if (m_draftTheme != m_baseTheme) {
            m_draftTheme = m_baseTheme;
            AppContext::instance()->setThemeMode(m_baseTheme);   // az előnézet visszaáll
        }
        emit youChanged();
    } else if (step == QLatin1String("folders")) {
        m_draft.audioDir = m_base.audioDir;
        m_draft.notesDir = m_base.notesDir;
        emit foldersChanged();
    } else if (step == QLatin1String("watcher")) {
        m_draft.watcherAutostart = m_base.watcherAutostart;
        emit watcherChanged();
    }
    emit dirtyChanged();
}

// ---- Te ----------------------------------------------------------------------------------

void OnboardingViewModel::setUserName(const QString& name)
{
    if (m_draft.userSpeakerName == name) return;
    m_draft.userSpeakerName = name;
    emit youChanged();
    emit dirtyChanged();
}

QString OnboardingViewModel::userNameError() const
{
    return m_draft.userSpeakerName.trimmed().isEmpty()
        ? tr("Adj meg egy nevet — ezen a néven szerepelsz az átiratokban.") : QString();
}

void OnboardingViewModel::setUiLanguage(const QString& lang)
{
    if (m_draft.uiLanguage == lang) return;
    m_draft.uiLanguage = lang;
    emit youChanged();
    emit dirtyChanged();
}

QVariantList OnboardingViewModel::uiLanguageOptions() const
{
    // Ugyanaz a három választás, mint a Beállítások › Általános lapon.
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
    if (!m_controller) return m_draft.uiLanguage != m_base.uiLanguage;
    const QString active = activeUiLanguage();
    return !active.isEmpty() && resolvedUiLanguage(m_draft.uiLanguage) != active;
}

void OnboardingViewModel::setThemeMode(const QString& mode)
{
    const QString m = AppContext::normalizedThemeMode(mode);
    if (m == m_draftTheme) return;
    m_draftTheme = m;
    AppContext::instance()->setThemeMode(m);   // élő előnézet
    emit youChanged();
    emit dirtyChanged();
}

// ---- Mappák ------------------------------------------------------------------------------

QString OnboardingViewModel::folderPath(const QString& key) const
{
    if (key == QLatin1String("audio")) return m_draft.audioDir;
    if (key == QLatin1String("notes")) return m_draft.notesDir;
    return QString();
}

QString OnboardingViewModel::defaultFolder(const QString& key) const
{
    if (!m_controller) {
        // Demó: a kitalált felhasználó alapértelmezett mappái.
        return key == QLatin1String("audio") ? QStringLiteral("/home/lilla/Tanara/felvetelek")
                                             : QStringLiteral("/home/lilla/Tanara/jegyzetek");
    }
    return key == QLatin1String("audio") ? paths::defaultAudioDir() : paths::defaultNotesDir();
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
    if (key == QLatin1String("audio")) m_draft.audioDir = p;
    else if (key == QLatin1String("notes")) m_draft.notesDir = p;
    else return;
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

// ---- Szolgáltatások ----------------------------------------------------------------------

QString OnboardingViewModel::providerLabel(const AppSettings& s, bool stt)
{
    const QString id = stt ? s.sttProviderId : s.llmProviderId;
    const ProviderDescriptor d = stt ? SttProviderRegistry::instance().descriptor(id)
                                     : LlmProviderRegistry::instance().descriptor(id);
    if (d.id.isEmpty()) return id;
    const ProviderConfig cfg = stt ? s.sttSelected() : s.llmSelected();
    QString label = d.displayName;
    // A helyi szerver jól ismert portjáról a nevét mutatjuk (a leíró neve általános) —
    // ugyanúgy, mint a Beállítások összecsukott szolgáltató-kártyája.
    if (d.authMode != AuthMode::Login && isLocalEndpoint(cfg.baseUrl)) {
        const QUrl url(cfg.baseUrl.trimmed());
        if (url.port() == 1234) label = QStringLiteral("LM Studio");
        else if (url.port() == 11434) label = QStringLiteral("Ollama");
    }
    if (stt || d.authMode == AuthMode::Login) return label;
    QString model = cfg.model.trimmed();
    model = model.mid(model.lastIndexOf(QLatin1Char('/')) + 1);
    return model.isEmpty() ? label : label + QStringLiteral(" · ") + model;
}

void OnboardingViewModel::refreshReadiness()
{
    QString stt, llm;
    bool sttOk = false, llmOk = false, live = false, chosen = false;
    if (AppController* c = m_controller) {
        const AppSettings s = c->settings()->settings();
        const ReadinessModel::SecretProbe probe = [c](const QString& key) { return c->hasSecret(key); };
        const Line a = readinessLine(s, true, probe);
        const Line b = readinessLine(s, false, probe);
        stt = a.text;
        llm = b.text;
        sttOk = a.ready;
        llmOk = b.ready;
        live = c->cloudLive();
        chosen = live && c->usesCloud(WorkflowStep::Transcribe) && c->usesCloud(WorkflowStep::Summarize);
    } else {
        // Demó: a friss telepítés tipikus állapota — kulcs még nincs, a helyi LLM be van írva.
        stt = tr("Átírás") + QStringLiteral(": ") + tr("nincs kulcs");
        llm = tr("Összefoglaló") + QStringLiteral(": ") + QStringLiteral("LM Studio · gemma-4-12b");
        llmOk = true;
    }
    if (stt == m_sttStatus && llm == m_llmStatus && sttOk == m_sttReady && llmOk == m_llmReady
        && live == m_cloudLive && chosen == m_cloudChosen)
        return;
    m_sttStatus = stt;
    m_llmStatus = llm;
    m_sttReady = sttOk;
    m_llmReady = llmOk;
    m_cloudLive = live;
    m_cloudChosen = chosen;
    emit readinessChanged();
}

void OnboardingViewModel::openServices()
{
    emit openSettingsRequested(QStringLiteral("providers"));
}

// ---- Hívásfigyelő ------------------------------------------------------------------------

void OnboardingViewModel::setWatcherAutostart(bool on)
{
    if (m_draft.watcherAutostart == on) return;
    m_draft.watcherAutostart = on;
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
    // Homokozóban (TANARA_HOME) és tesztben a bejegyzés nem íródik — ezt is megmondjuk.
    if (m_controller && !autostart::managed() && !autostart::watcherEntryPath().isEmpty())
        note += QLatin1Char(' ') + tr("Most nem íródik bejegyzés (elkülönített adatmappával fut a Tanara).");
    return note;
}

// ---- demó --------------------------------------------------------------------------------

void OnboardingViewModel::loadDemo()
{
    AppSettings s;
    s.userSpeakerName = QStringLiteral("Kovács Lilla");
    s.audioDir = QStringLiteral("/home/lilla/Tanara/felvetelek");
    s.notesDir = QStringLiteral("/home/lilla/Obsidian/Megbeszélések");
    s.metadataDir = QStringLiteral("/home/lilla/.tanara");
    s.uiLanguage = QStringLiteral("auto");
    s.watcherAutostart = true;
    m_base = m_draft = s;
    m_baseTheme = m_draftTheme = AppContext::instance()->themeMode();
    m_done = false;

    m_status.clear();
    const QString st = kSteps.contains(m_demoState) ? m_demoState : kSteps.first();
    // A korábbi lépések: egy kihagyott (Mappák), a többi elfogadva — a navigáció mindkét jelét mutatja.
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
