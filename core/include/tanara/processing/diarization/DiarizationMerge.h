#pragma once
//
// DiarizationMerge (D4): the local diarization's speaker turns written into a transcript whose STT gave no
// speaker separation (Whisper: every token "speaker:1"; a non-diarizing Tanara Cloud tier). Pure functions,
// no I/O; unit-tested in test_diarization_merge.
//
// Rules (kept deliberately simple):
//   1. Applies only when the transcript has at most ONE distinct speaker label (needsSpeakers); a transcript
//      the provider diarized (Soniox: "speaker:1", "speaker:2" ...) is left untouched.
//   2. Token -> speaker: the (track, speaker) whose turns overlap the token's [startMs, endMs] the longest
//      (a zero-length token counts as 1 ms at its start). No overlap: the nearest turn within snapMs; else
//      undecided.
//   3. Blocks: tokens separated by a pause > blockGapMs (the transcript's own line gap, 1.5 s) form a block.
//      Inside a block an undecided token takes the block's majority speaker (by overlap time); a block
//      without any decided token takes the nearest turn's speaker (any distance), failing that the old label.
//   4. Splits / smoothing: inside a block the speaker changes only at a CLEAR change: a run of tokens of one
//      speaker shorter than minRunTokens tokens or minRunMs is an island and joins its neighbour (the
//      previous run, at the block's start the next one). A block that is a single short run keeps its own
//      speaker (a backchannel "igen" between pauses is not swallowed). The line split itself follows from
//      MergedTranscript::segments() (a speaker change starts a new line).
//   5. Labels: the raw speaker key, "speaker:N" (domain/SpeakerKeys.h) (N from 1 in the order of the speakers'
//      first token), so identification, the editor, the review and the participant code work unchanged.
//      Speakers of different tracks are different raw speakers (no cross-track matching).
//
// All times are meeting time: the transcript comes from the mixdown (meeting time), the turns are already
// shifted by their track's startOffsetMs (IDiarizer).
#include "tanara/domain/Transcript.h"
#include "tanara/providers/voice/IDiarizer.h"

#include <QHash>
#include <QString>

namespace tanara::processing::diarizationmerge {

struct Options {
    qint64 snapMs = 1000;        // rule 2: the farthest turn a token without overlap may snap to
    qint64 blockGapMs = 1500;    // rule 3: a longer pause starts a new block (= MergedTranscript's line gap)
    int    minRunTokens = 2;     // rule 4: a clear change has at least this many tokens ...
    qint64 minRunMs = 400;       // ... and lasts at least this long
};

// The transcript metadata value of MergedTranscript::speakerSource after a merge.
inline const QString kSourceLocalDiarization = QStringLiteral("local-diarization");

// The raw label of the n-th speaker (1-based): "speaker:n" (speakerkeys::rawSpeakerKey).
QString rawLabel(int n);
// The key of a diarized speaker: "<trackId>#<index>".
QString speakerKey(const QString& trackId, int index);

// Rule 1: whether the transcript lacks speaker separation (non-empty, at most one distinct label).
bool needsSpeakers(const MergedTranscript& t);

struct MergeResult {
    bool applied = false;
    QString reason;                          // why not applied ("" when applied)
    QHash<QString, QString> labels;          // speakerKey -> raw label (only speakers that got tokens)
    int changedTokens = 0;                   // tokens whose label changed
};

// Rules 2-5 on `t` in place (also sets speakerSource / speakerModels from the result). Not applied (t
// unchanged): the transcript already has speakers, has no tokens, or the diarization has no turns.
MergeResult apply(MergedTranscript& t, const providers::DiarizeResult& d, const Options& opts = {});

} // namespace tanara::processing::diarizationmerge
