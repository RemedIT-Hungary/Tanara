#include "tanara/library/store/ArchiveService.h"

#include "tanara/domain/Job.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/MeetingArchive.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/tags/TagService.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QThread>
#include <QUuid>

namespace tanara::library {

struct ArchiveService::Impl {
    MeetingRepository* store = nullptr;
    TagService*        tags = nullptr;
    MeetingJobTracker* jobs = nullptr;
    std::function<bool(const Meeting&)> isBeingRecorded;

    // The running operation: one at a time, on a background thread.
    QThread* thread = nullptr;
    std::shared_ptr<std::atomic<bool>> cancel;
    QString op;     // "export:<meetingId>" | "import:<uuid>"
};

ArchiveService::ArchiveService(MeetingRepository* store, TagService* tags, MeetingJobTracker* jobs,
                               QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
    d->store = store;
    d->tags = tags;
    d->jobs = jobs;
}

ArchiveService::~ArchiveService()
{
    if (d->thread) {
        if (d->cancel) d->cancel->store(true);
        d->thread->disconnect(this);
        d->thread->wait();
        delete d->thread;
        d->thread = nullptr;
    }
}

void ArchiveService::setRecordingGuard(std::function<bool(const Meeting&)> isBeingRecorded)
{
    d->isBeingRecorded = std::move(isBeingRecorded);
}

bool ArchiveService::busy() const { return d->thread != nullptr; }
QString ArchiveService::currentOp() const { return d->op; }

bool ArchiveService::cancel(const QString& meetingId)
{
    if (d->op != QStringLiteral("export:") + meetingId || !d->cancel
        || !d->jobs->isRunning(meetingId, JobKind::Export))
        return false;
    d->jobs->setCancelling(meetingId, JobKind::Export);
    d->cancel->store(true);   // the thread stops at the next block, the half-made file is deleted
    return true;
}

bool ArchiveService::exportNow(const QString& meetingId, const QString& zipPath, QString* error,
                               std::function<void(int)> progress)
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) {
        if (error) *error = tr("Nincs ilyen megbeszélés: %1").arg(meetingId);
        return false;
    }
    if (d->isBeingRecorded && d->isBeingRecorded(m)) {
        if (error) *error = tr("Felvétel közben a megbeszélés nem exportálható.");
        return false;
    }
    return MeetingArchive::exportMeeting(m, zipPath, error, std::move(progress),
                                         MeetingArchive::tagsOf(d->tags, m));
}

Meeting ArchiveService::importArchiveNow(const QString& zipPath, QString* error,
                                         std::function<void(int)> progress)
{
    const QString root = QDir(d->store->audioDir()).absolutePath();
    QDir().mkpath(root);
    ArchiveManifest manifest;
    const QString folder = MeetingArchive::importArchive(zipPath, root, error, std::move(progress), &manifest);
    if (folder.isEmpty()) return {};
    return MeetingArchive::adoptFolder(d->store, d->tags, folder, manifest, error);
}

QString ArchiveService::exportMeeting(const QString& meetingId, const QString& zipPath)
{
    if (busy()) {
        emit rejected(tr("Már fut egy archívum-művelet — várd meg, vagy szakítsd meg."));
        return {};
    }
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) {
        emit rejected(tr("Nincs ilyen megbeszélés: %1").arg(meetingId));
        return {};
    }
    if (d->isBeingRecorded && d->isBeingRecorded(m)) {
        emit rejected(tr("Felvétel közben a megbeszélés nem exportálható."));
        return {};
    }
    const QVector<Tag> tags = MeetingArchive::tagsOf(d->tags, m);
    const QString opId = QStringLiteral("export:") + meetingId;
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    auto ok = std::make_shared<bool>(false);
    auto error = std::make_shared<QString>();
    d->jobs->begin(meetingId, JobKind::Export, tr("Exportálás archívumba"));
    d->jobs->setMessage(meetingId, JobKind::Export, QFileInfo(zipPath).fileName());
    d->jobs->setPercent(meetingId, JobKind::Export, 0);

    QPointer<ArchiveService> self(this);
    QThread* th = QThread::create([self, m, zipPath, tags, cancel, ok, error, opId]() {
        *ok = MeetingArchive::exportMeeting(m, zipPath, error.get(), [self, opId, mid = m.id](int pct) {
            if (!self) return;
            QMetaObject::invokeMethod(self.data(), [self, opId, mid, pct]() {
                if (!self || self->d->op != opId) return;
                if (self->d->jobs->isRunning(mid, JobKind::Export))
                    self->d->jobs->setPercent(mid, JobKind::Export, pct);
                emit self->progress(opId, pct);
            }, Qt::QueuedConnection);
        }, tags, cancel.get());
    });
    d->thread = th;
    d->cancel = cancel;
    d->op = opId;
    connect(th, &QThread::finished, this, [this, th, meetingId, zipPath, cancel, ok, error, opId]() {
        d->thread = nullptr;
        d->cancel.reset();
        d->op.clear();
        th->deleteLater();
        if (*ok) {
            d->jobs->finish(meetingId, JobKind::Export);
            emit finished(opId, true, meetingId, zipPath, QString());
        } else if (cancel->load()) {
            d->jobs->cancelled(meetingId, JobKind::Export);
            emit finished(opId, false, meetingId, QString(), tr("Megszakítva."));
        } else {
            JobError je;
            je.kind = JobKind::Export;
            je.message = *error;
            je.when = QDateTime::currentDateTime();
            d->jobs->fail(meetingId, JobKind::Export, je);
            emit finished(opId, false, meetingId, QString(), *error);
        }
    });
    th->start();
    return opId;
}

