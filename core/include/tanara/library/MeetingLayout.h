#pragma once
//
// MeetingLayout: the names of every file inside a meeting folder, in ONE place.
//
// A meeting folder (<recording root>/yyyy-MM-dd_HHmm_<title slug>_<short id>) holds the recording
// (the track files, written by the recorder; their names are the recorder's business and live in
// meeting.json), the mixdown, the transcript, the summary and a few derived caches. Every other
// module asks this class for a name instead of spelling a literal; the repositories
// (MeetingRepository, TranscriptRepository, SummaryRepository) build their paths from it.
//
// All names are RELATIVE to the meeting folder; path(folder, name) joins them.
#include "tanara/domain/Meeting.h"

#include <QString>
#include <QStringList>

namespace tanara::library {

class MeetingLayout {
public:
    // <folder>/<name> (a clean path; an empty folder gives an empty path).
    static QString path(const QString& meetingFolder, const QString& name);

    // ---- the meeting itself ----------------------------------------------------------------
    static QString meetingJson();               // meeting.json (Meeting, see MeetingRepository)
    static QString processingJson();            // processing.json (MeetingJobTracker state)
    static QString profileJson();               // profile.json (MeetingProfiles: speakers' talk share)

    // ---- recording --------------------------------------------------------------------------
    static QString trackFilePattern();          // track_*.ogg (recovery of a folder without meeting.json)
    static QString trackFilePrefix();           // "track_"
    static QString mixdownFile();               // mixdown.mp3 (the default mixdown name)
    static QString mixdownPartFile();           // mixdown.part.mp3 (being encoded)
    static QString mixdownOf(const Meeting& m); // the meeting's mixdown (relative): its mixdownFile or the default
    static QString waveformCache(const QString& audioPath);   // <audio>.peaks.json (next to the audio file)

    // ---- transcript -------------------------------------------------------------------------
    static QString tokensJson();                // transcript.tokens.json (the raw STT tokens, the truth)
    static QString segmentsJson();              // transcript.segments.json (the lines)
    static QString transcriptMd();              // transcript.md (human-readable, regenerated)
    static QString overlayJson();               // transcript.speakers.json (the editor's speaker layer)
    static QString utteranceEmbeddings();       // transcript.embeddings.bin (per-line voice vectors)
    static QString textEmbeddings();            // text.embeddings.bin (semantic search index)
    static QString embeddingCacheSuffix();      // ".embeddings.bin" (every embedding cache ends so)
    static QString transcriptBackupPrefix();    // "transcript-backup-" (folder of a pre-retranscribe copy)
    static QString backupSpeakerMapJson();      // speakerMap.json (inside such a backup folder)

    // ---- analysis caches --------------------------------------------------------------------
    static QString trackActivity();             // tracks.activity.bin
    static QString trackActivityView();         // tracks.activity.view.bin
    static QString participantsAnalysisJson();  // participants.analysis.json

    // ---- summary ----------------------------------------------------------------------------
    static QString summaryMd();                 // summary.md
    static QString summaryJson();               // summary.json (structured summary + provenance)
    static QString topicsJson();                // summary.topics.json (editable topic list, pass 1)
    static QString analysesJson();              // summary.analyses.json (finished per-topic analyses)
    static QString summaryNotesCache();         // summary.notes.json (quick summary's part notes)

    // The transcript-derived files a re-transcription replaces (copied into a backup first).
    static QStringList transcriptFiles();
    // Every name above (for tests: no two coincide).
    static QStringList allNames();
};

} // namespace tanara::library
