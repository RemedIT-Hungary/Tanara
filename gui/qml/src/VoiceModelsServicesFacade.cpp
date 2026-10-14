#include "VoiceModelsServicesFacade.h"

#include "tanara/app/SettingsManager.h"
#include "tanara/library/store/MeetingRepository.h"

using namespace tanara;

namespace tanara_qml {

VoiceModelsServicesFacade::VoiceModelsServicesFacade(const Deps& deps, QObject* parent)
    : IVoiceModelsFacade(parent), m_deps(deps)
{
    if (m_deps.settings)
        connect(m_deps.settings, &app::SettingsManager::settingsChanged, this, &IVoiceModelsFacade::settingsChanged);
}

QString VoiceModelsServicesFacade::modelsDir() const
{
    return m_deps.settings ? m_deps.settings->paths().modelsDir() : QString();
}

QStringList VoiceModelsServicesFacade::enabledModels() const
{
    return m_deps.settings ? m_deps.settings->enabledVoiceModels() : QStringList();
}

void VoiceModelsServicesFacade::setEnabledModels(const QStringList& ids)
{
    if (m_deps.settings) m_deps.settings->setEnabledVoiceModels(ids);
}

void VoiceModelsServicesFacade::reloadModels()
{
    // The core re-reads the model files only on a setting change: writing the same value back
    // recomputes the models in use (and backfills).
    if (m_deps.settings) m_deps.settings->setSettings(m_deps.settings->settings());
}

bool VoiceModelsServicesFacade::deleteModelFile(const QString& id, QString* error)
{
    return m_deps.hooks.deleteModelFile && m_deps.hooks.deleteModelFile(id, error);
}

void VoiceModelsServicesFacade::rescan()
{
    if (m_deps.hooks.rescan) m_deps.hooks.rescan();
}

bool VoiceModelsServicesFacade::backfillRunning() const
{
    return m_deps.hooks.backfillRunning && m_deps.hooks.backfillRunning();
}

QVector<processing::VoiceprintBackfillSkip> VoiceModelsServicesFacade::backfillSkips(const QString&) const
{
    return m_deps.hooks.backfillSkips ? m_deps.hooks.backfillSkips() : QVector<processing::VoiceprintBackfillSkip>();
}

QString VoiceModelsServicesFacade::meetingTitle(const QString& meetingId) const
{
    return m_deps.store ? m_deps.store->load(meetingId).title : QString();
}

} // namespace tanara_qml
