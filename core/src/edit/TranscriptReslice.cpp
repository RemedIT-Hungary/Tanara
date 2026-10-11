#include "tanara/edit/TranscriptReslice.h"

#include "tanara/edit/UtteranceEmbeddings.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/stt/SonioxTiming.h"
#include "tanara/summary/SummarySources.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QMap>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <limits>

namespace tanara::reslice {

namespace {

const QString kTokens   = QStringLiteral("transcript.tokens.json");
const QString kSegments = QStringLiteral("transcript.segments.json");
const QString kSpeakers = QStringLiteral("transcript.speakers.json");
const QString kSummary  = QStringLiteral("summary.json");
const QString kMarkdown = QStringLiteral("transcript.md");

QString pathIn(const QString& folder, const QString& name) { return QDir(folder).filePath(name); }

bool readJson(const QString& path, QJsonDocument* doc)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError pe;
    *doc = QJsonDocument::fromJson(f.readAll(), &pe);
    return pe.error == QJsonParseError::NoError;
}

bool writeBytes(const QString& path, const QByteArray& data)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(data);
    return f.commit();
}

QVector<TranscriptToken> tokensFromJson(const QJsonArray& arr)
{
    QVector<TranscriptToken> out;
    out.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        TranscriptToken t;
        t.text       = o.value(QStringLiteral("text")).toString();
        t.speaker    = o.value(QStringLiteral("speaker")).toString();
        t.startMs    = qint64(o.value(QStringLiteral("startMs")).toDouble());
        t.endMs      = qint64(o.value(QStringLiteral("endMs")).toDouble());
        t.confidence = o.value(QStringLiteral("confidence")).toDouble();
        t.trackId    = o.value(QStringLiteral("trackId")).toString();
        out.append(t);
    }
    return out;
}

// Ugyanaz a formátum, amit az AppController ír (writeSegmentsJson).
QByteArray segmentsJson(const QVector<Utterance>& segs)
{
    QJsonArray arr;
    for (const Utterance& u : segs) {
        QJsonObject o;
        o[QStringLiteral("startMs")] = double(u.startMs);
        o[QStringLiteral("endMs")]   = double(u.endMs);
        o[QStringLiteral("speaker")] = u.speaker;
        o[QStringLiteral("text")]    = u.text;
        arr.append(o);
    }
    return QJsonDocument(arr).toJson(QJsonDocument::Indented);
}

QVector<TranscriptLine> toLines(const QVector<Utterance>& segs)
{
    const QVector<summarysrc::SourceLine> src = summarysrc::linesFromSegments(segs);
    QVector<TranscriptLine> out;
    out.reserve(segs.size());
    for (int i = 0; i < segs.size(); ++i) {
        TranscriptLine l;
        l.id = src.at(i).id;
        l.startMs = segs.at(i).startMs;
        l.endMs = segs.at(i).endMs;
        l.rawLabel = segs.at(i).speaker;
        l.text = segs.at(i).text;
        out.append(l);
    }
    return out;
}

QString shiftBucket(qint64 shift)
{
    if (shift <= 0) return QStringLiteral("0");
    if (shift <= 100) return QStringLiteral("1-100");
    if (shift <= 200) return QStringLiteral("101-200");
    if (shift <= 300) return QStringLiteral("201-300");
    return QStringLiteral(">300");
}

// Id-átnevezés egy JSON-fán: az objektum-kulcsok és a szöveg-értékek közül azok, amelyek a
// térképben vannak. A térkép 1:1 (injektív), így kulcs-ütközés nem lehet.
struct Remapper {
    const QMap<QString, QString>& map;
    int refs = 0;
    int renamed = 0;

    QString id(const QString& s)
    {
        const auto it = map.constFind(s);
        if (it == map.constEnd()) return s;
        ++refs;
        if (it.value() != it.key()) ++renamed;
        return it.value();
    }

    QJsonValue value(const QJsonValue& v)
    {
        if (v.isString()) return id(v.toString());
        if (v.isArray()) {
            QJsonArray out;
            for (const QJsonValue& e : v.toArray()) out.append(value(e));
            return out;
        }
        if (v.isObject()) return object(v.toObject());
        return v;
    }

    QJsonObject object(const QJsonObject& o)
    {
        QJsonObject out;
        for (auto it = o.constBegin(); it != o.constEnd(); ++it) out[id(it.key())] = value(it.value());
        return out;
    }
};

