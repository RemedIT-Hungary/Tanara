#include "tanara/processing/diarization/PyannoteSegmenter.h"

#include "tanara/foundation/log/Logging.h"
#include "tanara/processing/voiceid/VoiceModelDownloader.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cstring>

#ifdef TANARA_HAVE_VOICEID
#include "../voiceid/OnnxRuntimeInclude.h"

#include <array>
#include <string>
#include <vector>
#endif

namespace tanara::processing {

using namespace segmentation;

// ---- the ONNX model ---------------------------------------------------------------------------------

#ifdef TANARA_HAVE_VOICEID

namespace {

class OnnxSegmentationModel final : public SegmentationModel {
public:
    bool load(const QString& path, const OnnxSegmentationOptions& opts, QString* error)
    {
        try {
            Ort::SessionOptions so;
            so.SetIntraOpNumThreads(std::max(1, opts.intraOpThreads));
            so.SetInterOpNumThreads(1);
            so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            const auto p = ortPath(path);
            m_session = std::make_unique<Ort::Session>(m_env, p.c_str(), so);
            Ort::AllocatorWithDefaultOptions alloc;
            m_input = m_session->GetInputNameAllocated(0, alloc).get();
            m_output = m_session->GetOutputNameAllocated(0, alloc).get();

            // The custom metadata of the sherpa-onnx export (absent keys keep segmentation-3.0's values).
            Ort::ModelMetadata md = m_session->GetModelMetadata();
            auto num = [&](const char* key, int fallback) {
                Ort::AllocatedStringPtr v = md.LookupCustomMetadataMapAllocated(key, alloc);
                if (!v) return fallback;
                bool ok = false;
                const int x = QString::fromUtf8(v.get()).trimmed().toInt(&ok);
                return ok ? x : fallback;
            };
            if (Ort::AllocatedStringPtr t = md.LookupCustomMetadataMapAllocated("model_type", alloc))
                m_info.modelType = QString::fromUtf8(t.get());
            m_info.sampleRate = num("sample_rate", m_info.sampleRate);
            m_info.windowSize = num("window_size", m_info.windowSize);
            m_info.receptiveFieldSize = num("receptive_field_size", m_info.receptiveFieldSize);
            m_info.receptiveFieldShift = num("receptive_field_shift", m_info.receptiveFieldShift);
            m_info.numSpeakers = num("num_speakers", m_info.numSpeakers);
            m_info.powersetMaxClasses = num("powerset_max_classes", m_info.powersetMaxClasses);
            m_info.numClasses = num("num_classes", m_info.numClasses);

            // Output [N, frames, classes]: the static frame / class counts win over the metadata.
            const std::vector<int64_t> out =
                m_session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
            m_info.framesPerWindow = m_info.computedFramesPerWindow();
            if (out.size() == 3) {
                if (out[1] > 0) m_info.framesPerWindow = int(out[1]);
                if (out[2] > 0) m_info.numClasses = int(out[2]);
            }
            const std::vector<int64_t> in =
                m_session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
            if (in.size() != 3 || (in[1] > 0 && in[1] != 1)) {
                if (error) *error = QStringLiteral("Not a segmentation model: the input is not [N, 1, samples].");
                return false;
            }
            if (in[2] > 0) m_info.windowSize = int(in[2]);
            if (!m_info.isValid()) {
                if (error)
                    *error = QStringLiteral("Not a pyannote powerset segmentation model (speakers %1, max %2, classes %3).")
                                 .arg(m_info.numSpeakers).arg(m_info.powersetMaxClasses).arg(m_info.numClasses);
                return false;
            }
            return true;
        } catch (const std::exception& e) {
            if (error) *error = QStringLiteral("ONNX load error: %1").arg(QString::fromUtf8(e.what()));
            m_session.reset();
            return false;
        }
    }

    const ModelInfo& info() const override { return m_info; }

