#include "SettingsViewModel.h"
#include "tanara/providers/core/ProviderConfig.h"

#include "DemoText.h"

#include "AppContext.h"
#include "UiServices.h"
#include "SettingsDeviceModel.h"
#include "SettingsDialogs.h"
#include "SettingsEmbeddingModel.h"
#include "SettingsProviderModel.h"

#include "tanara/app/ISettingsFacade.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/domain/Meeting.h"
#include "tanara/foundation/i18n/Localization.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/processing/summary/PromptLibrary.h"
#include "tanara/providers/core/ProviderRegistry.h"
#include "tanara/providers/core/Routes.h"
#include "tanara/providers/embedding/ITextEmbedder.h"
#include "tanara/recording/IRecorderFacade.h"
#include "tanara/watcher/PipeWireGraph.h"
#include "tanara/watcher/detect/IMeetingDetector.h"

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
                         QStringLiteral("processing"), QStringLiteral("cloud"),
                         QStringLiteral("people")};

// Where an openPage() call lands: page + section, the card whose key the deep link highlights
// ("" | "stt" | "llm"), and whether the summary card's "Haladó" block opens (the old Summary page = the prompts).
struct PageTarget {
    QString page;
    QString section;
    QString focus;
    bool advanced = false;
};

// Maps a page key or a legacy target (the old six-page names, ShellActions.openSettings's
// fixTargets) to page + section. `current` is the page shown now (the target of "").
PageTarget resolvePageTarget(const QString& page, const QString& arg, const QString& current)
{
    const auto sectionOr = [&arg](const QString& p, const QString& fallback) {
        return SettingsViewModel::sectionsOf(p).contains(arg) ? arg : fallback;
    };
    const QString processing = QStringLiteral("processing");
    if (page.isEmpty()) return {current, sectionOr(current, QString()), QString()};
    if (page == QLatin1String("general") || page == QLatin1String("recording")) {
        const QString first = SettingsViewModel::sectionsOf(page).value(0);
        return {page, sectionOr(page, first), QString()};
    }
    if (page == QLatin1String("watcher")) return {QStringLiteral("recording"), QStringLiteral("watcher"), QString()};
    if (page == QLatin1String("providers") || page == QLatin1String("services")) {
        // providers + nothing: the transcriber comes first; services + nothing: the summary
        // (OverviewSummaryStep's fixTarget).
        const bool stt = arg == QLatin1String("stt")
                         || (arg.isEmpty() && page == QLatin1String("providers"));
        const QString focus = arg == QLatin1String("stt") || arg == QLatin1String("llm") ? arg : QString();
        return {processing, stt ? QStringLiteral("stt") : QStringLiteral("summary"), focus};
    }
    if (page == QLatin1String("summary")) return {processing, QStringLiteral("summary"), QString(), true};
    if (page == QLatin1String("speakers") || page == QLatin1String("diarization"))
        return {processing, arg == QLatin1String("voiceModels") ? QStringLiteral("voice") : QStringLiteral("diar"), QString()};
    if (page == QLatin1String("voiceModels")) return {processing, QStringLiteral("voice"), QString()};
    if (kPages.contains(page)) return {page, sectionOr(page, QString()), QString()};
    return {current, QString(), QString()};   // unknown: stay
}

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
    init();
    AppContext* ctx = AppContext::instance();
    connect(ctx, &AppContext::controllerChanged, this, &SettingsViewModel::resolveServices);
    connect(ctx, &AppContext::demoChanged, this, &SettingsViewModel::resolveServices);
    resolveServices();
}

SettingsViewModel::SettingsViewModel(const UiServices& services, QObject* parent)
    : QObject(parent), m_explicitFacade(true)
{
    init();
    m_voiceModels->useServices(services, QStringLiteral("B08"));
    m_segModels->useServices(services, QStringLiteral("B08"));
    useFacade(services.settings(QString(), this));
}

void SettingsViewModel::init()
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
    // The segmentation models of the local diarization: immediate download, nothing in the draft.
    m_segModels = new VoiceModelListModel(this);
    m_segModels->setKind(QStringLiteral("segmentation"));
    connect(m_segModels, &VoiceModelListModel::openUrlRequested, this, [this](const QString& url) {
        if (m_dialogs) m_dialogs->openUrl(url);
    });

    // The cloud page asks for a fresh account / catalog whenever it is shown.
    connect(this, &SettingsViewModel::pageChanged, this, [this] {
        if (m_page == QLatin1String("cloud") && m_facade) m_facade->cloudRefresh();
    });
    // The account texts depend on the draft too (the hours line follows the tier).
    connect(this, &SettingsViewModel::dirtyChanged, this, &SettingsViewModel::cloudChanged);
    // S2: the cards' status follows the tests, the model lists and the embedding state.
    for (SettingsProviderModel* card : {m_stt, m_llm, m_embedding->card()}) {
        connect(card, &SettingsProviderModel::testChanged, this, &SettingsViewModel::refreshProcessing);
        connect(card, &SettingsProviderModel::valuesChanged, this, &SettingsViewModel::refreshProcessing);
        connect(card, &SettingsProviderModel::providerChanged, this, &SettingsViewModel::refreshProcessing);
    }
    connect(m_embedding, &SettingsEmbeddingModel::changed, this, &SettingsViewModel::refreshProcessing);
    for (VoiceModelListModel* list : {m_voiceModels, m_segModels}) {
        connect(list, &VoiceModelListModel::summaryChanged, this, &SettingsViewModel::refreshProcessing);
        connect(list, &QAbstractItemModel::dataChanged, this, &SettingsViewModel::refreshProcessing);
        connect(list, &QAbstractItemModel::modelReset, this, &SettingsViewModel::refreshProcessing);
    }

    m_detectTimer.setInterval(4000);
    connect(&m_detectTimer, &QTimer::timeout, this, &SettingsViewModel::pollDetector);

    m_baseTheme = m_draftTheme = AppContext::instance()->themeMode();
}

SettingsViewModel::~SettingsViewModel()
{
    if (m_facade && m_monitoring) m_facade->releaseLevelMonitoring(this);
}

SettingsDialogs* SettingsViewModel::dialogs() const { return m_dialogs; }

