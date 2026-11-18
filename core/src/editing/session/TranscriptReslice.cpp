#include "tanara/editing/session/TranscriptReslice.h"
#include "tanara/domain/SpeakerKeys.h"
#include "tanara/library/MeetingLayout.h"

#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/library/store/UtteranceEmbeddingCache.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/processing/stt/SonioxTiming.h"
#include "tanara/domain/Summary.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QMap>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <limits>

namespace tanara::editing::reslice {

namespace {

const QString kTokens   = library::MeetingLayout::tokensJson();
const QString kSegments = library::MeetingLayout::segmentsJson();
const QString kSpeakers = library::MeetingLayout::overlayJson();
const QString kSummary  = library::MeetingLayout::summaryJson();
const QString kMarkdown = library::MeetingLayout::transcriptMd();

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
        t.speaker    = speakerkeys::normalizeSpeakerLabel(o.value(QStringLiteral("speaker")).toString());   // legacy "Beszélő N"
        t.startMs    = qint64(o.value(QStringLiteral("startMs")).toDouble());
        t.endMs      = qint64(o.value(QStringLiteral("endMs")).toDouble());
        t.confidence = o.value(QStringLiteral("confidence")).toDouble();
        t.trackId    = o.value(QStringLiteral("trackId")).toString();
        out.append(t);
    }
    return out;
}

// The same format AppController writes (writeSegmentsJson).
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
    const QVector<tanara::summarysrc::SourceLine> src = tanara::summarysrc::linesFromSegments(segs);
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

// Renaming ids on a JSON tree: of the object keys and the text values, those that are in the map. The
// map is 1:1 (injective), so no key collision can occur.
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

// Side split: rewriting the ids of a JSON tree where a line may become several. In an array every id
// becomes all of its parts; an object key or a single text value becomes the first part.
struct Expander {
    const QMap<QString, QStringList>& parts;   // old id -> new ids (only the changed ones)
    int refs = 0;

    QString first(const QString& s)
    {
        const auto it = parts.constFind(s);
        if (it == parts.constEnd() || it->isEmpty()) return s;
        ++refs;
        return it->first();
    }

    QJsonValue value(const QJsonValue& v)
    {
        if (v.isString()) return first(v.toString());
        if (v.isArray()) {
            QJsonArray out;
            for (const QJsonValue& e : v.toArray()) {
                const auto it = e.isString() ? parts.constFind(e.toString()) : parts.constEnd();
                if (it == parts.constEnd()) { out.append(value(e)); continue; }
                ++refs;
                for (const QString& id : *it) out.append(id);
            }
            return out;
        }
        if (v.isObject()) {
            QJsonObject out;
            const QJsonObject o = v.toObject();
            for (auto it = o.constBegin(); it != o.constEnd(); ++it) out[first(it.key())] = value(it.value());
            return out;
        }
        return v;
    }
};

bool opposite(Side a, Side b)
{
    return (a == Side::Local && b == Side::Remote) || (a == Side::Remote && b == Side::Local);
}

// The times of summary.json's source ranges from the new lines (the ids already renamed).
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
    for (const QJsonValue& c : root.value(QStringLiteral("sideCuts")).toArray()) raw.sideCuts.append(qint64(c.toDouble()));
    p.tokenCount = int(raw.tokens.size());
    if (raw.tokens.isEmpty()) { p.error = QStringLiteral("%1 has no tokens").arg(kTokens); return p; }
    p.medianWidthMs = processing::sonioxtiming::medianTokenWidthMs(raw.tokens);
    if (root.value(timingKey()).toString() == emissionTimingValue()) {
        p.verdict = Verdict::AlreadyPadded;
        return p;
    }
    if (!processing::sonioxtiming::looksLikeEmissionTiming(raw.tokens)) {
        p.verdict = Verdict::RealWordSpans;
        return p;
    }

    // Grouping uses the ORIGINAL token times (the same merger), only the finished lines get padded, so the
    // number and order of lines do not change, and the old -> new map is 1:1 by index.
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
    p.embeddingsPresent = QFile::exists(library::UtteranceEmbeddingCache::filePath(folder));
    p.verdict = Verdict::Reslice;
    return p;
}

