#pragma once
//
// WaveformService: per-track waveform peaks for drawing.
//
// Asynchronous: ffmpeg decodes the audio file (QProcess, like the rest of the code), the PCM is
// processed in chunks in the event loop, so the UI thread NEVER blocks. The result is cached in the
// meeting's folder (<audio file>.peaks.json), so the next open is instant; the cache is tied to the
// source file's size + modification time (recomputed if the file changes). Decoding also yields the
// track's EXACT length (durationMs).
#include <QObject>
#include <QString>
#include <QVector>
#include <QMetaType>

namespace tanara::library {

struct TrackPeaks {
    QString        trackId;
    QVector<float> peaks;          // per bucket the maximum of |sample|, 0..1
    // Per bucket the effective (RMS) level, 0..1: the same number of elements as peaks. For a long
    // recording the per-bucket peak is near maximal almost everywhere (an evenly high picture); this gives
    // the true speech / silence proportions for drawing.
    QVector<float> rms;
    qint64         durationMs = 0; // length of the decoded audio
    bool isValid() const { return !peaks.isEmpty(); }
};

class WaveformService : public QObject {
    Q_OBJECT
public:
    static constexpr int kBuckets = 400;   // the cached resolution ("a few hundred buckets")

    explicit WaveformService(QObject* parent = nullptr);
    ~WaveformService() override;

    // ---- cache (synchronous, small file) -------------------------------------------
    static QString cachePath(const QString& audioPath);          // <audio file>.peaks.json
    // Loads a valid cache; an invalid TrackPeaks if missing / stale / corrupt.
    static TrackPeaks loadCached(const QString& audioPath);
    static void removeCache(const QString& audioPath);
    // Resampling to the bucket count requested for drawing (max-hold; repeats when zooming in).
    static QVector<float> resample(const QVector<float>& peaks, int buckets);

    // ---- asynchronous computation -----------------------------------------------------------
    // Request peaks for an audio file. With a valid cache peaksReady comes in the next event-loop turn;
    // otherwise it is queued (at most 2 ffmpeg run at once). A second request for the same file already
    // running/waiting does not start another.
    void request(const QString& meetingId, const QString& trackId, const QString& audioPath);
    // Drops the meeting's waiting requests and stops the running ones (e.g. when another meeting is selected).
    void cancel(const QString& meetingId);
    bool isPending(const QString& audioPath) const;

    void setFfmpegPath(const QString& path) { m_ffmpeg = path; }

signals:
    void peaksReady(QString meetingId, QString trackId, tanara::library::TrackPeaks peaks);
    void peaksFailed(QString meetingId, QString trackId, QString error);

private:
    struct Job;
    void startNext();
    void finishJob(Job* job, bool ok, const QString& error);

    QString m_ffmpeg{QStringLiteral("ffmpeg")};
    QVector<Job*> m_queue;     // waiting
    QVector<Job*> m_running;   // running (max 2)
};

} // namespace tanara::library
Q_DECLARE_METATYPE(tanara::library::TrackPeaks)
