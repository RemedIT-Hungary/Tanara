#include "demo/SettingsDemoFacade.h"

#include "demo/DemoText.h"
#include "demo/RecorderDemoFacade.h"

#include "tanara/cloud/CloudTypes.h"
#include "tanara/processing/summary/PromptLibrary.h"
#include "tanara/providers/embedding/ITextEmbedder.h"

#include <QDate>
#include <QTimer>

using namespace tanara;

namespace tanara_demo {

namespace {

constexpr int kSecondsPerMeeting = 9;   // the fictional preparation's speed

// The made-up user's default folders (also the configured ones).
QString audioFolder() { return demoText("/home/lilla/Tanara/felvetelek", "/home/emma/Tanara/recordings"); }
QString notesFolder() { return demoText("/home/lilla/Tanara/jegyzetek", "/home/emma/Tanara/notes"); }
QString dataFolder() { return demoText("/home/lilla/.tanara", "/home/emma/.tanara"); }
QString modelsFolder() { return demoText("/home/lilla/.tanara/models", "/home/emma/.tanara/models"); }

} // namespace

SettingsDemoFacade::SettingsDemoFacade(const QString& designState, Fixture fixture, QObject* parent)
    : ISettingsFacade(parent), m_fixture(fixture), m_registry(std::make_unique<providers::ProviderRegistry>())
{
    providers::registerBuiltinProviders(*m_registry);
    if (fixture == Fixture::Onboarding) {
        loadOnboarding(designState);
    } else {
        m_devices = new RecorderDemoFacade(QString(), RecorderDemoFacade::Fixture::SettingsList, this);
        loadSettings(designState.isEmpty() ? QStringLiteral("B01") : designState);
    }
}

SettingsDemoFacade::~SettingsDemoFacade() = default;

// ---- the Settings window's design states ------------------------------------------------------

void SettingsDemoFacade::loadSettings(const QString& st)
{
    app::AppSettings s;
    s.recording.userSpeakerName = demoName("Kovács Lilla");
    s.paths.setPrimaryRecordingRoot(audioFolder());
    s.paths.recordingRoots << paths::RecordingRoot{demoText("/media/lilla/archivum/Tanara", "/media/emma/archive/Tanara"), false};
    s.paths.notesDir = notesFolder();
    s.paths.dataDir = dataFolder();
    s.paths.modelsDir = modelsFolder();
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
    // The default sources (shared with the recorder).
    m_devices->setLastUsedDeviceNames({trust, headsetMon, kantoMon});

    const bool keyMissing = st == QLatin1String("B04");
    m_secrets = {{QStringLiteral("soniox.apiKey"), keyMissing ? QString() : QStringLiteral("sk-demo-0000000000003f9a")},
                 {QStringLiteral("llm.apiKey"), QString()},
                 {QStringLiteral("stt.whisper.apiKey"), QString()}};

    app::SettingsScenario sc;
    std::optional<app::AppSettings> draft;
    if (st == QLatin1String("B02") || st == QLatin1String("rename")) {
        sc.page = QStringLiteral("recording");
        draft = s;
        draft->recording.deviceNames.insert(kantoMon, demoText("Kanto hangfal", "Kanto speakers"));   // 1 unsaved change
    } else if (st == QLatin1String("B03")) {
        sc.page = QStringLiteral("watcher");
        sc.liveCallActive = true;
        sc.liveCallApp = QStringLiteral("Microsoft Teams");
        sc.liveCallAppId = QStringLiteral("teams");
    } else if (st == QLatin1String("B04")) {
        sc.page = QStringLiteral("services");
        sc.focusField = QStringLiteral("stt");
        sc.llmTest = app::SettingsScenario::TestResult{QStringLiteral("ok"), 48, {}, {}};
    } else if (st.startsWith(QLatin1String("B04"))) {
        // Transcription and summary are already set up -> collapsed cards.
        sc.page = QStringLiteral("services");
        sc.sttTest = app::SettingsScenario::TestResult{QStringLiteral("ok"), 210, {}, {}};
        sc.llmTest = app::SettingsScenario::TestResult{QStringLiteral("ok"), 48, {}, {}};
    } else if (st == QLatin1String("advanced")) {
        // The LLM card's "Advanced" part is open (temperature, max tokens, thinking).
        sc.page = QStringLiteral("services");
        sc.llmTest = app::SettingsScenario::TestResult{QStringLiteral("ok"), 48, {}, {}};
        sc.llmAdvancedOpen = true;
        providers::llmctx::LlmServerInfo info;   // LM Studio: the model is loaded, on one thread
        info.kind = providers::llmctx::LlmServerInfo::Kind::LmStudio;
        info.modelListed = true;
        info.modelKey = QStringLiteral("google/gemma-4-12b-qat");
        info.maxContext = 131072;
        info.instances = {{info.modelKey, 20480, 1}};
        sc.llmServer = info;
    } else if (st == QLatin1String("B05")) {
        sc.page = QStringLiteral("services");
        sc.sttTest = app::SettingsScenario::TestResult{QStringLiteral("ok"), 210, {}, {}};
        sc.llmTest = app::SettingsScenario::TestResult{
            QStringLiteral("failed"), -1,
            tr("A végpont nem válaszol. Fut a helyi szerver (pl. LM Studio), és jó a port?"),
            QStringLiteral("ECONNREFUSED")};
        sc.llmAdvancedOpen = true;
    } else if (st == QLatin1String("cloudComingSoon")) {
        sc.page = QStringLiteral("services");
        sc.serviceMode = QStringLiteral("cloud");
    } else if (st == QLatin1String("cloudSelected")) {
        // The saved setting names Tanara Cloud (developer setting): not available, own key needed.
        sc.page = QStringLiteral("services");
        draft = s;
        draft->providers.stt.selectedId = cloud::ProviderId;
        draft->providers.llm.selectedId = cloud::ProviderId;
    } else if (st == QLatin1String("B07") || st == QLatin1String("B07notes") || st == QLatin1String("schema")) {
        sc.page = QStringLiteral("summary");
        // A prompt extended with an own rule -> "modified".
        s.processing.summaryPrompt = processing::promptBuiltin(QStringLiteral("single"))
            + QStringLiteral("\nFor each action item, name the owner and the deadline when they were said.\n");
        if (st == QLatin1String("B07notes"))
            sc.promptIndex = 1;      // the per-part note prompt
    } else if (st.startsWith(QLatin1String("B08"))) {
        sc.page = QStringLiteral("speakers");
        // The enabled list follows the voice-model list's state.
        s.processing.voiceModels = QStringList{QStringLiteral("wespeaker-resnet34-lm")};
        // D5: the local speaker separation switched off.
        if (st == QLatin1String("B08diaroff")) s.processing.localDiarization = false;
    } else if (st == QLatin1String("dirty") || st == QLatin1String("unsaved")) {
        draft = s;
        draft->recording.userSpeakerName = demoText("Kovács Lilla Anna", "Emma Anne Carter");
    }

    // The Embedding card's states; other pages have no embedding.
    const QString emb = st == QLatin1String("B04") || st == QLatin1String("B04embedding") ? QStringLiteral("local")
        : st == QLatin1String("B04embeddingRunning") ? QStringLiteral("localRunning")
        : st == QLatin1String("B04embeddingError") ? QStringLiteral("localError")
        : st == QLatin1String("B04embeddingDone") ? QStringLiteral("localDone")
        : st == QLatin1String("B04embeddingCloud") ? QStringLiteral("cloud")
        : st == QLatin1String("B04modelChange") ? QStringLiteral("modelChange")
        : st == QLatin1String("B04embeddingNone") ? QStringLiteral("none") : QString();
    const bool local = emb == QLatin1String("local") || emb == QLatin1String("localRunning")
                    || emb == QLatin1String("localError") || emb == QLatin1String("localDone")
                    || emb == QLatin1String("modelChange");
    const bool cloudEmb = emb == QLatin1String("cloud");
    s.providers.embedding.configs.clear();
    s.providers.embedding.selectedId = local ? providers::embeddingproviders::LocalId
                                     : cloudEmb ? providers::embeddingproviders::CloudId : QString();
    providers::ProviderConfig cfg;
    cfg.type = providers::embeddingproviders::LocalId;
    cfg.baseUrl = QStringLiteral("http://localhost:1234/v1");
    cfg.model = QStringLiteral("text-embedding-nomic-embed-text-v1.5");
    s.providers.embedding.configs.insert(providers::embeddingproviders::LocalId, cfg);
    if (cloudEmb) {
        providers::ProviderConfig c;
        c.type = providers::embeddingproviders::CloudId;
        s.providers.embedding.configs.insert(providers::embeddingproviders::CloudId, c);
    }
    s.processing.tagSuggestions = true;
    s.processing.llmTagSuggestions = true;
    if (draft) {   // the draft differs only where the state says
        draft->providers.embedding = s.providers.embedding;
        draft->processing.tagSuggestions = draft->processing.llmTagSuggestions = true;
        draft->processing.summaryPrompt = s.processing.summaryPrompt;
        draft->processing.voiceModels = s.processing.voiceModels;
    }
    // The model was edited, not saved yet.
    if (emb == QLatin1String("modelChange")) {
        draft = s;
        draft->providers.embedding.configs[providers::embeddingproviders::LocalId].model = QStringLiteral("nomic-embed-text-v2-moe");
    }
    sc.draft = draft;
    m_settings = s;
    m_scenario = sc;
    loadEmbedding(emb);
}

void SettingsDemoFacade::loadEmbedding(const QString& state)
{
    const bool local = state == QLatin1String("local") || state == QLatin1String("localRunning")
                    || state == QLatin1String("localError") || state == QLatin1String("localDone")
                    || state == QLatin1String("modelChange");
    const bool cloudEmb = state == QLatin1String("cloud");
    m_embedding = {};
    m_embedding.provider = local ? providers::embeddingproviders::LocalId
                         : cloudEmb ? providers::embeddingproviders::CloudId : QString();
    m_embedding.total = 40;
    if (state == QLatin1String("localRunning")) {
        m_embedding.status = library::EmbeddingState::Running;
        m_embedding.prepared = 23;
        m_embedding.etaSec = 230;
    } else if (state == QLatin1String("localError") || state == QLatin1String("modelChange")) {
        m_embedding.status = library::EmbeddingState::Error;
        m_embedding.prepared = 31;
        m_embedding.error = tr("a végpont nem válaszol. Fut a helyi szerver?");
    } else if (state == QLatin1String("localDone") || cloudEmb) {
        m_embedding.status = library::EmbeddingState::Done;
        m_embedding.prepared = 40;
        m_embedding.lastRun = QDateTime(QDate(2026, 10, 5), QTime(9, 12));
    } else if (local) {
        m_embedding.prepared = 12;                  // interrupted preparation: "Prepare" button
    }
    m_rejected = 12;
}

// ---- the "Getting started" window's steps --------------------------------------------------------

void SettingsDemoFacade::loadOnboarding(const QString& state)
{
    // A fresh install's typical state: no transcription key yet, the local LLM entered.
    app::AppSettings s;
    s.recording.userSpeakerName = demoName("Kovács Lilla");
    s.paths.setPrimaryRecordingRoot(audioFolder());
    s.paths.notesDir = demoText("/home/lilla/Obsidian/Megbeszélések", "/home/emma/Obsidian/Meetings");
    s.paths.dataDir = dataFolder();
    s.ui.language = QStringLiteral("auto");
    s.watcher.autostart = true;
    s.providers.stt.selectedId = QStringLiteral("soniox");
    s.providers.llm.selectedId = QStringLiteral("openai-compat");
    providers::ProviderConfig llm;
    llm.type = s.providers.llm.selectedId;
    llm.baseUrl = QStringLiteral("http://localhost:1234/v1");
    llm.model = QStringLiteral("google/gemma-4-12b");
    s.providers.llm.configs.insert(s.providers.llm.selectedId, llm);
    m_settings = s;

    static const QStringList steps{QStringLiteral("welcome"), QStringLiteral("you"), QStringLiteral("folders"),
                                   QStringLiteral("providers"), QStringLiteral("watcher"), QStringLiteral("done")};
    app::SettingsScenario sc;
    sc.onboardingStep = steps.contains(state) ? state : steps.first();
    // The earlier steps: one skipped (Folders), the rest accepted, so the navigation shows both marks.
    for (const QString& k : steps) {
        if (k == sc.onboardingStep) break;
        sc.onboardingStatus.insert(k, k == QLatin1String("folders") ? QStringLiteral("skipped") : QStringLiteral("done"));
    }
    m_scenario = sc;
}

// ---- ISettingsFacade ------------------------------------------------------------------------------

void SettingsDemoFacade::setSettings(const app::AppSettings& settings)
{
    m_settings = settings;
    emit settingsChanged();
}

void SettingsDemoFacade::setUserSpeakerName(const QString& name)
{
    m_settings.recording.userSpeakerName = name;
    emit settingsChanged();
}

QString SettingsDemoFacade::promptDefault(const QString& id) const
{
    return processing::promptBuiltin(id);
}

QString SettingsDemoFacade::defaultFolder(paths::Role role) const
{
    switch (role) {
    case paths::Role::Recordings: return audioFolder();
    case paths::Role::Notes:      return notesFolder();
    case paths::Role::Models:     return modelsFolder();
    default:                      return dataFolder();
    }
}

app::FolderUsage SettingsDemoFacade::folderUsage(const QString& path, bool) const
{
    app::FolderUsage u;
    u.exists = true;
    if (path == m_settings.paths.primaryRecordingRoot()) {
        u.bytes = 12'400'000'000;
        u.meetings = 86;
    } else if (path == m_settings.paths.notesDir) {
        u.bytes = 38'000'000;
    } else if (path == m_settings.paths.modelsDir) {
        u.bytes = 96'000'000;
    } else if (path == m_settings.paths.dataDir) {
        u.bytes = 210'000'000;
    } else {
        u.exists = false;
    }
    return u;
}

int SettingsDemoFacade::testConnection(const providers::ProviderDescriptor& descriptor,
                                       const providers::ProviderConfig&)
{
    // No network: a fictional but realistic result in the next turn.
    const int id = m_nextTest++;
    providers::ConnectionTestResult r;
    r.status = providers::ConnectionTestResult::Status::Ok;
    r.latencyMs = descriptor.capability == providers::CapabilityKind::Transcription ? 210 : 48;
    r.models = descriptor.capability == providers::CapabilityKind::TextEmbedding
        ? QStringList{QStringLiteral("text-embedding-nomic-embed-text-v1.5"),
                      QStringLiteral("nomic-embed-text-v2-moe"), QStringLiteral("text-embedding-bge-m3")}
        : QStringList{QStringLiteral("google/gemma-4-12b-qat"), QStringLiteral("qwen/qwen3-coder-30b")};
    QTimer::singleShot(0, this, [this, id, r] {
        if (m_cancelled.removeAll(id) == 0) emit connectionTested(id, r);
    });
    return id;
}

QStringList SettingsDemoFacade::runningApps() const
{
    return {QStringLiteral("signal-desktop"), QStringLiteral("telegram-desktop"),
            QStringLiteral("thunderbird"), QStringLiteral("zoom")};
}

void SettingsDemoFacade::startEmbedding()
{
    m_embedding.status = library::EmbeddingState::Running;
    if (m_embedding.prepared >= m_embedding.total) m_embedding.prepared = 0;
    m_embedding.etaSec = (m_embedding.total - m_embedding.prepared) * kSecondsPerMeeting;
    emit embeddingStateChanged();
}

void SettingsDemoFacade::cancelEmbedding()
{
    m_embedding.status = library::EmbeddingState::Idle;
    m_embedding.etaSec = -1;
    emit embeddingStateChanged();
}

void SettingsDemoFacade::resumeEmbedding()
{
    m_embedding.status = library::EmbeddingState::Running;
    m_embedding.error.clear();
    m_embedding.etaSec = (m_embedding.total - m_embedding.prepared) * kSecondsPerMeeting;
    emit embeddingStateChanged();
}

void SettingsDemoFacade::reprepareEmbeddings()
{
    m_embedding.prepared = 0;
    startEmbedding();
}

void SettingsDemoFacade::clearRejectedSuggestions()
{
    if (m_rejected == 0) return;
    m_rejected = 0;
    emit rejectedSuggestionsChanged();
}

} // namespace tanara_demo
