#pragma once
//
// SpeakerEditor: TRANSITIONAL FACADE over the split speaker editor (cleanup P13). The same public
// interface as before the split, so the GUI / CLI / PeopleService callers compile unchanged; every call
// is delegated to
//  - SpeakerEditSession (editing/session/SpeakerEditSession.h): read model, line-level operations, undo/redo
//    (SpeakerUndoStack), suggestions, "To review" analysis, manual voiceprint, summary staleness; the
//    persistence goes through library::TranscriptRepository;
//  - RecheckService (editing/recheck/RecheckService.h): background line embeddings (through the
//    voice-embedding port), the recheck from the confirmed lines and the pairwise review.
// The facade also installs the two defaults that still come from the processing module: the track-activity
// cache reader and the voice models' display names.
// TODO(P15): remove facade -- rewire the callers to SpeakerEditSession / RecheckService and let the
// composition root (app::Services) install the defaults above.
//
// The member comments below are kept for the callers; the authoritative ones are on SpeakerEditSession /
// RecheckService.
// An instance is normally provided by AppController::speakerEditor(meetingId); tests can
// also create one directly with explicit stores.
#include "tanara/editing/recheck/RecheckService.h"
#include "tanara/editing/review/CandidateRanker.h"
#include "tanara/editing/review/ReviewGroups.h"
#include "tanara/editing/session/SpeakerEditSession.h"
#include "tanara/editing/session/SpeakerEditTypes.h"
#include "tanara/providers/voice/IUtteranceEmbedder.h"

#include <QObject>
#include <QtNumeric>
#include <QStringList>
#include <QVector>
#include <functional>
#include <memory>

namespace tanara::library { class MeetingRepository; }
namespace tanara::library { class PeopleStore; }
namespace tanara::library { class VoiceprintStore; }

namespace tanara::editing {

// TODO(P15): remove facade
class SpeakerEditor : public QObject {
    Q_OBJECT
public:
    // people / voiceprints may be nullptr (the person-list / voiceprint operations are then skipped).
    SpeakerEditor(library::MeetingRepository* store, library::PeopleStore* people, library::VoiceprintStore* voiceprints,
                  const QString& meetingId, QObject* parent = nullptr);
    ~SpeakerEditor() override;

    QString meetingId() const;
    bool hasTranscript() const;     // whether transcript.segments.json exists (otherwise everything is empty)

    // The user's own person (EditorSpeaker::isSelf; the side analysis' default).
    void setSelfPerson(const SelfPerson& self);
    // Factory of the utterance embedder. Without it there is no uncertainty / suggestion /
    // manual voiceprint; everything else works (graceful degradation without a model or audio).
    void setEmbedderFactory(providers::UtteranceEmbedderFactory factory);
    // The speaker models in use (enabled AND available VoiceModelRegistry ids; provided by
    // AppController). Default: {"campplus"}. The analysis works per line with the models'
    // fused vector (fusion::fuse); a line lacking a vector for some model is skipped. A manual
    // voiceprint yields a separate Voiceprint per model. On change the uncertainty flags
    // refresh; missing vectors are computed by the next startEmbedding().
    void setVoiceModelIds(const QStringList& ids);
    QStringList voiceModelIds() const;

    // ---- read model ----------------------------------------------------
    int utteranceCount() const;
    QVector<EditorUtterance> utterances() const;            // in time order
    EditorUtterance utteranceAt(int index) const;
    EditorUtterance utterance(const QString& id) const;     // unknown id -> index == -1
    int indexOf(const QString& id) const;

    // The meeting's speakers by colorIndex (order of first appearance; manually added ones
    // last). Empty, removed or merged raw labels are not listed.
    QVector<EditorSpeaker> speakers() const;
    EditorSpeaker speaker(const QString& key) const;
    // A person's nicknames (from the person store), so the pickers match nicknames too.
    QStringList personAliases(const PersonId& person) const;            // unknown person = empty
    // The person a picker / text field hands over (a PersonId, or a typed name): a known id, else the first
    // person with that name, else a new person to create (PeopleStore::choiceFor). Empty = anonymous.
    PersonChoice personChoice(const QString& idOrName) const;
    // A person's current display name (empty if unknown).
    QString personDisplayName(const PersonId& person) const;

