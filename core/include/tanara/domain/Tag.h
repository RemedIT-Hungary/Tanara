#pragma once
//
// Domain: tags, shared value types (the contract between TagService, the suggesters and the UI).
//
// A tag's meaning comes not from its name but from which meetings it was applied to: the
// suggestions (similar meetings, co-occurring tags, the language model) work from that.
// The per-meeting assignment lives in meeting.json (Meeting::tagIds), the tag set in
// <metadataDir>/tags.json (TagService).
#include <QDateTime>
#include <QMetaType>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

struct Tag {
    QString   id;          // QUuid without braces; empty = no such tag
    QString   name;        // display name (normalizeTagName)
    QDateTime createdAt;
    bool isValid() const { return !id.isEmpty(); }
};

struct TagUsage {
    Tag       tag;
    int       meetingCount = 0;
    QDateTime firstUsedAt;   // its earliest meeting
    QDateTime lastUsedAt;    // its latest meeting, or the latest time it was applied
};

// Where the tag was applied from (TagService logs it; it does not affect behavior).
enum class TagSource { Manual, Suggestion, Llm, Bulk };
// Which suggester gave the suggestion (People: based on the participants' manual tags).
enum class SuggestionSource { Similar, Cooccur, Llm, People };
// Kind of a reason row ("Shared participant" / "Shared phrases" / "Similar title" /
// "Participants' tag": the names of the participants carrying the tag).
enum class ReasonKind { Participant, Terms, Title, PeopleTags };

struct SuggestionReason {
    ReasonKind  kind = ReasonKind::Terms;
    QStringList values;
};

struct MeetingRef {
    QString   meetingId;
    QString   title;
    QDateTime startedAt;
    qint64    durationMs = 0;
};

struct TagSuggestion {
    QString tagId;                   // empty if isNew
    QString name;
    bool    isNew = false;           // a name invented by the language model, not in the set yet
    SuggestionSource source = SuggestionSource::Similar;
    double  score = 0.0;             // 0..1, only for ordering
    QVector<SuggestionReason> reasons;
    QVector<MeetingRef> similarMeetings;   // "Similar meeting" links
    QString baseTagId;               // Cooccur: the tag it often goes with ("often with <Tag>")
};

// What the system learned about a tag from its use (derived, read-only).
struct TagProfile {
    QString tagId;
    int     meetingCount = 0;
    QVector<QPair<QString, int>> topParticipants;   // name, in how many of the tag's meetings
    QStringList topTerms;                           // at most 12
    QVector<QPair<QString, int>> cooccurring;       // tagId, how many times together
    QVector<MeetingRef> meetings;                   // newest first
};

// A similar meeting (MeetingProfiles::similar / EmbeddingIndex::similar).
struct SimilarHit {
    QString meetingId;
    double  score = 0.0;
    QVector<SuggestionReason> reasons;
};

// One row of the tag input field's popup list (TagService::inputRows).
struct TagInputRow {
    enum Kind {
        Recent,         // empty field: recently used
        Match,          // typing: a match from the set
        New,            // "+ New tag: ..."
        NearDuplicate,  // "SIMILAR EXISTS": the existing, very similarly named tag
        ForceNew,       // "New anyway: ..."
    };
    Kind    kind = Match;
    Tag     tag;               // New / ForceNew: empty id, name = the (normalized) typed name
    int     meetingCount = 0;
    int     matchStart = -1;   // position of the match in tag.name (for highlighting); -1 = none
    int     matchLen = 0;
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::Tag)
Q_DECLARE_METATYPE(tanara::TagUsage)
Q_DECLARE_METATYPE(tanara::TagSuggestion)
Q_DECLARE_METATYPE(QVector<tanara::TagSuggestion>)
Q_DECLARE_METATYPE(tanara::TagProfile)
Q_DECLARE_METATYPE(tanara::SimilarHit)
Q_DECLARE_METATYPE(tanara::TagInputRow)
