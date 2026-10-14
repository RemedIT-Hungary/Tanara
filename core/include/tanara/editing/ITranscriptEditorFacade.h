#pragma once
//
// ITranscriptEditorFacade: what the transcript editor's view model (TranscriptEditorViewModel, the
// "Transcript" tab) needs from the application besides the editing session itself: the session of a
// meeting (SpeakerEditSession + its RecheckService), the meeting's tracks, the per-meeting view state
// (the review rail), the person list of the pickers, the tag side of the candidates and of the new
// person's question, and the auto-play switch of the "To review" view.
//
// The real implementation (gui/qml/src/TranscriptEditorServicesFacade) stands on
// editing::EditorSessions and the stores (one shared instance per application); the fictional one
// (demo/TranscriptEditorDemoFacade) builds a sample meeting in a temporary folder with a REAL session
// on it (one instance per view model and variant), for --demo, --gallery, --qml-shot and the tests.
//
// It is a QObject only because it carries a signal. The view model never learns which implementation
// it talks to; live() only says whether what it does is kept (a fictional facade keeps the view
// state and the switches in memory).
#include "tanara/domain/Person.h"
#include "tanara/domain/Meeting.h"
#include "tanara/editing/session/SpeakerEditTypes.h"
#include "tanara/library/tags/PersonTags.h"

#include <QObject>
#include <QString>
#include <QVector>

namespace tanara::editing {

class RecheckService;
class SpeakerEditSession;

class ITranscriptEditorFacade : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~ITranscriptEditorFacade() override = default;

    // False: a fictional meeting (nothing outside its temporary folder is touched).
    virtual bool live() const = 0;

    // One meeting's editing session as the editor sees it. Both pointers are null if the meeting
    // has no editable session (unknown meeting, empty id); they live as long as the session is
    // open (the session's destroyed() signal tells the end).
    struct Session {
        SpeakerEditSession* session = nullptr;
        RecheckService*     recheck = nullptr;
        // The meeting HAS a transcript but in the old format (no utterance list): not editable.
        bool    legacyTranscript = false;
        QString legacyText;     // transcript.md as plain text (for the read-only view)
    };
    // The meeting's session (created on demand). A fictional facade serves its sample meeting
    // whatever the id.
    virtual Session open(const QString& meetingId) = 0;
    // The meeting's tracks (the "Which track does the speaker use?" chips); empty if unknown.
    virtual QVector<Track> tracks(const QString& meetingId) const = 0;

    // The review rail's remembered visibility per meeting (default hidden).
    virtual bool railVisible(const QString& meetingId) const = 0;
    virtual void setRailVisible(const QString& meetingId, bool visible) = 0;

    // The person pickers' list (people + voiceprint state + meeting counts).
    virtual QVector<PersonInfo> people() const = 0;

    // ---- tags ----
    // A tag's display name; empty if unknown.
    virtual QString tagName(const QString& tagId) const = 0;
    // The user's own person (excluded from every tag rule); null if unknown.
    virtual PersonId selfId() const = 0;
    // The quick "+ #Tag": puts the tag on the person (one step on the tag undo stack). False: nothing happened.
    virtual bool addPersonTag(const PersonId& person, const QString& tagId) = 0;
    // A new person on a tagged meeting: the tag questions to ask (marked as asked, so they come only
    // once); empty: nothing to ask. `name` is the person's display name.
    virtual QVector<library::PersonTagQuestion> newPersonTagQuestions(const QString& meetingId,
                                                                      const PersonId& person,
                                                                      const QString& name) = 0;
    // The answer to a question (yes: the tag goes on them; no: a lasting refusal): one tag-stack step.
    virtual void answerTagQuestion(const library::PersonTagQuestion& question, bool yes) = 0;
    // Undoes the answer just given (the tag stack's latest step).
    virtual void undoTagAnswer() = 0;

    // The "To review" view's auto-play switch (settings; a fictional facade keeps it in memory).
    virtual bool reviewAutoPlay() const = 0;
    virtual void setReviewAutoPlay(bool on) = 0;

    // ---- demo staging (a fictional facade only; the real one has none) ----
    // Prepares a design state on the sample meeting knowing its script (confirmed cores, markers,
    // the new person's tag question ...). False: nothing to prepare.
    virtual bool stage(const QString& /*state*/) { return false; }
    // For the "suggestion" design state: this line moved to this speaker brings up the suggestion.
    virtual QString suggestionSeedUtteranceId() const { return {}; }
    virtual QString suggestionTargetKey() const { return {}; }

signals:
    // The person list or the voiceprints changed (the pickers re-read people()).
    void peopleChanged();
};

} // namespace tanara::editing
