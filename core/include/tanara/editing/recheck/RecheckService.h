#pragma once
//
// RecheckService: the voice-based analysis of a speaker-editor session (SpeakerEditSession):
//  - the line embeddings: computed in the background through the voice-embedding port
//    (providers::IVoiceEmbedder) for the lines still lacking a vector for some model in use, in small
//    batches (progress), into the session's cache (transcript.embeddings.bin); at the end the session's
//    uncertainty flags and "To review" analysis refresh;
//  - the recheck from the confirmed lines and the pairwise review ("Review between A and B"): the pure
//    analysis (SpeakerAnalysis: computeUncertainRechecked / computePairRecheck) on the session's lines,
//    applied to the session as ONE undo step;
//  - the feedback texts of a recheck (reference composition, basis, very similar voices).
//
// The embedding source is either an IVoiceEmbedder (not owned; e.g. a registry provider) or a synchronous
// utterance-embedder factory (decode-once; wrapped per run into a FactoryVoiceEmbedder). The session must
// outlive the service, or be destroyed first: the service stops on the session's closing() signal.
#include "tanara/editing/session/SpeakerEditTypes.h"
#include "tanara/providers/voice/IUtteranceEmbedder.h"

#include <QObject>
#include <QStringList>
#include <QVector>
#include <QtNumeric>

#include <functional>
#include <memory>

namespace tanara::providers { class IVoiceEmbedder; }

namespace tanara::editing {

class SpeakerEditSession;

class RecheckService : public QObject {
    Q_OBJECT
public:
    explicit RecheckService(SpeakerEditSession* session, QObject* parent = nullptr);
    ~RecheckService() override;

    SpeakerEditSession* session() const;

    // ---- embedding source ------------------------------------------------
    // A voice-embedding provider (not owned; it must outlive the service). Takes precedence over the factory.
    void setVoiceEmbedder(providers::IVoiceEmbedder* embedder);
    // A synchronous utterance-embedder factory: every embedding run wraps it into a FactoryVoiceEmbedder
    // (one audio decode per run).
    void setEmbedderFactory(providers::UtteranceEmbedderFactory factory);
    // Display name of a voice model id for the feedback (RecheckResult::models). Default: the id itself.
    using ModelNameFn = std::function<QString(const QString& modelId)>;
    void setModelNameResolver(ModelNameFn fn);

    bool embeddingsSupported() const;   // a source exists (whether the model/audio exists is known at run time)
    bool embeddingsComplete() const;    // available for every sufficiently long line (or attempted)
    bool isEmbeddingRunning() const;
    QString embeddingError() const;     // error of the last run (empty = none)

    // ---- recheck based on confirmed lines ----------------------
    // Measures the other lines against the "reliable" voice centroids built from the confirmed /
    // corrected lines (SpeakerAnalysis: computeUncertainRechecked). Lines found doubtful stay
    // uncertain (persisted in the overlay) until a correction or "Fine as is" comes; a newer
    // recheck replaces the set. One undo step (if anything changed).
    // For named speakers the person's stored voiceprints join the reference (SpeakerAnalysis:
    // SpeakerPrior): those made in this meeting as local evidence, earlier ones with a smaller,
    // capped weight (confirmed lines always dominate).
    struct RecheckResult {
        int flagged = 0;                    // this many lines flagged doubtful
        int speakersWithConfirmedCore = 0;  // for this many speakers a centroid was built from their locked lines
        int confirmedLines = 0;             // this many locked lines formed those cores
        bool ran = false;                   // whether it ran (whether canRecheck held)
        QVector<SpeakerReference> references;   // per-speaker reference (those with a core / voiceprints)
        // For feedback: what the recheck relied on.
        int localPrints = 0;                // stored samples in the reference: from this meeting
        int priorPrints = 0;                //   ... and earlier ones (from other meetings)
        QStringList models;                 // display names of the voice models in use (alphabetical by id)
        QVector<SimilarVoices> similar;     // speaker pairs with very similar voices (descending similarity)
        int storedPrints() const { return localPrints + priorPrints; }
        QString referenceSummary() const { return RecheckService::referenceSummary(references); }
        // "Based on: 23 confirmed lines, 5 stored samples (2 from this meeting), model: CAM++."
        QString basisSummary() const { return RecheckService::recheckBasisSummary(*this); }
        // "Warning: Gabor White and Arpad Varga sound very similar (0.92) ..."; empty if there is no such pair.
        QString similarityNote() const { return RecheckService::similarVoicesNote(similar); }
    };
    // "Reference: Alice 3 lines + 1 voiceprint from here + 2 earlier voiceprints, Bob 5 lines": only
    // speakers with a core / voiceprints; empty if no reference used a voiceprint.
    static QString referenceSummary(const QVector<SpeakerReference>& refs);
    static QString recheckBasisSummary(const RecheckResult& r);
    static QString similarVoicesNote(const QVector<SimilarVoices>& similar);
    RecheckResult recheckFromConfirmed();
    // Whether it can run: an embedding exists, the voice analysis is not running, and at least one
    // speaker has at least kMinSpeakerLines confirmed / corrected (embedded) lines.
    bool canRecheck() const;
    // If it cannot run: why (a sentence for the user); otherwise empty.
    QString recheckBlocker() const;

    // ---- pairwise review ("Review between A and B") ---------------------
    // Only A's and B's lines, based only on their two voices (SpeakerAnalysis: computePairRecheck):
    // the reference is built from the confirmed / corrected lines (+ the stored voiceprints); with few
    // lines and no voiceprint, from all the speaker's clean lines; fallbackA/B signals this. Lines found
    // doubtful become uncertain, the suggestion (recheckHint) is the other speaker. It replaces the earlier
    // recheck flags of lines on A and B; other speakers' are untouched. One undo step (if anything changed).
    struct PairRecheckResult {
        int    flagged = 0;
        int    refLinesA = 0;
        int    refLinesB = 0;
        bool   fallbackA = false;
        bool   fallbackB = false;
        double centroidSimilarity = qQNaN();    // cosine of the two references' voices
        SpeakerReference refA;                  // composition of the two references
        SpeakerReference refB;
        bool   ran = false;
        QString blocker;                        // if it did not run: why (a user-facing sentence)
        QString referenceSummary() const { return RecheckService::referenceSummary({refA, refB}); }
    };
    PairRecheckResult recheckPair(const QString& speakerKeyA, const QString& speakerKeyB);
    // If it cannot run now: why; otherwise empty.
    QString pairRecheckBlocker(const QString& speakerKeyA, const QString& speakerKeyB) const;

public slots:
    // Computes embeddings in the background (for those missing from the cache). embeddingProgress /
    // embeddingFinished signals; at the end the uncertainty flags refresh.
    void startEmbedding();
    void cancelEmbedding();

signals:
    void embeddingRunningChanged(bool running);
    void embeddingProgress(int done, int total);
    void embeddingFinished(bool complete);
    // A recheck ran (the view may switch to the "Uncertain" filter).
    void recheckFinished(int flagged, int speakersWithConfirmedCore, int confirmedLines);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace tanara::editing
