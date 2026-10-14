#pragma once
//
// Participant-identification port (CapabilityKind::ParticipantIdentification): who is behind a set of
// voice vectors. The input is READY vectors (IVoiceEmbedder's output), never audio: the Cloud variant
// sends only vectors (privacy mode, D5).
//
// The client sends the candidates (their voiceprints) with every request; the server does NOT keep a
// voiceprint directory (decision 2026-10-14, gateway question 5).
//
// One probe = one speaker cluster of a track (its centroid vector per model) -> one ParticipantGuess
// (the probe's track / device / windows / sample are copied; name + score are the identification:
// the best candidate if its score reaches the threshold, otherwise an empty name).
//
// Implementations: processing::LocalParticipantIdentifier (Origin::Local), cloud::CloudParticipantIdentifier
// (Origin::Cloud, /v1/voice/identify).
#include "tanara/domain/Participant.h"
#include "tanara/domain/Voice.h"
#include "tanara/providers/core/CapabilityJob.h"
#include "tanara/providers/core/ProviderDescriptor.h"
#include "tanara/providers/voice/IVoiceEmbedder.h"

#include <QString>
#include <QVector>

namespace tanara::providers {

// A known person and their voiceprints (one per model and sample).
// TODO(P11): a stable PersonId next to the name, once the domain has it.
struct IdentifyCandidate {
    QString name;
    QVector<Voiceprint> prints;
};

struct IdentifyProbe {
    QVector<VoiceVector> vectors;   // one per model (the cluster's centroid)
    QString trackId;
    QString deviceName;
    int     windows = 0;
    QString sampleRef;
};

struct IdentifyMeetingContext {
    QString meetingId;
    QString title;
    QStringList invited;            // names expected in the meeting (a hint only)
};

struct IdentifyRequest {
    QVector<IdentifyProbe>     probes;
    QVector<IdentifyCandidate> candidates;
    // Minimum cosine score of a match; below it the guess gets an empty name. < 0 = provider default.
    double threshold = -1.0;
    IdentifyMeetingContext meetingContext;
};

using IdentifyJob = ResultJob<QVector<ParticipantGuess>>;

class IParticipantIdentifier {
public:
    virtual ~IParticipantIdentifier() = default;
    virtual QString name() const = 0;
    // One guess per probe, in probe order.
    virtual IdentifyJob* identify(const IdentifyRequest& req) = 0;
};

template <> struct ProviderTraits<IParticipantIdentifier> {
    static constexpr CapabilityKind kind = CapabilityKind::ParticipantIdentification;
};

} // namespace tanara::providers
