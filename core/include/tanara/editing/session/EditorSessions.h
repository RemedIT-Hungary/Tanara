#pragma once
//
// EditorSessions: the transcript editor's sessions, one SpeakerEditor per meeting (cleanup phase 2, P12f;
// moved unchanged from AppController). It creates an editor on demand (wired to the meeting's shared undo),
// keeps it while open, and relays its side effects (speaker map, people, voiceprints, the summary's
// staleness) as its own signals; changes from outside (another view's rename, an auto-identification, a
// person rename, a new summary / transcript) reach the open editor through the slots.
// The summary's staleness bookkeeping lives here too: an open editor answers from memory, otherwise the
// overlay files on disk do.
// The voice side of an editor (model ids, embedder factory, track activity, tag evidence) is injected by
// the app (configure), so the editing module does not depend on processing.
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
class SpeakerEditor;

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
        // The voice side of a new editor (model ids, embedder factory, activity / tag-evidence / person-tags
        // providers).
        std::function<void(SpeakerEditor*)> configure;
    };

    explicit EditorSessions(Deps deps, QObject* parent = nullptr);
    ~EditorSessions() override;

    // The meeting's session (created on demand; nullptr for an unknown meeting).
    SpeakerEditor* editor(const QString& meetingId);
    // The open session, or nullptr (never creates one).
    SpeakerEditor* existing(const QString& meetingId) const;
    // Closes the session (the editor is deleted later).
    void close(const QString& meetingId);
    // Runs fn on every open editor (e.g. the speaker models changed).
    void forEach(const std::function<void(SpeakerEditor*)>& fn) const;

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
    Deps m_deps;
    QHash<QString, QPointer<SpeakerEditor>> m_editors;
    bool m_emitting = false;   // an editor's own speakerMap signal is going out (do not reload it)
};

} // namespace tanara::editing
