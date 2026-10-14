#pragma once
//
// LocalDiarizer: the local IDiarizer (Origin::Local). D3 STAGE: segmentation only. Every track with
// input is decoded (16 kHz mono float) and segmented by PyannoteSegmenter; the turns carry the
// STITCHED local speaker as speakerIndex (renumbered 0, 1, ... by first appearance per track), the
// speakers have an EMPTY centroid. D4 adds the embeddings + clustering on top of the same
// SegmentationResult, so the speaker indices become real clusters.
//
// A track bound to a person by hand (DiarizeTrack::knownPerson) is one speaker (index 0, person set):
// all its speech, merged, is that speaker's.
//
// Options of the request used here: minDurationOn / minDurationOff (seconds) and segmentationModelId;
// numClusters / threshold / embeddingModelIds / knownSpeakers are D4's.
//
// The work runs on a worker thread (one model / ORT session per job); progress(done, total) counts
// windows over all tracks (the tracks are decoded first); cancel() (or destroying the job) stops the
// worker after the window batch in progress. Errors: no input -> Input "no_input"; no usable model ->
// Unsupported "no_segmentation_model"; a decode failure -> Input "decode_failed"; an inference
// failure -> Service "segmentation_failed".
#include "tanara/media/MediaTool.h"
#include "tanara/processing/diarization/PyannoteSegmenter.h"
#include "tanara/providers/voice/IDiarizer.h"

#include <QObject>

#include <functional>
#include <memory>

namespace tanara::processing {

class LocalDiarizer : public QObject, public providers::IDiarizer {
    Q_OBJECT
public:
    static inline const QString Id = QStringLiteral("local");

    // Opens a segmenter for a model id ("" = the default model) on the worker thread; nullptr + *error
    // when it cannot. Test seam: a loader that wraps a fake SegmentationModel.
    using SegmenterLoader =
        std::function<std::unique_ptr<PyannoteSegmenter>(const QString& modelId, const SegmenterOptions& opts,
                                                         QString* error)>;

    LocalDiarizer(SegmenterLoader loader, media::MediaToolPtr media, QObject* parent = nullptr);

    // The production loader: VoiceModelRegistry spec -> PyannoteSegmenter::open (integrity check) with
    // `ort` options.
    static SegmenterLoader catalogLoader(const QString& modelsDir, const QString& appDir,
                                         OnnxSegmentationOptions ort = {});
    // Whether a segmentation model is usable (the default one, or else any catalog segmentation model).
    static bool hasUsableModel(const QString& modelsDir, const QString& appDir);

    // The segmenter options the request asks for (defaults where it gives none).
    static SegmenterOptions optionsFor(const providers::DiarizeRequest& req, SegmenterOptions base = {});
    // One track's segmentation -> the port's turns and speakers (D3 stage; exposed for the tests).
    static void appendTrackResult(const providers::DiarizeTrack& track, const SegmentationResult& seg,
                                  const SegmenterOptions& opts, providers::DiarizeResult* out);

    QString name() const override { return Id; }
    providers::DiarizeJob* diarize(const providers::DiarizeRequest& req) override;

    // Base options (window shift, stitching, batch); the request's own options override per job.
    void setBaseOptions(const SegmenterOptions& opts) { m_base = opts; }

private:
    SegmenterLoader m_loader;
    media::MediaToolPtr m_media;
    SegmenterOptions m_base;
};

} // namespace tanara::processing
