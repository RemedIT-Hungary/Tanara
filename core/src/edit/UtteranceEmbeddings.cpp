#include "tanara/edit/UtteranceEmbeddings.h"

#include "tanara/voiceid/VoiceEmbedder.h"

#include <QByteArray>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>

#include <algorithm>

namespace tanara {

namespace {

constexpr quint32 kCacheMagic = 0x54454d42;   // "TEMB"
constexpr quint32 kCacheVersion = 1;
constexpr int kSampleRate = 16000;

// CAM++ embedder egy meeting hangjára: a teljes hang 16 kHz mono s16-ként a memóriában
// (~115 MB / óra), a megszólalások ebből szeletelődnek.
class VoiceUtteranceEmbedder final : public IUtteranceEmbedder {
public:
    VoiceUtteranceEmbedder(const QString& modelPath, const QString& ffmpegPath)
        : m_modelPath(modelPath), m_ffmpeg(ffmpegPath) {}

    bool open(const QString& audioPath) override
    {
        m_pcm.clear();
        if (!QFileInfo::exists(m_modelPath)) {
            m_error = QStringLiteral("Hiányzik a hangmodell: %1").arg(m_modelPath);
            return false;
        }
        if (!QFileInfo::exists(audioPath)) {
            m_error = QStringLiteral("Hiányzik a hangfájl: %1").arg(audioPath);
            return false;
        }
        if (!m_embedder) {
            m_embedder = std::make_unique<VoiceEmbedder>(m_modelPath);
            if (!m_embedder->isValid()) {
                m_error = m_embedder->lastError();
                m_embedder.reset();
                return false;
            }
        }

        QProcess proc;
        proc.start(m_ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"),
                              QStringLiteral("-i"), audioPath,
                              QStringLiteral("-ac"), QStringLiteral("1"),
                              QStringLiteral("-ar"), QString::number(kSampleRate),
                              QStringLiteral("-f"), QStringLiteral("s16le"), QStringLiteral("-")});
        if (!proc.waitForStarted(5000)) {
            m_error = QStringLiteral("Az ffmpeg nem indítható.");
            return false;
        }
        // Folyamatosan olvassuk a kimenetet (egy órás hang ~115 MB — ne a pipe-pufferben álljon).
        while (proc.state() != QProcess::NotRunning) {
            proc.waitForReadyRead(200);
            m_pcm += proc.readAllStandardOutput();
        }
        m_pcm += proc.readAllStandardOutput();
        if (proc.exitStatus() != QProcess::NormalExit || m_pcm.size() < 2) {
            m_error = QStringLiteral("ffmpeg dekódolás sikertelen: %1").arg(audioPath);
            m_pcm.clear();
            return false;
        }
        return true;
    }

    QVector<float> embed(qint64 startMs, qint64 endMs) override
    {
        if (!m_embedder || m_pcm.isEmpty()) return {};
        const qint64 total = m_pcm.size() / qint64(sizeof(qint16));
        const qint64 from = std::clamp<qint64>(startMs * kSampleRate / 1000, 0, total);
        const qint64 to   = std::clamp<qint64>(endMs * kSampleRate / 1000, from, total);
        if (to - from < kSampleRate / 2) return {};   // fél mp alatt nincs értelmes embedding
        const qint16* src = reinterpret_cast<const qint16*>(m_pcm.constData());
        QVector<float> pcm(int(to - from));
        for (qint64 i = from; i < to; ++i)
            pcm[int(i - from)] = float(src[i]) / 32768.0f;
        return m_embedder->embedPcm(pcm);
    }

    QString lastError() const override { return m_error; }

private:
    QString m_modelPath;
    QString m_ffmpeg;
    QString m_error;
    std::unique_ptr<VoiceEmbedder> m_embedder;
    QByteArray m_pcm;   // s16le mono 16 kHz
};

} // namespace

UtteranceEmbedderFactory voiceUtteranceEmbedderFactory(const QString& modelPath,
                                                       const QString& ffmpegPath)
{
    return [modelPath, ffmpegPath]() -> std::unique_ptr<IUtteranceEmbedder> {
        return std::make_unique<VoiceUtteranceEmbedder>(modelPath, ffmpegPath);
    };
}

QString UtteranceEmbeddingCache::filePath(const QString& meetingFolder)
{
    return QDir(meetingFolder).filePath(QStringLiteral("transcript.embeddings.bin"));
}

UtteranceEmbeddingCache UtteranceEmbeddingCache::load(const QString& meetingFolder,
                                                      const QString& fingerprint)
{
    UtteranceEmbeddingCache cache;
    cache.fingerprint = fingerprint;
    QFile f(filePath(meetingFolder));
    if (!f.open(QIODevice::ReadOnly)) return cache;
    QDataStream in(&f);
    in.setVersion(QDataStream::Qt_6_0);
    in.setFloatingPointPrecision(QDataStream::SinglePrecision);
    quint32 magic = 0, version = 0;
    QString fp;
    in >> magic >> version >> fp;
    if (magic != kCacheMagic || version != kCacheVersion || fp != fingerprint)
        return cache;   // más átirat / ismeretlen formátum → üres cache (újraszámoljuk)
    QHash<QString, QVector<float>> vectors;
    in >> vectors;
    if (in.status() == QDataStream::Ok)
        cache.vectors = vectors;
    return cache;
}

bool UtteranceEmbeddingCache::save(const QString& meetingFolder) const
{
    QSaveFile f(filePath(meetingFolder));
    if (!f.open(QIODevice::WriteOnly)) return false;
    QDataStream out(&f);
    out.setVersion(QDataStream::Qt_6_0);
    out.setFloatingPointPrecision(QDataStream::SinglePrecision);
    out << kCacheMagic << kCacheVersion << fingerprint << vectors;
    return f.commit();
}

void UtteranceEmbeddingCache::remove(const QString& meetingFolder)
{
    QFile::remove(filePath(meetingFolder));
}

} // namespace tanara
