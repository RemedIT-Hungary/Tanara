#include "tanara/processing/voiceid/VoiceModelDownloader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QFileInfo>
#include "tanara/foundation/net/HttpClient.h"
#include <QNetworkInformation>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>

#include <memory>

namespace tanara::processing {

namespace {
const QString kPartSuffix = QStringLiteral(".part");
const QString kMismatchSuffix = QStringLiteral(".mismatch");
const QString kAcceptedSuffix = QStringLiteral(".accepted-sha256");

// One file of a download job: the companions first, the main model file last.
struct FileTask {
    QString target;
    QString url;
    qint64  size = 0;
    QString sha256;
};

QString readTrimmed(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}

// The files of a model: companions (next to the main file) and the main file itself, last.
QVector<FileTask> tasksOf(const VoiceModelSpec& spec, const QString& target)
{
    QVector<FileTask> out;
    const QDir dir(QFileInfo(target).absolutePath());
    for (const VoiceModelFile& c : spec.companions)
        out.append({dir.filePath(c.fileName), c.downloadUrl, c.sizeBytes, c.sha256.toLower()});
    out.append({target, spec.downloadUrl, spec.sizeBytes, spec.sha256.toLower()});
    return out;
}

// The override record and the parked file of an OLD file (it is being replaced).
void removeSidecars(const QString& path)
{
    QFile::remove(path + kAcceptedSuffix);
    QFile::remove(path + kMismatchSuffix);
}
} // namespace

struct VoiceModelDownloader::Job {
    VoiceModelSpec spec;
    QString target;
    bool overwrite = false;
    QVector<FileTask> tasks;
    int index = 0;
    qint64 doneBefore = 0;          // bytes of the files finished before the current one
    std::unique_ptr<QFile> file;    // <current target>.part
    QCryptographicHash hash{QCryptographicHash::Sha256};
    QPointer<QNetworkReply> reply;
    qint64 written = 0;
    bool writeFailed = false;
    bool cancelled = false;
    QStringList mismatches;         // human lines of the files whose SHA-256 differed
    const FileTask& task() const { return tasks.at(index); }
    qint64 remainingBytes() const
    {
        qint64 n = 0;
        for (int i = index + 1; i < tasks.size(); ++i) n += tasks.at(i).size;
        return n;
    }
    void discardPart()
    {
        if (!file) return;
        const QString name = file->fileName();
        file->close();
        QFile::remove(name);
        file.reset();
    }
};

VoiceModelDownloader::VoiceModelDownloader(QObject* parent) : QObject(parent) {}

VoiceModelDownloader::~VoiceModelDownloader()
{
    // Running downloads: the replies belong to the NAM; we detach and drop them (a half-made QSaveFile
    // leaves nothing behind without a commit).
    for (Job* job : std::as_const(m_jobs)) {
        if (job->reply) {
            job->reply->disconnect(this);
            job->reply->abort();
            job->reply->deleteLater();
        }
        job->discardPart();
        delete job;
    }
    m_jobs.clear();
}

void VoiceModelDownloader::setHttpClient(net::HttpClient* http) { m_http = http; }

net::HttpClient* VoiceModelDownloader::http()
{
    if (m_http) return m_http;
    if (!m_ownHttp) m_ownHttp = new net::HttpClient(this);
    return m_ownHttp;
}

QString VoiceModelDownloader::targetPath(const VoiceModelSpec& spec, const QString& modelsDir)
{
    return QDir(modelsDir).filePath(spec.fileName);
}

QString VoiceModelDownloader::quarantinePath(const VoiceModelSpec& spec, const QString& modelsDir)
{
    return targetPath(spec, modelsDir) + kMismatchSuffix;
}

bool VoiceModelDownloader::hasQuarantined(const VoiceModelSpec& spec, const QString& modelsDir)
{
    if (modelsDir.isEmpty() || spec.fileName.isEmpty()) return false;
    for (const FileTask& t : tasksOf(spec, targetPath(spec, modelsDir)))
        if (QFileInfo::exists(t.target + kMismatchSuffix)) return true;
    return false;
}

bool VoiceModelDownloader::hasAcceptedRecord(const VoiceModelSpec& spec, const QString& modelsDir)
{
    return !modelsDir.isEmpty() && !spec.fileName.isEmpty()
           && QFileInfo::exists(targetPath(spec, modelsDir) + kAcceptedSuffix);
}

QString VoiceModelDownloader::sha256OfFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) return QString();
    return QString::fromLatin1(h.result().toHex());
}

