#pragma once
//
// TagService: manages the tag set and the per-meeting tags (UI-independent).
//
// Storage:
//  - the set: <metadataDir>/tags.json
//      { "version": 1,
//        "tags":     [ {"id", "name", "createdAt", "lastUsedAt"?, "aliases"?: [...]}, ... ],
//        "rejected": [ {"meetingId", "tagId"?, "name"?}, ... ] }
//    The person-level items of "rejected": {"person", "tagId", "meetingId"?}: without meetingId a
//    lasting (person, tag) rejection, with a meetingId only for that meeting.
//    {"person", "meetingId"} without a tag: the person's suggestion rejected for that meeting
//    (the x of the participant suggestion, rejectPersonForMeeting).
//    written atomically, under an inter-process lock (the recorder process may write it too; see
//    store/SharedFile.h). "aliases" are the old names of merged tags: typing one offers the kept tag.
//  - the assignment: "tags" in meeting.json (Meeting::tagIds), via MeetingStore::saveMeeting.
//
// Undo: every modifying call is one step (a group, beginGroup/endGroup, is one step together). A
// step is the "before" snapshot of the affected state (set + rejections + the affected meetings'
// tags); undo() restores it. At most 50 steps.
//
// The meetings' tags (and title, date, participants) are cached lazily from the store, per
// meeting; the store's signals refresh them.
//
// Tags on people (see tags/PersonTags.h): manual tags live in the people.json records
// (PeopleStore::setTags), the tag set is shared. Person-tag operations are in the same undo
// stack (a step also saves the affected people's tags). Deleting / merging a tag carries through
// to the people (in one step). The learned association's statistics are computed from the row
// cache: after ensurePersonStats() on a background thread (the old snapshot stays valid until the
// new one is ready; personStatsChanged), before that the first read is synchronous (tests, CLI).
// The own person is left out of every rule.
#include "tanara/tags/PersonTags.h"
#include "tanara/tags/TagTypes.h"

#include <QHash>
#include <QObject>
#include <QVector>

#include <functional>
#include <memory>

namespace tanara {

class MeetingStore;
class MeetingProfiles;
class PeopleStore;

class TagService : public QObject {
    Q_OBJECT
public:
    enum class Sort { LastUsed, Alpha, MostUsed };

    // tagsFile: path of tags.json (typically <metadataDir>/tags.json).
    TagService(MeetingStore* store, const QString& tagsFile, QObject* parent = nullptr);
    ~TagService() override;

    // For the tag profile's characteristic phrases (without it topTerms is empty).
    void setProfiles(MeetingProfiles* profiles);

    // ---- set ----
    QVector<TagUsage> all(Sort sort = Sort::LastUsed) const;
    Tag tag(const QString& id) const;                          // empty id if unknown
    Tag byName(const QString& name) const;                     // tagKey match (old name too)
    QVector<Tag> similarNames(const QString& name, int limit = 3) const;   // nearDuplicate, without the exact key
    QVector<TagInputRow> inputRows(const QString& typed, int limit = 5) const;
    // Index of the preselected row in the rows (SIMILAR EXISTS -> the existing one; otherwise the first).
    static int preferredRow(const QVector<TagInputRow>& rows);
    int meetingCount(const QString& tagId) const;
    int totalMeetings() const;

    Tag  create(const QString& name);                          // for an existing key, the existing one
    bool rename(const QString& id, const QString& name);       // false: the key belongs to another / empty name
    void merge(const QString& fromId, const QString& keepId);
    void remove(const QString& id);

