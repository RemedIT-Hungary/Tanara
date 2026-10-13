#pragma once
//
// JobStats: measured speed of earlier runs, for an HONEST estimate of the remaining time.
//
// The providers (Soniox async, OpenAI-compatible /audio/transcriptions) give NO percentage during
// transcription, only a state. So an estimate comes solely from our own earlier successful runs:
// per key (e.g. "transcribe/soniox") the moving average of the "processing time / audio length"
// ratio. Without a sample there is no estimate (-1); we do not invent a number.
//
// Storage: <metadata folder>/job-stats.json (small, deletable any time).
#include <QString>
#include <QHash>

namespace tanara::library {

class JobStats {
public:
    // Empty filePath = in memory only (tests).
    explicit JobStats(const QString& filePath = QString());

    // One successful run: processed audioSec seconds of audio in wallSec seconds.
    void addSample(const QString& key, double wallSec, double audioSec);

    // Estimated total run time (s) for audioSec of audio; -1 if there is no sample for the key.
    int estimateSec(const QString& key, double audioSec) const;

    int sampleCount(const QString& key) const;

private:
    struct Entry { double ratio = 0.0; int samples = 0; };
    void load();
    void save() const;

    QString m_filePath;
    QHash<QString, Entry> m_entries;
};

} // namespace tanara::library