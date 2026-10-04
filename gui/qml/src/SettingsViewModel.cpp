#include "SettingsViewModel.h"

#include "AppContext.h"
#include "SettingsCloudModel.h"
#include "SettingsDeviceModel.h"
#include "SettingsDialogs.h"
#include "SettingsProviderModel.h"

#include "tanara/AppController.h"
#include "tanara/Localization.h"
#include "tanara/Paths.h"
#include "tanara/PromptLibrary.h"
#include "tanara/SettingsManager.h"
#include "tanara/audio/DeviceManager.h"
#include "tanara/cloud/CloudAccount.h"
#include "tanara/detect/Autostart.h"
#include "tanara/detect/DetectorRegistry.h"
#include "tanara/detect/IMeetingDetector.h"
#include "tanara/detect/detail/PwDumpParser.h"
#include "tanara/provider/ProviderRegistry.h"
#include "tanara/store/JsonSerialization.h"
#include "tanara/store/VoiceprintStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QSet>
#include <QThreadPool>

#include <algorithm>

using namespace tanara;

namespace tanara_qml {

namespace {

const QStringList kPages{QStringLiteral("general"), QStringLiteral("recording"),
                         QStringLiteral("watcher"), QStringLiteral("services"),
                         QStringLiteral("summary")};
const QStringList kPromptIds{QStringLiteral("simple"), QStringLiteral("topic"),
                             QStringLiteral("analysis")};

QLocale uiLocale()
{
    return activeUiLanguage() == QLatin1String("en") ? QLocale(QLocale::English)
                                                     : QLocale(QLocale::Hungarian);
}

QString resolvedUiLanguage(const QString& setting)
{
    if (setting == QLatin1String("hu") || setting == QLatin1String("en")) return setting;
    return QLocale::system().language() == QLocale::Hungarian ? QStringLiteral("hu")
                                                              : QStringLiteral("en");
}

// A figyelt alkalmazás szép neve a folyamatnév-részletből („teams” → „Microsoft Teams”).
QString appLabel(const QString& match)
{
    const QString lower = match.trimmed().toLower();
    if (lower == QLatin1String("meet")) return QStringLiteral("Google Meet");
    const QString pretty = detail::prettyAppName(lower, QString());
    if (pretty != lower) return pretty;
    QString out = match.trimmed();
    if (!out.isEmpty()) out[0] = out.at(0).toUpper();
    return out;
}

// A felhő-szinkronban lévő mappák jellemző nevei (a belső adatokat nem szabad szinkronizálni:
// két gép egyszerre írná az indexet és a hanglenyomatokat).
bool looksSynced(const QString& path)
{
    static const QStringList marks{
        QStringLiteral("dropbox"), QStringLiteral("google drive"), QStringLiteral("google-drive"),
        QStringLiteral("googledrive"), QStringLiteral("onedrive"), QStringLiteral("nextcloud"),
        QStringLiteral("icloud"), QStringLiteral("pcloud"), QStringLiteral("/mega/"),
        QStringLiteral("syncthing")};
    const QString p = path.toLower();
    for (const QString& m : marks)
        if (p.contains(m)) return true;
    return false;
}

struct FolderUsage { qint64 bytes = 0; int meetings = 0; bool exists = false; };

// Mappa mérete (rekurzívan) + a megbeszélés-mappák száma (közvetlen almappa meeting.json-nal).
FolderUsage measureFolder(const QString& path, bool countMeetings)
{
    FolderUsage u;
    const QDir dir(path);
    if (path.isEmpty() || !dir.exists()) return u;
    u.exists = true;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        u.bytes += it.fileInfo().size();
    }
    if (countMeetings) {
        const QStringList subs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& s : subs)
            if (QFile::exists(dir.filePath(s + QStringLiteral("/meeting.json")))) ++u.meetings;
    }
    return u;
}

void collectLeaves(const QJsonValue& v, const QString& path, QHash<QString, QJsonValue>* out)
{
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        if (o.isEmpty()) return;
        for (auto it = o.constBegin(); it != o.constEnd(); ++it)
            collectLeaves(it.value(), path + QLatin1Char('/') + it.key(), out);
        return;
    }
    out->insert(path, v);
}

} // namespace

// ---- JSON-különbség (statikus, tesztelhető) --------------------------------------------

QJsonObject SettingsViewModel::mergedSettings(const QJsonObject& base, const QJsonObject& draft,
                                              const QJsonObject& onto)
{
    QJsonObject out = onto;
    QSet<QString> keys;
    for (auto it = base.constBegin(); it != base.constEnd(); ++it) keys.insert(it.key());
    for (auto it = draft.constBegin(); it != draft.constEnd(); ++it) keys.insert(it.key());
    for (const QString& k : std::as_const(keys)) {
        const QJsonValue b = base.value(k), d = draft.value(k);
        if (b == d) continue;                       // a felhasználó nem nyúlt hozzá
        if (!draft.contains(k)) { out.remove(k); continue; }
        if (b.isObject() && d.isObject())
            out[k] = mergedSettings(b.toObject(), d.toObject(), onto.value(k).toObject());
        else
            out[k] = d;
    }
    return out;
}

int SettingsViewModel::countChanges(const QJsonObject& base, const QJsonObject& draft)
{
    QHash<QString, QJsonValue> b, d;
    collectLeaves(base, QString(), &b);
    collectLeaves(draft, QString(), &d);
    QSet<QString> paths;
    for (auto it = b.constBegin(); it != b.constEnd(); ++it) paths.insert(it.key());
    for (auto it = d.constBegin(); it != d.constEnd(); ++it) paths.insert(it.key());
    int n = 0;
    for (const QString& p : std::as_const(paths)) {
        if (b.value(p) == d.value(p)) continue;
        // Egy most először kiválasztott szolgáltató alapértelmezésekkel feltöltött configja
        // nem külön változás (a szolgáltató-váltás már számít).
        const QStringList parts = p.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.size() >= 3
            && (parts.at(0) == QLatin1String("sttProviders") || parts.at(0) == QLatin1String("llmProviders"))
            && !base.value(parts.at(0)).toObject().contains(parts.at(1)))
            continue;
        ++n;
    }
    return n;
}

QString SettingsViewModel::formatBytes(qint64 bytes)
{
    const QLocale loc = uiLocale();
    const double gb = bytes / 1e9, mb = bytes / 1e6;
    if (gb >= 1.0) return loc.toString(gb, 'f', 1) + QStringLiteral(" GB");
    if (mb >= 1.0) return loc.toString(qRound(mb)) + QStringLiteral(" MB");
    if (bytes >= 1000) return loc.toString(qRound(bytes / 1e3)) + QStringLiteral(" kB");
    return loc.toString(bytes) + QStringLiteral(" B");
}

// ---- életciklus ------------------------------------------------------------------------

SettingsViewModel::SettingsViewModel(QObject* parent) : QObject(parent)
{
    registerBuiltinProviders();   // idempotens; controller nélkül is kell a kártyákhoz
    m_devices = new SettingsDeviceModel(this);
    m_stt = new SettingsProviderModel(this, ProviderKind::Stt);
    m_llm = new SettingsProviderModel(this, ProviderKind::Llm);
    m_cloud = new SettingsCloudModel(this);

    m_detectTimer.setInterval(4000);
    connect(&m_detectTimer, &QTimer::timeout, this, &SettingsViewModel::pollDetector);

    m_baseTheme = m_draftTheme = AppContext::instance()->themeMode();

    // Az App.controller a következő eseményhurok-körben kötődik be, ha addig a hívó nem
    // adott sajátot és nem kért demó-állapotot.
    QMetaObject::invokeMethod(this, [this] {
        if (m_controllerSet) return;
        if (AppController* c = AppContext::instance()->controller())
            setController(c);
        else
            setDemoState(m_demoState.isEmpty() ? QStringLiteral("B01") : m_demoState);
    }, Qt::QueuedConnection);
}

