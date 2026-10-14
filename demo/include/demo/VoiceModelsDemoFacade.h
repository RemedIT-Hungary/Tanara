#pragma once
//
// VoiceModelsDemoFacade: the fictional implementation of tanara::processing::IVoiceModelsFacade for
// --demo, --gallery, --qml-shot and the tests. No file is read, downloaded or deleted; the design
// state (scenario()) says how the voice models and the voiceprint backfill look:
//   B08 (default) | B08download | B08none | B08error | B08offline | B08bundled | B08fallback |
//   B08verify | B08backfill | B08partial (three skipped samples) | samplesModels (the People
//   window's note: one skipped sample of the asked person).
// Enabling changes only the in-memory list.
#include "tanara/processing/voiceid/IVoiceModelsFacade.h"

namespace tanara_demo {

class VoiceModelsDemoFacade : public tanara::processing::IVoiceModelsFacade {
    Q_OBJECT
public:
    explicit VoiceModelsDemoFacade(const QString& designState, QObject* parent = nullptr);

    bool live() const override { return false; }
    std::optional<tanara::processing::VoiceModelScenario> scenario() const override { return m_scenario; }

    QString modelsDir() const override { return QString(); }
    QStringList enabledModels() const override { return m_enabled; }
    void setEnabledModels(const QStringList& ids) override;
    void reloadModels() override {}
    bool deleteModelFile(const QString&, QString*) override { return true; }
    void rescan() override {}

    bool backfillRunning() const override { return m_scenario.backfillState == QLatin1String("running"); }
    QVector<tanara::processing::VoiceprintBackfillSkip> backfillSkips(const QString& person = QString()) const override;
    QString meetingTitle(const QString& meetingId) const override;

private:
    QString m_state;
    tanara::processing::VoiceModelScenario m_scenario;
    QStringList m_enabled;
};

} // namespace tanara_demo
