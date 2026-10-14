#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace tanara::processing {

QVector<VoiceModelSpec> VoiceModelRegistry::builtin()
{
    // The download is from the sherpa-onnx "speaker-recongition-models" (sic) release: the models are
    // available there as a single ONNX file with [B,T,80] fbank input. Size: the release assets.
    const QString sherpa = QStringLiteral(
        "https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-recongition-models/");

    // CAM++ (3D-Speaker). License: the ModelScope API "License" field (Apache License 2.0).
    VoiceModelSpec campplus;
    campplus.id = defaultModelId();
    campplus.displayName = QStringLiteral("CAM++ (3D-Speaker)");
    campplus.fileName = QStringLiteral("campplus_sv_zh_en_16k.onnx");
    campplus.features = EmbedderConfig();   // VoiceEmbedder's defaults are tuned for this model
    campplus.dim = 192;
    campplus.license = QStringLiteral("Apache-2.0");
    campplus.sourceUrl = QStringLiteral(
        "https://modelscope.cn/models/iic/speech_campplus_sv_zh_en_16k-common_advanced");
    campplus.downloadUrl = sherpa + QStringLiteral("3dspeaker_speech_campplus_sv_zh_en_16k-common_advanced.onnx");
    // The existing campplus_sv_zh_en_16k.onnx is byte-for-byte the same size as this release asset.
    campplus.sizeBytes = 28281164;

    // ERes2NetV2 (3D-Speaker): the same front end as CAM++'s (3D-Speaker Kaldi.fbank, 80 bins, dither 0,
    // mean normalization). License: ModelScope API (Apache License 2.0).
    VoiceModelSpec eres;
    eres.id = QStringLiteral("eres2netv2");
    eres.displayName = QStringLiteral("ERes2NetV2 (3D-Speaker)");
    eres.fileName = QStringLiteral("3dspeaker_speech_eres2netv2_sv_zh-cn_16k-common.onnx");
    eres.features = campplus.features;
    eres.dim = 192;
    eres.license = QStringLiteral("Apache-2.0");
    eres.sourceUrl = QStringLiteral("https://modelscope.cn/models/iic/speech_eres2netv2_sv_zh-cn_16k-common");
    eres.downloadUrl = sherpa + eres.fileName;
    eres.sizeBytes = 71441526;

    // WeSpeaker ResNet34-LM (VoxCeleb). License: the HF model card's cardData (cc-by-4.0).
    // Front end: the ground truth of training is compute_fbank() in wespeaker/bin/infer_onnx.py:
    //   waveform * (1 << 15)                         -> waveScale 32768 (int16 scale)
    //   kaldi.fbank(num_mel_bins=80, frame_length=25, frame_shift=10, dither=0.0,
    //               sample_frequency=16000, window_type='hamming', use_energy=False)
    //                                                -> numMelBins 80, dither 0, windowType "hamming";
    //     the rest are torchaudio-kaldi defaults (snip_edges=True, low_freq 20, high_freq 0 = Nyquist,
    //     preemph 0.97, remove_dc_offset), which are also kaldi-native-fbank's defaults; energy_floor
    //     matters only with use_energy.
    //   mat - mean(mat, dim=0)  ("CMN, without CVN")  -> subtractMean true.
    // sherpa-onnx (features.h default: povey window, snip_edges=false; on the wespeaker model
    // normalize_samples=0 -> x32768, feature_normalize_type empty -> no CMN) differs from this; we follow
    // the training pipeline.
    VoiceModelSpec wespeaker;
    wespeaker.id = QStringLiteral("wespeaker-resnet34-lm");
    wespeaker.displayName = QStringLiteral("WeSpeaker ResNet34-LM (VoxCeleb)");
    wespeaker.fileName = QStringLiteral("wespeaker_en_voxceleb_resnet34_LM.onnx");
    wespeaker.features.numMelBins = 80;
    wespeaker.features.sampleRate = 16000;
    wespeaker.features.snipEdges = true;
    wespeaker.features.dither = 0.0f;
    wespeaker.features.subtractMean = true;
    wespeaker.features.waveScale = 32768.0f;
    wespeaker.features.windowType = QStringLiteral("hamming");
    wespeaker.dim = 256;
    wespeaker.license = QStringLiteral("CC-BY-4.0");
    wespeaker.sourceUrl = QStringLiteral("https://huggingface.co/Wespeaker/wespeaker-voxceleb-resnet34-LM");
    wespeaker.downloadUrl = sherpa + wespeaker.fileName;
    wespeaker.sizeBytes = 26530550;

    return {campplus, eres, wespeaker};
}

