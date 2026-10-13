#pragma once
//
// VoiceModelRegistry: descriptors of the built-in speaker-embedding models (id, file name, fbank
// parameters, license, source) and the resolution of the model file. A single resolution rule:
// <metaDir>/models/<fileName>, otherwise <appDir>/models/<fileName>.
// The list of enabled models is the "voiceModels" key of settings.json (SettingsManager).
#include "tanara/domain/Voice.h"
#include "tanara/voiceid/VoiceEmbedder.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace tanara {

struct VoiceModelSpec {
    QString id;               // stable id, e.g. "campplus" (lowercase, hyphen)
    QString displayName;      // "CAM++ (3D-Speaker)"
    QString fileName;         // ONNX file name in the models/ folder
    EmbedderConfig features;  // fbank parameters for this model
    int     dim = 0;          // expected dimension (0 = unknown until the first inference)
    QString license;          // "Apache-2.0" stb.
    QString sourceUrl;        // the model's (and its license's) description page
    QString downloadUrl;      // direct ONNX download (a sherpa-onnx release asset)
    qint64  sizeBytes = 0;    // expected size of the downloaded file (0 = unknown)
};

class VoiceModelRegistry {
public:
    // Model id of the "old" single-model paths (voiceprints and caches without a model field belong to this:
    // "campplus"). NOT the recommended model; for that see recommendedModelId().
    static QString defaultModelId() { return voicemodels::defaultModelId(); }
    // The recommended (and by default enabled) model: WeSpeaker ResNet34-LM. On four real meetings it
    // separated the speakers best.
    static QString recommendedModelId() { return QStringLiteral("wespeaker-resnet34-lm"); }
    // The default enabled list (if settings.json has no "voiceModels" key).
    static QStringList defaultEnabledIds() { return {recommendedModelId()}; }

    // Built-in descriptors, alphabetical by id: "campplus", "eres2netv2", "wespeaker-resnet34-lm".
    static QVector<VoiceModelSpec> builtin();
    static std::optional<VoiceModelSpec> spec(const QString& id);

    // <metaDir>/models/<fileName> if it exists; otherwise <appDir>/models/<fileName> if it exists;
    // otherwise the first (the "expected location", for an error message / download target). appDir may be empty.
    static QString resolvePath(const VoiceModelSpec& spec, const QString& metaDir, const QString& appDir);
    // File name and resolution of the DEFAULT model (defaultModelId(), CAM++) with the rule above. Order:
    //  1) <metaDir>/models/<name>  - the user's own (downloaded) model;
    //  2) <appDir>/models/<name>   - a model bundled with the app (Windows zip/installer);
    //  3) neither exists -> the path from 1) (the "expected location", for error messages / download target).
    // appDir may be empty (e.g. without QCoreApplication); then 2) is skipped.
    static QString defaultModelFileName();
    static QString resolveDefaultModelPath(const QString& metaDir, const QString& appDir);
    // Of the built-in models, those whose file exists.
    static QVector<VoiceModelSpec> available(const QString& metaDir, const QString& appDir);

    // The usable models: enabled intersect available (file exists), alphabetical by id.
    // Enabled ids that are unknown / have a missing file are dropped.
    // Fallback until WeSpeaker is downloaded: if enabled intersect available is empty but "campplus" is
    // available (e.g. it is bundled), it alone becomes ["campplus"].
    static QVector<VoiceModelSpec> active(const QStringList& enabledIds, const QString& metaDir,
                                          const QString& appDir);
    // True exactly when active() switched to the CAM++ fallback (the UI signals this).
    static bool isFallbackActive(const QStringList& enabledIds, const QString& metaDir,
                                 const QString& appDir);

    // Deterministic form of an id list: trimmed, without empties, unique, alphabetical.
    static QStringList normalizeIds(const QStringList& ids);
};

} // namespace tanara
