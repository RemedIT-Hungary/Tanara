#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/library/MeetingLayout.h"

#include "tanara/processing/analysis/UtteranceEmbeddings.h"
#include "tanara/library/summary/SummaryRepository.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace tanara::editing {

namespace speakeredit {

namespace {

const QLatin1String kParticipantPrefix("participant:");

bool writeFile(const QString& path, const QByteArray& data)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(data);
    return f.commit();
}


void appendUnique(QStringList& list, const QString& s)
{
    if (!s.isEmpty() && !list.contains(s)) list << s;
}

} // namespace

// ---- files -----------------------------------------------------------------

QString overlayPath(const QString& meetingFolder)
{
    return library::MeetingLayout::path(meetingFolder, library::MeetingLayout::overlayJson());
}

QString segmentsPath(const QString& meetingFolder)
{
    return library::TranscriptRepository::segmentsPath(meetingFolder);
}

QString mixdownPath(const Meeting& m)
{
    return QDir(m.folder).filePath(library::MeetingLayout::mixdownOf(m));
}

QVector<TranscriptLine> loadTranscriptLines(const QString& meetingFolder)
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

QString transcriptFingerprint(const QVector<TranscriptLine>& lines)
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

SpeakerOverlay loadOverlay(const QString& meetingFolder)
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

SpeakerOverlay loadOverlayFor(const QString& meetingFolder, const QVector<TranscriptLine>& lines)
{
    SpeakerOverlay ov = loadOverlay(meetingFolder);
    const QString fp = transcriptFingerprint(lines);
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

bool saveOverlay(const QString& meetingFolder, const SpeakerOverlay& ov)
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

    return writeFile(path, QJsonDocument(root).toJson(QJsonDocument::Indented));
}

// ---- resolution ---------------------------------------------------------------

bool isParticipantKey(const QString& key)
{
    return key.startsWith(kParticipantPrefix);
}

QString resolveSpeakerKey(const SpeakerOverlay& ov, const TranscriptLine& line)
{
    const auto it = ov.utterances.constFind(line.id);
    if (it != ov.utterances.constEnd() && !it->speaker.isEmpty())
        return it->speaker;
    const auto mg = ov.merged.constFind(line.rawLabel);
    return mg != ov.merged.constEnd() ? mg.value() : line.rawLabel;
}

PersonId speakerPerson(const SpeakerOverlay& ov, const QMap<QString, PersonId>& speakerMap,
                       const QString& key)
{
    if (isParticipantKey(key)) {
        const OverlayParticipant* p = ov.participant(key);
        return p ? p->personId : PersonId();
    }
    return speakerMap.value(key);
}

QString speakerDisplayName(const SpeakerOverlay& ov, const QMap<QString, PersonId>& speakerMap,
                           const PersonNames& names, const QString& key)
{
    if (isParticipantKey(key)) {
        const OverlayParticipant* p = ov.participant(key);
        if (!p) return key;
        return personName(names, p->personId, p->label);
    }
    return personName(names, speakerMap.value(key), key);
}