ApplyResult apply(const QString& folder, const Plan& p)
{
    ApplyResult res;
    if (p.verdict != Verdict::Reslice) { res.error = QStringLiteral("nothing to apply"); return res; }

    // 1) backups, once: an existing .bak is not overwritten.
    for (const QString& name : {kTokens, kSegments, kSpeakers, kSummary, kMarkdown}) {
        const QString src = pathIn(folder, name);
        const QString bak = src + QStringLiteral(".bak");
        if (!QFile::exists(src) || QFile::exists(bak)) continue;
        if (!QFile::copy(src, bak)) { res.error = QStringLiteral("cannot write backup %1").arg(bak); return res; }
        res.backups << bak;
    }

    // 2) tokens.json: the tokens are UNTOUCHED, only the "timing": "emission" marker is put on (that makes
    //    the merger pad everywhere the app slices from the tokens).
    QJsonDocument tokDoc;
    readJson(pathIn(folder, kTokens), &tokDoc);
    QJsonObject root = tokDoc.object();
    root[timingKey()] = emissionTimingValue();
    if (!writeBytes(pathIn(folder, kTokens), QJsonDocument(root).toJson(QJsonDocument::Compact))) {
        res.error = QStringLiteral("cannot write %1").arg(kTokens);
        return res;
    }
    res.written << kTokens;

    // 3) segments.json (with raw labels, as at transcription; padded times).
    if (!writeBytes(pathIn(folder, kSegments), segmentsJson(p.newSegments))) {
        res.error = QStringLiteral("cannot write %1").arg(kSegments);
        return res;
    }
    res.written << kSegments;


    // 4) overlay: every id reference + the fingerprint (if the overlay belonged to the current transcript).
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

    // 5) summary.json: ids + the times of the source ranges.
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

    // 6) the utterance-embedding cache is tied to the transcript fingerprint, so it is deleted.
    if (QFile::exists(library::UtteranceEmbeddingCache::filePath(folder))) {
        library::UtteranceEmbeddingCache::remove(folder);
        res.embeddingsRemoved = !QFile::exists(library::UtteranceEmbeddingCache::filePath(folder));
    }

    // 7) transcript.md (timestamps) with the resolved names, if there was one.
    if (QFile::exists(pathIn(folder, kMarkdown))) {
        Meeting m = library::MeetingRepository::readMeetingFolder(folder);
        if (!m.id.isEmpty()) {
            m.folder = folder;
            const library::PeopleStore people;   // the display names (<data>/people.json)
            res.markdownRegenerated = speakeredit::regenerateTranscriptMarkdown(m, people.names());
            if (res.markdownRegenerated) res.written << kMarkdown;
        }
    }
    res.ok = true;
    return res;
}

// ---- side split ----------------------------------------------------------------------------------------