SettingsViewModel::~SettingsViewModel()
{
    if (m_controller && m_monitoring) m_controller->releaseLevelMonitoring(this);
}

QObject* SettingsViewModel::controllerObject() const { return m_controller; }
AppController* SettingsViewModel::controller() const { return m_controller; }
SettingsDialogs* SettingsViewModel::dialogs() const { return m_dialogs; }

void SettingsViewModel::setControllerObject(QObject* controller)
{
    setController(qobject_cast<AppController*>(controller));
}

void SettingsViewModel::setController(AppController* controller)
{
    if (m_controllerSet && m_controller == controller) return;
    if (m_controller && m_monitoring) m_controller->releaseLevelMonitoring(this);
    m_controller = controller;
    m_controllerSet = true;
    attach();
    emit controllerChanged();
}

QObject* SettingsViewModel::dialogsObject() const { return m_dialogs; }

void SettingsViewModel::setDialogsObject(QObject* dialogs)
{
    auto* d = qobject_cast<SettingsDialogs*>(dialogs);
    if (m_dialogs == d) return;
    m_dialogs = d;
    emit dialogsChanged();
}

void SettingsViewModel::setDemoState(const QString& state)
{
    if (m_controller) return;            // valódi adat mellett a demó nem hat
    m_controllerSet = true;
    m_demoState = state;
    attach();
    emit demoStateChanged();
    emit controllerChanged();            // a cloud-elérhetőség a demó-állapottól is függ
}

void SettingsViewModel::attach()
{
    for (const QMetaObject::Connection& c : std::as_const(m_connections)) disconnect(c);
    m_connections.clear();
    m_cloud->attach();

    AppController* c = m_controller;
    if (!c) {
        loadDemo();
        return;
    }
    m_connections << connect(c->settings(), &SettingsManager::settingsChanged, this,
                             &SettingsViewModel::onExternalSettingsChanged);
    m_connections << connect(c, &AppController::devicesChanged, this, [this] {
        m_devices->rebuild();
        applyDefaultSelection();
    });
    m_connections << connect(c, &AppController::deviceLevelPeak, this,
                             [this](const QString& name, float rms, float peak) {
        if (m_monitoring) m_devices->onLevel(name, rms, qMax(rms, peak));
    });
    m_connections << connect(c, &AppController::peopleChanged, this, &SettingsViewModel::refreshVoiceprint);
    m_connections << connect(c, &AppController::voiceprintsChanged, this, &SettingsViewModel::refreshVoiceprint);
    loadFromCore();
    if (m_monitoring) c->retainLevelMonitoring(this);
}

void SettingsViewModel::applyDefaultSelection()
{
    // Még soha nem volt mentett kijelölés (és a felhasználó sem nyúlt hozzá): a rendszer
    // alapértelmezett eszközei az alap — a vonalbemenet-félék nélkül, ahogy a felvevőben.
    if (!m_controller || !m_controller->devices() || !m_controller->lastUsedDeviceNames().isEmpty()
        || !m_baseSelected.isEmpty() || !m_draftSelected.isEmpty())
        return;
    QStringList def;
    for (const AudioDeviceInfo& d : m_controller->devices()->captureDevices())
        if (d.isDefault && d.kind != TrackKind::Other) def << d.name;
    if (def.isEmpty()) return;
    m_baseSelected = m_draftSelected = def;
    m_devices->refreshFromDraft();
}

void SettingsViewModel::normalize(AppSettings& s) const
{
    // A kiválasztott szolgáltatók üres mezői a leíró alapértelmezéseit kapják (ahogy a régi
    // ablak is feltöltötte őket) — így ez nem számít „változásnak”.
    const ProviderDescriptor sd = SttProviderRegistry::instance().descriptor(s.sttProviderId);
    if (!sd.id.isEmpty()) m_stt->fillDefaults(s.sttConfigs[s.sttProviderId], sd);
    const ProviderDescriptor ld = LlmProviderRegistry::instance().descriptor(s.llmProviderId);
    if (!ld.id.isEmpty()) m_llm->fillDefaults(s.llmConfigs[s.llmProviderId], ld);
}

void SettingsViewModel::loadFromCore()
{
    AppController* c = m_controller;
    if (!c) return;
    m_loading = true;
    m_base = c->settings()->settings();
    normalize(m_base);
    m_draft = m_base;

    m_baseSecrets.clear();
    for (SettingsProviderModel* card : {m_stt, m_llm})
        for (const ProviderDescriptor& d : card->allDescriptors())
            for (const ConfigField& f : d.fields)
                if (f.isSecret && !f.secretKey.isEmpty())
                    m_baseSecrets.insert(f.secretKey, c->secret(f.secretKey));
    m_draftSecrets = m_baseSecrets;

    m_baseSelected = m_draftSelected = c->lastUsedDeviceNames();
    m_baseTheme = m_draftTheme = AppContext::instance()->themeMode();

    m_promptText.clear();
    m_promptDefault.clear();
    const QStringList stored{m_base.summaryPrompt, m_base.topicExtractionPrompt, m_base.topicAnalysisPrompt};
    for (int i = 0; i < kPromptIds.size(); ++i) {
        const QString def = promptDefault(kPromptIds.at(i), m_base.metadataDir);
        m_promptDefault.insert(kPromptIds.at(i), def);
        m_promptText.insert(kPromptIds.at(i), stored.at(i).isEmpty() ? def : stored.at(i));
    }

    const bool cloudLive = c->cloudLive();
    const bool sttCloud = m_base.sttProviderId == cloud::ProviderId;
    const bool llmCloud = m_base.llmProviderId == cloud::ProviderId;
    m_ownStt = sttCloud ? QString() : m_base.sttProviderId;
    m_ownLlm = llmCloud ? QString() : m_base.llmProviderId;
    const bool teaserView = !cloudLive && c->cloudTeaser() && m_serviceMode == QLatin1String("cloud");
    m_serviceMode = (cloudLive && sttCloud && llmCloud) || teaserView ? QStringLiteral("cloud")
                                                                      : QStringLiteral("own");

    m_folderUsage.clear();
    m_loading = false;

    m_devices->rebuild();
    applyDefaultSelection();
    m_stt->reset();
    m_llm->reset();
    m_cloud->notifyDraftChanged();
    refreshVoiceprint();
    refreshFolderUsage();
    touch();
    emitAllChanged();
}

void SettingsViewModel::reload()
{
    if (m_controller) loadFromCore();
    else loadDemo();
}

void SettingsViewModel::emitAllChanged()
{
    emit generalChanged();
    emit themeModeChanged();
    emit foldersChanged();
    emit recordingChanged();
    emit watcherChanged();
    emit watchedAppsChanged();
    emit serviceModeChanged();
    emit summaryChanged();
    emit promptsChanged();
    emit promptIndexChanged();
    emit promptTextChanged();
    emit focusChanged();
}