QString ArchiveService::importFolder(const QString& folderIn)
{
    if (busy()) {
        emit rejected(tr("Már fut egy archívum-művelet — várd meg, vagy szakítsd meg."));
        return {};
    }
    const QString folder = QFileInfo(folderIn).absoluteFilePath();
    const QString root = QDir(d->store->audioDir()).absolutePath();
    const QString opId = QStringLiteral("import:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto staged = std::make_shared<QString>();
    auto error = std::make_shared<QString>();
    // A source under the recordings folder stays in place (no copy); everything else first goes into the
    // .import-* temporary folder on a background thread, so the GUI does not freeze.
    const bool inPlace = QFileInfo(folder).dir().absolutePath() == root;

    QPointer<ArchiveService> self(this);
    MeetingRepository* store = d->store;
    QThread* th = QThread::create([self, folder, inPlace, store, staged, error, opId]() {
        if (inPlace) { *staged = folder; return; }
        *staged = store->stageFolderForImport(folder, error.get());
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, opId]() {
            if (self && self->d->op == opId) emit self->progress(opId, 100);
        }, Qt::QueuedConnection);
    });
    d->thread = th;
    d->cancel.reset();
    d->op = opId;
    connect(th, &QThread::finished, this, [this, th, folder, staged, error, opId]() {
        d->thread = nullptr;
        d->op.clear();
        th->deleteLater();
        QString message = *error;
        Meeting m;
        if (!staged->isEmpty())
            m = MeetingArchive::adoptFolder(d->store, d->tags, *staged, ArchiveManifest{}, &message);
        if (m.id.isEmpty())
            emit finished(opId, false, QString(), folder, message);
        else
            emit finished(opId, true, m.id, folder, QString());
    });
    th->start();
    return opId;
}

QString ArchiveService::importArchive(const QString& zipPath)
{
    if (busy()) {
        emit rejected(tr("Már fut egy archívum-művelet — várd meg, vagy szakítsd meg."));
        return {};
    }
    const QString root = QDir(d->store->audioDir()).absolutePath();
    QDir().mkpath(root);
    const QString opId = QStringLiteral("import:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto folder = std::make_shared<QString>();
    auto error = std::make_shared<QString>();
    auto manifest = std::make_shared<ArchiveManifest>();

    QPointer<ArchiveService> self(this);
    QThread* th = QThread::create([self, zipPath, root, folder, error, manifest, opId]() {
        *folder = MeetingArchive::importArchive(zipPath, root, error.get(), [self, opId](int pct) {
            if (!self) return;
            QMetaObject::invokeMethod(self.data(), [self, opId, pct]() {
                if (self && self->d->op == opId) emit self->progress(opId, pct);
            }, Qt::QueuedConnection);
        }, manifest.get());
    });
    d->thread = th;
    d->cancel.reset();   // unpacking cannot be cancelled (it is short, and cleans up at the end anyway)
    d->op = opId;
    connect(th, &QThread::finished, this, [this, th, zipPath, folder, error, manifest, opId]() {
        d->thread = nullptr;
        d->op.clear();
        th->deleteLater();
        QString message = *error;
        Meeting m;
        if (!folder->isEmpty())
            m = MeetingArchive::adoptFolder(d->store, d->tags, *folder, *manifest, &message);
        if (m.id.isEmpty())
            emit finished(opId, false, QString(), zipPath, message);
        else
            emit finished(opId, true, m.id, zipPath, QString());
    });
    th->start();
    return opId;
}

} // namespace tanara::library
