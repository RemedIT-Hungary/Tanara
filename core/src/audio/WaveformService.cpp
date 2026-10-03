#include "tanara/audio/WaveformService.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QTimer>
#include <algorithm>
#include <cstdlib>

namespace tanara {

namespace {
constexpr int kMaxConcurrent = 2;
constexpr int kRate = 8000;            // dekódolási mintavétel (csúcsokhoz bőven elég)
constexpr int kWindow = 400;           // finom ablak: 50 ms
constexpr int kCacheVersion = 1;

// Finom ablak-csúcsok → pontosan `buckets` (vagy kevesebb, ha nincs annyi ablak) vödör.
QVector<float> reduceMax(const QVector<float>& fine, int buckets)
{
    const int n = int(fine.size());
    if (n == 0 || buckets <= 0) return {};
    if (n <= buckets) return fine;
    QVector<float> out(buckets, 0.0f);
    for (int b = 0; b < buckets; ++b) {
        const int from = int(qint64(b) * n / buckets);
        const int to   = qMax(from + 1, int(qint64(b + 1) * n / buckets));
        float mx = 0.0f;
        for (int i = from; i < to && i < n; ++i) mx = qMax(mx, fine.at(i));
        out[b] = mx;
    }
    return out;
}
} // namespace

struct WaveformService::Job {
    QString meetingId;
    QString trackId;
    QString audioPath;
    QProcess* proc = nullptr;
    QByteArray carry;            // páratlan bájt a darabhatáron
    QVector<float> fine;         // 50 ms-os ablakok csúcsai
    int   winFill = 0;           // minták az aktuális ablakban
    int   winMax = 0;            // |minta| max az aktuális ablakban
    qint64 samples = 0;
    bool  cancelled = false;
    qint64 srcSize = 0;
    qint64 srcMtimeMs = 0;

    void consume(const QByteArray& chunk) {
        QByteArray data = carry + chunk;
        const int n = int(data.size() / 2);
        const auto* p = reinterpret_cast<const qint16*>(data.constData());
        for (int i = 0; i < n; ++i) {
            const int a = std::abs(int(p[i]));
            if (a > winMax) winMax = a;
            if (++winFill == kWindow) {
                fine.append(float(winMax) / 32768.0f);
                winFill = 0;
                winMax = 0;
            }
        }
        samples += n;
        carry = data.mid(n * 2);
    }
};

WaveformService::WaveformService(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<tanara::TrackPeaks>();
}

WaveformService::~WaveformService()
{
    for (Job* j : std::as_const(m_running)) {
        if (j->proc) {
            j->proc->disconnect(this);
            j->proc->kill();
            j->proc->waitForFinished(1000);
            delete j->proc;
        }
        delete j;
    }
    qDeleteAll(m_queue);
}

QString WaveformService::cachePath(const QString& audioPath)
{
    return audioPath + QStringLiteral(".peaks.json");
}

void WaveformService::removeCache(const QString& audioPath)
{
    QFile::remove(cachePath(audioPath));
}

TrackPeaks WaveformService::loadCached(const QString& audioPath)
{
    TrackPeaks out;
    const QFileInfo src(audioPath);
    if (!src.exists()) return out;
    QFile f(cachePath(audioPath));
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.value(QStringLiteral("version")).toInt() != kCacheVersion) return out;
    // A gyorsítótár a forrásfájl állapotához kötött: méret + módosítási idő.
    if (qint64(o.value(QStringLiteral("srcSize")).toDouble()) != src.size()) return out;
    if (qint64(o.value(QStringLiteral("srcMtimeMs")).toDouble()) != src.lastModified().toMSecsSinceEpoch())
        return out;
    const QJsonArray arr = o.value(QStringLiteral("peaks")).toArray();
    out.peaks.reserve(arr.size());
    for (const QJsonValue& v : arr)
        out.peaks.append(float(qBound(0, v.toInt(), 1000)) / 1000.0f);
    out.durationMs = qint64(o.value(QStringLiteral("durationMs")).toDouble());
    return out;
}

QVector<float> WaveformService::resample(const QVector<float>& peaks, int buckets)
{
    const int n = int(peaks.size());
    if (n == 0 || buckets <= 0) return {};
    if (buckets < n) return reduceMax(peaks, buckets);
    QVector<float> out(buckets);
    for (int b = 0; b < buckets; ++b)
        out[b] = peaks.at(int(qint64(b) * n / buckets));
    return out;
}

bool WaveformService::isPending(const QString& audioPath) const
{
    for (const Job* j : m_running) if (j->audioPath == audioPath && !j->cancelled) return true;
    for (const Job* j : m_queue)   if (j->audioPath == audioPath) return true;
    return false;
}

