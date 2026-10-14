#include "tanara/editing/participants/ParticipantService.h"

#include "tanara/editing/participants/ParticipantSample.h"

#include "tanara/editing/review/SideAnalysis.h"
#include "tanara/editing/session/SpeakerEditSession.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/editing/undo/MeetingUndoRegistry.h"
#include "tanara/library/people/PeopleService.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/library/tracks/TrackCatalog.h"

#include <QDateTime>
#include <QSet>
#include <QUuid>

#include <algorithm>

namespace tanara::editing {

namespace {

// The leading ids of the meeting's included logical tracks on a given side (in track order).
QStringList includedLeaders(const Meeting& m, bool mic, bool loop)
{
    QStringList out;
    const QVector<library::tracknames::SegmentInfo> seg = library::tracknames::segments(m.tracks);
    for (int i = 0; i < m.tracks.size(); ++i) {
        const int lead = seg.value(i).leader >= 0 ? seg.value(i).leader : i;
        const Track& t = m.tracks.at(i);
        if (!t.included()) continue;
        if (!((mic && t.kind == TrackKind::Mic) || (loop && t.kind == TrackKind::Loopback))) continue;
        const QString id = m.tracks.at(lead).id;
        if (!out.contains(id)) out << id;
    }
    return out;
}

// Track ids mapped to the leading ids of the meeting's logical tracks (unknown ones dropped, no repeats).
QStringList normalizeTrackIds(const Meeting& m, const QStringList& trackIds)
{
    QStringList out;
    const QVector<library::tracknames::SegmentInfo> seg = library::tracknames::segments(m.tracks);
    for (const QString& id : trackIds) {
        for (int i = 0; i < m.tracks.size(); ++i) {
            if (m.tracks.at(i).id != id) continue;
            const int lead = seg.value(i).leader >= 0 ? seg.value(i).leader : i;
            const QString leadId = m.tracks.at(lead).id;
            if (!out.contains(leadId)) out << leadId;
            break;
        }
    }
    return out;
}

const Participant* participantById(const Meeting& m, const QString& id)
{
    for (const Participant& p : m.participants)
        if (!id.isEmpty() && p.id == id) return &p;
    return nullptr;
}

} // namespace

ParticipantService::ParticipantService(const Deps& deps, QObject* parent) : QObject(parent), m_deps(deps) {}

ParticipantService::~ParticipantService() = default;

SpeakerEditSession* ParticipantService::editorFor(const QString& meetingId) const
{
    return m_deps.editorFor ? m_deps.editorFor(meetingId) : nullptr;
}

SpeakerEditSession* ParticipantService::existingEditor(const QString& meetingId) const
{
    return m_deps.existingEditor ? m_deps.existingEditor(meetingId) : nullptr;
}

QVector<Participant> ParticipantService::participants(const QString& meetingId) const
{
    return m_deps.store->load(meetingId).participants;
}

bool ParticipantService::participantApprovalPending(const QString& meetingId) const
{
    const Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty() || m.approval || (m_deps.analysis && m_deps.analysis->analysisRunning(meetingId))) return false;
    for (const Participant& p : m.participants)
        if (p.source != ParticipantSource::Manual || !p.sides.isEmpty()) return true;
    return false;
}

// ---- listening to tracks: speaker -> track binding ---------------------------------------------

QStringList ParticipantService::speakerKeysFor(const Meeting& m, const QString& id) const
{
    if (!m.hasTranscript || id.isEmpty()) return {};
    SpeakerEditSession* ed = editorFor(m.id);
    if (!ed || !ed->hasTranscript()) return {};
    const QVector<EditorSpeaker> speakers = ed->speakers();
    const Participant* p = participantById(m, id);
    if (!p) {
        for (const EditorSpeaker& s : speakers)
            if (s.key == id) return {id};
        return {};
    }
    QStringList raws;   // the bound raw speakers ("Speaker 1" or "Speaker 1@mic")
    for (const QString& r : p->rawSpeakerIds) raws << r.section(QLatin1Char('@'), 0, 0);
    QStringList keys;
    for (const EditorSpeaker& s : speakers) {
        const bool byName = !p->personId.isNull() && s.personId == p->personId;
        const bool byRaw = !s.rawLabel.isEmpty() && raws.contains(s.rawLabel);
        if ((byName || byRaw) && !keys.contains(s.key)) keys << s.key;
    }
    return keys;
}

