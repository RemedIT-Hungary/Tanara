#pragma once
//
// SettingsDemoFacade: the fictional implementation of tanara::app::ISettingsFacade for --demo,
// --gallery, --qml-shot and the tests. Everything lives in memory: saving changes only this
// object, the connection test answers without the network, nothing reaches the keychain, the
// disk or the system.
//
// Fixture::Settings serves the Settings window's design states ("B01" ... "B08*", "advanced",
// "dirty", "cloudSelected", ...): the made-up user's settings and keys, the five made-up devices,
// the folder sizes, the library preparation's state, and what the window shows on top
// (scenario()). Fixture::Onboarding serves the "Getting started" window's steps ("welcome" ...
// "done"): a fresh install's settings with no transcription key yet.
#include "tanara/app/ISettingsFacade.h"

#include <memory>

namespace tanara::recording { class IRecorderFacade; }

namespace tanara_demo {

class SettingsDemoFacade : public tanara::app::ISettingsFacade {
    Q_OBJECT
public:
    enum class Fixture { Settings, Onboarding };

    explicit SettingsDemoFacade(const QString& designState, Fixture fixture = Fixture::Settings,
                                QObject* parent = nullptr);
    ~SettingsDemoFacade() override;

    bool live() const override { return false; }
    std::optional<tanara::app::SettingsScenario> scenario() const override { return m_scenario; }

    tanara::app::AppSettings settings() const override { return m_settings; }
    void setSettings(const tanara::app::AppSettings& settings) override;
    void setUserSpeakerName(const QString& name) override;
    QString promptDefault(const QString& id) const override;
    QString defaultFolder(tanara::paths::Role role) const override;
    bool dataDirPinned() const override { return false; }
    QString activeUiLanguage() const override { return QString(); }
    tanara::app::FolderUsage folderUsage(const QString& path, bool countMeetings) const override;
    int meetingFolderCount(const QString&) const override { return 0; }
    void applyWatcherAutostart(bool) override {}

    QString secret(const QString& key) const override { return m_secrets.value(key); }
    bool hasSecret(const QString& key) const override { return !m_secrets.value(key).isEmpty(); }
    void setSecret(const QString& key, const QString& value) override { m_secrets.insert(key, value); }
    QString secretBackend() const override { return QStringLiteral("keychain"); }
    QString secretBackendWarning() const override { return QString(); }

    tanara::recording::IRecorderFacade* devices() const override { return m_devices; }
    void retainLevelMonitoring(QObject*) override {}
    void releaseLevelMonitoring(QObject*) override {}

    tanara::app::VoiceprintSummary ownVoiceprints() const override { return {23, 9}; }

    const tanara::providers::ProviderRegistry& registry() const override { return *m_registry; }
    int testConnection(const tanara::providers::ProviderDescriptor& descriptor,
                       const tanara::providers::ProviderConfig& config) override;
    void cancelConnectionTest(int id) override { m_cancelled << id; }
    void probeLlmServer(const tanara::providers::ProviderConfig&) override {}

    tanara::watcher::IMeetingDetector* createDetector(const QString&) const override { return nullptr; }
    QStringList runningApps() const override;

    tanara::library::EmbeddingState embeddingState() const override { return m_embedding; }
    void startEmbedding() override;
    void cancelEmbedding() override;
    void resumeEmbedding() override;
    void reprepareEmbeddings() override;
    int rejectedSuggestionCount() const override { return m_rejected; }
    void clearRejectedSuggestions() override;

private:
    void loadSettings(const QString& state);
    void loadOnboarding(const QString& state);
    void loadEmbedding(const QString& state);

    Fixture m_fixture;
    tanara::app::AppSettings m_settings;
    QHash<QString, QString> m_secrets;
    std::optional<tanara::app::SettingsScenario> m_scenario;
    std::unique_ptr<tanara::providers::ProviderRegistry> m_registry;
    tanara::recording::IRecorderFacade* m_devices = nullptr;
    tanara::library::EmbeddingState m_embedding;
    int m_rejected = 0;
    int m_nextTest = 1;
    QList<int> m_cancelled;
};

} // namespace tanara_demo
