#pragma once
//
// The pure math of the clustering step of the local diarization (D4), without ONNX Runtime:
//
//   1. Complete-linkage agglomerative clustering of L2-normalized embeddings, cosine DISTANCE (1 - cos)
//      (the sherpa-onnx `FastClustering` / hclust "complete" method). Self-written NN-chain algorithm:
//      O(n^2) time and O(n^2) memory (a condensed float matrix: n = 7000 -> ~98 MB). The dendrogram is
//      cut either at a distance threshold (merges with height < threshold) or into exactly k clusters.
//   2. Small clusters (fewer members than minClusterSize) are reassigned to the nearest large cluster
//      by centroid cosine (the pyannote 3.1 `min_cluster_size` idea): a few noisy short-span embeddings
//      must not become speakers of their own. Skipped when every cluster is small.
//   3. The embedding cap: above `cap` items a deterministic, time-uniform subsample is clustered (every
//      group - the stitched local speaker - keeps at least one item); the rest inherit the cluster of their
//      group (see LocalDiarizer).
//
// Labels are dense (0, 1, ...) in the order of the first item of each cluster.
// Everything is deterministic and unit-tested with synthetic embeddings (test_diarization_clustering).
#include <QVector>

namespace tanara::processing::clustering {

// One merge of the dendrogram: the clusters represented by items a and b (a < b; the merged cluster is
// represented by a from then on) joined at `height` (complete-linkage cosine distance).
struct Merge {
    int a = 0;
    int b = 0;
    float height = 0.0f;
};

// Cosine distance of two L2-normalized vectors (1 - dot); vectors of different size: 2 (= maximal).
float cosineDistance(const QVector<float>& a, const QVector<float>& b);

// The complete-linkage dendrogram of `vectors` (L2-normalized, equal size): n - 1 merges, sorted by
// height (ties: in the order the NN-chain found them). n < 2: no merge.
QVector<Merge> completeLinkage(const QVector<QVector<float>>& vectors);

// Cuts the dendrogram of n items: numClusters > 0 -> exactly min(numClusters, n) clusters (the
// n - k lowest merges); otherwise every merge with height < threshold. Dense labels.
QVector<int> cutTree(int n, const QVector<Merge>& merges, double threshold, int numClusters = -1);

// completeLinkage + cutTree.
QVector<int> clusterCompleteLinkage(const QVector<QVector<float>>& vectors, double threshold, int numClusters = -1);

// Step 2: the members of clusters smaller than minClusterSize go to the nearest (centroid cosine) large
// cluster. Unchanged if minClusterSize <= 1 or no cluster is large enough. Dense labels again.
QVector<int> reassignSmallClusters(const QVector<int>& labels, const QVector<QVector<float>>& vectors,
                                   int minClusterSize);

// The L2-normalized mean of the given members' vectors (empty if none / all empty).
QVector<float> centroid(const QVector<QVector<float>>& vectors, const QVector<int>& members);

// Step 3: which of n items (in time order) are clustered when there are more than `cap` (cap <= 0: all).
// Every round(n / cap)-th item, plus the first item of every group (groups[i] >= 0) that would have none.
// Sorted indices.
QVector<int> subsample(int n, int cap, const QVector<int>& groups);

// Number of distinct labels (labels are dense: max + 1; -1 entries ignored).
int clusterCount(const QVector<int>& labels);

} // namespace tanara::processing::clustering