bool ParticipantService::setParticipantTracks(const QString& meetingId, const QString& id,
                                              const QStringList& trackIds)
{
    Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty() || id.isEmpty()) return false;
    const QStringList ids = normalizeTrackIds(m, trackIds);
    int pIdx = -1;
    for (int i = 0; i < m.participants.size(); ++i)
        if (m.participants.at(i).id == id) pIdx = i;
    const QStringList keys = speakerKeysFor(m, id);
    if (pIdx < 0 && !keys.isEmpty()) {
        // Speaker key: the participant belonging to it (if any) gets it too.
        for (int i = 0; i < m.participants.size() && pIdx < 0; ++i)
            if (speakerKeysFor(m, m.participants.at(i).id).contains(id)) pIdx = i;
    }
    if (pIdx < 0 && keys.isEmpty()) return false;
    bool changed = false;
    if (pIdx >= 0 && m.participants.at(pIdx).tracks != ids) {
        Participant& p = m.participants[pIdx];
        p.tracks = ids;
        // The track-side evidence from the manual binding (the voice evidence is unchanged).
        double score = -1.0;
        const bool heard = !p.sides.isEmpty() || p.talkShare > 0.0;
        for (const Evidence& e : p.evidence)
            if (e.kind == EvidenceKind::Voice) score = e.value;
        if (m_deps.analysis) p.evidence = m_deps.analysis->evidence(p, heard, score, m);
        m_deps.store->saveMeeting(m);
        changed = true;
    }
    if (!keys.isEmpty())
        if (SpeakerEditSession* ed = editorFor(meetingId))
            for (const QString& key : keys)
                changed = ed->setTrackAssignment(key, ids) || changed;
    if (changed) emit participantsChanged(meetingId);
    return changed;
}

ParticipantTrackBinding ParticipantService::participantTracks(const QString& meetingId, const QString& id)
{
    ParticipantTrackBinding b;
    const Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) return b;
    const Participant* p = participantById(m, id);
    const QStringList keys = speakerKeysFor(m, id);
    SpeakerEditSession* ed = keys.isEmpty() ? nullptr : editorFor(meetingId);
    // Manual: after the transcript the editor's assignment is the truth (the "Why them?" chips), before it the participant's.
    if (ed) {
        for (const QString& key : keys) {
            const QStringList t = ed->trackAssignment(key);
            if (!t.isEmpty()) {
                b.trackIds = t;
                b.manual = true;
                return b;
            }
        }
    } else if (p && !p->tracks.isEmpty()) {
        b.trackIds = normalizeTrackIds(m, p->tracks);
        b.manual = !b.trackIds.isEmpty();
        if (b.manual) return b;
    }
    // Inferred: the editor's side analysis, otherwise the voice analysis's sides.
    bool mic = false, loop = false;
    if (ed) {
        const SideReport r = ed->sideReport();
        for (const PersonSide& ps : r.persons) {
            if (!keys.contains(ps.speakerKey)) continue;
            mic = mic || ps.side == Side::Local || ps.side == Side::Mixed;
            loop = loop || ps.side == Side::Remote || ps.side == Side::Mixed;
        }
    }
    if (!mic && !loop && p) {
        mic = p->sides.contains(QStringLiteral("mic"));
        loop = p->sides.contains(QStringLiteral("loopback"));
    }
    b.trackIds = includedLeaders(m, mic, loop);
    return b;
}

void ParticipantService::copyParticipantTracksToOverlay(const QString& meetingId)
{
    const Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty() || !m.hasTranscript) return;
    SpeakerEditSession* ed = editorFor(meetingId);
    if (!ed || !ed->hasTranscript()) return;
    for (const Participant& p : m.participants) {
        if (p.tracks.isEmpty()) continue;
        for (const QString& key : speakerKeysFor(m, p.id))
            if (ed->trackAssignment(key).isEmpty()) ed->setTrackAssignment(key, p.tracks);
    }
}

// ---- binding to the transcript and the decision ---------------------------------------------------

bool ParticipantService::bindRawSpeakers(const QString& meetingId)
{
    Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty() || m.participants.isEmpty()) return false;
    const QVector<TranscriptLine> lines = speakeredit::loadTranscriptLines(m.folder);
    if (lines.isEmpty() || !m_deps.analysis) return false;
    if (!m_deps.analysis->bindRawSpeakers(m.participants, m.folder, lines)) return false;
    m_deps.store->saveMeeting(m);
    emit participantsChanged(meetingId);
    return true;
}

bool ParticipantService::applySpeakerBindings(const QString& meetingId, const QVector<SpeakerBinding>& bindings,
                                              const QString& undoText)
{
    if (bindings.isEmpty()) return false;
    SpeakerEditSession* ed = editorFor(meetingId);
    if (!ed || !ed->hasTranscript()) return false;
    return ed->applyBindings(bindings, undoText);
}