// The process's services changed (a controller arrived, App.demo flipped), or the design state.
void SettingsViewModel::resolveServices()
{
    if (m_explicitFacade) return;
    const UiServices& services = AppContext::instance()->services();
    const QString voiceScenario = m_demoState.startsWith(QLatin1String("B08")) ? m_demoState : QStringLiteral("B08");
    m_voiceModels->useServices(services, voiceScenario);
    m_segModels->useServices(services, voiceScenario);
    useFacade(services.settings(m_demoState, this));
}

void SettingsViewModel::useFacade(app::ISettingsFacade* facade)
{
    for (const QMetaObject::Connection& c : std::as_const(m_connections)) disconnect(c);
    m_connections.clear();
    if (m_facade && m_monitoring) m_facade->releaseLevelMonitoring(this);
    if (m_facade && m_facade != facade && m_facade->parent() == this) m_facade->deleteLater();   // a fictional facade of ours
    m_facade = facade;
    m_live = facade && facade->live();
    m_detector.reset();
    m_detectorTried = false;
    m_stt->attach();
    m_llm->attach();
    if (m_embedding->card()) m_embedding->card()->attach();
    m_embedding->attach();
    if (!facade) return;

    m_connections << connect(facade, &app::ISettingsFacade::settingsChanged, this,
                             &SettingsViewModel::onExternalSettingsChanged);
    m_connections << connect(facade, &app::ISettingsFacade::voiceprintsChanged, this, &SettingsViewModel::refreshVoiceprint);
    if (recording::IRecorderFacade* dev = facade->devices()) {
        m_connections << connect(dev, &recording::IRecorderFacade::devicesChanged, this, [this] {
            m_devices->rebuild();
            applyDefaultSelection();
        });
        m_connections << connect(dev, &recording::IRecorderFacade::deviceLevel, this,
                                 [this](const QString& name, float rms, float peak) {
            if (m_monitoring) m_devices->onLevel(name, rms, qMax(rms, peak));
        });
    }
    attachCloud(facade);
    loadFromCore();
    if (m_monitoring) facade->retainLevelMonitoring(this);
    emit facadeChanged();
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
    if (m_demoState == state) return;
    m_demoState = state;
    // A fictional facade serves one design state: a new one for the new state.
    if (!m_live) resolveServices();
    emit demoStateChanged();
}

void SettingsViewModel::applyDefaultSelection()
{
    // There was never a saved selection (and the user has not touched it): the system's default
    // devices are the base, without line-in-type inputs, as in the recorder.
    recording::IRecorderFacade* dev = m_facade ? m_facade->devices() : nullptr;
    if (!dev || !dev->lastUsedDeviceNames().isEmpty() || !m_baseSelected.isEmpty() || !m_draftSelected.isEmpty())
        return;
    QStringList def;
    for (const AudioDeviceInfo& d : dev->captureDevices())
        if (d.isDefault && d.kind != TrackKind::Other) def << d.name;
    if (def.isEmpty()) return;
    m_baseSelected = m_draftSelected = def;
    m_devices->refreshFromDraft();
}

void SettingsViewModel::normalize(tanara::app::AppSettings& s) const
{
    // The selected providers' empty fields get the descriptor's defaults (as the old window
    // filled them), so this does not count as a "change".
    s.routes.normalize(s.providers);   // S3: the routes follow the provider selection
    if (!m_facade) return;
    const providers::ProviderRegistry& reg = m_facade->registry();
    const providers::ProviderDescriptor sd = reg.descriptor(providers::CapabilityKind::Transcription, s.providers.stt.selectedId);
    if (!sd.id.isEmpty()) m_stt->fillDefaults(s.providers.stt.configs[s.providers.stt.selectedId], sd);
    const providers::ProviderDescriptor ld = reg.descriptor(providers::CapabilityKind::Chat, s.providers.llm.selectedId);
    if (!ld.id.isEmpty()) m_llm->fillDefaults(s.providers.llm.configs[s.providers.llm.selectedId], ld);
    m_embedding->normalize(s);
}

