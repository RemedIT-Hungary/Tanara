#pragma once
//
// STT port (CapabilityKind::Transcription). Async, signal-based (fits the Qt event loop), and
// prepared for a later live/streaming mode (startLive()).
#include "tanara/domain/Transcript.h"
#include "tanara/domain/Job.h"
#include "tanara/foundation/error/Error.h"
#include "tanara/providers/core/ProviderDescriptor.h"
#include <QObject>

namespace tanara::providers {

// A running transcription job. The caller connects to the finished/failed signals.
class SttJob : public QObject {
    Q_OBJECT
public:
    explicit SttJob(QObject* parent = nullptr) : QObject(parent) {}
    ~SttJob() override = default;
    virtual void cancel() = 0;
    // The classified error (filled before the failed signal).
    const Error& error() const { return m_error; }

signals:
    void stateChanged(tanara::SttJobState state);
    void progress(int percent, const QString& message);
    // REAL progress of the audio upload in bytes (from the network layer). During transcription the
    // providers give no percentage; only the stateChanged states exist.
    void uploadProgress(qint64 bytesSent, qint64 bytesTotal);
    void finished(const tanara::TrackTranscript& result);
    void failed(const QString& error);

protected:
    Error m_error;
};

struct SttRequest {
    QString audioFilePath;          // one track's .ogg file
    QString trackId;
    QString speakerLabel;           // the fixed speaker of the track
    QStringList languageHints{QStringLiteral("hu")};
    bool diarization = false;       // usually false per track (the track = the speaker)
    // Context envelope: the STT decides better in doubtful parts. Soniox stt-async-v5 expects a
    // "context" object (general/text/terms); we map it to the three fields:
    //   contextGeneral -> "general" (key-value metadata: e.g. the meeting's title)
    //   context        -> "text"    (free background text: the user's description)
    //   contextTerms   -> "terms"   (important words: participant names, jargon)
    // Providers that do not support context ignore these fields.
    QMap<QString, QString> contextGeneral;
    QString context;
    QStringList contextTerms;
    QVariantMap providerOptions;
};

class ISttProvider {
public:
    virtual ~ISttProvider() = default;
    virtual QString name() const = 0;
    virtual bool supportsLive() const = 0;

    // The returned job belongs to the caller (parent it / deleteLater after finish).
    virtual SttJob* transcribe(const SttRequest& req) = 0;

    // Future live mode: none by default; a provider opts in later.
    virtual SttJob* startLive(const SttRequest& /*req*/) { return nullptr; }
};

template <> struct ProviderTraits<ISttProvider> {
    static constexpr CapabilityKind kind = CapabilityKind::Transcription;
};

} // namespace tanara::providers