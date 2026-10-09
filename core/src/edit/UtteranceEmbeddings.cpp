#include "tanara/edit/UtteranceEmbeddings.h"

#include "tanara/voiceid/VoiceEmbedder.h"
#include "tanara/voiceid/VoiceModelRegistry.h"

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
constexpr quint32 kCacheVersion = 2;   // v2: modellenkénti vektorok (v1 → eldobva)
constexpr int kSampleRate = 16000;

// Egy meeting hangjára több modell: a teljes hang 16 kHz mono s16-ként a memóriában
// (~115 MB / óra, EGY ffmpeg-futás), a megszólalások ebből szeletelődnek, és minden betöltött
// modell ugyanebből a szeletből számol.
class VoiceUtteranceEmbedder final : public IUtteranceEmbedder {
public:
    VoiceUtteranceEmbedder(const QVector<UtteranceModel>& models, const QString& ffmpegPath)
        : m_models(models), m_ffmpeg(ffmpegPath) {}

    bool open(const QString& audioPath) override
    {
        m_pcm.clear();
        if (!QFileInfo::exists(audioPath)) {
            m_error = QStringLiteral("Hiányzik a hangfájl: %1").arg(audioPath);
            return false;
        }
        if (!loadModels()) return false;

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
        const QString id = VoiceModelRegistry::defaultModelId();
        return embedAll(startMs, endMs, {id}).value(id);
    }

    EmbeddingSet embedAll(qint64 startMs, qint64 endMs, const QStringList& modelIds) override
    {
        EmbeddingSet out;
        if (m_pcm.isEmpty()) return out;
        QVector<float> pcm;
        bool sliced = false;
        for (auto it = m_embedders.cbegin(); it != m_embedders.cend(); ++it) {
            if (!modelIds.contains(it.key())) continue;
            if (!sliced) { pcm = slice(startMs, endMs); sliced = true; }
            out.insert(it.key(), pcm.isEmpty() ? QVector<float>() : it.value()->embedPcm(pcm));
        }
        return out;
    }

    QString lastError() const override { return m_error; }

private:
    // A modellek betöltése (egyszer). Legalább egynek sikerülnie kell.
    bool loadModels()
    {
        if (!m_embedders.isEmpty()) return true;
        QStringList errors;
        for (const UtteranceModel& m : m_models) {
            if (!QFileInfo::exists(m.path)) {
                errors << QStringLiteral("Hiányzik a hangmodell: %1").arg(m.path);
                continue;
            }
            auto e = std::make_shared<VoiceEmbedder>(m.path, m.features);
            if (!e->isValid()) {
                errors << e->lastError();
                continue;
            }
            m_embedders.insert(m.id, e);
        }
        if (m_embedders.isEmpty()) {
            m_error = errors.isEmpty() ? QStringLiteral("Nincs hangmodell.") : errors.join(QLatin1Char('\n'));
            return false;
        }
        return true;
    }

    // A [startMs,endMs] szelet float PCM-ként; fél mp alatt üres (nincs értelmes embedding).
    QVector<float> slice(qint64 startMs, qint64 endMs) const
    {
        const qint64 total = m_pcm.size() / qint64(sizeof(qint16));
        const qint64 from = std::clamp<qint64>(startMs * kSampleRate / 1000, 0, total);
        const qint64 to   = std::clamp<qint64>(endMs * kSampleRate / 1000, from, total);
        if (to - from < kSampleRate / 2) return {};
        const qint16* src = reinterpret_cast<const qint16*>(m_pcm.constData());
        QVector<float> pcm(int(to - from));
        for (qint64 i = from; i < to; ++i)
            pcm[int(i - from)] = float(src[i]) / 32768.0f;
        return pcm;
    }

    QVector<UtteranceModel> m_models;
    QString m_ffmpeg;
    QString m_error;
    QMap<QString, std::shared_ptr<VoiceEmbedder>> m_embedders;   // modelId → betöltött modell
    QByteArray m_pcm;   // s16le mono 16 kHz
};

} // namespace

EmbeddingSet IUtteranceEmbedder::embedAll(qint64 startMs, qint64 endMs, const QStringList& modelIds)
{
    EmbeddingSet out;
    const QString id = VoiceModelRegistry::defaultModelId();
    if (modelIds.contains(id)) out.insert(id, embed(startMs, endMs));
    return out;
}

UtteranceEmbedderFactory voiceUtteranceEmbedderFactory(const QVector<UtteranceModel>& models,
                                                       const QString& ffmpegPath)
{
    return [models, ffmpegPath]() -> std::unique_ptr<IUtteranceEmbedder> {
        return std::make_unique<VoiceUtteranceEmbedder>(models, ffmpegPath);
    };
}

UtteranceEmbedderFactory voiceUtteranceEmbedderFactory(const QString& modelPath,
                                                       const QString& ffmpegPath)
{
    const auto spec = VoiceModelRegistry::spec(VoiceModelRegistry::defaultModelId());
    return voiceUtteranceEmbedderFactory(
        QVector<UtteranceModel>{{spec->id, modelPath, spec->features}}, ffmpegPath);
}

bool UtteranceEmbeddingCache::has(const QString& modelId, const QString& utteranceId) const
{
    const auto it = models.constFind(modelId);
    return it != models.constEnd() && it->contains(utteranceId);
}

EmbeddingSet UtteranceEmbeddingCache::setFor(const QString& utteranceId, const QStringList& modelIds) const
{
    EmbeddingSet out;
    for (const QString& m : modelIds) {
        const auto it = models.constFind(m);
        if (it == models.constEnd()) continue;
        const auto v = it->constFind(utteranceId);
        if (v != it->constEnd() && !v->isEmpty()) out.insert(m, v.value());
    }
    return out;
}

QStringList UtteranceEmbeddingCache::missingFor(const QString& modelId, const QStringList& utteranceIds) const
{
    QStringList out;
    const auto it = models.constFind(modelId);
    for (const QString& id : utteranceIds)
        if (it == models.constEnd() || !it->contains(id)) out << id;
    return out;
}

bool UtteranceEmbeddingCache::isEmpty() const
{
    for (const auto& h : models)
        if (!h.isEmpty()) return false;
    return true;
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
        return cache;   // más átirat / régi (v1) vagy ismeretlen formátum → üres cache (újraszámoljuk)
    QMap<QString, QHash<QString, QVector<float>>> models;
    in >> models;
    if (in.status() == QDataStream::Ok)
        cache.models = models;
    return cache;
}

bool UtteranceEmbeddingCache::save(const QString& meetingFolder) const
{
    QSaveFile f(filePath(meetingFolder));
    if (!f.open(QIODevice::WriteOnly)) return false;
    QDataStream out(&f);
    out.setVersion(QDataStream::Qt_6_0);
    out.setFloatingPointPrecision(QDataStream::SinglePrecision);
    out << kCacheMagic << kCacheVersion << fingerprint << models;
    return f.commit();
}

void UtteranceEmbeddingCache::remove(const QString& meetingFolder)
{
    QFile::remove(filePath(meetingFolder));
}

} // namespace tanara
