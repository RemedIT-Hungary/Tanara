#include "tanara/edit/SpeakerOverlay.h"

#include "tanara/edit/UtteranceEmbeddings.h"

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

namespace tanara {

const OverlayParticipant* SpeakerOverlay::participant(const QString& key) const
{
    for (const OverlayParticipant& p : participants)
        if (p.key == key) return &p;
    return nullptr;
}

OverlayParticipant* SpeakerOverlay::participant(const QString& key)
{
    for (OverlayParticipant& p : participants)
        if (p.key == key) return &p;
    return nullptr;
}

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

// A transcript.tokens.json beolvasása (ugyanaz a formátum, amit az AppController ír).
MergedTranscript readTokens(const QString& path)
{
    MergedTranscript mt;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return mt;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    mt.language = root.value(QStringLiteral("language")).toString();
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

void appendUnique(QStringList& list, const QString& s)
{
    if (!s.isEmpty() && !list.contains(s)) list << s;
}

} // namespace

// ---- fájlok -----------------------------------------------------------------

QString overlayPath(const QString& meetingFolder)
{
    return QDir(meetingFolder).filePath(QStringLiteral("transcript.speakers.json"));
}

QString segmentsPath(const QString& meetingFolder)
{
    return QDir(meetingFolder).filePath(QStringLiteral("transcript.segments.json"));
}

QString mixdownPath(const Meeting& m)
{
    return QDir(m.folder).filePath(m.mixdownFile.isEmpty() ? QStringLiteral("mixdown.mp3")
                                                           : m.mixdownFile);
}

QVector<TranscriptLine> loadTranscriptLines(const QString& meetingFolder)
{
    QVector<TranscriptLine> lines;
    QFile f(segmentsPath(meetingFolder));
    if (!f.open(QIODevice::ReadOnly)) return lines;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    lines.reserve(arr.size());
    QHash<QString, int> seen;   // alap-id → hányszor fordult elő
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        TranscriptLine l;
        l.startMs  = qint64(o.value(QStringLiteral("startMs")).toDouble());
        l.endMs    = qint64(o.value(QStringLiteral("endMs")).toDouble());
        l.rawLabel = o.value(QStringLiteral("speaker")).toString();
        l.text     = o.value(QStringLiteral("text")).toString();
        // Az id a kezdőidőből képződik: az átirat újragenerálásáig stabil, és a fájl
        // átrendezésére sem érzékeny. Azonos kezdőidő (átfedő beszélők) → sorszám-utótag.
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
        p.person     = o.value(QStringLiteral("person")).toString();
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
        if (!u.isDefault()) ov.utterances.insert(it.key(), u);
    }

    const QJsonObject ident = root.value(QStringLiteral("identified")).toObject();
    for (auto it = ident.constBegin(); it != ident.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        OverlayIdentification id;
        id.person = o.value(QStringLiteral("person")).toString();
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
        // Másik (korábbi) átirathoz tartozó javítások: a megszólalás-határok már nem
        // ugyanazok → nem alkalmazhatók. Az azonosítás/elavult-jelző átirat-független.
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
        // Nincs mit tárolni → ne hagyjunk üres fájlt a meeting-mappában.
        return !QFile::exists(path) || QFile::remove(path);
    }

    QJsonObject root;
    root[QStringLiteral("version")] = 2;
    root[QStringLiteral("transcript")] = ov.transcriptFingerprint;

