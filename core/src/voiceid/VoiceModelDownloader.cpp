#include "tanara/voiceid/VoiceModelDownloader.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkInformation>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSaveFile>
#include <QUrl>

#include <memory>

namespace tanara {

struct VoiceModelDownloader::Job {
    VoiceModelSpec spec;
    QString target;
    std::unique_ptr<QSaveFile> file;
    QPointer<QNetworkReply> reply;
    qint64 written = 0;
    bool writeFailed = false;
    bool cancelled = false;
};

VoiceModelDownloader::VoiceModelDownloader(QObject* parent) : QObject(parent) {}

VoiceModelDownloader::~VoiceModelDownloader()
{
    // Futó letöltés: a reply-k a NAM-hoz tartoznak; leválasztjuk és eldobjuk őket (a félkész
    // QSaveFile commit nélkül semmit nem hagy hátra).
    for (Job* job : std::as_const(m_jobs)) {
        if (job->reply) {
            job->reply->disconnect(this);
            job->reply->abort();
            job->reply->deleteLater();
        }
        if (job->file) job->file->cancelWriting();
        delete job;
    }
    m_jobs.clear();
}

void VoiceModelDownloader::setNetworkAccessManager(QNetworkAccessManager* nam) { m_nam = nam; }

QNetworkAccessManager* VoiceModelDownloader::nam()
{
    if (m_nam) return m_nam;
    if (!m_ownNam) m_ownNam = new QNetworkAccessManager(this);
    return m_ownNam;
}

QString VoiceModelDownloader::targetPath(const VoiceModelSpec& spec, const QString& metaDir)
{
    return QDir(metaDir).filePath(QStringLiteral("models/") + spec.fileName);
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
    // A backend egyszer töltődik be (folyamat-szintű). Ha nincs (pl. konténer), nem tudjuk:
    // ilyenkor megpróbáljuk, és a hálózati hiba úgyis kiderül.
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
        // A futó letöltés megy tovább; jelet nem küldünk (a figyelők a futó letöltés végét várják).
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
    auto file = std::make_unique<QSaveFile>(target);
    if (!file->open(QIODevice::WriteOnly)) {
        fail(id, Failure::Write, QStringLiteral("Cannot write %1: %2").arg(QDir::toNativeSeparators(target),
                                                                           file->errorString()));
        return false;
    }

    auto* job = new Job;
    job->spec = spec;
    job->target = target;
    job->file = std::move(file);
    m_jobs.insert(id, job);
    m_last.remove(id);

    QNetworkRequest req{url};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = nam()->get(req);
    job->reply = reply;

    connect(reply, &QNetworkReply::readyRead, this, [this, id]() {
        Job* j = m_jobs.value(id);
        if (!j || !j->reply) return;
        const QByteArray chunk = j->reply->readAll();
        if (j->file->write(chunk) != chunk.size()) {
            j->writeFailed = true;
            j->reply->abort();
            return;
        }
        j->written += chunk.size();
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, id](qint64 got, qint64 total) {
        const Job* j = m_jobs.value(id);
        if (!j) return;
        if (total <= 0) total = j->spec.sizeBytes;
        emit progress(id, got, total);
    });
    connect(reply, &QNetworkReply::finished, this, [this, id]() { finishJob(id); });

    emit started(id);
    emit progress(id, 0, spec.sizeBytes);
    return true;
}

void VoiceModelDownloader::cancel(const QString& id)
{
    Job* job = m_jobs.value(id);
    if (!job || !job->reply) return;
    job->cancelled = true;
    job->reply->abort();   // a finished jel (OperationCanceledError) zárja le
}

void VoiceModelDownloader::finishJob(const QString& id)
{
    std::unique_ptr<Job> job(m_jobs.take(id));
    if (!job) return;
    QNetworkReply* reply = job->reply;
    if (!reply) {
        job->file->cancelWriting();
        fail(id, Failure::Network, QStringLiteral("Download interrupted."));
        return;
    }
    if (!job->writeFailed && !job->cancelled) {
        const QByteArray rest = reply->readAll();
        if (!rest.isEmpty()) {
            if (job->file->write(rest) != rest.size()) job->writeFailed = true;
            else job->written += rest.size();
        }
    }
    const QNetworkReply::NetworkError netErr = reply->error();
    const QString netMsg = reply->errorString();
    reply->deleteLater();

    if (job->cancelled) {
        job->file->cancelWriting();
        fail(id, Failure::Cancelled, QStringLiteral("Cancelled."));
        return;
    }
    if (job->writeFailed) {
        const QString msg = job->file->errorString();
        job->file->cancelWriting();
        fail(id, Failure::Write, QStringLiteral("Cannot write %1: %2").arg(QDir::toNativeSeparators(job->target), msg));
        return;
    }
    if (netErr != QNetworkReply::NoError) {
        job->file->cancelWriting();
        fail(id, isOfflineError(netErr) ? Failure::Offline : Failure::Network, netMsg);
        return;
    }
    if (job->spec.sizeBytes > 0 && job->written != job->spec.sizeBytes) {
        job->file->cancelWriting();
        fail(id, Failure::SizeMismatch,
             QStringLiteral("Size mismatch: got %1 bytes, expected %2 — nothing was written.")
                 .arg(job->written).arg(job->spec.sizeBytes));
        return;
    }
    if (!job->file->commit()) {
        fail(id, Failure::Write, QStringLiteral("Cannot save %1: %2").arg(QDir::toNativeSeparators(job->target),
                                                                          job->file->errorString()));
        return;
    }
    m_last.insert(id, Failure::None);
    emit progress(id, job->written, job->written);
    emit finished(id, true, QString());
}

} // namespace tanara
