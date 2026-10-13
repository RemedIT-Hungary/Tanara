#pragma once
//
// UI-independent rules around the meeting note (Meeting::contextNote):
//
//  - Legacy automatic note: the watcher used to write an "automatically detected call: <app>"
//    sentence into the note. That is not the user's own words and tells the transcriber nothing:
//    on load it is interpreted as an empty note + a detected call app (interpretLegacyNote);
//    the next save writes the clean form.
//  - The context sent to the transcriber (fillSttContext): the title (general), the note (text)
//    and the participant names (terms). The automatic call text never goes in.
//  - Template suggestions: the notes of earlier meetings with a similar title (e.g. a recurring
//    "<Project> weekly meeting"). Titles are compared word by word, accent- and case-insensitive,
//    ignoring dates and numbers (titleWords / titleSimilarity). With a few hundred meetings one
//    call takes microseconds.
#include "tanara/Types.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

struct SttRequest;

namespace meetingnotes {

// The legacy auto sentence (Hungarian or English) with ": Teams" -> true, app = "Teams".
bool parseAutoCallNote(const QString& note, QString* app = nullptr);

// If the note is exactly the automatic sentence: the note becomes empty, the app name goes into
// detectedCallApp (if that is still empty). true if anything changed.
bool interpretLegacyNote(Meeting& m);

// Free text for the transcriber: the note, trimmed; never the automatic sentence.
QString sttContextText(const Meeting& m);

// Fills the context envelope from the meeting (general: title, text: note, terms: the fixed
// speakers of the active tracks).
void fillSttContext(SttRequest& req, const Meeting& m);

// The comparable words of a title: without accents, lowercase, once each; every word containing a
// number (date, ordinal, time), month and day names (also abbreviated), single-letter words and a few
// articles / conjunctions are dropped.
QStringList titleWords(const QString& title);

// Similarity of two word sets, 0..1 (Jaccard: shared / total). 0 if the shared words are all
// "weak" (meeting, weekly, call, Teams ...); alone they do not make two meetings related.
double titleSimilarity(const QStringList& a, const QStringList& b);

// Whether it is similar enough for a suggestion (titleSimilarity >= kSimilarThreshold).
constexpr double kSimilarThreshold = 0.5;
bool titlesSimilar(const QString& a, const QString& b);

// A candidate for the suggestions (from the library's cache; words = titleWords(title)).
struct NoteCandidate {
    QString     meetingId;
    QString     title;
    QDateTime   startedAt;
    QString     note;
    QStringList words;
};

struct NoteSuggestion {
    QString   meetingId;
    QString   title;
    QDateTime startedAt;
    QString   note;
    double    score = 0.0;
};

// At most `limit` suggestions for the `selfId` meeting: OTHER meetings with a similar title and a
// NON-empty note different from the current one (currentNote), newest first; an identical note
// only once (with the newest source).
QVector<NoteSuggestion> suggestNotes(const QVector<NoteCandidate>& candidates,
                                     const QString& selfId, const QString& selfTitle,
                                     const QString& currentNote, int limit = 3);

// A convenience variant directly from meetings (for tests, small lists).
QVector<NoteSuggestion> suggestNotes(const QVector<Meeting>& meetings,
                                     const QString& selfId, const QString& selfTitle,
                                     const QString& currentNote, int limit = 3);

} // namespace meetingnotes
} // namespace tanara
