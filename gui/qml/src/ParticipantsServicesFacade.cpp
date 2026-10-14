#include "ParticipantsServicesFacade.h"

#include "ControllerEvents.h"
#include "ParticipantSample.h"

#include "tanara/app/SettingsManager.h"
#include "tanara/editing/participants/ParticipantService.h"
#include "tanara/editing/session/PeopleDirectory.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/library/people/PeopleService.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/tags/TagService.h"
#include "tanara/library/tracks/TrackCatalog.h"
#include "tanara/processing/analysis/ParticipantAnalysis.h"
#include "tanara/processing/analysis/VoiceAnalysisService.h"

using namespace tanara;
using namespace tanara::editing;

namespace tanara_qml {

namespace {

QString iconOf(const library::TrackView& v)
{
    if (v.role == library::TrackRole::OwnMic || v.track.kind == TrackKind::Mic) return QStringLiteral("mic");
    if (v.role == library::TrackRole::CallAudio || v.track.kind == TrackKind::Loopback) return QStringLiteral("phone");
    return QStringLiteral("audio-lines");
}

} // namespace

ParticipantsServicesFacade::ParticipantsServicesFacade(const Deps& deps, QObject* parent)
    : IParticipantsFacade(parent), m_d(deps)
{
    if (!m_d.events) return;
    connect(m_d.events, &ControllerEvents::participantsChanged, this, &IParticipantsFacade::participantsChanged);
    connect(m_d.events, &ControllerEvents::participantAnalysisStarted, this,
            &IParticipantsFacade::participantAnalysisStarted);
    connect(m_d.events, &ControllerEvents::participantAnalysisFinished, this,
            &IParticipantsFacade::participantAnalysisFinished);
    connect(m_d.events, &ControllerEvents::tracksChanged, this, &IParticipantsFacade::tracksChanged);
    connect(m_d.events, &ControllerEvents::speakerMapChanged, this, &IParticipantsFacade::speakerMapChanged);
}

ParticipantsState ParticipantsServicesFacade::state(const QString& meetingId) const
{
    ParticipantsState st;
    const Meeting m = !meetingId.isEmpty() && m_d.store ? m_d.store->load(meetingId) : Meeting();
    if (m.id.isEmpty()) return st;
    st.found = true;
    st.selfName = m_d.settings ? m_d.settings->settings().recording.userSpeakerName.trimmed() : QString();
    PersonNames names;
    if (m_d.people) {
        m_d.people->refresh();
        names = m_d.people->names();
    }
    st.running = m_d.voice && m_d.voice->analysisRunning(m.id);
    st.approvalPending = m_d.participants && m_d.participants->participantApprovalPending(m.id);
    st.hasTranscript = m.hasTranscript;
    st.tagIds = m.tagIds;

    const processing::ParticipantAnalysisFile file = processing::ParticipantAnalysisFile::load(m.folder);
    if (!file.isEmpty()) {
        st.analysisModelIds = file.modelIds;
        st.analysisAt = QDateTime::fromString(file.at, Qt::ISODate);
    }
    QVector<TranscriptLine> lines;
    if (st.hasTranscript) {
        lines = editing::speakeredit::loadTranscriptLines(m.folder);
        for (const TranscriptLine& l : std::as_const(lines))
            if (!l.rawLabel.isEmpty() && !st.rawLabels.contains(l.rawLabel)) st.rawLabels << l.rawLabel;
    }

    // If there was a (non-skipped) decision -> that check; otherwise the default.
    const bool decided = m.approval && !m.approval->skipped && !m.approval->solo;
    for (const Participant& p : m.participants) {
        ParticipantCandidate c;
        c.id = p.id;
        c.personId = p.personId;
        c.name = personName(names, p.personId);
        c.group = processing::participants::participantGroup(p);
        // An invited but not heard candidate is unchecked by default: there is nobody to bind the name to.
        c.checked = decided ? p.approved
                            : (c.group != ParticipantGroup::InvitedNotHeard && processing::participants::defaultChecked(p));
        c.manual = p.source == ParticipantSource::Manual;
        c.evidence = p.evidence;
        c.rawIds = p.rawSpeakerIds;
        c.sides = p.sides;
        // The listenable sample: before the transcript the voice cluster's, after it the longest line
        // of the bound raw speakers (on the track of the participant's cluster).
        if (!c.manual) {
            if (!st.hasTranscript) {
                c.sample = tanara_qml::participantsample::clusterSample(file.clusters, p.id);
            } else {
                const TranscriptLine* longest = nullptr;
                for (const TranscriptLine& l : std::as_const(lines)) {
                    bool mine = false;
                    for (const QString& raw : p.rawSpeakerIds)
                        mine = mine || processing::participants::rawLabelOf(raw) == l.rawLabel;
                    if (mine && (!longest || l.endMs - l.startMs > longest->endMs - longest->startMs)) longest = &l;
                }
                if (longest)
                    c.sample = tanara_qml::participantsample::lineSample(file.clusters, p.id, longest->startMs, longest->endMs);
            }
        }
        st.candidates.append(c);
    }
    return st;
}

QVector<PersonInfo> ParticipantsServicesFacade::peopleDirectory() const
{
    if (m_d.people) m_d.people->refresh();
    return editing::listPeople(m_d.people, m_d.voiceprints, m_d.store);
}

TagPeopleHint ParticipantsServicesFacade::suggestPeople(const QStringList& tagIds, const QVector<PersonId>& presentIds,
                                                        const QStringList& presentNames, int limit) const
{
    if (!m_d.tags) return {};
    // The people already in the dialog (a typed name: the person with that name) and the own person.
    PersonIds present = presentIds;
    if (m_d.people)
        for (const QString& name : presentNames) present << m_d.people->idForName(name);
    if (m_d.peopleService) present << m_d.peopleService->selfPerson().id;
    // The first tag that has a suggestion.
    for (const QString& tagId : tagIds) {
        const QVector<library::PersonTagStat> stats = m_d.tags->suggestPeopleForTag(tagId, present, limit);
        if (stats.isEmpty()) continue;
        return {m_d.tags->tag(tagId).name, stats};
    }
    return {};
}

ApprovalOutcome ParticipantsServicesFacade::approve(const QString& meetingId, const QVector<ApprovedRow>& accepted)
{
    ApprovalOutcome out;
    if (!m_d.participants) return out;
    QVector<Participant> list;
    for (const ApprovedRow& a : accepted) {
        Participant p;
        p.id = a.id;
        // The row's person; a name typed in the dialog: a known person with that name, else a new person.
        p.personId = !a.personId.isNull() ? a.personId
                   : m_d.people ? m_d.people->resolve(m_d.people->choiceFor(a.name)) : PersonId();
        if (p.personId.isNull()) continue;
        p.rawSpeakerIds = a.rawIds;
        p.sides = a.sides;
        list.append(p);
    }
    if (!m_d.participants->approveParticipants(meetingId, list)) return out;
    out.ok = true;
    // Learning question: once, for the approved names.
    if (m_d.tags) {
        PersonIds people;
        for (const Participant& p : std::as_const(list)) people << p.personId;
        out.questions = m_d.tags->learningQuestions(meetingId, people);
        m_d.tags->markLearningAsked(meetingId, out.questions);
    }
    return out;
}

bool ParticipantsServicesFacade::skip(const QString& meetingId)
{
    return m_d.participants && m_d.participants->skipApproval(meetingId);
}

bool ParticipantsServicesFacade::unbind(const QString& meetingId, const QString& participantId)
{
    return m_d.participants && m_d.participants->unbindParticipant(meetingId, participantId);
}

void ParticipantsServicesFacade::answerLearning(const library::PersonTagQuestion& question, bool yes)
{
    if (m_d.tags) m_d.tags->answerLearning(question, yes);
}

QVector<ParticipantTrackChoice> ParticipantsServicesFacade::selectableTracks(const QString& meetingId) const
{
    QVector<ParticipantTrackChoice> out;
    if (meetingId.isEmpty() || !m_d.store) return out;
    const Meeting m = m_d.store->load(meetingId);
    // The selectable tracks: the logical tracks included in the mixdown (once per segment).
    for (const library::TrackView& v : library::TrackCatalog::tracks(m)) {
        if (v.segment > 1 || !v.track.included() || v.fileMissing) continue;
        out.append({v.track.id, v.displayName, iconOf(v)});
    }
    return out;
}

ParticipantTrackBinding ParticipantsServicesFacade::participantTracks(const QString& meetingId, const QString& id) const
{
    return m_d.participants ? m_d.participants->participantTracks(meetingId, id) : ParticipantTrackBinding();
}

bool ParticipantsServicesFacade::setParticipantTracks(const QString& meetingId, const QString& id,
                                                      const QStringList& trackIds)
{
    return m_d.participants && m_d.participants->setParticipantTracks(meetingId, id, trackIds);
}

} // namespace tanara_qml