void WaveformService::request(const QString& meetingId, const QString& trackId,
                              const QString& audioPath)
{
    if (!QFileInfo::exists(audioPath)) {
        QTimer::singleShot(0, this, [this, meetingId, trackId]() {
            emit peaksFailed(meetingId, trackId, tr("A hangfájl nem található."));
        });
        return;
    }
    TrackPeaks cached = loadCached(audioPath);
    if (cached.isValid()) {
        cached.trackId = trackId;
        QTimer::singleShot(0, this, [this, meetingId, trackId, cached]() {
            emit peaksReady(meetingId, trackId, cached);
        });
        return;
    }
    if (isPending(audioPath)) return;
    auto* job = new Job;
    job->meetingId = meetingId;
    job->trackId = trackId;
    job->audioPath = audioPath;
    m_queue.append(job);
    startNext();
}

void WaveformService::cancel(const QString& meetingId)
{
    for (int i = m_queue.size() - 1; i >= 0; --i)
        if (m_queue.at(i)->meetingId == meetingId) delete m_queue.takeAt(i);
    for (Job* j : std::as_const(m_running))
        if (j->meetingId == meetingId && !j->cancelled) {
            j->cancelled = true;
            if (j->proc) j->proc->kill();   // a finished ág takarít (jel nélkül)
        }
}

void WaveformService::startNext()
{
    while (m_running.size() < kMaxConcurrent && !m_queue.isEmpty()) {
        Job* job = m_queue.takeFirst();
        const QFileInfo src(job->audioPath);
        job->srcSize = src.size();
        job->srcMtimeMs = src.lastModified().toMSecsSinceEpoch();

        auto* proc = new QProcess(this);
        job->proc = proc;
        m_running.append(job);
        proc->setProgram(m_ffmpeg);
        proc->setArguments({QStringLiteral("-hide_banner"), QStringLiteral("-v"), QStringLiteral("error"),
                            QStringLiteral("-i"), job->audioPath, QStringLiteral("-vn"),
                            QStringLiteral("-ac"), QStringLiteral("1"),
                            QStringLiteral("-ar"), QString::number(kRate),
                            QStringLiteral("-f"), QStringLiteral("s16le"), QStringLiteral("pipe:1")});
        connect(proc, &QProcess::readyReadStandardOutput, this, [job, proc]() {
            job->consume(proc->readAllStandardOutput());
        });
        connect(proc, &QProcess::errorOccurred, this, [this, job, proc](QProcess::ProcessError err) {
            if (err != QProcess::FailedToStart) return;   // a többi esetben jön finished
            finishJob(job, false, tr("Az ffmpeg nem indítható."));
            proc->deleteLater();
        });
        connect(proc, &QProcess::finished, this,
                [this, job, proc](int code, QProcess::ExitStatus status) {
            job->consume(proc->readAllStandardOutput());
            const bool ok = status == QProcess::NormalExit && code == 0 && job->samples > 0;
            const QString err = ok ? QString()
                : QString::fromUtf8(proc->readAllStandardError()).trimmed().left(300);
            finishJob(job, ok, err.isEmpty() ? tr("A hullámforma számítása nem sikerült.") : err);
            proc->deleteLater();
        });
        proc->start();
    }
}

void WaveformService::finishJob(Job* job, bool ok, const QString& error)
{
    if (!m_running.removeOne(job)) return;   // már lezárva
    if (!job->cancelled) {
        if (ok) {
            if (job->winFill > 0)                // a csonka utolsó ablak
                job->fine.append(float(job->winMax) / 32768.0f);
            TrackPeaks tp;
            tp.trackId = job->trackId;
            tp.peaks = reduceMax(job->fine, kBuckets);
            tp.durationMs = job->samples * 1000 / kRate;

            QJsonArray arr;
            for (float v : std::as_const(tp.peaks))
                arr.append(qBound(0, int(v * 1000.0f + 0.5f), 1000));
            QJsonObject o;
            o[QStringLiteral("version")]    = kCacheVersion;
            o[QStringLiteral("srcSize")]    = double(job->srcSize);
            o[QStringLiteral("srcMtimeMs")] = double(job->srcMtimeMs);
            o[QStringLiteral("durationMs")] = double(tp.durationMs);
            o[QStringLiteral("peaks")]      = arr;
            QSaveFile f(cachePath(job->audioPath));
            if (f.open(QIODevice::WriteOnly)) {
                f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
                f.commit();
            }
            // A jel a gyorsítótárral egyező (kvantált) értékeket adja — így az első és a
            // későbbi megnyitás pontosan ugyanazt rajzolja.
            for (float& v : tp.peaks) v = float(qBound(0, int(v * 1000.0f + 0.5f), 1000)) / 1000.0f;
            emit peaksReady(job->meetingId, job->trackId, tp);
        } else {
            emit peaksFailed(job->meetingId, job->trackId, error);
        }
    }
    delete job;
    startNext();
}

} // namespace tanara
