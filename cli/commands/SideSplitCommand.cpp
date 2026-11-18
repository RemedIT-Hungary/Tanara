// tanara-cli side-split. All output is English, no tr().
#include "SideSplitCommand.h"

#include "tanara/app/SettingsManager.h"
#include "tanara/editing/review/SideAnalysis.h"
#include "tanara/editing/session/TranscriptReslice.h"
#include "tanara/foundation/time/TimeFormat.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/media/FfmpegMediaTool.h"
#include "tanara/processing/analysis/TrackActivity.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace tanara::cli {

namespace {

QTextStream& out() { static QTextStream s(stdout); return s; }
QTextStream& err() { static QTextStream s(stderr); return s; }

QString sideList(const QVector<editing::Side>& sides)
{
    QStringList names;
    for (editing::Side s : sides) names << editing::sideName(s);
    return names.join(QStringLiteral(" | "));
}

QJsonObject planJson(const QString& folder, const editing::reslice::SplitPlan& p, bool apply, bool fromCache)
{
    QJsonObject o{{"meetingFolder", folder}, {"apply", apply}, {"active", p.active}, {"activityFromCache", fromCache},
                  {"constants", QJsonObject{{"windowMs", double(editing::sides::kSplitWindowMs)},
                                            {"stepMs", double(editing::sides::kSplitStepMs)},
                                            {"minRunMs", double(editing::sides::kSplitMinRunMs)},
                                            {"minRunWords", editing::sides::kSplitMinRunWords},
                                            {"leadMs", double(editing::sides::kSplitLeadMs)}}}};
    if (!p.ok) { o["error"] = p.error; return o; }
    QJsonArray lines;
    for (const editing::sides::LineSplit& l : p.split.lines) {
        QJsonArray cuts, sides;
        for (qint64 c : l.cuts) cuts.append(double(c));
        for (editing::Side s : l.partSides) sides.append(editing::sideName(s));
        lines.append(QJsonObject{{"startMs", double(l.startMs)}, {"endMs", double(l.endMs)}, {"rawLabel", l.rawLabel},
                                 {"cutsMs", cuts}, {"partSides", sides}});
    }
    o["split"] = QJsonObject{{"lines", int(p.split.lines.size())}, {"parts", p.split.parts()},
                             {"affectedMs", double(p.split.affectedMs())}, {"cuts", int(p.split.cuts.size())},
                             {"newCuts", int(p.newCuts.size())}, {"cutLines", lines}};
    if (!p.hasWork()) return o;
    o["lines"] = QJsonObject{{"old", int(p.oldLines.size())}, {"new", int(p.newLines.size())},
                             {"cut", p.cutLines}, {"parts", p.parts}, {"cutMs", double(p.cutMs)},
                             {"renamedIds", p.renamedIds}};
    o["overlay"] = QJsonObject{{"present", p.overlayPresent}, {"stale", p.overlayStale},
                               {"entriesOfCutLines", p.overlayEntriesCut}, {"inherited", p.overlayInherited},
                               {"contradicted", p.overlayContradicted},
                               {"confirmedNotInherited", p.confirmedNotInherited}};
    o["summary"] = QJsonObject{{"present", p.summaryPresent}, {"refs", p.summaryRefs}};
    o["embeddingsCache"] = QJsonObject{{"present", p.embeddingsPresent}, {"changedLines", int(p.changedIds.size())}};
    return o;
}

void printPlan(const editing::reslice::SplitPlan& p, bool fromCache)
{
    out() << "Activity: mic + loopback" << (fromCache ? " (cache)" : " (decoded)") << "\n"
          << "Side cuts: " << p.split.cuts.size() << " (new: " << p.newCuts.size() << ") in "
          << p.split.lines.size() << " lines -> " << p.split.parts() << " parts, "
          << QString::number(double(p.split.affectedMs()) / 1000.0, 'f', 1) << " s\n";
    for (const editing::sides::LineSplit& l : p.split.lines) {
        QStringList cuts;
        for (qint64 c : l.cuts) cuts << timefmt::timestamp(c);
        out() << "  " << timefmt::timestamp(l.startMs) << "-" << timefmt::timestamp(l.endMs) << "  " << l.rawLabel
              << "  cut at " << cuts.join(QStringLiteral(", ")) << "  sides: " << sideList(l.partSides) << "\n";
    }
    if (!p.hasWork()) return;
    out() << "Lines: " << p.oldLines.size() << " -> " << p.newLines.size() << "  (cut: " << p.cutLines
          << ", parts: " << p.parts << ", ids renamed: " << p.renamedIds << ")\n";
    if (!p.overlayPresent) out() << "Overlay: none\n";
    else if (p.overlayStale) out() << "Overlay: belongs to an older transcript; left untouched\n";
    else out() << "Overlay: " << p.overlayEntriesCut << " entries of cut lines; " << p.overlayInherited
               << " parts inherit, " << p.overlayContradicted << " parts contradict their speaker's side, "
               << p.confirmedNotInherited << " confirmed flags not inherited\n";
    if (!p.summaryPresent) out() << "Summary: none\n";
    else out() << "Summary: " << p.summaryRefs << " references to changed lines\n";
    out() << "Utterance embedding cache: "
          << (p.embeddingsPresent ? QStringLiteral("entries of %1 changed lines will be dropped").arg(p.changedIds.size())
                                  : QStringLiteral("none"))
          << "\n";
}

} // namespace

