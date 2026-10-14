#pragma once
//
// Tags on people: the learned association, thresholds, evidence (UI-independent, pure data).
//
// Concepts (see the vault UX plan "Person tags"):
//  - M(t): the meetings of tag t; M(p): the meetings where p is a named participant
//    (MeetingLibrary::participantsOf); c(p,t) = |M(p) intersect M(t)|. People are keyed by PersonId.
//  - Learned association (not stored): c(p,t) computed from the meetings. The own person is
//    left out everywhere (present at every meeting, carries no information).
//  - Manual tag: the "tags" array of the people.json record (PeopleStore), handled by TagService.
//
// PersonTagStats is an immutable snapshot; PersonTagSnapshot (statistics + manual tags + tag
// names) can be passed between threads, so the evidence can also be computed on a background
// thread (identification / recheck).
#include "tanara/domain/Person.h"
#include "tanara/domain/Tag.h"

#include <QDateTime>
#include <QHash>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace tanara::library {

// One meeting for the computation (from TagService's row cache).
struct PersonTagRow {
    QString     meetingId;
    QDateTime   startedAt;
    QStringList tagIds;
    PersonIds   participants;   // named participants
};

// A (person, tag) pair with numbers, for list rows and suggestions.
struct PersonTagStat {
    PersonId  personId;          // the person
    QString   name;              // their display name
    QString   tagId;
    int       shared = 0;        // c(p,t)
    int       tagTotal = 0;      // |M(t)|
    int       personTotal = 0;   // |M(p)|
    bool      manual = false;    // the tag is manually on the person
    bool      rejected = false;  // the (person, tag) suggestion was rejected
    QDateTime lastShared;        // the latest shared meeting
};

// A supporting / contradicting tag in the evidence.
struct TagEvidenceItem {
    QString tagId;
    QString name;
    int     shared = 0;      // c(p,t)
    int     total = 0;       // |M(t)|
    bool    manual = false;  // manual tag (otherwise learned)
};

// The tag as identification evidence for a person at a meeting. It carries NO weight (the ranker
// gives that); only a probability, never a decision.
struct TagEvidence {
    QStringList supportTagIds;       // the meeting's tags that support the person
    QStringList contradictTagIds;    // on contradiction: the meeting's tags (the person has none of them)
    QVector<TagEvidenceItem> items;  // the supporting / contradicting tags with numbers
    // The person's tags for the "Usually on other tags" text: the manual tags, failing that
    // the learned (c >= 1) tags by number of shared meetings.
    QStringList personTagIds;
    QStringList personTagNames;      // the names of the same
    QString text;                    // the chip: "#Nordvik", with several tags "#Nordvik +1"
    QString detail;                  // the tooltip number: "11 / 14" (the first tag's)
    bool isSupport() const { return !supportTagIds.isEmpty(); }
    bool isContradiction() const { return !contradictTagIds.isEmpty(); }
    bool isEmpty() const { return !isSupport() && !isContradiction(); }
};

// Learning question: one of the meeting's tags could go onto these people.
// Approved participants' tags are never applied by themselves; the UI asks once.
struct PersonTagQuestion {
    QString tagId;
    QString tagName;
    PersonIds   people;       // in input order
    QStringList names;        // their display names (same order)
    bool newPeople = false;   // their first meeting (a new person): "X is a new person. Put ... on them?"
};

namespace persontags {

// Thresholds (fixed; by the product owner's decision not adjustable).
constexpr int kMinShared = 2;            // >= 2 shared meetings
constexpr int kTagRatioPct = 30;         // >= 30% of the tag's meetings
constexpr int kPersonRatioPct = 50;      // or >= 50% of the person's meetings (3.2 only)
constexpr int kMaxSuggestions = 3;
// Contradiction guards: the person needs at least this many tagged meetings (without a manual
// tag), and the meeting's tags need at least this many meetings (a "mature" tag).
constexpr int kContradictPersonTagged = 5;
constexpr int kMatureTagMeetings = 5;

// 3.1 / evidence: c >= 2 and c / |M(t)| >= 30%.
bool learnedLink(int shared, int tagTotal);
// 3.2: c >= 2 and (c / |M(t)| >= 30% or c / |M(p)| >= 50%).
bool suggestableForPerson(int shared, int tagTotal, int personTotal);

} // namespace persontags

