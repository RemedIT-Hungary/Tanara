#pragma once
//
// EmbeddingIndex: the meetings' text embeddings (for the "related topic" similarity).
//
// One file per meeting in the meeting folder: text.embeddings.bin
//   header: "TNEMB1" (6 bytes), then QDataStream (Qt 6.0, little-endian, float32):
//           model name (QString), dim (qint32), chunk count (qint32)
//   per chunk: startMs (qint64), endMs (qint64), dim x float32
// Chunking: windows of ~1500 characters aligned to utterance boundaries (chunkTranscript).
// The meeting's vector is the normalized mean of the chunks; similarity is their cosine.
//
// An index is valid if its model is the current model and it is not older than the transcript
// (transcript.segments.json). The vectors load into memory lazily, per meeting.
#include "tanara/tags/TagTypes.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QVector>

namespace tanara {

class MeetingStore;
class MeetingProfiles;
struct TranscriptLine;

struct EmbeddingChunk {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;                 // only when embedding is requested (does not go into the file)
    QVector<float> vector;
};

class EmbeddingIndex : public QObject {
    Q_OBJECT
public:
    static constexpr int kChunkChars = 1500;
    static const QString kFileName;   // "text.embeddings.bin"

    explicit EmbeddingIndex(MeetingStore* store, QObject* parent = nullptr);
    ~EmbeddingIndex() override;

    // For the shared phrases of the "Why?" explanation (without it the explanation is empty).
    void setProfiles(MeetingProfiles* profiles);

    // The current model (empty = no embedding: has() is always false).
    void setModel(const QString& model);
    QString model() const;

    // The folder is looked up from the index (MeetingStore): only from the owner (main) thread.
    bool has(const QString& meetingId) const;
    QVector<SimilarHit> similar(const QString& meetingId, int limit = 8) const;
    // Thread-safe variants with known folders (id -> folder, e.g. MeetingProfiles::folders()):
    // callable from a background thread too; the in-memory cache is protected by a mutex.
    bool has(const QString& meetingId, const QString& folder) const;
    QVector<SimilarHit> similar(const QString& meetingId, const QHash<QString, QString>& folders,
                                int limit = 8) const;

    // Writes a meeting's index (the chunks with vectors) -> indexChanged.
    bool store(const QString& meetingId, const QString& model, const QVector<EmbeddingChunk>& chunks);
    // Drops every index (model change): files are deleted, memory is freed -> indexReset.
    void invalidateAll();
    // Drops one meeting's index (e.g. a new transcript).
    void invalidate(const QString& meetingId);

    // ---- pure helpers (testable) ----
    static QVector<EmbeddingChunk> chunkTranscript(const QVector<TranscriptLine>& lines,
                                                   int maxChars = kChunkChars);
    static bool writeFile(const QString& path, const QString& model, const QVector<EmbeddingChunk>& chunks);
    static bool readFile(const QString& path, QString* model, QVector<EmbeddingChunk>* chunks);
    // The normalized mean of the chunks (empty if there are no chunks).
    static QVector<float> meanVector(const QVector<EmbeddingChunk>& chunks);

signals:
    void indexChanged(QString meetingId);
    void indexReset();

private:
    struct Entry { qint64 fileMtime = -1; QString model; QVector<float> mean; };
    QString folderOf(const QString& meetingId) const;
    // The meeting's mean vector if its index is valid and of the current model; otherwise empty.
    QVector<float> meanFor(const QString& meetingId, const QString& folder) const;

    MeetingStore* m_store = nullptr;
    MeetingProfiles* m_profiles = nullptr;
    mutable QMutex m_mutex;                     // m_model, m_cache, m_folders
    QString m_model;
    mutable QHash<QString, Entry> m_cache;
    mutable QHash<QString, QString> m_folders;
};

} // namespace tanara
