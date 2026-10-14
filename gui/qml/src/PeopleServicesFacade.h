#pragma once
//
// PeopleServicesFacade: the real tanara::library::IPeopleFacade, composed from the people service,
// the learned statistics, the voiceprint store and the voice analysis (no AppController). One per
// controller, built by AppContext::servicesFor().
//
#include "tanara/library/people/IPeopleFacade.h"

#include <QPointer>

namespace tanara {
namespace library { class VoiceprintStore; }
namespace processing { class VoiceAnalysisService; }
}

namespace tanara_qml {

class ControllerEvents;

class PeopleServicesFacade : public tanara::library::IPeopleFacade {
    Q_OBJECT
public:
    struct Deps {
        tanara::library::PeopleService* service = nullptr;
        tanara::library::PeopleStats* stats = nullptr;
        tanara::library::VoiceprintStore* voiceprints = nullptr;
        tanara::processing::VoiceAnalysisService* voice = nullptr;
        ControllerEvents* events = nullptr;
    };
    explicit PeopleServicesFacade(const Deps& deps, QObject* parent = nullptr);

    void reload() override;
    QVector<tanara::library::PersonRecord> persons() const override;
    bool statsReady() const override;
    tanara::library::PersonStats stats(const tanara::PersonId& person) const override;
    void refreshStats() override;
    QVector<tanara::library::VoiceSample> samples(const tanara::PersonId& person) const override;
    QStringList voiceprintModels(const QString& sampleId) const override;
    QStringList activeVoiceModelIds() const override;
    bool backfillRunning() const override;
    double similarity(const tanara::PersonId& a, const tanara::PersonId& b) const override;
    tanara::library::VoiceprintPlan voiceprintPlan(const tanara::PersonId& person) const override;
    tanara::library::MergePreview mergePreview(const tanara::PersonId& loser,
                                               const tanara::PersonId& survivor) const override;
    tanara::library::DeletePreview deletePreview(const tanara::PersonId& person) const override;

    tanara::library::PeopleOpResult addPerson(const QString& name) override;
    tanara::library::PeopleOpResult renamePerson(const tanara::PersonId& person, const QString& newName) override;
    tanara::library::PeopleOpResult addAlias(const tanara::PersonId& person, const QString& alias) override;
    bool removeAlias(const tanara::PersonId& person, const QString& alias) override;
    void setNote(const tanara::PersonId& person, const QString& note) override;
    bool setDefaultSide(const tanara::PersonId& person, const QString& side) override;
    tanara::library::PeopleOpResult removeSample(const QString& printId) override;
    tanara::library::PeopleOpResult moveSample(const QString& printId, const tanara::PersonChoice& to) override;
    tanara::VoiceprintResult createVoiceprintFromMeeting(const tanara::PersonId& person,
                                                         const QString& meetingId) override;
    tanara::library::PeopleOpResult merge(const tanara::PersonId& loser, const tanara::PersonId& survivor) override;
    tanara::library::PeopleOpResult removePerson(const tanara::PersonId& person, bool keepSamples) override;

    bool canUndo() const override;
    tanara::library::PeopleUndoInfo undoInfo() const override;
    tanara::library::PeopleUndoInfo undo() override;

private:
    Deps m_d;
};

} // namespace tanara_qml