    int uncertainCount() const;
    QStringList uncertainUtteranceIds() const;

    // ---- operations (each is one undo step; false / empty = nothing happened) ----
    // Moves lines to an existing speaker of the meeting. Lines already there are skipped.
    // noisy: "-> Y, not a sample": the line also gets the "talked over each other" flag (not a
    // voice sample) in the SAME step; then there is no "similar lines" suggestion or
    // deviating-sample flag either. noisy=false: the line's noisy flag is untouched (the old
    // behavior) and it counts as a sample -> lastSampleDeviation() updates. The move also clears
    // the "unidentifiable" flag.
    bool moveUtterances(const QStringList& utteranceIds, const QString& speakerKey, bool noisy = false);
    // ...to a person: if already a speaker of the meeting, to that one; otherwise a new participant is
    // created (a known person, OR a brand-new one from PersonChoice::newName: created in the same undo
    // step). Returns the target speaker's key. noisy: as above.
    QString moveUtterancesToPerson(const QStringList& utteranceIds, const PersonChoice& person,
                                   bool noisy = false);
    // ...to a new ANONYMOUS participant ("New speaker N"). Returns the new speaker's key. noisy: as above.
    QString moveUtterancesToNewParticipant(const QStringList& utteranceIds, bool noisy = false);

    // The whole speaker goes to another person (all its lines). If the person is already a
    // speaker of the meeting, the two merge. fixVoiceprints: "wrong recognition": the meeting's
    // samples leave the PREVIOUS person's voiceprint and are added to the chosen one
    // (undo reverts this too).
    bool reassignSpeaker(const QString& speakerKey, const PersonChoice& person,
                         bool fixVoiceprints = false);
    // Resets the speaker to anonymous. fixVoiceprints: the meeting's samples leave the
    // previous person's voiceprint.
    bool revertSpeakerToAnonymous(const QString& speakerKey, bool fixVoiceprints = false);
    // Several bindings in ONE undo step (approving "Who was there?", "Only I spoke",
    // "They were not there"). For a whole raw speaker: bind to a person (merge if the person is
    // already a speaker) or set anonymous; for a line list the lines go to the person (to a new
    // participant if needed). An invisible / unchanged binding is skipped. false: nothing changed.
    bool applyBindings(const QVector<SpeakerBinding>& bindings, const QString& undoText);
    // All lines of fromKey go to intoKey; fromKey ceases to exist.
    bool mergeSpeakers(const QString& fromKey, const QString& intoKey);

    // Adds a participant (no lines yet). An anonymous choice = an anonymous participant. If the person is
    // already a speaker, returns that key (no step).
    QString addParticipant(const PersonChoice& person = {});
    // Removes an empty speaker (0 lines). false for a non-empty one.
    bool removeParticipant(const QString& speakerKey);

    // "Fine as is": confirms the lines (no longer uncertain). asNoisy: "Fine, but do not
    // use as a sample": the line also gets the "talked over each other" flag (not a voice sample).
    // Confirming also clears the "unidentifiable" flag. asNoisy=false -> lastSampleDeviation() updates.
    bool confirmUtterances(const QStringList& utteranceIds, bool asNoisy = false);
    // Manually sets the "talked over each other" flag (true: no voice sample; false: usable as
    // a sample even if the overlap rule would call it noisy).
    bool setUtterancesNoisy(const QStringList& utteranceIds, bool noisy);
    // "Unidentifiable" (ignored=true) / its undo (false): ONE undo step. The line's name (and its
    // corrected / confirmed flag) stays; the recheck flag is cleared. An ignored line is left out of
    // every branch of the voice analysis (uncertainty, recheck, pair review, "similar lines", review
    // groups, side learning, candidate profiles, voiceprint) and is never uncertain; it still counts
    // under its name in speaking time and the transcript. false: nothing changed.
    bool setUtterancesIgnored(const QStringList& utteranceIds, bool ignored);

