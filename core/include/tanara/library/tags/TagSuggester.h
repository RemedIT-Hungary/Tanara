#pragma once
//
// TagSuggester: tag suggestions from similar meetings and co-occurring tags.
//
//  - suggest(): the tags of the neighbors (MeetingProfiles::similar, and if the meeting has an
//    embedding, EmbeddingIndex::similar, merged by reciprocal rank fusion, RRF, k = 60) vote with
//    the neighbor's score. A tag's vote is divided by log(2 + global ratio * 10), so tags that are
//    everywhere do not win. Tags already on the meeting and rejected ones are dropped; so are
//    those below 15% of the best vote; at most 6.
//  - cooccur(): tags that appear together with the freshly applied tag in at least half (>= 2) of its meetings.
//  - suggestForDraft(): for a not-yet-existing meeting (import, before recording), by title.
//
// Threading: TagService belongs to the main thread, so a small snapshot (TagSnapshot) of the tag data
// needed for a suggestion is made on the main thread; suggestFrom uses it plus the thread-safe
// readers of MeetingProfiles / EmbeddingIndex, so it can run on a background thread. The methods
// reading TagService directly (suggest, cooccur, suggestForDraft) belong to the main thread.
#include "tanara/domain/Person.h"
#include "tanara/domain/Tag.h"

#include <QHash>
#include <QSet>

namespace tanara::library {

class TagService;
class MeetingProfiles;
class EmbeddingIndex;

// A copy of the tag data for one meeting's suggestion (a value type, passable between threads).
struct TagSnapshot {
    QString meetingId;                              // empty: a draft (a not-yet-existing meeting)
    QHash<QString, QStringList> tagsByMeeting;      // tagged meetings -> their tags
    QHash<QString, Tag> tags;                       // id -> tag
    QHash<QString, int> counts;                     // id -> number of meetings
    QSet<QString> rejected;                         // tag ids rejected on the meeting
    QHash<QString, MeetingRef> refs;                // references of the tagged meetings
    int totalMeetings = 0;
    // Display names: the "shared participant" reasons of MeetingProfiles carry PersonIds; the
    // suggestion's reason shows the names (an unknown person is dropped).
    PersonNames personNames;
};

class TagSuggester {
public:
    static constexpr int kMaxSuggestions = 6;
    static constexpr int kRrfK = 60;
    static constexpr double kKeepRatio = 0.15;

    TagSuggester(TagService* tags, MeetingProfiles* profiles, EmbeddingIndex* index = nullptr);

    QVector<TagSuggestion> suggest(const QString& meetingId) const;
    // Builds the snapshot (main thread; ~milliseconds depending on the meeting count).
    static TagSnapshot snapshot(const TagService& tags, const QString& meetingId);
    // The thread-safe core of suggest(): callable from any thread.
    static QVector<TagSuggestion> suggestFrom(const TagSnapshot& snap, const MeetingProfiles* profiles,
                                              const EmbeddingIndex* index = nullptr);
    QVector<TagSuggestion> cooccur(const QString& meetingId, const QString& justAddedTagId) const;
    QVector<TagSuggestion> suggestForDraft(const QString& title, const PersonIds& participants = {}) const;

    // Merging the neighbors by reciprocal rank fusion (pure function, testable).
    static QVector<SimilarHit> fuseRrf(const QVector<SimilarHit>& a, const QVector<SimilarHit>& b, int k = kRrfK);

private:
    static QVector<TagSuggestion> vote(const TagSnapshot& snap, const QVector<SimilarHit>& neighbours);

    TagService*      m_tags;
    MeetingProfiles* m_profiles;
    EmbeddingIndex*  m_index;
};

} // namespace tanara::library