void SettingsViewModel::onExternalSettingsChanged()
{
    if (m_saving || m_loading || !m_controller) return;
    // Valaki más mentett (Expert-modell választó, becslés-ablak, várólista…): az új állapot
    // lesz az alap, a még nem mentett saját változások pedig rákerülnek.
    storePromptsInDraft();
    AppSettings live = m_controller->settings()->settings();
    normalize(live);
    const QJsonObject merged = mergedSettings(toJson(m_base), toJson(m_draft), toJson(live));
    const QString typedName = m_draft.userSpeakerName;
    m_base = live;
    m_draft = appSettingsFromJson(merged);
    if (typedName.trimmed() == m_draft.userSpeakerName) m_draft.userSpeakerName = typedName;
    {
        // A promptok szerkesztő-szövege az összefésült piszkozatból (külső prompt-változás is megjelenik).
        const QStringList stored{m_draft.summaryPrompt, m_draft.topicExtractionPrompt, m_draft.topicAnalysisPrompt};
        for (int i = 0; i < kPromptIds.size(); ++i)
            if (!stored.at(i).isEmpty() || m_promptText.value(kPromptIds.at(i)).trimmed()
                                               != m_promptDefault.value(kPromptIds.at(i)).trimmed())
                m_promptText.insert(kPromptIds.at(i), stored.at(i).isEmpty()
                                                          ? m_promptDefault.value(kPromptIds.at(i)) : stored.at(i));
    }
    m_devices->refreshFromDraft();
    m_stt->reset();
    m_llm->reset();
    m_cloud->notifyDraftChanged();
    touch();
    emitAllChanged();
}

// ---- piszkozat: számlálás, ellenőrzés ---------------------------------------------------

void SettingsViewModel::setDraftSecret(const QString& key, const QString& value)
{
    m_draftSecrets.insert(key, value);
}

void SettingsViewModel::storePromptsInDraft()
{
    // Az alapértelmezettel egyező szöveg ÜRESEN mentődik: a kód-default későbbi javításai így
    // érvényesülnek, amíg a felhasználó nem ír sajátot.
    auto stored = [this](const QString& id) {
        const QString text = m_promptText.value(id);
        return text.trimmed() == m_promptDefault.value(id).trimmed() ? QString() : text;
    };
    m_draft.summaryPrompt = stored(QStringLiteral("simple"));
    m_draft.topicExtractionPrompt = stored(QStringLiteral("topic"));
    m_draft.topicAnalysisPrompt = stored(QStringLiteral("analysis"));
}

void SettingsViewModel::touch()
{
    if (m_loading) return;
    storePromptsInDraft();
    AppSettings d = m_draft;
    d.userSpeakerName = d.userSpeakerName.trimmed();
    int n = countChanges(toJson(m_base), toJson(d));
    for (auto it = m_draftSecrets.constBegin(); it != m_draftSecrets.constEnd(); ++it)
        if (m_baseSecrets.value(it.key()) != it.value()) ++n;
    QStringList a = m_baseSelected, b = m_draftSelected;
    a.sort();
    b.sort();
    if (a != b) ++n;
    if (m_baseTheme != m_draftTheme) ++n;
    m_changeCount = n;
    validate();
    updateServicesWarn();
    emit dirtyChanged();
}

void SettingsViewModel::validate()
{
    QVariantMap e;
    if (m_draft.userSpeakerName.trimmed().isEmpty())
        e.insert(QStringLiteral("userName"), tr("Adj meg egy nevet — ezen a néven szerepelsz az átiratokban."));
    for (const QString& key : {QStringLiteral("audio"), QStringLiteral("notes"), QStringLiteral("meta")}) {
        const QString p = folderPath(key).trimmed();
        if (p.isEmpty())
            e.insert(QStringLiteral("folder.") + key, tr("Válassz egy mappát."));
        else if (!QDir::isAbsolutePath(paths::expandHome(p)))
            e.insert(QStringLiteral("folder.") + key, tr("Teljes elérési utat adj meg."));
    }
    if (m_draft.knownCallApps.isEmpty())
        e.insert(QStringLiteral("watchedApps"),
                 tr("Legalább egy alkalmazás kell: üres listával a figyelő semmire nem jelez."));
    if (e != m_errors) {
        m_errors = e;
        emit errorsChanged();
        emit foldersChanged();
    }
}

ReadinessResult SettingsViewModel::draftReadiness(WorkflowStep step) const
{
    // Kitalált megbeszélés: van hangsáv és van átirat — így csak a szolgáltató-beállítás számít.
    Meeting m;
    Track t;
    t.active = true;
    m.tracks << t;
    m.hasTranscript = true;
    const ReadinessModel model(m_draft, [this](const QString& key) {
        if (m_draftSecrets.contains(key)) return !m_draftSecrets.value(key).trimmed().isEmpty();
        if (!m_controller) return m_cloud->loggedIn();      // demó: a bejelentkezés-kulcs
        return m_controller->hasSecret(key);
    });
    return model.check(step, m);
}

void SettingsViewModel::updateServicesWarn()
{
    bool warn;
    if (!m_controller && m_serviceMode == QLatin1String("cloud") && cloudAvailability() == QLatin1String("live"))
        warn = !m_cloud->loggedIn();
    else
        warn = !draftReadiness(WorkflowStep::Transcribe).runnable
               || !draftReadiness(WorkflowStep::Summarize).runnable
               || m_stt->lastTestFailed() || m_llm->lastTestFailed();
    if (warn == m_servicesWarn) return;
    m_servicesWarn = warn;
    emit servicesWarnChanged();
}

QString SettingsViewModel::footerText() const
{
    if (m_changeCount <= 0) return tr("Nincs mentetlen változás");
    const bool toCloud = m_draft.sttProviderId == cloud::ProviderId && m_draft.llmProviderId == cloud::ProviderId
                         && (m_base.sttProviderId != cloud::ProviderId || m_base.llmProviderId != cloud::ProviderId);
    if (toCloud && m_changeCount <= 2) return tr("Tanara Cloud kiválasztva · mentéskor átvált");
    return tr("%n nem mentett változás", nullptr, m_changeCount);
}

// ---- ablak -----------------------------------------------------------------------------

void SettingsViewModel::setPage(const QString& page)
{
    if (!kPages.contains(page) || page == m_page) return;
    m_page = page;
    emit pageChanged();
}

QString SettingsViewModel::focusBannerText() const
{
    if (m_focusField == QLatin1String("stt"))
        return tr("Az átíráshoz kell egy szolgáltató. Válassz egyet, add meg a kulcsot, és "
                  "visszaviszünk a megbeszéléshez.");
    if (m_focusField == QLatin1String("llm"))
        return tr("Az összefoglalóhoz kell egy nyelvi modell. Állítsd be a végpontot, és "
                  "visszaviszünk a megbeszéléshez.");
    return QString();
}

void SettingsViewModel::openPage(const QString& page, const QString& focusField)
{
    QString target = page;
    if (page == QLatin1String("providers")) target = QStringLiteral("services");
    else if (page == QLatin1String("cloud")) target = QStringLiteral("services");
    else if (page.isEmpty()) target = m_page;
    if (kPages.contains(target)) setPage(target);

    if (page == QLatin1String("cloud") && cloudAvailability() != QLatin1String("none"))
        setServiceMode(QStringLiteral("cloud"));
    else if (page == QLatin1String("providers") && !focusField.isEmpty())
        setServiceMode(QStringLiteral("own"));

    const QString f = focusField == QLatin1String("stt") || focusField == QLatin1String("llm")
                          ? focusField : QString();
    if (f != m_focusField) {
        m_focusField = f;
        emit focusChanged();
        m_stt->notifyFocusChanged();
        m_llm->notifyFocusChanged();
    }
}

