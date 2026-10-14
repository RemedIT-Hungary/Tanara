#include "tanara/library/MeetingLayout.h"

#include <QDir>

namespace tanara::library {

QString MeetingLayout::path(const QString& meetingFolder, const QString& name)
{
    if (meetingFolder.isEmpty()) return QString();
    return QDir(meetingFolder).filePath(name);
}

QString MeetingLayout::meetingJson()              { return QStringLiteral("meeting.json"); }
QString MeetingLayout::processingJson()           { return QStringLiteral("processing.json"); }
QString MeetingLayout::profileJson()              { return QStringLiteral("profile.json"); }

QString MeetingLayout::trackFilePrefix()          { return QStringLiteral("track_"); }
QString MeetingLayout::trackFilePattern()         { return trackFilePrefix() + QStringLiteral("*.ogg"); }
QString MeetingLayout::mixdownFile()              { return QStringLiteral("mixdown.mp3"); }
QString MeetingLayout::mixdownPartFile()          { return QStringLiteral("mixdown.part.mp3"); }
QString MeetingLayout::mixdownOf(const Meeting& m)
{
    return m.mixdownFile.isEmpty() ? mixdownFile() : m.mixdownFile;
}
QString MeetingLayout::waveformCache(const QString& audioPath) { return audioPath + QStringLiteral(".peaks.json"); }

QString MeetingLayout::tokensJson()               { return QStringLiteral("transcript.tokens.json"); }
QString MeetingLayout::segmentsJson()             { return QStringLiteral("transcript.segments.json"); }
QString MeetingLayout::transcriptMd()             { return QStringLiteral("transcript.md"); }
QString MeetingLayout::overlayJson()              { return QStringLiteral("transcript.speakers.json"); }
QString MeetingLayout::embeddingCacheSuffix()     { return QStringLiteral(".embeddings.bin"); }
QString MeetingLayout::utteranceEmbeddings()      { return QStringLiteral("transcript") + embeddingCacheSuffix(); }
QString MeetingLayout::textEmbeddings()           { return QStringLiteral("text") + embeddingCacheSuffix(); }
QString MeetingLayout::transcriptBackupPrefix()   { return QStringLiteral("transcript-backup-"); }
QString MeetingLayout::backupSpeakerMapJson()     { return QStringLiteral("speakerMap.json"); }

QString MeetingLayout::trackActivity()            { return QStringLiteral("tracks.activity.bin"); }
QString MeetingLayout::trackActivityView()        { return QStringLiteral("tracks.activity.view.bin"); }
QString MeetingLayout::participantsAnalysisJson() { return QStringLiteral("participants.analysis.json"); }

QString MeetingLayout::summaryMd()                { return QStringLiteral("summary.md"); }
QString MeetingLayout::summaryJson()              { return QStringLiteral("summary.json"); }
QString MeetingLayout::topicsJson()               { return QStringLiteral("summary.topics.json"); }
QString MeetingLayout::analysesJson()             { return QStringLiteral("summary.analyses.json"); }
QString MeetingLayout::summaryNotesCache()        { return QStringLiteral("summary.notes.json"); }

QStringList MeetingLayout::transcriptFiles()
{
    return {transcriptMd(), tokensJson(), segmentsJson(), overlayJson()};
}

QStringList MeetingLayout::allNames()
{
    return {meetingJson(), processingJson(), profileJson(), mixdownFile(), mixdownPartFile(),
            tokensJson(), segmentsJson(), transcriptMd(), overlayJson(), utteranceEmbeddings(),
            textEmbeddings(), backupSpeakerMapJson(), trackActivity(), trackActivityView(),
            participantsAnalysisJson(), summaryMd(), summaryJson(), topicsJson(), analysesJson(),
            summaryNotesCache()};
}

} // namespace tanara::library
