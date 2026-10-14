#pragma once
//
// PeopleService: the backend of the People window (UI-independent): the list of people with the
// extra data, voice samples with a source description, and the operations (new person, rename,
// nicknames, note, delete / move a sample / new person from a sample, voiceprint from manually
// assigned meetings, merge, delete) plus undo.
//
// People are identified by PersonId (domain/Person.h); the name is only a field of the record.
// What writes where:
//  - people.json: one record per person (name, nicknames, note) (PeopleStore);
//  - voiceprints.json: voiceprints bound to the PersonId (VoiceprintStore);
//    both through the stores (lock + reread + atomic write);
//  - rename: ONE field (renameEverywhere; for the own person the settings' name too);
//  - merge / delete: mergeEverywhere / removeEverywhere rewrite the references (voiceprints,
//    tag rejections, every meeting's speakerMap / participants / overlay).
// These global person operations (formerly AppController::renamePerson / removePerson / mergePeople /
// undoMergePeople) live here; the AppController facade delegates to them.
//
// The meeting-side files beyond meeting.json (speaker overlay, summary stale marks, transcript.md) and
// the voiceprint material come through two ports that the editing module implements
// (PersonReferenceEditor, VoiceprintMaterialSource; wired by app::Services): library does not depend on
// editing (cleanup plan T 1.2).
//
// What affects who the speakers are (merge, delete, moving a sample) marks the affected meetings'
// summary stale with the existing mechanism (speakeredit::markSummaryStale), and emits the usual
// signals (peopleChanged, voiceprintsChanged, speakerMapChanged, summaryStaleChanged,
// participantsChanged; the AppController facade re-emits them), so open windows refresh without a restart.
//
// Undoable: deleting a sample, moving a sample (and "new person from this sample"), deleting a
// nickname, rename, merge (the merge's exact reference list goes on the stack: PersonMergeUndo).
// NOT undoable: deleting a person (the stack is cleared then).
#include "tanara/domain/Meeting.h"
#include "tanara/domain/Person.h"
#include "tanara/domain/Voice.h"
#include "tanara/library/people/PersonMerge.h"
#include "tanara/library/people/VoiceprintMaterialSource.h"

#include <QDate>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace tanara::editing { struct SideReport; }

namespace tanara::library {

class MeetingRepository;
class PeopleStore;
class PeopleStats;
class PersonReferenceEditor;
class TagService;
class VoiceprintStore;

// Makes sure the user's own person exists in people.json ("self"), named `selfName` (no-op for an empty
// name, a legacy store, or an existing self).
void ensureSelfPerson(PeopleStore* people, const QString& selfName);

struct PersonRecord {
    PersonId    id;
    QString     name;
    QStringList aliases;
    QString     note;
    bool        isSelf = false;
    int         sampleCount = 0;
    QString     defaultSide;    // learned track side: "local" | "remote" | empty (people.json)
    bool        defaultSideManual = false;   // overwritten by the user (learning does not overwrite it)
};

// A voice sample (voiceprint) ready for display.
struct VoiceSample {
    QString id;
    PersonId person;
    QString sourceKind;     // "mic" | "call" | "system" | "mix" | "unknown"
    QString deviceLabel;    // friendly device / track name; empty = unknown
    QString meetingId;
    QString meetingTitle;   // empty if the meeting no longer exists
    QDate   recordedAt;     // the meeting's day (failing that, the sample's creation)
    qint64  startMs = 0;    // position of the sample in the audio file (file time: the track offset subtracted)
    qint64  endMs = 0;
    QString audioPath;      // absolute path; empty if it cannot be resolved
    bool    audioExists = false;
    qint64 durationMs() const { return endMs > startMs ? endMs - startMs : 0; }
};

struct PeopleOpResult {
    bool    ok = false;
    QString error;          // human error message if !ok
    PersonId person;        // the affected / created person
    QString name;           // their name
    int     meetings = 0;   // affected meetings
    int     samples = 0;    // affected samples
    int     staleSummaries = 0;   // summaries marked stale
};

struct MergePreview {
    int meetingCount = 0;       // union of the two people's meetings
    int sampleCount = 0;
    int staleSummaries = 0;     // this many summaries become stale (in the meetings of the name that disappears)
};

struct DeletePreview {
    int     meetingCount = 0;
    int     sampleCount = 0;
    int     staleSummaries = 0;
    QString exampleLabel;       // an anonymous label it becomes (e.g. "Speaker 2"); may be empty
};

// What a voiceprint can be made from, for someone who has none yet.
struct VoiceprintPlan {
    bool        modelAvailable = false; // a voice model exists
    int         meetingsWithLines = 0;  // meetings where they have lines
    QStringList usableMeetingIds;       // where a sure, long line is enough for one sample
    qint64      usableMs = 0;           // this much audio of those gets used in total
};

enum class PeopleUndoKind { None, SampleRemoved, SampleMoved, AliasRemoved, Renamed, Merged };

struct PeopleUndoInfo {
    PeopleUndoKind kind = PeopleUndoKind::None;
    PersonId person;    // the person the change is visible at (worth selecting after an undo)
    QString detail;     // sample: the meeting's title; nickname: the nickname; rename: the new name; merge: the dropped name
};

class PeopleService : public QObject {
    Q_OBJECT
public:
    struct Deps {
        MeetingRepository* store = nullptr;
        PeopleStore*       people = nullptr;
        VoiceprintStore*   voiceprints = nullptr;
        PeopleStats*       stats = nullptr;
        TagService*        tags = nullptr;        // person tag rejections (merge / delete)
        // The ports implemented by the editing module (may be null: then the meeting-side files are not
        // touched and no voiceprint can be made from a meeting).
        PersonReferenceEditor*    references = nullptr;
        VoiceprintMaterialSource* material = nullptr;
        // The settings' own name ("Your name"): read, and written when the own person is renamed / merged.
        std::function<QString()> selfName;
        std::function<void(const QString&)> setSelfName;
    };