void SettingsViewModel::loadFromCore()
{
    app::ISettingsFacade* c = m_facade;
    if (!c) return;
    m_loading = true;
    m_base = c->settings();
    normalize(m_base);
    m_draft = m_base;

    m_baseSecrets.clear();
    for (SettingsProviderModel* card : {m_stt, m_llm, m_embedding->card()})
        for (const providers::ProviderDescriptor& d : card->allDescriptors())
            for (const providers::ConfigField& f : d.fields)
                if (f.isSecret && !f.secretKey.isEmpty())
                    m_baseSecrets.insert(f.secretKey, c->secret(f.secretKey));
    m_draftSecrets = m_baseSecrets;

    m_baseSelected = m_draftSelected = c->devices() ? c->devices()->lastUsedDeviceNames() : QStringList();
    m_baseTheme = m_draftTheme = AppContext::instance()->themeMode();

    m_promptText.clear();
    m_promptDefault.clear();
    for (const QString& id : kPromptIds) {
        const QString def = c->promptDefault(id);
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
    applyScenario();
    touch();
    emitAllChanged();
}

void SettingsViewModel::reload()
{
    loadFromCore();
}

void SettingsViewModel::emitAllChanged()
{
    emit generalChanged();
    emit themeModeChanged();
    emit foldersChanged();
    emit recordingChanged();
    emit watcherChanged();
    emit watchedAppsChanged();
    emit routesChanged();
    emit summaryChanged();
    emit promptsChanged();
    emit promptIndexChanged();
    emit promptTextChanged();
    emit focusChanged();
}

void SettingsViewModel::onExternalSettingsChanged()
{
    if (m_saving || m_loading || !m_facade) return;
    // Someone else saved (another window, the CLI...): the new state
    // becomes the base, and own unsaved changes are applied on top.
    storePromptsInDraft();
    tanara::app::AppSettings live = m_facade->settings();
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

int SettingsViewModel::routeChanges(const tanara::app::AppSettings& base, const tanara::app::AppSettings& draft)
{
    // S3: a route counts as a change of its own unless it only follows a provider picked on a card (that
    // change is counted already: the embedding's route always follows its provider, stt / llm while both
    // routes are Local / External); the remembered provider ids are bookkeeping.
    using providers::Capability;
    using providers::Route;
    auto own = [](Route r) { return r == Route::Local || r == Route::External; };
    int n = 0;
    for (Capability cap : providers::allCapabilities()) {
        const Route a = base.routes.route(cap), b = draft.routes.route(cap);
        if (a == b || cap == Capability::Embedding) continue;
        const QString selA = cap == Capability::Stt ? base.providers.stt.selectedId : base.providers.llm.selectedId;
        const QString selB = cap == Capability::Stt ? draft.providers.stt.selectedId : draft.providers.llm.selectedId;
        if ((cap == Capability::Stt || cap == Capability::Llm) && selA != selB && own(a) && own(b)) continue;
        ++n;
    }
    return n;
}

void SettingsViewModel::touch()
{
    if (m_loading) return;
    storePromptsInDraft();
    tanara::app::AppSettings d = m_draft;
    d.recording.userSpeakerName = d.recording.userSpeakerName.trimmed();
    d.routes.normalize(d.providers);   // a provider picked on a card moves its route (as the save does)
    QJsonObject baseJson = m_base.toJson(), draftJson = d.toJson();
    baseJson.remove(QStringLiteral("routes"));
    draftJson.remove(QStringLiteral("routes"));
    int n = countChanges(baseJson, draftJson) + routeChanges(m_base, d);
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
    refreshProcessing();
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
    return readiness(step, false);
}

providers::ReadinessResult SettingsViewModel::readiness(providers::WorkflowStep step, bool ownPath) const
{
    // Fictional meeting: has an audio track and a transcript, so only the provider setting matters.
    Meeting m;
    Track t;
    t.active = true;
    m.tracks << t;
    m.hasTranscript = true;
    // S3: the draft routes' providers (Cloud -> the Tanara Cloud adapter; Ask -> the own provider).
    tanara::app::AppSettings d = m_draft;
    d.routes.normalize(d.providers);
    auto stored = [&d, ownPath](providers::Capability cap) {
        const providers::Route r = d.routes.route(cap);
        const bool own = r == providers::Route::Ask || (ownPath && r == providers::Route::Cloud);
        return own ? d.routes.fallbackRoute(cap, d.providers) : r;
    };
    const providers::ProviderSettings eff =
        d.routes.effective(d.providers, stored(providers::Capability::Stt), stored(providers::Capability::Llm));
    const providers::ReadinessModel model(eff, [this](const QString& key) {
        if (m_draftSecrets.contains(key)) return !m_draftSecrets.value(key).trimmed().isEmpty();
        return m_facade && m_facade->hasSecret(key);
    }, m_facade ? &m_facade->registry() : nullptr);
    return model.check(step, m);
}

QString SettingsViewModel::secretsBackend() const
{
    return m_facade ? m_facade->secretBackend() : QStringLiteral("keychain");
}

QString SettingsViewModel::secretsLocationText() const
{
    return secretsBackend() == QLatin1String("keychain")
        ? tr("A kulcsok helye: a rendszer kulcstartója. A beállítás-fájlba nem kerülnek.")
        : tr("A kulcsok helye: fájl a belső adatok mappájában, csak neked olvasható. A beállítás-fájlba nem kerülnek.");
}

QString SettingsViewModel::secretsWarning() const
{
    if (!m_facade || m_facade->secretBackendWarning().isEmpty()) return QString();
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
    setSection(QString());
}

QStringList SettingsViewModel::sectionsOf(const QString& page)
{
    if (page == QLatin1String("general"))
        return {QStringLiteral("appearance"), QStringLiteral("folders"), QStringLiteral("account")};
    if (page == QLatin1String("recording"))
        return {QStringLiteral("devices"), QStringLiteral("watcher")};
    if (page == QLatin1String("processing"))
        return {QStringLiteral("stt"), QStringLiteral("diar"), QStringLiteral("voice"),
                QStringLiteral("summary"), QStringLiteral("tags")};
    return {};
}

void SettingsViewModel::setSection(const QString& section)
{
    if (section == m_section || (!section.isEmpty() && !sectionsOf(m_page).contains(section))) return;
    m_section = section;
    emit sectionChanged();
}

void SettingsViewModel::showSection(const QString& page, const QString& section)
{
    setPage(page);
    setSection(section);
    // A jump to a processing card opens it.
    if (m_page == QLatin1String("processing") && !m_section.isEmpty()) setCardExpanded(m_section, true);
    emit sectionRequested(m_section, !m_section.isEmpty());
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

void SettingsViewModel::openPage(const QString& page, const QString& section)
{
    const PageTarget target = resolvePageTarget(page, section, m_page);
    // "" (Ctrl+, / reopening): the last page stays as it was, without scrolling.
    if (target.advanced) setSummaryAdvancedOpen(true);
    if (!page.isEmpty()) showSection(target.page, target.section);

    const QString& f = target.focus;
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
    if (m_facade) {
        const app::VoiceprintSummary prints = m_facade->ownVoiceprints();
        has = prints.samples > 0;
        if (has)
            text = tr("Hanglenyomat: %n minta,", nullptr, prints.samples) + QLatin1Char(' ')
                   + tr("%n megbeszélésből", nullptr, prints.meetings);
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
    if (!m_facade) return false;
    const QString active = m_facade->activeUiLanguage();
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
    return m_facade ? m_facade->defaultFolder(folderRole(key)) : QString();
}

QString SettingsViewModel::folderPath(const QString& key) const
{
    if (!folderKeys().contains(key)) return QString();
    // TANARA_HOME pins the data folder (the sandbox).
    if (key == QLatin1String("meta") && m_live && m_facade->dataDirPinned())
        return paths::homeOverride();
    const QString configured = configuredFolder(m_draft.paths, key);
    if (!configured.isEmpty()) return configured;
    return m_live ? defaultFolder(key) : QString();
}

QVariantList SettingsViewModel::folders() const
{
    const bool metaLocked = m_live && m_facade->dataDirPinned();
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
    if (!m_facade) return;
    const int generation = ++m_usageGeneration;
    auto usageText = [](const app::FolderUsage& u, bool audio) {
        if (!u.exists) return SettingsViewModel::tr("még nincs ilyen mappa — mentéskor létrejön");
        QString text = formatBytes(u.bytes);
        if (audio) text += QStringLiteral(" · ") + SettingsViewModel::tr("%n megbeszélés", nullptr, u.meetings);
        return text;
    };
    QPointer<SettingsViewModel> self(this);
    QPointer<app::ISettingsFacade> facade(m_facade);
    for (const QString& key : folderKeys()) {
        const QString path = paths::expandHome(folderPath(key).trimmed());
        const bool audio = key == QLatin1String("audio");
        if (!m_live) {
            // The fictional facade measures nothing: its sizes are at hand.
            m_folderUsage.insert(key, usageText(m_facade->folderUsage(path, audio), audio));
            continue;
        }
        QThreadPool::globalInstance()->start([self, facade, generation, key, path, audio, usageText] {
            if (!facade) return;
            const app::FolderUsage u = facade->folderUsage(path, audio);
            QMetaObject::invokeMethod(qApp, [self, generation, key, u, audio, usageText] {
                if (!self || self->m_usageGeneration != generation) return;
                self->m_folderUsage.insert(key, usageText(u, audio));
                emit self->foldersChanged();
            }, Qt::QueuedConnection);
        });
    }
    if (!m_live) emit foldersChanged();
}

void SettingsViewModel::setFolder(const QString& key, const QString& path)
{
    const QString p = path.trimmed();
    if (p == folderPath(key)) return;
    if (!folderKeys().contains(key)) return;
    if (key == QLatin1String("meta") && m_live && m_facade->dataDirPinned()) return;   // pinned
    // The default folder is stored as "no setting" (later default changes then apply).
    const QString stored = m_live && paths::normalize(p) == defaultFolder(key) ? QString() : p;
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

// ---- further recording roots ------------------------------------------------------------------

QVariantList SettingsViewModel::recordingRoots() const
{
    QVariantList out;
    const QString primary = paths::normalize(folderPath(QStringLiteral("audio")));
    for (const paths::RecordingRoot& r : m_draft.paths.recordingRoots) {
        const QString path = paths::normalize(r.path);
        if (path.isEmpty() || path == primary) continue;
        const int meetings = m_facade ? m_facade->meetingFolderCount(path) : 0;
        out << QVariantMap{{QStringLiteral("path"), r.path},
                           {QStringLiteral("meetings"), meetings},
                           {QStringLiteral("removable"), meetings == 0}};
    }
    return out;
}

void SettingsViewModel::addRecordingRoot()
{
    if (!m_dialogs) return;
    const QString dir = m_dialogs->pickFolder(tr("Újabb felvétel-mappa"), QDir::homePath());
    if (!dir.isEmpty()) addRecordingRootPath(dir);
}

void SettingsViewModel::addRecordingRootPath(const QString& path)
{
    const QString p = paths::normalize(path);
    if (p.isEmpty() || p == paths::normalize(folderPath(QStringLiteral("audio")))) return;
    for (const paths::RecordingRoot& r : std::as_const(m_draft.paths.recordingRoots))
        if (paths::normalize(r.path) == p) return;
    // Keeping the effective primary explicit: with only the default primary there is no entry for it yet.
    if (m_draft.paths.primaryRecordingRoot().isEmpty() && m_live)
        m_draft.paths.recordingRoots.prepend(paths::RecordingRoot{defaultFolder(QStringLiteral("audio")), true});
    m_draft.paths.recordingRoots << paths::RecordingRoot{path.trimmed(), false};
    emit foldersChanged();
    touch();
}

void SettingsViewModel::makePrimaryRecordingRoot(const QString& path)
{
    const QString p = paths::normalize(path);
    const QString oldPrimary = folderPath(QStringLiteral("audio"));
    if (p.isEmpty() || p == paths::normalize(oldPrimary)) return;
    QList<paths::RecordingRoot> roots{paths::RecordingRoot{path.trimmed(), true}};
    if (!oldPrimary.trimmed().isEmpty()) roots << paths::RecordingRoot{oldPrimary.trimmed(), false};
    for (const paths::RecordingRoot& r : std::as_const(m_draft.paths.recordingRoots)) {
        const QString n = paths::normalize(r.path);
        if (n == p || n == paths::normalize(oldPrimary)) continue;
        roots << paths::RecordingRoot{r.path, false};
    }
    m_draft.paths.recordingRoots = roots;
    m_folderUsage.remove(QStringLiteral("audio"));
    emit foldersChanged();
    touch();
    refreshFolderUsage();
}

void SettingsViewModel::removeRecordingRoot(const QString& path)
{
    const QString p = paths::normalize(path);
    if (p.isEmpty() || p == paths::normalize(folderPath(QStringLiteral("audio")))) return;   // not the primary
    if (m_facade && m_facade->meetingFolderCount(p) > 0) return;   // only an empty root
    const auto before = m_draft.paths.recordingRoots.size();
    m_draft.paths.recordingRoots.removeIf([&p](const paths::RecordingRoot& r) { return paths::normalize(r.path) == p; });
    if (m_draft.paths.recordingRoots.size() == before) return;
    emit foldersChanged();
    touch();
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
    if (m_facade) {
        if (on) m_facade->retainLevelMonitoring(this);
        else m_facade->releaseLevelMonitoring(this);
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
    QStringList names = m_facade ? m_facade->runningApps() : QStringList();
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
    if (on && (m_live || m_detector)) {
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
    if (!m_detector && !m_detectorTried && m_facade) {
        m_detectorTried = true;
        m_detector.reset(m_facade->createDetector(m_draft.watcher.detectorId));
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
    return m_draft.routes.stt.route == providers::Route::Cloud || m_draft.routes.llm.route == providers::Route::Cloud;
}

// ---- S3: processing routes ------------------------------------------------------

providers::RouteContext SettingsViewModel::routeContext(providers::Capability cap) const
{
    providers::RouteContext ctx;
    if (!m_facade) return ctx;
    const providers::ProviderRegistry& reg = m_facade->registry();
    // The stt / llm cloud adapters exist only in live mode: that is the "cloud is live" signal here too.
    ctx.cloudLive = reg.has(providers::CapabilityKind::Transcription, providers::CloudProviderId);
    const providers::CapabilityKind kind = cap == providers::Capability::Stt ? providers::CapabilityKind::Transcription
        : cap == providers::Capability::Llm ? providers::CapabilityKind::Chat
        : cap == providers::Capability::Embedding ? providers::CapabilityKind::TextEmbedding
        : cap == providers::Capability::Voice ? providers::CapabilityKind::VoiceEmbedding
        : providers::CapabilityKind::Diarization;
    for (const providers::ProviderDescriptor& d : reg.descriptors(kind)) {
        if (d.kind == providers::ProviderKind::Local) ctx.localIds << d.id;
        else if (d.kind == providers::ProviderKind::External) ctx.externalIds << d.id;
        else ctx.cloudId = d.id;
    }
    ctx.localIds.sort();
    ctx.externalIds.sort();
    return ctx;
}

bool SettingsViewModel::applyRoute(providers::Capability cap, providers::Route r)
{
    if (m_draft.routes.route(cap) == r) return true;
    const QString sttBefore = m_draft.providers.stt.selectedId, llmBefore = m_draft.providers.llm.selectedId;
    const QString embBefore = m_draft.providers.embedding.selectedId;
    if (!m_draft.routes.set(cap, r, routeContext(cap), m_draft.providers)) return false;
    // A newly selected provider gets its defaults and the card follows (the provider models do it).
    if (m_draft.providers.stt.selectedId != sttBefore) {
        const QString id = m_draft.providers.stt.selectedId;
        m_draft.providers.stt.selectedId = sttBefore;
        m_stt->setProviderId(id);
    }
    if (m_draft.providers.llm.selectedId != llmBefore) {
        const QString id = m_draft.providers.llm.selectedId;
        m_draft.providers.llm.selectedId = llmBefore;
        m_llm->setProviderId(id);
    }
    if (m_draft.providers.embedding.selectedId != embBefore) {
        const QString id = m_draft.providers.embedding.selectedId;
        if (!id.isEmpty() && m_facade) {
            providers::ProviderConfig cfg = m_draft.providers.embedding.configs.value(id);
            m_embedding->card()->fillDefaults(
                cfg, m_facade->registry().descriptor(providers::CapabilityKind::TextEmbedding, id));
            m_draft.providers.embedding.configs.insert(id, cfg);
        }
        m_embedding->card()->reset();
    }
    emit routesChanged();
    touch();
    return true;
}

QString SettingsViewModel::route(const QString& cap) const
{
    const std::optional<providers::Capability> c = providers::capabilityFromKey(cap);
    if (!c) return QString();
    // A provider picked on a card moves a Local / External route (normalize: the provider's kind decides).
    tanara::app::AppSettings d = m_draft;
    d.routes.normalize(d.providers);
    return providers::routeKey(d.routes.route(*c));
}

bool SettingsViewModel::setRoute(const QString& cap, const QString& route)
{
    const std::optional<providers::Capability> c = providers::capabilityFromKey(cap);
    const std::optional<providers::Route> r = providers::routeFromKey(route);
    if (!c || !r) return false;
    m_draft.routes.normalize(m_draft.providers);
    return applyRoute(*c, *r);
}

QVariantList SettingsViewModel::routeOptions(const QString& cap) const
{
    const std::optional<providers::Capability> c = providers::capabilityFromKey(cap);
    if (!c) return {};
    const providers::RouteContext ctx = routeContext(*c);
    const QList<providers::Route> values = *c == providers::Capability::Embedding
        ? QList<providers::Route>{providers::Route::Basic, providers::Route::Local, providers::Route::External, providers::Route::Cloud}
        : QList<providers::Route>{providers::Route::Local, providers::Route::External, providers::Route::Cloud, providers::Route::Ask};
    QVariantList out;
    for (providers::Route r : values) {
        const QString code = providers::routeBlocker(*c, r, ctx);
        QString reason;
        if (code == QLatin1String("voice_local_only"))
            reason = tr("A hanglenyomat a gépeden marad: a beszélők felismerése mindig helyben fut.");
        else if (code == QLatin1String("embedding_no_ask"))
            reason = tr("A címkejavaslat a háttérben fut, ezért nem tud kérdezni.");
        else if (code == QLatin1String("not_embedding"))
            reason = tr("Ennél a képességnél nincs alap mód.");
        else if (code == QLatin1String("no_local"))
            reason = tr("Ehhez nincs helyi szolgáltató.");
        else if (code == QLatin1String("no_external"))
            reason = tr("Ehhez nincs külső, saját kulcsos szolgáltató.");
        else if (code == QLatin1String("cloud_unavailable"))
            reason = tr("A Tanara Cloud még nem érhető el.");
        else if (code == QLatin1String("ask_unavailable"))
            reason = tr("A „Kérdezzen” módhoz a Tanara Cloud és egy saját szolgáltató is kell.");
        else if (code == QLatin1String("diarization_later"))
            reason = tr("A beszélők elkülönítésénél ez a lehetőség később érkezik.");
        out << QVariantMap{{QStringLiteral("value"), providers::routeKey(r)},
                           {QStringLiteral("enabled"), code.isEmpty()},
                           {QStringLiteral("code"), code},
                           {QStringLiteral("reason"), reason}};
    }
    return out;
}

QString SettingsViewModel::providerKind(const QString& providerId) const
{
    if (m_facade)
        for (providers::CapabilityKind k : {providers::CapabilityKind::Transcription, providers::CapabilityKind::Chat,
                                            providers::CapabilityKind::TextEmbedding})
            if (m_facade->registry().has(k, providerId))
                return providers::providerKindKey(m_facade->registry().descriptor(k, providerId).kind);
    return providers::providerKindKey(providers::providerKindOf(providerId));
}

// ---- S2: the Processing page's cards ---------------------------------------------

namespace {

// The state of a model list's rows: "ok" (a verified model is usable) | "unverified" (only a model accepted
// despite a checksum mismatch) | "missing". onlyEnabled: the voice models count only when enabled (the CAM++
// fallback runs without one: VoiceModelListModel::fallbackActive).
QString modelListStatus(const VoiceModelListModel* list, bool onlyEnabled)
{
    bool verified = false, unverified = false;
    for (int r = 0; r < list->rowCount(); ++r) {
        const QModelIndex i = list->index(r);
        if (onlyEnabled && !list->data(i, VoiceModelListModel::EnabledRole).toBool()) continue;
        const QString st = list->data(i, VoiceModelListModel::StateRole).toString();
        if (st == QLatin1String("downloaded") || st == QLatin1String("bundled")) verified = true;
        else if (st == QLatin1String("unverified")) unverified = true;
    }
    if (verified) return QStringLiteral("ok");
    if (unverified) return QStringLiteral("unverified");
    return QStringLiteral("missing");
}

} // namespace

QVariantMap SettingsViewModel::capabilityEntry(const QString& key, providers::Capability cap) const
{
    using providers::Route;
    const QString capKey = providers::capabilityKey(cap);
    const QString routeStr = route(capKey);
    tanara::app::AppSettings d = m_draft;
    d.routes.normalize(d.providers);
    const Route r = d.routes.route(cap);
    const Route own = d.routes.fallbackRoute(cap, d.providers);
    const bool cloudLive = routeContext(cap).hasCloud();

    QString status = QStringLiteral("none");
    QString warnText;
    QString where;
    // A local route: an own server (whisper-compat, LM Studio, the embedding endpoint) or a model Tanara runs.
    const QString localWhere = cap == providers::Capability::Diarization || cap == providers::Capability::Voice
        ? QStringLiteral("local-tanara") : QStringLiteral("local-server");
    switch (r) {
    case Route::Local: where = localWhere; break;
    case Route::External: where = QStringLiteral("external"); break;
    case Route::Cloud: where = QStringLiteral("cloud"); break;
    case Route::Ask: where = QStringLiteral("ask"); break;
    case Route::Basic: where = QStringLiteral("basic"); break;
    }

    if (cap == providers::Capability::Stt || cap == providers::Capability::Llm) {
        const providers::WorkflowStep step = cap == providers::Capability::Stt ? providers::WorkflowStep::Transcribe
                                                                              : providers::WorkflowStep::Summarize;
        const SettingsProviderModel* card = cap == providers::Capability::Stt ? m_stt : m_llm;
        const bool ownReady = readiness(step, true).runnable;
        if (r == Route::Cloud) {
            if (!cloudLive) {
                status = QStringLiteral("soon");
                warnText = tr("A Tanara Cloud még nem érhető el: válassz helyi vagy külső utat");
            } else if (readiness(step, false).runnable) {
                status = QStringLiteral("ok");
            } else {
                warnText = tr("Jelentkezz be a Tanara Cloudba");
            }
        } else {
            status = ownReady ? QStringLiteral("ok") : QStringLiteral("none");
            if (!ownReady) warnText = tr("Egy szolgáltató beállítása hiányzik");
            else if (card->lastTestFailed()) warnText = tr("A legutóbbi kapcsolat-teszt nem sikerült");
        }
    } else if (cap == providers::Capability::Diarization) {
        if (r == Route::Local) {
            status = modelListStatus(m_segModels, false);
            if (status == QLatin1String("missing")) warnText = tr("Nincs szegmentáló modell");
            else if (status == QLatin1String("unverified")) warnText = tr("Ellenőrizetlen szegmentáló modell");
        } else {
            // External: the transcriber separates the speakers (Soniox; the Tanara Cloud transcription too).
            const Route stt = d.routes.route(providers::Capability::Stt);
            bool separates = stt == Route::Cloud;
            if (m_facade && !separates)
                separates = m_facade->registry().descriptor(providers::CapabilityKind::Transcription,
                                                            d.providers.stt.selectedId).supportsDiarization;
            status = separates ? QStringLiteral("ok") : QStringLiteral("none");
        }
    } else if (cap == providers::Capability::Voice) {
        status = m_voiceModels->fallbackActive() ? QStringLiteral("ok") : modelListStatus(m_voiceModels, true);
        if (!m_voiceModels->anyAvailable()) status = QStringLiteral("missing");
        if (status == QLatin1String("missing")) warnText = tr("Nincs letöltött hangmodell");
        else if (status == QLatin1String("unverified")) warnText = tr("Ellenőrizetlen hangmodell");
    } else {   // embedding: a background step, never an attention dot
        if (r == Route::Basic) status = QStringLiteral("ok");
        else if (r == Route::Cloud) status = cloudLive ? QStringLiteral("ok") : QStringLiteral("soon");
        else status = m_embedding->card()->configured() ? QStringLiteral("ok") : QStringLiteral("none");
    }

    return {{QStringLiteral("key"), key},
            {QStringLiteral("cap"), capKey},
            {QStringLiteral("route"), routeStr},
            {QStringLiteral("where"), where},
            {QStringLiteral("ownRoute"), providers::routeKey(own)},
            {QStringLiteral("status"), status},
            {QStringLiteral("warn"), !warnText.isEmpty()},
            {QStringLiteral("warnText"), warnText},
            {QStringLiteral("options"), routeOptions(capKey)}};
}

void SettingsViewModel::refreshProcessing()
{
    if (m_loading) return;
    using providers::Capability;
    QVariantList caps;
    caps << capabilityEntry(QStringLiteral("stt"), Capability::Stt)
         << capabilityEntry(QStringLiteral("diar"), Capability::Diarization)
         << capabilityEntry(QStringLiteral("voice"), Capability::Voice)
         << capabilityEntry(QStringLiteral("summary"), Capability::Llm)
         << capabilityEntry(QStringLiteral("tags"), Capability::Embedding);
    tanara::app::AppSettings d = m_draft;
    d.routes.normalize(d.providers);
    const bool locked = d.routes.route(Capability::Llm) == providers::Route::Cloud;
    const bool notice = locked && m_base.routes.route(Capability::Llm) != providers::Route::Cloud && anyPromptModified();
    if (caps == m_capabilities && locked == m_promptsLocked && notice == m_promptNotice) return;
    m_capabilities = caps;
    m_promptsLocked = locked;
    m_promptNotice = notice;
    emit processingChanged();
}

void SettingsViewModel::setCardExpanded(const QString& key, bool expanded)
{
    if (!sectionsOf(QStringLiteral("processing")).contains(key) || m_expandedCards.contains(key) == expanded) return;
    if (expanded) m_expandedCards << key;
    else m_expandedCards.removeAll(key);
    emit expandedCardsChanged();
}

void SettingsViewModel::setSummaryAdvancedOpen(bool open)
{
    if (m_summaryAdvancedOpen == open) return;
    m_summaryAdvancedOpen = open;
    m_llm->setAdvancedOpen(open);   // the LLM's advanced fields live in this block (and refresh the server data)
    emit summaryAdvancedOpenChanged();
}

bool SettingsViewModel::anyPromptModified() const
{
    for (const QString& id : kPromptIds)
        if (m_promptText.value(id).trimmed() != m_promptDefault.value(id).trimmed()) return true;
    return false;
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
    QString badPage, badSection;
    if (m_errors.contains(QStringLiteral("userName")))
        badPage = QStringLiteral("people");
    else if (m_errors.contains(QStringLiteral("folder.audio"))
        || m_errors.contains(QStringLiteral("folder.notes")) || m_errors.contains(QStringLiteral("folder.meta")))
        badPage = QStringLiteral("general"), badSection = QStringLiteral("folders");
    else if (m_errors.contains(QStringLiteral("watchedApps")))
        badPage = QStringLiteral("recording"), badSection = QStringLiteral("watcher");
    else if (!m_stt->fieldErrors().isEmpty())
        badPage = QStringLiteral("processing"), badSection = QStringLiteral("stt");
    else if (!m_llm->fieldErrors().isEmpty())
        badPage = QStringLiteral("processing"), badSection = QStringLiteral("summary");
    else if (!m_embedding->card()->fieldErrors().isEmpty())
        badPage = QStringLiteral("processing"), badSection = QStringLiteral("tags");
    if (!badPage.isEmpty()) {
        showSection(badPage, badSection);
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

    if (!m_live) {
        // Fictional facade: nothing to write to disk; the draft becomes the base.
        m_draft.recording.userSpeakerName = newName;
        m_base = m_draft;
        m_baseSecrets = m_draftSecrets;
        m_baseSelected = m_draftSelected;
        m_baseTheme = m_draftTheme;
        touch();
        emit saved();
        return true;
    }

    app::ISettingsFacade* c = m_facade;
    m_saving = true;

    // 1) settings: only the DIFFERENCE goes onto the core's currently effective state.
    const tanara::app::AppSettings live = c->settings();
    tanara::app::AppSettings draft = m_draft;
    draft.recording.userSpeakerName = m_base.recording.userSpeakerName;   // the core handles the rename as a separate operation
    const QJsonObject liveJson = live.toJson();
    const QJsonObject mergedJson = mergedSettings(m_base.toJson(), draft.toJson(), liveJson);
    if (mergedJson != liveJson) {
        tanara::app::AppSettings merged = tanara::app::AppSettings::fromJson(mergedJson);
        if (c->dataDirPinned()) merged.paths.dataDir = live.paths.dataDir;
        c->setSettings(merged);
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
        if (a != b && c->devices()) c->devices()->setLastUsedDeviceNames(selectedDevicesForSave());
    }
    // 5) starting the watcher at sign-in: effective immediately (never in a sandbox / test).
    if (m_base.watcher.autostart != m_draft.watcher.autostart)
        c->applyWatcherAutostart(m_draft.watcher.autostart);
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

// ---- Tanara Cloud (S4) ---------------------------------------------------------------------

void SettingsViewModel::attachCloud(app::ISettingsFacade* facade)
{
    m_connections << connect(facade, &app::ISettingsFacade::cloudStateChanged, this, &SettingsViewModel::refreshCloud);
    m_connections << connect(facade, &app::ISettingsFacade::cloudTopupReady, this, [this](const QString& url) {
        if (m_dialogs && !url.isEmpty()) m_dialogs->openUrl(url);
    });
    refreshCloud();
    if (m_page == QLatin1String("cloud")) facade->cloudRefresh();
}

void SettingsViewModel::refreshCloud()
{
    m_cloud = m_facade ? m_facade->cloudState() : app::CloudAccountState{};
    emit cloudChanged();
}

QVariantMap SettingsViewModel::cloudAccount() const
{
    const QString lang = activeUiLanguage();
    const cloud::AccountInfo& a = m_cloud.account;
    QVariantMap m;
    m.insert(QStringLiteral("valid"), a.valid);
    m.insert(QStringLiteral("email"), m_cloud.email);
    m.insert(QStringLiteral("balance"), a.valid ? cloud::formatMoney(a.balance, cloud::MoneyStyle::Balance, lang) : QString());
    m.insert(QStringLiteral("vatLabel"), a.valid && !a.vatMode.isEmpty() ? cloud::vatLabel(a.vatMode) : QString());
    m.insert(QStringLiteral("lowBalance"), a.valid && a.lowBalance && !a.balanceEmpty);
    m.insert(QStringLiteral("balanceEmpty"), a.valid && a.balanceEmpty);
    QString hours;
    if (a.valid) {
        // What the balance is enough for at the chosen tier (the account gives both).
        const bool fast = cloudTier() == QLatin1String("fast");
        const double h = fast ? a.hoursFast : a.hoursAccurate;
        if (h > 0)
            hours = fast ? tr("kb. %1 átírás a Gyors szinten").arg(cloud::formatHours(h, lang))
                         : tr("kb. %1 átírás a Pontos szinten").arg(cloud::formatHours(h, lang));
    }
    m.insert(QStringLiteral("hoursText"), hours);
    m.insert(QStringLiteral("signInCode"), m_cloud.deviceCode.userCode);
    m.insert(QStringLiteral("signInUrl"), m_cloud.deviceCode.verificationUriComplete.isEmpty()
                                              ? m_cloud.deviceCode.verificationUri
                                              : m_cloud.deviceCode.verificationUriComplete);
    m.insert(QStringLiteral("error"), m_cloud.error);
    return m;
}

QString SettingsViewModel::cloudTier() const
{
    // One account tier over the two stored ones (transcription, summary).
    const QString& stt = m_draft.cloud.sttTier;
    return stt == m_draft.cloud.llmTier ? stt : QStringLiteral("mixed");
}

void SettingsViewModel::setCloudTier(const QString& tier)
{
    if (!cloudWritable() || (tier != QLatin1String("fast") && tier != QLatin1String("accurate"))) return;
    cloud::CloudSettings& c = m_draft.cloud;
    if (c.sttTier == tier && c.llmTier == tier && c.sttModel.isEmpty() && c.llmModel.isEmpty()) return;
    c.sttTier = c.llmTier = tier;
    // The client has no Pro level: a concrete (Expert) model left from the CLI would override the tier.
    c.sttModel.clear();
    c.llmModel.clear();
    touch();
}

QVariantList SettingsViewModel::cloudTierOptions() const
{
    const QString lang = activeUiLanguage();
    QVariantList out;
    for (const QString& tier : {QStringLiteral("fast"), QStringLiteral("accurate")}) {
        QString price;
        // The transcription's per-hour price is the comparable one (the summary is per token).
        const std::optional<cloud::CloudModel> m = cloud::findTierModel(m_cloud.models, QStringLiteral("stt"), tier);
        if (m && m->perHour.isValid())
            price = tr("%1 / óra").arg(cloud::formatMoney(m->perHour, cloud::MoneyStyle::Charge, lang));
        out << QVariantMap{{QStringLiteral("value"), tier}, {QStringLiteral("price"), price}};
    }
    return out;
}

void SettingsViewModel::setCloudLevel(const QString& level)
{
    if (!cloudWritable() || (level != QLatin1String("simple") && level != QLatin1String("advanced"))
        || m_draft.cloud.level == level)
        return;
    m_draft.cloud.level = level;
    touch();
}

void SettingsViewModel::setCloudEstimateBeforeRun(bool on)
{
    if (!cloudWritable() || m_draft.cloud.estimateBeforeRun == on) return;
    m_draft.cloud.estimateBeforeRun = on;
    touch();
}

void SettingsViewModel::setCloudFastForLongRecordings(bool on)
{
    if (!cloudWritable() || m_draft.cloud.fastForLongRecordings == on) return;
    m_draft.cloud.fastForLongRecordings = on;
    touch();
}

int SettingsViewModel::cloudLongRecordingMinutes() const
{
    return cloud::CloudSettings::kLongRecordingMinutes;
}

void SettingsViewModel::setCloudDeleteUploadedAudio(bool on)
{
    if (!cloudWritable() || m_draft.cloud.deleteUploadedAudio == on) return;
    m_draft.cloud.deleteUploadedAudio = on;
    touch();
}

void SettingsViewModel::cloudSignIn()
{
    if (cloudEditable() && m_facade) m_facade->cloudSignIn();
}

void SettingsViewModel::cloudCancelSignIn()
{
    if (m_facade) m_facade->cloudCancelSignIn();
}

void SettingsViewModel::cloudSignOut()
{
    if (cloudEditable() && m_facade) m_facade->cloudSignOut();
}

void SettingsViewModel::cloudTopUp()
{
    if (!cloudEditable() || !m_facade) return;
    if (m_cloud.signedIn && (!m_cloud.account.valid || m_cloud.account.topupAvailable))
        m_facade->cloudRequestTopup();
    else if (m_dialogs && !cloudTopUpUrl().isEmpty())
        m_dialogs->openUrl(cloudTopUpUrl());
}

void SettingsViewModel::cloudOpenSignInPage()
{
    const QString url = cloudAccount().value(QStringLiteral("signInUrl")).toString();
    if (m_dialogs && !url.isEmpty()) m_dialogs->openUrl(url);
}

// ---- design state (fictional facade) -------------------------------------------------------

void SettingsViewModel::applyScenario()
{
    const std::optional<app::SettingsScenario> sc = m_facade ? m_facade->scenario() : std::nullopt;
    if (!sc) return;
    if (sc->draft) {
        // The design state's unsaved changes: its difference from the saved settings, onto the
        // (normalized) base, as a save would apply it.
        const QJsonObject raw = m_facade->settings().toJson();
        m_draft = tanara::app::AppSettings::fromJson(mergedSettings(raw, sc->draft->toJson(), m_base.toJson()));
        for (const QString& id : kPromptIds) {
            const QString stored = storedPrompt(m_draft, id);
            if (!stored.isEmpty()) m_promptText.insert(id, stored);
        }
    }
    // The design states may still name the old pages: mapped like openPage() (no page: General).
    const PageTarget target = resolvePageTarget(sc->page.isEmpty() ? QStringLiteral("general") : sc->page,
                                                sc->section.isEmpty() ? sc->focusField : sc->section,
                                                m_page);
    m_page = target.page;
    m_section = target.section;
    m_focusField = sc->focusField;
    // The design state's processing card is open (none: all collapsed), its "Haladó" as the state says.
    m_expandedCards = m_page == QLatin1String("processing") && !m_section.isEmpty() ? QStringList{m_section} : QStringList();
    m_summaryAdvancedOpen = sc->summaryAdvancedOpen || sc->llmAdvancedOpen || target.advanced;
    m_promptIndex = std::clamp(sc->promptIndex, 0, int(kPromptIds.size()) - 1);
    m_liveActive = sc->liveCallActive;
    m_liveApp = sc->liveCallApp;
    m_liveAppId = sc->liveCallAppId;
    m_detectorAvailable = true;
    m_stt->reset();
    m_llm->reset();
    m_embedding->reset();
    m_stt->setAdvancedOpen(false);
    m_llm->setAdvancedOpen(m_summaryAdvancedOpen);
    auto show = [](SettingsProviderModel* card, const std::optional<app::SettingsScenario::TestResult>& r) {
        if (r) card->setDemoResult(r->state, r->latencyMs, r->message, r->code);
    };
    show(m_stt, sc->sttTest);
    show(m_llm, sc->llmTest);
    if (sc->llmServer) m_llm->setDemoServerInfo(*sc->llmServer);
    m_devices->refreshFromDraft();
    emit pageChanged();
    emit sectionChanged();
    emit expandedCardsChanged();
    emit summaryAdvancedOpenChanged();
    emit sectionRequested(m_section, false);
    emit voiceprintChanged();
    emit liveCallChanged();
    emit deviceCountChanged();
    m_stt->notifyFocusChanged();
    m_llm->notifyFocusChanged();
}

} // namespace tanara_qml
