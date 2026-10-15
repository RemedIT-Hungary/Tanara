#pragma once
//
// Domain: transcript value types (tokens, per-track transcripts, utterances, the merged transcript).
// The MergedTranscript methods are implemented by the TranscriptMerger module.
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

// ---- transcript ------------------------------------------------------------
struct TranscriptToken {
    QString text;
    QString speaker;        // Soniox "speaker" (usually a fixed label per track)
    qint64 startMs = 0;
    qint64 endMs = 0;
    double confidence = 0.0;
    QString trackId;        // which track it came from (filled during merge)
};

struct TrackTranscript {
    QString trackId;
    QString speakerLabel;
    QString language;
    QVector<TranscriptToken> tokens;
    // Token times mark the moment the token was EMITTED (Soniox), not a real word span:
    // utterances need a later pre/post-roll pass (MergedTranscript::segments). Only SonioxProvider sets this.
    bool emissionTiming = false;
};

// A contiguous speech block (one speaker, up to a pause). The machine-friendly unit
// for player sync and the clickable transcript.
struct Utterance {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString speaker;
    QString text;
};

struct MergedTranscript {
    QString language;
    QVector<TranscriptToken> tokens;     // sorted by startMs, tracks merged
    // Emission-time timing (see TrackTranscript): after grouping, segments() pads the finished
    // lines (sonioxtiming::padUtterances). tokens.json: "timing": "emission".
    bool emissionTiming = false;
    // Where the speaker labels came from: "" = the STT provider (its diarization, or none);
    // "local-diarization" = the local diarizer's turns merged in (processing::diarizationmerge). tokens.json:
    // "speakers": {"source": ..., "models": [...]}.
    QString speakerSource;
    QStringList speakerModels;           // that source's models (segmentation id, then the embedding ids)
    QVector<Utterance> segments() const; // per speaker, pause-based blocks in time order
    QString renderMarkdown() const;      // impl: the TranscriptMerger module
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::TranscriptToken)
Q_DECLARE_METATYPE(tanara::TrackTranscript)
Q_DECLARE_METATYPE(tanara::Utterance)
Q_DECLARE_METATYPE(tanara::MergedTranscript)
