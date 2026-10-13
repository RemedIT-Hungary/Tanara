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
    bool markdownRegenerated = false;
};

// Applies the plan (needs Verdict::Reslice). First the .bak copies (transcript.tokens.json /
// .segments.json / .speakers.json / summary.json / transcript.md), then the write.
ApplyResult apply(const QString& meetingFolder, const Plan& plan);

} // namespace tanara::editing::reslice