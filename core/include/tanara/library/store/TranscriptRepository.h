#pragma once
//
// TranscriptRepository: the transcript files of a meeting folder.
//
//   transcript.tokens.json    the STT tokens (the truth the lines and names are derived from)
//   transcript.segments.json  the lines (start, end, raw speaker label, text)
//   transcript.md             the human-readable transcript (regenerated, LF line endings)
//   transcript.speakers.json  the editor's speaker layer (overlay: manual speaker corrections, domain/Overlay.h)
//
// The file I/O is stateless (static members, callable from any thread). An instance adds a small
// cache for questions the UI asks often (speakerCount); the owner invalidates it when a transcript
// changes (AppController: transcriptReady / speakerMapChanged).
// The overlay's operations (resolution, staleness, person references) stay in the editing module
// (editing/session/SpeakerOverlay.h); only its file I/O is here.
#include "tanara/domain/Overlay.h"
#include "tanara/domain/Transcript.h"

#include <QHash>
#include <QMutex>
#include <QString>
#include <QVector>

namespace tanara::library {

class TranscriptRepository {
public:
    static QString tokensPath(const QString& meetingFolder);
    static QString segmentsPath(const QString& meetingFolder);
    static QString markdownPath(const QString& meetingFolder);

    static bool hasTokens(const QString& meetingFolder);
    // Missing / unreadable file: an empty transcript.
    static MergedTranscript readTokens(const QString& meetingFolder);
    static bool writeTokens(const QString& meetingFolder, const MergedTranscript& mt);

    // The lines; a missing / unreadable file gives none. Both the plain array and the
    // {"segments": [...]} wrapper are accepted on reading.
    static QVector<Utterance> readSegments(const QString& meetingFolder);
    static bool writeSegments(const QString& meetingFolder, const QVector<Utterance>& segments);

    // transcript.md; maxBytes < 0 reads it whole. Missing: empty.
    static QString readMarkdown(const QString& meetingFolder, qint64 maxBytes = -1);
    static bool writeMarkdown(const QString& meetingFolder, const QString& markdown);

    // ---- the editor's lines and speaker layer (overlay) ----
    static QString overlayPath(const QString& meetingFolder);    // transcript.speakers.json
    // The lines of transcript.segments.json (a plain array) with stable ids ("u<startMs>", a "-2", "-3" ...
    // suffix on collision in file order). Empty if there is no transcript.
    static QVector<TranscriptLine> readLines(const QString& meetingFolder);
    // Fingerprint of the lines (starts/ends/labels); shows if an overlay belongs to an earlier transcript.
    static QString linesFingerprint(const QVector<TranscriptLine>& lines);
    // The overlay raw, as on disk (a missing file = an empty overlay).
    static SpeakerOverlay readOverlay(const QString& meetingFolder);
    // The same, but VALIDATED against the given lines: if the fingerprint does not match, the line-/speaker-
    // level corrections are dropped (identification + stale flag stay).
    static SpeakerOverlay readOverlayFor(const QString& meetingFolder, const QVector<TranscriptLine>& lines);
    // Save (QSaveFile). A completely empty overlay deletes the file.
    static bool writeOverlay(const QString& meetingFolder, const SpeakerOverlay& overlay);

    // Number of distinct raw speaker labels in the lines (0 if there is no transcript). Cached per
    // folder by the file's modification time and size, so asking often costs nothing.
    int speakerCount(const QString& meetingFolder) const;
    // Drops the cached answers of a folder (empty = every folder).
    void invalidate(const QString& meetingFolder = QString());

private:
    struct CountEntry { QString key; int count = 0; };
    mutable QMutex m_mutex;
    mutable QHash<QString, CountEntry> m_counts;   // folder -> (file stamp, speaker count)
};

} // namespace tanara::library