bool ParticipantService::applyParticipantsToTranscript(const QString& meetingId)
{
    bindRawSpeakers(meetingId);
    // A manual track binding set before the transcript is carried into the speakers' assignment (one piece of data).
    copyParticipantTracksToOverlay(meetingId);
    const Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty() || !m.approval || m.approval->skipped) return false;
    const QVector<TranscriptLine> lines = speakeredit::loadTranscriptLines(m.folder);
    if (lines.isEmpty()) return false;
    QVector<SpeakerBinding> bindings;
    if (m.approval->solo) {
        if (m_deps.peopleService) m_deps.peopleService->ensureSelf();
        const PersonId self = m_deps.peopleService ? m_deps.peopleService->selfPerson().id : PersonId();
        if (self.isNull()) return false;
        QStringList raws;
        for (const TranscriptLine& l : lines)
            if (!l.rawLabel.isEmpty() && !raws.contains(l.rawLabel)) raws << l.rawLabel;
        for (const QString& r : raws) bindings.append({r, self, {}});
        applySpeakerBindings(meetingId, bindings, tr("Csak én beszéltem"));
        return true;
    }
    QStringList accepted;
    for (const Participant& p : m.participants)
        if (p.approved) accepted << p.id;
    if (m_deps.analysis)
        bindings = m_deps.analysis->approvalBindings(m.participants, accepted, lines, m, /*trackVeto*/ true);
    applySpeakerBindings(meetingId, bindings, tr("Résztvevők jóváhagyása"));
    return true;
}

bool ParticipantService::approveParticipants(const QString& meetingId, const QVector<Participant>& accepted)
{
    ParticipantUndoScope undoScope(m_deps.undo, meetingId, tr("Résztvevők jóváhagyva"));
    Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) return false;
    QStringList acceptedIds;
    for (const Participant& a : accepted) {
        int idx = -1;
        for (int i = 0; i < m.participants.size(); ++i)
            if (!a.id.isEmpty() && m.participants.at(i).id == a.id) idx = i;
        if (idx < 0) {
            // The new person added in the dialog (the caller resolved / created the person).
            Participant p = a;
            if (p.personId.isNull()) continue;
            p.id = QStringLiteral("m-") + QUuid::createUuid().toString(QUuid::Id128).left(8);
            p.source = ParticipantSource::Manual;
            if (m_deps.analysis) p.evidence = m_deps.analysis->evidence(p, !p.sides.isEmpty(), -1.0, m);
            m.participants.append(p);
            idx = m.participants.size() - 1;
        } else if (!a.personId.isNull()) {
            m.participants[idx].personId = a.personId;   // person changed in the dialog
        }
        acceptedIds << m.participants.at(idx).id;
    }

    ParticipantApproval ap;
    ap.at = QDateTime::currentDateTime().toString(Qt::ISODate);
    ap.modelIds = m_deps.analysis ? m_deps.analysis->analysisModelIds(m.folder) : QStringList();
    for (Participant& p : m.participants) {
        p.approved = acceptedIds.contains(p.id) && !p.personId.isNull();
        ap.candidates.append({p.id, p.personId,
                              m_deps.analysis ? m_deps.analysis->group(p) : ParticipantGroup::Doubt, p.approved,
                              p.rawSpeakerIds});
    }
    m.approval = ap;
    m_deps.store->saveMeeting(m);
    emit participantsChanged(meetingId);
    // If the transcript already exists: the binding now (one undo step); otherwise when the transcript arrives.
    if (m.hasTranscript) applyParticipantsToTranscript(meetingId);
    return true;
}

bool ParticipantService::skipApproval(const QString& meetingId)
{
    ParticipantUndoScope undoScope(m_deps.undo, meetingId, tr("Jóváhagyás kihagyva"));
    Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) return false;
    ParticipantApproval ap;
    ap.at = QDateTime::currentDateTime().toString(Qt::ISODate);
    ap.modelIds = m_deps.analysis ? m_deps.analysis->analysisModelIds(m.folder) : QStringList();
    ap.skipped = true;
    for (const Participant& p : m.participants)
        ap.candidates.append({p.id, p.personId,
                              m_deps.analysis ? m_deps.analysis->group(p) : ParticipantGroup::Doubt, false,
                              p.rawSpeakerIds});
    m.approval = ap;
    m_deps.store->saveMeeting(m);
    emit participantsChanged(meetingId);
    return true;
}

bool ParticipantService::unbindParticipant(const QString& meetingId, const QString& participantId)
{
    ParticipantUndoScope undoScope(m_deps.undo, meetingId, tr("Nem volt ott"));
    Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) return false;
    int idx = -1;
    for (int i = 0; i < m.participants.size(); ++i)
        if (m.participants.at(i).id == participantId) idx = i;
    if (idx < 0) return false;
    Participant& p = m.participants[idx];
    const QString pname = m_deps.people ? m_deps.people->displayName(p.personId) : QString();
    undoScope.text = tr("Nem volt ott: %1").arg(pname);

    // Its lines go anonymous: the bound raw speakers (for a two-sided one only that side's lines), and every
    // speaker bound to the person (e.g. from an earlier automatic identification).
    QVector<SpeakerBinding> bindings;
    if (m.hasTranscript) {
        const QVector<TranscriptLine> lines = speakeredit::loadTranscriptLines(m.folder);
        Participant named = p;   // only their bindings (with a person, so two-sided lines come out)
        if (named.personId.isNull()) named.personId = PersonId::fromString(QStringLiteral("?"));
        if (m_deps.analysis)
            for (SpeakerBinding b : m_deps.analysis->approvalBindings({named}, {p.id}, lines, m, /*trackVeto*/ false)) {
                b.personId = PersonId();
                bindings.append(b);
            }
        if (!p.personId.isNull())
            if (SpeakerEditSession* ed = editorFor(meetingId))
                for (const EditorSpeaker& s : ed->speakers())
                    if (s.personId == p.personId)
                        bindings.append({s.key, PersonId(), {}});
    }
    p.approved = false;
    p.rawSpeakerIds.clear();
    if (m.approval)
        for (ParticipantApprovalEntry& e : m.approval->candidates)
            if (e.participantId == participantId) e.checked = false;
    m_deps.store->saveMeeting(m);
    applySpeakerBindings(meetingId, bindings, tr("Nem volt ott: %1").arg(pname));
    emit participantsChanged(meetingId);
    return true;
}

