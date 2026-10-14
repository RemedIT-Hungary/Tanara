#include "SettingsViewModel.h"
#include "tanara/providers/core/ProviderConfig.h"

#include "DemoText.h"

#include "AppContext.h"
#include "SettingsDeviceModel.h"
#include "SettingsDialogs.h"
#include "SettingsEmbeddingModel.h"
#include "SettingsProviderModel.h"

#include "tanara/app/AppController.h"
#include "tanara/foundation/i18n/Localization.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/processing/summary/PromptLibrary.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/recording/audio/DeviceManager.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/watcher/detect/Autostart.h"
#include "tanara/watcher/detect/DetectorRegistry.h"
#include "tanara/watcher/detect/IMeetingDetector.h"
#include "tanara/watcher/PipeWireGraph.h"
#include "tanara/providers/embedding/ITextEmbedder.h"
#include "tanara/providers/core/ProviderRegistry.h"
#include "tanara/library/store/JsonSerialization.h"
#include "tanara/library/store/VoiceprintStore.h"

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
                         QStringLiteral("speakers"), QStringLiteral("summary")};
// The editable prompts in order: the quick summary's three prompts (single-step, per-part
// note, merge), then the two for per-topic analysis. "reduce" (the per-topic analysis'
// closing summary) can only be tuned via a file override and is not listed here.
const QStringList kPromptIds{QStringLiteral("single"), QStringLiteral("notes"), QStringLiteral("merge"),
                             QStringLiteral("topic"), QStringLiteral("analysis")};

QString promptGroupOf(const QString& id)
{
    return id == QLatin1String("topic") || id == QLatin1String("analysis") ? QStringLiteral("topics")
                                                                            : QStringLiteral("quick");
}

// The override stored in settings for the given prompt (empty = the built-in / file default applies).
QString storedPrompt(const tanara::app::AppSettings& s, const QString& id)
{
    if (id == QLatin1String("single"))   return s.processing.summaryPrompt;
    if (id == QLatin1String("notes"))    return s.processing.notesPrompt;
    if (id == QLatin1String("merge"))    return s.processing.mergePrompt;
    if (id == QLatin1String("topic"))    return s.processing.topicExtractionPrompt;
    if (id == QLatin1String("analysis")) return s.processing.topicAnalysisPrompt;
    return QString();
}

void setStoredPrompt(tanara::app::AppSettings& s, const QString& id, const QString& text)
{
    if (id == QLatin1String("single"))        s.processing.summaryPrompt = text;
    else if (id == QLatin1String("notes"))    s.processing.notesPrompt = text;
    else if (id == QLatin1String("merge"))    s.processing.mergePrompt = text;
    else if (id == QLatin1String("topic"))    s.processing.topicExtractionPrompt = text;
    else if (id == QLatin1String("analysis")) s.processing.topicAnalysisPrompt = text;
}

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

// The watched app's pretty name from the process-name fragment ("teams" -> "Microsoft Teams").
QString appLabel(const QString& match)
{
    const QString lower = match.trimmed().toLower();
    if (lower == QLatin1String("meet")) return QStringLiteral("Google Meet");
    const QString pretty = watcher::detail::prettyAppName(lower, QString());
    if (pretty != lower) return pretty;
    QString out = match.trimmed();
    if (!out.isEmpty()) out[0] = out.at(0).toUpper();
    return out;
}

// Typical names of cloud-synced folders (internal data must not be synced: two machines
// would write the index and the voiceprints at once).
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

// Folder size (recursive) + number of meeting folders (direct subfolders with a meeting.json).
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

// ---- JSON diff (static, testable) --------------------------------------------