// A summary.json forrás-tartományainak ideje az új sorokból (az id-k már átnevezve).
void refreshSpans(QJsonValue& node, const QHash<QString, const TranscriptLine*>& byId)
{
    if (node.isArray()) {
        QJsonArray arr = node.toArray();
        for (int i = 0; i < arr.size(); ++i) {
            QJsonValue e = arr.at(i);
            refreshSpans(e, byId);
            arr[i] = e;
        }
        node = arr;
        return;
    }
    if (!node.isObject()) return;
    QJsonObject o = node.toObject();
    if (o.contains(QStringLiteral("utteranceIds")) && o.value(QStringLiteral("utteranceIds")).isArray()) {
        QStringList ids;
        for (const QJsonValue& v : o.value(QStringLiteral("utteranceIds")).toArray())
            if (!ids.contains(v.toString())) ids << v.toString();
        qint64 s = std::numeric_limits<qint64>::max(), e = -1;
        for (const QString& id : ids)
            if (const TranscriptLine* l = byId.value(id)) { s = std::min(s, l->startMs); e = std::max(e, l->endMs); }
        o[QStringLiteral("utteranceIds")] = QJsonArray::fromStringList(ids);
        if (e >= 0) {
            o[QStringLiteral("startMs")] = double(s);
            o[QStringLiteral("endMs")] = double(e);
        }
    }
    for (auto it = o.begin(); it != o.end(); ++it) {
        QJsonValue child = it.value();
        if (child.isObject() || child.isArray()) {
            refreshSpans(child, byId);
            it.value() = child;
        }
    }
    node = o;
}


} // namespace

Plan plan(const QString& folder)
{
    Plan p;
    QJsonDocument tokDoc;
    if (!readJson(pathIn(folder, kTokens), &tokDoc) || !tokDoc.isObject()) {
        p.error = QStringLiteral("missing or unreadable %1").arg(kTokens);
        return p;
    }
    const QJsonObject root = tokDoc.object();
    MergedTranscript raw;
    raw.language = root.value(QStringLiteral("language")).toString();
    raw.tokens = tokensFromJson(root.value(QStringLiteral("tokens")).toArray());
    p.tokenCount = int(raw.tokens.size());
    if (raw.tokens.isEmpty()) { p.error = QStringLiteral("%1 has no tokens").arg(kTokens); return p; }
    p.medianWidthMs = sonioxtiming::medianTokenWidthMs(raw.tokens);
    if (root.value(timingKey()).toString() == emissionTimingValue()) {
        p.verdict = Verdict::AlreadyPadded;
        return p;
    }
    if (!sonioxtiming::looksLikeEmissionTiming(raw.tokens)) {
        p.verdict = Verdict::RealWordSpans;
        return p;
    }

    // A csoportosítás az EREDETI token-időkkel (ugyanaz a merger), csak a kész sorok párnázódnak
    // → a sorok száma és sorrendje nem változik, a régi → új térkép index szerinti 1:1.
    MergedTranscript padded = raw;
    padded.emissionTiming = true;
    p.newSegments = padded.segments();
    p.newLines = toLines(p.newSegments);
    p.oldLines = speakeredit::loadTranscriptLines(folder);
    if (p.oldLines.isEmpty()) p.oldLines = toLines(raw.segments());
    if (p.newLines.isEmpty()) { p.error = QStringLiteral("re-slicing produced no lines"); return p; }
    if (p.oldLines.size() != p.newLines.size()) {
        p.error = QStringLiteral("%1 has %2 lines but %3 yields %4; not touching it")
                      .arg(kSegments).arg(p.oldLines.size()).arg(kTokens).arg(p.newLines.size());
        return p;
    }
    for (int i = 0; i < p.oldLines.size(); ++i) {
        const TranscriptLine& o = p.oldLines.at(i);
        const TranscriptLine& n = p.newLines.at(i);
        if (o.rawLabel != n.rawLabel || n.startMs > o.startMs || n.endMs < o.endMs) {
            p.error = QStringLiteral("line %1 does not match %2 (line %3); not touching it")
                          .arg(o.id, kTokens).arg(i + 1);
            return p;
        }
        p.idMap.insert(o.id, n.id);
        if (n.id != o.id) ++p.renamedIds;
        if (o.text != n.text) ++p.textChanged;
        p.shiftHistogram[shiftBucket(o.startMs - n.startMs)]++;
    }

    QJsonDocument ov;
    p.overlayPresent = readJson(pathIn(folder, kSpeakers), &ov) && ov.isObject();
    if (p.overlayPresent) {
        const QString fp = ov.object().value(QStringLiteral("transcript")).toString();
        p.overlayStale = !fp.isEmpty() && fp != speakeredit::transcriptFingerprint(p.oldLines);
        if (!p.overlayStale) {
            Remapper r{p.idMap};
            r.object(ov.object());
            p.overlayRefs = r.refs;
            p.overlayRenamed = r.renamed;
        }
    }
    QJsonDocument sum;
    p.summaryPresent = readJson(pathIn(folder, kSummary), &sum) && (sum.isObject() || sum.isArray());
    if (p.summaryPresent) {
        Remapper r{p.idMap};
        r.value(sum.isObject() ? QJsonValue(sum.object()) : QJsonValue(sum.array()));
        p.summaryRefs = r.refs;
        p.summaryRenamed = r.renamed;
    }
    p.embeddingsPresent = QFile::exists(UtteranceEmbeddingCache::filePath(folder));
    p.verdict = Verdict::Reslice;
    return p;
}