// ---- B01 Általános ---------------------------------------------------------------------

void SettingsViewModel::setUserName(const QString& name)
{
    if (m_draft.userSpeakerName == name) return;
    m_draft.userSpeakerName = name;
    emit generalChanged();
    touch();
}

void SettingsViewModel::refreshVoiceprint()
{
    QString text;
    bool has = false;
    if (m_controller && m_controller->voiceprints()) {
        const QVector<Voiceprint> prints = m_controller->voiceprints()->printsFor(m_base.userSpeakerName);
        QSet<QString> meetings;
        for (const Voiceprint& p : prints)
            if (!p.sourceMeetingId.isEmpty()) meetings.insert(p.sourceMeetingId);
        has = !prints.isEmpty();
        if (has)
            text = tr("Hanglenyomat: %n minta,", nullptr, prints.size()) + QLatin1Char(' ')
                   + tr("%n megbeszélésből", nullptr, meetings.size());
        else
            text = tr("Ehhez a névhez még nincs hanglenyomat.");
    }
    if (text == m_voiceprintText && has == m_hasVoiceprint) return;
    m_voiceprintText = text;
    m_hasVoiceprint = has;
    emit voiceprintChanged();
}

void SettingsViewModel::setUiLanguage(const QString& lang)
{
    if (m_draft.uiLanguage == lang) return;
    m_draft.uiLanguage = lang;
    emit generalChanged();
    touch();
}

QVariantList SettingsViewModel::uiLanguageOptions() const
{
    auto mk = [](const QString& v, const QString& l) {
        return QVariantMap{{QStringLiteral("value"), v}, {QStringLiteral("label"), l}};
    };
    const bool sysHu = QLocale::system().language() == QLocale::Hungarian;
    // A nyelvek neve szándékosan a saját nyelvén áll: idegen nyelvű felületen is megtalálható.
    return {mk(QStringLiteral("auto"), sysHu ? tr("Rendszer nyelve (magyar)") : tr("Rendszer nyelve (English)")),
            mk(QStringLiteral("hu"), QStringLiteral("Magyar")),
            mk(QStringLiteral("en"), QStringLiteral("English"))};
}

bool SettingsViewModel::languageNeedsRestart() const
{
    if (!m_controller) return false;
    const QString active = activeUiLanguage();
    return !active.isEmpty() && resolvedUiLanguage(m_draft.uiLanguage) != active;
}

void SettingsViewModel::setThemeMode(const QString& mode)
{
    const QString m = AppContext::normalizedThemeMode(mode);
    if (m == m_draftTheme) return;
    m_draftTheme = m;
    AppContext::instance()->setThemeMode(m);   // élő előnézet
    emit themeModeChanged();
    touch();
}

QString SettingsViewModel::folderPath(const QString& key) const
{
    if (key == QLatin1String("audio")) return m_draft.audioDir;
    if (key == QLatin1String("notes")) return m_draft.notesDir;
    if (key == QLatin1String("meta")) return m_draft.metadataDir;
    return QString();
}

QVariantList SettingsViewModel::folders() const
{
    const bool metaLocked = m_controller && !paths::homeOverride().isEmpty();
    auto mk = [this](const QString& key, const QString& label, const QString& hint, bool locked,
                     const QString& warning) {
        return QVariantMap{
            {QStringLiteral("key"), key}, {QStringLiteral("label"), label},
            {QStringLiteral("path"), folderPath(key)},
            {QStringLiteral("usage"), m_folderUsage.value(key)},
            {QStringLiteral("hint"), hint}, {QStringLiteral("locked"), locked},
            {QStringLiteral("warning"), warning},
            {QStringLiteral("error"), m_errors.value(QStringLiteral("folder.") + key).toString()}};
    };
    QString metaWarn;
    if (looksSynced(m_draft.metadataDir))
        metaWarn = tr("Ez a mappa felhő-szinkronban lévőnek tűnik. A belső adatokat ne szinkronizáld: "
                      "két gép egyszerre írná, és megsérülhet.");
    QString metaHint = metaLocked
        ? tr("Hanglenyomatok, beállítások és index. Ezt a mappát most a TANARA_HOME környezeti változó rögzíti.")
        : tr("Hanglenyomatok, beállítások és index. Ne szinkronizáld felhőtárhellyel. "
             "A váltás újraindítás után érvényes; a meglévő adatokat nem költözteti át.");
    return {
        mk(QStringLiteral("audio"), tr("Felvételek"),
           tr("Hangsávok, megbeszélésenként egy mappa. Az új felvételek ide kerülnek."), false, QString()),
        mk(QStringLiteral("notes"), tr("Jegyzetek"),
           tr("Az összefoglalók másolata sima Markdown-fájlként (pl. a jegyzettáradba)."), false, QString()),
        mk(QStringLiteral("meta"), tr("Belső adatok"), metaHint, metaLocked, metaWarn),
    };
}

void SettingsViewModel::refreshFolderUsage()
{
    if (!m_controller) return;
    const int generation = ++m_usageGeneration;
    QPointer<SettingsViewModel> self(this);
    for (const QString& key : {QStringLiteral("audio"), QStringLiteral("notes"), QStringLiteral("meta")}) {
        const QString path = paths::expandHome(folderPath(key).trimmed());
        const bool audio = key == QLatin1String("audio");
        QThreadPool::globalInstance()->start([self, generation, key, path, audio] {
            const FolderUsage u = measureFolder(path, audio);
            QMetaObject::invokeMethod(qApp, [self, generation, key, u, audio] {
                if (!self || self->m_usageGeneration != generation) return;
                QString text;
                if (!u.exists)
                    text = SettingsViewModel::tr("még nincs ilyen mappa — mentéskor létrejön");
                else {
                    text = formatBytes(u.bytes);
                    if (audio)
                        text += QStringLiteral(" · ")
                                + SettingsViewModel::tr("%n megbeszélés", nullptr, u.meetings);
                }
                self->m_folderUsage.insert(key, text);
                emit self->foldersChanged();
            }, Qt::QueuedConnection);
        });
    }
}

void SettingsViewModel::setFolder(const QString& key, const QString& path)
{
    const QString p = path.trimmed();
    if (p == folderPath(key)) return;
    if (key == QLatin1String("audio")) m_draft.audioDir = p;
    else if (key == QLatin1String("notes")) m_draft.notesDir = p;
    else if (key == QLatin1String("meta")) {
        if (m_controller && !paths::homeOverride().isEmpty()) return;   // rögzített
        m_draft.metadataDir = p;
    } else return;
    m_folderUsage.remove(key);
    emit foldersChanged();
    touch();
    refreshFolderUsage();
}

void SettingsViewModel::browseFolder(const QString& key)
{
    if (!m_dialogs) return;
    const QString title = key == QLatin1String("audio") ? tr("Felvételek mappája")
                        : key == QLatin1String("notes") ? tr("Jegyzetek mappája")
                                                        : tr("Belső adatok mappája");
    QString start = paths::expandHome(folderPath(key).trimmed());
    if (start.isEmpty() || !QDir(start).exists()) start = QDir::homePath();
    const QString dir = m_dialogs->pickFolder(title, start);
    if (!dir.isEmpty()) setFolder(key, dir);
}