    QJsonArray parts;
    for (const OverlayParticipant& p : ov.participants) {
        QJsonObject o;
        o[QStringLiteral("key")] = p.key;
        o[QStringLiteral("person")] = p.person;
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
        o[QStringLiteral("person")] = it.value().person;
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

// ---- feloldás ---------------------------------------------------------------

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

QString speakerPerson(const SpeakerOverlay& ov, const QMap<QString, QString>& speakerMap,
                      const QString& key)
{
    if (isParticipantKey(key)) {
        const OverlayParticipant* p = ov.participant(key);
        return p ? p->person : QString();
    }
    const QString name = speakerMap.value(key);
    return name == key ? QString() : name;
}

QString speakerDisplayName(const SpeakerOverlay& ov, const QMap<QString, QString>& speakerMap,
                           const QString& key)
{
    if (isParticipantKey(key)) {
        const OverlayParticipant* p = ov.participant(key);
        if (!p) return key;
        return !p->person.isEmpty() ? p->person : p->label;
    }
    const QString name = speakerMap.value(key);
    return name.isEmpty() ? key : name;
}

void applyResolvedSpeakers(MergedTranscript& mt, const Meeting& m)
{
    const QVector<TranscriptLine> lines = loadTranscriptLines(m.folder);
    const SpeakerOverlay ov = loadOverlayFor(m.folder, lines);

    if (!ov.hasEdits() || lines.isEmpty()) {
        // Nincs kézi javítás (vagy régi meeting segments.json nélkül): a régi viselkedés.
        if (m.speakerMap.isEmpty()) return;
        for (TranscriptToken& t : mt.tokens) {
            const auto it = m.speakerMap.constFind(t.speaker);
            if (it != m.speakerMap.constEnd()) t.speaker = it.value();
        }
        return;
    }

    // Nyers címkénként a sorok kezdőidő szerint — a token a saját nyers beszélőjének abba
    // a sorába tartozik, amelyik időben tartalmazza (egy beszélő sorai nem fedik egymást).
    struct Span { qint64 startMs; qint64 endMs; QString name; };
    QHash<QString, QVector<Span>> byRaw;
    for (const TranscriptLine& l : lines) {
        const QString key = resolveSpeakerKey(ov, l);
        byRaw[l.rawLabel].append({l.startMs, l.endMs, speakerDisplayName(ov, m.speakerMap, key)});
    }
    for (auto it = byRaw.begin(); it != byRaw.end(); ++it)
        std::sort(it->begin(), it->end(),
                  [](const Span& a, const Span& b) { return a.startMs < b.startMs; });

    for (TranscriptToken& t : mt.tokens) {
        const QString raw = t.speaker;
        const auto it = byRaw.constFind(raw);
        const Span* hit = nullptr;
        if (it != byRaw.constEnd()) {
            // Az utolsó sor, amelyik a token kezdete előtt (vagy épp akkor) indul.
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
            // Sorhoz nem köthető token (pl. üres szövegű, kihagyott blokk): beszélő-szint.
            const QString key = ov.merged.value(raw, raw);
            t.speaker = speakerDisplayName(ov, m.speakerMap, key);
        }
    }
}

bool regenerateTranscriptMarkdown(const Meeting& m)
{
    MergedTranscript mt =
        readTokens(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
    if (mt.tokens.isEmpty()) return false;
    applyResolvedSpeakers(mt, m);
    return writeFile(QDir(m.folder).filePath(QStringLiteral("transcript.md")),
                     mt.renderMarkdown().toUtf8());
}

// ---- összefoglaló-elavultság -----------------------------------------------

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

// ---- az AppController horgai -----------------------------------------------

void recordIdentification(const QString& meetingFolder, const QString& rawLabel,
                          const QString& person, double score)
{
    if (rawLabel.isEmpty() || person.isEmpty()) return;
    SpeakerOverlay ov = loadOverlay(meetingFolder);
    ov.identified.insert(rawLabel, OverlayIdentification{person, score});
    saveOverlay(meetingFolder, ov);
}

bool renamePersonInOverlay(const QString& meetingFolder, const QString& oldName,
                           const QString& newName)
{
    if (!QFile::exists(overlayPath(meetingFolder))) return false;
    SpeakerOverlay ov = loadOverlay(meetingFolder);
    bool changed = false;
    for (OverlayParticipant& p : ov.participants)
        if (p.person == oldName) { p.person = newName; changed = true; }
    for (auto it = ov.identified.begin(); it != ov.identified.end(); ++it)
        if (it->person == oldName) { it->person = newName; changed = true; }
    if (changed) saveOverlay(meetingFolder, ov);
    return changed;
}

bool removePersonFromOverlay(const QString& meetingFolder, const QString& name)
{
    if (!QFile::exists(overlayPath(meetingFolder))) return false;
    SpeakerOverlay ov = loadOverlay(meetingFolder);
    bool changed = false;
    for (OverlayParticipant& p : ov.participants) {
        if (p.person != name) continue;
        // A résztvevő névtelenné válik (a sorai megmaradnak nála).
        p.person.clear();
        if (p.label.isEmpty())
            p.label = QCoreApplication::translate("tanara::SpeakerEditor", "Új beszélő %1")
                          .arg(ov.nextAnonymous++);
        changed = true;
    }
    const QStringList idKeys = ov.identified.keys();
    for (const QString& k : idKeys)
        if (ov.identified.value(k).person == name) { ov.identified.remove(k); changed = true; }
    if (changed) saveOverlay(meetingFolder, ov);
    return changed;
}

// ---- újra-átírás ------------------------------------------------------------

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
    QSet<QString> named;
    for (auto it = m.speakerMap.constBegin(); it != m.speakerMap.constEnd(); ++it)
        if (!it.value().isEmpty() && it.value() != it.key()) named.insert(it.value());
    for (const OverlayParticipant& p : ov.participants)
        if (!p.person.isEmpty()) named.insert(p.person);
    imp.namedSpeakers = named.size();
    return imp;
}

QString backupTranscript(const Meeting& m)
{
    if (m.folder.isEmpty()) return {};
    // A szerkesztő a transcript.md-t késleltetve írja újra: a másolat mindig a MOSTANI
    // (feloldott nevű) állapotot tartalmazza.
    regenerateTranscriptMarkdown(m);
    QDir folder(m.folder);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString name = QStringLiteral("transcript-backup-%1").arg(stamp);
    for (int i = 2; folder.exists(name); ++i)
        name = QStringLiteral("transcript-backup-%1-%2").arg(stamp).arg(i);
    if (!folder.mkpath(name)) return {};
    const QDir dest(folder.filePath(name));

    int copied = 0;
    const QStringList files{
        QStringLiteral("transcript.md"), QStringLiteral("transcript.tokens.json"),
        QStringLiteral("transcript.segments.json"), QStringLiteral("transcript.speakers.json")};
    for (const QString& f : files)
        if (folder.exists(f) && QFile::copy(folder.filePath(f), dest.filePath(f)))
            ++copied;
    if (copied == 0) {
        folder.rmdir(name);   // nem volt mit menteni
        return {};
    }
    // A nyers címke → név leképezés a meeting.json-ban él, és újra-átíráskor törlődik:
    // a másolat mellé tesszük, hogy az átirat a nevekkel együtt visszaállítható legyen.
    QJsonObject sm;
    for (auto it = m.speakerMap.constBegin(); it != m.speakerMap.constEnd(); ++it)
        sm[it.key()] = it.value();
    QJsonObject root;
    root[QStringLiteral("speakerMap")] = sm;
    root[QStringLiteral("createdAt")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    writeFile(dest.filePath(QStringLiteral("speakerMap.json")),
              QJsonDocument(root).toJson(QJsonDocument::Indented));
    return dest.absolutePath();
}

void discardForNewTranscript(const QString& meetingFolder)
{
    UtteranceEmbeddingCache::remove(meetingFolder);
    if (!QFile::exists(overlayPath(meetingFolder))) return;
    SpeakerOverlay ov = loadOverlay(meetingFolder);
    ov.participants.clear();
    ov.merged.clear();
    ov.removedRaw.clear();
    ov.utterances.clear();
    ov.speakerTracks.clear();
    ov.identified.clear();      // az új diarizáció címkéi mást jelentenek
    ov.nextParticipant = 1;
    ov.nextAnonymous = 1;
    ov.transcriptFingerprint.clear();
    // Az elavult-jelző marad: az összefoglaló a RÉGI átiratból készült.
    saveOverlay(meetingFolder, ov);
}

} // namespace speakeredit
} // namespace tanara
