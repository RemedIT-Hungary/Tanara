#pragma once
//
// DiarizationService (D4): the "diarize" stage of the transcription chain when the STT provider did not
// separate the speakers. One run per meeting:
//
//   1. the request: every INCLUDED track of the meeting (its file, startOffsetMs; the mixdown is NOT used);
//      a track becomes single-speaker (DiarizeTrack::knownPerson) ONLY when exactly one named participant
//      is bound to it by hand (Participant::tracks) - never from the track kind (no "mic = owner");
//   2. the diarizer (IDiarizer from the deps' factory: processing::LocalDiarizer in the app) on a worker
//      thread; its progress moves the Transcribe job's "diarize" stage;
//   3. the merge (processing::diarizationmerge) into the current transcript and the save through the deps
//      (TranscriptionService::replaceTranscript: the normal transcript files + the "speakers" metadata).
//
// The caller (app::Workflows) decides whether to run (provider trait, setting, usable model), starts it
// after the STT step and continues the chain in the callback. The stage is closed here (Done / Failed);
// the job is NOT (the caller continues with the identification, or closes it on cancel).
// A failure is not fatal for the chain: the transcript stays as the STT gave it (one speaker).
#include "tanara/domain/Meeting.h"
#include "tanara/domain/Transcript.h"
#include "tanara/providers/voice/IDiarizer.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

namespace tanara::library {
class MeetingJobTracker;
class MeetingRepository;
}

namespace tanara::processing {

struct DiarizationOutcome {
    bool ok = false;                 // the diarizer ran and the transcript was checked
    bool applied = false;            // ... and got the speakers (saved)
    bool cancelled = false;
    int  rawSpeakers = 0;            // raw speakers written into the transcript
    QString message;                 // why not ok / not applied (English, for the log and the stage detail)
    providers::DiarizeResult result; // the diarizer's result (ok only)
};

class DiarizationService : public QObject {
    Q_OBJECT
public:
    struct Deps {
        library::MeetingRepository* store = nullptr;
        library::MeetingJobTracker* jobs = nullptr;      // the meeting's Transcribe job ("diarize" stage); may be null
        // A new diarizer (the caller's child); nullptr = none available.
        std::function<providers::IDiarizer*(QObject* parent)> diarizer;
        // The meeting's current transcript (the STT step's cache, else the disk).
        std::function<MergedTranscript(const QString& meetingId)> transcript;
        // Saves the merged transcript (the normal transcript files, a new transcript generation).
        std::function<void(const QString& meetingId, const MergedTranscript& merged)> saveTranscript;
    };
    using Done = std::function<void(const DiarizationOutcome&)>;

    explicit DiarizationService(Deps deps, QObject* parent = nullptr);
    ~DiarizationService() override;

    // The request for a meeting (exposed for the tests). Empty: no included track with a file.
    static providers::DiarizeRequest requestFor(const Meeting& m);

    // Starts the run; `done` is called exactly once on the main thread (also on cancel, with cancelled),
    // never from inside start(). false: nothing started (unknown meeting, no track, the transcript already
    // has speakers, no diarizer) and `done` is not called.
    bool start(const QString& meetingId, Done done);
    bool running(const QString& meetingId) const { return m_runs.contains(meetingId); }
    // Stops the diarizer; `done` follows with cancelled (the transcript untouched).
    bool cancel(const QString& meetingId);

signals:
    void jobProgress(QString meetingId, QString message);

private:
    struct Run {
        QPointer<providers::DiarizeJob> job;
        QObject* owner = nullptr;    // the diarizer instance (deleted with the run)
        Done done;
    };
    void finish(const QString& meetingId, const DiarizationOutcome& outcome);

    Deps m_deps;
    QHash<QString, Run> m_runs;
};

} // namespace tanara::processing