void applyResolvedSpeakers(MergedTranscript& mt, const Meeting& m, const PersonNames& names)
{
    const QVector<TranscriptLine> lines = loadTranscriptLines(m.folder);
    const SpeakerOverlay ov = loadOverlayFor(m.folder, lines);

    if (!ov.hasEdits() || lines.isEmpty()) {
        // No manual correction (or an old meeting without segments.json): the old behavior.
        if (m.speakerMap.isEmpty()) return;
        for (TranscriptToken& t : mt.tokens) {
            const auto it = m.speakerMap.constFind(t.speaker);
            if (it != m.speakerMap.constEnd()) t.speaker = personName(names, it.value(), t.speaker);
        }
        return;
    }

    // Per raw label the lines by start time: a token belongs to the line of its own raw speaker that contains
    // it in time (one speaker's lines do not overlap).
    struct Span { qint64 startMs; qint64 endMs; QString name; };
    QHash<QString, QVector<Span>> byRaw;
    for (const TranscriptLine& l : lines) {
        const QString key = resolveSpeakerKey(ov, l);
        byRaw[l.rawLabel].append({l.startMs, l.endMs, speakerDisplayName(ov, m.speakerMap, names, key)});
    }
    for (auto it = byRaw.begin(); it != byRaw.end(); ++it)
        std::sort(it->begin(), it->end(),
                  [](const Span& a, const Span& b) { return a.startMs < b.startMs; });

    for (TranscriptToken& t : mt.tokens) {
        const QString raw = t.speaker;
        const auto it = byRaw.constFind(raw);
        const Span* hit = nullptr;
        if (it != byRaw.constEnd()) {
            // The last line that starts before (or exactly at) the token's start.
            auto up = std::upper_bound(it->constBegin(), it->constEnd(), t.startMs,
                [](qint64 ms, const Span& s) { return ms < s.startMs; });
            if (up != it->constBegin()) {
                --up;
                if (t.startMs <= up->endMs) hit = &*up;
            }
        }
        if (hit) {
            t.speaker = hit->name;
        } else {
            // A token not attachable to a line (e.g. an empty-text, skipped block): speaker level.
            const QString key = ov.merged.value(raw, raw);
            t.speaker = speakerDisplayName(ov, m.speakerMap, names, key);
        }
    }
}

bool regenerateTranscriptMarkdown(const Meeting& m, const PersonNames& names)
{
    MergedTranscript mt = library::TranscriptRepository::readTokens(m.folder);
    if (mt.tokens.isEmpty()) return false;
    applyResolvedSpeakers(mt, m, names);
    return library::TranscriptRepository::writeMarkdown(m.folder, mt.renderMarkdown());
}

// ---- summary staleness -----------------------------------------------

SummaryStaleInfo summaryStale(const Meeting& m)
{
    SummaryStaleInfo info;
    if (!m.hasSummary) return info;
    const SpeakerOverlay ov = loadOverlay(m.folder);
    info.correctedSpeakers = ov.changedSinceSummary.size();
    info.stale = info.correctedSpeakers > 0;
    return info;
}

bool markSummaryStale(const Meeting& m, const QStringList& speakerKeys)
{
    if (!m.hasSummary || speakerKeys.isEmpty()) return false;
    SpeakerOverlay ov = loadOverlay(m.folder);
    const int before = ov.changedSinceSummary.size();
    for (const QString& k : speakerKeys) appendUnique(ov.changedSinceSummary, k);
    if (ov.changedSinceSummary.size() == before) return false;
    saveOverlay(m.folder, ov);
    return true;
}

QStringList markSummaryStaleKeys(const Meeting& m, const QStringList& speakerKeys)
{
    QStringList added;
    if (!m.hasSummary || speakerKeys.isEmpty()) return added;
    SpeakerOverlay ov = loadOverlay(m.folder);
    for (const QString& k : speakerKeys) {
        if (k.isEmpty() || ov.changedSinceSummary.contains(k)) continue;
        ov.changedSinceSummary.append(k);
        added.append(k);
    }
    if (!added.isEmpty()) saveOverlay(m.folder, ov);
    return added;
}

bool unmarkSummaryStale(const QString& meetingFolder, const QStringList& speakerKeys)
{
    if (speakerKeys.isEmpty() || !QFile::exists(overlayPath(meetingFolder))) return false;
    SpeakerOverlay ov = loadOverlay(meetingFolder);
    const int before = ov.changedSinceSummary.size();
    for (const QString& k : speakerKeys) ov.changedSinceSummary.removeAll(k);
    if (ov.changedSinceSummary.size() == before) return false;
    saveOverlay(meetingFolder, ov);
    return true;
}

bool clearSummaryStale(const QString& meetingFolder)
{
    if (!QFile::exists(overlayPath(meetingFolder))) return false;
    SpeakerOverlay ov = loadOverlay(meetingFolder);
    const bool was = !ov.changedSinceSummary.isEmpty();
    ov.changedSinceSummary.clear();
    ++ov.summaryEpoch;
    saveOverlay(meetingFolder, ov);
    return was;
}