    // ---- transparency of sample training (shared base of "To review") ----------------
    // Result of the last sample-giving decision (confirmUtterances asNoisy=false, moveUtterances*
    // noisy=false): of the decided lines, the one farthest from the target speaker's core (see
    // SampleDeviation; threshold: speakeredit::kSampleDeviationFit). Invalid (isValid()==false) if
    // it could not be measured (no embedding, the speaker has too few other lines), or another
    // edit / undo happened since. Emits sampleDeviationChanged on change.
    SampleDeviation lastSampleDeviation() const;
    // Sample meter: the sample lines of the visible speakers (in speakers() order; see SpeakerSampleStats).
    // Emits sampleStatsChanged on change.
    QVector<SpeakerSampleStats> sampleStats() const;

    // ---- recheck based on confirmed lines ----------------------
    // Measures the other lines against the "reliable" voice centroids built from the confirmed /
    // corrected lines (SpeakerAnalysis: computeUncertainRechecked). Lines found doubtful stay
    // uncertain (persisted in the overlay) until a correction or "Fine as is" comes; a newer
    // recheck replaces the set. One undo step (if anything changed).
    // For named speakers the person's stored voiceprints join the reference (SpeakerAnalysis:
    // SpeakerPrior): those made in this meeting as local evidence, earlier ones with a smaller,
    // capped weight (confirmed lines always dominate).
    using SpeakerReference = editing::SpeakerReference;
    using SimilarVoices = editing::SimilarVoices;
    using RecheckResult = RecheckService::RecheckResult;
    static QString referenceSummary(const QVector<SpeakerReference>& refs);
    static QString recheckBasisSummary(const RecheckResult& r);
    static QString similarVoicesNote(const QVector<SimilarVoices>& similar);
    RecheckResult recheckFromConfirmed();
    // Whether it can run: an embedding exists, the voice analysis is not running, and at least one
    // speaker has at least kMinSpeakerLines confirmed / corrected (embedded) lines.
    bool canRecheck() const;
    // If it cannot run: why (a sentence for the user); otherwise empty.
    QString recheckBlocker() const;

    // ---- pairwise review ("Review between A and B") ---------------------
    // Only A's and B's lines, based only on their two voices (SpeakerAnalysis: computePairRecheck):
    // the reference is built from the confirmed / corrected lines (+ the stored voiceprints, see
    // above); with few lines and no voiceprint, from all the speaker's clean lines; fallbackA/B
    // signals this. Lines found doubtful become uncertain, the suggestion (recheckHint) is the
    // other speaker; same persistence as the recheck (a correction / "Fine as is" clears it). It
    // replaces the earlier recheck flags of lines on A and B; other speakers' are untouched. One
    // undo step (if anything changed).
    using PairRecheckResult = RecheckService::PairRecheckResult;
    PairRecheckResult recheckPair(const QString& speakerKeyA, const QString& speakerKeyB);
    // If it cannot run now: why; otherwise empty.
    QString pairRecheckBlocker(const QString& speakerKeyA, const QString& speakerKeyB) const;

    // The offer (see PairRecheckOffer). Changes together with the suggestionChanged signal and
    // is dropped by the next edit / undo.
    bool hasPairOffer() const;
    PairRecheckOffer pairOffer() const;
    // "Not now": this pair is not offered again in this session.
    void declinePairOffer();
    void dismissPairOffer();

    // ---- suggestion ("N more lines sound like this voice") --------------------
    bool hasSuggestion() const;
    SpeakerSuggestion suggestion() const;
    bool acceptSuggestion();        // bulk move, one undo step
    void dismissSuggestion();

