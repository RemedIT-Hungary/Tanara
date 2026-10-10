// tanara-cli track-sides. Minden kimenet angol, tr() nélkül.
#include "TrackSidesCommand.h"

#include "tanara/SettingsManager.h"
#include "tanara/edit/SideAnalysis.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/edit/TrackActivity.h"
#include "tanara/store/MeetingStore.h"

#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <cmath>

namespace tanara::cli {

namespace {

QTextStream& out() { static QTextStream s(stdout); return s; }
QTextStream& err() { static QTextStream s(stderr); return s; }

constexpr int kMaxListedConflicts = 20;

QString kindName(TrackKind k)
{
    return k == TrackKind::Mic ? QStringLiteral("mic") : k == TrackKind::Loopback ? QStringLiteral("loopback")
                                                                                   : QStringLiteral("other");
}

QString clock(qint64 ms)
{
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString num(double v, int prec = 2) { return std::isnan(v) ? QStringLiteral("-") : QString::number(v, 'f', prec); }
QJsonValue jnum(double v) { return std::isnan(v) ? QJsonValue() : QJsonValue(v); }

QJsonObject countsJson(const SideCounts& c)
{
    return {{"local", c.local}, {"remote", c.remote}, {"mixed", c.mixed}, {"unknown", c.unknown}};
}

QString countsText(const SideCounts& c)
{
    return QStringLiteral("local %1  remote %2  mixed %3  unknown %4").arg(c.local).arg(c.remote).arg(c.mixed).arg(c.unknown);
}

QJsonObject toJson(const SideReport& r, const MeetingActivity& act, const QString& folder, bool fromCache,
                   qint64 meetingMs)
{
    QJsonObject root;
    root["meetingFolder"] = folder;
    root["active"] = r.active;
    root["activityFromCache"] = fromCache;
    QJsonArray tracks;
    for (const TrackActivity& t : act.tracks)
        tracks.append(QJsonObject{{"id", t.trackId}, {"kind", kindName(t.kind)}, {"frameMs", t.frameMs},
                                  {"originMs", double(t.originMs)}, {"floorDb", double(t.floorDb)},
                                  {"coveredMs", double(t.coveredMs())},
                                  {"coverage", meetingMs > 0 ? double(t.coveredMs()) / double(meetingMs) : 0.0}});
    root["tracks"] = tracks;
    root["thresholds"] = QJsonObject{{"loRemote", double(r.thresholds.loRemote)}, {"hiLocal", double(r.thresholds.hiLocal)},
                                     {"adaptive", r.thresholds.adaptive},
                                     {"centerRemote", jnum(r.thresholds.centerRemote)},
                                     {"centerLocal", jnum(r.thresholds.centerLocal)}};
    QJsonArray hist;
    for (int h : r.histogram) hist.append(h);
    root["histogram"] = hist;
    root["totals"] = countsJson(r.totals);
    QJsonObject raw;
    for (auto it = r.rawLabels.cbegin(); it != r.rawLabels.cend(); ++it) raw[it.key()] = countsJson(it.value());
    root["rawLabels"] = raw;
    QHash<QString, int> conflictsPer;
    for (const SideConflict& c : r.conflicts) ++conflictsPer[c.speakerKey];
    QJsonArray persons;
    for (const PersonSide& p : r.persons)
        persons.append(QJsonObject{{"speakerKey", p.speakerKey}, {"person", p.personName}, {"name", p.displayName},
                                   {"side", sideName(p.side)}, {"defaultSide", sideName(p.defaultSide)},
                                   {"basis", p.basis}, {"confidence", double(p.confidence)},
                                   {"confirmed", QJsonObject{{"local", p.localLines}, {"remote", p.remoteLines},
                                                             {"mixed", p.mixedLines}}},
                                   {"all", countsJson(p.allLines)},
                                   {"conflicts", conflictsPer.value(p.speakerKey)}});
    root["persons"] = persons;
    QJsonArray conflicts;
    for (const SideConflict& c : r.conflicts)
        conflicts.append(QJsonObject{{"utteranceId", c.utteranceId}, {"startMs", double(c.startMs)},
                                     {"speakerKey", c.speakerKey}, {"lineSide", sideName(c.lineSide)},
                                     {"personSide", sideName(c.personSide)}, {"localShare", jnum(c.localShare)}});
    root["conflicts"] = conflicts;
    QJsonArray lines;
    for (const LineSide& l : r.lines)
        lines.append(QJsonObject{{"id", l.utteranceId}, {"startMs", double(l.startMs)}, {"endMs", double(l.endMs)},
                                 {"speakerKey", l.speakerKey}, {"rawLabel", l.rawLabel}, {"side", sideName(l.side)},
                                 {"localShare", jnum(l.localShare)}, {"micDb", jnum(l.micDb)},
                                 {"loopDb", jnum(l.loopDb)}, {"locked", l.locked}, {"noisy", l.noisy}});
    root["lines"] = lines;
    return root;
}

void printText(const SideReport& r, const MeetingActivity& act, bool fromCache, qint64 meetingMs)
{
    out() << "Tracks (activity " << (fromCache ? "from cache" : "computed") << "):\n";
    for (const TrackActivity& t : act.tracks)
        out() << "  " << t.trackId.leftJustified(24) << " " << kindName(t.kind).leftJustified(8)
              << " floor " << num(t.floorDb, 1).rightJustified(6) << " dBFS   covered " << clock(t.coveredMs())
              << (meetingMs > 0 ? QStringLiteral(" (%1%)").arg(qRound(100.0 * t.coveredMs() / meetingMs)) : QString())
              << "   origin " << clock(t.originMs) << "\n";
    if (act.tracks.isEmpty()) out() << "  (none)\n";
    if (!r.active) {
        out() << "\nInactive: the meeting needs both a mic and a loopback track with audio; every line is unknown.\n";
        out() << "Lines: " << countsText(r.totals) << "\n";
        out().flush();
        return;
    }

    const auto& th = r.thresholds;
    out() << "\nThresholds: remote <= " << num(th.loRemote) << ", local >= " << num(th.hiLocal)
          << (th.adaptive ? QStringLiteral("  (adaptive; centers %1 / %2)").arg(num(th.centerRemote), num(th.centerLocal))
                          : QStringLiteral("  (default)"))
          << "\n";
    out() << "\nlocalShare histogram (0 = remote only, 1 = local only):\n";
    int maxH = 1;
    for (int h : r.histogram) maxH = std::max(maxH, h);
    for (int i = 0; i < r.histogram.size(); ++i)
        out() << QStringLiteral("  %1-%2 ").arg(i / 10.0, 0, 'f', 1).arg((i + 1) / 10.0, 0, 'f', 1)
              << QString::number(r.histogram[i]).rightJustified(5) << " "
              << QString(qRound(40.0 * r.histogram[i] / maxH), QLatin1Char('#')) << "\n";
    out() << "\nLines: " << countsText(r.totals) << "\n";

    out() << "\nRaw diarization labels:\n";
    for (auto it = r.rawLabels.cbegin(); it != r.rawLabels.cend(); ++it)
        out() << "  " << it.key().leftJustified(20) << " " << countsText(it.value()) << "\n";

    QHash<QString, int> conflictsPer;
    for (const SideConflict& c : r.conflicts) ++conflictsPer[c.speakerKey];
    out() << "\nSpeakers (confirmed L/R/M | all L/R/M/U):\n";
    for (const PersonSide& p : r.persons)
        out() << "  " << p.displayName.leftJustified(20) << " " << sideName(p.side).leftJustified(7)
              << " conf " << num(p.confidence) << "  " << p.basis.leftJustified(11)
              << QStringLiteral(" %1/%2/%3 | %4/%5/%6/%7").arg(p.localLines).arg(p.remoteLines).arg(p.mixedLines)
                     .arg(p.allLines.local).arg(p.allLines.remote).arg(p.allLines.mixed).arg(p.allLines.unknown)
              << "   conflicts " << conflictsPer.value(p.speakerKey)
              << (p.displayName != p.speakerKey ? QStringLiteral("   (%1)").arg(p.speakerKey) : QString()) << "\n";

    out() << "\nConflicts: " << r.conflicts.size() << "\n";
    for (int i = 0; i < r.conflicts.size() && i < kMaxListedConflicts; ++i) {
        const SideConflict& c = r.conflicts[i];
        out() << "  " << clock(c.startMs) << "  " << c.utteranceId.leftJustified(14) << " " << c.speakerKey.leftJustified(20)
              << " line " << sideName(c.lineSide).leftJustified(6) << " speaker " << sideName(c.personSide).leftJustified(6)
              << " share " << num(c.localShare) << "\n";
    }
    if (r.conflicts.size() > kMaxListedConflicts)
        out() << "  … " << (r.conflicts.size() - kMaxListedConflicts) << " more (see --json)\n";
    out().flush();
}

} // namespace

int runTrackSidesCommand(const QStringList& args)
{
    QString folder;
    int frameMs = trackactivity::kDefaultFrameMs;
    bool json = false, noCache = false, writeCache = false;
    for (int i = 2; i < args.size(); ++i) {
        const QString a = args.at(i);
        if (a == "--frame-ms" && i + 1 < args.size()) frameMs = args.at(++i).toInt();
        else if (a == "--json") json = true;
        else if (a == "--no-cache") noCache = true;
        else if (a == "--write-cache") writeCache = true;
        else if (folder.isEmpty() && !a.startsWith("--")) folder = a;
        else { err() << "Unknown argument: " << a << "\n"; return 2; }
    }
    if (folder.isEmpty() || frameMs < 10 || frameMs > 1000) {
        err() << "Usage: track-sides <meeting-folder> [--frame-ms 50] [--json] [--no-cache] [--write-cache]\n";
        return 2;
    }
    folder = QFileInfo(folder).absoluteFilePath();

    Meeting m = MeetingStore::readMeetingFolder(folder);
    if (m.id.isEmpty()) { err() << "Not a meeting folder (no readable meeting.json): " << folder << "\n"; return 1; }
    m.folder = folder;
    const QVector<TranscriptLine> lines = speakeredit::loadTranscriptLines(folder);
    if (lines.isEmpty()) { err() << "No transcript (transcript.segments.json) in " << folder << "\n"; return 1; }
    const SpeakerOverlay ov = speakeredit::loadOverlayFor(folder, lines);
    const SettingsManager sm;
    const QString userName = sm.settings().userSpeakerName;

    // Aktivitás: a cache-ből, ha a lenyomat egyezik; különben számolva (a mappába csak --write-cache-sel ír).
    const QString fp = trackactivity::activityFingerprint(m, folder, frameMs);
    MeetingActivity act;
    bool fromCache = false;
    if (!noCache) {
        act = MeetingActivity::load(folder, fp);
        fromCache = !act.isEmpty();
    }
    if (!fromCache) {
        err() << "Decoding tracks…\n";
        err().flush();
        act = computeMeetingActivity(m, folder, QStringLiteral("ffmpeg"), frameMs);
        if (writeCache && !act.save(folder))
            err() << "warning: cannot write " << MeetingActivity::filePath(folder) << "\n";
    }

    qint64 meetingMs = m.durationMs;
    for (const TranscriptLine& l : lines) meetingMs = std::max(meetingMs, l.endMs);

    const SideReport r = analyzeSides(m, lines, ov, act, userName);
    if (json) {
        out() << QJsonDocument(toJson(r, act, folder, fromCache, meetingMs)).toJson(QJsonDocument::Indented);
        out().flush();
    } else {
        printText(r, act, fromCache, meetingMs);
    }
    return 0;
}

} // namespace tanara::cli
