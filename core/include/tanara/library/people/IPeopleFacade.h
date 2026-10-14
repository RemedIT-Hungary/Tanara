#pragma once
//
// IPeopleFacade: what the People window's view model needs from the people side, as ONE abstract
// surface: the directory with the learned statistics, a person's voice samples, the operations
// (rename, nicknames, merge, delete, moving samples, voiceprints from meetings) with their shared
// undo, and the voice-model facts (the models in use, the voiceprint backfill).
//
// The real implementation composes library::PeopleService / PeopleStats and the voice analysis
// (gui/qml/src/PeopleServicesFacade); the fictional one (demo/PeopleDemoFacade) serves --demo,
// --gallery, --qml-shot and the tests. It is a QObject only because it carries signals.
//
// readOnly(): the fictional facade has no storage: the view model then treats the operations as
// no-ops (a screenshot never changes), exactly as it did without a controller.
#include "tanara/library/people/PeopleService.h"
#include "tanara/library/people/PeopleStats.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::library {

class IPeopleFacade : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IPeopleFacade() override = default;

    // True: fictional data, the operations do nothing.
    virtual bool readOnly() const { return false; }
    // Design-state hook of the fictional facade (screenshots): which people / samples it serves.
    virtual void applyDemoState(const QString& state) { Q_UNUSED(state); }

    // ---- reading ----
    // The fresh state on disk.
    virtual void reload() = 0;
    virtual QVector<PersonRecord> persons() const = 0;
    // The statistics are computed in the background: before the first result every statistic is empty.
    virtual bool statsReady() const = 0;
    virtual PersonStats stats(const PersonId& person) const = 0;
    virtual void refreshStats() = 0;
    // The person's samples, newest first.
    virtual QVector<VoiceSample> samples(const PersonId& person) const = 0;
    // The voice models that have a voiceprint of the sample (its sibling group).
    virtual QStringList voiceprintModels(const QString& sampleId) const = 0;
    // The voice models in use / whether the voiceprint backfill runs.
    virtual QStringList activeVoiceModelIds() const = 0;
    virtual bool backfillRunning() const = 0;
    // Voice similarity of two people (0..1); -1 if either has no voiceprint.
    virtual double similarity(const PersonId& a, const PersonId& b) const = 0;
    virtual VoiceprintPlan voiceprintPlan(const PersonId& person) const = 0;
    virtual MergePreview mergePreview(const PersonId& loser, const PersonId& survivor) const = 0;
    virtual DeletePreview deletePreview(const PersonId& person) const = 0;

    // ---- operations (PeopleService semantics) ----
    virtual PeopleOpResult addPerson(const QString& name) = 0;
    virtual PeopleOpResult renamePerson(const PersonId& person, const QString& newName) = 0;
    virtual PeopleOpResult addAlias(const PersonId& person, const QString& alias) = 0;
    virtual bool removeAlias(const PersonId& person, const QString& alias) = 0;
    virtual void setNote(const PersonId& person, const QString& note) = 0;
    virtual bool setDefaultSide(const PersonId& person, const QString& side) = 0;
    virtual PeopleOpResult removeSample(const QString& printId) = 0;
    virtual PeopleOpResult moveSample(const QString& printId, const PersonChoice& to) = 0;
    virtual VoiceprintResult createVoiceprintFromMeeting(const PersonId& person, const QString& meetingId) = 0;
    virtual PeopleOpResult merge(const PersonId& loser, const PersonId& survivor) = 0;
    virtual PeopleOpResult removePerson(const PersonId& person, bool keepSamples) = 0;

    // ---- undo ----
    virtual bool canUndo() const = 0;
    virtual PeopleUndoInfo undoInfo() const = 0;
    virtual PeopleUndoInfo undo() = 0;

signals:
    // The list or a person's data changed.
    void changed();
    void undoChanged();
    // The learned statistics changed (refreshed, or started computing).
    void statsChanged();
    // App-wide notifications that also invalidate the list.
    void peopleChanged();
    void voiceprintsChanged();
    void voiceModelsChanged();
    void backfillFinished();
};

} // namespace tanara::library
