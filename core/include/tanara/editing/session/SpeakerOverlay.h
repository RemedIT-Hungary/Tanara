#pragma once
//
// SpeakerOverlay: the PERSISTENT layer of a meeting's manual speaker corrections
// (<meeting folder>/transcript.speakers.json). The raw diarization (transcript.tokens.json /
// transcript.segments.json) stays untouched; corrections layer on top, so they are undoable and
// survive rerunning participant identification. Dropped on re-transcription (the utterance
// boundaries change).
//
// Resolution order of the displayed name:
//   per-line override -> (redirect of a merged raw label) -> Meeting.speakerMap -> raw label.
// The semantics of Meeting.speakerMap are unchanged (the CLI reads/writes it).
// The overlay's DATA (TranscriptLine, SpeakerOverlay ...) is in domain/Overlay.h; the operations are here.
#include "tanara/domain/Meeting.h"
#include "tanara/domain/Overlay.h"
#include "tanara/domain/Summary.h"
#include "tanara/domain/Transcript.h"
#include "tanara/editing/session/SpeakerEditTypes.h"

#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace tanara::editing {

namespace speakeredit {

// ---- files ---------------------------------------------------------------
QString overlayPath(const QString& meetingFolder);      // transcript.speakers.json
QString segmentsPath(const QString& meetingFolder);     // transcript.segments.json
QString mixdownPath(const Meeting& m);                  // the voice-ID audio source (may not exist)

// The lines of transcript.segments.json with stable ids. Empty if there is no transcript.
QVector<TranscriptLine> loadTranscriptLines(const QString& meetingFolder);
// Fingerprint of the transcript (line starts/ends/labels); shows if the overlay belongs to an
// earlier transcript.
QString transcriptFingerprint(const QVector<TranscriptLine>& lines);

// The overlay raw, as on disk (a missing file = an empty overlay; old meetings load
// unchanged that way).
SpeakerOverlay loadOverlay(const QString& meetingFolder);
// The same, but VALIDATED against the current transcript: if the fingerprint does not match,
// the line-/speaker-level corrections are dropped (identification + stale flag stay).
SpeakerOverlay loadOverlayFor(const QString& meetingFolder, const QVector<TranscriptLine>& lines);
// Save (QSaveFile). A completely empty overlay deletes the file.
bool saveOverlay(const QString& meetingFolder, const SpeakerOverlay& overlay);

// ---- resolution -------------------------------------------------------------
bool isParticipantKey(const QString& key);
// A line's resolved speaker key: override -> merge redirect -> raw label.
QString resolveSpeakerKey(const SpeakerOverlay& ov, const TranscriptLine& line);
// The speaker's person (empty = anonymous) and displayed name.
QString speakerPerson(const SpeakerOverlay& ov, const QMap<QString, QString>& speakerMap,
                      const QString& key);
QString speakerDisplayName(const SpeakerOverlay& ov, const QMap<QString, QString>& speakerMap,
                           const QString& key);

// Writes the RESOLVED name onto the tokens' speaker (overlay + speakerMap). AppController
// calls this wherever names must go onto the transcript (transcript.md, summary input).
// Without an overlay = the old speakerMap application.
void applyResolvedSpeakers(MergedTranscript& mt, const Meeting& m);
// Regenerates transcript.md with the resolved names (from transcript.tokens.json).
bool regenerateTranscriptMarkdown(const Meeting& m);

// ---- summary staleness ---------------------------------------------
SummaryStaleInfo summaryStale(const Meeting& m);
// A speaker change from OUTSIDE the overlay (old UI rename, auto-identification): if there is a
// summary, marks it stale. true if the state changed.
bool markSummaryStale(const Meeting& m, const QStringList& speakerKeys);
// The same, but tells which keys were NEWLY added to the list (for undo).
QStringList markSummaryStaleKeys(const Meeting& m, const QStringList& speakerKeys);
// Undo of one markSummaryStaleKeys: the given keys leave the stale list. true if the state changed.
bool unmarkSummaryStale(const QString& meetingFolder, const QStringList& speakerKeys);
// Summary (re)generated OR accepted by the user ("Fine as is").
bool clearSummaryStale(const QString& meetingFolder);

// The transcript's utterances with their CURRENT resolved speaker (key + displayed name), for the
// summary's source references (a snapshot at creation, compared later). Empty if there is no
// transcript.segments.json.
QVector<tanara::summarysrc::SourceLine> resolvedSourceLines(const Meeting& m);
// Fills the targeted staleness in the info (the summary.json statements + the source lines'
// speaker then vs. now + the overlay's corrected keys). Does not touch info.stale /
// correctedSpeakers. statements != nullptr: the statements with their staleBecause fields.
void fillTargetedStale(const Meeting& m, SummaryStaleInfo& info,
                       QVector<SummaryStatement>* statements = nullptr);

// ---- AppController hooks ---------------------------------------------
// Records the voice-identification score (raw label -> person, cosine).
void recordIdentification(const QString& meetingFolder, const QString& rawLabel,
                          const QString& person, double score);
// Carries a global person rename / delete through the manually added participants.
bool renamePersonInOverlay(const QString& meetingFolder, const QString& oldName,
                           const QString& newName);
bool removePersonFromOverlay(const QString& meetingFolder, const QString& name);

// ---- re-transcription ----------------------------------------------------------
RetranscribeImpact retranscribeImpact(const Meeting& m);
// Copies the current transcript files (md, tokens, segments, speakers + the speakerMap) into
// the meeting folder's "transcript-backup-<timestamp>" subfolder. Returns the folder path (empty = error).
QString backupTranscript(const Meeting& m);
// A new transcript was made: deletes the overlay's line-level corrections + the utterance-embedding cache.
void discardForNewTranscript(const QString& meetingFolder);

} // namespace speakeredit
} // namespace tanara::editing