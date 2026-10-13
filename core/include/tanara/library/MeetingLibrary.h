#pragma once
//
// MeetingLibrary: the query layer of the main window's left library (UI-independent).
//
//  - List: meetings newest first, with a date section key (today / yesterday / this week / earlier).
//  - Search: full text, in the title AND the transcript text, accent- and case-insensitive
//    ("odon" finds "Odon" with accents); per hit a snippet + the match position (for highlighting).
//  - Filters: no transcript / no summary / person X attended; hit count.
//  - "Waiting for you": meetings awaiting transcription and with failed transcription.
//
// Performance: meeting metadata and transcript text are cached lazily per meeting; invalidated per
// meeting on signals of the store / AppController. With a few hundred meetings the first search
// loads the transcripts (warmUp() does this ahead of time, in slices, in the event loop), after
// that a search runs in memory.
#include "tanara/domain/Meeting.h"
#include "tanara/domain/Job.h"
#include "tanara/library/MeetingNotes.h"
#include "tanara/foundation/text/Text.h"
#include "tanara/domain/Tag.h"

#include <QObject>
#include <QHash>
#include <QVector>
#include <QDate>

namespace tanara {

class MeetingStore;
class MeetingJobTracker;
class TagService;

enum class DateSection { Today, Yesterday, ThisWeek, Earlier };

struct LibraryEntry {
    QString   id;
    QString   title;
    QDateTime startedAt;
    qint64    durationMs = 0;
    bool      hasTranscript = false;
    bool      hasSummary = false;
    DateSection section = DateSection::Earlier;
    QStringList participants;          // named participants (speakerMap + the own mic track)
    MeetingProcessingState state;      // icon states (if the library got a tracker)

    // --- search hit (all invalid/empty for an empty search) ---
    text::Range titleMatch;        // position of the match in the title
    QString   snippet;                 // snippet from the transcript around the match
    text::Range snippetMatch;      // position of the match in the snippet
    qint64    snippetMs = -1;          // start of the utterance containing the match (for jumping)
    QString   snippetSpeaker;          // the speaker (displayed name)

    // --- tags (after setTagService) ---
    QStringList tagIds;                // in the meeting's order, only existing tags
    QStringList tagNames;              // in the same order
    bool      tagMatch = false;        // the search matched in a tag's name
    QString   tagMatchName;            // name of the first matching tag
};

struct LibraryQuery {
    QString     text;                  // search term (empty = no text filter)
    bool        noTranscript = false;  // only those without a transcript
    bool        noSummary = false;     // only those without a summary
    QStringList people;                // every given person attended
    QStringList tags;                  // tag ids: any (tagsAll = every) is on it
    bool        tagsAll = false;       // true = AND, false = OR
    bool        untagged = false;      // untagged ones (OR-ed with tags)

    bool isEmpty() const {
        return text.trimmed().isEmpty() && !noTranscript && !noSummary && people.isEmpty()
            && tags.isEmpty() && !untagged;
    }
};

struct LibraryResult {
    QVector<LibraryEntry> entries;     // newest first
    int totalMeetings = 0;             // the library's total size (without filtering)
    int count() const { return int(entries.size()); }   // hit count
};

struct PersonPresence {
    QString name;
    int meetingCount = 0;
};

// Items of the "Pick a meeting / Waiting for you" card.
enum class PendingKind {
    AwaitingTranscription,   // there is a recording, no transcript yet ("Open")
    TranscriptionFailed,     // transcription failed ("Take a look"); error filled
    StaleSummary,            // stale summary ("Refresh"); correctedSpeakers filled
};

struct PendingItem {
    PendingKind kind = PendingKind::AwaitingTranscription;
    QString   meetingId;
    QString   title;
    QDateTime startedAt;
    qint64    durationMs = 0;
    JobError  error;         // for TranscriptionFailed
    int       correctedSpeakers = 0;   // StaleSummary: how many speakers were corrected since the summary
};

class MeetingLibrary : public QObject {
    Q_OBJECT
public:
    // tracker may be nullptr (then the state consists only of the hasTranscript/hasSummary flags).
    explicit MeetingLibrary(MeetingStore* store, MeetingJobTracker* tracker = nullptr,
                            QObject* parent = nullptr);
    ~MeetingLibrary() override;

