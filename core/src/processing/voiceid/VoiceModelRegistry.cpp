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
    campplus.sha256 = QStringLiteral("aa3cfc16963a10586a9393f5035d6d6b57e98d358b347f80c2a30bf4f00ceba2");

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
    eres.sha256 = QStringLiteral("bf1a75b9930474cf3389ef415e6e5d38ca96fea4a3a00f7e301d080a58ee2239");

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
    wespeaker.sha256 = QStringLiteral("e9848563da86f263117134dfd7ad63c92355b37de492b55e325400c9d9c39012");

    return {campplus, eres, wespeaker};
}

QVector<VoiceModelSpec> VoiceModelRegistry::segmentation()
{
    // pyannote/segmentation-3.0 (MIT, Copyright (c) 2022 CNRS), ONNX export by the sherpa-onnx project
    // (k2-fsa). The files are byte-identical to the ones inside the sherpa release archive
    // https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-segmentation-models/
    //   sherpa-onnx-pyannote-segmentation-3-0.tar.bz2 (6958444 B, SHA-256 24615ee8...6488); we download
    // them one by one from the Hugging Face mirror of that export (no bzip2 decoder needed). Both models
    // and the LICENSE live in one folder under the models dir.
    const QString hf = QStringLiteral(
        "https://huggingface.co/csukuangfj/sherpa-onnx-pyannote-segmentation-3-0/resolve/main/");
    const QString dir = QStringLiteral("pyannote-segmentation-3.0/");
    const QString source = QStringLiteral("https://huggingface.co/pyannote/segmentation-3.0");

    VoiceModelFile license;
    license.fileName = QStringLiteral("LICENSE");
    license.downloadUrl = hf + QStringLiteral("LICENSE");
    license.sizeBytes = 1061;
    license.sha256 = QStringLiteral("14d7016ad68e7394d6e6b78d96cc2ae431c905287b89674cfdf021e79e62b8ba");

    VoiceModelSpec seg;
    seg.id = defaultSegmentationModelId();
    seg.kind = VoiceModelKind::Segmentation;
    seg.displayName = QStringLiteral("pyannote segmentation 3.0");
    seg.fileName = dir + QStringLiteral("model.onnx");
    seg.license = QStringLiteral("MIT");
    seg.sourceUrl = source;
    seg.downloadUrl = hf + QStringLiteral("model.onnx");
    seg.sizeBytes = 5992913;
    seg.sha256 = QStringLiteral("220ad67ca923bef2fa91f2390c786097bf305bceb5e261d4af67b38e938e1079");
    seg.companions = {license};
    seg.note = QStringLiteral("float32, the default");

    VoiceModelSpec int8 = seg;
    int8.id = defaultSegmentationModelId() + QStringLiteral("-int8");
    int8.displayName = QStringLiteral("pyannote segmentation 3.0 (int8)");
    int8.fileName = dir + QStringLiteral("model.int8.onnx");
    int8.downloadUrl = hf + QStringLiteral("model.int8.onnx");
    int8.sizeBytes = 1540506;
    int8.sha256 = QStringLiteral("d582f4b4c6b48205de7e0643c57df0df5615a3c176189be3fc461e9d18827b5d");
    // UI wording (D5): "kisebb, gyorsabb, pontosság mérés alatt".
    int8.note = QStringLiteral("optional: smaller and faster, accuracy under evaluation");

    return {seg, int8};
}

QVector<VoiceModelSpec> VoiceModelRegistry::catalog()
{
    QVector<VoiceModelSpec> all = builtin();
    all += segmentation();
    return all;
}

std::optional<VoiceModelSpec> VoiceModelRegistry::spec(const QString& id)
{
    for (const VoiceModelSpec& s : catalog())
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
        if (s && s->kind == VoiceModelKind::SpeakerEmbedding && QFileInfo::exists(VoiceModelRegistry::resolvePath(*s, modelsDir, appDir))) out.append(*s);
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