QVector<tanara::summarysrc::SourceLine> resolvedSourceLines(const Meeting& m, const PersonNames& names)
{
    QVector<tanara::summarysrc::SourceLine> out;
    if (m.folder.isEmpty()) return out;
    const QVector<TranscriptLine> lines = loadTranscriptLines(m.folder);
    if (lines.isEmpty()) return out;
    const SpeakerOverlay ov = loadOverlayFor(m.folder, lines);
    out.reserve(lines.size());
    for (const TranscriptLine& l : lines) {
        tanara::summarysrc::SourceLine sl;
        sl.id = l.id;
        sl.startMs = l.startMs;
        sl.endMs = l.endMs;
        sl.speakerKey = resolveSpeakerKey(ov, l);
        sl.speakerName = speakerDisplayName(ov, m.speakerMap, names, sl.speakerKey);
        out.append(sl);
    }
    return out;
}

void fillTargetedStale(const Meeting& m, const PersonNames& names, SummaryStaleInfo& info,
                       QVector<SummaryStatement>* statements)
{
    if (statements) statements->clear();
    if (!m.hasSummary || m.folder.isEmpty()) return;
    const library::SummaryDocument doc = library::SummaryRepository::load(m.folder);
    QVector<SummaryStatement> sts = doc.summary.statements;
    if (sts.isEmpty()) return;
    QHash<QString, SourceSpeaker> now;
    if (!doc.summary.sourceSpeakers.isEmpty()) {
        for (const tanara::summarysrc::SourceLine& l : resolvedSourceLines(m, names))
            if (doc.summary.sourceSpeakers.contains(l.id))
                now.insert(l.id, SourceSpeaker{l.speakerKey, l.speakerName});
    }
    const SpeakerOverlay ov = loadOverlay(m.folder);
    tanara::summarysrc::applyTargetedStaleness(sts, doc.summary.sourceSpeakers, now, ov.changedSinceSummary, info);
    if (statements) *statements = sts;
}

// ---- AppController hooks -----------------------------------------------

void recordIdentification(const QString& meetingFolder, const QString& rawLabel,
                          const PersonId& person, double score)
{
    if (rawLabel.isEmpty() || person.isNull()) return;
    SpeakerOverlay ov = loadOverlay(meetingFolder);
    ov.identified.insert(rawLabel, OverlayIdentification{person, score});
    saveOverlay(meetingFolder, ov);
}

OverlayPersonRefs replacePersonInOverlay(const QString& meetingFolder, const PersonId& from,
                                         const PersonId& to)
{
    OverlayPersonRefs refs;
    if (from.isNull() || to.isNull() || from == to || !QFile::exists(overlayPath(meetingFolder))) return refs;
    SpeakerOverlay ov = loadOverlay(meetingFolder);
    for (OverlayParticipant& p : ov.participants)
        if (p.personId == from) { p.personId = to; refs.participantKeys << p.key; }
    for (auto it = ov.identified.begin(); it != ov.identified.end(); ++it)
        if (it->personId == from) { it->personId = to; refs.identifiedLabels << it.key(); }
    if (!refs.isEmpty()) saveOverlay(meetingFolder, ov);
    return refs;
}

bool restorePersonInOverlay(const QString& meetingFolder, const OverlayPersonRefs& refs,
                            const PersonId& current, const PersonId& original)
{
    if (refs.isEmpty() || !QFile::exists(overlayPath(meetingFolder))) return false;
    SpeakerOverlay ov = loadOverlay(meetingFolder);
    bool changed = false;
    for (OverlayParticipant& p : ov.participants)
        if (refs.participantKeys.contains(p.key) && p.personId == current) { p.personId = original; changed = true; }
    for (auto it = ov.identified.begin(); it != ov.identified.end(); ++it)
        if (refs.identifiedLabels.contains(it.key()) && it->personId == current) {
            it->personId = original;
            changed = true;
        }
    if (changed) saveOverlay(meetingFolder, ov);
    return changed;
}