void SettingsViewModel::openFolder(const QString& key)
{
    if (!m_dialogs) return;
    const QString path = paths::expandHome(folderPath(key).trimmed());
    if (!path.isEmpty() && QDir(path).exists()) m_dialogs->openFolder(path);
}

void SettingsViewModel::openPeople()
{
    // A hivatkozás a saját hanglenyomat sora mellett áll: a saját személy lesz kijelölve.
    if (m_dialogs) m_dialogs->openPeopleAt(m_base.userSpeakerName.trimmed());
}

// ---- B02 Rögzítés ----------------------------------------------------------------------

QAbstractItemModel* SettingsViewModel::devicesModel() const { return m_devices; }
int SettingsViewModel::deviceCount() const { return m_devices->rowCount(); }

void SettingsViewModel::setMonitoring(bool on)
{
    if (m_monitoring == on) return;
    m_monitoring = on;
    if (m_controller) {
        if (on) m_controller->retainLevelMonitoring(this);
        else m_controller->releaseLevelMonitoring(this);
    }
    m_devices->setTicking(on);
    emit monitoringChanged();
}

void SettingsViewModel::toggleDevice(int row) { m_devices->toggle(row); }
void SettingsViewModel::renameDevice(int row, const QString& name) { m_devices->rename(row, name); }

void SettingsViewModel::setAutoRecordAll(bool on)
{
    if (m_draft.autoRecordAllDevices == on) return;
    m_draft.autoRecordAllDevices = on;
    emit recordingChanged();
    touch();
}

void SettingsViewModel::setAudioQuality(const QString& quality)
{
    if (m_draft.audioQuality == quality) return;
    m_draft.audioQuality = quality;
    emit recordingChanged();
    touch();
}

QVariantList SettingsViewModel::audioQualityOptions() const
{
    auto mk = [](const QString& v, const QString& l) {
        return QVariantMap{{QStringLiteral("value"), v}, {QStringLiteral("label"), l}};
    };
    return {mk(QStringLiteral("low"), tr("Takarékos")), mk(QStringLiteral("medium"), tr("Beszéd")),
            mk(QStringLiteral("high"), tr("Magas")), mk(QStringLiteral("best"), tr("Legjobb"))};
}

QString SettingsViewModel::audioQualityHint() const
{
    const int kbps = opusBitrateKbps(m_draft.audioQuality);
    const int mb = qRound(kbps * 1000.0 / 8.0 * 5400.0 / 1e6);   // 1,5 óra egy sávon
    const QString q = m_draft.audioQuality;
    const QString tail = q == QLatin1String("low")    ? tr("A legkisebb fájl; az átíráshoz még elég.")
                       : q == QLatin1String("medium") ? tr("Beszédhez elég.")
                       : q == QLatin1String("high")   ? tr("Zajos teremhez, több beszélőhöz.")
                                                      : tr("A legjobb minőség, a legnagyobb fájl.");
    return tr("%1 kbps Opus · kb. %2 MB sávonként 1,5 óra alatt.").arg(kbps).arg(mb) + QLatin1Char(' ') + tail;
}

void SettingsViewModel::setMixdownMode(const QString& mode)
{
    if (m_draft.mixdownMode == mode) return;
    m_draft.mixdownMode = mode;
    emit recordingChanged();
    touch();
}

QStringList SettingsViewModel::selectedDevicesForSave() const
{
    // A most nem csatlakoztatott eszközök kijelölése megmarad; a jelen lévőké a piszkozatból jön.
    const QStringList present = m_devices->presentNames();
    QStringList out;
    for (const QString& n : m_baseSelected)
        if (!present.contains(n)) out << n;
    for (const QString& n : present)
        if (m_draftSelected.contains(n)) out << n;
    return out;
}

// ---- B03 Hívásfigyelő ------------------------------------------------------------------

void SettingsViewModel::setDetectorEnabled(bool on)
{
    if (m_draft.detectorEnabled == on) return;
    m_draft.detectorEnabled = on;
    emit watcherChanged();
    touch();
}

void SettingsViewModel::setWatcherAutostart(bool on)
{
    if (m_draft.watcherAutostart == on) return;
    m_draft.watcherAutostart = on;
    emit watcherChanged();
    touch();
}

void SettingsViewModel::setAskStopOnCallEnd(bool on)
{
    if (m_draft.askStopOnCallEnd == on) return;
    m_draft.askStopOnCallEnd = on;
    emit watcherChanged();
    touch();
}

void SettingsViewModel::setSilenceAskMinutes(int minutes)
{
    const int m = std::clamp(minutes, 0, 60);
    if (m_draft.silenceAskMinutes == m) return;
    m_draft.silenceAskMinutes = m;
    emit watcherChanged();
    touch();
}

void SettingsViewModel::setDetectorIntervalSec(int sec)
{
    const int s = std::clamp(sec, 3, 60);
    if (m_draft.detectorIntervalSec == s) return;
    m_draft.detectorIntervalSec = s;
    emit watcherChanged();
    touch();
}

QVariantList SettingsViewModel::watchedApps() const
{
    QVariantList out;
    for (const QString& match : m_draft.knownCallApps) {
        const QString lower = match.toLower();
        const bool active = m_liveActive && !m_liveAppId.isEmpty()
                            && (m_liveAppId.contains(lower) || lower.contains(m_liveAppId));
        out << QVariantMap{{QStringLiteral("match"), match}, {QStringLiteral("label"), appLabel(match)},
                           {QStringLiteral("active"), active}};
    }
    return out;
}

QString SettingsViewModel::addWatchedApp(const QString& match)
{
    const QString m = match.trimmed().toLower();
    if (m.isEmpty()) return tr("Írd be a folyamat nevét vagy annak egy részét.");
    if (m.size() < 2) return tr("Legalább két karakter kell.");
    for (const QString& have : m_draft.knownCallApps)
        if (have.compare(m, Qt::CaseInsensitive) == 0) return tr("Ez már szerepel a listán.");
    m_draft.knownCallApps << m;
    emit watchedAppsChanged();
    touch();
    return QString();
}

void SettingsViewModel::removeWatchedApp(int index)
{
    if (index < 0 || index >= m_draft.knownCallApps.size()) return;
    m_draft.knownCallApps.removeAt(index);
    emit watchedAppsChanged();
    touch();
}

QVariantList SettingsViewModel::runningApps() const
{
    QStringList names;
    if (!m_controller) {
        names = {QStringLiteral("signal-desktop"), QStringLiteral("telegram-desktop"),
                 QStringLiteral("thunderbird"), QStringLiteral("zoom")};
    } else {
#if defined(Q_OS_LINUX)
        // A futó folyamatok neve a /proc-ból (a kernel-szálaknak nincs parancssoruk → kimaradnak).
        const QDir proc(QStringLiteral("/proc"));
        const QStringList pids = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& pid : pids) {
            bool isNumber = false;
            pid.toLongLong(&isNumber);
            if (!isNumber) continue;
            QFile cmd(proc.filePath(pid + QStringLiteral("/cmdline")));
            if (!cmd.open(QIODevice::ReadOnly)) continue;
            const QByteArray first = cmd.readAll().split('\0').value(0);
            if (first.isEmpty()) continue;
            const QString name = QFileInfo(QString::fromLocal8Bit(first)).fileName().toLower();
            if (!name.isEmpty() && !names.contains(name)) names << name;
        }
#endif
    }
    names.sort();
    QVariantList out;
    for (const QString& n : std::as_const(names)) {
        bool covered = n.startsWith(QLatin1String("tanara"));
        for (const QString& have : m_draft.knownCallApps)
            if (n.contains(have, Qt::CaseInsensitive)) { covered = true; break; }
        if (covered) continue;
        out << QVariantMap{{QStringLiteral("match"), n}, {QStringLiteral("label"), appLabel(n)}};
    }
    return out;
}

