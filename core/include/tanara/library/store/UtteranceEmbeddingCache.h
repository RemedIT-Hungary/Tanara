#pragma once
//
// UtteranceEmbeddingCache: the per-model line embeddings of a meeting, persisted in the meeting folder
// (transcript.embeddings.bin, v2). Model -> (utterance id -> embedding). Tied to the transcript's
// fingerprint: another transcript -> an empty cache. A v1 (single-model) file is dropped at load.
// An empty vector = tried, failed (do not retry).
// (Moved here from processing/analysis/UtteranceEmbeddings.h: it is meeting-folder persistence; processing
// keeps an alias.)
#include "tanara/domain/Voice.h"

#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::library {

struct UtteranceEmbeddingCache {
    QString fingerprint;
    QMap<QString, QHash<QString, QVector<float>>> models;   // modelId -> utteranceId -> vector

    // All vectors of one model (an empty hash if none).
    QHash<QString, QVector<float>> vectors(const QString& modelId) const { return models.value(modelId); }
    void set(const QString& modelId, const QString& utteranceId, const QVector<float>& vec)
    {
        models[modelId].insert(utteranceId, vec);
    }
    // Whether this line has already been tried (even unsuccessfully) with this model.
    bool has(const QString& modelId, const QString& utteranceId) const;
    // The line's vectors with the requested models (only the non-empty ones).
    EmbeddingSet setFor(const QString& utteranceId, const QStringList& modelIds) const;
    // Of the listed lines, those not yet tried with this model (order kept).
    QStringList missingFor(const QString& modelId, const QStringList& utteranceIds) const;
    // There is no entry at all (with any model).
    bool isEmpty() const;

    static QString filePath(const QString& meetingFolder);   // transcript.embeddings.bin
    static UtteranceEmbeddingCache load(const QString& meetingFolder, const QString& fingerprint);
    bool save(const QString& meetingFolder) const;
    static void remove(const QString& meetingFolder);
};

} // namespace tanara::library