QString ParticipantService::addParticipant(const QString& meetingId, const PersonChoice& choice)
{
    const QString label = !choice.id.isNull() && m_deps.people ? m_deps.people->displayName(choice.id) : choice.newName;
    ParticipantUndoScope undoScope(m_deps.undo, meetingId, tr("Résztvevő felvéve: %1").arg(label));
    Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty() || choice.isAnonymous()) return {};
    if (!choice.id.isNull())
        for (const Participant& p : m.participants)
            if (p.personId == choice.id) return p.id;
    const PersonId person = m_deps.people ? m_deps.people->resolve(choice) : choice.id;
    if (person.isNull()) return {};
    if (choice.id.isNull()) emit peopleChanged();
    Participant p;
    p.id = QStringLiteral("m-") + QUuid::createUuid().toString(QUuid::Id128).left(8);
    p.personId = person;
    p.source = ParticipantSource::Manual;
    if (m_deps.analysis) p.evidence = m_deps.analysis->evidence(p, false, -1.0, m);
    m.participants.append(p);
    m_deps.store->saveMeeting(m);
    emit participantsChanged(meetingId);
    return p.id;
}

bool ParticipantService::setSoloMeeting(const QString& meetingId)
{
    ParticipantUndoScope undoScope(m_deps.undo, meetingId, tr("Csak én beszéltem"));
    if (m_deps.peopleService) m_deps.peopleService->ensureSelf();
    const SelfPerson me = m_deps.peopleService ? m_deps.peopleService->selfPerson() : SelfPerson();
    const PersonId self = me.id;
    Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty() || self.isNull() || me.name.isEmpty()) return false;   // no own name configured
    // The running analysis's result is no longer needed.
    if (m_deps.analysis && m_deps.analysis->analysisRunning(meetingId)) m_deps.analysis->cancelAnalysis(meetingId);

    bool have = false;
    for (Participant& p : m.participants) {
        const bool mine = p.personId == self;
        p.approved = mine;
        have = have || mine;
    }
    if (!have) {
        Participant p;
        p.id = QStringLiteral("m-") + QUuid::createUuid().toString(QUuid::Id128).left(8);
        p.personId = self;
        p.source = ParticipantSource::Manual;
        p.approved = true;
        if (m_deps.analysis) p.evidence = m_deps.analysis->evidence(p, false, -1.0, m);
        m.participants.append(p);
    }
    ParticipantApproval ap;
    ap.at = QDateTime::currentDateTime().toString(Qt::ISODate);
    ap.solo = true;
    for (const Participant& p : m.participants)
        ap.candidates.append({p.id, p.personId,
                              m_deps.analysis ? m_deps.analysis->group(p) : ParticipantGroup::Doubt, p.approved,
                              p.rawSpeakerIds});
    m.approval = ap;
    m_deps.store->saveMeeting(m);
    if (m.hasTranscript) applyParticipantsToTranscript(meetingId);
    emit participantsChanged(meetingId);
    return true;
}

bool ParticipantService::removeParticipant(const QString& meetingId, const QString& participantId)
{
    ParticipantUndoScope undoScope(m_deps.undo, meetingId, tr("Résztvevő eltávolítva"));
    const Meeting m0 = m_deps.store->load(meetingId);
    if (m0.id.isEmpty()) return false;
    const auto has = [&participantId](const Participant& p) { return p.id == participantId; };
    const auto it0 = std::find_if(m0.participants.cbegin(), m0.participants.cend(), has);
    if (it0 == m0.participants.cend()) {
        // There is no participant record (the meeting got names before participant analysis): the id is the
        // transcript speaker's key; its lines go anonymous (one step).
        SpeakerEditSession* ed = m0.hasTranscript ? editorFor(meetingId) : nullptr;
        if (!ed) return false;
        for (const EditorSpeaker& s : ed->speakers()) {
            if (s.key != participantId || s.anonymous) continue;
            undoScope.text = tr("Nem volt ott: %1").arg(s.displayName);
            applySpeakerBindings(meetingId, {{s.key, PersonId(), {}}}, undoScope.text);
            emit participantsChanged(meetingId);
            return true;
        }
        return false;
    }
    if (!it0->personId.isNull() && m_deps.people)
        undoScope.text = tr("Résztvevő eltávolítva: %1").arg(m_deps.people->displayName(it0->personId));

    // Its lines go anonymous (one editor step, which the shared step absorbs), then the deletion.
    unbindParticipant(meetingId, participantId);
    Meeting m = m_deps.store->load(meetingId);
    m.participants.erase(std::remove_if(m.participants.begin(), m.participants.end(), has), m.participants.end());
    if (m.approval) {
        auto& c = m.approval->candidates;
        c.erase(std::remove_if(c.begin(), c.end(),
                               [&](const ParticipantApprovalEntry& e) { return e.participantId == participantId; }),
                c.end());
    }
    m_deps.store->saveMeeting(m);
    emit participantsChanged(meetingId);
    return true;
}

