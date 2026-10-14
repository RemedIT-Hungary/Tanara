#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/library/MeetingLayout.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSet>

namespace tanara::library {

namespace {

// Always LF line endings (WITHOUT QIODevice::Text): on Windows the Text mode would write CRLF, but
// the readback reads raw, so a \r would end up in the text.
bool writeBytes(const QString& path, const QByteArray& data)
{
    if (path.isEmpty()) return false;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(data);
    return f.commit();
}

QJsonArray segmentsArray(const QByteArray& data)
{
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    return doc.isArray() ? doc.array() : doc.object().value(QStringLiteral("segments")).toArray();
}

} // namespace

QString TranscriptRepository::tokensPath(const QString& meetingFolder)
{
    return MeetingLayout::path(meetingFolder, MeetingLayout::tokensJson());
}

QString TranscriptRepository::segmentsPath(const QString& meetingFolder)
{
    return MeetingLayout::path(meetingFolder, MeetingLayout::segmentsJson());
}

QString TranscriptRepository::markdownPath(const QString& meetingFolder)
{
    return MeetingLayout::path(meetingFolder, MeetingLayout::transcriptMd());
}

bool TranscriptRepository::hasTokens(const QString& meetingFolder)
{
    return !meetingFolder.isEmpty() && QFile::exists(tokensPath(meetingFolder));
}

MergedTranscript TranscriptRepository::readTokens(const QString& meetingFolder)
{
    MergedTranscript mt;
    if (meetingFolder.isEmpty()) return mt;
    QFile f(tokensPath(meetingFolder));
    if (!f.open(QIODevice::ReadOnly)) return mt;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    mt.language = root.value(QStringLiteral("language")).toString();
    mt.emissionTiming = root.value(QStringLiteral("timing")).toString() == QLatin1String("emission");
    const QJsonArray arr = root.value(QStringLiteral("tokens")).toArray();
    mt.tokens.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        TranscriptToken t;
        t.text       = o.value(QStringLiteral("text")).toString();
        t.speaker    = o.value(QStringLiteral("speaker")).toString();
        t.startMs    = qint64(o.value(QStringLiteral("startMs")).toDouble());
        t.endMs      = qint64(o.value(QStringLiteral("endMs")).toDouble());
        t.confidence = o.value(QStringLiteral("confidence")).toDouble();
        t.trackId    = o.value(QStringLiteral("trackId")).toString();
        mt.tokens.append(t);
    }
    return mt;
}

bool TranscriptRepository::writeTokens(const QString& meetingFolder, const MergedTranscript& mt)
{
    if (meetingFolder.isEmpty()) return false;
    QJsonArray arr;
    for (const auto& t : mt.tokens) {
        QJsonObject o;
        o[QStringLiteral("text")] = t.text;
        o[QStringLiteral("speaker")] = t.speaker;
        o[QStringLiteral("startMs")] = double(t.startMs);
        o[QStringLiteral("endMs")] = double(t.endMs);
        o[QStringLiteral("confidence")] = t.confidence;
        o[QStringLiteral("trackId")] = t.trackId;
        arr.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("language")] = mt.language;
    if (mt.emissionTiming) root[QStringLiteral("timing")] = QStringLiteral("emission");   // the lines need padding (Soniox)
    root[QStringLiteral("tokens")] = arr;
    return writeBytes(tokensPath(meetingFolder), QJsonDocument(root).toJson(QJsonDocument::Compact));
}

QVector<Utterance> TranscriptRepository::readSegments(const QString& meetingFolder)
{
    QVector<Utterance> out;
    if (meetingFolder.isEmpty()) return out;
    QFile f(segmentsPath(meetingFolder));
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QJsonArray arr = segmentsArray(f.readAll());
    out.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        Utterance u;
        u.startMs = qint64(o.value(QStringLiteral("startMs")).toDouble());
        u.endMs   = qint64(o.value(QStringLiteral("endMs")).toDouble());
        u.speaker = o.value(QStringLiteral("speaker")).toString();
        u.text    = o.value(QStringLiteral("text")).toString();
        out.append(u);
    }
    return out;
}

bool TranscriptRepository::writeSegments(const QString& meetingFolder, const QVector<Utterance>& segments)
{
    if (meetingFolder.isEmpty()) return false;
    QJsonArray arr;
    for (const auto& u : segments) {
        QJsonObject o;
        o[QStringLiteral("startMs")] = double(u.startMs);
        o[QStringLiteral("endMs")]   = double(u.endMs);
        o[QStringLiteral("speaker")] = u.speaker;
        o[QStringLiteral("text")]    = u.text;
        arr.append(o);
    }
    return writeBytes(segmentsPath(meetingFolder), QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

QString TranscriptRepository::readMarkdown(const QString& meetingFolder, qint64 maxBytes)
{
    if (meetingFolder.isEmpty()) return QString();
    QFile f(markdownPath(meetingFolder));
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(maxBytes < 0 ? f.readAll() : f.read(maxBytes));
}

bool TranscriptRepository::writeMarkdown(const QString& meetingFolder, const QString& markdown)
{
    if (meetingFolder.isEmpty()) return false;
    return writeBytes(markdownPath(meetingFolder), markdown.toUtf8());
}

int TranscriptRepository::speakerCount(const QString& meetingFolder) const
{
    if (meetingFolder.isEmpty()) return 0;
    const QFileInfo fi(segmentsPath(meetingFolder));
    if (!fi.exists()) return 0;
    // Identified by path + modification time + size: a rewritten file is re-read even without an
    // explicit invalidate.
    const QString key = QString::number(fi.lastModified().toMSecsSinceEpoch()) + QLatin1Char('|')
                        + QString::number(fi.size());
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_counts.constFind(meetingFolder);
        if (it != m_counts.constEnd() && it->key == key) return it->count;
    }
    QFile f(fi.absoluteFilePath());
    if (!f.open(QIODevice::ReadOnly)) return 0;
    QSet<QString> speakers;
    for (const QJsonValue& v : segmentsArray(f.readAll())) {
        const QString s = v.toObject().value(QStringLiteral("speaker")).toString();
        if (!s.isEmpty()) speakers.insert(s);
    }
    const int count = int(speakers.size());
    QMutexLocker lock(&m_mutex);
    m_counts.insert(meetingFolder, CountEntry{key, count});
    return count;
}

void TranscriptRepository::invalidate(const QString& meetingFolder)
{
    QMutexLocker lock(&m_mutex);
    if (meetingFolder.isEmpty()) m_counts.clear();
    else m_counts.remove(meetingFolder);
}

} // namespace tanara::library