// Statistics of the learned association (immutable; readable from any thread).
class PersonTagStats {
public:
    // The own person (self) is left out of every computation.
    static PersonTagStats compute(const QVector<PersonTagRow>& rows, const PersonId& self);

    int tagTotal(const QString& tagId) const { return m_tagTotal.value(tagId); }
    int personTotal(const PersonId& person) const { return m_personTotal.value(person); }
    // Number of the person's tagged (>= 1 tag) meetings.
    int personTagged(const PersonId& person) const { return m_personTagged.value(person); }
    // The meeting's row in the computation: its tags; empty for an unknown one.
    QStringList meetingTags(const QString& meetingId) const { return m_meetingTags.value(meetingId); }
    bool meetingHasPerson(const QString& meetingId, const PersonId& person) const;
    int shared(const PersonId& person, const QString& tagId) const;
    QDateTime lastShared(const PersonId& person, const QString& tagId) const;
    // The tag's participants (c >= 1), or the person's tags (c >= 1); unordered.
    PersonIds peopleOf(const QString& tagId) const;
    QStringList tagsOf(const PersonId& person) const;
    int meetingCount() const { return m_meetings; }

private:
    QHash<QString, int> m_tagTotal;                          // tagId → |M(t)|
    QHash<PersonId, int> m_personTotal;                      // person → |M(p)|
    QHash<QString, QHash<PersonId, int>> m_shared;           // tagId → person → c
    QHash<QString, QHash<PersonId, QDateTime>> m_last;       // tagId -> person -> last shared
    QHash<PersonId, QStringList> m_tagsOf;                   // person → tagIds
    QHash<PersonId, int> m_personTagged;                     // person -> its tagged meetings
    QHash<QString, QStringList> m_meetingTags;               // meetingId -> tags (in order)
    QHash<QString, PersonIds> m_meetingPeople;               // meetingId → people
    int m_meetings = 0;
};

// Everything the evidence needs, in a package that can be passed between threads.
struct PersonTagSnapshot {
    std::shared_ptr<const PersonTagStats> stats;   // never null (TagService::personTagSnapshot)
    QHash<PersonId, QStringList> manual;           // person -> manual tagIds
    QHash<QString, QString> tagNames;              // tagId -> name
    PersonId self;                                 // the own person (null = none)
    PersonNames names;                             // display names (learning questions)

    // The evidence (pure function; UX plan rule 4). Rules:
    //  - supports: one of the meeting's tags is manually on the person, OR the learned association
    //    reaches the 3.1 threshold (c >= 2 and >= 30%), even without a manual tag;
    //  - contradicts, ONLY if all four hold: (1) the meeting has a tag; (2) the person has a manual
    //    tag, or >= 5 tagged meetings; (3) for every tag of the meeting c = 0 and none is manually
    //    on them; (4) those tags are mature (|M(t)| >= 5);
    //  - otherwise (and for the own person, for a meeting without tags) empty.
    // meetingId: the examined meeting's own row is left out of the numbers (otherwise the person
    // would support themselves with their current presence); empty = every row counts.
    TagEvidence evidence(const PersonId& person, const QStringList& meetingTagIds,
                         const QString& meetingId = QString()) const;

    // Learning questions for a meeting's participants (pure function; UX plan rule 5). The meeting's
    // own row is left out of the computation, so the snapshot's freshness does not matter. Per person:
    //  - a new person (not at another meeting yet): every tag of the meeting;
    //  - otherwise tag t, if c(p,t) becomes 2 with it (exactly 1 at other meetings);
    //  - left out: the own person, a null id, those already manually on them, and the rejected ones.
    // Grouped by tag (new people separately), a multi-person question first.
    QVector<PersonTagQuestion> learningQuestions(
        const QString& meetingId, const QStringList& meetingTagIds, const PersonIds& people,
        const std::function<bool(const PersonId& person, const QString& tagId)>& rejected = {}) const;
};

// The evidence seam (identification, candidate ranking, "Who was there?"): the person, the
// meeting's tags and id -> TagEvidence. AppController binds it to the TagService snapshot.
using TagEvidenceFn = std::function<TagEvidence(const PersonId& person, const QStringList& meetingTagIds,
                                                const QString& meetingId)>;

} // namespace tanara::library
Q_DECLARE_METATYPE(tanara::library::PersonTagStat)
Q_DECLARE_METATYPE(tanara::library::TagEvidence)