QVector<Participant> ParticipantService::unheardParticipants(const QString& meetingId) const
{
    const Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty() || !m.hasTranscript) return {};
    // The persons of the transcript's speakers: from the open editor, otherwise resolved from disk.
    QSet<PersonId> heard;
    if (SpeakerEditSession* ed = existingEditor(meetingId)) {
        for (const EditorSpeaker& s : ed->speakers())
            if (s.utteranceCount > 0 && !s.personId.isNull()) heard.insert(s.personId);
    } else {
        const QVector<TranscriptLine> lines = speakeredit::loadTranscriptLines(m.folder);
        const SpeakerOverlay ov = speakeredit::loadOverlayFor(m.folder, lines);
        QSet<QString> keys;
        for (const TranscriptLine& l : lines) keys.insert(speakeredit::resolveSpeakerKey(ov, l));
        for (const QString& k : std::as_const(keys)) {
            const PersonId person = speakeredit::speakerPerson(ov, m.speakerMap, k);
            if (!person.isNull()) heard.insert(person);
        }
    }
    QVector<Participant> out;
    for (const Participant& p : m.participants)
        if (!p.personId.isNull() && p.rawSpeakerIds.isEmpty() && !heard.contains(p.personId))
            out.append(p);
    return out;
}

// ---- the "Who was there?" card's row operations (UA1) -------------------------------------------------

namespace {

// A manual row nobody heard: no voice cluster, side, raw speaker or sample.
bool voiceless(const Participant& p)
{
    return p.source == ParticipantSource::Manual && p.sides.isEmpty() && p.rawSpeakerIds.isEmpty()
           && p.sampleRef.isEmpty() && p.mergedFrom.isEmpty() && p.talkShare <= 0.0;
}

double voiceScoreOf(const Participant& p)
{
    double score = -1.0;
    for (const Evidence& e : p.evidence)
        if (e.kind == EvidenceKind::Voice) score = std::max(score, e.value);
    return score;
}

// A real decision was made (approved; not skipped, not "only me").
bool decided(const Meeting& m) { return m.approval && !m.approval->skipped && !m.approval->solo; }

ParticipantApprovalEntry* approvalEntry(Meeting& m, const QString& participantId)
{
    if (!m.approval) return nullptr;
    for (ParticipantApprovalEntry& e : m.approval->candidates)
        if (e.participantId == participantId) return &e;
    return nullptr;
}

void dropApprovalEntry(Meeting& m, const QString& participantId)
{
    if (!m.approval) return;
    auto& c = m.approval->candidates;
    c.erase(std::remove_if(c.begin(), c.end(),
                           [&](const ParticipantApprovalEntry& e) { return e.participantId == participantId; }),
            c.end());
}

void appendUniqueTo(QStringList& to, const QStringList& from)
{
    for (const QString& s : from)
        if (!s.isEmpty() && !to.contains(s)) to << s;
}

int indexOfId(const Meeting& m, const QString& id)
{
    for (int i = 0; i < m.participants.size(); ++i)
        if (!id.isEmpty() && m.participants.at(i).id == id) return i;
    return -1;
}

} // namespace

int ParticipantService::recordIndexFor(const Meeting& m, const QString& id) const
{
    const int direct = indexOfId(m, id);
    if (direct >= 0 || !m.hasTranscript || id.isEmpty()) return direct;
    // A speaker key: the record bound to that speaker (by person or raw label).
    for (int i = 0; i < m.participants.size(); ++i)
        if (speakerKeysFor(m, m.participants.at(i).id).contains(id)) return i;
    return -1;
}

void ParticipantService::refreshEvidence(Participant& p, const Meeting& m)
{
    if (!m_deps.analysis) return;
    const bool heard = !p.sides.isEmpty() || p.talkShare > 0.0;
    p.evidence = m_deps.analysis->evidence(p, heard, voiceScoreOf(p), m);
}