SplitPlan planSideSplit(const QString& folder, const MeetingActivity& activity, const SelfPerson& self,
                        const SideHints& hints)
{
    SplitPlan p;
    if (!QFile::exists(pathIn(folder, kTokens))) { p.error = QStringLiteral("missing %1").arg(kTokens); return p; }
    const MergedTranscript cur = library::TranscriptRepository::readTokens(folder);
    if (cur.tokens.isEmpty()) { p.error = QStringLiteral("%1 has no tokens").arg(kTokens); return p; }
    p.oldLines = speakeredit::loadTranscriptLines(folder);
    if (p.oldLines.isEmpty()) { p.error = QStringLiteral("missing or empty %1").arg(kSegments); return p; }

    // The current lines must be what tokens.json yields (otherwise the ids cannot be carried over).
    const QVector<Utterance> curSegs = cur.segments();
    if (curSegs.size() != p.oldLines.size()) {
        p.error = QStringLiteral("%1 has %2 lines but %3 yields %4; not touching it")
                      .arg(kSegments).arg(p.oldLines.size()).arg(kTokens).arg(curSegs.size());
        return p;
    }
    for (int i = 0; i < curSegs.size(); ++i) {
        const TranscriptLine& o = p.oldLines.at(i);
        if (o.rawLabel != curSegs.at(i).speaker || o.startMs != curSegs.at(i).startMs || o.endMs != curSegs.at(i).endMs) {
            p.error = QStringLiteral("line %1 does not match %2 (line %3); not touching it").arg(o.id, kTokens).arg(i + 1);
            return p;
        }
    }

    p.split = sides::splitLinesBySide(cur, activity);
    p.active = p.split.active;
    p.ok = true;
    const QSet<qint64> existing(cur.sideCuts.cbegin(), cur.sideCuts.cend());
    for (qint64 c : std::as_const(p.split.cuts))
        if (!existing.contains(c)) p.newCuts.append(c);
    if (p.newCuts.isEmpty()) return p;
    p.allCuts = cur.sideCuts;
    p.allCuts << p.newCuts;
    std::sort(p.allCuts.begin(), p.allCuts.end());
    p.allCuts.erase(std::unique(p.allCuts.begin(), p.allCuts.end()), p.allCuts.end());

    MergedTranscript next = cur;
    next.sideCuts = p.allCuts;
    p.newSegments = next.segments();
    p.newLines = toLines(p.newSegments);

    // Old line -> its parts: the new lines refine the old ones (cuts are only added), matched by first token.
    const QVector<QVector<int>> oldGroups = cur.lineTokenGroups();
    const QVector<QVector<int>> newGroups = next.lineTokenGroups();
    if (oldGroups.size() != p.oldLines.size() || newGroups.size() != p.newLines.size()) {
        p.ok = false;
        p.error = QStringLiteral("internal: line grouping mismatch");
        return p;
    }
    QHash<int, int> oldOfToken;
    for (int i = 0; i < oldGroups.size(); ++i)
        for (int t : oldGroups.at(i)) oldOfToken.insert(t, i);
    QVector<QVector<int>> partIdx(oldGroups.size());
    for (int j = 0; j < newGroups.size(); ++j) partIdx[oldOfToken.value(newGroups.at(j).first())].append(j);
    QSet<int> cutOld;
    for (int i = 0; i < partIdx.size(); ++i) {
        // Parts in time order; the one holding the old line's first token keeps the line.
        std::sort(partIdx[i].begin(), partIdx[i].end(), [&](int a, int b) {
            return newGroups.at(a).first() < newGroups.at(b).first();
        });
        QStringList ids;
        for (int j : std::as_const(partIdx.at(i))) ids << p.newLines.at(j).id;
        const QString oldId = p.oldLines.at(i).id;
        p.partsOf.insert(oldId, ids);
        if (ids.size() > 1) {
            cutOld.insert(i);
            ++p.cutLines;
            p.parts += int(ids.size());
            p.cutMs += p.oldLines.at(i).endMs - p.oldLines.at(i).startMs;
        }
        if (ids.isEmpty() || ids.first() != oldId) ++p.renamedIds;
        const bool changed = ids.size() > 1 || ids.isEmpty() || ids.first() != oldId
            || p.newLines.at(partIdx.at(i).value(0)).startMs != p.oldLines.at(i).startMs
            || p.newLines.at(partIdx.at(i).value(0)).endMs != p.oldLines.at(i).endMs;
        if (changed) p.changedIds << oldId;
    }

    // Overlay: carried over with the inheritance rules (only if it belongs to the current transcript).
    p.overlayPresent = QFile::exists(pathIn(folder, kSpeakers));
    if (p.overlayPresent) {
        const SpeakerOverlay disk = library::TranscriptRepository::readOverlay(folder);
        p.overlayStale = !disk.transcriptFingerprint.isEmpty()
            && disk.transcriptFingerprint != speakeredit::transcriptFingerprint(p.oldLines);
        if (!p.overlayStale) {
            Meeting m = library::MeetingRepository::readMeetingFolder(folder);
            m.folder = folder;
            const SideReport before = analyzeSides(m, p.oldLines, disk, activity, self, hints);
            QHash<QString, Side> personSide;
            for (const PersonSide& ps : before.persons) personSide.insert(ps.speakerKey, ps.side);
            const QVector<Side> newSides = sides::classifyLineSides(activity, p.newLines);

            QHash<QString, int> oldIndex;
            for (int i = 0; i < p.oldLines.size(); ++i) oldIndex.insert(p.oldLines.at(i).id, i);
            SpeakerOverlay ov = disk;
            ov.utterances.clear();
            for (auto it = disk.utterances.constBegin(); it != disk.utterances.constEnd(); ++it) {
                const int oi = oldIndex.value(it.key(), -1);
                if (oi < 0) { ov.utterances.insert(it.key(), it.value()); continue; }   // (an unknown id stays)
                const QVector<int>& parts = partIdx.at(oi);
                if (parts.isEmpty()) continue;
                if (!cutOld.contains(oi)) {
                    ov.utterances.insert(p.newLines.at(parts.first()).id, it.value());
                    continue;
                }
                ++p.overlayEntriesCut;
                const OverlayUtterance& e = it.value();
                const QString speakerKey = !e.speaker.isEmpty() ? e.speaker : before.lines.value(oi).speakerKey;
                const Side owner = personSide.value(speakerKey, Side::Unknown);
                for (int k = 0; k < parts.size(); ++k) {
                    const Side partSide = newSides.value(parts.at(k), Side::Unknown);
                    const bool contradicts = opposite(owner, partSide);
                    OverlayUtterance u = e;
                    if (contradicts) {
                        ++p.overlayContradicted;
                        u.speaker.clear();
                        u.corrected = false;
                        u.confirmed = false;
                        u.rechecked = false;
                        u.recheckHint.clear();
                        if (k > 0) continue;   // a new part inherits nothing
                    } else if (k > 0) {
                        if (u.confirmed) ++p.confirmedNotInherited;
                        u.confirmed = false;
                        u.rechecked = false;
                        u.recheckHint.clear();
                        ++p.overlayInherited;
                    }
                    if (!u.isDefault()) ov.utterances.insert(p.newLines.at(parts.at(k)).id, u);
                }
            }
            ov.transcriptFingerprint = speakeredit::transcriptFingerprint(p.newLines);
            p.newOverlay = ov;
        }
    }

    QMap<QString, QStringList> changedParts;
    for (const QString& id : std::as_const(p.changedIds)) changedParts.insert(id, p.partsOf.value(id));
    QJsonDocument sum;
    p.summaryPresent = readJson(pathIn(folder, kSummary), &sum) && (sum.isObject() || sum.isArray());
    if (p.summaryPresent) {
        Expander x{changedParts};
        x.value(sum.isObject() ? QJsonValue(sum.object()) : QJsonValue(sum.array()));
        p.summaryRefs = x.refs;
    }
    p.embeddingsPresent = QFile::exists(library::UtteranceEmbeddingCache::filePath(folder));
    return p;
}

