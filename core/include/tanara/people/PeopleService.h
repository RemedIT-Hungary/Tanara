#pragma once
//
// PeopleService: the backend of the People window (UI-independent): the list of people with the
// extra data, voice samples with a source description, and the operations (new person, rename,
// nicknames, note, delete / move a sample / new person from a sample, voiceprint from manually
// assigned meetings, merge, delete) plus undo.
//
// What writes where:
//  - people.json: one record per person (name, nicknames, note) (PeopleStore);
//  - voiceprints.json: voiceprints bound to a name (VoiceprintStore);
//    both through the stores (lock + reread + atomic write);
//  - meetings: rename / merge / delete go through AppController's existing global operations
//    (renamePerson / setUserSpeakerName / removePerson), which carry through every meeting's
//    speakerMap, track name and manual participants.
//
// What affects who the speakers are (merge, delete, moving a sample) marks the affected meetings'
// summary stale with the existing mechanism (speakeredit::markSummaryStale), and emits the usual
// signals on the AppController (peopleChanged, voiceprintsChanged, speakerMapChanged,
// summaryStaleChanged), so open windows refresh without a restart.
//
// Undoable: deleting a sample, moving a sample (and "new person from this sample"), deleting a
// nickname, rename. NOT undoable: merge, deleting a person: they rewrite every affected meeting; the stack is cleared then.
#include "tanara/Types.h"
#include "tanara/edit/SpeakerEditTypes.h"
#include "tanara/edit/UtteranceEmbeddings.h"

#include <QDate>
#include <QObject>
#include <QStringList>
#include <QVector>

#include <memory>

namespace tanara {

class AppController;
struct SideReport;
class MeetingStore;
class PeopleStore;
class PeopleStats;
class VoiceprintStore;

struct PersonRecord {
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
    QString person;
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
    QString name;           // name of the affected / created person
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

enum class PeopleUndoKind { None, SampleRemoved, SampleMoved, AliasRemoved, Renamed };

struct PeopleUndoInfo {
    PeopleUndoKind kind = PeopleUndoKind::None;
    QString person;     // the person the change is visible at (worth selecting after an undo)
    QString detail;     // sample: the meeting's title; nickname: the nickname; rename: the new name
};

class PeopleService : public QObject {
    Q_OBJECT
public:
    PeopleService(AppController* controller, MeetingStore* store, PeopleStore* people,
                  VoiceprintStore* voiceprints, PeopleStats* stats, QObject* parent = nullptr);
    ~PeopleService() override;

    // Factory of the utterance embedder for the "sample from meetings" operation. By default the real
    // one (if there is a voice model); tests inject a fake.
    void setEmbedderFactory(UtteranceEmbedderFactory factory);

    // ---- reading (fast: does not read meetings) ----
    // The fresh state on disk (another process may write the two files too).
    void reload();
    QString selfName() const;
    bool isSelf(const QString& name) const;
    // people.json + the voiceprint DB + the own name; unordered.
    QVector<PersonRecord> persons() const;
    PersonRecord person(const QString& name) const;
    bool exists(const QString& name) const;
    // The person's samples, newest first.
    QVector<VoiceSample> samples(const QString& name) const;
    // Similarity of two people's voices from the stored voiceprints (cosine of the best matching pair,
    // clamped to 0..1); -1 if either has no voiceprint.
    double similarity(const QString& a, const QString& b) const;

    // ---- operations ----
    PeopleOpResult addPerson(const QString& name);
    PeopleOpResult renamePerson(const QString& oldName, const QString& newName);
    PeopleOpResult addAlias(const QString& name, const QString& alias);
    bool removeAlias(const QString& name, const QString& alias);
    void setNote(const QString& name, const QString& note);
    // Refreshes the learned track side from an analysis (SideAnalysis: sides::learnedDefaults: the
    // Local / Remote that comes from confirmed lines or a manual assignment). Returns how many people's
    // changed. Not undoable (a learned value; the next analysis overwrites it).
    int learnDefaultSides(const SideReport& report);
    // Manual override of the track side (People window: "Typically: local / remote / no data"):
    // "local" | "remote" is a protected manual value; empty = delete (learning may write it again).
    // true if changed (changed signal).
    bool setDefaultSide(const QString& name, const QString& side);

    PeopleOpResult removeSample(const QString& printId);
    // The sample moves to `toName`. If there is no such person, one is created ("new person from this
    // sample"). The summary of the sample's source meeting becomes stale.
    PeopleOpResult moveSample(const QString& printId, const QString& toName);

    VoiceprintPlan voiceprintPlan(const QString& name) const;
    // One sample from a meeting (with the existing SpeakerEditor::createVoiceprint logic).
    VoiceprintResult createVoiceprintFromMeeting(const QString& name, const QString& meetingId);

    MergePreview mergePreview(const QString& loser, const QString& survivor) const;
    // Every meeting, sample and nickname of `loser` goes to `survivor`; the `loser` name becomes a
    // nickname. Not undoable.
    PeopleOpResult merge(const QString& loser, const QString& survivor);

    DeletePreview deletePreview(const QString& name) const;
    // Deletes the person from every meeting (an anonymous speaker remains). keepSamples: their
    // samples go to a new person named "Anonymous N" (result.name), who shows in the list and can be
    // renamed or merged later. The own person cannot be deleted. Not undoable.
    PeopleOpResult removePerson(const QString& name, bool keepSamples);

    // ---- undo ----
    bool canUndo() const;
    PeopleUndoInfo undoInfo() const;    // the top step
    PeopleUndoInfo undo();              // undoes; kind == None if there was nothing to undo

signals:
    // The list or a person's data changed (the view model rebuilds).
    void changed();
    void undoChanged();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::VoiceSample)
