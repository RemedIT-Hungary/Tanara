#pragma once
//
// PeopleDemoFacade: the fictional implementation of tanara::library::IPeopleFacade for --demo,
// --gallery, --qml-shot and the tests: ~14 made-up people (a few more in the tag states) with
// voice samples and meetings, the learned statistics already "ready". It is read-only
// (readOnly() == true): the operations do nothing, except the manual track side, which changes in
// memory so the segmented control can be tried.
//
// applyDemoState() picks the content: P06 = only the own person; the tags* states add the four
// people of the tag UX plan; samplesModels / backfill show the voice-model labels.
#include "tanara/library/people/IPeopleFacade.h"

#include <QHash>

namespace tanara_demo {

class PeopleDemoFacade : public tanara::library::IPeopleFacade {
    Q_OBJECT
public:
    explicit PeopleDemoFacade(QObject* parent = nullptr);

    bool readOnly() const override { return true; }
    void applyDemoState(const QString& state) override;

    void reload() override {}
    QVector<tanara::library::PersonRecord> persons() const override;
    bool statsReady() const override { return true; }
    tanara::library::PersonStats stats(const tanara::PersonId& person) const override;
    void refreshStats() override {}
    QVector<tanara::library::VoiceSample> samples(const tanara::PersonId& person) const override;
    QStringList voiceprintModels(const QString& sampleId) const override;
    QStringList activeVoiceModelIds() const override;
    bool backfillRunning() const override { return m_state == QLatin1String("backfill"); }
    double similarity(const tanara::PersonId& a, const tanara::PersonId& b) const override;
    tanara::library::VoiceprintPlan voiceprintPlan(const tanara::PersonId& person) const override;
    tanara::library::MergePreview mergePreview(const tanara::PersonId& loser,
                                               const tanara::PersonId& survivor) const override;
    tanara::library::DeletePreview deletePreview(const tanara::PersonId& person) const override;

    tanara::library::PeopleOpResult addPerson(const QString&) override { return {}; }
    tanara::library::PeopleOpResult renamePerson(const tanara::PersonId&, const QString&) override { return {}; }
    tanara::library::PeopleOpResult addAlias(const tanara::PersonId&, const QString&) override { return {}; }
    bool removeAlias(const tanara::PersonId&, const QString&) override { return false; }
    void setNote(const tanara::PersonId&, const QString&) override {}
    bool setDefaultSide(const tanara::PersonId& person, const QString& side) override;
    tanara::library::PeopleOpResult removeSample(const QString&) override { return {}; }
    tanara::library::PeopleOpResult moveSample(const QString&, const tanara::PersonChoice&) override { return {}; }
    tanara::VoiceprintResult createVoiceprintFromMeeting(const tanara::PersonId&, const QString&) override { return {}; }
    tanara::library::PeopleOpResult merge(const tanara::PersonId&, const tanara::PersonId&) override { return {}; }
    tanara::library::PeopleOpResult removePerson(const tanara::PersonId&, bool) override { return {}; }

    bool canUndo() const override { return false; }
    tanara::library::PeopleUndoInfo undoInfo() const override { return {}; }
    tanara::library::PeopleUndoInfo undo() override { return {}; }

private:
    struct Entry {
        tanara::library::PersonRecord record;
        tanara::library::PersonStats stats;
        QVector<tanara::library::VoiceSample> samples;
    };
    void load();
    const Entry* find(const tanara::PersonId& id) const;

    QString m_state;
    QVector<Entry> m_entries;
};

} // namespace tanara_demo
