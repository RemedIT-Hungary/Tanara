// TODO(P15): remove facade -- SpeakerEditor only delegates to SpeakerEditSession and RecheckService.
#include "tanara/editing/session/SpeakerEditor.h"

// The two defaults that still come from processing (the facade's legacy edges; P15 moves them to app::Services).
#include "tanara/processing/analysis/TrackActivity.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

namespace tanara::editing {

namespace {

// The default track-activity source (on a background thread): only the tracks.activity.bin cache. Computing
// a missing one needs the media tool: the owner installs that provider (setActivityProvider, e.g.
// processing::loadOrComputeSideActivity in AppController).
MeetingActivity defaultActivity(const Meeting& m)
{
    if (m.folder.isEmpty()) return {};
    bool mic = false, loop = false;
    for (const Track& t : m.tracks) {
        if (!t.active) continue;
        mic |= t.kind == TrackKind::Mic;
        loop |= t.kind == TrackKind::Loopback;
    }
    if (!mic || !loop) return {};
    return MeetingActivity::load(m.folder, processing::trackactivity::activityFingerprint(m, m.folder));
}

QString modelDisplayName(const QString& id)
{
    const auto spec = processing::VoiceModelRegistry::spec(id);
    return spec ? spec->displayName : QString();
}

} // namespace

SpeakerEditor::SpeakerEditor(library::MeetingRepository* store, library::PeopleStore* people,
                             library::VoiceprintStore* voiceprints, const QString& meetingId, QObject* parent)
    : QObject(parent)
    , m_session(std::make_unique<SpeakerEditSession>(store, people, voiceprints, meetingId))
    , m_recheck(std::make_unique<RecheckService>(m_session.get()))
{
    m_session->setDefaultActivityProvider(defaultActivity);
    m_recheck->setModelNameResolver(modelDisplayName);

    SpeakerEditSession* s = m_session.get();
    connect(s, &SpeakerEditSession::utterancesChanged, this, &SpeakerEditor::utterancesChanged);
    connect(s, &SpeakerEditSession::speakersChanged, this, &SpeakerEditor::speakersChanged);
    connect(s, &SpeakerEditSession::uncertainCountChanged, this, &SpeakerEditor::uncertainCountChanged);
    connect(s, &SpeakerEditSession::undoStateChanged, this, &SpeakerEditor::undoStateChanged);
    connect(s, &SpeakerEditSession::suggestionChanged, this, &SpeakerEditor::suggestionChanged);
    connect(s, &SpeakerEditSession::summaryStaleChanged, this, &SpeakerEditor::summaryStaleChanged);
    connect(s, &SpeakerEditSession::reloaded, this, &SpeakerEditor::reloaded);
    connect(s, &SpeakerEditSession::reviewGroupsChanged, this, &SpeakerEditor::reviewGroupsChanged);
    connect(s, &SpeakerEditSession::reviewRunningChanged, this, &SpeakerEditor::reviewRunningChanged);
    connect(s, &SpeakerEditSession::sampleDeviationChanged, this, &SpeakerEditor::sampleDeviationChanged);
    connect(s, &SpeakerEditSession::sampleStatsChanged, this, &SpeakerEditor::sampleStatsChanged);
    connect(s, &SpeakerEditSession::speakerMapChanged, this, &SpeakerEditor::speakerMapChanged);
    connect(s, &SpeakerEditSession::peopleChanged, this, &SpeakerEditor::peopleChanged);
    connect(s, &SpeakerEditSession::voiceprintsChanged, this, &SpeakerEditor::voiceprintsChanged);
    RecheckService* r = m_recheck.get();
    connect(r, &RecheckService::embeddingRunningChanged, this, &SpeakerEditor::embeddingRunningChanged);
    connect(r, &RecheckService::embeddingProgress, this, &SpeakerEditor::embeddingProgress);
    connect(r, &RecheckService::embeddingFinished, this, &SpeakerEditor::embeddingFinished);
    connect(r, &RecheckService::recheckFinished, this, &SpeakerEditor::recheckFinished);
}

SpeakerEditor::~SpeakerEditor()
{
    // The service works on the session: it goes first (stops a running embedding, saves the cache).
    m_recheck.reset();
    m_session.reset();
}

SpeakerEditSession* SpeakerEditor::session() const { return m_session.get(); }
RecheckService* SpeakerEditor::recheck() const { return m_recheck.get(); }

void SpeakerEditor::flushPendingWrites() { m_session->flushPendingWrites(); }
QString SpeakerEditor::meetingId() const { return m_session->meetingId(); }
bool SpeakerEditor::hasTranscript() const { return m_session->hasTranscript(); }
void SpeakerEditor::setSelfPerson(const SelfPerson& self) { m_session->setSelfPerson(self); }

void SpeakerEditor::setEmbedderFactory(providers::UtteranceEmbedderFactory factory)
{
    m_session->setEmbedderFactory(factory);
    m_recheck->setEmbedderFactory(std::move(factory));
}

void SpeakerEditor::setVoiceModelIds(const QStringList& ids) { m_session->setVoiceModelIds(ids); }
QStringList SpeakerEditor::voiceModelIds() const { return m_session->voiceModelIds(); }

// ---- read model ----------------------------------------------------------

int SpeakerEditor::utteranceCount() const { return m_session->utteranceCount(); }
QVector<EditorUtterance> SpeakerEditor::utterances() const { return m_session->utterances(); }
EditorUtterance SpeakerEditor::utteranceAt(int index) const { return m_session->utteranceAt(index); }
EditorUtterance SpeakerEditor::utterance(const QString& id) const { return m_session->utterance(id); }
int SpeakerEditor::indexOf(const QString& id) const { return m_session->indexOf(id); }
QVector<EditorSpeaker> SpeakerEditor::speakers() const { return m_session->speakers(); }
EditorSpeaker SpeakerEditor::speaker(const QString& key) const { return m_session->speaker(key); }
QStringList SpeakerEditor::personAliases(const PersonId& person) const { return m_session->personAliases(person); }
PersonChoice SpeakerEditor::personChoice(const QString& idOrName) const { return m_session->personChoice(idOrName); }
QString SpeakerEditor::personDisplayName(const PersonId& person) const { return m_session->personDisplayName(person); }
int SpeakerEditor::uncertainCount() const { return m_session->uncertainCount(); }
QStringList SpeakerEditor::uncertainUtteranceIds() const { return m_session->uncertainUtteranceIds(); }

// ---- operations --------------------------------------------------------------

bool SpeakerEditor::moveUtterances(const QStringList& utteranceIds, const QString& speakerKey, bool noisy)
{
    return m_session->moveUtterances(utteranceIds, speakerKey, noisy);
}

QString SpeakerEditor::moveUtterancesToPerson(const QStringList& utteranceIds, const PersonChoice& person,
                                              bool noisy)
{
    return m_session->moveUtterancesToPerson(utteranceIds, person, noisy);
}

QString SpeakerEditor::moveUtterancesToNewParticipant(const QStringList& utteranceIds, bool noisy)
{
    return m_session->moveUtterancesToNewParticipant(utteranceIds, noisy);
}

bool SpeakerEditor::reassignSpeaker(const QString& speakerKey, const PersonChoice& person, bool fixVoiceprints)
{
    return m_session->reassignSpeaker(speakerKey, person, fixVoiceprints);
}

bool SpeakerEditor::revertSpeakerToAnonymous(const QString& speakerKey, bool fixVoiceprints)
{
    return m_session->revertSpeakerToAnonymous(speakerKey, fixVoiceprints);
}

bool SpeakerEditor::applyBindings(const QVector<SpeakerBinding>& bindings, const QString& undoText)
{
    return m_session->applyBindings(bindings, undoText);
}

bool SpeakerEditor::mergeSpeakers(const QString& fromKey, const QString& intoKey)
{
    return m_session->mergeSpeakers(fromKey, intoKey);
}

QString SpeakerEditor::addParticipant(const PersonChoice& person) { return m_session->addParticipant(person); }
bool SpeakerEditor::removeParticipant(const QString& speakerKey) { return m_session->removeParticipant(speakerKey); }

bool SpeakerEditor::confirmUtterances(const QStringList& utteranceIds, bool asNoisy)
{
    return m_session->confirmUtterances(utteranceIds, asNoisy);
}

bool SpeakerEditor::setUtterancesNoisy(const QStringList& utteranceIds, bool noisy)
{
    return m_session->setUtterancesNoisy(utteranceIds, noisy);
}

bool SpeakerEditor::setUtterancesIgnored(const QStringList& utteranceIds, bool ignored)
{
    return m_session->setUtterancesIgnored(utteranceIds, ignored);
}

SampleDeviation SpeakerEditor::lastSampleDeviation() const { return m_session->lastSampleDeviation(); }
QVector<SpeakerSampleStats> SpeakerEditor::sampleStats() const { return m_session->sampleStats(); }

// ---- recheck -----------------------------------------------------------------

QString SpeakerEditor::referenceSummary(const QVector<SpeakerReference>& refs)
{
    return RecheckService::referenceSummary(refs);
}

QString SpeakerEditor::recheckBasisSummary(const RecheckResult& r) { return RecheckService::recheckBasisSummary(r); }

QString SpeakerEditor::similarVoicesNote(const QVector<SimilarVoices>& similar)
{
    return RecheckService::similarVoicesNote(similar);
}

SpeakerEditor::RecheckResult SpeakerEditor::recheckFromConfirmed() { return m_recheck->recheckFromConfirmed(); }
bool SpeakerEditor::canRecheck() const { return m_recheck->canRecheck(); }
QString SpeakerEditor::recheckBlocker() const { return m_recheck->recheckBlocker(); }

SpeakerEditor::PairRecheckResult SpeakerEditor::recheckPair(const QString& speakerKeyA, const QString& speakerKeyB)
{
    return m_recheck->recheckPair(speakerKeyA, speakerKeyB);
}

QString SpeakerEditor::pairRecheckBlocker(const QString& speakerKeyA, const QString& speakerKeyB) const
{
    return m_recheck->pairRecheckBlocker(speakerKeyA, speakerKeyB);
}

bool SpeakerEditor::hasPairOffer() const { return m_session->hasPairOffer(); }
PairRecheckOffer SpeakerEditor::pairOffer() const { return m_session->pairOffer(); }
void SpeakerEditor::declinePairOffer() { m_session->declinePairOffer(); }
void SpeakerEditor::dismissPairOffer() { m_session->dismissPairOffer(); }

// ---- suggestion ---------------------------------------------------------------

bool SpeakerEditor::hasSuggestion() const { return m_session->hasSuggestion(); }
SpeakerSuggestion SpeakerEditor::suggestion() const { return m_session->suggestion(); }
bool SpeakerEditor::acceptSuggestion() { return m_session->acceptSuggestion(); }
void SpeakerEditor::dismissSuggestion() { m_session->dismissSuggestion(); }

// ---- undo / redo ------------------------------------------------------------

bool SpeakerEditor::canUndo() const { return m_session->canUndo(); }
bool SpeakerEditor::canRedo() const { return m_session->canRedo(); }
QString SpeakerEditor::undoText() const { return m_session->undoText(); }
QString SpeakerEditor::redoText() const { return m_session->redoText(); }
QVector<quint64> SpeakerEditor::undoSerials() const { return m_session->undoSerials(); }
void SpeakerEditor::undo() { m_session->undo(); }
void SpeakerEditor::redo() { m_session->redo(); }

// ---- embeddings ------------------------------------------------------------

bool SpeakerEditor::embeddingsSupported() const { return m_recheck->embeddingsSupported(); }
bool SpeakerEditor::embeddingsComplete() const { return m_recheck->embeddingsComplete(); }
bool SpeakerEditor::isEmbeddingRunning() const { return m_recheck->isEmbeddingRunning(); }
QString SpeakerEditor::embeddingError() const { return m_recheck->embeddingError(); }
void SpeakerEditor::startEmbedding() { m_recheck->startEmbedding(); }
void SpeakerEditor::cancelEmbedding() { m_recheck->cancelEmbedding(); }

// ---- manual voiceprint ------------------------------------------------------

VoiceprintMaterial SpeakerEditor::voiceprintMaterial(const QString& speakerKey) const
{
    return m_session->voiceprintMaterial(speakerKey);
}

VoiceprintResult SpeakerEditor::createVoiceprint(const QString& speakerKey)
{
    return m_session->createVoiceprint(speakerKey);
}

VoiceprintResult SpeakerEditor::createVoiceprintFromLines(const QString& speakerKey, const QStringList& utteranceIds)
{
    return m_session->createVoiceprintFromLines(speakerKey, utteranceIds);
}

bool SpeakerEditor::removeVoiceprint(const QString& printId) { return m_session->removeVoiceprint(printId); }

// ---- manual track assignment -----------------------------------------------------

QStringList SpeakerEditor::trackAssignment(const QString& speakerKey) const
{
    return m_session->trackAssignment(speakerKey);
}

bool SpeakerEditor::setTrackAssignment(const QString& speakerKey, const QStringList& trackIds)
{
    return m_session->setTrackAssignment(speakerKey, trackIds);
}

// ---- evidence, candidates, "To review" -----------------------------------------

void SpeakerEditor::setActivityProvider(ActivityProvider provider) { m_session->setActivityProvider(std::move(provider)); }
void SpeakerEditor::setPersonTagsProvider(PersonTagsFn fn) { m_session->setPersonTagsProvider(std::move(fn)); }
QStringList SpeakerEditor::meetingTagIds() const { return m_session->meetingTagIds(); }
QStringList SpeakerEditor::personTagIds(const PersonId& person) const { return m_session->personTagIds(person); }
void SpeakerEditor::notifyPersonTagsChanged() { m_session->notifyPersonTagsChanged(); }
void SpeakerEditor::setTagEvidenceProvider(library::TagEvidenceFn fn) { m_session->setTagEvidenceProvider(std::move(fn)); }
QVector<ReviewGroup> SpeakerEditor::reviewGroups() const { return m_session->reviewGroups(); }
bool SpeakerEditor::isReviewRunning() const { return m_session->isReviewRunning(); }
bool SpeakerEditor::applyReviewGroup(const QString& groupId) { return m_session->applyReviewGroup(groupId); }
bool SpeakerEditor::splitSpeaker(const QString& speakerKey) { return m_session->splitSpeaker(speakerKey); }

QVector<Candidate> SpeakerEditor::lineCandidates(const QString& utteranceId) const
{
    return m_session->lineCandidates(utteranceId);
}

QVector<Candidate> SpeakerEditor::speakerCandidates(const QString& speakerKey) const
{
    return m_session->speakerCandidates(speakerKey);
}

QVector<Evidence> SpeakerEditor::whyNot(const QString& utteranceId) const { return m_session->whyNot(utteranceId); }

QVector<Evidence> SpeakerEditor::speakerEvidence(const QString& speakerKey) const
{
    return m_session->speakerEvidence(speakerKey);
}

QVector<SpeakerEditor::SimilarVoices> SpeakerEditor::similarVoices() const { return m_session->similarVoices(); }
SideReport SpeakerEditor::sideReport() const { return m_session->sideReport(); }
void SpeakerEditor::refreshReviewGroups() { m_session->refreshReviewGroups(); }

// ---- summary staleness / re-transcription ----------------------------------

SummaryStaleInfo SpeakerEditor::summaryStale() const { return m_session->summaryStale(); }
RetranscribeImpact SpeakerEditor::retranscribeImpact() const { return m_session->retranscribeImpact(); }
void SpeakerEditor::dismissSummaryStale() { m_session->dismissSummaryStale(); }
void SpeakerEditor::notifySummaryRegenerated() { m_session->notifySummaryRegenerated(); }
void SpeakerEditor::refreshFromDisk() { m_session->refreshFromDisk(); }
void SpeakerEditor::reloadTranscript() { m_session->reloadTranscript(); }

} // namespace tanara::editing
