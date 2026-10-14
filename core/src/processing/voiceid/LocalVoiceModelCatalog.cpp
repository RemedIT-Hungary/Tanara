#include "tanara/processing/voiceid/LocalVoiceModelCatalog.h"

#include <QFileInfo>
#include <QPointer>

namespace tanara::processing {

using providers::CapabilityKind;

LocalVoiceModelCatalog::LocalVoiceModelCatalog(QString modelsDir, QString appDir, QVector<VoiceModelSpec> specs,
                                               net::HttpClient* http, QObject* parent)
    : QObject(parent)
    , m_modelsDir(std::move(modelsDir))
    , m_appDir(std::move(appDir))
    , m_specs(specs.isEmpty() ? VoiceModelRegistry::catalog() : std::move(specs))
    , m_downloader(new VoiceModelDownloader(this))
{
    if (http) m_downloader->setHttpClient(http);
}

providers::ModelListJob* LocalVoiceModelCatalog::list(CapabilityKind capability)
{
    return new providers::FunctionJob<QVector<providers::ModelInfo>>(
        [this, capability](QVector<providers::ModelInfo>* out, Error*) {
            // Speaker-embedding models serve VoiceEmbedding, segmentation models serve Diarization.
            if (capability != CapabilityKind::VoiceEmbedding && capability != CapabilityKind::Diarization)
                return true;
            const VoiceModelKind wanted = capability == CapabilityKind::Diarization ? VoiceModelKind::Segmentation
                                                                                    : VoiceModelKind::SpeakerEmbedding;
            for (const VoiceModelSpec& s : m_specs) {
                if (s.kind != wanted) continue;
                providers::ModelInfo i;
                i.id = s.id;
                i.displayName = s.displayName;
                i.capability = capability;
                i.origin = providers::Origin::Local;
                i.localPath = VoiceModelRegistry::resolvePath(s, m_modelsDir, m_appDir);
                i.available = QFileInfo::exists(i.localPath);
                i.recommended = s.id == VoiceModelRegistry::recommendedModelId()
                                || s.id == VoiceModelRegistry::defaultSegmentationModelId();
                i.dim = s.dim;
                i.sizeBytes = s.sizeBytes;
                i.license = s.license;
                i.sourceUrl = s.sourceUrl;
                i.sha256 = s.sha256;
                i.note = s.note;
                if (i.available) {
                    switch (VoiceModelDownloader::verifyFile(s, i.localPath)) {
                    case VoiceModelDownloader::VerifyResult::Ok:           i.state = providers::ModelState::Verified; break;
                    case VoiceModelDownloader::VerifyResult::Unverified:   i.state = providers::ModelState::Unverified; break;
                    case VoiceModelDownloader::VerifyResult::ShaMismatch:  i.state = providers::ModelState::Mismatch; break;
                    case VoiceModelDownloader::VerifyResult::SizeMismatch: i.state = providers::ModelState::Corrupt; break;
                    default:                                               i.state = providers::ModelState::Unchecked; break;
                    }
                } else if (VoiceModelDownloader::hasQuarantined(s, m_modelsDir)) {
                    i.state = providers::ModelState::Mismatch;   // downloaded, parked: waits for the user's decision
                }
                out->append(i);
            }
            return true;
        },
        this);
}

providers::ModelReadyJob* LocalVoiceModelCatalog::ensureAvailable(const QString& modelId,
                                                                   providers::ProgressSink progress)
{
    auto* job = new providers::ModelReadyJob(this);
    const VoiceModelSpec* spec = nullptr;
    for (const VoiceModelSpec& s : m_specs)
        if (s.id == modelId) spec = &s;
    if (!spec) {
        job->failLater(providers::inputError(QStringLiteral("unknown_model"), modelId));
        return job;
    }
    providers::ModelLocation loc;
    loc.modelId = modelId;
    const QString existing = VoiceModelRegistry::resolvePath(*spec, m_modelsDir, m_appDir);
    if (QFileInfo::exists(existing)) {
        loc.localPath = existing;
        job->completeLater(loc);
        return job;
    }

    // Download into <modelsDir>/<fileName>.
    const QString target = VoiceModelDownloader::targetPath(*spec, m_modelsDir);
    QPointer<providers::ModelReadyJob> guard(job);
    const QString id = spec->id;
    connect(m_downloader, &VoiceModelDownloader::progress, job,
            [guard, id, progress](const QString& which, qint64 received, qint64 total) {
        if (!guard || which != id) return;
        if (progress) progress(received, total);
        emit guard->progress(received, total);
    });
    connect(m_downloader, &VoiceModelDownloader::finished, job,
            [this, guard, id, target, loc](const QString& which, bool ok, const QString& error) mutable {
        if (!guard || which != id) return;
        if (ok) {
            loc.localPath = target;
            guard->completeLater(loc);
            return;
        }
        Error e;
        switch (m_downloader->lastFailure(id)) {
        case VoiceModelDownloader::Failure::Offline:
        case VoiceModelDownloader::Failure::Network:   e.kind = Error::Kind::Transient; break;
        case VoiceModelDownloader::Failure::Cancelled: e.kind = Error::Kind::Cancelled; break;
        case VoiceModelDownloader::Failure::NoUrl:     e.kind = Error::Kind::Unsupported; break;
        default:                                       e.kind = Error::Kind::Service; break;
        }
        e.code = m_downloader->lastFailure(id) == VoiceModelDownloader::Failure::ShaMismatch
                     ? QStringLiteral("sha_mismatch") : QStringLiteral("download_failed");
        e.detail = error;
        guard->failLater(e);
    });
    connect(job, &providers::CapabilityJob::destroyed, m_downloader, [this, id]() {
        if (m_downloader->isRunning(id)) m_downloader->cancel(id);
    });
    // A failed start reports finished(id, false, ...) inside the call; the handler above takes it.
    // "Already downloading" returns false without a signal: the running download's finished serves us.
    m_downloader->start(*spec, target);
    return job;
}

bool LocalVoiceModelCatalog::acceptMismatch(const QString& modelId, QString* error)
{
    for (const VoiceModelSpec& s : m_specs)
        if (s.id == modelId) return VoiceModelDownloader::acceptMismatch(s, m_modelsDir, error);
    if (error) *error = QStringLiteral("Unknown model: %1").arg(modelId);
    return false;
}

} // namespace tanara::processing
