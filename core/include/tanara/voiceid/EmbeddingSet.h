#pragma once
//
// The fusion of an EmbeddingSet (the per-model embeddings of an audio slice or voiceprint). The type
// itself is in domain/Voice.h.
#include "tanara/domain/Voice.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

namespace fusion {

// One common vector: the L2-normalized vector of every requested model concatenated (in the
// alphabetical order of modelIds), each multiplied by 1/sqrt(k). So the cosine of two fused vectors =
// the MEAN of the per-model cosines. Empty if any requested model lacks a (non-empty) vector, or modelIds is empty.
QVector<float> fuse(const EmbeddingSet& set, const QStringList& modelIds);

// The mean of the per-model cosines over the COMMON requested models (non-empty on both sides).
// No common model -> -1. used: how many models took part (optional).
double averageCosine(const EmbeddingSet& a, const EmbeddingSet& b, const QStringList& modelIds,
                     int* used = nullptr);

} // namespace fusion
} // namespace tanara