void ParticipantService::mergeRecords(Meeting& m, int fromIdx, int intoIdx)
{
    if (fromIdx < 0 || intoIdx < 0 || fromIdx == intoIdx) return;
    const Participant from = m.participants.at(fromIdx);
    Participant& into = m.participants[intoIdx];
    if (into.personId.isNull()) into.personId = from.personId;
    // The larger cluster's sample (the speech share before the sum).
    if (!from.sampleRef.isEmpty() && (into.sampleRef.isEmpty() || from.talkShare > into.talkShare)) {
        into.sampleTrackId = from.sampleTrackId;
        into.sampleRef = from.sampleRef;
    }
    appendUniqueTo(into.rawSpeakerIds, from.rawSpeakerIds);
    appendUniqueTo(into.sides, from.sides);
    std::sort(into.sides.begin(), into.sides.end());
    if (into.tracks.isEmpty()) into.tracks = from.tracks;
    into.talkShare = std::min(1.0, into.talkShare + from.talkShare);
    into.approved = (into.approved || from.approved) && !into.personId.isNull();
    appendUniqueTo(into.mergedFrom, QStringList{from.id} + from.mergedFrom);
    // The evidence anew; the voice score: the better of the two.
    const double score = std::max(voiceScoreOf(into), voiceScoreOf(from));
    if (m_deps.analysis)
        into.evidence = m_deps.analysis->evidence(into, !into.sides.isEmpty() || into.talkShare > 0.0, score, m);
    if (ParticipantApprovalEntry* e = approvalEntry(m, into.id)) {
        e->personId = into.personId;
        e->rawSpeakerIds = into.rawSpeakerIds;
        e->checked = into.approved;
    }
    const QString fromId = from.id;
    dropApprovalEntry(m, fromId);
    m.participants.removeAt(fromIdx);
}

bool ParticipantService::nameParticipantImpl(const QString& meetingId, const QString& participantId,
                                             const PersonId& person, const QString& undoText)
{
    ParticipantUndoScope undoScope(m_deps.undo, meetingId, undoText);
    Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty() || person.isNull()) return false;
    const int idx = recordIndexFor(m, participantId);
    if (!m.hasTranscript && idx < 0) return false;
    // After the transcript: the row's speakers (taken before the record changes) go to the person below.
    const QStringList keys =
        m.hasTranscript ? speakerKeysFor(m, idx >= 0 ? m.participants.at(idx).id : participantId) : QStringList();
    bool changed = false;

    if (idx >= 0) {
        const QString id = m.participants.at(idx).id;
        if (m.participants.at(idx).personId != person) {
            m.participants[idx].personId = person;
            changed = true;
        }
        // Other rows of the same person: a manual voiceless one dissolves into this row; a voiced one is the
        // same voice, so this row joins it.
        QString keptId = id;
        for (int j = m.participants.size() - 1; j >= 0; --j) {
            const int self = indexOfId(m, keptId);
            if (j == self || m.participants.at(j).personId != person) continue;
            const Participant other = m.participants.at(j);
            if (voiceless(other)) {
                Participant& p = m.participants[self];
                if (p.tracks.isEmpty()) p.tracks = other.tracks;
                p.approved = p.approved || other.approved;
                dropApprovalEntry(m, other.id);
                m.participants.removeAt(j);
            } else {
                mergeRecords(m, self, j);
                keptId = other.id;
            }
            changed = true;
        }
        const int k = indexOfId(m, keptId);
        if (k >= 0) {
            Participant& p = m.participants[k];
            if (decided(m)) p.approved = true;
            refreshEvidence(p, m);
            if (ParticipantApprovalEntry* e = approvalEntry(m, p.id)) {
                e->personId = p.personId;
                e->checked = p.approved;
            } else if (decided(m)) {
                m.approval->candidates.append({p.id, p.personId,
                                               m_deps.analysis ? m_deps.analysis->group(p) : ParticipantGroup::Doubt,
                                               p.approved, p.rawSpeakerIds});
            }
        }
        if (changed) m_deps.store->saveMeeting(m);
    }
    // The editor binding after the record is saved (the editor writes the meeting too): the speakers merge
    // with the person's speaker, if any; one editor step, absorbed by the shared step.
    if (!keys.isEmpty()) {
        QVector<SpeakerBinding> bindings;
        for (const QString& key : keys) bindings.append({key, person, {}});
        changed = applySpeakerBindings(meetingId, bindings, undoText) || changed;
    }
    if (changed) emit participantsChanged(meetingId);
    return changed;
}

bool ParticipantService::nameParticipant(const QString& meetingId, const QString& participantId,
                                         const PersonChoice& choice)
{
    if (choice.isAnonymous() || participantId.isEmpty()) return false;
    const Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) return false;
    // The row must exist before a new person is created for it.
    if (recordIndexFor(m, participantId) < 0 && speakerKeysFor(m, participantId).isEmpty()) return false;
    const PersonId person = m_deps.people ? m_deps.people->resolve(choice) : choice.id;
    if (person.isNull()) return false;
    if (choice.id.isNull()) emit peopleChanged();
    const QString name = m_deps.people ? m_deps.people->displayName(person) : choice.newName;
    return nameParticipantImpl(meetingId, participantId, person, tr("Elnevezve: %1").arg(name));
}

