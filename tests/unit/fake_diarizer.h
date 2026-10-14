#pragma once
// A scripted IDiarizer for the tests of the diarization slices (D3+): no model, no audio decoding, no
// network. By default every job completes in the next event-loop turn with the canned result (or the
// one the `script` function builds from the request); with `manual = true` the test finishes / fails
// the jobs by hand (cancel and progress tests). Every request and job is logged.
#include "tanara/providers/voice/IDiarizer.h"

#include <QObject>
#include <QPointer>
#include <QVector>

#include <functional>

namespace tanara::testing {

class FakeDiarizeJob final : public providers::DiarizeJob {
public:
    using providers::DiarizeJob::DiarizeJob;
    bool cancelRequested = false;
protected:
    void onCancel() override { cancelRequested = true; }
};

class FakeDiarizer final : public QObject, public providers::IDiarizer {
public:
    explicit FakeDiarizer(QObject* parent = nullptr) : QObject(parent) {}

    // The result of a job without a script: for every track with input, two speakers alternating in
    // 1 s turns (0-1 s speaker 0, 1-2 s speaker 1), shifted by the track's startOffsetMs; a track bound to
    // a person is a single speaker 0 with `person` set. Centroids are tiny 2-d vectors (model "fake").
    static providers::DiarizeResult defaultResult(const providers::DiarizeRequest& req)
    {
        providers::DiarizeResult r;
        for (const providers::DiarizeTrack& t : req.tracks) {
            if (!t.hasInput()) continue;
            const bool bound = !t.knownPerson.isNull();
            const int speakers = bound ? 1 : 2;
            for (int s = 0; s < speakers; ++s) {
                providers::SpeakerTurn turn;
                turn.trackId = t.trackId;
                turn.startMs = t.startOffsetMs + (bound ? 0 : s * 1000);
                turn.endMs = turn.startMs + (bound ? 2000 : 1000);
                turn.speakerIndex = s;
                r.turns.append(turn);

                providers::DiarizedSpeaker sp;
                sp.trackId = t.trackId;
                sp.index = s;
                sp.totalMs = turn.durationMs();
                sp.person = bound ? t.knownPerson : PersonId();
                providers::VoiceVector v;
                v.vector = {s == 0 ? 1.0f : 0.0f, s == 0 ? 0.0f : 1.0f};
                v.dim = 2;
                v.modelId = QStringLiteral("fake");
                sp.centroid.vectors.append(v);
                sp.centroid.trackId = t.trackId;
                sp.centroid.windows = 1;
                r.speakers.append(sp);
            }
        }
        std::sort(r.turns.begin(), r.turns.end(), [](const providers::SpeakerTurn& a, const providers::SpeakerTurn& b) {
            return a.startMs != b.startMs ? a.startMs < b.startMs : a.trackId < b.trackId;
        });
        return r;
    }

    QString name() const override { return QStringLiteral("fake-diarizer"); }

    providers::DiarizeJob* diarize(const providers::DiarizeRequest& req) override
    {
        requests.append(req);
        auto* job = new FakeDiarizeJob(this);
        jobs.append(job);
        if (req.isEmpty()) {
            job->failLater(providers::inputError(QStringLiteral("no_input"), QStringLiteral("no track has input")));
        } else if (!manual) {
            job->completeLater(script ? script(req) : defaultResult(req));
        }
        return job;
    }

    bool manual = false;                                                   // the test finishes the jobs
    std::function<providers::DiarizeResult(const providers::DiarizeRequest&)> script;
    QVector<providers::DiarizeRequest> requests;
    QVector<QPointer<FakeDiarizeJob>> jobs;
};

} // namespace tanara::testing
