#pragma once
//
// SettingsServicesFacade: the real tanara::app::ISettingsFacade, composed from SettingsManager, the
// provider registry, the recording service (level metering), the recorder facade (devices), the
// voiceprint store, the tag service, the embedding preparer / index and the detector registry. What
// still lives on the AppController (the secrets, the own name and person) comes in as small hooks
// that AppContext wires; the people / voiceprint notifications are forwarded by AppContext too.
// One per controller, built by AppContext::servicesFor().
//
#include "tanara/app/ISettingsFacade.h"

#include <QPointer>

#include <functional>

namespace tanara {
namespace app { class SettingsManager; }
namespace library { class EmbeddingIndex; class EmbeddingPreparer; class TagService; class VoiceprintStore; }
namespace providers { class LlmServerProbe; }
namespace recording { class RecordingService; }
namespace watcher { class MeetingDetectorRegistry; }
}

namespace tanara_qml {

class SettingsServicesFacade : public tanara::app::ISettingsFacade {
    Q_OBJECT
public:
    struct Hooks {
        std::function<QString(const QString& key)> secret;
        std::function<bool(const QString& key)> hasSecret;
        std::function<void(const QString& key, const QString& value)> setSecret;
        std::function<QString()> secretBackend;
        std::function<QString()> secretBackendWarning;
        std::function<void(const QString& name)> setUserSpeakerName;
        std::function<tanara::PersonId()> selfPersonId;
    };
    struct Deps {
        tanara::app::SettingsManager* settings = nullptr;
        const tanara::providers::ProviderRegistry* registry = nullptr;
        tanara::recording::RecordingService* recording = nullptr;
        tanara::recording::IRecorderFacade* devices = nullptr;
        tanara::library::VoiceprintStore* voiceprints = nullptr;
        tanara::library::TagService* tags = nullptr;
        tanara::library::EmbeddingPreparer* preparer = nullptr;
        tanara::library::EmbeddingIndex* embeddings = nullptr;
        const tanara::watcher::MeetingDetectorRegistry* detectors = nullptr;
        Hooks hooks;
    };
    explicit SettingsServicesFacade(const Deps& deps, QObject* parent = nullptr);
    ~SettingsServicesFacade() override;

    // The controller's people / voiceprint notifications, forwarded by AppContext.
    void reportVoiceprintsChanged() { emit voiceprintsChanged(); }

    tanara::app::AppSettings settings() const override;
    void setSettings(const tanara::app::AppSettings& settings) override;
    void setUserSpeakerName(const QString& name) override;
    QString promptDefault(const QString& id) const override;
    QString defaultFolder(tanara::paths::Role role) const override;
    bool dataDirPinned() const override;
    QString activeUiLanguage() const override;
    tanara::app::FolderUsage folderUsage(const QString& path, bool countMeetings) const override;
    int meetingFolderCount(const QString& path) const override;
    void applyWatcherAutostart(bool on) override;

    QString secret(const QString& key) const override;
    bool hasSecret(const QString& key) const override;
    void setSecret(const QString& key, const QString& value) override;
    QString secretBackend() const override;
    QString secretBackendWarning() const override;

    tanara::recording::IRecorderFacade* devices() const override { return m_deps.devices; }
    void retainLevelMonitoring(QObject* owner) override;
    void releaseLevelMonitoring(QObject* owner) override;

    tanara::app::VoiceprintSummary ownVoiceprints() const override;

    const tanara::providers::ProviderRegistry& registry() const override { return *m_deps.registry; }
    int testConnection(const tanara::providers::ProviderDescriptor& descriptor,
                       const tanara::providers::ProviderConfig& config) override;
    void cancelConnectionTest(int id) override;
    void probeLlmServer(const tanara::providers::ProviderConfig& config) override;

    tanara::watcher::IMeetingDetector* createDetector(const QString& detectorId) const override;
    QStringList runningApps() const override;

    tanara::library::EmbeddingState embeddingState() const override;
    void startEmbedding() override;
    void cancelEmbedding() override;
    void resumeEmbedding() override;
    void reprepareEmbeddings() override;
    int rejectedSuggestionCount() const override;
    void clearRejectedSuggestions() override;

private:
    Deps m_deps;
    tanara::providers::ConnectionTester* m_tester = nullptr;
    tanara::providers::LlmServerProbe* m_probe = nullptr;
};

} // namespace tanara_qml
