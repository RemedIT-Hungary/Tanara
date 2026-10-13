#pragma once
//
// Domain: per-track audio activity of a meeting (an energy time series per logical track) and the
// voice clusters of the pre-transcript analysis. Computation: processing/analysis/TrackActivity.h,
// processing/analysis/ParticipantAnalysis.h; the activity cache file I/O (MeetingActivity::load / save) is
// implemented next to the computation (processing/analysis/TrackActivity.cpp).
#include "tanara/domain/Audio.h"
#include "tanara/domain/Voice.h"

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

// ---- track activity -------------------------------------------------------------
struct TrackActivity {
    QString trackId;                // id of the logical track's first span
    TrackKind kind = TrackKind::Other;
    int frameMs = 50;
    qint64 originMs = 0;            // start of the first frame in MEETING time
    QVector<float> dbAboveFloor;    // per frame >= 0; NaN = no data (a gap)
    float floorDb = 0.0f;           // estimated noise floor (10th percentile of the frame dBs), dBFS

    // How many frames carry data (not NaN).
    int coveredFrames() const;
    qint64 coveredMs() const { return qint64(coveredFrames()) * frameMs; }
    qint64 endMs() const { return originMs + qint64(dbAboveFloor.size()) * frameMs; }
};

struct MeetingActivity {
    QString fingerprint;            // activityFingerprint(): if it changes, we recompute
    QVector<TrackActivity> tracks;

    bool isEmpty() const { return tracks.isEmpty(); }
    static QString filePath(const QString& meetingFolder);   // tracks.activity.bin
    // The display cache (speech strip): of EVERY track, including those left out of the mixdown (for the
    // keep / drop decision). See AppController::trackActivity.
    static QString viewFilePath(const QString& meetingFolder);   // tracks.activity.view.bin
    // An empty result (filled with the fingerprint, with no tracks) if there is no file / the fingerprint
    // differs / the format is unknown. path: the cache file (empty = filePath(meetingFolder)).
    static MeetingActivity load(const QString& meetingFolder, const QString& fingerprint,
                                const QString& path = {});
    bool save(const QString& meetingFolder, const QString& path = {}) const;
};

// ---- voice clusters (pre-transcript analysis) -----------------------------------
// A voice cluster: (presumably) one speaker on one track.
struct VoiceCluster {
    QString trackId;
    QString side;
    QVector<QPair<qint64, qint64>> windows;   // the member windows [start, end) in meeting time
    EmbeddingSet centroid;                    // per-model L2-normalized centroid (not persisted)
    QString sampleRef;                        // "<file>#start-end": the medoid window
    // Result of the matching (filled by buildParticipants):
    QString participantId;
    QString matchName;                        // the accepted voiceprint match (above threshold); empty = unknown
    double  matchScore = -1.0;                // score of the best match (also below the threshold)
    qint64 speechMs() const;
};

struct ClusterSet {
    QStringList modelIds;           // models taking part in the fusion
    QVector<VoiceCluster> clusters;
    int windows = 0;                // how many (embedded) windows it consisted of
    bool cancelled = false;
    QString error;                  // did not run: why (e.g. no model)
};

} // namespace tanara