int runSideSplitCommand(const QStringList& args)
{
    QString folder;
    bool apply = false, json = false;
    for (int i = 2; i < args.size(); ++i) {
        const QString& a = args.at(i);
        if (a == "--apply") apply = true;
        else if (a == "--json") json = true;
        else if (folder.isEmpty() && !a.startsWith("--")) folder = a;
        else { err() << "Unknown argument: " << a << "\n"; return 2; }
    }
    if (folder.isEmpty()) {
        err() << "Usage: side-split <meeting-folder> [--apply] [--json]\n";
        return 2;
    }
    if (!QFileInfo(folder).isDir()) { err() << "Not a folder: " << folder << "\n"; return 1; }
    folder = QFileInfo(folder).absoluteFilePath();

    Meeting m = library::MeetingRepository::readMeetingFolder(folder);
    if (m.id.isEmpty()) { err() << "Not a meeting folder (no readable meeting.json): " << folder << "\n"; return 1; }
    m.folder = folder;
    const app::SettingsManager sm;
    const library::PeopleStore people;
    const SelfPerson self{people.selfId(), sm.settings().recording.userSpeakerName.trimmed()};
    editing::SideHints hints;
    hints.names = people.names();

    // The side check's activity: the cache, else decoded (written to the folder only with --apply).
    bool mic = false, loop = false;
    for (const Track& t : m.tracks) {
        if (!t.active) continue;
        mic |= t.kind == TrackKind::Mic;
        loop |= t.kind == TrackKind::Loopback;
    }
    MeetingActivity act;
    bool fromCache = false;
    if (mic && loop) {
        act = MeetingActivity::load(folder, processing::trackactivity::activityFingerprint(m, folder));
        fromCache = !act.isEmpty();
        if (!fromCache) {
            if (!json) { err() << "Decoding tracks…\n"; err().flush(); }
            act = processing::computeMeetingActivity(m, folder, *media::FfmpegMediaTool::fromSettings(sm.settings().media));
            act.fingerprint = processing::trackactivity::activityFingerprint(m, folder);
            if (apply && !act.isEmpty() && !act.save(folder))
                err() << "warning: cannot write " << MeetingActivity::filePath(folder) << "\n";
        }
    }

    const editing::reslice::SplitPlan p = editing::reslice::planSideSplit(folder, act, self, hints);
    QJsonObject jo = planJson(folder, p, apply, fromCache);
    auto finish = [&](int code) {
        if (json) out() << QJsonDocument(jo).toJson(QJsonDocument::Indented);
        out().flush();
        return code;
    };
    if (!p.ok) {
        if (!json) err() << "Error: " << p.error << "\n";
        return finish(1);
    }
    if (!p.active) {
        if (!json) out() << "No mic + loopback activity (single-track meeting or import): nothing to do\n";
        return finish(0);
    }
    if (!json) printPlan(p, fromCache);
    if (!p.hasWork()) {
        if (!json) out() << "Nothing to do: every side cut is already applied.\n";
        return finish(0);
    }
    if (!apply) {
        if (!json) out() << "Dry run: nothing written (--apply writes).\n";
        return finish(0);
    }
    const editing::reslice::ApplyResult r = editing::reslice::applySideSplit(folder, p);
    jo["applied"] = r.ok;
    jo["backups"] = QJsonArray::fromStringList(r.backups);
    jo["written"] = QJsonArray::fromStringList(r.written);
    jo["embeddingsDropped"] = r.embeddingsDropped;
    jo["embeddingsRemoved"] = r.embeddingsRemoved;
    if (!r.ok) {
        jo["error"] = r.error;
        if (!json) err() << "Error: " << r.error << "\n";
        return finish(1);
    }
    if (!json) {
        for (const QString& b : r.backups) out() << "Backup: " << QFileInfo(b).fileName() << "\n";
        out() << "Written: " << r.written.join(QStringLiteral(", ")) << "\n";
        if (r.embeddingsDropped > 0) out() << "Embedding entries dropped: " << r.embeddingsDropped << "\n";
        if (r.embeddingsRemoved) out() << "Deleted: transcript.embeddings.bin (stale)\n";
        out() << "Done.\n";
    }
    return finish(0);
}

} // namespace tanara::cli
