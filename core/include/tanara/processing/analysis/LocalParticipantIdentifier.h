#pragma once
//
// LocalParticipantIdentifier: the local IParticipantIdentifier (Origin::Local). The matching rule
// of the voiceprint database (library::VoiceprintStore::bestMatch), applied to the candidates of the
// request instead of the stored database:
//   per candidate per model: the MAX cosine over the candidate's voiceprints of that model;
//   the candidate's score: the mean of these over the shared models (the probe has a vector AND the
//   candidate has a voiceprint of the model); no shared model -> -1.
// The best candidate becomes the guess's name if its score reaches the threshold
// (kDefaultThreshold = AppController's voice-match threshold); otherwise the name stays empty.
// Pure computation, delivered in the next event-loop turn.
#include "tanara/providers/voice/IParticipantIdentifier.h"

#include <QObject>

namespace tanara::processing {

class LocalParticipantIdentifier : public QObject, public providers::IParticipantIdentifier {
    Q_OBJECT
public:
    static inline const QString Id = QStringLiteral("local");
    static constexpr double kDefaultThreshold = 0.5;

    explicit LocalParticipantIdentifier(QObject* parent = nullptr) : QObject(parent) {}

    QString name() const override { return Id; }
    providers::IdentifyJob* identify(const providers::IdentifyRequest& req) override;

    // The pure rule (tests, the CLI).
    static QVector<ParticipantGuess> identifyNow(const providers::IdentifyRequest& req);
    static double score(const providers::IdentifyProbe& probe, const providers::IdentifyCandidate& candidate);
};

} // namespace tanara::processing