bool VoiceModelDownloader::isRunning(const QString& id) const { return m_jobs.contains(id); }
QStringList VoiceModelDownloader::runningIds() const { return m_jobs.keys(); }

VoiceModelDownloader::Failure VoiceModelDownloader::lastFailure(const QString& id) const
{
    return m_last.value(id, Failure::None);
}

void VoiceModelDownloader::setReachableOverride(std::optional<bool> reachable) { m_reachableOverride = reachable; }

bool VoiceModelDownloader::networkReachable() const
{
    if (m_reachableOverride) return *m_reachableOverride;
    // The backend loads once (process-wide). If there is none (e.g. a container), we cannot know: then we
    // just try, and the network error shows up anyway.
    static const bool loaded = QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability);
    if (!loaded || !QNetworkInformation::instance()) return true;
    return QNetworkInformation::instance()->reachability() != QNetworkInformation::Reachability::Disconnected;
}

bool VoiceModelDownloader::isOfflineError(int e)
{
    switch (static_cast<QNetworkReply::NetworkError>(e)) {
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::UnknownNetworkError:
        return true;
    default:
        return false;
    }
}

void VoiceModelDownloader::fail(const QString& id, Failure why, const QString& error)
{
    m_last.insert(id, why);
    emit finished(id, false, error);
}

bool VoiceModelDownloader::start(const VoiceModelSpec& spec, const QString& target, bool overwrite)
{
    const QString id = spec.id;
    if (m_jobs.contains(id)) {
        // The running download goes on; we send no signal (the observers wait for the running download's end).
        return false;
    }
    const QUrl url(spec.downloadUrl);
    if (spec.downloadUrl.isEmpty() || !url.isValid()) {
        fail(id, Failure::NoUrl, QStringLiteral("No download URL for %1").arg(id));
        return false;
    }
    if (QFileInfo::exists(target) && !overwrite) {
        fail(id, Failure::Exists, QStringLiteral("The file already exists: %1").arg(QDir::toNativeSeparators(target)));
        return false;
    }
    if (!url.isLocalFile() && !networkReachable()) {
        fail(id, Failure::Offline, QStringLiteral("No network connection."));
        return false;
    }
    QDir().mkpath(QFileInfo(target).absolutePath());

    auto* job = new Job;
    job->spec = spec;
    job->target = target;
    job->overwrite = overwrite;
    // The companions that are already in place (a LICENSE shared by two models) are not fetched again.
    for (const FileTask& t : tasksOf(spec, target)) {
        const bool isMain = t.target == target;
        const QFileInfo fi(t.target);
        const bool fine = !isMain && fi.isFile() && (t.size <= 0 || fi.size() == t.size)
                          && (t.sha256.isEmpty() || sha256OfFile(t.target) == t.sha256);
        if (!fine) job->tasks.append(t);
    }
    m_jobs.insert(id, job);
    m_last.remove(id);

    if (!beginFile(id)) return false;   // failed to open the first .part: fail() already reported
    emit started(id);
    emit progress(id, 0, spec.sizeBytes);
    return true;
}

