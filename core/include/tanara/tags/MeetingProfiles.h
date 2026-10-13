#pragma once
//
// MeetingProfiles: meeting similarity without an embedding model (the "basic" level).
//
// One PROFILE per meeting:
//  - participants: the named speakers (speakerMap + the own mic track's name), weighted by their
//    rarity in the library (someone present in over 60% of meetings has ~0 weight);
//  - title words: meetingnotes::titleWords;
//  - characteristic phrases (<= 60): the transcript's words folded, without numbers / short words /
//    filler words, with light Hungarian suffix stripping (tagtext::stem), weight =
//    tf * log(N / df) over the library. Rare stems differing by at most one letter (mishearing:
//    "remedi" ~ "remedit") are merged; the displayed form is the most frequent original spelling.
//
// Loading the rows (meeting.json) and counting phrases (reading the transcript, splitting into
// words) run on the "tanara-profiles" background thread; the profile is cached in the meeting's
// folder (profile.json, valid by the transcript file's mtime + size). The library-level
// statistics (df, merging, weights) assemble lazily as an immutable snapshot from the finished
// profiles, and also ahead of time on the background thread at the end of a build.
//
// Thread safety: the reader methods (similar, similarToDraft, termsOf, topTerms, sharedTerms,
// participantWeight, folders, isBuilt) may be called from any thread; ensureBuilt /
// invalidate / isIdle belong to the owner (main) thread. After the first ensureBuilt (background
// mode; the app calls it at startup) the main thread never loads rows and never computes
// statistics: for stale state it gets the earlier snapshot while the background thread refreshes.
// Before that (synchronous use only: tests, simple tools) the first read loads and computes on the main thread.
//
// Similarity: 0.45 * participants (weighted Jaccard) + 0.40 * phrases (cosine) +
// 0.15 * title (meetingnotes::titleSimilarity).
#include "tanara/tags/TagTypes.h"

#include <QHash>
#include <QObject>
#include <QPair>
#include <QStringList>
#include <QVector>

#include <memory>

namespace tanara {

class MeetingStore;

namespace tagtext {

// Stem of a folded (accent-free, lowercase) word: the longest matching suffix removed, once, only
// if at least 4 letters remain.
QString stem(const QString& foldedWord);

// Whether it is a filler / conjunction word (in folded form; Hungarian and English).
bool isStopWord(const QString& foldedWord);

// Phrase candidates of a text: (stem, original word form) pairs, in text order.
QVector<QPair<QString, QString>> terms(const QString& text);

} // namespace tagtext

class MeetingProfiles : public QObject {
    Q_OBJECT
public:
    explicit MeetingProfiles(MeetingStore* store, QObject* parent = nullptr);
    ~MeetingProfiles() override;

    // Loading the rows and building missing / stale profiles on the background thread (non-blocking;
    // only the index query runs on the main thread). profileReady signals, idle() at the end of a
    // round, even if there was nothing to do. A round not yet started is not duplicated.
    void ensureBuilt();
    bool isIdle() const;
    bool isBuilt(const QString& meetingId) const;
    // Drops one meeting's profile (e.g. a new transcript); the next ensureBuilt rebuilds it.
    void invalidate(const QString& meetingId);

    // The most similar OTHER meetings, by descending score (a zero score is dropped).
    QVector<SimilarHit> similar(const QString& meetingId, int limit = 8) const;
    // For a not-yet-existing meeting (import / before recording): by title + participants.
    QVector<SimilarHit> similarToDraft(const QString& title, const QStringList& participants,
                                       const QString& excludeId = QString(), int limit = 8) const;

    // For display: the meeting's characteristic phrases, or the shared ones of several meetings.
    QStringList termsOf(const QString& meetingId, int limit = 12) const;
    QStringList topTerms(const QStringList& meetingIds, int limit = 12) const;
    // The shared characteristic phrases of two meetings (by the product of the weights).
    QStringList sharedTerms(const QString& a, const QString& b, int limit = 3) const;
    // Rarity weight (0..1) of the meeting's participants, for tests / explanation.
    double participantWeight(const QString& name) const;
    // Folders of the known meetings (id -> folder), from a background thread without the index (EmbeddingIndex).
    QHash<QString, QString> folders() const;

signals:
    void profileReady(QString meetingId);
    void idle();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace tanara