void SettingsViewModel::setWatching(bool on)
{
    if (m_watching == on) return;
    m_watching = on;
    if (on && (m_controller || m_detector)) {
        m_detectTimer.start();
        QMetaObject::invokeMethod(this, &SettingsViewModel::pollDetector, Qt::QueuedConnection);
    } else {
        m_detectTimer.stop();
    }
    emit watchingChanged();
}

void SettingsViewModel::setDetectorForTest(IMeetingDetector* detector)
{
    m_detector.reset(detector);
    m_detectorTried = true;
}

void SettingsViewModel::pollDetector()
{
    if (!m_watching) return;
    if (!m_detector && !m_detectorTried && m_controller) {
        m_detectorTried = true;
        registerBuiltinDetectors();   // idempotens
        m_detector.reset(m_draft.detectorId.isEmpty()
                             ? MeetingDetectorRegistry::instance().createBest()
                             : MeetingDetectorRegistry::instance().create(m_draft.detectorId));
    }
    const bool available = m_detector && m_detector->isAvailable();
    MeetingSignal sig;
    if (available) {
        // A PISZKOZAT listájával: a most hozzáadott alkalmazás azonnal látszik, ha épp hívásban van.
        m_detector->configure(m_draft.knownCallApps, QStringLiteral("tanara"));
        sig = m_detector->poll();
    }
    if (available == m_detectorAvailable && sig.active == m_liveActive && sig.appName == m_liveApp
        && sig.appId == m_liveAppId)
        return;
    m_detectorAvailable = available;
    m_liveActive = sig.active;
    m_liveApp = sig.appName;
    m_liveAppId = sig.appId.toLower();
    emit liveCallChanged();
    emit watchedAppsChanged();
}

// ---- B04–B06 Szolgáltatások ------------------------------------------------------------

QString SettingsViewModel::cloudAvailability() const
{
    if (!m_controller)
        return m_demoState == QLatin1String("teaser") ? QStringLiteral("teaser") : QStringLiteral("live");
    if (m_controller->cloudLive()) return QStringLiteral("live");
    if (m_controller->cloudTeaser()) return QStringLiteral("teaser");
    return QStringLiteral("none");
}

QString SettingsViewModel::lastOwnProvider(bool stt) const
{
    const QString remembered = stt ? m_ownStt : m_ownLlm;
    if (!remembered.isEmpty()) return remembered;
    const SettingsProviderModel* card = stt ? m_stt : m_llm;
    const QMap<QString, ProviderConfig>& configs = stt ? m_draft.sttConfigs : m_draft.llmConfigs;
    const QString fallback = stt ? QStringLiteral("soniox") : QStringLiteral("openai-compat");
    QString first;
    bool hasFallback = false;
    for (const ProviderDescriptor& d : card->allDescriptors()) {
        if (d.authMode == AuthMode::Login) continue;
        if (first.isEmpty()) first = d.id;
        if (d.id == fallback) hasFallback = true;
    }
    // Amelyiknek már van beállítása (és nem az alapértelmezett), az a valószínű korábbi választás.
    for (const ProviderDescriptor& d : card->allDescriptors())
        if (d.authMode != AuthMode::Login && d.id != fallback && configs.contains(d.id)) return d.id;
    return hasFallback ? fallback : first;
}

void SettingsViewModel::setServiceMode(const QString& mode)
{
    const QString m = mode == QLatin1String("cloud") ? QStringLiteral("cloud") : QStringLiteral("own");
    if (m == QLatin1String("cloud") && cloudAvailability() == QLatin1String("none")) return;
    const bool live = cloudAvailability() == QLatin1String("live");
    // Csak VALÓDI módváltáskor nyúlunk a szolgáltatókhoz: a vegyes beállítás (pl. az átírás a
    // Cloudban, az összefoglaló saját kulccsal) egy mély hivatkozástól nem változhat meg.
    if (live && m != m_serviceMode) {
        // Élő módban a választás a piszkozat része: mentéskor mindkét lépés szolgáltatója vált.
        if (m == QLatin1String("cloud")) {
            if (m_draft.sttProviderId != cloud::ProviderId) m_ownStt = m_draft.sttProviderId;
            if (m_draft.llmProviderId != cloud::ProviderId) m_ownLlm = m_draft.llmProviderId;
            m_draft.sttProviderId = cloud::ProviderId;
            m_draft.llmProviderId = cloud::ProviderId;
            m_draft.sttConfigs[cloud::ProviderId].type = cloud::ProviderId;
            m_draft.llmConfigs[cloud::ProviderId].type = cloud::ProviderId;
        } else {
            if (m_draft.sttProviderId == cloud::ProviderId) m_draft.sttProviderId = lastOwnProvider(true);
            if (m_draft.llmProviderId == cloud::ProviderId) m_draft.llmProviderId = lastOwnProvider(false);
            normalize(m_draft);
        }
        m_stt->reset();
        m_llm->reset();
    }
    // Teaser-módban a „Tanara Cloud” kártya csak a várólista-ajánlatot mutatja (nincs mit menteni).
    if (m != m_serviceMode) {
        m_serviceMode = m;
        emit serviceModeChanged();
    }
    touch();
}

// ---- B07 Összefoglaló ------------------------------------------------------------------

void SettingsViewModel::setSummaryLanguage(const QString& language)
{
    const QString l = language.trimmed();
    if (l.isEmpty() || m_draft.summaryLanguage == l) return;
    m_draft.summaryLanguage = l;
    emit summaryChanged();
    touch();
}

QStringList SettingsViewModel::summaryLanguageOptions() const
{
    // Az érték szabad szöveg (a promptba kerül): magyarul írjuk, ahogy a beépített promptok.
    QStringList out{QStringLiteral("magyar"), QStringLiteral("angol"), QStringLiteral("német"),
                    QStringLiteral("francia"), QStringLiteral("spanyol")};
    if (!out.contains(m_draft.summaryLanguage) && !m_draft.summaryLanguage.isEmpty())
        out.prepend(m_draft.summaryLanguage);
    return out;
}

QString SettingsViewModel::promptId(int index) const
{
    return kPromptIds.value(index, kPromptIds.first());
}

QVariantList SettingsViewModel::promptTabs() const
{
    const QStringList labels{tr("Gyors összefoglaló"), tr("Témajavaslat"), tr("Témánkénti elemzés")};
    QVariantList out;
    for (int i = 0; i < kPromptIds.size(); ++i) {
        const QString id = kPromptIds.at(i);
        out << QVariantMap{
            {QStringLiteral("id"), id}, {QStringLiteral("label"), labels.at(i)},
            {QStringLiteral("modified"),
             m_promptText.value(id).trimmed() != m_promptDefault.value(id).trimmed()}};
    }
    return out;
}

