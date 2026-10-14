#pragma once
//
// TranscriptionService: ONE step of the transcription chain (cleanup phase 2, P12f): the speech-to-text of a
// meeting's finished mixdown with the selected STT provider (a single request; speaker separation by the
// provider's diarization, "Speaker N" labels), the merge, and the save through library::TranscriptRepository
// (transcript.md + tokens + segments, meeting.hasTranscript).
//
// The step reports into the meeting's Transcribe job, which the CALLER begins (app::Workflows): the
// "upload", "transcribe" and "diarize" stages, the estimate, and the job's end when the step fails or is
// cancelled. On success transcriptReady and then transcribed go out; the caller continues (identification)
// and closes the job.
//
// Where the audio goes and who pays (own key / Tanara Cloud run) is the app's TranscriptionPort. Moved
// unchanged from AppController::transcribeFromMixdown.
#include "tanara/domain/Transcript.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

namespace tanara::library {
class JobStats;
class MeetingJobTracker;
class MeetingRepository;
}
namespace tanara::providers {
class ProviderRegistry;
class SttJob;
}

namespace tanara::processing {

class TranscriptionPort;

class TranscriptionService : public QObject {
    Q_OBJECT
public:
    struct Deps {
        library::MeetingRepository*  store = nullptr;
        library::MeetingJobTracker*  jobs = nullptr;       // the meeting's Transcribe job (begun by the caller)
        library::JobStats*           jobStats = nullptr;   // speed of earlier runs (estimates); may be null
        providers::ProviderRegistry* registry = nullptr;   // the STT providers
        TranscriptionPort*           port = nullptr;       // settings, key, Tanara Cloud run (the app)
    };

    explicit TranscriptionService(Deps deps, QObject* parent = nullptr);
    ~TranscriptionService() override;

    // Starts the STT of the meeting's mixdown (it must exist). clearSpeakers: the old speaker assignments go
    // with the new transcript (they always go when a transcript existed before). false: the step did not
    // start, and the job is already closed (unknown meeting: cancelled; unknown provider: failed).
    bool start(const QString& meetingId, bool clearSpeakers = false);
    bool running(const QString& meetingId) const;
    // Cancels the provider job: it cleans up at the provider; the job closes as cancelled (no error, the
    // earlier transcript untouched). false: no transcription runs. The caller marks the job "cancelling".
    bool cancel(const QString& meetingId);

    // The in-memory transcript of the last fresh transcription (empty: read it from disk).
    MergedTranscript cachedTranscript(const QString& meetingId) const;
    // Drops the meeting's in-memory state (the meeting is deleted).
    void forget(const QString& meetingId);
    // The transcript's identity (generation + the tokens file's state): an identification that started on an
    // older transcript drops its result.
    QString transcriptStamp(const QString& meetingId, const QString& folder) const;

signals:
    void jobProgress(QString meetingId, QString message);
    // The new transcript is saved (meeting.hasTranscript, transcript.md).
    void transcriptReady(QString meetingId, QString markdownPath);
    // After transcriptReady and the run's close: the caller continues the chain and closes the job.
    void transcribed(QString meetingId);
    void errorOccurred(QString message);

private:
    struct Run {
        QPointer<providers::SttJob> job;
        bool cancelRequested = false;
        bool clearSpeakers = false;
    };

    Deps m_deps;
    QHash<QString, Run> m_runs;
    QHash<QString, MergedTranscript> m_cache;
    // Per meeting the transcript's "generation": every new transcript write increments it.
    QHash<QString, int> m_generation;
};

} // namespace tanara::processing