QJsonObject SettingsViewModel::mergedSettings(const QJsonObject& base, const QJsonObject& draft,
                                              const QJsonObject& onto)
{
    QJsonObject out = onto;
    QSet<QString> keys;
    for (auto it = base.constBegin(); it != base.constEnd(); ++it) keys.insert(it.key());
    for (auto it = draft.constBegin(); it != draft.constEnd(); ++it) keys.insert(it.key());
    for (const QString& k : std::as_const(keys)) {
        const QJsonValue b = base.value(k), d = draft.value(k);
        if (b == d) continue;                       // the user did not touch it
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
        // A config of a just-selected provider filled with defaults is not a separate change
        // (the provider switch already counts).
        // Path: providers/<capability>/configs/<id>/<field>.
        const QStringList parts = p.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.size() >= 5 && parts.at(0) == QLatin1String("providers")
            && parts.at(2) == QLatin1String("configs")
            && !base.value(parts.at(0)).toObject().value(parts.at(1)).toObject()
                    .value(parts.at(2)).toObject().contains(parts.at(3)))
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

// ---- lifecycle ------------------------------------------------------------------------

SettingsViewModel::SettingsViewModel(QObject* parent) : QObject(parent)
{
    m_devices = new SettingsDeviceModel(this);
    m_stt = new SettingsProviderModel(this, providers::CapabilityKind::Transcription);
    m_llm = new SettingsProviderModel(this, providers::CapabilityKind::Chat);
    m_embedding = new SettingsEmbeddingModel(this);
    // Voice models: the enabled list is part of the draft; the download is immediate (not a draft).
    m_voiceModels = new VoiceModelListModel(this);
    m_voiceModels->setDraftAccess([this] { return m_draft.processing.voiceModels; },
                                  [this](const QStringList& ids) {
                                      if (m_draft.processing.voiceModels == ids) return;
                                      m_draft.processing.voiceModels = ids;
                                      touch();
                                  });
    connect(m_voiceModels, &VoiceModelListModel::openUrlRequested, this, [this](const QString& url) {
        if (m_dialogs) m_dialogs->openUrl(url);
    });

    m_detectTimer.setInterval(4000);
    connect(&m_detectTimer, &QTimer::timeout, this, &SettingsViewModel::pollDetector);

    m_baseTheme = m_draftTheme = AppContext::instance()->themeMode();

    // App.controller binds in the next event-loop turn, if by then the caller has not given its
    // own and has not asked for a demo state.
    QMetaObject::invokeMethod(this, [this] {
        if (m_controllerSet) return;
        if (app::AppController* c = AppContext::instance()->controller())
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
app::AppController* SettingsViewModel::controller() const { return m_controller; }
SettingsDialogs* SettingsViewModel::dialogs() const { return m_dialogs; }

void SettingsViewModel::setControllerObject(QObject* controller)
{
    setController(qobject_cast<app::AppController*>(controller));
}

void SettingsViewModel::setController(app::AppController* controller)
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
    if (m_controller) return;            // with real data the demo has no effect
    m_controllerSet = true;
    m_demoState = state;
    attach();
    emit demoStateChanged();
    emit controllerChanged();
}

void SettingsViewModel::attach()
{
    for (const QMetaObject::Connection& c : std::as_const(m_connections)) disconnect(c);
    m_connections.clear();
    m_embedding->attach();
    m_voiceModels->setController(m_controller);

    app::AppController* c = m_controller;
    if (!c) {
        loadDemo();
        return;
    }
    m_connections << connect(c->settings(), &app::SettingsManager::settingsChanged, this,
                             &SettingsViewModel::onExternalSettingsChanged);
    m_connections << connect(c, &app::AppController::devicesChanged, this, [this] {
        m_devices->rebuild();
        applyDefaultSelection();
    });
    m_connections << connect(c, &app::AppController::deviceLevelPeak, this,
                             [this](const QString& name, float rms, float peak) {
        if (m_monitoring) m_devices->onLevel(name, rms, qMax(rms, peak));
    });
    m_connections << connect(c, &app::AppController::peopleChanged, this, &SettingsViewModel::refreshVoiceprint);
    m_connections << connect(c, &app::AppController::voiceprintsChanged, this, &SettingsViewModel::refreshVoiceprint);
    loadFromCore();
    if (m_monitoring) c->retainLevelMonitoring(this);
}

void SettingsViewModel::applyDefaultSelection()
{
    // There was never a saved selection (and the user has not touched it): the system's default
    // devices are the base, without line-in-type inputs, as in the recorder.
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

void SettingsViewModel::normalize(tanara::app::AppSettings& s) const
{
    // The selected providers' empty fields get the descriptor's defaults (as the old window
    // filled them), so this does not count as a "change".
    const providers::ProviderDescriptor sd = providers::ProviderRegistry::defaultInstance().descriptor(providers::CapabilityKind::Transcription, s.providers.stt.selectedId);
    if (!sd.id.isEmpty()) m_stt->fillDefaults(s.providers.stt.configs[s.providers.stt.selectedId], sd);
    const providers::ProviderDescriptor ld = providers::ProviderRegistry::defaultInstance().descriptor(providers::CapabilityKind::Chat, s.providers.llm.selectedId);
    if (!ld.id.isEmpty()) m_llm->fillDefaults(s.providers.llm.configs[s.providers.llm.selectedId], ld);
    m_embedding->normalize(s);
}

void SettingsViewModel::loadFromCore()
{
    app::AppController* c = m_controller;
    if (!c) return;
    m_loading = true;
    m_base = c->settings()->settings();
    normalize(m_base);
    m_draft = m_base;

    m_baseSecrets.clear();
    for (SettingsProviderModel* card : {m_stt, m_llm, m_embedding->card()})
        for (const providers::ProviderDescriptor& d : card->allDescriptors())
            for (const providers::ConfigField& f : d.fields)
                if (f.isSecret && !f.secretKey.isEmpty())
                    m_baseSecrets.insert(f.secretKey, c->secret(f.secretKey));
    m_draftSecrets = m_baseSecrets;

    m_baseSelected = m_draftSelected = c->lastUsedDeviceNames();
    m_baseTheme = m_draftTheme = AppContext::instance()->themeMode();

    m_promptText.clear();
    m_promptDefault.clear();
    for (const QString& id : kPromptIds) {
        const QString def = processing::promptDefault(id, m_controller ? m_controller->settings()->paths().dataDir() : QString());
        const QString stored = storedPrompt(m_base, id);
        m_promptDefault.insert(id, def);
        m_promptText.insert(id, stored.isEmpty() ? def : stored);
    }


    m_folderUsage.clear();
    m_loading = false;

    m_devices->rebuild();
    applyDefaultSelection();
    m_stt->reset();
    m_llm->reset();
    m_embedding->reset();
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
    // Someone else saved (another window, the CLI...): the new state
    // becomes the base, and own unsaved changes are applied on top.
    storePromptsInDraft();
    tanara::app::AppSettings live = m_controller->settings()->settings();
    normalize(live);
    const QJsonObject merged = mergedSettings(m_base.toJson(), m_draft.toJson(), live.toJson());
    const QString typedName = m_draft.recording.userSpeakerName;
    m_base = live;
    m_draft = tanara::app::AppSettings::fromJson(merged);
    if (typedName.trimmed() == m_draft.recording.userSpeakerName) m_draft.recording.userSpeakerName = typedName;
    {
        // The prompts' editor text from the merged draft (an external prompt change shows too).
        for (const QString& id : kPromptIds) {
            const QString stored = storedPrompt(m_draft, id);
            if (!stored.isEmpty() || m_promptText.value(id).trimmed() != m_promptDefault.value(id).trimmed())
                m_promptText.insert(id, stored.isEmpty() ? m_promptDefault.value(id) : stored);
        }
    }
    m_devices->refreshFromDraft();
    m_stt->reset();
    m_llm->reset();
    m_embedding->reset();
    touch();
    emitAllChanged();
}

// ---- draft: counting, validation ---------------------------------------------------

void SettingsViewModel::setDraftSecret(const QString& key, const QString& value)
{
    m_draftSecrets.insert(key, value);
}

void SettingsViewModel::storePromptsInDraft()
{
    // Text equal to the default is saved EMPTY: later improvements of the code default then
    // apply until the user writes their own.
    auto stored = [this](const QString& id) {
        const QString text = m_promptText.value(id);
        return text.trimmed() == m_promptDefault.value(id).trimmed() ? QString() : text;
    };
    for (const QString& id : kPromptIds)
        setStoredPrompt(m_draft, id, stored(id));
}

void SettingsViewModel::touch()
{
    if (m_loading) return;
    storePromptsInDraft();
    tanara::app::AppSettings d = m_draft;
    d.recording.userSpeakerName = d.recording.userSpeakerName.trimmed();
    int n = countChanges(m_base.toJson(), d.toJson());
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
    m_embedding->notifyDraftChanged();
    m_voiceModels->notifyDraftChanged();
}

void SettingsViewModel::validate()
{
    QVariantMap e;
    if (m_draft.recording.userSpeakerName.trimmed().isEmpty())
        e.insert(QStringLiteral("userName"), tr("Adj meg egy nevet — ezen a néven szerepelsz az átiratokban."));
    for (const QString& key : folderKeys()) {
        const QString p = folderPath(key).trimmed();
        if (p.isEmpty())
            e.insert(QStringLiteral("folder.") + key, tr("Válassz egy mappát."));
        else if (!QDir::isAbsolutePath(paths::expandHome(p)))
            e.insert(QStringLiteral("folder.") + key, tr("Teljes elérési utat adj meg."));
    }
    if (m_draft.watcher.knownCallApps.isEmpty())
        e.insert(QStringLiteral("watchedApps"),
                 tr("Legalább egy alkalmazás kell: üres listával a figyelő semmire nem jelez."));
    if (e != m_errors) {
        m_errors = e;
        emit errorsChanged();
        emit foldersChanged();
    }
}

providers::ReadinessResult SettingsViewModel::draftReadiness(providers::WorkflowStep step) const
{
    // Fictional meeting: has an audio track and a transcript, so only the provider setting matters.
    Meeting m;
    Track t;
    t.active = true;
    m.tracks << t;
    m.hasTranscript = true;
    const providers::ReadinessModel model(m_draft.providers, [this](const QString& key) {
        if (m_draftSecrets.contains(key)) return !m_draftSecrets.value(key).trimmed().isEmpty();
        if (!m_controller) return false;
        return m_controller->hasSecret(key);
    });
    return model.check(step, m);
}

QString SettingsViewModel::secretsBackend() const
{
    // Demo / no controller: the keychain (the fictional user's usual case).
    return m_controller ? m_controller->secretBackend() : QStringLiteral("keychain");
}

QString SettingsViewModel::secretsLocationText() const
{
    return secretsBackend() == QLatin1String("keychain")
        ? tr("A kulcsok helye: a rendszer kulcstartója. A beállítás-fájlba nem kerülnek.")
        : tr("A kulcsok helye: fájl a belső adatok mappájában, csak neked olvasható. A beállítás-fájlba nem kerülnek.");
}

QString SettingsViewModel::secretsWarning() const
{
    if (!m_controller || m_controller->secretBackendWarning().isEmpty()) return QString();
    return tr("A rendszer kulcstartója nem érhető el, ezért a kulcsok titkosítatlan fájlban vannak "
              "(csak a te felhasználód olvashatja).");
}

void SettingsViewModel::updateServicesWarn()
{
    const bool warn = !draftReadiness(providers::WorkflowStep::Transcribe).runnable
                      || !draftReadiness(providers::WorkflowStep::Summarize).runnable
                      || m_stt->lastTestFailed() || m_llm->lastTestFailed();
    if (warn == m_servicesWarn) return;
    m_servicesWarn = warn;
    emit servicesWarnChanged();
}

QString SettingsViewModel::footerText() const
{
    if (m_changeCount <= 0) return tr("Nincs mentetlen változás");
    // Changing the embedding model also restarts the library's preparation.
    if (m_embedding->restartPending()) return tr("Modellváltás · mentéskor újraindul az előkészítés");
    // A new voice model was enabled: on save, backfilling the existing samples starts.
    bool modelAdded = false;
    for (const QString& id : m_draft.processing.voiceModels)
        if (!m_base.processing.voiceModels.contains(id)) modelAdded = true;
    if (modelAdded && m_changeCount == 1) return tr("Új hangmodell · mentéskor indul a minták pótlása");
    return tr("%n nem mentett változás", nullptr, m_changeCount);
}

// ---- window ----

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
    else if (page == QLatin1String("voiceModels")) target = QStringLiteral("speakers");
    else if (page.isEmpty()) target = m_page;
    if (kPages.contains(target)) setPage(target);

    if (page == QLatin1String("cloud"))
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

// ---- General ---------------------------------------------------------------------

void SettingsViewModel::setUserName(const QString& name)
{
    if (m_draft.recording.userSpeakerName == name) return;
    m_draft.recording.userSpeakerName = name;
    emit generalChanged();
    touch();
}

void SettingsViewModel::refreshVoiceprint()
{
    QString text;
    bool has = false;
    if (m_controller && m_controller->voiceprints()) {
        const QVector<Voiceprint> prints = m_controller->voiceprints()->printsFor(m_base.recording.userSpeakerName);
        // One sample is one item: per-model sibling voiceprints count as one.
        QSet<QString> meetings, samples;
        for (const Voiceprint& p : prints) {
            if (!p.sourceMeetingId.isEmpty()) meetings.insert(p.sourceMeetingId);
            samples.insert(library::VoiceprintStore::siblingKey(p));
        }
        has = !prints.isEmpty();
        if (has)
            text = tr("Hanglenyomat: %n minta,", nullptr, samples.size()) + QLatin1Char(' ')
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
    if (m_draft.ui.language == lang) return;
    m_draft.ui.language = lang;
    emit generalChanged();
    touch();
}

QVariantList SettingsViewModel::uiLanguageOptions() const
{
    auto mk = [](const QString& v, const QString& l) {
        return QVariantMap{{QStringLiteral("value"), v}, {QStringLiteral("label"), l}};
    };
    const bool sysHu = QLocale::system().language() == QLocale::Hungarian;
    // Language names are deliberately in their own language: findable in a foreign-language UI too.
    return {mk(QStringLiteral("auto"), sysHu ? tr("Rendszer nyelve (magyar)") : tr("Rendszer nyelve (English)")),
            mk(QStringLiteral("hu"), QStringLiteral("Magyar")),
            mk(QStringLiteral("en"), QStringLiteral("English"))};
}

bool SettingsViewModel::languageNeedsRestart() const
{
    if (!m_controller) return false;
    const QString active = activeUiLanguage();
    return !active.isEmpty() && resolvedUiLanguage(m_draft.ui.language) != active;
}

void SettingsViewModel::setThemeMode(const QString& mode)
{
    const QString m = AppContext::normalizedThemeMode(mode);
    if (m == m_draftTheme) return;
    m_draftTheme = m;
    AppContext::instance()->setThemeMode(m);   // live preview
    emit themeModeChanged();
    touch();
}

// The folder keys of the General page: recordings, notes, internal data, speaker models.
QStringList SettingsViewModel::folderKeys()
{
    return {QStringLiteral("audio"), QStringLiteral("notes"), QStringLiteral("meta"), QStringLiteral("models")};
}

namespace {
paths::Role folderRole(const QString& key)
{
    if (key == QLatin1String("audio")) return paths::Role::Recordings;
    if (key == QLatin1String("notes")) return paths::Role::Notes;
    if (key == QLatin1String("models")) return paths::Role::Models;
    return paths::Role::Data;
}

// The configured ("paths" section) value of a folder; empty = the default.
QString configuredFolder(const paths::PathSettings& p, const QString& key)
{
    if (key == QLatin1String("audio")) return p.primaryRecordingRoot();
    if (key == QLatin1String("notes")) return p.notesDir;
    if (key == QLatin1String("meta")) return p.dataDir;
    if (key == QLatin1String("models")) return p.modelsDir;
    return {};
}
} // namespace

QString SettingsViewModel::defaultFolder(const QString& key) const
{
    // The effective folder without a "paths" setting (with TANARA_HOME: the sandbox's).
    return paths::AppPaths::system(paths::PathSettings()).dir(folderRole(key));
}

QString SettingsViewModel::folderPath(const QString& key) const
{
    if (!folderKeys().contains(key)) return QString();
    // TANARA_HOME pins the data folder (the sandbox).
    if (key == QLatin1String("meta") && m_controller && !paths::homeOverride().isEmpty())
        return paths::homeOverride();
    const QString configured = configuredFolder(m_draft.paths, key);
    if (!configured.isEmpty()) return configured;
    return m_controller ? defaultFolder(key) : QString();
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
    if (looksSynced(folderPath(QStringLiteral("meta"))))
        metaWarn = tr("Ez a mappa felhő-szinkronban lévőnek tűnik. A belső adatokat ne szinkronizáld: "
                      "két gép egyszerre írná, és megsérülhet.");
    QString metaHint = metaLocked
        ? tr("Hanglenyomatok, személyek, címkék és index. Ezt a mappát most a TANARA_HOME környezeti változó rögzíti.")
        : tr("Hanglenyomatok, személyek, címkék és index. Ne szinkronizáld felhőtárhellyel. "
             "A váltás újraindítás után érvényes; a meglévő adatokat nem költözteti át.");
    // With TANARA_HOME the sandbox's own folders are in effect, whatever is set here.
    const QString sandboxWarn = metaLocked
        ? tr("A TANARA_HOME be van állítva: amíg él, a homokozó saját mappája érvényes, nem ez.")
        : QString();
    return {
        mk(QStringLiteral("audio"), tr("Felvételek"),
           tr("Hangsávok, megbeszélésenként egy mappa. Az új felvételek ide kerülnek."), false, sandboxWarn),
        mk(QStringLiteral("notes"), tr("Jegyzetek"),
           tr("Az összefoglalók másolata sima Markdown-fájlként (pl. a jegyzettáradba)."), false, sandboxWarn),
        mk(QStringLiteral("meta"), tr("Belső adatok"), metaHint, metaLocked, metaWarn),
        mk(QStringLiteral("models"), tr("Hangmodellek"),
           tr("A beszélő-felismerés letöltött modelljei (.onnx). A váltás újraindítás után érvényes; "
              "a meglévő fájlokat nem költözteti át."), false, sandboxWarn),
    };
}

void SettingsViewModel::refreshFolderUsage()
{
    if (!m_controller) return;
    const int generation = ++m_usageGeneration;
    QPointer<SettingsViewModel> self(this);
    for (const QString& key : folderKeys()) {
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
    if (!folderKeys().contains(key)) return;
    if (key == QLatin1String("meta") && m_controller && !paths::homeOverride().isEmpty()) return;   // pinned
    // The default folder is stored as "no setting" (later default changes then apply).
    const QString stored = m_controller && paths::normalize(p) == defaultFolder(key) ? QString() : p;
    if (key == QLatin1String("audio")) m_draft.paths.setPrimaryRecordingRoot(stored);
    else if (key == QLatin1String("notes")) m_draft.paths.notesDir = stored;
    else if (key == QLatin1String("meta")) m_draft.paths.dataDir = stored;
    else m_draft.paths.modelsDir = stored;
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
    // The link sits next to the own-voiceprint row: the own person gets selected.
    if (m_dialogs) m_dialogs->openPeopleAt(m_base.recording.userSpeakerName.trimmed());
}

void SettingsViewModel::openPerson(const QString& person)
{
    if (m_dialogs) m_dialogs->openPeopleAt(person.trimmed());
}

bool SettingsViewModel::onboardingAvailable() const
{
    if (m_dialogs) return m_dialogs->canOpenOnboarding();
    return demo();
}

void SettingsViewModel::openOnboarding()
{
    if (m_dialogs) m_dialogs->openOnboarding();
}

// ---- Recording ----------------------------------------------------------------------

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
    if (m_draft.recording.autoRecordAllDevices == on) return;
    m_draft.recording.autoRecordAllDevices = on;
    emit recordingChanged();
    touch();
}

void SettingsViewModel::setAudioQuality(const QString& quality)
{
    if (m_draft.recording.audioQuality == quality) return;
    m_draft.recording.audioQuality = quality;
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
    const int kbps = opusBitrateKbps(m_draft.recording.audioQuality);
    const int mb = qRound(kbps * 1000.0 / 8.0 * 5400.0 / 1e6);   // 1.5 hours on one track
    const QString q = m_draft.recording.audioQuality;
    const QString tail = q == QLatin1String("low")    ? tr("A legkisebb fájl; az átíráshoz még elég.")
                       : q == QLatin1String("medium") ? tr("Beszédhez elég.")
                       : q == QLatin1String("high")   ? tr("Zajos teremhez, több beszélőhöz.")
                                                      : tr("A legjobb minőség, a legnagyobb fájl.");
    return tr("%1 kbps Opus · kb. %2 MB sávonként 1,5 óra alatt.").arg(kbps).arg(mb) + QLatin1Char(' ') + tail;
}

void SettingsViewModel::setMixdownMode(const QString& mode)
{
    if (m_draft.recording.mixdownMode == mode) return;
    m_draft.recording.mixdownMode = mode;
    emit recordingChanged();
    touch();
}

QStringList SettingsViewModel::selectedDevicesForSave() const
{
    // The selection of currently disconnected devices is kept; present ones come from the draft.
    const QStringList present = m_devices->presentNames();
    QStringList out;
    for (const QString& n : m_baseSelected)
        if (!present.contains(n)) out << n;
    for (const QString& n : present)
        if (m_draftSelected.contains(n)) out << n;
    return out;
}

// ---- Call watcher ------------------------------------------------------------------

void SettingsViewModel::setDetectorEnabled(bool on)
{
    if (m_draft.watcher.detectorEnabled == on) return;
    m_draft.watcher.detectorEnabled = on;
    emit watcherChanged();
    touch();
}

void SettingsViewModel::setWatcherAutostart(bool on)
{
    if (m_draft.watcher.autostart == on) return;
    m_draft.watcher.autostart = on;
    emit watcherChanged();
    touch();
}

void SettingsViewModel::setAskStopOnCallEnd(bool on)
{
    if (m_draft.watcher.askStopOnCallEnd == on) return;
    m_draft.watcher.askStopOnCallEnd = on;
    emit watcherChanged();
    touch();
}

void SettingsViewModel::setSilenceAskMinutes(int minutes)
{
    const int m = std::clamp(minutes, 0, 60);
    if (m_draft.watcher.silenceAskMinutes == m) return;
    m_draft.watcher.silenceAskMinutes = m;
    emit watcherChanged();
    touch();
}

void SettingsViewModel::setDetectorIntervalSec(int sec)
{
    const int s = std::clamp(sec, 3, 60);
    if (m_draft.watcher.detectorIntervalSec == s) return;
    m_draft.watcher.detectorIntervalSec = s;
    emit watcherChanged();
    touch();
}

QVariantList SettingsViewModel::watchedApps() const
{
    QVariantList out;
    for (const QString& match : m_draft.watcher.knownCallApps) {
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
    for (const QString& have : m_draft.watcher.knownCallApps)
        if (have.compare(m, Qt::CaseInsensitive) == 0) return tr("Ez már szerepel a listán.");
    m_draft.watcher.knownCallApps << m;
    emit watchedAppsChanged();
    touch();
    return QString();
}

void SettingsViewModel::removeWatchedApp(int index)
{
    if (index < 0 || index >= m_draft.watcher.knownCallApps.size()) return;
    m_draft.watcher.knownCallApps.removeAt(index);
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
        // Names of running processes from /proc (kernel threads have no command line, so they are skipped).
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
        for (const QString& have : m_draft.watcher.knownCallApps)
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

void SettingsViewModel::setDetectorForTest(watcher::IMeetingDetector* detector)
{
    m_detector.reset(detector);
    m_detectorTried = true;
}

void SettingsViewModel::pollDetector()
{
    if (!m_watching) return;
    if (!m_detector && !m_detectorTried && m_controller) {
        m_detectorTried = true;
        watcher::registerBuiltinDetectors();   // idempotent
        m_detector.reset(m_draft.watcher.detectorId.isEmpty()
                             ? watcher::MeetingDetectorRegistry::instance().createBest()
                             : watcher::MeetingDetectorRegistry::instance().create(m_draft.watcher.detectorId));
    }
    const bool available = m_detector && m_detector->isAvailable();
    watcher::MeetingSignal sig;
    if (available) {
        // With the DRAFT's list: an app just added shows immediately if it is in a call.
        m_detector->configure(m_draft.watcher.knownCallApps, QStringLiteral("tanara"));
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

// ---- Services ------------------------------------------------------------

bool SettingsViewModel::cloudSelected() const
{
    return m_draft.providers.stt.selectedId == cloud::ProviderId || m_draft.providers.llm.selectedId == cloud::ProviderId;
}

void SettingsViewModel::setServiceMode(const QString& mode)
{
    // Only the shown tab changes: Tanara Cloud is "coming soon", so there is nothing to save.
    const QString m = mode == QLatin1String("cloud") ? QStringLiteral("cloud") : QStringLiteral("own");
    if (m == m_serviceMode) return;
    m_serviceMode = m;
    emit serviceModeChanged();
}

// ---- Summary ------------------------------------------------------------------

void SettingsViewModel::setSummaryLanguage(const QString& language)
{
    const QString l = language.trimmed();
    if (l.isEmpty() || m_draft.processing.summaryLanguage == l) return;
    m_draft.processing.summaryLanguage = l;
    emit summaryChanged();
    touch();
}

QStringList SettingsViewModel::summaryLanguageOptions() const
{
    // The value is free text (it goes into the prompt): written in Hungarian, like the built-in prompts.
    QStringList out{QStringLiteral("magyar"), QStringLiteral("angol"), QStringLiteral("német"),
                    QStringLiteral("francia"), QStringLiteral("spanyol")};
    if (!out.contains(m_draft.processing.summaryLanguage) && !m_draft.processing.summaryLanguage.isEmpty())
        out.prepend(m_draft.processing.summaryLanguage);
    return out;
}

QString SettingsViewModel::promptId(int index) const
{
    return kPromptIds.value(index, kPromptIds.first());
}

QVariantList SettingsViewModel::promptGroups() const
{
    // tabs: the index of the group's prompts in promptTabs (constant: the tabs are not rebuilt).
    auto indices = [](const QString& group) {
        QVariantList out;
        for (int i = 0; i < kPromptIds.size(); ++i)
            if (promptGroupOf(kPromptIds.at(i)) == group) out << i;
        return out;
    };
    QVariantList groups{
        QVariantMap{{QStringLiteral("value"), QStringLiteral("quick")},
                    {QStringLiteral("label"), tr("Gyors összefoglaló")},
                    {QStringLiteral("note"),
                     tr("Rövid megbeszélésnél (kb. 22 percig) egyetlen hívás fut az „Egy lépésben” "
                        "utasítással; hosszabbnál az átirat kb. 15 perces részeire egyenként "
                        "„Részjegyzet” készül, majd az „Összefésülés” írja meg belőlük a vezetői "
                        "összefoglalót (a memó a részjegyzetekből áll össze).")}},
        QVariantMap{{QStringLiteral("value"), QStringLiteral("topics")},
                    {QStringLiteral("label"), tr("Témánkénti elemzés")},
                    {QStringLiteral("note"),
                     tr("Előbb a „Témajavaslat” gyűjti ki a megbeszélés témáit, majd a jóváhagyott "
                        "témákra egyenként az „Elemzés” fut.")}},
    };
    for (QVariant& g : groups) {
        QVariantMap m = g.toMap();
        m.insert(QStringLiteral("tabs"), indices(m.value(QStringLiteral("value")).toString()));
        g = m;
    }
    return groups;
}

QString SettingsViewModel::promptGroup() const
{
    return promptGroupOf(promptId(m_promptIndex));
}

void SettingsViewModel::setPromptGroup(const QString& group)
{
    if (group == promptGroup()) return;
    for (int i = 0; i < kPromptIds.size(); ++i)
        if (promptGroupOf(kPromptIds.at(i)) == group) { setPromptIndex(i); return; }
}

QVariantList SettingsViewModel::promptTabs() const
{
    const QStringList labels{tr("Egy lépésben"), tr("Részjegyzet"), tr("Összefésülés"),
                             tr("Témajavaslat"), tr("Elemzés")};
    QVariantList out;
    for (int i = 0; i < kPromptIds.size(); ++i) {
        const QString id = kPromptIds.at(i);
        out << QVariantMap{
            {QStringLiteral("id"), id}, {QStringLiteral("label"), labels.at(i)},
            {QStringLiteral("group"), promptGroupOf(id)},
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
    // promptTextChanged is deliberately NOT emitted: the editor wrote it itself (the cursor must not jump).
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
    for (const processing::PromptVariable& v : tanara::processing::promptVariables())
        out << QVariantMap{{QStringLiteral("token"), v.token}, {QStringLiteral("description"), v.description}};
    return out;
}

QString SettingsViewModel::schemaSummary() const { return processing::promptOutputFormat(promptId(m_promptIndex)).summary; }
QString SettingsViewModel::schemaBody() const { return processing::promptOutputFormat(promptId(m_promptIndex)).body; }
QString SettingsViewModel::schemaKind() const { return processing::promptOutputFormat(promptId(m_promptIndex)).kind; }

// ---- save / discard ------------------------------------------------------------------

bool SettingsViewModel::save()
{
    touch();
    if (!dirty()) return true;

    // We do not save with an invalid field: we switch to the faulty page, where the field shows below it.
    QString badPage;
    if (m_errors.contains(QStringLiteral("userName")))
        badPage = QStringLiteral("speakers");
    else if (m_errors.contains(QStringLiteral("folder.audio"))
        || m_errors.contains(QStringLiteral("folder.notes")) || m_errors.contains(QStringLiteral("folder.meta")))
        badPage = QStringLiteral("general");
    else if (m_errors.contains(QStringLiteral("watchedApps")))
        badPage = QStringLiteral("watcher");
    else if (!m_stt->fieldErrors().isEmpty() || !m_llm->fieldErrors().isEmpty()
             || !m_embedding->card()->fieldErrors().isEmpty())
        badPage = QStringLiteral("services");
    if (!badPage.isEmpty()) {
        setPage(badPage);
        return false;
    }

    // Provider fields sit raw in the draft while typing: trimmed on save.
    for (QMap<QString, providers::ProviderConfig>* map : {&m_draft.providers.stt.configs, &m_draft.providers.llm.configs, &m_draft.providers.embedding.configs})
        for (auto it = map->begin(); it != map->end(); ++it) {
            it->baseUrl = it->baseUrl.trimmed();
            it->model = it->model.trimmed();
            for (auto e = it->extra.begin(); e != it->extra.end(); ++e)
                if (e->typeId() == QMetaType::QString) *e = e->toString().trimmed();
        }

    const QString newName = m_draft.recording.userSpeakerName.trimmed();
    const bool themeChanged = m_baseTheme != m_draftTheme;
    const QString theme = m_draftTheme;

    if (!m_controller) {
        // Demo: nothing to write to disk; the draft becomes the base.
        m_draft.recording.userSpeakerName = newName;
        m_base = m_draft;
        m_baseSecrets = m_draftSecrets;
        m_baseSelected = m_draftSelected;
        m_baseTheme = m_draftTheme;
        touch();
        emit saved();
        return true;
    }

    app::AppController* c = m_controller;
    m_saving = true;

    // 1) settings: only the DIFFERENCE goes onto the core's currently effective state.
    const tanara::app::AppSettings live = c->settings()->settings();
    tanara::app::AppSettings draft = m_draft;
    draft.recording.userSpeakerName = m_base.recording.userSpeakerName;   // the core handles the rename as a separate operation
    const QJsonObject liveJson = live.toJson();
    const QJsonObject mergedJson = mergedSettings(m_base.toJson(), draft.toJson(), liveJson);
    if (mergedJson != liveJson) {
        tanara::app::AppSettings merged = tanara::app::AppSettings::fromJson(mergedJson);
        if (!paths::homeOverride().isEmpty()) merged.paths.dataDir = live.paths.dataDir;
        c->settings()->setSettings(merged);
    }
    // 2) own name: the core carries it through the person DB, the meetings and the voiceprints.
    if (!newName.isEmpty() && newName != m_base.recording.userSpeakerName.trimmed())
        c->setUserSpeakerName(newName);
    // 3) secrets -> SecretStore.
    for (auto it = m_draftSecrets.constBegin(); it != m_draftSecrets.constEnd(); ++it)
        if (m_baseSecrets.value(it.key()) != it.value()) c->setSecret(it.key(), it.value().trimmed());
    // 4) default sources (selection shared with the recorder).
    {
        QStringList a = m_baseSelected, b = m_draftSelected;
        a.sort();
        b.sort();
        if (a != b) c->setLastUsedDeviceNames(selectedDevicesForSave());
    }
    // 5) starting the watcher at sign-in: effective immediately (never in a sandbox / test).
    if (m_base.watcher.autostart != m_draft.watcher.autostart)
        watcher::autostart::applyWatcher(m_draft.watcher.autostart,
                                watcher::autostart::findWatcherExecutable(QCoreApplication::applicationDirPath()));
    m_saving = false;

    loadFromCore();
    if (themeChanged) {
        AppContext::instance()->setThemeMode(theme);
        m_baseTheme = m_draftTheme = theme;
        emit themeModeSaved(theme);
        touch();
    }
    emit saved();

    // If the gap is gone, back to the meeting.
    if (!m_focusField.isEmpty()) {
        const providers::WorkflowStep step = m_focusField == QLatin1String("stt") ? providers::WorkflowStep::Transcribe
                                                                        : providers::WorkflowStep::Summarize;
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
        AppContext::instance()->setThemeMode(m_baseTheme);   // the preview reverts
    }
    for (const QString& id : kPromptIds) {
        const QString stored = storedPrompt(m_base, id);
        m_promptText.insert(id, stored.isEmpty() ? m_promptDefault.value(id) : stored);
    }
    m_devices->refreshFromDraft();
    m_stt->reset();
    m_llm->reset();
    m_embedding->reset();
    touch();
    emitAllChanged();
    refreshFolderUsage();
}

// ---- demo (fictional data for the design states) -----------------------------------------

void SettingsViewModel::loadDemo()
{
    const QString st = m_demoState;
    m_loading = true;

    tanara::app::AppSettings s;
    s.recording.userSpeakerName = demoName("Kovács Lilla");
    s.paths.setPrimaryRecordingRoot(demoText("/home/lilla/Tanara/felvetelek", "/home/emma/Tanara/recordings"));
    s.paths.notesDir = demoText("/home/lilla/Tanara/jegyzetek", "/home/emma/Tanara/notes");
    s.paths.dataDir = demoText("/home/lilla/.tanara", "/home/emma/.tanara");
    s.paths.modelsDir = demoText("/home/lilla/.tanara/models", "/home/emma/.tanara/models");
    s.ui.language = QStringLiteral("auto");
    s.recording.autoRecordAllDevices = false;
    s.recording.audioQuality = QStringLiteral("medium");
    s.recording.mixdownMode = QStringLiteral("manual");
    s.processing.summaryLanguage = QStringLiteral("magyar");
    s.watcher.knownCallApps = {QStringLiteral("zoom"), QStringLiteral("teams"), QStringLiteral("webex"),
                       QStringLiteral("slack"), QStringLiteral("discord"), QStringLiteral("meet")};
    s.watcher.autostart = true;
    s.providers.stt.selectedId = QStringLiteral("soniox");
    s.providers.llm.selectedId = QStringLiteral("openai-compat");
    s.providers.stt.configs.clear();
    s.providers.llm.configs.clear();
    {
        providers::ProviderConfig llm;
        llm.type = s.providers.llm.selectedId;
        llm.baseUrl = QStringLiteral("http://localhost:1234/v1");
        llm.model = QStringLiteral("google/gemma-4-12b-qat");
        llm.temperature = 0.2;
        llm.maxTokens = 30000;
        s.providers.llm.configs.insert(s.providers.llm.selectedId, llm);
    }
    const QString trust = QStringLiteral("alsa_input.usb-Trust_USB_Microphone-00.mono-fallback");
    const QString headsetMon = demoText("Monitor of Sennheiser headset - Kommunikáció",
                                        "Monitor of Sennheiser headset - Communication");
    const QString kantoMon = demoText("Monitor of Kanto YU4 - Optikai digitális sztereó",
                                      "Monitor of Kanto YU4 - Digital Stereo (IEC958)");
    s.recording.deviceNames = {
        {trust, demoText("Trust USB mikrofon", "Trust USB microphone")},
        {QStringLiteral("alsa_input.pci-0000_00_1f.3.analog-stereo"), demoText("Sennheiser fejhallgató (analóg)", "Sennheiser headphones (analog)")},
        {headsetMon, QStringLiteral("Sennheiser headset")},
        {QStringLiteral("alsa_input.pci-0000_00_1f.3.analog-stereo.line-in"), demoText("Alaplapi vonalbemenet", "Motherboard line-in")},
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
        const QString def = processing::promptBuiltin(id);
        m_promptDefault.insert(id, def);
        m_promptText.insert(id, def);
    }
    m_promptIndex = 0;

    m_folderUsage = {{QStringLiteral("audio"), QStringLiteral("12,4 GB · ") + tr("%n megbeszélés", nullptr, 86)},
                     {QStringLiteral("notes"), QStringLiteral("38 MB")},
                     {QStringLiteral("meta"), QStringLiteral("210 MB")},
                     {QStringLiteral("models"), QStringLiteral("96 MB")}};
    m_voiceprintText = tr("Hanglenyomat: %n minta,", nullptr, 23) + QLatin1Char(' ')
                       + tr("%n megbeszélésből", nullptr, 9);
    m_hasVoiceprint = true;
    m_serviceMode = QStringLiteral("own");
    m_focusField.clear();
    m_liveActive = false;
    m_liveApp.clear();
    m_liveAppId.clear();
    m_detectorAvailable = true;
    m_page = QStringLiteral("general");
    m_loading = false;

    // States of the Embedding card; other pages have no embedding ("base").
    const QString emb = st == QLatin1String("B04") || st == QLatin1String("B04embedding") ? QStringLiteral("local")
        : st == QLatin1String("B04embeddingRunning") ? QStringLiteral("localRunning")
        : st == QLatin1String("B04embeddingError") ? QStringLiteral("localError")
        : st == QLatin1String("B04embeddingDone") ? QStringLiteral("localDone")
        : st == QLatin1String("B04embeddingCloud") ? QStringLiteral("cloud")
        : st == QLatin1String("B04modelChange") ? QStringLiteral("modelChange")
        : st == QLatin1String("B04embeddingNone") ? QStringLiteral("none") : QString();
    m_embedding->loadDemo(emb);
    m_voiceModels->loadDemo(st.startsWith(QLatin1String("B08")) ? st : QStringLiteral("B08"));
    m_devices->rebuild();
    m_stt->reset();
    m_llm->reset();
    m_stt->setAdvancedOpen(false);
    m_llm->setAdvancedOpen(false);

    if (st == QLatin1String("B02") || st == QLatin1String("rename")) {
        m_page = QStringLiteral("recording");
        m_draft.recording.deviceNames.insert(kantoMon, demoText("Kanto hangfal", "Kanto speakers"));   // 1 unsaved change
    } else if (st == QLatin1String("B03")) {
        m_page = QStringLiteral("watcher");
        m_liveActive = true;
        m_liveApp = QStringLiteral("Microsoft Teams");
        m_liveAppId = QStringLiteral("teams");
    } else if (st == QLatin1String("B04")) {
        m_page = QStringLiteral("services");
        m_focusField = QStringLiteral("stt");
        m_llm->setDemoResult(QStringLiteral("ok"), 48, QString(), QString());
    } else if (st.startsWith(QLatin1String("B04"))) {
        // Transcription and summary are already set up -> collapsed cards.
        m_page = QStringLiteral("services");
        m_stt->setDemoResult(QStringLiteral("ok"), 210, QString(), QString());
        m_llm->setDemoResult(QStringLiteral("ok"), 48, QString(), QString());
    } else if (st == QLatin1String("advanced")) {
        // The LLM card's "Advanced" part is open (temperature, max tokens, thinking).
        m_page = QStringLiteral("services");
        m_llm->setDemoResult(QStringLiteral("ok"), 48, QString(), QString());
        m_llm->setAdvancedOpen(true);
        providers::llmctx::LlmServerInfo info;   // LM Studio: the model is loaded, on one thread
        info.kind = providers::llmctx::LlmServerInfo::Kind::LmStudio;
        info.modelListed = true;
        info.modelKey = QStringLiteral("google/gemma-4-12b-qat");
        info.maxContext = 131072;
        info.instances = {{info.modelKey, 20480, 1}};
        m_llm->setDemoServerInfo(info);
    } else if (st == QLatin1String("B05")) {
        m_page = QStringLiteral("services");
        m_stt->setDemoResult(QStringLiteral("ok"), 210, QString(), QString());
        m_llm->setDemoResult(QStringLiteral("failed"), -1,
                             tr("A végpont nem válaszol. Fut a helyi szerver (pl. LM Studio), és jó a port?"),
                             QStringLiteral("ECONNREFUSED"));
        m_llm->setAdvancedOpen(true);
    } else if (st == QLatin1String("cloudComingSoon")) {
        m_page = QStringLiteral("services");
        m_serviceMode = QStringLiteral("cloud");
    } else if (st == QLatin1String("cloudSelected")) {
        // The saved setting names Tanara Cloud (developer setting): not available, own key needed.
        m_page = QStringLiteral("services");
        m_draft.providers.stt.selectedId = cloud::ProviderId;
        m_draft.providers.llm.selectedId = cloud::ProviderId;
        m_stt->reset();
        m_llm->reset();
    } else if (st == QLatin1String("B07") || st == QLatin1String("B07notes") || st == QLatin1String("schema")) {
        m_page = QStringLiteral("summary");
        // A prompt extended with an own rule -> "modified".
        const QString custom = processing::promptBuiltin(QStringLiteral("single"))
            + QStringLiteral("\nFor each action item, name the owner and the deadline when they were said.\n");
        m_promptText.insert(QStringLiteral("single"), custom);
        m_base.processing.summaryPrompt = m_draft.processing.summaryPrompt = custom;
        if (st == QLatin1String("B07notes"))
            m_promptIndex = 1;      // the per-part note prompt
    } else if (st.startsWith(QLatin1String("B08"))) {
        m_page = QStringLiteral("speakers");
        // The draft's enabled list follows the demo's model state.
        m_base.processing.voiceModels = m_draft.processing.voiceModels = QStringList{QStringLiteral("wespeaker-resnet34-lm")};
    } else if (st == QLatin1String("dirty") || st == QLatin1String("unsaved")) {
        m_draft.recording.userSpeakerName = demoText("Kovács Lilla Anna", "Emma Anne Carter");
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
