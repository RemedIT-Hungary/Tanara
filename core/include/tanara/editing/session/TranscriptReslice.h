#pragma once
//
// TranscriptReslice: padding the lines of an EXISTING (Soniox-timed) transcript from
// transcript.tokens.json, carrying the utterance ids through (the core of `tanara-cli reslice`).
//
// Grouping runs on the ORIGINAL token times (the same merger); only the finished lines get a
// pre-/post-roll (sonioxtiming::padUtterances), so the number and order of lines do not change, and the
// old -> new id map is 1:1 by index. The utterance id is formed from the start time ("u<startMs>"), so
// padding changes the ids: the map goes to the overlay (transcript.speakers.json: every id reference + the
// transcript fingerprint) and summary.json (sourceSpans.utteranceIds + the span times, sourceSpeakers
// keys); transcript.embeddings.bin is deleted. The tokens in tokens.json are untouched; "timing":
// "emission" goes into the root (this makes the merger pad at every slicing in the app, and it is also
// the second run's "nothing to do" marker).
//
// UI-independent, no QObject.
#include "tanara/domain/Transcript.h"
#include "tanara/editing/review/SideSplit.h"
#include "tanara/editing/session/SpeakerOverlay.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::editing::reslice {

// Root field of tokens.json (AppController writes the same for Soniox transcripts).
inline QString timingKey() { return QStringLiteral("timing"); }
inline QString emissionTimingValue() { return QStringLiteral("emission"); }

enum class Verdict {
    Error,              // no / unreadable tokens.json, or segments.json does not fit it
    RealWordSpans,      // the median of the token widths is > 80 ms: real word spans (Whisper)
    AlreadyPadded,      // tokens.json already has "timing": "emission"
    Reslice,            // there is something to do
};

struct Plan {
    Verdict verdict = Verdict::Error;
    QString error;
    qint64  medianWidthMs = -1;
    int     tokenCount = 0;

    QVector<TranscriptLine> oldLines;   // the current segments.json (or from the tokens if it is missing)
    QVector<Utterance>      newSegments;// the padded lines (the new content of segments.json)
    QVector<TranscriptLine> newLines;   // the same, with new ids; oldLines[i] -> newLines[i]
    QMap<QString, QString> idMap;       // old id -> new id (every old line)
    int  renamedIds = 0;                // where the id changes
    int  textChanged = 0;               // where the text differs (unexpected; the text does not change)
    // Histogram of the start shift (old start - new start, ms): "0", "1-100", "101-200", "201-300",
    // ">300" (padding is at most 300 ms).
    QMap<QString, int> shiftHistogram;

    bool overlayPresent = false;
    bool overlayStale = false;          // the overlay belongs to an earlier transcript, so we do not touch it
    int  overlayRefs = 0;               // number of id references in the overlay
    int  overlayRenamed = 0;            // of which to rename

    bool summaryPresent = false;
    int  summaryRefs = 0;
    int  summaryRenamed = 0;

    bool embeddingsPresent = false;
};

// Analysis, without writing.
Plan plan(const QString& meetingFolder);

struct ApplyResult {
    bool ok = false;
    QString error;
    QStringList backups;        // .bak files made now (an existing one is not overwritten)
    QStringList written;        // files rewritten
    bool embeddingsRemoved = false;
    int  embeddingsDropped = 0;     // side split: embedding entries dropped (changed lines)
    bool markdownRegenerated = false;
};

// Applies the plan (needs Verdict::Reslice). First the .bak copies (transcript.tokens.json /
// .segments.json / .speakers.json / summary.json / transcript.md), then the write.
ApplyResult apply(const QString& meetingFolder, const Plan& plan);

// ---- side split of an existing transcript (the core of `tanara-cli side-split`) ----
//
// The lines are cut where the talker moved between the mic and the loopback side
// (editing/review/SideSplit.h); the cuts go into tokens.json ("sideCuts", added to the ones already there,
// none is removed), the tokens stay untouched. Id bookkeeping: the first part of a cut line keeps the id
// "u<startMs>", the other parts get their own ids. Overlay (transcript.speakers.json, if it belongs to the
// current transcript): a new part inherits the original line's entry (speaker, corrected, noisy,
// ignored) unless its side contradicts the side of the line's speaker; it never inherits confirmed /
// rechecked. The first part keeps its entry, but loses the speaker, corrected, confirmed and recheck
// flags if its side contradicts. summary.json: every reference to a cut line lists all its parts, the
// source span times are refreshed. transcript.embeddings.bin: the entries of the changed lines are dropped
// (they are recomputed), the rest is kept under the new fingerprint.

struct SplitPlan {
    bool ok = false;
    QString error;
    bool active = false;                // mic + loopback activity (otherwise nothing to do)
    sides::SplitResult split;           // the cuts found (computed afresh)
    QVector<qint64> newCuts;            // the cuts not yet in tokens.json
    QVector<qint64> allCuts;            // tokens.json's cuts after applying

    QVector<TranscriptLine> oldLines;   // the current segments.json
    QVector<Utterance> newSegments;     // the cut lines (the new content of segments.json)
    QVector<TranscriptLine> newLines;   // the same, with ids
    QMap<QString, QStringList> partsOf; // old id -> new ids (one for an uncut line; the first keeps the line)
    int cutLines = 0;                   // old lines that are cut
    int parts = 0;                      // the parts they become
    qint64 cutMs = 0;                   // total duration of the cut lines
    int renamedIds = 0;                 // old lines whose (first) id changes

    bool overlayPresent = false;
    bool overlayStale = false;          // belongs to an earlier transcript: left untouched
    int overlayEntriesCut = 0;          // overlay entries of cut lines
    int overlayInherited = 0;           // new parts that got the original line's entry
    int overlayContradicted = 0;        // parts whose side contradicts the speaker (no speaker inherited)
    int confirmedNotInherited = 0;      // new parts of a confirmed line (the flag stays on the first part only)
    SpeakerOverlay newOverlay;          // the overlay after the split (valid if overlayPresent && !overlayStale)

    bool summaryPresent = false;
    int summaryRefs = 0;                // references to cut / renamed lines in summary.json

    bool embeddingsPresent = false;
    QStringList changedIds;             // old ids whose line changes (their embeddings are dropped)

    bool hasWork() const { return ok && !newCuts.isEmpty(); }
};

// Analysis, without writing. activity: the meeting's track activity (processing::loadOrComputeSideActivity);
// self / hints: the person sides of the overlay inheritance (analyzeSides).
SplitPlan planSideSplit(const QString& meetingFolder, const MeetingActivity& activity, const SelfPerson& self,
                        const SideHints& hints = {});

// Applies the plan (needs hasWork()). First the .side-split.bak copies (tokens / segments / speakers /
// summary / transcript.md; an existing one is not overwritten), then the write.
ApplyResult applySideSplit(const QString& meetingFolder, const SplitPlan& plan);

} // namespace tanara::editing::reslice