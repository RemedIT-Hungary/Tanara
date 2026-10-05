#include "tanara/embedding/EmbeddingIndex.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/SharedFile.h"
#include "tanara/tags/MeetingProfiles.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace tanara {

namespace {

const QByteArray kMagic = QByteArrayLiteral("TNEMB1");

QString segmentsPath(const QString& folder)
{
    return QDir(folder).filePath(QStringLiteral("transcript.segments.json"));
}

void normalizeVec(QVector<float>& v)
{
    double sq = 0;
    for (float x : std::as_const(v)) sq += double(x) * double(x);
    const double n = std::sqrt(sq);
    if (n <= 0) return;
    for (float& x : v) x = float(x / n);
}

} // namespace

const QString EmbeddingIndex::kFileName = QStringLiteral("text.embeddings.bin");

EmbeddingIndex::EmbeddingIndex(MeetingStore* store, QObject* parent)
    : QObject(parent), m_store(store)
{
    if (store) {
        connect(store, &MeetingStore::meetingRemoved, this, [this](const QString& id) {
            m_cache.remove(id);
            m_folders.remove(id);
        });
    }
}

EmbeddingIndex::~EmbeddingIndex() = default;

void EmbeddingIndex::setProfiles(MeetingProfiles* profiles) { m_profiles = profiles; }

void EmbeddingIndex::setModel(const QString& model)
{
    if (m_model == model) return;
    m_model = model;
    emit indexReset();
}

QString EmbeddingIndex::model() const { return m_model; }

QString EmbeddingIndex::folderOf(const QString& meetingId) const
{
    const auto it = m_folders.constFind(meetingId);
    if (it != m_folders.constEnd()) return *it;
    const QString folder = m_store ? m_store->load(meetingId).folder : QString();
    if (!folder.isEmpty()) m_folders.insert(meetingId, folder);
    return folder;
}

const EmbeddingIndex::Entry* EmbeddingIndex::entry(const QString& meetingId) const
{
    if (m_model.isEmpty()) return nullptr;
    const QString folder = folderOf(meetingId);
    if (folder.isEmpty()) return nullptr;
    const QString path = QDir(folder).filePath(kFileName);
    const FileStamp fs = FileStamp::of(path);
    if (fs.mtimeMs < 0) { m_cache.remove(meetingId); return nullptr; }
    // Régebbi, mint az átirat → elavult.
    const FileStamp ts = FileStamp::of(segmentsPath(folder));
    if (ts.mtimeMs > fs.mtimeMs) return nullptr;
    auto it = m_cache.find(meetingId);
    if (it == m_cache.end() || it->fileMtime != fs.mtimeMs) {
        Entry e;
        e.fileMtime = fs.mtimeMs;
        QVector<EmbeddingChunk> chunks;
        if (!readFile(path, &e.model, &chunks)) return nullptr;
        e.mean = meanVector(chunks);
        it = m_cache.insert(meetingId, e);
    }
    if (it->model != m_model || it->mean.isEmpty()) return nullptr;
    return &*it;
}

bool EmbeddingIndex::has(const QString& meetingId) const { return entry(meetingId) != nullptr; }

QVector<SimilarHit> EmbeddingIndex::similar(const QString& meetingId, int limit) const
{
    QVector<SimilarHit> hits;
    const Entry* self = entry(meetingId);
    if (!self || !m_store) return hits;
    const QVector<float> a = self->mean;
    for (const Meeting& m : m_store->loadAll()) {
        if (m.id == meetingId) continue;
        if (!m.folder.isEmpty()) m_folders.insert(m.id, m.folder);
        const Entry* o = entry(m.id);
        if (!o || o->mean.size() != a.size()) continue;
        double dot = 0;
        for (int i = 0; i < a.size(); ++i) dot += double(a.at(i)) * double(o->mean.at(i));
        if (dot <= 0) continue;
        SimilarHit h;
        h.meetingId = m.id;
        h.score = dot;
        if (m_profiles) {
            const QStringList terms = m_profiles->sharedTerms(meetingId, m.id, 3);
            if (!terms.isEmpty()) h.reasons.append({ ReasonKind::Terms, terms });
        }
        hits.append(h);
    }
    std::sort(hits.begin(), hits.end(), [](const SimilarHit& x, const SimilarHit& y) {
        if (x.score != y.score) return x.score > y.score;
        return x.meetingId < y.meetingId;
    });
    if (limit >= 0 && hits.size() > limit) hits.resize(limit);
    return hits;
}

