#pragma once
//
// EmbeddingIndex — a megbeszélések szöveg-beágyazásai (a „rokon téma” hasonlósághoz).
//
// Meetingenként egy fájl a meeting mappájában: text.embeddings.bin
//   fejléc: "TNEMB1" (6 bájt), majd QDataStream (Qt 6.0, little-endian, float32):
//           modellnév (QString), dim (qint32), darabszám (qint32)
//   darabonként: startMs (qint64), endMs (qint64), dim × float32
// Darabolás: megszólalás-határra igazított, ~1500 karakteres ablakok (chunkTranscript).
// A meeting vektora a darabok normalizált átlaga; a hasonlóság ezek koszinusza.
//
// Egy index akkor érvényes, ha a modellje az aktuális modell, és nem régebbi az átiratnál
// (transcript.segments.json). A vektorok lustán, meetingenként töltődnek a memóriába.
//
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
    QString text;                 // csak a beágyazás kérésekor (a fájlba nem kerül)
    QVector<float> vector;
};

class EmbeddingIndex : public QObject {
    Q_OBJECT
public:
    static constexpr int kChunkChars = 1500;
    static const QString kFileName;   // "text.embeddings.bin"

    explicit EmbeddingIndex(MeetingStore* store, QObject* parent = nullptr);
    ~EmbeddingIndex() override;

    // A „Miért?” indoklás közös kifejezéseihez (nélküle az indoklás üres).
    void setProfiles(MeetingProfiles* profiles);

    // Az aktuális modell (üres → nincs beágyazás: has() mindig hamis).
    void setModel(const QString& model);
    QString model() const;

    // A mappát az indexből (MeetingStore) keresik ki: csak a tulajdonos (fő) szálról.
    bool has(const QString& meetingId) const;
    QVector<SimilarHit> similar(const QString& meetingId, int limit = 8) const;
    // Szálbiztos változatok ismert mappákkal (id → mappa, pl. MeetingProfiles::folders()):
    // háttérszálról is hívhatók; a memóriabeli gyorsítótárat mutex védi.
    bool has(const QString& meetingId, const QString& folder) const;
    QVector<SimilarHit> similar(const QString& meetingId, const QHash<QString, QString>& folders,
                                int limit = 8) const;

    // Egy meeting indexének kiírása (a darabok vektorokkal) → indexChanged.
    bool store(const QString& meetingId, const QString& model, const QVector<EmbeddingChunk>& chunks);
    // Minden index eldobása (modellváltás): a fájlok törlődnek, a memória ürül → indexReset.
    void invalidateAll();
    // Egy meeting indexének eldobása (pl. új átirat).
    void invalidate(const QString& meetingId);

    // ---- tiszta segédek (tesztelhetők) ----
    static QVector<EmbeddingChunk> chunkTranscript(const QVector<TranscriptLine>& lines,
                                                   int maxChars = kChunkChars);
    static bool writeFile(const QString& path, const QString& model, const QVector<EmbeddingChunk>& chunks);
    static bool readFile(const QString& path, QString* model, QVector<EmbeddingChunk>* chunks);
    // A darabok normalizált átlaga (üres, ha nincs darab).
    static QVector<float> meanVector(const QVector<EmbeddingChunk>& chunks);

signals:
    void indexChanged(QString meetingId);
    void indexReset();

private:
    struct Entry { qint64 fileMtime = -1; QString model; QVector<float> mean; };
    QString folderOf(const QString& meetingId) const;
    // A meeting átlag-vektora, ha az indexe érvényes és az aktuális modellé; különben üres.
    QVector<float> meanFor(const QString& meetingId, const QString& folder) const;

    MeetingStore* m_store = nullptr;
    MeetingProfiles* m_profiles = nullptr;
    mutable QMutex m_mutex;                     // m_model, m_cache, m_folders
    QString m_model;
    mutable QHash<QString, Entry> m_cache;
    mutable QHash<QString, QString> m_folders;
};

} // namespace tanara