ApplyResult apply(const QString& folder, const Plan& p)
{
    ApplyResult res;
    if (p.verdict != Verdict::Reslice) { res.error = QStringLiteral("nothing to apply"); return res; }

    // 1) biztonsági másolatok — egyszer: a meglévő .bak-ot nem írjuk felül.
    for (const QString& name : {kTokens, kSegments, kSpeakers, kSummary, kMarkdown}) {
        const QString src = pathIn(folder, name);
        const QString bak = src + QStringLiteral(".bak");
        if (!QFile::exists(src) || QFile::exists(bak)) continue;
        if (!QFile::copy(src, bak)) { res.error = QStringLiteral("cannot write backup %1").arg(bak); return res; }
        res.backups << bak;
    }

    // 2) tokens.json: a tokenek ÉRINTETLENEK, csak a "timing": "emission" jelző kerül rá (ettől
    //    párnáz a merger mindenhol, ahol az app a tokenekből szeletel).
    QJsonDocument tokDoc;
    readJson(pathIn(folder, kTokens), &tokDoc);
    QJsonObject root = tokDoc.object();
    root[timingKey()] = emissionTimingValue();
    if (!writeBytes(pathIn(folder, kTokens), QJsonDocument(root).toJson(QJsonDocument::Compact))) {
        res.error = QStringLiteral("cannot write %1").arg(kTokens);
        return res;
    }
    res.written << kTokens;

    // 3) segments.json (nyers címkékkel, mint az átíráskor; párnázott idők).
    if (!writeBytes(pathIn(folder, kSegments), segmentsJson(p.newSegments))) {
        res.error = QStringLiteral("cannot write %1").arg(kSegments);
        return res;
    }
    res.written << kSegments;


    // 4) overlay: minden id-hivatkozás + az ujjlenyomat (ha az overlay a mostani átiraté volt).
    QJsonDocument ov;
    if (p.overlayPresent && !p.overlayStale && readJson(pathIn(folder, kSpeakers), &ov) && ov.isObject()) {
        Remapper r{p.idMap};
        QJsonObject o = r.object(ov.object());
        if (o.contains(QStringLiteral("transcript")))
            o[QStringLiteral("transcript")] = speakeredit::transcriptFingerprint(p.newLines);
        if (!writeBytes(pathIn(folder, kSpeakers), QJsonDocument(o).toJson(QJsonDocument::Indented))) {
            res.error = QStringLiteral("cannot write %1").arg(kSpeakers);
            return res;
        }
        res.written << kSpeakers;
    }

    // 5) summary.json: id-k + a forrás-tartományok ideje.
    QJsonDocument sum;
    if (p.summaryPresent && readJson(pathIn(folder, kSummary), &sum)) {
        Remapper r{p.idMap};
        QJsonValue v = r.value(sum.isObject() ? QJsonValue(sum.object()) : QJsonValue(sum.array()));
        QHash<QString, const TranscriptLine*> byId;
        for (const TranscriptLine& l : p.newLines) byId.insert(l.id, &l);
        refreshSpans(v, byId);
        const QJsonDocument out = v.isObject() ? QJsonDocument(v.toObject()) : QJsonDocument(v.toArray());
        if (!writeBytes(pathIn(folder, kSummary), out.toJson(QJsonDocument::Indented))) {
            res.error = QStringLiteral("cannot write %1").arg(kSummary);
            return res;
        }
        res.written << kSummary;
    }

    // 6) a megszólalás-embedding cache az átirat ujjlenyomatához kötött → törlés.
    if (QFile::exists(UtteranceEmbeddingCache::filePath(folder))) {
        UtteranceEmbeddingCache::remove(folder);
        res.embeddingsRemoved = !QFile::exists(UtteranceEmbeddingCache::filePath(folder));
    }

    // 7) transcript.md (időbélyegek) a feloldott nevekkel, ha volt.
    if (QFile::exists(pathIn(folder, kMarkdown))) {
        Meeting m = MeetingStore::readMeetingFolder(folder);
        if (!m.id.isEmpty()) {
            m.folder = folder;
            res.markdownRegenerated = speakeredit::regenerateTranscriptMarkdown(m);
            if (res.markdownRegenerated) res.written << kMarkdown;
        }
    }
    res.ok = true;
    return res;
}

} // namespace tanara::reslice