ApplyResult applySideSplit(const QString& folder, const SplitPlan& p)
{
    ApplyResult res;
    if (!p.hasWork()) { res.error = QStringLiteral("nothing to apply"); return res; }

    // 1) backups, once: an existing one is not overwritten.
    for (const QString& name : {kTokens, kSegments, kSpeakers, kSummary, kMarkdown}) {
        const QString src = pathIn(folder, name);
        const QString bak = src + QStringLiteral(".side-split.bak");
        if (!QFile::exists(src) || QFile::exists(bak)) continue;
        if (!QFile::copy(src, bak)) { res.error = QStringLiteral("cannot write backup %1").arg(bak); return res; }
        res.backups << bak;
    }

    // 2) tokens.json: the tokens are UNTOUCHED, only the cut list goes into the root.
    QJsonDocument tokDoc;
    if (!readJson(pathIn(folder, kTokens), &tokDoc) || !tokDoc.isObject()) {
        res.error = QStringLiteral("cannot read %1").arg(kTokens);
        return res;
    }
    QJsonObject root = tokDoc.object();
    QJsonArray cuts;
    for (qint64 c : p.allCuts) cuts.append(double(c));
    root[QStringLiteral("sideCuts")] = cuts;
    if (!writeBytes(pathIn(folder, kTokens), QJsonDocument(root).toJson(QJsonDocument::Compact))) {
        res.error = QStringLiteral("cannot write %1").arg(kTokens);
        return res;
    }
    res.written << kTokens;

    // 3) segments.json.
    if (!writeBytes(pathIn(folder, kSegments), segmentsJson(p.newSegments))) {
        res.error = QStringLiteral("cannot write %1").arg(kSegments);
        return res;
    }
    res.written << kSegments;

    // 4) overlay (if it belonged to the current transcript).
    if (p.overlayPresent && !p.overlayStale) {
        if (!library::TranscriptRepository::writeOverlay(folder, p.newOverlay)) {
            res.error = QStringLiteral("cannot write %1").arg(kSpeakers);
            return res;
        }
        res.written << kSpeakers;
    }

    // 5) summary.json: every reference to a cut line lists its parts; the span times are refreshed.
    QMap<QString, QStringList> changedParts;
    for (const QString& id : p.changedIds) changedParts.insert(id, p.partsOf.value(id));
    QJsonDocument sum;
    if (p.summaryPresent && readJson(pathIn(folder, kSummary), &sum)) {
        Expander x{changedParts};
        QJsonValue v = x.value(sum.isObject() ? QJsonValue(sum.object()) : QJsonValue(sum.array()));
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

    // 6) utterance embeddings: the changed lines' entries go (recomputed later), the rest is kept.
    if (p.embeddingsPresent) {
        const QString oldFp = speakeredit::transcriptFingerprint(p.oldLines);
        library::UtteranceEmbeddingCache cache = library::UtteranceEmbeddingCache::load(folder, oldFp);
        if (cache.isEmpty()) {
            library::UtteranceEmbeddingCache::remove(folder);
            res.embeddingsRemoved = true;
        } else {
            for (auto it = cache.models.begin(); it != cache.models.end(); ++it)
                for (const QString& id : p.changedIds) res.embeddingsDropped += int(it.value().remove(id));
            cache.fingerprint = speakeredit::transcriptFingerprint(p.newLines);
            if (!cache.save(folder)) {
                res.error = QStringLiteral("cannot write %1").arg(library::UtteranceEmbeddingCache::filePath(folder));
                return res;
            }
        }
    }

    // 7) transcript.md with the resolved names, if there was one.
    if (QFile::exists(pathIn(folder, kMarkdown))) {
        Meeting m = library::MeetingRepository::readMeetingFolder(folder);
        if (!m.id.isEmpty()) {
            m.folder = folder;
            const library::PeopleStore people;
            res.markdownRegenerated = speakeredit::regenerateTranscriptMarkdown(m, people.names());
            if (res.markdownRegenerated) res.written << kMarkdown;
        }
    }
    res.ok = true;
    return res;
}

} // namespace tanara::editing::reslice