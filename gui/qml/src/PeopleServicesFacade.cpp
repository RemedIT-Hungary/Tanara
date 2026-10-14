#include "PeopleServicesFacade.h"

#include "ControllerEvents.h"

#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/processing/analysis/VoiceAnalysisService.h"

using namespace tanara;
using namespace tanara::library;

namespace tanara_qml {

PeopleServicesFacade::PeopleServicesFacade(const Deps& deps, QObject* parent)
    : IPeopleFacade(parent), m_d(deps)
{
    connect(m_d.service, &PeopleService::changed, this, &IPeopleFacade::changed);
    connect(m_d.service, &PeopleService::undoChanged, this, &IPeopleFacade::undoChanged);
    if (m_d.stats) connect(m_d.stats, &PeopleStats::changed, this, &IPeopleFacade::statsChanged);
    if (m_d.events) {
        connect(m_d.events, &ControllerEvents::peopleChanged, this, &IPeopleFacade::peopleChanged);
        connect(m_d.events, &ControllerEvents::voiceprintsChanged, this, &IPeopleFacade::voiceprintsChanged);
        connect(m_d.events, &ControllerEvents::voiceModelsChanged, this, &IPeopleFacade::voiceModelsChanged);
        connect(m_d.events, &ControllerEvents::voiceprintBackfillFinished, this, &IPeopleFacade::backfillFinished);
    }
}

void PeopleServicesFacade::reload() { m_d.service->reload(); }
QVector<PersonRecord> PeopleServicesFacade::persons() const { return m_d.service->persons(); }
bool PeopleServicesFacade::statsReady() const { return m_d.stats && m_d.stats->ready(); }
PersonStats PeopleServicesFacade::stats(const PersonId& person) const
{
    return m_d.stats ? m_d.stats->stats(person) : PersonStats();
}
void PeopleServicesFacade::refreshStats()
{
    if (m_d.stats) m_d.stats->refresh();
}
QVector<VoiceSample> PeopleServicesFacade::samples(const PersonId& person) const
{
    return m_d.service->samples(person);
}

QStringList PeopleServicesFacade::voiceprintModels(const QString& sampleId) const
{
    QStringList models;
    if (!m_d.voiceprints) return models;
    for (const Voiceprint& vp : m_d.voiceprints->siblingsOf(sampleId))
        if (!models.contains(vp.model)) models << vp.model;
    return models;
}

QStringList PeopleServicesFacade::activeVoiceModelIds() const
{
    return m_d.voice ? m_d.voice->modelIds() : QStringList();
}
bool PeopleServicesFacade::backfillRunning() const { return m_d.voice && m_d.voice->backfillRunning(); }
double PeopleServicesFacade::similarity(const PersonId& a, const PersonId& b) const
{
    return m_d.service->similarity(a, b);
}
VoiceprintPlan PeopleServicesFacade::voiceprintPlan(const PersonId& person) const
{
    return m_d.service->voiceprintPlan(person);
}
MergePreview PeopleServicesFacade::mergePreview(const PersonId& loser, const PersonId& survivor) const
{
    return m_d.service->mergePreview(loser, survivor);
}
DeletePreview PeopleServicesFacade::deletePreview(const PersonId& person) const
{
    return m_d.service->deletePreview(person);
}

PeopleOpResult PeopleServicesFacade::addPerson(const QString& name) { return m_d.service->addPerson(name); }
PeopleOpResult PeopleServicesFacade::renamePerson(const PersonId& person, const QString& newName)
{
    return m_d.service->renamePerson(person, newName);
}
PeopleOpResult PeopleServicesFacade::addAlias(const PersonId& person, const QString& alias)
{
    return m_d.service->addAlias(person, alias);
}
bool PeopleServicesFacade::removeAlias(const PersonId& person, const QString& alias)
{
    return m_d.service->removeAlias(person, alias);
}
void PeopleServicesFacade::setNote(const PersonId& person, const QString& note) { m_d.service->setNote(person, note); }
bool PeopleServicesFacade::setDefaultSide(const PersonId& person, const QString& side)
{
    return m_d.service->setDefaultSide(person, side);
}
PeopleOpResult PeopleServicesFacade::removeSample(const QString& printId) { return m_d.service->removeSample(printId); }
PeopleOpResult PeopleServicesFacade::moveSample(const QString& printId, const PersonChoice& to)
{
    return m_d.service->moveSample(printId, to);
}
VoiceprintResult PeopleServicesFacade::createVoiceprintFromMeeting(const PersonId& person, const QString& meetingId)
{
    return m_d.service->createVoiceprintFromMeeting(person, meetingId);
}
PeopleOpResult PeopleServicesFacade::merge(const PersonId& loser, const PersonId& survivor)
{
    return m_d.service->merge(loser, survivor);
}
PeopleOpResult PeopleServicesFacade::removePerson(const PersonId& person, bool keepSamples)
{
    return m_d.service->removePerson(person, keepSamples);
}

bool PeopleServicesFacade::canUndo() const { return m_d.service->canUndo(); }
PeopleUndoInfo PeopleServicesFacade::undoInfo() const { return m_d.service->undoInfo(); }
PeopleUndoInfo PeopleServicesFacade::undo() { return m_d.service->undo(); }

} // namespace tanara_qml
