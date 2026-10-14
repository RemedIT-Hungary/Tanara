#include "SettingsServicesFacade.h"

#include "tanara/app/SettingsManager.h"
#include "tanara/foundation/i18n/Localization.h"
#include "tanara/library/index/EmbeddingIndex.h"
#include "tanara/library/index/EmbeddingPreparer.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/library/tags/TagService.h"
#include "tanara/processing/summary/PromptLibrary.h"
#include "tanara/providers/chat/LlmServer.h"
#include "tanara/recording/RecordingService.h"
#include "tanara/watcher/RunningApps.h"
#include "tanara/watcher/detect/Autostart.h"
#include "tanara/watcher/detect/DetectorRegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSet>

using namespace tanara;

namespace tanara_qml {

SettingsServicesFacade::SettingsServicesFacade(const Deps& deps, QObject* parent)
    : ISettingsFacade(parent), m_deps(deps)
{
    if (m_deps.settings)
        connect(m_deps.settings, &app::SettingsManager::settingsChanged, this, &ISettingsFacade::settingsChanged);
    if (m_deps.preparer)
        connect(m_deps.preparer, &library::EmbeddingPreparer::stateChanged, this, &ISettingsFacade::embeddingStateChanged);
    if (m_deps.tags)
        connect(m_deps.tags, &library::TagService::rejectedChanged, this, &ISettingsFacade::rejectedSuggestionsChanged);
    m_tester = new providers::ConnectionTester(this);
    connect(m_tester, &providers::ConnectionTester::finished, this, &ISettingsFacade::connectionTested);
}

SettingsServicesFacade::~SettingsServicesFacade() = default;

// ---- settings ---------------------------------------------------------------------------

app::AppSettings SettingsServicesFacade::settings() const
{
    return m_deps.settings ? m_deps.settings->settings() : app::AppSettings();
}

void SettingsServicesFacade::setSettings(const app::AppSettings& settings)
{
    if (m_deps.settings) m_deps.settings->setSettings(settings);
}

void SettingsServicesFacade::setUserSpeakerName(const QString& name)
{
    if (m_deps.hooks.setUserSpeakerName) m_deps.hooks.setUserSpeakerName(name);
}

QString SettingsServicesFacade::promptDefault(const QString& id) const
{
    return processing::promptDefault(id, m_deps.settings ? m_deps.settings->paths().dataDir() : QString());
}

QString SettingsServicesFacade::defaultFolder(paths::Role role) const
{
    return paths::AppPaths::system(paths::PathSettings()).dir(role);
}

bool SettingsServicesFacade::dataDirPinned() const
{
    return !paths::homeOverride().isEmpty();
}

QString SettingsServicesFacade::activeUiLanguage() const
{
    return tanara::activeUiLanguage();
}

app::FolderUsage SettingsServicesFacade::folderUsage(const QString& path, bool countMeetings) const
{
    app::FolderUsage u;
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

int SettingsServicesFacade::meetingFolderCount(const QString& path) const
{
    return library::MeetingRepository::meetingFolderCount(path);
}

void SettingsServicesFacade::applyWatcherAutostart(bool on)
{
    watcher::autostart::applyWatcher(on, watcher::autostart::findWatcherExecutable(QCoreApplication::applicationDirPath()));
}

// ---- secrets ----------------------------------------------------------------------------

QString SettingsServicesFacade::secret(const QString& key) const
{
    return m_deps.hooks.secret ? m_deps.hooks.secret(key) : QString();
}

bool SettingsServicesFacade::hasSecret(const QString& key) const
{
    return m_deps.hooks.hasSecret && m_deps.hooks.hasSecret(key);
}

void SettingsServicesFacade::setSecret(const QString& key, const QString& value)
{
    if (m_deps.hooks.setSecret) m_deps.hooks.setSecret(key, value);
}

QString SettingsServicesFacade::secretBackend() const
{
    return m_deps.hooks.secretBackend ? m_deps.hooks.secretBackend() : QStringLiteral("keychain");
}

QString SettingsServicesFacade::secretBackendWarning() const
{
    return m_deps.hooks.secretBackendWarning ? m_deps.hooks.secretBackendWarning() : QString();
}

// ---- level meters, own voiceprints ----------------------------------------------------------

void SettingsServicesFacade::retainLevelMonitoring(QObject* owner)
{
    if (m_deps.recording) m_deps.recording->retainLevelMonitoring(owner);
}

void SettingsServicesFacade::releaseLevelMonitoring(QObject* owner)
{
    if (m_deps.recording) m_deps.recording->releaseLevelMonitoring(owner);
}

app::VoiceprintSummary SettingsServicesFacade::ownVoiceprints() const
{
    app::VoiceprintSummary out;
    if (!m_deps.voiceprints || !m_deps.hooks.selfPersonId) return out;
    // One sample is one item: per-model sibling voiceprints count as one.
    QSet<QString> meetings, samples;
    for (const Voiceprint& p : m_deps.voiceprints->printsFor(m_deps.hooks.selfPersonId())) {
        if (!p.sourceMeetingId.isEmpty()) meetings.insert(p.sourceMeetingId);
        samples.insert(library::VoiceprintStore::siblingKey(p));
    }
    out.samples = int(samples.size());
    out.meetings = int(meetings.size());
    return out;
}

// ---- providers --------------------------------------------------------------------------

int SettingsServicesFacade::testConnection(const providers::ProviderDescriptor& descriptor,
                                           const providers::ProviderConfig& config)
{
    return m_tester->test(descriptor, config);
}

void SettingsServicesFacade::cancelConnectionTest(int id)
{
    m_tester->cancel(id);
}

void SettingsServicesFacade::probeLlmServer(const providers::ProviderConfig& config)
{
    if (!m_probe) {
        m_probe = new providers::LlmServerProbe(this);
        connect(m_probe, &providers::LlmServerProbe::finished, this, &ISettingsFacade::llmServerProbed);
    }
    m_probe->probe(config);
}

// ---- call watcher -------------------------------------------------------------------------

watcher::IMeetingDetector* SettingsServicesFacade::createDetector(const QString& detectorId) const
{
    if (!m_deps.detectors) return nullptr;
    return detectorId.isEmpty() ? m_deps.detectors->createBest() : m_deps.detectors->create(detectorId);
}

QStringList SettingsServicesFacade::runningApps() const
{
    return watcher::RunningApps::names();
}

// ---- library preparation, tag suggestions ---------------------------------------------------

library::EmbeddingState SettingsServicesFacade::embeddingState() const
{
    return m_deps.preparer ? m_deps.preparer->state() : library::EmbeddingState();
}

void SettingsServicesFacade::startEmbedding()
{
    if (m_deps.preparer) m_deps.preparer->start();
}

void SettingsServicesFacade::cancelEmbedding()
{
    if (m_deps.preparer) m_deps.preparer->cancel();
}

void SettingsServicesFacade::resumeEmbedding()
{
    if (m_deps.preparer) m_deps.preparer->resume();
}

void SettingsServicesFacade::reprepareEmbeddings()
{
    if (m_deps.embeddings) m_deps.embeddings->invalidateAll();
    if (m_deps.preparer) m_deps.preparer->start();
}

int SettingsServicesFacade::rejectedSuggestionCount() const
{
    return m_deps.tags ? m_deps.tags->rejectedCount() : 0;
}

void SettingsServicesFacade::clearRejectedSuggestions()
{
    if (m_deps.tags) m_deps.tags->clearRejected();
}

} // namespace tanara_qml