    // ---- undo / redo (for the open session, in memory) --------------------
    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;       // short description of the top step
    QString redoText() const;
    // For the adapter of the shared undo (editing/undo/MeetingUndo.h): the sequence numbers of the undoable
    // steps, bottom to top. A step's number is unique for the process life and the same after redo.
    QVector<quint64> undoSerials() const;

    // ---- utterance embeddings (background thread) -----------------------------
    bool embeddingsSupported() const;   // a factory exists (whether the model/audio exists is known at run time)
    bool embeddingsComplete() const;    // available for every sufficiently long line (or attempted)
    bool isEmbeddingRunning() const;
    QString embeddingError() const;     // error of the last run (empty = none)

    // ---- manual voiceprint ------------------------------------------------
    VoiceprintMaterial voiceprintMaterial(const QString& speakerKey) const;
    // A voiceprint from the speaker's lines HERE (longer lines, ~15-20 s). With too little
    // material it does not make a weak voiceprint: ok=false + missingMs. Not an undo step (deletable by printId).
    VoiceprintResult createVoiceprint(const QString& speakerKey);
    // A voiceprint from EXPLICITLY given lines (the line popup's "sample from this line"
    // button): the line belongs to the speaker, is not uncertain, not "talked over each other",
    // at least 3 s. A single long, clean line is enough (the 15 s minimum does not apply). Not an undo step.
    VoiceprintResult createVoiceprintFromLines(const QString& speakerKey, const QStringList& utteranceIds);
    // Undo of the manual creation: EXACTLY this voiceprint is deleted (the printId of
    // createVoiceprint). false if it no longer exists. Not an undo step either.
    bool removeVoiceprint(const QString& printId);

    // ---- manual track assignment ("Which track do they speak on?") ------------------
    // The speaker's tracks (Track::id); empty = no constraint. When given, the single truth for the
    // person's side (SideAnalysis); it also provides the people.json default learned from confirmed lines.
    QStringList trackAssignment(const QString& speakerKey) const;
    // One undo step. Ids not among the meeting's tracks are dropped; an empty list = clears the
    // constraint. false: nothing changed / unknown speaker.
    bool setTrackAssignment(const QString& speakerKey, const QStringList& trackIds);

    // ---- evidence, candidates, "To review" groups ----------------
    // Source of the track activity (called on a background thread!). Default: only the tracks.activity.bin
    // cache (with both a mic and a loopback track). AppController installs the computing source
    // (processing::loadOrComputeSideActivity: cache, else decode via the media tool and save).
    using ActivityProvider = SpeakerEditSession::ActivityProvider;
    void setActivityProvider(ActivityProvider provider);
    // The person's MANUAL tags (TagService::tagsOfPerson), only for the quick "+ #Tag" offer;
    // the candidate ranking's evidence comes from setTagEvidenceProvider. Called on the main thread.
    using PersonTagsFn = SpeakerEditSession::PersonTagsFn;
    void setPersonTagsProvider(PersonTagsFn fn);
    // The meeting's tags (Meeting::tagIds, at load) and a person's tags from the source above
    // (empty if no source), for the quick "+ #Tag" offer.
    QStringList meetingTagIds() const;
    QStringList personTagIds(const PersonId& person) const;
    // The person's tags changed (e.g. quick "+ #Tag"): recompute the candidates / evidence.
    void notifyPersonTagsChanged();
    // The tag evidence (manual tag + learned association; PersonTagSnapshot::evidence). Called
    // on the main thread, once per person per analysis.
    void setTagEvidenceProvider(library::TagEvidenceFn fn);

    // The groups of the latest background analysis (reviewGroupsChanged signals a refresh). Recomputed
    // by itself after every edit / undo / embedding run.
    QVector<ReviewGroup> reviewGroups() const;
    bool isReviewRunning() const;
    // Carries out the group's suggestion in ONE undo step (ContaminatedCore: split).
    // ShortLines (informational only) / unknown id -> false.
    bool applyReviewGroup(const QString& groupId);
    // Splits the speaker's confirmed core (ContaminatedCore): the smaller half goes to the best
    // candidate, or to a new anonymous participant. One undo step; false if the core is not contaminated.
    bool splitSpeaker(const QString& speakerKey);