bool EmbeddingIndex::store(const QString& meetingId, const QString& model,
                           const QVector<EmbeddingChunk>& chunks)
{
    const QString folder = folderOf(meetingId);
    if (folder.isEmpty()) return false;
    if (!writeFile(QDir(folder).filePath(kFileName), model, chunks)) return false;
    m_cache.remove(meetingId);
    emit indexChanged(meetingId);
    return true;
}

void EmbeddingIndex::invalidateAll()
{
    if (m_store)
        for (const Meeting& m : m_store->loadAll())
            if (!m.folder.isEmpty()) QFile::remove(QDir(m.folder).filePath(kFileName));
    m_cache.clear();
    m_folders.clear();
    emit indexReset();
}

void EmbeddingIndex::invalidate(const QString& meetingId)
{
    const QString folder = folderOf(meetingId);
    if (!folder.isEmpty()) QFile::remove(QDir(folder).filePath(kFileName));
    m_cache.remove(meetingId);
    emit indexChanged(meetingId);
}

QVector<EmbeddingChunk> EmbeddingIndex::chunkTranscript(const QVector<TranscriptLine>& lines, int maxChars)
{
    QVector<EmbeddingChunk> out;
    EmbeddingChunk cur;
    bool open = false;
    for (const TranscriptLine& l : lines) {
        const QString t = l.text.simplified();
        if (t.isEmpty()) continue;
        // Megszólalás-határon zárunk, ha a következő már túllógna (egy túl hosszú megszólalás
        // egymagában is egy darab).
        if (open && cur.text.size() + 1 + t.size() > maxChars) {
            out.append(cur);
            cur = EmbeddingChunk{};
            open = false;
        }
        if (!open) { cur.startMs = l.startMs; open = true; }
        else cur.text += QLatin1Char('\n');
        cur.text += t;
        cur.endMs = std::max(cur.endMs, l.endMs);
    }
    if (open) out.append(cur);
    return out;
}

bool EmbeddingIndex::writeFile(const QString& path, const QString& model, const QVector<EmbeddingChunk>& chunks)
{
    if (chunks.isEmpty()) return false;
    const int dim = int(chunks.first().vector.size());
    if (dim <= 0) return false;
    for (const EmbeddingChunk& c : chunks)
        if (c.vector.size() != dim) return false;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(kMagic);
    QDataStream ds(&f);
    ds.setVersion(QDataStream::Qt_6_0);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setFloatingPointPrecision(QDataStream::SinglePrecision);
    ds << model << qint32(dim) << qint32(chunks.size());
    for (const EmbeddingChunk& c : chunks) {
        ds << qint64(c.startMs) << qint64(c.endMs);
        for (float x : c.vector) ds << x;
    }
    return ds.status() == QDataStream::Ok && f.commit();
}

bool EmbeddingIndex::readFile(const QString& path, QString* model, QVector<EmbeddingChunk>* chunks)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    if (f.read(kMagic.size()) != kMagic) return false;
    QDataStream ds(&f);
    ds.setVersion(QDataStream::Qt_6_0);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setFloatingPointPrecision(QDataStream::SinglePrecision);
    QString m;
    qint32 dim = 0, count = 0;
    ds >> m >> dim >> count;
    if (ds.status() != QDataStream::Ok || dim <= 0 || count < 0 || dim > 65536) return false;
    // A fájl mérete alapján is ellenőrzünk (sérült fejléc ne foglaljon sok memóriát).
    const qint64 need = qint64(count) * (16 + qint64(dim) * 4);
    if (f.size() - f.pos() < need) return false;
    QVector<EmbeddingChunk> out;
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        EmbeddingChunk c;
        qint64 s = 0, e = 0;
        ds >> s >> e;
        c.startMs = s; c.endMs = e;
        c.vector.resize(dim);
        for (int k = 0; k < dim; ++k) ds >> c.vector[k];
        out.append(c);
    }
    if (ds.status() != QDataStream::Ok) return false;
    if (model) *model = m;
    if (chunks) *chunks = out;
    return true;
}

QVector<float> EmbeddingIndex::meanVector(const QVector<EmbeddingChunk>& chunks)
{
    if (chunks.isEmpty()) return {};
    const int dim = int(chunks.first().vector.size());
    QVector<double> acc(dim, 0.0);
    for (const EmbeddingChunk& c : chunks) {
        if (c.vector.size() != dim) continue;
        QVector<float> v = c.vector;
        normalizeVec(v);   // a darabok egyenlő súllyal
        for (int i = 0; i < dim; ++i) acc[i] += v.at(i);
    }
    QVector<float> mean(dim);
    for (int i = 0; i < dim; ++i) mean[i] = float(acc.at(i) / chunks.size());
    normalizeVec(mean);
    return mean;
}

} // namespace tanara