    explicit PeopleService(const Deps& deps, QObject* parent = nullptr);
    ~PeopleService() override;

    // Factory of the utterance embedder for the "sample from meetings" operation. By default the real
    // one (if there is a voice model); tests inject a fake. Forwarded to the VoiceprintMaterialSource.
    void setEmbedderFactory(VoiceprintMaterialSource::EmbedderFactory factory);

    // ---- reading (fast: does not read meetings) ----
    // The fresh state on disk (another process may write the two files too).
    void reload();
    PersonId selfId() const;
    bool isSelf(const PersonId& person) const;
    // The user's own person with the settings' name.
    SelfPerson selfPerson() const;
    // ensureSelfPerson with the settings' name.
    void ensureSelf();
    // Sets the user's own (mic track) speaker name in the settings AND on the own person's record
    // (created if missing); the meetings showing it are refreshed (renameEverywhere). peopleChanged.
    void setSelfName(const QString& name);
    // The current display names (people.json, reread).
    PersonNames personNames() const;
    // Every display name (people.json, reread), for autocomplete.
    QStringList knownNames() const;
    // Every person of people.json; unordered.
    QVector<PersonRecord> persons() const;
    PersonRecord person(const PersonId& person) const;   // unknown: an invalid record (null id)
    bool exists(const PersonId& person) const;
    // The people with this name (case-insensitive; two people may share a name).
    PersonIds findByName(const QString& name) const;
    // The person's samples, newest first.
    QVector<VoiceSample> samples(const PersonId& person) const;
    // Similarity of two people's voices from the stored voiceprints (cosine of the best matching pair,
    // clamped to 0..1); -1 if either has no voiceprint.
    double similarity(const PersonId& a, const PersonId& b) const;