    // Candidates for one line (the current speaker is included) / for a whole speaker (not itself),
    // by score. Computed on the latest analysis context.
    QVector<Candidate> lineCandidates(const QString& utteranceId) const;
    QVector<Candidate> speakerCandidates(const QString& speakerKey) const;
    // "Why not X?": evidence against the line's current speaker (with a fix link).
    QVector<Evidence> whyNot(const QString& utteranceId) const;
    // "Why them?": evidence for / against the speaker.
    QVector<Evidence> speakerEvidence(const QString& speakerKey) const;
    // The latest analysis's pairs of speakers with very similar voices (descending similarity).
    QVector<SimilarVoices> similarVoices() const;
    // The latest side analysis (inactive until it has run / there are no two sides).
    SideReport sideReport() const;

    // ---- summary staleness / re-transcription ----------------------------
    SummaryStaleInfo summaryStale() const;
    RetranscribeImpact retranscribeImpact() const;

    // Corrections (overlay, meeting.json) go to disk immediately; regenerating transcript.md,
    // however, runs batched with a short delay (growing with the meeting's length).
    // This writes it out at once if an update is pending (exit, export, test). The destructor
    // calls it too.
    void flushPendingWrites();

    // The parts behind the facade (for callers that already move to them; P15).
    SpeakerEditSession* session() const;
    RecheckService* recheck() const;

public slots:
    void undo();
    void redo();

    // Computes embeddings in the background (for those missing from the cache). embeddingProgress /
    // embeddingFinished signals; at the end the uncertainty flags refresh.
    void startEmbedding();
    void cancelEmbedding();

    // "Fine as is": dismisses the stale flag without regenerating.
    void dismissSummaryStale();
    // The summary was (re)made, so the stale flag is cleared. (Called by AppController.)
    void notifySummaryRegenerated();

    // External change (old UI rename, auto-identification, person rename): rereads the speakerMap
    // and the overlay from disk. The undo stack stays if the overlay did not change.
    void refreshFromDisk();
    // A new transcript was made: full reload, the undo stack and the suggestion dropped (reloaded signal).
    void reloadTranscript();

    // Recomputes the "To review" groups in the background (reviewGroupsChanged at the end).
    void refreshReviewGroups();

signals:
    // The data of the listed utterances changed (speaker / flags / uncertainty); the list model
    // emits dataChanged for them.
    void utterancesChanged(QStringList utteranceIds);
    // The speaker list or its data (name, counters, voiceprint flag) changed.
    void speakersChanged();
    void uncertainCountChanged(int count);
    void undoStateChanged();
    // A suggestion arrived or disappeared (hasSuggestion() / suggestion()).
    void suggestionChanged();
    void embeddingRunningChanged(bool running);
    void embeddingProgress(int done, int total);
    void embeddingFinished(bool complete);
    void summaryStaleChanged(bool stale, int correctedSpeakers);
    // Everything changed (new transcript / external overlay change): model reset.
    void reloaded();
    // A recheck ran (the view may switch to the "Uncertain" filter).
    void recheckFinished(int flagged, int speakersWithConfirmedCore, int confirmedLines);
    // A background analysis finished: reviewGroups(), the candidates and the side analysis refreshed.
    void reviewGroupsChanged();
    void reviewRunningChanged(bool running);
    // lastSampleDeviation() changed (a new flag, or the old one lost its validity).
    void sampleDeviationChanged();
    // sampleStats() changed.
    void sampleStatsChanged();

    // Side effects toward the other components (AppController translates them into its own signals).
    void speakerMapChanged(QString meetingId);
    void peopleChanged();
    void voiceprintsChanged();

private:
    std::unique_ptr<SpeakerEditSession> m_session;
    std::unique_ptr<RecheckService> m_recheck;
};

} // namespace tanara::editing