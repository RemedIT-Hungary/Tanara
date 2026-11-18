#pragma once
//
// Side split: cutting a transcript line where the talker moved between the local (mic) and the remote
// (loopback) side. The transcript is made from the mixdown, so the provider's diarization decides where
// a line ends; when it fuses a local and a remote voice under one raw speaker it does not cut where the
// talker changed either, and such a line cannot be given to one person. The track energy separates the
// two sides trivially (SideAnalysis.h).
//
// The rule is the speech-level-normalized one of sides::classifyLevels, run on sliding windows of the
// line (sides::kSplitWindowMs wide, every sides::kSplitStepMs): consecutive windows of the same side form
// runs. Mixed (talking over each other) and Unknown (silence, no coverage) windows are neutral: the Local /
// Remote runs around them join when they are of the same side. A run counts only if it spans at least
// kSplitMinRunMs and holds at least kSplitMinRunWords words; a weaker one is absorbed (the shortest first,
// its neighbours of the same side join). The echo case (the local voice returning on the loopback) is
// Local by classifyLevels already, so it never makes a Remote run. Between
// two counted runs of different sides the line is cut at the word boundary nearest to the middle of
// the change, if both parts keep at least kSplitMinRunWords words. A line can be cut more than once.
//
// The cuts are token start times (MergedTranscript::sideCuts): the grouping breaks a line before such a
// token, the parts keep the raw speaker label and the text as it is, and the padding of emission-timed
// lines (sonioxtiming::padUtterances) applies to the parts as to any line. A single-track meeting
// (no mic + loopback activity) gives no cut. Pure functions, no I/O.
#include "tanara/domain/Activity.h"
#include "tanara/domain/Transcript.h"
#include "tanara/editing/review/SideAnalysis.h"

#include <QVector>

namespace tanara::editing::sides {

// One cut line.
struct LineSplit {
    qint64 startMs = 0;          // the line's raw token span (before padding)
    qint64 endMs = 0;
    QString rawLabel;
    QVector<qint64> cuts;        // the start times of the tokens that begin a new part (ascending)
    QVector<Side> partSides;     // the side of each part (cuts.size() + 1)
};

struct SplitResult {
    bool active = false;         // there is mic AND loopback activity
    QVector<qint64> cuts;        // every cut (ascending): MergedTranscript::sideCuts
    QVector<LineSplit> lines;    // the cut lines in time order
    int parts() const;           // the number of parts the cut lines became
    qint64 affectedMs() const;   // the total duration of the cut lines
};

// The side cuts of a transcript, computed afresh (the transcript's own sideCuts are ignored).
SplitResult splitLinesBySide(const MergedTranscript& transcript, const MeetingActivity& activity);

} // namespace tanara::editing::sides
