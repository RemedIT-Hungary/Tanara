#pragma once
//
// Domain: a meeting and its tracks.
#include "tanara/domain/Audio.h"
#include "tanara/domain/Participant.h"
#include "tanara/domain/Person.h"

#include <QDateTime>
#include <QMap>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace tanara {

// ---- track ------------------------------------------------------------------
struct Track {
    QString id;             // pl. "mic", "loopback", deviceslug
    QString deviceName;
    QString file;           // relative to the meeting folder (e.g. "track_mic.ogg")
    QString speakerLabel;   // the (fixed) speaker belonging to this track
    TrackKind kind = TrackKind::Mic;
    bool fixedSpeaker = false;
    int sampleRate = 48000;
    int channels = 1;
    bool active = true;     // false = judged silent after recording and dropped (the file STAYS)
    float peakLevel = 0.0f; // peak RMS during recording (for the keep/drop decision)
    QString customName;     // track name given by the user; empty = friendly name (TrackCatalog)
    // The track's audio starts this much LATER than the recording's zero point (ms). 0 = started together.
    // The file is the span actually recorded (NO silence padding): the recorder writes here the
    // open time of a device track enabled mid-recording, or of each span of a device toggled
    // on and off (new file: track_<slug>-2.ogg ...). The mixdown shifts by that much silence
    // (adelay); readers of the track file shift meeting time by it (tracktiming). For an old /
    // imported folder the CLI `align` command sets it (--auto: assumes a common end).
    qint64 startOffsetMs = 0;
    // Share of speech-level frames in the track's covered time (0..1); -1 = not measured yet.
    // Filled by the speech check after recording / import (editing/session/TrackSpeech.h).
    double speechRatio = -1.0;
    // Why it was left out of the mixdown: "" = included, "noSpeech" = no speech on it
    // (automatic), "manual" = the user removed it. A left-out track also has active == false.
    QString excludedReason;
    // Whether it goes into the mixdown (and the analyses).
    bool included() const { return active && excludedReason.isEmpty(); }
};

// ---- meeting --------------------------------------------------------------
struct Meeting {
    QString id;
    QString title;
    QString folder;          // absolute folder path
    QDateTime startedAt;
    qint64 durationMs = 0;
    QVector<Track> tracks;
    QString mixdownFile;     // pl. "mixdown.mp3"
    bool mixdownDirty = false;   // true = the tracks changed since the mixdown (remix needed)
    bool hasTranscript = false;
    bool hasSummary = false;
    QMap<QString, PersonId> speakerMap;  // raw speaker label ("Remote 1") -> the person (people.json)
    // A few words from the user describing the meeting (topic/context). It goes into the STT
    // context envelope (more accurate transcript for doubtful parts) AND into the LLM summary
    // context. Editable in the pre-transcript view and on the Summary tab; names, jargon and
    // known mishearings belong here ("X" is correctly: ...).
    QString contextNote;
    // "How many speakers?" the user gave for the local diarization (0 = automatic). Applies per unbound
    // track; "diarizeSpeakers" in meeting.json, written only when set.
    int diarizeSpeakerCount = 0;
    // The call app detected by the watcher (e.g. "Microsoft Teams"); empty for a manual recording.
    // Informational only: not sent to the transcriber or the summarizer.
    QString detectedCallApp;
    // Ids of the meeting's tags, in the order they were applied (the tag set lives in
    // <metadataDir>/tags.json, see library/tags/TagService.h). "tags" in meeting.json.
    QStringList tagIds;
    // Participants (manual + voice-analysis candidates) and the approval. "participants" /
    // "participantApproval" in meeting.json.
    QVector<Participant> participants;
    std::optional<ParticipantApproval> approval;
};

// ---- track timing: meeting time -> track-file time -----------------------------
// A track's file contains the span ACTUALLY recorded: its zero point is at the meeting's
// Track::startOffsetMs. Stored time ranges are in MEETING time; convert before reading a track file.
namespace tracktiming {

// A time range within a file (ms). Invalid (valid() == false) if the requested meeting range falls
// entirely before the track's start, or is empty.
struct FileRange {
    qint64 startMs = -1;
    qint64 endMs = -1;
    bool valid() const { return startMs >= 0 && endMs > startMs; }
};

// Meeting time [startMs, endMs) -> the track file's time. The part before the track's start is cut
// off (there is no audio before the file's zero point); if fileDurationMs >= 0, the part after the
// file's end too. If nothing remains, it returns an invalid range.
FileRange meetingToFileRange(qint64 offsetMs, qint64 startMs, qint64 endMs,
                             qint64 fileDurationMs = -1);

// The same for a track (with its startOffsetMs).
inline FileRange fileRange(const Track& track, qint64 startMs, qint64 endMs,
                           qint64 fileDurationMs = -1)
{
    return meetingToFileRange(track.startOffsetMs, startMs, endMs, fileDurationMs);
}

} // namespace tracktiming

} // namespace tanara

Q_DECLARE_METATYPE(tanara::Track)
Q_DECLARE_METATYPE(tanara::Meeting)