// Opens <task.target>.part and sends the request. false: could not open (the job is dropped, failure reported).
bool VoiceModelDownloader::beginFile(const QString& id)
{
    Job* job = m_jobs.value(id);
    if (!job) return false;
    const FileTask& t = job->task();
    QDir().mkpath(QFileInfo(t.target).absolutePath());
    job->file = std::make_unique<QFile>(t.target + kPartSuffix);
    if (!job->file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString msg = QStringLiteral("Cannot write %1: %2").arg(QDir::toNativeSeparators(t.target),
                                                                     job->file->errorString());
        job->file.reset();
        delete m_jobs.take(id);
        fail(id, Failure::Write, msg);
        return false;
    }
    job->hash.reset();
    job->written = 0;

    QNetworkRequest req{QUrl(t.url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = http()->get(req);
    job->reply = reply;

    connect(reply, &QNetworkReply::readyRead, this, [this, id, reply]() {
        Job* j = m_jobs.value(id);
        if (!j || j->reply != reply || j->cancelled || !reply->isOpen()) return;
        const QByteArray chunk = reply->readAll();
        if (j->file->write(chunk) != chunk.size()) {
            j->writeFailed = true;
            reply->abort();
            return;
        }
        j->hash.addData(chunk);
        j->written += chunk.size();
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, id, reply](qint64 got, qint64 total) {
        const Job* j = m_jobs.value(id);
        if (!j || j->reply != reply) return;
        if (total <= 0) total = j->task().size;
        emit progress(id, j->doneBefore + got, total > 0 ? j->doneBefore + total + j->remainingBytes() : 0);
    });
    connect(reply, &QNetworkReply::finished, this, [this, id, reply]() {
        const Job* j = m_jobs.value(id);
        if (j && j->reply == reply) finishJob(id);
    });
    return true;
}

void VoiceModelDownloader::cancel(const QString& id)
{
    Job* job = m_jobs.value(id);
    if (!job || !job->reply) return;
    job->cancelled = true;
    job->reply->abort();   // the finished signal (OperationCanceledError) closes it
}

void VoiceModelDownloader::finishJob(const QString& id)
{
    Job* job = m_jobs.value(id);
    if (!job) return;
    QNetworkReply* reply = job->reply;
    auto drop = [&](Failure why, const QString& msg) {
        std::unique_ptr<Job> owned(m_jobs.take(id));
        owned->discardPart();
        fail(id, why, msg);
    };
    if (!reply) {
        drop(Failure::Network, QStringLiteral("Download interrupted."));
        return;
    }
    if (!job->writeFailed && !job->cancelled && reply->isOpen()) {
        const QByteArray rest = reply->readAll();
        if (!rest.isEmpty()) {
            if (job->file->write(rest) != rest.size()) job->writeFailed = true;
            else { job->hash.addData(rest); job->written += rest.size(); }
        }
    }
    const QNetworkReply::NetworkError netErr = reply->error();
    const QString netMsg = reply->errorString();
    reply->deleteLater();
    job->reply = nullptr;

    const FileTask t = job->task();
    if (job->cancelled) {
        drop(Failure::Cancelled, QStringLiteral("Cancelled."));
        return;
    }
    if (job->writeFailed) {
        drop(Failure::Write, QStringLiteral("Cannot write %1: %2").arg(QDir::toNativeSeparators(t.target),
                                                                       job->file->errorString()));
        return;
    }
    if (netErr != QNetworkReply::NoError) {
        drop(isOfflineError(netErr) ? Failure::Offline : Failure::Network, netMsg);
        return;
    }
    if (t.size > 0 && job->written != t.size) {
        drop(Failure::SizeMismatch,
             QStringLiteral("Size mismatch: got %1 bytes, expected %2 - nothing was written.")
                 .arg(job->written).arg(t.size));
        return;
    }

    // The file is complete: settle it. A hash mismatch parks it as <target>.mismatch (a warning the user
    // may override); otherwise it replaces the target atomically (rename).
    const QString actual = QString::fromLatin1(job->hash.result().toHex());
    const bool shaBad = !t.sha256.isEmpty() && actual != t.sha256;
    const QString partName = job->file->fileName();
    if (!job->file->flush()) {
        drop(Failure::Write, QStringLiteral("Cannot save %1: %2").arg(QDir::toNativeSeparators(t.target),
                                                                      job->file->errorString()));
        return;
    }
    job->file->close();
    job->file.reset();
    removeSidecars(t.target);   // a stale override / quarantine belongs to the old file
    bool moved = false;
    if (shaBad) {
        moved = QFile::rename(partName, t.target + kMismatchSuffix);
        job->mismatches << QStringLiteral("%1: expected %2, got %3")
                               .arg(QFileInfo(t.target).fileName(), t.sha256, actual);
    } else {
        QFile::remove(t.target);   // overwrite (the caller allowed it, or this is a companion refresh)
        moved = QFile::rename(partName, t.target);
    }
    if (!moved) {
        QFile::remove(partName);
        drop(Failure::Write, QStringLiteral("Cannot save %1.").arg(QDir::toNativeSeparators(t.target)));
        return;
    }
    job->doneBefore += job->written;

    if (job->index + 1 < job->tasks.size()) {   // the next file
        ++job->index;
        beginFile(id);   // a failure to open is reported inside
        return;
    }
    std::unique_ptr<Job> owned(m_jobs.take(id));
    if (!owned->mismatches.isEmpty()) {
        fail(id, Failure::ShaMismatch,
             QStringLiteral("The downloaded file does not match the pinned SHA-256 (%1). "
                            "It is kept aside as %2; accept it to use it anyway.")
                 .arg(owned->mismatches.join(QStringLiteral("; ")),
                      QDir::toNativeSeparators(owned->target + kMismatchSuffix)));
        return;
    }
    m_last.insert(id, Failure::None);
    emit progress(id, owned->doneBefore, owned->doneBefore);
    emit finished(id, true, QString());
}

// ---- verification and deletion ---------------------------------------------------------

VoiceModelDownloader::VerifyResult VoiceModelDownloader::verifyFile(const VoiceModelSpec& spec, const QString& path,
                                                                    QString* detail, bool checkSha)
{
    const QFileInfo fi(path);
    if (path.isEmpty() || !fi.exists() || !fi.isFile()) {
        if (detail) *detail = QStringLiteral("Missing file: %1").arg(QDir::toNativeSeparators(path));
        return VerifyResult::Missing;
    }
    const QString pinned = spec.sha256.toLower();
    // The user's override covers exactly the accepted file, whatever its size.
    if (checkSha && !pinned.isEmpty() && QFileInfo::exists(path + kAcceptedSuffix)) {
        const QString actual = sha256OfFile(path);
        if (!actual.isEmpty() && actual != pinned && readTrimmed(path + kAcceptedSuffix) == actual) {
            if (detail)
                *detail = QStringLiteral("SHA-256 differs from the pinned one (expected %1, got %2); "
                                         "accepted by the user.").arg(pinned, actual);
            return VerifyResult::Unverified;
        }
    }
    if (spec.sizeBytes <= 0 && (pinned.isEmpty() || !checkSha)) {
        if (detail) *detail = QStringLiteral("No expected size for %1; %2 bytes on disk.").arg(spec.id).arg(fi.size());
        return VerifyResult::Unverifiable;
    }
    if (spec.sizeBytes > 0 && fi.size() != spec.sizeBytes) {
        if (detail)
            *detail = QStringLiteral("Size mismatch: %1 bytes on disk, expected %2.").arg(fi.size()).arg(spec.sizeBytes);
        return VerifyResult::SizeMismatch;
    }
    if (checkSha && !pinned.isEmpty()) {
        const QString actual = sha256OfFile(path);
        if (actual != pinned) {
            if (detail)
                *detail = QStringLiteral("SHA-256 mismatch: expected %1, got %2.").arg(pinned, actual);
            return VerifyResult::ShaMismatch;
        }
    }
    if (detail) detail->clear();
    return VerifyResult::Ok;
}

bool VoiceModelDownloader::verify(const VoiceModelSpec& spec, const QString& path)
{
    if (spec.id.isEmpty() || m_verifying.contains(spec.id) || isRunning(spec.id)) return false;
    m_verifying << spec.id;
    emit verifyStarted(spec.id);
    QPointer<VoiceModelDownloader> self(this);
    QTimer::singleShot(0, this, [self, spec, path]() {
        if (!self) return;
        QString detail;
        const VerifyResult r = verifyFile(spec, path, &detail);
        self->m_verifying.removeAll(spec.id);
        self->m_verified.insert(spec.id, r);
        emit self->verified(spec.id, r, detail);
    });
    return true;
}

bool VoiceModelDownloader::isVerifying(const QString& id) const { return m_verifying.contains(id); }

std::optional<VoiceModelDownloader::VerifyResult> VoiceModelDownloader::lastVerify(const QString& id) const
{
    const auto it = m_verified.constFind(id);
    if (it == m_verified.constEnd()) return std::nullopt;
    return it.value();
}

bool VoiceModelDownloader::deleteModelFile(const VoiceModelSpec& spec, const QString& modelsDir, QString* error)
{
    const QString target = targetPath(spec, modelsDir);
    const QFileInfo fi(target);
    const bool parked = hasQuarantined(spec, modelsDir);
    if (modelsDir.isEmpty() || spec.fileName.isEmpty() || ((!fi.exists() || !fi.isFile()) && !parked)) {
        if (error) *error = QStringLiteral("No downloaded file for %1 (%2).").arg(spec.id, QDir::toNativeSeparators(target));
        return false;
    }
    if (fi.exists()) {
        QFile f(target);
        if (!f.remove()) {
            if (error) *error = QStringLiteral("Cannot delete %1: %2").arg(QDir::toNativeSeparators(target), f.errorString());
            return false;
        }
    }
    QFile::remove(target + kAcceptedSuffix);
    QFile::remove(target + kMismatchSuffix);
    // The companions (LICENSE) belong to the folder: they go when no other model file is left in it.
    if (!spec.companions.isEmpty()) {
        const QDir dir(fi.absolutePath());
        bool otherModels = false;
        for (const QFileInfo& e : dir.entryInfoList(QDir::Files))
            if (e.fileName().endsWith(QLatin1String(".onnx"))) otherModels = true;
        if (!otherModels) {
            for (const VoiceModelFile& c : spec.companions) {
                const QString p = dir.filePath(c.fileName);
                QFile::remove(p);
                removeSidecars(p);
            }
            if (dir.absolutePath() != QDir(modelsDir).absolutePath()) QDir().rmdir(dir.absolutePath());
        }
    }
    if (error) error->clear();
    return true;
}

bool VoiceModelDownloader::acceptMismatch(const VoiceModelSpec& spec, const QString& modelsDir, QString* error)
{
    const QString target = targetPath(spec, modelsDir);
    bool anything = false;
    for (const FileTask& t : tasksOf(spec, target)) {
        const QString parked = t.target + kMismatchSuffix;
        if (QFileInfo::exists(parked)) {
            QFile::remove(t.target);
            if (!QFile::rename(parked, t.target)) {
                if (error) *error = QStringLiteral("Cannot move %1 into place.").arg(QDir::toNativeSeparators(parked));
                return false;
            }
            anything = true;
        }
        if (t.sha256.isEmpty() || !QFileInfo(t.target).isFile()) continue;
        const QString actual = sha256OfFile(t.target);
        if (actual.isEmpty() || actual == t.sha256) continue;
        QFile rec(t.target + kAcceptedSuffix);
        if (!rec.open(QIODevice::WriteOnly | QIODevice::Truncate) || rec.write(actual.toUtf8()) < 0) {
            if (error) *error = QStringLiteral("Cannot record the override: %1").arg(rec.errorString());
            return false;
        }
        anything = true;
    }
    if (!anything || !QFileInfo(target).isFile()) {
        if (error) *error = QStringLiteral("Nothing to accept for %1.").arg(spec.id);
        return false;
    }
    if (error) error->clear();
    return true;
}

} // namespace tanara::processing