std::optional<VoiceModelSpec> VoiceModelRegistry::spec(const QString& id)
{
    for (const VoiceModelSpec& s : builtin())
        if (s.id == id) return s;
    return std::nullopt;
}

QString VoiceModelRegistry::resolvePath(const VoiceModelSpec& spec, const QString& modelsDir,
                                        const QString& appDir)
{
    const QString rel = QStringLiteral("models/") + spec.fileName;   // bundled: <appDir>/models/<file>
    const QString user = QDir(modelsDir).filePath(spec.fileName);
    if (QFileInfo::exists(user))
        return user;
    if (!appDir.isEmpty()) {
        const QString bundled = QDir(appDir).filePath(rel);
        if (QFileInfo::exists(bundled))
            return bundled;
    }
    return user;
}

QString VoiceModelRegistry::defaultModelFileName()
{
    return spec(defaultModelId())->fileName;
}

QString VoiceModelRegistry::resolveDefaultModelPath(const QString& modelsDir, const QString& appDir)
{
    // Single resolution rule: resolvePath (for the base model).
    return resolvePath(*spec(defaultModelId()), modelsDir, appDir);
}

QVector<VoiceModelSpec> VoiceModelRegistry::available(const QString& modelsDir, const QString& appDir)
{
    QVector<VoiceModelSpec> out;
    for (const VoiceModelSpec& s : builtin())
        if (QFileInfo::exists(resolvePath(s, modelsDir, appDir))) out.append(s);
    return out;
}

namespace {
QVector<VoiceModelSpec> enabledAvailable(const QStringList& ids, const QString& modelsDir, const QString& appDir)
{
    QVector<VoiceModelSpec> out;
    for (const QString& id : VoiceModelRegistry::normalizeIds(ids)) {
        const auto s = VoiceModelRegistry::spec(id);
        if (s && QFileInfo::exists(VoiceModelRegistry::resolvePath(*s, modelsDir, appDir))) out.append(*s);
    }
    return out;
}
} // namespace

QVector<VoiceModelSpec> VoiceModelRegistry::active(const QStringList& enabledIds, const QString& modelsDir,
                                                   const QString& appDir)
{
    QVector<VoiceModelSpec> out = enabledAvailable(enabledIds, modelsDir, appDir);
    // Fallback until WeSpeaker is downloaded: there is no usable enabled model, but CAM++ is present (it
    // is in the Windows package), so that one runs.
    if (out.isEmpty()) {
        const auto cam = spec(defaultModelId());
        if (cam && QFileInfo::exists(resolvePath(*cam, modelsDir, appDir))) out.append(*cam);
    }
    return out;
}

bool VoiceModelRegistry::isFallbackActive(const QStringList& enabledIds, const QString& modelsDir,
                                          const QString& appDir)
{
    if (!enabledAvailable(enabledIds, modelsDir, appDir).isEmpty()) return false;
    const auto cam = spec(defaultModelId());
    return cam && QFileInfo::exists(resolvePath(*cam, modelsDir, appDir));
}

QStringList VoiceModelRegistry::normalizeIds(const QStringList& ids)
{
    return voicemodels::normalizeIds(ids);
}

} // namespace tanara::processing