    // The list / search. `now` is the "today" for the section computation (can be passed for tests).
    LibraryResult query(const LibraryQuery& q = {},
                        const QDateTime& now = QDateTime::currentDateTime()) const;

    // One meeting's entry without a search (for incremental model refresh); empty id if none.
    LibraryEntry entry(const QString& meetingId,
                       const QDateTime& now = QDateTime::currentDateTime()) const;

    // The full meeting (loaded from meeting.json) from the cache.
    Meeting meeting(const QString& meetingId) const;
    int meetingCount() const;

    // Template suggestions for the meeting's note: the non-empty notes, different from the current
    // one, of earlier meetings with a similar title, newest first (see MeetingNotes.h).
    QVector<meetingnotes::NoteSuggestion> noteSuggestions(const QString& meetingId,
                                                          const QString& currentNote,
                                                          int limit = 3) const;

    // The named people in the library, by frequency (for the filter chips).
    QVector<PersonPresence> people() const;

    // Tags: the entries' tag names; the tag filter and the search in tags need this.
    void setTagService(TagService* tags);
    // The filter popover's tags: every tag with its library count (frequency, then alphabetical).
    QVector<TagUsage> tagOptions() const;
    // How many meetings have no tag (the "Untagged" count).
    int untaggedCount() const;

    // "Waiting for you": newest first. limit <= 0 -> all.
    QVector<PendingItem> pendingItems(int limit = 0) const;

    // Date section: today / yesterday / this week (the week starts on Monday) / earlier.
    static DateSection sectionFor(const QDateTime& startedAt, const QDate& today);
    static QString sectionKey(DateSection s);     // "today" | "yesterday" | "thisWeek" | "earlier"
    static QString sectionTitle(DateSection s);   // Hungarian section captions ("Today" | "Yesterday" | "This week" | "Earlier")

    // Whether every transcript's text has already been loaded for search.
    bool isWarm() const;

    // The meeting's named participants: the fixed speaker of the own (mic) track + the speakerMap
    // names, once each. Generic track labels ("Microphone 2") are not people.
    static QStringList participantsOf(const Meeting& m);

public slots:
    // Preloading the transcript texts in slices in the event loop (non-blocking). warmedUp signal.
    void warmUp();
    // Drops the cache of one meeting (or, with an empty id, of all); the next query reloads.
    void invalidate(const QString& meetingId = QString());

signals:
    void meetingAdded(QString meetingId);
    void meetingChanged(QString meetingId);     // title, state, transcript, participants ... changed
    void meetingRemoved(QString meetingId);
    void pendingItemsChanged();
    void reset();                               // a full reload is needed (invalidate() everything)
    // The tag set changed (name, delete, merge): the rows' tag names and the filter popover need
    // refreshing. (A change of a meeting's tags comes as meetingChanged.)
    void tagsChanged();
    void warmedUp();

private:
    struct TextDoc;
    struct Impl;
    void ensureLoaded() const;
    void reloadMeeting(const QString& id);
    const TextDoc* textDoc(const Meeting& m) const;
    LibraryEntry makeEntry(const Meeting& m, const QDate& today) const;

    Impl* d;
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::DateSection)
Q_DECLARE_METATYPE(tanara::LibraryEntry)
Q_DECLARE_METATYPE(tanara::LibraryQuery)
Q_DECLARE_METATYPE(tanara::LibraryResult)
Q_DECLARE_METATYPE(tanara::PersonPresence)
Q_DECLARE_METATYPE(tanara::PendingKind)
Q_DECLARE_METATYPE(tanara::PendingItem)