void SettingsViewModel::setPromptIndex(int index)
{
    if (index < 0 || index >= kPromptIds.size() || index == m_promptIndex) return;
    m_promptIndex = index;
    emit promptIndexChanged();
    emit promptsChanged();
    emit promptTextChanged();
}

QString SettingsViewModel::promptText() const
{
    return m_promptText.value(promptId(m_promptIndex));
}

void SettingsViewModel::setPromptText(const QString& text)
{
    const QString id = promptId(m_promptIndex);
    if (m_promptText.value(id) == text) return;
    const bool wasModified = promptModified();
    m_promptText.insert(id, text);
    // promptTextChanged szándékosan NINCS: a szerkesztő maga írta (a kurzor ne ugorjon).
    if (wasModified != promptModified()) emit promptsChanged();
    touch();
}

bool SettingsViewModel::promptModified() const
{
    const QString id = promptId(m_promptIndex);
    return m_promptText.value(id).trimmed() != m_promptDefault.value(id).trimmed();
}

void SettingsViewModel::resetPrompt()
{
    const QString id = promptId(m_promptIndex);
    if (!promptModified()) return;
    m_promptText.insert(id, m_promptDefault.value(id));
    emit promptsChanged();
    emit promptTextChanged();
    touch();
}

QVariantList SettingsViewModel::promptVariables() const
{
    QVariantList out;
    for (const PromptVariable& v : tanara::promptVariables())
        out << QVariantMap{{QStringLiteral("token"), v.token}, {QStringLiteral("description"), v.description}};
    return out;
}

QString SettingsViewModel::schemaSummary() const { return promptOutputFormat(promptId(m_promptIndex)).summary; }
QString SettingsViewModel::schemaBody() const { return promptOutputFormat(promptId(m_promptIndex)).body; }
QString SettingsViewModel::schemaKind() const { return promptOutputFormat(promptId(m_promptIndex)).kind; }

// ---- mentés / eldobás ------------------------------------------------------------------

bool SettingsViewModel::save()
{
    touch();
    if (!dirty()) return true;

    // Érvénytelen mezővel nem mentünk: a hibás lapra váltunk, ott látszik a mező alatt.
    QString badPage;
    if (m_errors.contains(QStringLiteral("userName")) || m_errors.contains(QStringLiteral("folder.audio"))
        || m_errors.contains(QStringLiteral("folder.notes")) || m_errors.contains(QStringLiteral("folder.meta")))
        badPage = QStringLiteral("general");
    else if (m_errors.contains(QStringLiteral("watchedApps")))
        badPage = QStringLiteral("watcher");
    else if (!m_stt->fieldErrors().isEmpty() || !m_llm->fieldErrors().isEmpty())
        badPage = QStringLiteral("services");
    if (!badPage.isEmpty()) {
        setPage(badPage);
        return false;
    }

    // A szolgáltató-mezők gépelés közben nyersen állnak a piszkozatban: mentéskor vágjuk le.
    for (QMap<QString, ProviderConfig>* map : {&m_draft.sttConfigs, &m_draft.llmConfigs})
        for (auto it = map->begin(); it != map->end(); ++it) {
            it->baseUrl = it->baseUrl.trimmed();
            it->model = it->model.trimmed();
            for (auto e = it->extra.begin(); e != it->extra.end(); ++e)
                if (e->typeId() == QMetaType::QString) *e = e->toString().trimmed();
        }

    const QString newName = m_draft.userSpeakerName.trimmed();
    const bool themeChanged = m_baseTheme != m_draftTheme;
    const QString theme = m_draftTheme;

    if (!m_controller) {
        // Demó: nincs mit lemezre írni — a piszkozat lesz az alap.
        m_draft.userSpeakerName = newName;
        m_base = m_draft;
        m_baseSecrets = m_draftSecrets;
        m_baseSelected = m_draftSelected;
        m_baseTheme = m_draftTheme;
        touch();
        emit saved();
        return true;
    }

    AppController* c = m_controller;
    m_saving = true;

    // 1) beállítások: csak a KÜLÖNBSÉG kerül a core éppen érvényes állapotára.
    const AppSettings live = c->settings()->settings();
    AppSettings draft = m_draft;
    draft.userSpeakerName = m_base.userSpeakerName;   // a névváltást a core külön műveletként végzi
    const QJsonObject liveJson = toJson(live);
    const QJsonObject mergedJson = mergedSettings(toJson(m_base), toJson(draft), liveJson);
    if (mergedJson != liveJson) {
        AppSettings merged = appSettingsFromJson(mergedJson);
        if (!paths::homeOverride().isEmpty()) merged.metadataDir = live.metadataDir;
        c->settings()->setSettings(merged);
    }
    // 2) saját név: a core átvezeti a személy-DB-n, a megbeszéléseken és a hanglenyomatokon.
    if (!newName.isEmpty() && newName != m_base.userSpeakerName.trimmed())
        c->setUserSpeakerName(newName);
    // 3) titkok → KeyStore.
    for (auto it = m_draftSecrets.constBegin(); it != m_draftSecrets.constEnd(); ++it)
        if (m_baseSecrets.value(it.key()) != it.value()) c->setSecret(it.key(), it.value().trimmed());
    // 4) alapértelmezett források (a felvevővel közös kijelölés).
    {
        QStringList a = m_baseSelected, b = m_draftSelected;
        a.sort();
        b.sort();
        if (a != b) c->setLastUsedDeviceNames(selectedDevicesForSave());
    }
    // 5) a figyelő indítása bejelentkezéskor: azonnal érvényes (homokozóban / tesztben soha).
    if (m_base.watcherAutostart != m_draft.watcherAutostart)
        autostart::applyWatcher(m_draft.watcherAutostart,
                                autostart::findWatcherExecutable(QCoreApplication::applicationDirPath()));
    m_saving = false;

    loadFromCore();
    if (themeChanged) {
        AppContext::instance()->setThemeMode(theme);
        m_baseTheme = m_draftTheme = theme;
        emit themeModeSaved(theme);
        touch();
    }
    emit saved();

    // B04: ha a hiány megszűnt, vissza a megbeszéléshez.
    if (!m_focusField.isEmpty()) {
        const WorkflowStep step = m_focusField == QLatin1String("stt") ? WorkflowStep::Transcribe
                                                                        : WorkflowStep::Summarize;
        if (draftReadiness(step).runnable) {
            m_focusField.clear();
            emit focusChanged();
            m_stt->notifyFocusChanged();
            m_llm->notifyFocusChanged();
            emit returnRequested();
        }
    }
    return true;
}

void SettingsViewModel::discard()
{
    m_draft = m_base;
    m_draftSecrets = m_baseSecrets;
    m_draftSelected = m_baseSelected;
    if (m_draftTheme != m_baseTheme) {
        m_draftTheme = m_baseTheme;
        AppContext::instance()->setThemeMode(m_baseTheme);   // az előnézet visszaáll
    }
    const QStringList stored{m_base.summaryPrompt, m_base.topicExtractionPrompt, m_base.topicAnalysisPrompt};
    for (int i = 0; i < kPromptIds.size(); ++i)
        m_promptText.insert(kPromptIds.at(i),
                            stored.at(i).isEmpty() ? m_promptDefault.value(kPromptIds.at(i)) : stored.at(i));
    const bool cloudBoth = m_base.sttProviderId == cloud::ProviderId && m_base.llmProviderId == cloud::ProviderId;
    if (cloudAvailability() == QLatin1String("live"))
        m_serviceMode = cloudBoth ? QStringLiteral("cloud") : QStringLiteral("own");
    m_devices->refreshFromDraft();
    m_stt->reset();
    m_llm->reset();
    m_cloud->notifyDraftChanged();
    touch();
    emitAllChanged();
    refreshFolderUsage();
}