bool ParticipantService::setSelfParticipant(const QString& meetingId, const QString& participantId, QString* whyNot)
{
    if (m_deps.peopleService) m_deps.peopleService->ensureSelf();
    const SelfPerson me = m_deps.peopleService ? m_deps.peopleService->selfPerson() : SelfPerson();
    if (me.id.isNull() || me.name.isEmpty()) {
        if (whyNot) *whyNot = tr("Nincs megadva a saját neved: a Beállításokban adhatod meg.");
        return false;
    }
    if (whyNot) whyNot->clear();
    return nameParticipantImpl(meetingId, participantId, me.id, tr("Elnevezve: %1").arg(me.name));
}

bool ParticipantService::mergeParticipantInto(const QString& meetingId, const QString& fromId, const QString& intoId)
{
    if (fromId.isEmpty() || intoId.isEmpty() || fromId == intoId) return false;
    ParticipantUndoScope undoScope(m_deps.undo, meetingId, tr("Összevonva"));
    Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) return false;
    const int fi = recordIndexFor(m, fromId);
    const int ii = recordIndexFor(m, intoId);
    SpeakerEditSession* ed = m.hasTranscript ? editorFor(meetingId) : nullptr;
    if (ed && !ed->hasTranscript()) ed = nullptr;

    // The rows' names for the step's text.
    auto label = [&](int idx, const QString& id) {
        if (idx >= 0 && !m.participants.at(idx).personId.isNull() && m_deps.people)
            return m_deps.people->displayName(m.participants.at(idx).personId);
        if (ed)
            for (const EditorSpeaker& s : ed->speakers())
                if (s.key == id) return s.displayName;
        return tr("névtelen hang");
    };
    const QString text = tr("Összevonva: %1 → %2").arg(label(fi, fromId), label(ii, intoId));
    undoScope.text = text;

    if (!ed) {
        // Before the transcript: the records merge.
        if (fi < 0 || ii < 0 || fi == ii) return false;
        mergeRecords(m, fi, ii);
        m_deps.store->saveMeeting(m);
        emit participantsChanged(meetingId);
        return true;
    }

    // After the transcript: the speakers of `from` go to `into` (its person; if anonymous, the person of
    // `from`; if neither has one, into `into`'s speaker), in ONE editor step.
    QStringList intoKeys = speakerKeysFor(m, ii >= 0 ? m.participants.at(ii).id : intoId);
    QStringList fromKeys = speakerKeysFor(m, fi >= 0 ? m.participants.at(fi).id : fromId);
    for (const QString& k : std::as_const(intoKeys)) fromKeys.removeAll(k);
    if (fromKeys.isEmpty() && (fi < 0 || ii < 0 || fi == ii)) return false;
    auto personOf = [&](int idx, const QStringList& keys) {
        if (idx >= 0 && !m.participants.at(idx).personId.isNull()) return m.participants.at(idx).personId;
        for (const QString& k : keys)
            if (const EditorSpeaker s = ed->speaker(k); !s.personId.isNull()) return s.personId;
        return PersonId();
    };
    PersonId target = personOf(ii, intoKeys);
    if (target.isNull()) target = personOf(fi, fromKeys);
    // The records first (the editor writes the meeting too), then the speakers.
    bool changed = false;
    if (fi >= 0 && ii >= 0 && fi != ii) {
        if (m.participants.at(ii).personId.isNull()) m.participants[ii].personId = target;
        mergeRecords(m, fi, ii);
        changed = true;
    } else if (fi >= 0 && !target.isNull() && m.participants.at(fi).personId != target) {
        m.participants[fi].personId = target;   // `into` has no record: the record follows the speaker
        changed = true;
    }
    if (changed) m_deps.store->saveMeeting(m);
    if (!fromKeys.isEmpty()) {
        if (!target.isNull()) {
            QVector<SpeakerBinding> bindings;
            for (const QString& k : intoKeys + fromKeys) bindings.append({k, target, {}});
            changed = applySpeakerBindings(meetingId, bindings, text) || changed;
        } else if (!intoKeys.isEmpty()) {
            if (fromKeys.size() == 1) {
                changed = ed->mergeSpeakers(fromKeys.first(), intoKeys.first()) || changed;
            } else {
                QStringList lines;
                for (const EditorUtterance& u : ed->utterances())
                    if (fromKeys.contains(u.speakerKey)) lines << u.id;
                changed = ed->moveUtterances(lines, intoKeys.first()) || changed;
            }
        }
    }
    if (!changed) return false;
    emit participantsChanged(meetingId);
    return true;
}

