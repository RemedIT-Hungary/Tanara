#include "tanara/edit/TranscriptReslice.h"

#include "tanara/TranscriptMerger.h"
#include "tanara/edit/UtteranceEmbeddings.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/stt/SonioxTiming.h"
#include "tanara/summary/SummarySources.h"

#include <QDir>
#include <QFile>
#include <QHash>
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

// Ugyanaz a formátum, amit az AppController ír (writeTokensJson).
QJsonArray tokensToJson(const QVector<TranscriptToken>& tokens)
{
    QJsonArray arr;
    for (const TranscriptToken& t : tokens) {
        QJsonObject o;
        o[QStringLiteral("text")] = t.text;
        o[QStringLiteral("speaker")] = t.speaker;
        o[QStringLiteral("startMs")] = double(t.startMs);
        o[QStringLiteral("endMs")] = double(t.endMs);
        o[QStringLiteral("confidence")] = t.confidence;
        o[QStringLiteral("trackId")] = t.trackId;
        arr.append(o);
    }
    return arr;
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
// térképben vannak. Kulcs-ütközésnél (két régi id ugyanarra az újra) a korábbi régi sor
// bejegyzése marad; eltérő tartalomnál conflicts nő.
struct Remapper {
    const QMap<QString, QString>& map;
    const QHash<QString, int>& order;   // régi id → sorindex (ütközésnél a kisebb nyer)
    int refs = 0;
    int renamed = 0;
    int conflicts = 0;

    QJsonValue value(const QJsonValue& v)
    {
        if (v.isString()) {
            const auto it = map.constFind(v.toString());
            if (it == map.constEnd()) return v;
            ++refs;
            if (it.value() != it.key()) ++renamed;
            return it.value();
        }
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
        QHash<QString, int> srcRank;    // új kulcs → a nyertes forrás rangja
        for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
            QString key = it.key();
            int rank = -1;
            const auto m = map.constFind(key);
            if (m != map.constEnd()) {
                ++refs;
                if (m.value() != key) ++renamed;
                rank = order.value(key, std::numeric_limits<int>::max());
                key = m.value();
            }
            const QJsonValue val = value(it.value());
            if (out.contains(key)) {
                if (out.value(key) != val) ++conflicts;
                if (rank >= 0 && rank < srcRank.value(key, std::numeric_limits<int>::max())) {
                    out[key] = val;
                    srcRank[key] = rank;
                }
                continue;
            }
            out[key] = val;
            srcRank[key] = rank >= 0 ? rank : std::numeric_limits<int>::max();
        }
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

QHash<QString, int> orderOf(const QVector<TranscriptLine>& lines)
{
    QHash<QString, int> h;
    for (int i = 0; i < lines.size(); ++i) h.insert(lines.at(i).id, i);
    return h;
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
    const QVector<TranscriptToken> tokens = tokensFromJson(root.value(QStringLiteral("tokens")).toArray());
    p.tokenCount = int(tokens.size());
    if (tokens.isEmpty()) { p.error = QStringLiteral("%1 has no tokens").arg(kTokens); return p; }
    p.medianWidthMs = sonioxtiming::medianTokenWidthMs(tokens);
    if (root.value(timingMarkerKey()).toString() == timingMarkerValue()) {
        p.verdict = Verdict::AlreadyCorrected;
        return p;
    }
    if (!sonioxtiming::looksLikeEmissionTiming(tokens)) {
        p.verdict = Verdict::RealWordSpans;
        return p;
    }

    // Sávonként korrigálunk (a provider is egy sáv tokenjeit kapja), aztán a merger fésül.
    QVector<TrackTranscript> tracks;
    QHash<QString, int> trackIndex;
    for (const TranscriptToken& t : tokens) {
        auto it = trackIndex.constFind(t.trackId);
        if (it == trackIndex.constEnd()) {
            it = trackIndex.insert(t.trackId, int(tracks.size()));
            TrackTranscript tt;
            tt.trackId = t.trackId;
            tracks.append(tt);
        }
        tracks[it.value()].tokens.append(t);
    }
    for (TrackTranscript& tt : tracks) sonioxtiming::correctTrackTokens(tt.tokens);
    p.corrected = mergeTranscripts(tracks);
    p.corrected.language = root.value(QStringLiteral("language")).toString();

    const QVector<Utterance> newSegs = p.corrected.segments();
    p.newLines = toLines(newSegs);
    p.oldLines = speakeredit::loadTranscriptLines(folder);
    if (p.oldLines.isEmpty()) p.oldLines = toLines(MergedTranscript{root.value(QStringLiteral("language")).toString(), tokens}.segments());
    if (p.newLines.isEmpty()) { p.error = QStringLiteral("re-slicing produced no lines"); return p; }

    // Régi sor → új sor: ugyanaz a beszélő, és az új sor tartalmazza a régi kezdetét (a
    // legkésőbb kezdődő ilyen); ha nincs ilyen, a legközelebbi kezdetű ugyanazon beszélőtől.
    QHash<QString, QVector<int>> bySpeaker;
    for (int i = 0; i < p.newLines.size(); ++i) bySpeaker[p.newLines.at(i).rawLabel].append(i);
    QVector<int> hits(p.newLines.size(), 0);
    p.mapping.reserve(p.oldLines.size());
    for (const TranscriptLine& o : std::as_const(p.oldLines)) {
        const QVector<int> cand = bySpeaker.value(o.rawLabel);
        int best = -1;
        for (int ni : cand) {
            const TranscriptLine& n = p.newLines.at(ni);
            if (n.startMs <= o.startMs && o.startMs <= n.endMs
                && (best < 0 || n.startMs >= p.newLines.at(best).startMs)) best = ni;
        }
        if (best < 0) {
            qint64 bestD = std::numeric_limits<qint64>::max();
            for (int ni : cand) {
                const qint64 d = qAbs(p.newLines.at(ni).startMs - o.startMs);
                if (d < bestD) { bestD = d; best = ni; }
            }
        }
        if (best < 0) {
            p.error = QStringLiteral("line %1 (speaker \"%2\") has no counterpart after re-slicing").arg(o.id, o.rawLabel);
            p.verdict = Verdict::Error;
            return p;
        }
        p.mapping.append(best);
        ++hits[best];
        const TranscriptLine& n = p.newLines.at(best);
        p.idMap.insert(o.id, n.id);
        if (n.id != o.id) ++p.renamedIds;
        p.shiftHistogram[shiftBucket(o.startMs - n.startMs)]++;
    }
    for (int i = 0; i < hits.size(); ++i) {
        if (hits.at(i) > 1) ++p.mergedLines;
    }
    for (int i = 0; i < p.oldLines.size(); ++i)
        if (hits.at(p.mapping.at(i)) == 1 && p.oldLines.at(i).text != p.newLines.at(p.mapping.at(i)).text)
            ++p.textChanged;

    const QHash<QString, int> order = orderOf(p.oldLines);
    QJsonDocument ov;
    p.overlayPresent = readJson(pathIn(folder, kSpeakers), &ov) && ov.isObject();
    if (p.overlayPresent) {
        const QString fp = ov.object().value(QStringLiteral("transcript")).toString();
        p.overlayStale = !fp.isEmpty() && fp != speakeredit::transcriptFingerprint(p.oldLines);
        if (!p.overlayStale) {
            Remapper r{p.idMap, order};
            r.object(ov.object());
            p.overlayRefs = r.refs;
            p.overlayRenamed = r.renamed;
            p.overlayConflicts = r.conflicts;
        }
    }
    QJsonDocument sum;
    p.summaryPresent = readJson(pathIn(folder, kSummary), &sum) && (sum.isObject() || sum.isArray());
    if (p.summaryPresent) {
        Remapper r{p.idMap, order};
        r.value(sum.isObject() ? QJsonValue(sum.object()) : QJsonValue(sum.array()));
        p.summaryRefs = r.refs;
        p.summaryRenamed = r.renamed;
    }
    p.embeddingsPresent = QFile::exists(UtteranceEmbeddingCache::filePath(folder));
    p.verdict = Verdict::Reslice;
    return p;
}

ApplyResult apply(const QString& folder, const Plan& p, bool force)
{
    ApplyResult res;
    if (p.verdict != Verdict::Reslice) { res.error = QStringLiteral("nothing to apply"); return res; }
    if (p.overlayConflicts > 0 && !force) {
        res.error = QStringLiteral("%1 overlay entries would collide on merged lines (use --force: the earlier line wins)")
                        .arg(p.overlayConflicts);
        return res;
    }

    // 1) biztonsági másolatok — egyszer: a meglévő .bak-ot nem írjuk felül.
    for (const QString& name : {kTokens, kSegments, kSpeakers, kSummary, kMarkdown}) {
        const QString src = pathIn(folder, name);
        const QString bak = src + QStringLiteral(".bak");
        if (!QFile::exists(src) || QFile::exists(bak)) continue;
        if (!QFile::copy(src, bak)) { res.error = QStringLiteral("cannot write backup %1").arg(bak); return res; }
        res.backups << bak;
    }

    // 2) tokens.json: a korrigált tokenek + a jelölő (a többi gyökérmező marad).
    QJsonDocument tokDoc;
    readJson(pathIn(folder, kTokens), &tokDoc);
    QJsonObject root = tokDoc.object();
    root[QStringLiteral("language")] = p.corrected.language;
    root[QStringLiteral("tokens")] = tokensToJson(p.corrected.tokens);
    root[timingMarkerKey()] = timingMarkerValue();
    root[QStringLiteral("timingCorrection")] = QJsonObject{{QStringLiteral("leadMs"), double(sonioxtiming::kLeadMs)},
                                                          {QStringLiteral("tailMs"), double(sonioxtiming::kTailMs)}};
    if (!writeBytes(pathIn(folder, kTokens), QJsonDocument(root).toJson(QJsonDocument::Compact))) {
        res.error = QStringLiteral("cannot write %1").arg(kTokens);
        return res;
    }
    res.written << kTokens;

    // 3) segments.json (nyers, mint az átíráskor).
    if (!writeBytes(pathIn(folder, kSegments), segmentsJson(p.corrected.segments()))) {
        res.error = QStringLiteral("cannot write %1").arg(kSegments);
        return res;
    }
    res.written << kSegments;

    const QHash<QString, int> order = orderOf(p.oldLines);

    // 4) overlay: minden id-hivatkozás + az ujjlenyomat (ha az overlay a mostani átiraté volt).
    QJsonDocument ov;
    if (p.overlayPresent && !p.overlayStale && readJson(pathIn(folder, kSpeakers), &ov) && ov.isObject()) {
        Remapper r{p.idMap, order};
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
        Remapper r{p.idMap, order};
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
