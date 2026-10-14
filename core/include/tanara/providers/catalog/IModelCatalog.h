#pragma once
//
// Model-catalog port (CapabilityKind::ModelCatalog): which models a capability can use and how to
// make one usable. A provider descriptor names its catalog in ProviderDescriptor::catalogRef.
//
//  - processing::LocalVoiceModelCatalog: the built-in ONNX speaker models (VoiceModelRegistry) and
//    their download (VoiceModelDownloader): ensureAvailable gives the LOCAL file path;
//  - cloud::CloudModelCatalog: the gateway's /v1/models?capability=...: ensureAvailable gives the
//    REMOTE model id (nothing to download).
#include "tanara/providers/core/CapabilityJob.h"
#include "tanara/providers/core/ProviderDescriptor.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::providers {

struct ModelInfo {
    QString id;
    QString displayName;
    CapabilityKind capability = CapabilityKind::VoiceEmbedding;
    Origin  origin = Origin::Local;
    bool    available = false;      // usable now (local: the file exists; Cloud: listed)
    bool    recommended = false;
    QString tier;                   // Cloud: fast | accurate | "" (concrete)
    int     dim = 0;                // embedding models: the vector dimension (0 = unknown)
    qint64  sizeBytes = 0;          // local: the download size (0 = unknown)
    QString license;
    QString sourceUrl;
    QString localPath;              // local: the resolved (or expected) file
};

// Where a model is, once available: a local file OR a remote id (exactly one is set).
struct ModelLocation {
    QString modelId;
    QString localPath;
    QString remoteId;
    bool isLocal() const { return !localPath.isEmpty(); }
};

using ModelListJob = ResultJob<QVector<ModelInfo>>;
using ModelReadyJob = ResultJob<ModelLocation>;

class IModelCatalog {
public:
    virtual ~IModelCatalog() = default;
    virtual QString name() const = 0;
    // The models of a capability (an empty list for a capability this catalog does not serve).
    virtual ModelListJob* list(CapabilityKind capability) = 0;
    // Makes the model usable: downloads it if needed (progress through the sink AND the job's
    // progress signal). An unknown id fails with Error::Kind::Input.
    virtual ModelReadyJob* ensureAvailable(const QString& modelId, ProgressSink progress = {}) = 0;
};

template <> struct ProviderTraits<IModelCatalog> {
    static constexpr CapabilityKind kind = CapabilityKind::ModelCatalog;
};

} // namespace tanara::providers