    // ---- operations ----
    // A new person. The People window keeps names unique: an existing name is an error.
    PeopleOpResult addPerson(const QString& name);
    // Rename = one field; the old name stays as a nickname. An existing name (another person's) is an error
    // ("use Merge"), as before.
    PeopleOpResult renamePerson(const PersonId& person, const QString& newName);
    PeopleOpResult addAlias(const PersonId& person, const QString& alias);
    bool removeAlias(const PersonId& person, const QString& alias);
    void setNote(const PersonId& person, const QString& note);
    // Refreshes the learned track side from an analysis (SideAnalysis: sides::learnedDefaults: the
    // Local / Remote that comes from confirmed lines or a manual assignment). Returns how many people's
    // changed. Not undoable (a learned value; the next analysis overwrites it).
    // (Defined by the editing module, which owns SideReport: it computes sides::learnedDefaults and
    // calls the overload below.)
    int learnDefaultSides(const editing::SideReport& report);
    // The same with the learned sides ("local" | "remote" per person) already computed.
    int learnDefaultSides(const QHash<PersonId, QString>& learned);
    // Manual override of the track side (People window: "Typically: local / remote / no data"):
    // "local" | "remote" is a protected manual value; empty = delete (learning may write it again).
    // true if changed (changed signal).
    bool setDefaultSide(const PersonId& person, const QString& side);

    PeopleOpResult removeSample(const QString& printId);
    // The sample moves to the chosen person; PersonChoice::newName creates one ("new person from this
    // sample"). The summary of the sample's source meeting becomes stale.
    PeopleOpResult moveSample(const QString& printId, const PersonChoice& to);

    VoiceprintPlan voiceprintPlan(const PersonId& person) const;
    // One sample from a meeting (with the existing SpeakerEditor::createVoiceprint logic, through the
    // VoiceprintMaterialSource port).
    VoiceprintResult createVoiceprintFromMeeting(const PersonId& person, const QString& meetingId);

    MergePreview mergePreview(const PersonId& loser, const PersonId& survivor) const;
    // Every meeting, sample and nickname of `loser` goes to `survivor`; the `loser` name becomes a
    // nickname. One undo step (PeopleUndoKind::Merged).
    PeopleOpResult merge(const PersonId& loser, const PersonId& survivor);

    DeletePreview deletePreview(const PersonId& person) const;
    // Deletes the person from every meeting (an anonymous speaker remains). keepSamples: their
    // samples go to a new person named "Anonymous N" (result.person / result.name), who shows in the list
    // and can be renamed or merged later. The own person cannot be deleted. Not undoable.
    PeopleOpResult removePerson(const PersonId& person, bool keepSamples);

    // ---- undo ----
    bool canUndo() const;
    PeopleUndoInfo undoInfo() const;    // the top step
    PeopleUndoInfo undo();              // undoes; kind == None if there was nothing to undo

    // ---- global person operations (no undo stack of their own; the AppController facade delegates) ----
    // People are referred to by PersonId everywhere, so:
    //  - rename: writes the person's name field (people.json; for the own person the settings' name too);
    //    the meetings that show the person get their transcript.md regenerated and speakerMapChanged;
    //    peopleChanged;
    //  - remove: deletes the record, the voiceprints, the tag rejections and every meeting reference (the
    //    speakers become anonymous); peopleChanged;
    //  - merge: every reference to `drop` points to `keep` afterwards (voiceprints, tag rejections, every
    //    meeting's speakerMap / participants / approval / overlay), `drop`'s record folds into `keep`'s
    //    (its name becomes a nickname). Returns what changed for undoMergeEverywhere (invalid: nothing).
    void renameEverywhere(const PersonId& person, const QString& newName);
    void removeEverywhere(const PersonId& person);
    PersonMergeUndo mergeEverywhere(const PersonId& keep, const PersonId& drop);
    bool undoMergeEverywhere(const PersonMergeUndo& merge);
    // Whether the meeting refers to the person anywhere (speakerMap, participants, approval, overlay).
    bool meetingRefersTo(const Meeting& m, const PersonId& person) const;
    // transcript.md with the current names + speakerMapChanged (after a person change).
    void refreshMeetingNames(const Meeting& m);

signals:
    // The list or a person's data changed (the view model rebuilds).
    void changed();
    void undoChanged();
    // The app-wide notifications (re-emitted by the AppController facade).
    void peopleChanged();
    void voiceprintsChanged();
    void speakerMapChanged(QString meetingId);
    void summaryStaleChanged(QString meetingId);
    void participantsChanged(QString meetingId);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace tanara::library
Q_DECLARE_METATYPE(tanara::library::VoiceSample)