bool removePersonFromOverlay(const QString& meetingFolder, const PersonId& person)
{
    if (person.isNull() || !QFile::exists(overlayPath(meetingFolder))) return false;
    SpeakerOverlay ov = loadOverlay(meetingFolder);
    bool changed = false;
    for (OverlayParticipant& p : ov.participants) {
        if (p.personId != person) continue;
        // The participant becomes anonymous (their lines stay with them).
        p.personId = PersonId();
        if (p.label.isEmpty())
            p.label = QCoreApplication::translate("tanara::SpeakerEditor", "Új beszélő %1")
                          .arg(ov.nextAnonymous++);
        changed = true;
    }
    const QStringList idKeys = ov.identified.keys();
    for (const QString& k : idKeys)
        if (ov.identified.value(k).personId == person) { ov.identified.remove(k); changed = true; }
    if (changed) saveOverlay(meetingFolder, ov);
    return changed;
}

// ---- re-transcription ------------------------------------------------------------

RetranscribeImpact retranscribeImpact(const Meeting& m)
{
    RetranscribeImpact imp;
    const QVector<TranscriptLine> lines = loadTranscriptLines(m.folder);
    const SpeakerOverlay ov = loadOverlayFor(m.folder, lines);
    for (auto it = ov.utterances.constBegin(); it != ov.utterances.constEnd(); ++it) {
        if (it->corrected) ++imp.correctedUtterances;
        else if (it->confirmed) ++imp.confirmedUtterances;
    }
    imp.addedParticipants = ov.participants.size();
    QSet<PersonId> named;
    for (auto it = m.speakerMap.constBegin(); it != m.speakerMap.constEnd(); ++it)
        if (!it.value().isNull()) named.insert(it.value());
    for (const OverlayParticipant& p : ov.participants)
        if (!p.personId.isNull()) named.insert(p.personId);
    imp.namedSpeakers = named.size();
    return imp;
}

QString backupTranscript(const Meeting& m, const PersonNames& names)
{
    if (m.folder.isEmpty()) return {};
    // The editor rewrites transcript.md with a delay: the copy always contains the CURRENT (resolved-name) state.
    regenerateTranscriptMarkdown(m, names);
    QDir folder(m.folder);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString prefix = library::MeetingLayout::transcriptBackupPrefix();
    QString name = prefix + stamp;
    for (int i = 2; folder.exists(name); ++i)
        name = prefix + QStringLiteral("%1-%2").arg(stamp).arg(i);
    if (!folder.mkpath(name)) return {};
    const QDir dest(folder.filePath(name));

    int copied = 0;
    const QStringList files = library::MeetingLayout::transcriptFiles();
    for (const QString& f : files)
        if (folder.exists(f) && QFile::copy(folder.filePath(f), dest.filePath(f)))
            ++copied;
    if (copied == 0) {
        folder.rmdir(name);   // nothing to save
        return {};
    }
    // The raw label -> person mapping lives in meeting.json and is cleared at re-transcription: we put it next
    // to the copy so the transcript can be restored together with the people (by PersonId).
    QJsonObject sm;
    for (auto it = m.speakerMap.constBegin(); it != m.speakerMap.constEnd(); ++it)
        sm[it.key()] = it.value().toString();
    QJsonObject root;
    root[QStringLiteral("speakerMap")] = sm;
    root[QStringLiteral("createdAt")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    writeFile(dest.filePath(library::MeetingLayout::backupSpeakerMapJson()),
              QJsonDocument(root).toJson(QJsonDocument::Indented));
    return dest.absolutePath();
}

void discardForNewTranscript(const QString& meetingFolder)
{
    processing::UtteranceEmbeddingCache::remove(meetingFolder);
    if (!QFile::exists(overlayPath(meetingFolder))) return;
    SpeakerOverlay ov = loadOverlay(meetingFolder);
    ov.participants.clear();
    ov.merged.clear();
    ov.removedRaw.clear();
    ov.utterances.clear();
    ov.speakerTracks.clear();
    ov.identified.clear();      // the new diarization's labels mean something else
    ov.nextParticipant = 1;
    ov.nextAnonymous = 1;
    ov.transcriptFingerprint.clear();
    // The stale flag stays: the summary was made from the OLD transcript.
    saveOverlay(meetingFolder, ov);
}

} // namespace speakeredit
} // namespace tanara::editing