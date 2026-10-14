#pragma once
//
// VoiceModelsServicesFacade: the real tanara::processing::IVoiceModelsFacade over SettingsManager
// (models folder, enabled list) and the meeting repository (source meeting titles). The voice-model
// file operations and the backfill state still live on the AppController: they come in as hooks,
// and its notifications are forwarded by AppContext (reportModelsChanged ...). One per controller,
// built by AppContext::servicesFor().
//
#include "tanara/processing/voiceid/IVoiceModelsFacade.h"

#include <QPointer>

#include <functional>

namespace tanara {
namespace app { class SettingsManager; }
namespace library { class MeetingRepository; }
}

namespace tanara_qml {

class VoiceModelsServicesFacade : public tanara::processing::IVoiceModelsFacade {
    Q_OBJECT
public:
    struct Hooks {
        std::function<bool(const QString& id, QString* error)> deleteModelFile;
        std::function<void()> rescan;
        std::function<bool()> backfillRunning;
        std::function<QVector<tanara::processing::VoiceprintBackfillSkip>()> backfillSkips;
    };
    struct Deps {
        tanara::app::SettingsManager* settings = nullptr;
        tanara::library::MeetingRepository* store = nullptr;
        Hooks hooks;
    };
    explicit VoiceModelsServicesFacade(const Deps& deps, QObject* parent = nullptr);

    // The controller's notifications, forwarded by AppContext.
    void reportModelsChanged() { emit modelsChanged(); }
    void reportBackfillStarted(int total) { emit backfillStarted(total); }
    void reportBackfillProgress(int done, int total) { emit backfillProgress(done, total); }
    void reportBackfillFinished(int added, int skipped) { emit backfillFinished(added, skipped); }

    QString modelsDir() const override;
    QStringList enabledModels() const override;
    void setEnabledModels(const QStringList& ids) override;
    void reloadModels() override;
    bool deleteModelFile(const QString& id, QString* error) override;
    void rescan() override;
    bool backfillRunning() const override;
    QVector<tanara::processing::VoiceprintBackfillSkip> backfillSkips(const QString& person = QString()) const override;
    QString meetingTitle(const QString& meetingId) const override;

private:
    Deps m_deps;
};

} // namespace tanara_qml
