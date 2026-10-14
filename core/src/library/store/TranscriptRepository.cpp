#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/library/MeetingLayout.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
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

// ---- the editor's lines and speaker layer -----------------------------------------------------

QString TranscriptRepository::overlayPath(const QString& meetingFolder)
{
    return MeetingLayout::path(meetingFolder, MeetingLayout::overlayJson());
}

QVector<TranscriptLine> TranscriptRepository::readLines(const QString& meetingFolder)
{
    QVector<TranscriptLine> lines;
    QFile f(segmentsPath(meetingFolder));
    if (!f.open(QIODevice::ReadOnly)) return lines;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    lines.reserve(arr.size());
    QHash<QString, int> seen;   // base id -> how many times it occurred
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        TranscriptLine l;
        l.startMs  = qint64(o.value(QStringLiteral("startMs")).toDouble());
        l.endMs    = qint64(o.value(QStringLiteral("endMs")).toDouble());
        l.rawLabel = o.value(QStringLiteral("speaker")).toString();
        l.text     = o.value(QStringLiteral("text")).toString();
        // The id is formed from the start time: stable until the transcript is regenerated, and insensitive to
        // the file being reordered. The same start time (overlapping speakers) -> a number suffix.
        const QString base = QStringLiteral("u%1").arg(l.startMs);
        const int n = ++seen[base];
        l.id = n == 1 ? base : QStringLiteral("%1-%2").arg(base).arg(n);
        lines.append(l);
    }
    return lines;
}

QString TranscriptRepository::linesFingerprint(const QVector<TranscriptLine>& lines)
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    for (const TranscriptLine& l : lines) {
        h.addData(QByteArray::number(l.startMs));
        h.addData("|");
        h.addData(QByteArray::number(l.endMs));
        h.addData("|");
        h.addData(l.rawLabel.toUtf8());
        h.addData("\n");
    }
    return QStringLiteral("%1:%2").arg(lines.size())
        .arg(QString::fromLatin1(h.result().toHex().left(16)));
}

SpeakerOverlay TranscriptRepository::readOverlay(const QString& meetingFolder)
{
    SpeakerOverlay ov;
    QFile f(overlayPath(meetingFolder));
    if (!f.open(QIODevice::ReadOnly)) return ov;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();

    ov.transcriptFingerprint = root.value(QStringLiteral("transcript")).toString();
    for (const QJsonValue& v : root.value(QStringLiteral("participants")).toArray()) {
        const QJsonObject o = v.toObject();
        OverlayParticipant p;
        p.key        = o.value(QStringLiteral("key")).toString();
        p.personId   = PersonId::fromString(o.value(QStringLiteral("personId")).toString());
        p.label      = o.value(QStringLiteral("label")).toString();
        p.colorIndex = o.value(QStringLiteral("colorIndex")).toInt();
        if (!p.key.isEmpty()) ov.participants.append(p);
    }
    ov.nextParticipant = qMax(1, root.value(QStringLiteral("nextParticipant")).toInt(1));
    ov.nextAnonymous   = qMax(1, root.value(QStringLiteral("nextAnonymous")).toInt(1));

    const QJsonObject merged = root.value(QStringLiteral("merged")).toObject();
    for (auto it = merged.constBegin(); it != merged.constEnd(); ++it)
        ov.merged.insert(it.key(), it.value().toString());
    for (const QJsonValue& v : root.value(QStringLiteral("removed")).toArray())
        ov.removedRaw << v.toString();
    const QJsonObject tracks = root.value(QStringLiteral("speakerTracks")).toObject();
    for (auto it = tracks.constBegin(); it != tracks.constEnd(); ++it) {
        QStringList ids;
        for (const QJsonValue& v : it.value().toArray())
            if (!v.toString().isEmpty()) ids << v.toString();
        if (!ids.isEmpty()) ov.speakerTracks.insert(it.key(), ids);
    }

    const QJsonObject utts = root.value(QStringLiteral("utterances")).toObject();
    for (auto it = utts.constBegin(); it != utts.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        OverlayUtterance u;
        u.speaker   = o.value(QStringLiteral("speaker")).toString();
        u.corrected = o.value(QStringLiteral("corrected")).toBool();
        u.confirmed = o.value(QStringLiteral("confirmed")).toBool();
        if (o.contains(QStringLiteral("noisy"))) u.noisy = o.value(QStringLiteral("noisy")).toBool();
        u.rechecked = o.value(QStringLiteral("rechecked")).toBool();
        u.recheckHint = o.value(QStringLiteral("recheckHint")).toString();
        u.ignored = o.value(QStringLiteral("ignored")).toBool();
        if (!u.isDefault()) ov.utterances.insert(it.key(), u);
    }

    const QJsonObject ident = root.value(QStringLiteral("identified")).toObject();
    for (auto it = ident.constBegin(); it != ident.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        OverlayIdentification id;
        id.personId = PersonId::fromString(o.value(QStringLiteral("personId")).toString());
        if (id.personId.isNull()) continue;
        id.score  = o.value(QStringLiteral("score")).toDouble(-1.0);
        ov.identified.insert(it.key(), id);
    }

    const QJsonObject sum = root.value(QStringLiteral("summary")).toObject();
    for (const QJsonValue& v : sum.value(QStringLiteral("changedSpeakers")).toArray())
        ov.changedSinceSummary << v.toString();
    ov.summaryEpoch = sum.value(QStringLiteral("epoch")).toInt();
    return ov;
}

