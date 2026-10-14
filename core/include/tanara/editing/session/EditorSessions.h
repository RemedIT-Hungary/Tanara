#pragma once
//
// EditorSessions: the transcript editor's sessions, one SpeakerEditSession (+ its RecheckService) per meeting
// (cleanup phase 2, P12f; moved unchanged from AppController). It creates a session on demand (wired to the
// meeting's shared undo), keeps it while open, and relays its side effects (speaker map, people, voiceprints, the summary's
// staleness) as its own signals; changes from outside (another view's rename, an auto-identification, a
// person rename, a new summary / transcript) reach the open editor through the slots.
// The summary's staleness bookkeeping lives here too: an open editor answers from memory, otherwise the
// overlay files on disk do.
// The voice side of a session (model ids, embedder factory, track activity, tag evidence, the voice models'
// display names) is injected by the app (configure), so the editing module does not depend on processing.
// The RecheckService is a child of its session: it stops (and saves the embedding cache) on the session's
// closing() signal and is deleted with it.
#include "tanara/domain/Meeting.h"
#include "tanara/domain/Person.h"
#include "tanara/domain/Summary.h"
#include "tanara/editing/session/SpeakerEditTypes.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include <functional>

namespace tanara::library {
class MeetingRepository;
class PeopleStore;
class VoiceprintStore;
}

namespace tanara::editing {

class MeetingUndoRegistry;
class RecheckService;
class SpeakerEditSession;

class EditorSessions : public QObject {
    Q_OBJECT
public:
    struct Deps {
        library::MeetingRepository* store = nullptr;
        library::PeopleStore*       people = nullptr;        // may be null
        library::VoiceprintStore*   voiceprints = nullptr;   // may be null
        MeetingUndoRegistry*        undo = nullptr;          // the shared undo follows from the editor's first step
        std::function<SelfPerson()>  self;
        std::function<PersonNames()> personNames;            // the current display names (targeted staleness)
        // The voice side of a new session (model ids, embedder factory, activity / tag-evidence / person-tags
        // providers, the default activity source, the model display names).
        std::function<void(SpeakerEditSession*, RecheckService*)> configure;
    };

    explicit EditorSessions(Deps deps, QObject* parent = nullptr);
    ~EditorSessions() override;

    // The meeting's session (created on demand; nullptr for an unknown meeting).
    SpeakerEditSession* editor(const QString& meetingId);
    // The voice analysis of the meeting's session (created with the session on demand; nullptr for an
    // unknown meeting).
    RecheckService* recheck(const QString& meetingId);
    // The open session, or nullptr (never creates one).
    SpeakerEditSession* existing(const QString& meetingId) const;
    // Closes the session (the session and its recheck service are deleted later).
    void close(const QString& meetingId);
    // Runs fn on every open session (e.g. the speaker models changed).
    void forEach(const std::function<void(SpeakerEditSession*, RecheckService*)>& fn) const;

    // ---- the summary's staleness ----
    SummaryStaleInfo summaryStale(const QString& meetingId) const;
    // The same for an already loaded meeting (no targeted part; no disk read for meeting.json).
    SummaryStaleInfo summaryStale(const Meeting& meeting) const;
    QVector<SummaryStatement> summaryStatements(const QString& meetingId) const;
    RetranscribeImpact retranscribeImpact(const QString& meetingId) const;
    void dismissSummaryStale(const QString& meetingId);

public slots:
    // A speaker-map change from outside (not the editor's own): the open editor rereads the disk.
    void externalSpeakerMapChanged(const QString& meetingId);
    // The voiceprints changed: the open editors refresh their speakers.
    void notifyVoiceprintsChanged();
    // A new summary clears the stale flag.
    void summaryRegenerated(const QString& meetingId);
    // A new transcript drops the manual line corrections (the utterance boundaries changed).
    void transcriptReplaced(const QString& meetingId);

signals:
    void speakerMapChanged(QString meetingId);
    void peopleChanged();
    void voiceprintsChanged();
    void summaryStaleChanged(QString meetingId);

private:
    struct Entry {
        QPointer<SpeakerEditSession> session;
        QPointer<RecheckService>     recheck;
    };
    Entry* open(const QString& meetingId);   // the entry of the meeting (created on demand), or nullptr
    SpeakerEditSession* sessionOf(const QString& meetingId) const;

    Deps m_deps;
    QHash<QString, Entry> m_editors;
    bool m_emitting = false;   // an editor's own speakerMap signal is going out (do not reload it)
};

} // namespace tanara::editing
