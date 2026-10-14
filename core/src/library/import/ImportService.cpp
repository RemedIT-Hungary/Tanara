#include "tanara/library/import/ImportService.h"

#include "tanara/domain/Job.h"
#include "tanara/library/import/AudioImporter.h"
#include "tanara/library/jobs/MeetingJobTracker.h"

namespace tanara::library {

ImportService::ImportService(MeetingRepository* store, media::MediaToolPtr media,
                             MeetingJobTracker* jobs, QObject* parent)
    : QObject(parent), m_importer(new AudioImporter(store, std::move(media), this)), m_jobs(jobs)
{
    // Import -> a structured task under the FUTURE meeting's id (there is no such meeting in the library
    // yet; the shell shows it from activeJobs() / the signals).
    connect(m_importer, &AudioImporter::started, this, [this](const QString& id, const QString& title) {
        m_jobs->begin(id, JobKind::Import, tr("Importálás"));
        m_jobs->setMessage(id, JobKind::Import, title);
    });
    connect(m_importer, &AudioImporter::progress, this,
            [this](const QString& id, int percent, int fileIndex, int fileCount) {
        m_jobs->setPercent(id, JobKind::Import, percent);
        if (fileCount > 1)
            m_jobs->setCounts(id, JobKind::Import, fileIndex, fileCount);
    });
    connect(m_importer, &AudioImporter::finished, this, [this](const QString& id, const Meeting& m) {
        m_jobs->finish(id, JobKind::Import);
        emit finished(m);
    });
    connect(m_importer, &AudioImporter::failed, this,
            [this](const QString& id, const QString& message, const QString& detail) {
        JobError je;
        je.kind = JobKind::Import;
        je.message = message;
        je.detail = detail;
        m_jobs->fail(id, JobKind::Import, je);
        emit failed(id, message, detail);
    });
    connect(m_importer, &AudioImporter::cancelled, this, [this](const QString& id) {
        m_jobs->cancelled(id, JobKind::Import);
        emit cancelled(id);
    });
}

ImportService::~ImportService() = default;

AudioImporter* ImportService::importer() const { return m_importer; }
bool ImportService::busy() const { return m_importer->busy(); }

QString ImportService::start(const ImportRequest& request, int opusBitrateKbps, const SelfPerson& self)
{
    if (m_importer->busy()) {
        emit rejected(tr("Már fut egy importálás — várd meg, vagy szakítsd meg."));
        return {};
    }
    m_importer->setOpusBitrateKbps(opusBitrateKbps);
    m_importer->setSelfPerson(self);
    return m_importer->start(request);
}

bool ImportService::cancel(const QString& meetingId)
{
    if (m_importer->currentId() != meetingId || !m_jobs->isRunning(meetingId, JobKind::Import))
        return false;
    m_jobs->setCancelling(meetingId, JobKind::Import);
    m_importer->cancel();   // the cancelled branch closes: the half-made folder is deleted
    return true;
}

} // namespace tanara::library