bool ParticipantService::confirmParticipant(const QString& meetingId, const QString& participantId, bool confirmed)
{
    ParticipantUndoScope undoScope(m_deps.undo, meetingId, QString());
    Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) return false;
    const int idx = recordIndexFor(m, participantId);
    if (idx < 0) return false;
    Participant& p = m.participants[idx];
    if (confirmed && p.personId.isNull()) return false;   // name it first
    const QString name = m_deps.people ? m_deps.people->displayName(p.personId) : QString();
    undoScope.text = confirmed ? tr("Ott volt: %1").arg(name) : tr("Ott volt, visszavonva: %1").arg(name);
    ParticipantApprovalEntry* e = approvalEntry(m, p.id);
    if (p.approved == confirmed && (!e || e->checked == confirmed)) return false;
    p.approved = confirmed;
    if (e) {
        e->checked = confirmed;
        e->personId = p.personId;
    } else if (m.approval) {
        m.approval->candidates.append({p.id, p.personId,
                                       m_deps.analysis ? m_deps.analysis->group(p) : ParticipantGroup::Doubt,
                                       confirmed, p.rawSpeakerIds});
    }
    m_deps.store->saveMeeting(m);
    // After the transcript, with a decision: the confirmed row's speakers get the name now (one step).
    if (confirmed && m.hasTranscript && decided(m)) applyParticipantsToTranscript(meetingId);
    emit participantsChanged(meetingId);
    return true;
}

// ---- listening ----------------------------------------------------------------------------------

QVector<ParticipantSample> ParticipantService::participantSamples(const QString& meetingId,
                                                                  const QStringList& ids) const
{
    QVector<ParticipantSample> out(ids.size());
    const Meeting m = meetingId.isEmpty() ? Meeting() : m_deps.store->load(meetingId);
    if (m.id.isEmpty() || ids.isEmpty()) return out;
    const QVector<VoiceCluster> clusters = (m_deps.analysis && !m.participants.isEmpty())
        ? participantsample::withMerges(m_deps.analysis->analysisClusters(m.folder), m.participants)
        : QVector<VoiceCluster>();
    SpeakerEditSession* ed = m.hasTranscript ? editorFor(meetingId) : nullptr;
    if (ed && !ed->hasTranscript()) ed = nullptr;
    const QVector<EditorUtterance> utterances = ed ? ed->utterances() : QVector<EditorUtterance>();
    for (int i = 0; i < ids.size(); ++i) {
        const QString& id = ids.at(i);
        const int idx = ed ? recordIndexFor(m, id) : indexOfId(m, id);
        if (idx < 0) continue;   // no record: no cluster to play from
        const Participant& p = m.participants.at(idx);
        if (!ed) {
            ParticipantSample s = participantsample::storedSample(p);
            if (!s.isValid()) s = participantsample::clusterSample(clusters, p.id);
            out[i] = s;
            continue;
        }
        const QStringList keys = p.id == id ? speakerKeysFor(m, id) : QStringList{id};
        const EditorUtterance* longest = participantsample::longestLine(utterances, keys);
        if (!longest) continue;
        ParticipantSample s = participantsample::lineSample(clusters, p.id, longest->startMs, longest->endMs);
        if (!s.isValid() && !p.sampleTrackId.isEmpty()) {
            s.trackId = p.sampleTrackId;
            s.startMs = longest->startMs;
            s.endMs = std::min(longest->endMs, longest->startMs + participantsample::kMaxSampleMs);
            if (!s.isValid()) s = ParticipantSample();
        }
        out[i] = s;
    }
    return out;
}

ParticipantSample ParticipantService::participantSample(const QString& meetingId, const QString& id) const
{
    return participantSamples(meetingId, {id}).value(0);
}

// ---- voice identification -----------------------------------------------------------------------

// THE identification commit in ONE place (shared by the synchronous autoIdentifyMeeting and the asynchronous
// path): the name goes into the speakerMap, the score into the overlay (the editor's "recognized by voice
// (82%)"); then meeting.json, transcript.md with the resolved names, marking the summary stale (if there is
// one), and the signals.
void ParticipantService::commitIdentification(const QString& meetingId, const QVector<SpeakerVoiceMatch>& matches)
{
    if (matches.isEmpty()) return;
    Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) return;
    QStringList identifiedLabels;
    for (const SpeakerVoiceMatch& match : matches) {
        m.speakerMap.insert(match.rawLabel, match.personId);
        speakeredit::recordIdentification(m.folder, match.rawLabel, match.personId, match.score);
        identifiedLabels << match.rawLabel;
    }
    m_deps.store->saveMeeting(m);
    MergedTranscript merged = library::TranscriptRepository::readTokens(m.folder);
    if (!merged.tokens.isEmpty()) {
        speakeredit::applyResolvedSpeakers(merged, m,
                                           m_deps.peopleService ? m_deps.peopleService->personNames() : PersonNames());
        library::TranscriptRepository::writeMarkdown(m.folder, merged.renderMarkdown());
    }
    if (speakeredit::markSummaryStale(m, identifiedLabels)) emit summaryStaleChanged(m.id);
    emit speakerMapChanged(m.id);
}

} // namespace tanara::editing