    // ---- meetings ----
    QStringList tagsOf(const QString& meetingId) const;        // in the meeting's order
    Tag  addTag(const QString& meetingId, const QString& nameOrId, TagSource source = TagSource::Manual);
    void removeTag(const QString& meetingId, const QString& id);
    void setTags(const QString& meetingId, const QStringList& ids, TagSource source = TagSource::Manual);
    void bulkAdd(const QStringList& meetingIds, const QString& id);
    void bulkRemove(const QStringList& meetingIds, const QString& id);
    QVector<Tag> recent(int limit = 5) const;
    QStringList meetingsWith(const QString& tagId) const;      // newest first
    MeetingRef meetingRef(const QString& meetingId) const;     // title + date from the cache
    // The tags of every tagged meeting (meetingId -> tagsOf), for the suggestion snapshot.
    QHash<QString, QStringList> taggedMeetings() const;

    // ---- derived ----
    TagProfile profile(const QString& id) const;
    QVector<QPair<QString, int>> cooccurring(const QString& id) const;   // by frequency
    // One-line text digest of the profile (for the LLM suggestion's candidate list).
    QString profileLine(const QString& id) const;

    // ---- people: manual tags ----
    // The store of manual tags (people.json). Without it the manual tag operations have no effect; the
    // learned statistics work.
    void setPeopleStore(PeopleStore* people);
    // The own person's name (queried on every call; empty = none).
    void setSelfNameProvider(std::function<QString()> provider);
    QString selfName() const;
    bool isSelf(const QString& name) const;

    QStringList tagsOfPerson(const QString& name) const;       // only existing tags, in order
    // false: empty / own name, or no PeopleStore. Unknown ids are dropped.
    bool setPersonTags(const QString& name, const QStringList& ids);
    Tag  addPersonTag(const QString& name, const QString& nameOrId);   // a non-existent name -> a new tag
    void removePersonTag(const QString& name, const QString& id);
    QStringList peopleWith(const QString& tagId) const;        // manual; sorted by name

    // ---- people: learned association + suggestions ----
    // Turns background mode on and precomputes the statistics (the app calls it at startup).
    void ensurePersonStats();
    bool personStatsBusy() const;
    // Requests a recompute (e.g. the own name changed); it follows row changes by itself.
    void invalidatePersonStats();
    // Compute NOW, on the caller's thread (CLI, tests; it loads the rows too). personStatsChanged.
    void refreshPersonStatsNow();
    // The current snapshot of the learned association (never null).
    std::shared_ptr<const PersonTagStats> personStats() const;
    // Everything the evidence needs, passable between threads.
    PersonTagSnapshot personTagSnapshot() const;
    // The tag's people (Tags window > People): manual + learned >= threshold; manual first, then by
    // number of shared meetings, ties by the latest shared meeting. The own person is left out.
    // limit <= 0: all.
    QVector<PersonTagStat> peopleStats(const QString& tagId, int limit = 50) const;
    // The person's tags with numbers: manual (even with 0 shared) + every c >= 1 tag; manual first.
    QVector<PersonTagStat> personTagStats(const QString& name) const;
    // 3.1: "Put it on them": learned >= threshold, not manually on them, not rejected, not excluded.
    QVector<PersonTagStat> suggestPeopleForTag(const QString& tagId, const QStringList& excludeNames = {},
                                               int limit = persontags::kMaxSuggestions) const;
    // 3.2: "Usually at these" (c >= 2 and (>= 30% of the tag's or >= 50% of the person's)).
    QVector<PersonTagStat> suggestTagsForPerson(const QString& name, int limit = persontags::kMaxSuggestions) const;
    // 3.3: a meeting tag from the participants' manual tags: t, if it is on >= 2 participants, or there is
    // a single non-own participant and it is on them; tags on the meeting and rejected there are dropped.
    // source = People, reason ReasonKind::PeopleTags (the names of the participants carrying the tag).
    QVector<TagSuggestion> suggestTagsForPeople(const QStringList& names, const QString& meetingId) const;
    // The same with the meeting's named participants: the speaker map and the own track
    // (MeetingLibrary::participantsOf), plus manual, calendar and approved participants,
    // so also before transcription.
    QVector<TagSuggestion> suggestMeetingTagsFromParticipants(const QString& meetingId) const;
    // The tag as identification evidence (see PersonTagSnapshot::evidence; meetingId: its own row
    // is left out of the numbers).
    TagEvidence tagEvidence(const QString& personName, const QStringList& meetingTagIds,
                            const QString& meetingId = QString()) const;

