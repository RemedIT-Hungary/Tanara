#include "tanara/voiceid/VoiceModelRegistry.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace tanara {

QVector<VoiceModelSpec> VoiceModelRegistry::builtin()
{
    // A letöltés a sherpa-onnx "speaker-recongition-models" (sic) release-éből: ott a modellek
    // egyetlen ONNX-fájlként, [B,T,80] fbank bemenettel érhetők el. Méret: a release asset-ek.
    const QString sherpa = QStringLiteral(
        "https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-recongition-models/");

    // CAM++ (3D-Speaker). Licenc: a ModelScope API "License" mezője (Apache License 2.0).
    VoiceModelSpec campplus;
    campplus.id = defaultModelId();
    campplus.displayName = QStringLiteral("CAM++ (3D-Speaker)");
    campplus.fileName = QStringLiteral("campplus_sv_zh_en_16k.onnx");
    campplus.features = EmbedderConfig();   // a VoiceEmbedder alapértelmezése erre a modellre hangolt
    campplus.dim = 192;
    campplus.license = QStringLiteral("Apache-2.0");
    campplus.sourceUrl = QStringLiteral(
        "https://modelscope.cn/models/iic/speech_campplus_sv_zh_en_16k-common_advanced");
    campplus.downloadUrl = sherpa + QStringLiteral("3dspeaker_speech_campplus_sv_zh_en_16k-common_advanced.onnx");
    // A meglévő campplus_sv_zh_en_16k.onnx bájtra ugyanekkora, mint ez a release asset.
    campplus.sizeBytes = 28281164;

    // ERes2NetV2 (3D-Speaker): ugyanaz a front-end, mint a CAM++-é (3D-Speaker Kaldi.fbank, 80 bin,
    // dither 0, mean-normalizálás). Licenc: ModelScope API (Apache License 2.0).
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

    // WeSpeaker ResNet34-LM (VoxCeleb). Licenc: a HF modellkártya cardData-ja (cc-by-4.0).
    // Front-end — a tanítás igazsága a wespeaker/bin/infer_onnx.py compute_fbank():
    //   waveform * (1 << 15)                         → waveScale 32768 (int16-skála)
    //   kaldi.fbank(num_mel_bins=80, frame_length=25, frame_shift=10, dither=0.0,
    //               sample_frequency=16000, window_type='hamming', use_energy=False)
    //                                                → numMelBins 80, dither 0, windowType "hamming";
    //     a többi a torchaudio-kaldi alapértéke (snip_edges=True, low_freq 20, high_freq 0 = Nyquist,
    //     preemph 0.97, remove_dc_offset) — ezek a kaldi-native-fbank alapértékei is; az
    //     energy_floor csak use_energy mellett számít.
    //   mat - mean(mat, dim=0)  ("CMN, without CVN")  → subtractMean true.
    // A sherpa-onnx (features.h alap: povey ablak, snip_edges=false; wespeaker-modellen
    // normalize_samples=0 → ×32768, feature_normalize_type üres → nincs CMN) ettől eltér; mi a
    // tanítási pipeline-t követjük.
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

QString VoiceModelRegistry::resolvePath(const VoiceModelSpec& spec, const QString& metaDir,
                                        const QString& appDir)
{
    const QString rel = QStringLiteral("models/") + spec.fileName;
    const QString user = QDir(metaDir).filePath(rel);
    if (QFileInfo::exists(user))
        return user;
    if (!appDir.isEmpty()) {
        const QString bundled = QDir(appDir).filePath(rel);
        if (QFileInfo::exists(bundled))
            return bundled;
    }
    return user;
}

QVector<VoiceModelSpec> VoiceModelRegistry::available(const QString& metaDir, const QString& appDir)
{
    QVector<VoiceModelSpec> out;
    for (const VoiceModelSpec& s : builtin())
        if (QFileInfo::exists(resolvePath(s, metaDir, appDir))) out.append(s);
    return out;
}

QVector<VoiceModelSpec> VoiceModelRegistry::active(const QStringList& enabledIds, const QString& metaDir,
                                                   const QString& appDir)
{
    QVector<VoiceModelSpec> out;
    for (const QString& id : normalizeIds(enabledIds)) {
        const auto s = spec(id);
        if (s && QFileInfo::exists(resolvePath(*s, metaDir, appDir))) out.append(*s);
    }
    return out;
}

QStringList VoiceModelRegistry::normalizeIds(const QStringList& ids)
{
    QStringList out;
    for (const QString& id : ids) {
        const QString t = id.trimmed();
        if (!t.isEmpty() && !out.contains(t)) out << t;
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace tanara
