#pragma once
//
// media::MediaTool: the port to the external media tools (ffmpeg / ffprobe). Every audio decode,
// probe, encode and transcode / mixdown of the app goes through it; the adapter (FfmpegMediaTool)
// runs the tools as child processes, the tests use a fake (tests/unit/fake_media_tool.h).
//
// Threading: the synchronous operations (probe, decodePcm, startEncoder) may be called from any
// thread, concurrently: they create their process on the calling thread. The asynchronous jobs
// (startProbe, startDecode, runFilterGraph) live on the thread of their parent / creator and report
// through Qt signals. A MediaTool instance is immutable after construction; it is shared with
// std::shared_ptr (MediaToolPtr), so background threads can keep it alive.
#include "tanara/media/MediaTypes.h"

#include <QObject>

#include <memory>

namespace tanara::media {

// An asynchronous run (probe / decode / filter graph). Created NOT started: connect the signals, then
// start(). Exactly one finished() follows start() (also when the tool cannot be started). The owner
// deletes it (deleteLater) after finished().
class MediaJob : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~MediaJob() override = default;

    virtual void start() = 0;
    // Stops the run (kill); finished() follows with MediaResult::cancelled.
    virtual void cancel() = 0;
    // Synchronous teardown (owner destructor): no more signals, the process is killed and reaped
    // (waiting at most waitMs).
    virtual void abort(int waitMs) = 0;
    virtual bool isRunning() const = 0;

signals:
    // Raw stdout chunk (startDecode: the PCM; not frame-aligned).
    void output(const QByteArray& data);
    // runFilterGraph: the length already written (ffmpeg `-progress`: out_time_us / out_time_ms), the
    // latest value of each stdout chunk.
    void progress(qint64 outTimeMs);
    void finished(const tanara::media::MediaResult& result);
};

// A running encoder fed through its stdin (the recorder). Not thread-safe: use it on the thread that
// started it.
class EncoderHandle {
public:
    virtual ~EncoderHandle() = default;
    virtual bool isRunning() const = 0;
    // Writes raw PCM (in the EncodeSpec input format) to the encoder; ignored once it stopped.
    virtual void write(const char* data, qint64 bytes) = 0;
    // Closes the input and waits for the encoder (killed after timeoutMs). true = clean exit (code 0).
    virtual bool finish(int timeoutMs = 30000) = 0;
    // Kills the encoder without waiting for a clean end.
    virtual void kill() = 0;
};

class MediaTool {
public:
    virtual ~MediaTool() = default;

    // ---- diagnostics --------------------------------------------------------------------------
    // Whether the tools were found (the resolved paths exist).
    virtual bool available() const = 0;
    // The first line of `ffmpeg -version` (empty if not runnable). Runs the tool once, then cached.
    virtual QString version() const = 0;
    // The resolved programs (a bare name if not found: then starting them fails the usual way).
    virtual QString ffmpegPath() const = 0;
    virtual QString ffprobePath() const = 0;

    // ---- probe -------------------------------------------------------------------------------
    // Synchronous probe; the result's info is set on a clean exit (an unreadable file: info empty
    // or without streams). Exceeding timeoutMs kills the probe (timedOut).
    virtual MediaResult probe(const QString& path, int timeoutMs = 30000) const = 0;
    virtual MediaJob* startProbe(const QString& path, QObject* parent) const = 0;

    // ---- decode ------------------------------------------------------------------------------
    // Synchronous, streaming decode: the PCM goes to the sink in whole frames as it arrives (it is not
    // kept in memory). The token cancels between chunks.
    virtual MediaResult decodePcm(const DecodeSpec& spec, const RawPcmSink& sink,
                                  const CancelToken& cancel = {}) const = 0;
    // Asynchronous decode: the PCM arrives through MediaJob::output.
    virtual MediaJob* startDecode(const DecodeSpec& spec, QObject* parent) const = 0;

    // ---- encode / transcode / mixdown --------------------------------------------------------
    // Starts an encoder (waits for it to start); nullptr if it cannot be started.
    virtual std::unique_ptr<EncoderHandle> startEncoder(const EncodeSpec& spec) const = 0;
    // An asynchronous ffmpeg run with progress (mixdown, import transcode).
    virtual MediaJob* runFilterGraph(const FilterGraphSpec& spec, QObject* parent) const = 0;
    // Transcoding into Opus track files (the importer), with progress like runFilterGraph.
    virtual MediaJob* startTranscode(const TranscodeSpec& spec, QObject* parent) const = 0;

    // ---- helpers on top of decodePcm (non-virtual) -------------------------------------------
    // Mono s16 decoder at the given rate, for streaming analyses (e.g. track activity). The decoder
    // refers to this tool: keep the tool alive while it is used.
    PcmDecoder monoS16Decoder(int sampleRate) const;
    // A file segment as mono float PCM ([-1,1]); empty on failure. Lenient like the voice code needs
    // it: any output counts (the exit code is not checked), the time limit is 120 s.
    QVector<float> decodeMonoF32(const QString& path, qint64 startMs, qint64 endMs,
                                 int sampleRate = 16000) const;
};

using MediaToolPtr = std::shared_ptr<const MediaTool>;

} // namespace tanara::media