SpeakerOverlay TranscriptRepository::readOverlayFor(const QString& meetingFolder, const QVector<TranscriptLine>& lines)
{
    SpeakerOverlay ov = readOverlay(meetingFolder);
    const QString fp = linesFingerprint(lines);
    if (!ov.transcriptFingerprint.isEmpty() && ov.transcriptFingerprint != fp) {
        // Corrections belonging to another (earlier) transcript: the utterance boundaries are no longer the same,
        // so they cannot be applied. Identification / the stale flag are transcript-independent.
        ov.participants.clear();
        ov.merged.clear();
        ov.removedRaw.clear();
        ov.utterances.clear();
        ov.speakerTracks.clear();
        ov.nextParticipant = 1;
        ov.nextAnonymous = 1;
    }
    ov.transcriptFingerprint = fp;
    return ov;
}

bool TranscriptRepository::writeOverlay(const QString& meetingFolder, const SpeakerOverlay& ov)
{
    const QString path = overlayPath(meetingFolder);
    if (ov.isEmpty()) {
        // Nothing to store: we do not leave an empty file in the meeting folder.
        return !QFile::exists(path) || QFile::remove(path);
    }

    QJsonObject root;
    root[QStringLiteral("version")] = 3;   // 3: people by PersonId
    root[QStringLiteral("transcript")] = ov.transcriptFingerprint;

    QJsonArray parts;
    for (const OverlayParticipant& p : ov.participants) {
        QJsonObject o;
        o[QStringLiteral("key")] = p.key;
        if (!p.personId.isNull()) o[QStringLiteral("personId")] = p.personId.toString();
        o[QStringLiteral("label")] = p.label;
        o[QStringLiteral("colorIndex")] = p.colorIndex;
        parts.append(o);
    }
    root[QStringLiteral("participants")] = parts;
    root[QStringLiteral("nextParticipant")] = ov.nextParticipant;
    root[QStringLiteral("nextAnonymous")] = ov.nextAnonymous;

    QJsonObject merged;
    for (auto it = ov.merged.constBegin(); it != ov.merged.constEnd(); ++it)
        merged[it.key()] = it.value();
    root[QStringLiteral("merged")] = merged;
    root[QStringLiteral("removed")] = QJsonArray::fromStringList(ov.removedRaw);
    if (!ov.speakerTracks.isEmpty()) {
        QJsonObject tracks;
        for (auto it = ov.speakerTracks.constBegin(); it != ov.speakerTracks.constEnd(); ++it)
            if (!it.value().isEmpty()) tracks[it.key()] = QJsonArray::fromStringList(it.value());
        root[QStringLiteral("speakerTracks")] = tracks;
    }

    QJsonObject utts;
    for (auto it = ov.utterances.constBegin(); it != ov.utterances.constEnd(); ++it) {
        if (it.value().isDefault()) continue;
        QJsonObject o;
        if (!it.value().speaker.isEmpty()) o[QStringLiteral("speaker")] = it.value().speaker;
        if (it.value().corrected) o[QStringLiteral("corrected")] = true;
        if (it.value().confirmed) o[QStringLiteral("confirmed")] = true;
        if (it.value().noisy.has_value()) o[QStringLiteral("noisy")] = *it.value().noisy;
        if (it.value().ignored) o[QStringLiteral("ignored")] = true;
        if (it.value().rechecked) {
            o[QStringLiteral("rechecked")] = true;
            if (!it.value().recheckHint.isEmpty())
                o[QStringLiteral("recheckHint")] = it.value().recheckHint;
        }
        utts[it.key()] = o;
    }
    root[QStringLiteral("utterances")] = utts;

    QJsonObject ident;
    for (auto it = ov.identified.constBegin(); it != ov.identified.constEnd(); ++it) {
        QJsonObject o;
        o[QStringLiteral("personId")] = it.value().personId.toString();
        o[QStringLiteral("score")] = it.value().score;
        ident[it.key()] = o;
    }
    root[QStringLiteral("identified")] = ident;

    QJsonObject sum;
    sum[QStringLiteral("changedSpeakers")] = QJsonArray::fromStringList(ov.changedSinceSummary);
    sum[QStringLiteral("epoch")] = ov.summaryEpoch;
    root[QStringLiteral("summary")] = sum;

    return writeBytes(path, QJsonDocument(root).toJson(QJsonDocument::Indented));
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
