#pragma once
//
// MixdownService: remixes a meeting's mixdown.mp3 from its ACTIVE tracks (MixdownPlan -> the media
// port's filter graph), asynchronously, one run per meeting.
//
//  - The new mix is written to mixdown.part.mp3 and replaces the old one only on success: after an
//    error / cancel the earlier (playable) mix is untouched and no half-made file stays.
//  - With an active track's file missing, an existing (complete) mix is NOT replaced by a partial one.
//  - A standalone mix is its own cancellable task (JobKind::Mixdown in the MeetingJobTracker). A mix
//    that runs as a stage of a transcription is not (see setTranscriptionStageProbe): the caller
//    reports that stage from progress().
// UI-independent (QtCore).
#include "tanara/media/MediaTool.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

namespace tanara::library {
class MeetingJobTracker;
class MeetingRepository;
}

namespace tanara::recording {

class MixdownService : public QObject {
    Q_OBJECT
public:
    MixdownService(library::MeetingRepository* store, library::MeetingJobTracker* jobs,
                   media::MediaToolPtr media, QObject* parent = nullptr);
    ~MixdownService() override;

    // "This meeting's mix runs as a stage of its transcription": then no separate Mixdown task is begun.
    void setTranscriptionStageProbe(std::function<bool(const QString& meetingId)> probe);

    // Starts the remix. A no-op while a mix of the meeting is running (that result serves everyone).
    // The outcome always comes as finished(meetingId, ok), synchronously when the mix cannot even start.
    void regenerate(const QString& meetingId);

    bool running(const QString& meetingId) const;

    // Cancels the standalone mix task (JobKind::Mixdown). False if none is running.
    bool cancel(const QString& meetingId);
    // Stops the meeting's running ffmpeg regardless of the task (a transcription's own mix stage).
    // False if no mix runs.
    bool cancelProcess(const QString& meetingId);

    // The reason of the last mix that did not even start (e.g. which track's file is missing); taken once.
    QString takeFailReason(const QString& meetingId);

signals:
    void progress(QString meetingId, int percent);   // 0..100
    // The result of regenerate() (every path; ok == false also on cancel).
    void finished(QString meetingId, bool ok);
    // An ffmpeg run ended (after finished()): the meeting's mixdown state may have changed.
    void runEnded(QString meetingId, bool ok);
    void error(QString message);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace tanara::recording