// ---- demó (kitalált adat a design állapotaihoz) -----------------------------------------

void SettingsViewModel::loadDemo()
{
    const QString st = m_demoState;
    m_loading = true;

    AppSettings s;
    s.userSpeakerName = QStringLiteral("Kovács Lilla");
    s.audioDir = QStringLiteral("/home/lilla/Tanara/felvetelek");
    s.notesDir = QStringLiteral("/home/lilla/Tanara/jegyzetek");
    s.metadataDir = QStringLiteral("/home/lilla/.tanara");
    s.uiLanguage = QStringLiteral("auto");
    s.autoRecordAllDevices = false;
    s.audioQuality = QStringLiteral("medium");
    s.mixdownMode = QStringLiteral("manual");
    s.summaryLanguage = QStringLiteral("magyar");
    s.knownCallApps = {QStringLiteral("zoom"), QStringLiteral("teams"), QStringLiteral("webex"),
                       QStringLiteral("slack"), QStringLiteral("discord"), QStringLiteral("meet")};
    s.watcherAutostart = true;
    s.sttProviderId = QStringLiteral("soniox");
    s.llmProviderId = QStringLiteral("openai-compat");
    s.sttConfigs.clear();
    s.llmConfigs.clear();
    {
        ProviderConfig llm;
        llm.type = s.llmProviderId;
        llm.baseUrl = QStringLiteral("http://localhost:1234/v1");
        llm.model = QStringLiteral("google/gemma-4-12b-qat");
        llm.temperature = 0.2;
        llm.maxTokens = 30000;
        s.llmConfigs.insert(s.llmProviderId, llm);
    }
    const QString trust = QStringLiteral("alsa_input.usb-Trust_USB_Microphone-00.mono-fallback");
    const QString headsetMon = QStringLiteral("Monitor of Sennheiser headset - Kommunikáció");
    const QString kantoMon = QStringLiteral("Monitor of Kanto YU4 - Optikai digitális sztereó");
    s.deviceNames = {
        {trust, QStringLiteral("Trust USB mikrofon")},
        {QStringLiteral("alsa_input.pci-0000_00_1f.3.analog-stereo"), QStringLiteral("Sennheiser fejhallgató (analóg)")},
        {headsetMon, QStringLiteral("Sennheiser headset")},
        {QStringLiteral("alsa_input.pci-0000_00_1f.3.analog-stereo.line-in"), QStringLiteral("Alaplapi vonalbemenet")},
    };
    normalize(s);
    m_base = s;
    m_draft = s;

    const bool keyMissing = st == QLatin1String("B04");
    m_baseSecrets = {{QStringLiteral("soniox.apiKey"), keyMissing ? QString() : QStringLiteral("sk-demo-0000000000003f9a")},
                     {QStringLiteral("llm.apiKey"), QString()},
                     {QStringLiteral("stt.whisper.apiKey"), QString()}};
    m_draftSecrets = m_baseSecrets;
    m_baseSelected = m_draftSelected = {trust, headsetMon, kantoMon};
    m_baseTheme = m_draftTheme = AppContext::instance()->themeMode();

    m_promptText.clear();
    m_promptDefault.clear();
    for (const QString& id : kPromptIds) {
        const QString def = promptBuiltin(id);
        m_promptDefault.insert(id, def);
        m_promptText.insert(id, def);
    }
    m_promptIndex = 0;

    m_folderUsage = {{QStringLiteral("audio"), QStringLiteral("12,4 GB · ") + tr("%n megbeszélés", nullptr, 86)},
                     {QStringLiteral("notes"), QStringLiteral("38 MB")},
                     {QStringLiteral("meta"), QStringLiteral("210 MB")}};
    m_voiceprintText = tr("Hanglenyomat: %n minta,", nullptr, 23) + QLatin1Char(' ')
                       + tr("%n megbeszélésből", nullptr, 9);
    m_hasVoiceprint = true;
    m_ownStt = s.sttProviderId;
    m_ownLlm = s.llmProviderId;
    m_serviceMode = QStringLiteral("own");
    m_focusField.clear();
    m_liveActive = false;
    m_liveApp.clear();
    m_liveAppId.clear();
    m_detectorAvailable = true;
    m_page = QStringLiteral("general");
    m_loading = false;

    m_cloud->loadDemo(st);
    m_devices->rebuild();
    m_stt->reset();
    m_llm->reset();
    m_stt->setAdvancedOpen(false);
    m_llm->setAdvancedOpen(false);

    if (st == QLatin1String("B02") || st == QLatin1String("rename")) {
        m_page = QStringLiteral("recording");
        m_draft.deviceNames.insert(kantoMon, QStringLiteral("Kanto hangfal"));   // 1 mentetlen változás
    } else if (st == QLatin1String("B03")) {
        m_page = QStringLiteral("watcher");
        m_liveActive = true;
        m_liveApp = QStringLiteral("Microsoft Teams");
        m_liveAppId = QStringLiteral("teams");
    } else if (st == QLatin1String("B04")) {
        m_page = QStringLiteral("services");
        m_focusField = QStringLiteral("stt");
        m_llm->setDemoResult(QStringLiteral("ok"), 48, QString(), QString());
    } else if (st == QLatin1String("B05")) {
        m_page = QStringLiteral("services");
        m_stt->setDemoResult(QStringLiteral("ok"), 210, QString(), QString());
        m_llm->setDemoResult(QStringLiteral("failed"), -1,
                             tr("A végpont nem válaszol. Fut a helyi szerver (pl. LM Studio), és jó a port?"),
                             QStringLiteral("ECONNREFUSED"));
        m_llm->setAdvancedOpen(true);
    } else if (st == QLatin1String("B06") || st == QLatin1String("cloudOut")) {
        m_page = QStringLiteral("services");
        m_draft.sttProviderId = cloud::ProviderId;
        m_draft.llmProviderId = cloud::ProviderId;
        m_serviceMode = QStringLiteral("cloud");
    } else if (st == QLatin1String("teaser")) {
        m_page = QStringLiteral("services");
        m_serviceMode = QStringLiteral("cloud");
    } else if (st == QLatin1String("B07") || st == QLatin1String("schema")) {
        m_page = QStringLiteral("summary");
        // Egy saját szabállyal bővített prompt → „módosítva”.
        const QString custom = promptBuiltin(QStringLiteral("simple"))
            + QStringLiteral("7. Teendőnél add meg a felelőst és a határidőt, ha elhangzott.\n");
        m_promptText.insert(QStringLiteral("simple"), custom);
        m_base.summaryPrompt = m_draft.summaryPrompt = custom;
    } else if (st == QLatin1String("dirty") || st == QLatin1String("unsaved")) {
        m_draft.userSpeakerName = QStringLiteral("Kovács Lilla Anna");
    }

    m_devices->refreshFromDraft();
    touch();
    emit pageChanged();
    emit voiceprintChanged();
    emit liveCallChanged();
    emit deviceCountChanged();
    m_stt->notifyFocusChanged();
    m_llm->notifyFocusChanged();
    emitAllChanged();
}

} // namespace tanara_qml