    // (person, tag) rejection; empty meetingId = lasting, otherwise only for that meeting.
    void rejectPersonTag(const QString& name, const QString& tagId, const QString& meetingId = QString());
    bool isRejectedForPerson(const QString& name, const QString& tagId, const QString& meetingId = QString()) const;

    // ---- person suggestion for a meeting (Overview > Participants) ----
    // The people learned from the meeting's tags (suggestPeopleForTag in tag order) who are not
    // among the participants yet (excludeNames) and are not rejected for this meeting (neither
    // rejectPersonForMeeting nor the (person, tag, meeting) rejection). A person once (with their
    // first tag). The own person is left out. limit <= 0: all.
    QVector<PersonTagStat> suggestPeopleForMeeting(const QString& meetingId, const QStringList& excludeNames = {},
                                                   int limit = persontags::kMaxSuggestions) const;
    // The suggested person's x button: a LASTING rejection for this meeting only (tags.json rejected[]
    // {"person", "meetingId"} without a tag). One undo step. Deleting the meeting deletes the rejection too.
    void rejectPersonForMeeting(const QString& name, const QString& meetingId);
    bool isPersonRejectedForMeeting(const QString& name, const QString& meetingId) const;

    // Carrying person operations through (called by AppController's global rename / delete; the
    // people.json record is moved by PeopleStore): rejections go with the person (a union on merge),
    // the undo stack's person snapshots follow the name.
    void renamePersonRefs(const QString& oldName, const QString& newName);
    void forgetPerson(const QString& name);

    // ---- learning question (UX plan rule 5) ----
    // The questions after an approval / new person (PersonTagSnapshot::learningQuestions; a lastingly
    // rejected pair and an already asked question are dropped). The UI asks the question once:
    // then markLearningAsked (remembered for the run; an unanswered question is not a rejection).
    QVector<PersonTagQuestion> learningQuestions(const QString& meetingId, const QStringList& names) const;
    void markLearningAsked(const QString& meetingId, const QVector<PersonTagQuestion>& questions);
    // "Yes": the tag on every person (manual tag); "No": a lasting (person, tag) rejection.
    // Both are ONE undo step.
    void answerLearning(const PersonTagQuestion& question, bool accept);

    // ---- rejected suggestions ----
    void reject(const QString& meetingId, const TagSuggestion& suggestion);
    bool isRejected(const QString& meetingId, const QString& tagIdOrName) const;
    int  rejectedCount() const;
    void clearRejected();

    // ---- undo ----
    QString beginGroup(const QString& label);
    // The same, with the mark of a meeting's shared undo stack (edit/MeetingUndo.h): the group's step
    // belongs to the meeting even if it does not touch the meeting's tags (e.g. a participant's person
    // tag from the Overview). Steps touching the meeting's tags and rejecting for the meeting belong
    // to the meeting by themselves.
    QString beginGroup(const QString& label, const QString& meetingId);
    void    endGroup();
    bool    canUndo() const;
    QString undoLabel() const;
    void    undo();
    // For the shared undo (MeetingUndoService) adapter: the sequence numbers of the stack's steps
    // (bottom to top; a step's number is unique for the process life), and which meetings a step
    // belongs to (empty: none / unknown number).
    QVector<quint64> undoSerials() const;
    QStringList undoMeetingIds(quint64 serial) const;

    // Rereads tags.json (if it changed on disk) and drops the meeting cache.
    void reload();

signals:
    void tagsChanged();                       // the set (name, count) changed
    void meetingTagsChanged(QString meetingId);
    void rejectedChanged();
    void undoChanged();
    void personTagsChanged(QString name);     // a person's manual tags changed (empty: several)
    void personStatsChanged();                // the learned association's statistics refreshed

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace tanara