    bool run(const float* samples, int count, QVector<float>* out, QString* error) override
    {
        if (!m_session || count <= 0) return false;
        try {
            Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            std::array<int64_t, 3> shape{count, 1, m_info.windowSize};
            Ort::Value input = Ort::Value::CreateTensor<float>(mem, const_cast<float*>(samples),
                                                               size_t(count) * m_info.windowSize,
                                                               shape.data(), shape.size());
            const char* inNames[] = {m_input.c_str()};
            const char* outNames[] = {m_output.c_str()};
            auto outputs = m_session->Run(Ort::RunOptions{nullptr}, inNames, &input, 1, outNames, 1);
            if (outputs.empty()) {
                if (error) *error = QStringLiteral("Empty ONNX output.");
                return false;
            }
            const auto shapeOut = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
            if (shapeOut.size() != 3 || shapeOut[0] != count || shapeOut[1] != m_info.framesPerWindow
                || shapeOut[2] != m_info.numClasses) {
                if (error) *error = QStringLiteral("Unexpected segmentation output shape.");
                return false;
            }
            const size_t n = size_t(count) * m_info.framesPerWindow * m_info.numClasses;
            out->resize(qsizetype(n));
            std::memcpy(out->data(), outputs[0].GetTensorData<float>(), n * sizeof(float));
            return true;
        } catch (const std::exception& e) {
            if (error) *error = QStringLiteral("ONNX inference error: %1").arg(QString::fromUtf8(e.what()));
            return false;
        }
    }

private:
    Ort::Env m_env{ORT_LOGGING_LEVEL_ERROR, "tanara-segmentation"};
    std::unique_ptr<Ort::Session> m_session;
    std::string m_input, m_output;
    ModelInfo m_info;
};

} // namespace

std::unique_ptr<SegmentationModel> loadOnnxSegmentationModel(const QString& path, const OnnxSegmentationOptions& opts,
                                                             QString* error)
{
    if (!QFileInfo::exists(path)) {
        if (error) *error = QStringLiteral("Missing segmentation model: %1").arg(QDir::toNativeSeparators(path));
        return nullptr;
    }
    auto m = std::make_unique<OnnxSegmentationModel>();
    if (!m->load(path, opts, error)) return nullptr;
    return m;
}

QStringList onnxExecutionProviders()
{
    QStringList out;
    try {
        for (const std::string& p : Ort::GetAvailableProviders()) out.append(QString::fromStdString(p));
    } catch (const std::exception&) {
    }
    return out;
}

#else // !TANARA_HAVE_VOICEID

std::unique_ptr<SegmentationModel> loadOnnxSegmentationModel(const QString&, const OnnxSegmentationOptions&,
                                                             QString* error)
{
    if (error) *error = QStringLiteral("This build has no ONNX Runtime (voice ID is disabled).");
    return nullptr;
}

QStringList onnxExecutionProviders() { return {}; }

#endif

// ---- SegmentationResult -------------------------------------------------------------------------------

Activity SegmentationResult::windowActivity(int w) const
{
    if (w < 0 || w >= windows.size()) return {};
    return decodePowerset(windows[w].classes, powersetClasses(info.numSpeakers, info.powersetMaxClasses),
                          info.numSpeakers);
}

QVector<LocalTurn> SegmentationResult::windowTurns(int w) const
{
    if (w < 0 || w >= windows.size()) return {};
    WindowActivity wa{windows[w].startFrame, windowActivity(w)};
    return segmentation::windowTurns(w, wa, info);
}

// ---- PyannoteSegmenter ------------------------------------------------------------------------------

PyannoteSegmenter::PyannoteSegmenter(std::unique_ptr<SegmentationModel> model, SegmenterOptions opts, QString modelId)
    : m_model(std::move(model)), m_opts(opts), m_modelId(std::move(modelId))
{
    if (m_model) m_powerset = powersetClasses(m_model->info().numSpeakers, m_model->info().powersetMaxClasses);
}

PyannoteSegmenter::~PyannoteSegmenter() = default;

const ModelInfo& PyannoteSegmenter::info() const
{
    static const ModelInfo none;
    return m_model ? m_model->info() : none;
}

int PyannoteSegmenter::windowCount(qint64 numSamples) const
{
    const ModelInfo& mi = info();
    return int(windowStarts(numSamples, mi.windowSize, windowShiftSamples(mi, m_opts.windowShiftRatio)).size());
}

bool PyannoteSegmenter::isUsable(const VoiceModelSpec& spec, const QString& modelsDir, const QString& appDir)
{
    using V = VoiceModelDownloader::VerifyResult;
    const QString path = VoiceModelRegistry::resolvePath(spec, modelsDir, appDir);
    const V r = VoiceModelDownloader::verifyFile(spec, path, nullptr, /*checkSha=*/false);
    if (r == V::Missing) return false;
    return r != V::SizeMismatch || VoiceModelDownloader::hasAcceptedRecord(spec, modelsDir);
}

std::unique_ptr<PyannoteSegmenter> PyannoteSegmenter::open(const VoiceModelSpec& spec, const QString& modelsDir,
                                                           const QString& appDir, const SegmenterOptions& opts,
                                                           const OnnxSegmentationOptions& ort, QString* error)
{
    using V = VoiceModelDownloader::VerifyResult;
    const QString path = VoiceModelRegistry::resolvePath(spec, modelsDir, appDir);
    QString detail;
    const V r = VoiceModelDownloader::verifyFile(spec, path, &detail);
    switch (r) {
    case V::Missing:
        if (error)
            *error = QStringLiteral("The segmentation model %1 is missing (%2); fetch it with: tanara-cli voice-models fetch %1")
                         .arg(spec.id, QDir::toNativeSeparators(path));
        return nullptr;
    case V::SizeMismatch:
    case V::ShaMismatch:
        // verifyFile already says Unverified for the exact file the user accepted: this one is not it.
        qCWarning(lcVoice).noquote() << "Segmentation model" << spec.id << "refused:" << detail;
        if (error)
            *error = QStringLiteral("The segmentation model %1 does not match its pinned SHA-256 (%2). "
                                    "Use it anyway with: tanara-cli voice-models accept %1").arg(spec.id, detail);
        return nullptr;
    case V::Unverified:
        qCWarning(lcVoice).noquote() << "Segmentation model" << spec.id << "is UNVERIFIED (user override):" << path;
        break;
    case V::Unverifiable:
        qCInfo(lcVoice).noquote() << "Segmentation model" << spec.id << "is not pinned:" << path;
        break;
    case V::Ok:
        qCInfo(lcVoice).noquote() << "Segmentation model" << spec.id << "verified:" << path;
        break;
    }
    QString loadError;
    std::unique_ptr<SegmentationModel> model = loadOnnxSegmentationModel(path, ort, &loadError);
    if (!model) {
        qCWarning(lcVoice).noquote() << "Segmentation model" << spec.id << "failed to load:" << loadError;
        if (error) *error = loadError;
        return nullptr;
    }
    return std::make_unique<PyannoteSegmenter>(std::move(model), opts, spec.id);
}

std::optional<SegmentationResult> PyannoteSegmenter::segment(const QVector<float>& pcm, const Progress& progress,
                                                             const std::atomic<bool>* cancel, QString* error)
{
    if (!m_model) {
        if (error) *error = QStringLiteral("No segmentation model.");
        return std::nullopt;
    }
    const ModelInfo& mi = m_model->info();
    SegmentationResult res;
    res.modelId = m_modelId;
    res.info = mi;
    res.numSamples = pcm.size();
    res.windowShift = windowShiftSamples(mi, m_opts.windowShiftRatio);
    const QVector<qint64> starts = windowStarts(pcm.size(), mi.windowSize, res.windowShift);
    if (starts.isEmpty()) return res;

    const int total = int(starts.size());
    const int batch = std::max(1, m_opts.batchSize);
    const int F = mi.framesPerWindow, C = mi.numClasses, W = mi.windowSize;
    QVector<float> buf, logits;
    res.windows.reserve(total);
    for (int b0 = 0; b0 < total; b0 += batch) {
        if (cancel && cancel->load()) {
            if (error) *error = QStringLiteral("cancelled");
            return std::nullopt;
        }
        const int n = std::min(batch, total - b0);
        buf.fill(0.0f, qsizetype(n) * W);   // zero padding past the end of the audio
        for (int i = 0; i < n; ++i) {
            const qint64 s = starts[b0 + i];
            const qint64 len = std::min<qint64>(W, pcm.size() - s);
            if (len > 0) std::memcpy(buf.data() + qsizetype(i) * W, pcm.constData() + s, size_t(len) * sizeof(float));
        }
        QString runError;
        if (!m_model->run(buf.constData(), n, &logits, &runError)) {
            if (error) *error = runError.isEmpty() ? QStringLiteral("Segmentation inference failed.") : runError;
            return std::nullopt;
        }
        for (int i = 0; i < n; ++i) {
            SegmentationWindow w;
            w.startSample = starts[b0 + i];
            w.startFrame = int(w.startSample / mi.receptiveFieldShift);   // the shift is a whole number of frames
            w.classes = argmaxClasses(logits.constData() + qsizetype(i) * F * C, F, C);
            res.windows.append(std::move(w));
        }
        if (progress) progress(b0 + n, total);
    }

    QVector<WindowActivity> acts;
    acts.reserve(res.windows.size());
    for (const SegmentationWindow& w : res.windows)
        acts.append({w.startFrame, decodePowerset(w.classes, m_powerset, mi.numSpeakers)});
    const StitchResult st = stitchWindows(acts, m_opts.stitch);
    for (int k = 0; k < res.windows.size(); ++k) res.windows[k].stitched = st.localToStitched[k];
    res.numStitched = st.numStitched;
    res.numFrames = std::min(globalFrameCount(mi, pcm.size()), res.windows.last().startFrame + F);
    const FrameLabels labels = aggregate(acts, st, res.numFrames, mi.powersetMaxClasses);
    res.turns = labelsToTurns(labels, mi, m_opts.minDurationOn, m_opts.minDurationOff);
    return res;
}

} // namespace tanara::processing
