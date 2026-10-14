#pragma once
//
// TranscriptRepository: the transcript files of a meeting folder.
//
//   transcript.tokens.json    the STT tokens (the truth the lines and names are derived from)
//   transcript.segments.json  the lines (start, end, raw speaker label, text)
//   transcript.md             the human-readable transcript (regenerated, LF line endings)
//
// The file I/O is stateless (static members, callable from any thread). An instance adds a small
// cache for questions the UI asks often (speakerCount); the owner invalidates it when a transcript
// changes (AppController: transcriptReady / speakerMapChanged).
// (The editor's speaker layer, transcript.speakers.json, is still read and written by the editing
// module's SpeakerOverlay; its name comes from MeetingLayout::overlayJson().)
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
