// tanara-cli, library module: list, library roots, adopt / export / import, reindex, rename, tags, align, paths.
// All output is English, no tr().
#include "Commands.h"
#include "PathsCommands.h"
#include "TagPeopleCommands.h"

#include "tanara/app/AppController.h"
#include "tanara/app/Services.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/library/import/AudioImporter.h"
#include "tanara/library/import/ImportService.h"
#include "tanara/library/store/ArchiveService.h"
#include "tanara/library/store/MeetingArchive.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/tags/TagService.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QTimer>

#include <atomic>
#include <csignal>

namespace tanara::cli {

namespace {

// ---- library roots (P8) ------------------------------------------------------------------------

std::atomic<bool> g_moveCancel{false};
void onMoveSigint(int) { g_moveCancel.store(true); }

bool sameRoot(const QString& a, const QString& b) {
    return paths::normalize(a) == paths::normalize(b);
}

// Writes the roots into the settings (first = primary) and hands them to the running repository.
void storeRoots(app::AppController& app, const QStringList& roots) {
    app::AppSettings s = app.settings()->settings();
    s.paths.recordingRoots.clear();
    for (int i = 0; i < roots.size(); ++i)
        s.paths.recordingRoots << paths::RecordingRoot{roots.at(i), i == 0};
    app.settings()->setSettings(s);
    app.store()->setRoots(app.settings()->paths().recordingRoots());
}

int cmdLibrary(CliContext& ctx, const QStringList& args) {
    app::AppController& app = ctx.app();
    const QString sub = args.value(2);
    const bool sandbox = app.settings()->paths().sandboxed();
    auto warnSandbox = [&](const QString& path) {
        if (sandbox && !paths::normalize(path).startsWith(paths::homeOverride()))
            err() << "note: TANARA_HOME is set; a root outside the sandbox is ignored.\n";
    };
    QStringList roots = app.settings()->paths().recordingRoots();
    if (sub == "roots") {
        QHash<QString, int> indexed;
        for (const Meeting& m : app.store()->loadAll())
            ++indexed[library::MeetingRepository::rootOf(m.folder)];
        for (int i = 0; i < roots.size(); ++i) {
            const QString r = QDir::cleanPath(roots.at(i));
            out() << (i == 0 ? "* " : "  ") << QDir::toNativeSeparators(r) << "  "
                << plural(library::MeetingRepository::meetingFolderCount(r), "%n meeting folder", "%n meeting folders")
                << ", " << indexed.value(r) << " indexed" << (QDir(r).exists() ? "" : "  (missing)") << "\n";
        }
        out() << "(* = primary: new recordings go here)\n";
        out().flush();
        return 0;
    }
    if (sub == "add-root" || sub == "set-primary") {
        const QString path = paths::normalize(args.value(3), QDir::currentPath());
        if (path.isEmpty()) { err() << "Usage: library " << sub << " <path>\n"; return 2; }
        warnSandbox(path);
        QStringList next;
        for (const QString& r : std::as_const(roots))
            if (!sameRoot(r, path)) next << r;
        if (sub == "add-root") {
            if (next.size() != roots.size()) { out() << "Already a root: " << QDir::toNativeSeparators(path) << "\n"; return 0; }
            next << path;
        } else {
            next.prepend(path);
        }
        QDir().mkpath(path);
        storeRoots(app, next);
        app.store()->reindex();
        out() << (sub == "add-root" ? "Root added: " : "Primary root: ") << QDir::toNativeSeparators(path) << "\n"
            << QStringLiteral("Index: %1 meetings.").arg(app.store()->loadAll().size()) << "\n";
        out().flush();
        return 0;
    }
    if (sub == "move-root") {
        const QString from = paths::normalize(args.value(3), QDir::currentPath());
        const QString to = paths::normalize(args.value(4), QDir::currentPath());
        if (from.isEmpty() || to.isEmpty()) { err() << "Usage: library move-root <from> <to> [--dry-run]\n"; return 2; }
        warnSandbox(to);
        const library::MeetingRepository::MovePlan plan = app.store()->planMove(from, to);
        out() << QStringLiteral("Move %1 → %2: %3").arg(QDir::toNativeSeparators(from), QDir::toNativeSeparators(to),
                                                      plural(plan.folders.size(), "%n meeting folder", "%n meeting folders"));
        if (!plan.skipped.isEmpty()) out() << QStringLiteral(", %1 skipped (live recording)").arg(plan.skipped.size());
        out() << "\n" << (plan.sameFileSystem ? QStringLiteral("Same file system: folders are renamed.")
                                            : QStringLiteral("Copy + verify + delete: %1 MB, free at target: %2 MB.")
                                                  .arg(plan.bytes / (1024 * 1024)).arg(plan.bytesAvailable / (1024 * 1024)))
            << "\n";
        if (!plan.error.isEmpty()) { err() << "ERROR: " << plan.error << "\n"; return 1; }
        if (args.contains(QStringLiteral("--dry-run"))) {
            for (const QString& f : plan.folders) out() << "  " << f << "\n";
            out() << "(dry run: nothing changed)\n";
            out().flush();
            return 0;
        }
        out().flush();
        g_moveCancel.store(false);
        std::signal(SIGINT, onMoveSigint);
        const library::MeetingRepository::MoveResult res =
            app.store()->moveMeetings(from, to, &g_moveCancel, [](int done, int total, const QString& name) {
                out() << "\r  " << done << "/" << total << "  " << name.left(60).leftJustified(60); out().flush();
            });
        std::signal(SIGINT, SIG_DFL);
        out() << "\n";
        // The settings follow: the target becomes a root (in the source's place, keeping the primary
        // role); the source stays listed while anything is left in it.
        QStringList next;
        const bool sourceEmpty = library::MeetingRepository::meetingFolderCount(from) == 0;
        bool placed = false;
        for (const QString& r : std::as_const(roots)) {
            if (sameRoot(r, to)) { if (!placed) { next << r; placed = true; } continue; }
            if (sameRoot(r, from)) {
                if (!placed) { next << to; placed = true; }
                if (!sourceEmpty) next << r;
                continue;
            }
            next << r;
        }
        if (!placed) next << to;
        storeRoots(app, next);
        out() << QStringLiteral("Moved: %1, skipped: %2, failed: %3.").arg(res.moved.size()).arg(res.skipped.size()).arg(res.failed.size()) << "\n";
        for (const QString& f : res.failed) err() << "  " << f << "\n";
        if (!res.error.isEmpty()) err() << "ERROR: " << res.error << "\n";
        if (res.cancelled) out() << "Stopped. Run the same command again to continue.\n";
        out().flush();
        return res.ok() ? 0 : 1;
    }
    err() << "Usage: library roots | add-root <path> | set-primary <path> | move-root <from> <to> [--dry-run]\n";
    return 2;
}

int cmd_list(CliContext& ctx, const QStringList& args)
{
    Q_UNUSED(args)
    const auto ms = ctx.app().store()->loadAll();
    out() << QStringLiteral("Meetings (%1):").arg(ms.size()) << "\n";
    for (const auto& m : ms)
        out() << "  " << m.id << "  " << m.startedAt.toString(Qt::ISODate)
              << "  \"" << m.title << "\""
              << (m.hasTranscript ? "  [transcript]" : "")
              << (m.hasSummary ? "  [summary]" : "") << "\n";
    out().flush();
    return 0;
}

int cmd_adopt(CliContext& ctx, const QStringList& args)
{
    app::AppController& app = ctx.app();
    // Adopts a recording folder (from another machine, a USB stick): copies it into the recordings
    // and adds ONLY that one to the index. See MeetingRepository::adoptMeetingFolder.
    if (args.size() < 3) { err() << "Usage: adopt <folder>\n"; return 2; }
    QString error;
    const Meeting m = app.store()->adoptMeetingFolder(args.at(2), &error);
    if (m.id.isEmpty()) { err() << error << "\n"; return 1; }
    out() << QStringLiteral("Adopted: %1 — %2 (%3)\n%4\n")
               .arg(m.title, m.id, plural(m.tracks.size(), "%n track", "%n tracks"), m.folder);
    out().flush();
    return 0;
}

int cmd_export(CliContext& ctx, const QStringList& args)
{
    app::AppController& app = ctx.app();
    // Packs a meeting into a *.tanara.zip archive (see library/store/MeetingArchive.h). With no target it
    // goes to the working directory; for a folder target, into it, using the suggested file name.
    const QString id = args.value(2);
    if (id.isEmpty()) { err() << "Usage: export <id> [<archive.zip>]\n"; return 2; }
    const Meeting m = app.store()->load(id);
    if (m.id.isEmpty()) { err() << QStringLiteral("ERROR: no such meeting: %1").arg(id) << "\n"; return 1; }
    QString target = args.value(3);
    if (target.isEmpty() || QFileInfo(target).isDir())
        target = QDir(target.isEmpty() ? QDir::currentPath() : target)
                     .filePath(library::MeetingArchive::suggestedFileName(m));
    target = QFileInfo(target).absoluteFilePath();
    out() << QStringLiteral("Exporting: \"%1\" → %2").arg(m.title, target) << "\n";
    out().flush();
    QString error;
    const bool ok = ctx.services().archive().exportNow(id, target, &error, [](int pct) {
        out() << "\r  " << pct << "%"; out().flush();
    });
    if (!ok) { err() << "\n" << QStringLiteral("ERROR: %1").arg(error) << "\n"; return 1; }
    out() << "\r" << QStringLiteral("DONE. Archive: %1 (%2 MB)")
                   .arg(target).arg(QFileInfo(target).size() / (1024.0 * 1024.0), 0, 'f', 1) << "\n";
    out().flush();
    return 0;
}

int cmd_importArchive(CliContext& ctx, const QStringList& args)
{
    Q_UNUSED(ctx)
    // Adopts a *.tanara.zip archive as if recorded here: unpacks it, places it among the
    // recordings, resolves the tags. A folder or id with the same name is an error.
    const QString zip = args.value(2);
    if (zip.isEmpty()) { err() << "Usage: import-archive <archive.zip>\n"; return 2; }
    QString error;
    const Meeting m = ctx.services().archive().importArchiveNow(zip, &error, [](int pct) {
        out() << "\r  " << pct << "%"; out().flush();
    });
    if (m.id.isEmpty()) { err() << "\n" << QStringLiteral("ERROR: %1").arg(error) << "\n"; return 1; }
    out() << "\r" << QStringLiteral("DONE. Meeting: %1 — %2 (%3)\nFolder: %4")
                   .arg(m.id, m.title, plural(m.tracks.size(), "%n track", "%n tracks"), m.folder) << "\n";
    out().flush();
    return 0;
}

int cmd_reindex(CliContext& ctx, const QStringList& args)
{
    app::AppController& app = ctx.app();
    Q_UNUSED(args)
    // Rebuilds the whole index (SQLite cache) from the meeting folders on disk.
    // Useful after copying a recording folder in by hand (e.g. from another machine).
    app.store()->rebuildIndexFromDisk();
    const auto ms = app.store()->loadAll();
    out() << QStringLiteral("Index rebuilt from disk — %1 meetings.").arg(ms.size()) << "\n";
    out().flush();
    return 0;
}

int cmd_align(CliContext& ctx, const QStringList& args)
{
    app::AppController& app = ctx.app();
    // Track start offsets: a track whose file does not start at the recording's zero point
    // gets a startOffsetMs; the mixdown delays it by that much. The recorder writes the
    // offset itself (tracks turned on later, segments of a device toggled off and on), so
    // this is the repair path for folders recorded before that, or brought from elsewhere.
    // --auto assumes all tracks ENDED together (stop() closes them at once) and derives
    // offset = longest − own duration for every active track that is at least 1 s shorter
    // AND has no offset yet (recorded offsets are kept; a track that was switched off early
    // would be wrongly shifted — fix that one with --track). Then the mixdown is regenerated.
    // The transcript is NOT redone.
    const QString id = args.value(2);
    if (id.isEmpty()) { err() << "Usage: align <id> (--auto | --track <trackId>=<ms> ...)\n"; return 2; }
    Meeting m = app.store()->load(id);
    if (m.id.isEmpty()) { err() << "No such meeting: " << id << "\n"; return 1; }
    bool autoMode = false;
    QHash<QString, qint64> manual;
    for (int i = 3; i < args.size(); ++i) {
        if (args.at(i) == "--auto") autoMode = true;
        else if (args.at(i) == "--track" && i + 1 < args.size()) {
            const QString spec = args.at(++i);
            const int eq = spec.indexOf('=');
            if (eq <= 0) { err() << "Bad --track value (expected <trackId>=<ms>): " << spec << "\n"; return 2; }
            manual.insert(spec.left(eq), spec.mid(eq + 1).toLongLong());
        }
    }
    if (!autoMode && manual.isEmpty()) { err() << "Usage: align <id> (--auto | --track <trackId>=<ms> ...)\n"; return 2; }
    if (autoMode)
        out() << "Note: --auto assumes every track ENDED together and puts the whole length difference\n"
               "      at the START. A track with a gap in the middle (a loopback stream that stopped\n"
               "      for a while) or one that ended early gets a WRONG offset: check the alignment\n"
               "      by listening, and use --track <id>=<ms> (or repair the track file) if needed.\n";
    const media::MediaToolPtr media = app.mediaTool();
    auto probe = [&media](const QString& path) -> qint64 {
        const media::MediaResult r = media->probe(path, 10000);
        return r.info ? r.info->durationMs() : 0;
    };
    QHash<QString, qint64> durations;
    qint64 longest = 0;
    for (const Track& t : m.tracks) {
        if (!t.active) continue;
        const qint64 d = probe(QDir(m.folder).filePath(t.file));
        durations.insert(t.id, d);
        longest = qMax(longest, d);
    }
    bool changed = false;
    for (Track& t : m.tracks) {
        qint64 offset = t.startOffsetMs;
        if (manual.contains(t.id)) offset = manual.value(t.id);
        else if (autoMode && t.active && t.startOffsetMs == 0) {
            const qint64 gap = longest - durations.value(t.id);
            offset = gap >= 1000 ? gap : 0;
        }
        if (offset != t.startOffsetMs) { t.startOffsetMs = offset; changed = true; }
        out() << "  " << t.id << "  duration " << (durations.value(t.id) / 1000.0) << " s"
            << "  offset " << t.startOffsetMs << " ms" << (t.active ? "" : "  (dropped)") << "\n";
    }
    out().flush();
    if (!changed) { out() << "Nothing to change.\n"; out().flush(); return 0; }
    app.store()->saveMeeting(m);
    out() << "Offsets saved; regenerating the mixdown...\n"; out().flush();
    out().flush();
    return regenerateMixdownAndWait(ctx, id,
        QStringLiteral("Mixdown regenerated. Re-run the transcription to apply the new alignment."));
}

// TODO(P12b): import runs through AppController::importAudio (mixdown / speech check follow there).
int cmd_import(CliContext& ctx, const QStringList& args)
{
    app::AppController& app = ctx.app();
    QCoreApplication& qapp = ctx.qapp();
    // Audio file(s) -> new meeting, one track per file (--split-channels: one per channel).
    // Prints the meeting id at the end; transcription does not start.
    library::ImportRequest req;
    bool split = false;
    int ownTrack = 0;   // 1-based; 0 = none
    for (int i = 2; i < args.size(); ++i) {
        if (args[i] == "--title" && i + 1 < args.size()) req.title = args[++i];
        else if (args[i] == "--date" && i + 1 < args.size()) {
            const QString text = args[++i];
            req.startedAt = QDateTime::fromString(text, Qt::ISODate);
            if (!req.startedAt.isValid()) {
                err() << QStringLiteral("Invalid date: %1 (ISO format required, e.g. 2026-03-05T14:30)").arg(text) << "\n";
                return 1;
            }
        }
        else if (args[i] == "--split-channels") split = true;
        else if (args[i] == "--own-track" && i + 1 < args.size()) ownTrack = args[++i].toInt();
        else if (args[i].startsWith("--")) {
            err() << QStringLiteral("Unknown option: %1").arg(args[i]) << "\n"; return 1;
        }
        else req.sources.append({args[i], false});
    }
    if (req.sources.isEmpty()) {
        err() << QStringLiteral("Usage: import <file>… [--title T] [--date ISO] [--split-channels] [--own-track N]") << "\n";
        return 1;
    }
    // Pre-check: every file must contain audio (with a clear error message).
    QVector<library::ImportFileInfo> infos;
    for (library::ImportSource& s : req.sources) {
        const library::ImportFileInfo info = library::audioimport::probe(*app.mediaTool(), s.path);
        if (!info.ok) { err() << QStringLiteral("ERROR: %1").arg(info.error) << "\n"; err().flush(); return 1; }
        s.splitChannels = split && info.channels >= 2;
        infos.append(info);
    }
    const QVector<library::ImportPlannedTrack> plan = library::audioimport::planTracks(req.sources, infos);
    if (ownTrack < 0 || ownTrack > plan.size()) {
        err() << QStringLiteral("--own-track must be between 1 and %1.").arg(plan.size()) << "\n";
        return 1;
    }
    req.ownTrack = ownTrack - 1;
    if (req.title.trimmed().isEmpty()) {
        QStringList paths;
        for (const library::ImportSource& s : req.sources) paths << s.path;
        req.title = library::audioimport::defaultTitle(paths);
    }
    out() << plural(int(plan.size()), "Importing: \"%1\" — %n track", "Importing: \"%1\" — %n tracks").arg(req.title) << "\n";
    for (int i = 0; i < plan.size(); ++i)
        out() << "  • " << plan[i].name
            << (i == req.ownTrack ? QStringLiteral("  (my microphone)") : QString()) << "\n";
    out().flush();

    // This process exits after the import: the mixdown is not started (it would be cut off);
    // the analyzer builds it when needed (transcription generates a missing mix first).
    app.setAutoMixdownAfterRecording(false);
    QObject::connect(app.importer(), &library::AudioImporter::progress, &qapp, [&](QString, int pct, int, int) {
        if (pct >= 0) { out() << "\r  " << pct << "%"; out().flush(); }
    });
    QObject::connect(&app, &app::AppController::importFinished, &qapp, [&](Meeting m) {
        out() << "\r" << QStringLiteral("DONE. Folder: %1").arg(m.folder) << "\n";
        for (const auto& t : m.tracks)
            out() << QStringLiteral("  track: %1  (%2)").arg(t.file, t.deviceName) << "\n";
        out() << QStringLiteral("Meeting: %1").arg(m.id) << "\n";
        out().flush();
        qapp.quit();
    });
    QObject::connect(&app, &app::AppController::importFailed, &qapp, [&](QString, QString message, QString detail) {
        err() << "\n" << QStringLiteral("ERROR: %1").arg(message) << "\n";
        if (!detail.isEmpty()) err() << "  " << detail << "\n";
        err().flush(); qapp.exit(1);
    });
    if (app.importAudio(req).isEmpty()) return 1;
    return qapp.exec();
}

// TODO(P12d): rename runs on the speaker-edit service.
int cmd_rename(CliContext& ctx, const QStringList& args)
{
    app::AppController& app = ctx.app();
    QCoreApplication& qapp = ctx.qapp();
    const QString id = args.value(2), raw = args.value(3), name = args.value(4);
    if (id.isEmpty() || raw.isEmpty()) {
        err() << QStringLiteral("Usage: rename <meetingId> <rawLabel> <name|personId>") << "\n"; return 1;
    }
    QObject::connect(&app, &app::AppController::errorOccurred, &qapp, [&](QString e) {
        err() << QStringLiteral("ERROR: %1").arg(e) << "\n"; err().flush();
    });
    // A known PersonId, else the first person with that name, else a new person; empty = anonymous.
    app.renameSpeaker(id, raw, app.people()->choiceFor(name));   // synchronous
    out() << QStringLiteral("Renamed: \"%1\" → \"%2\"").arg(raw, name) << "\n"; out().flush();
    return 0;
}

// TODO(P12c): `tags suggest` runs on the tag-suggestion service; list/add/remove use library::TagService.
int cmd_tags(CliContext& ctx, const QStringList& args)
{
    app::AppController& app = ctx.app();
    QCoreApplication& qapp = ctx.qapp();
    if (const int rc = tanara::cli::runTagPeopleCommand(app, args); rc >= 0) return rc;
    library::TagService* tags = app.tags();
    const QString sub = args.value(2), id = args.value(3), name = args.value(4);
    auto usage = [&]() {
        err() << QStringLiteral("Usage: tags list [<meetingId>] | tags add|remove <meetingId> <name> | tags suggest <meetingId> [--llm] | tags people <tag> | tags person <name> | tags set-person <name> <tag,...>") << "\n";
        return 1;
    };
    if (sub == "list") {
        if (id.isEmpty()) {
            const auto all = tags->all(library::TagService::Sort::MostUsed);
            out() << QStringLiteral("Tags (%1):").arg(all.size()) << "\n";
            for (const TagUsage& u : all)
                out() << "  #" << u.tag.name << "  " << u.meetingCount << "  " << u.tag.id << "\n";
        } else {
            for (const QString& t : tags->tagsOf(id)) out() << "  #" << tags->tag(t).name << "\n";
        }
        out().flush();
        return 0;
    }
    if ((sub == "add" || sub == "remove") && !id.isEmpty() && !name.isEmpty()) {
        if (sub == "add") {
            const Tag t = tags->addTag(id, name);
            if (!t.isValid()) { err() << QStringLiteral("ERROR: unknown meeting or empty name.") << "\n"; return 1; }
            out() << QStringLiteral("Added: #%1").arg(t.name) << "\n";
        } else {
            Tag t = tags->tag(name);
            if (!t.isValid()) t = tags->byName(name);
            if (!t.isValid()) { err() << QStringLiteral("ERROR: no such tag.") << "\n"; return 1; }
            tags->removeTag(id, t.id);
            out() << QStringLiteral("Removed: #%1").arg(t.name) << "\n";
        }
        out().flush();
        return 0;
    }
    if (sub == "suggest" && !id.isEmpty()) {
        const bool llm = args.contains(QStringLiteral("--llm"));
        QObject::connect(&app, &app::AppController::tagSuggestionsReady, &qapp,
                         [&](const QString& mid, const QVector<TagSuggestion>& list) {
            if (mid != id) return;
            out() << QStringLiteral("Suggestions (%1):").arg(list.size()) << "\n";
            for (const TagSuggestion& sg : list) {
                out() << "  " << (sg.isNew ? "+NEW " : "+") << sg.name
                    << QStringLiteral("  (%1)").arg(sg.score, 0, 'f', 2);
                for (const SuggestionReason& r : sg.reasons) out() << "  · " << r.values.join(QStringLiteral(", "));
                out() << "\n";
            }
            out().flush();
            qapp.exit(0);
        });
        if (llm) app.requestLlmTagSuggestions(id);
        else app.requestTagSuggestions(id);
        QTimer::singleShot(llm ? 300000 : 60000, &qapp, [&]() {
            err() << QStringLiteral("ERROR: no suggestions came back (no summary?).") << "\n";
            qapp.exit(1);
        });
        return qapp.exec();
    }
    return usage();
}


} // namespace

void registerLibraryCommands(CommandRegistry& r)
{
    const QString m = QStringLiteral("library");
    r.add({"list", m, "List the meetings of the library", "list", true, cmd_list});
    r.add({"library", m, "Recording roots: show, add, make primary, move",
           "library roots\nlibrary add-root <path>\nlibrary set-primary <path>\n"
           "library move-root <from> <to> [--dry-run]", true, cmdLibrary});
    r.add({"paths", m, "Show the folders in use and their source; adopt the pre-split folders",
           "paths show\npaths adopt-legacy [--from <dir>] [--dry-run]", false,
           [](CliContext&, const QStringList& a) { return runPathsCommand(a); }});
    r.add({"adopt", m, "Adopt a recording folder (another machine, a USB stick)", "adopt <folder>", true, cmd_adopt});
    r.add({"export", m, "Pack a meeting into a *.tanara.zip archive", "export <id> [<archive.zip>]", true, cmd_export});
    r.add({"import-archive", m, "Adopt a *.tanara.zip archive as if recorded here",
           "import-archive <archive.zip>", true, cmd_importArchive});
    r.add({"import", m, "Import audio file(s) into a new meeting (one track per file)",
           "import <file>... [--title T] [--date ISO] [--split-channels] [--own-track N]", true, cmd_import});
    r.add({"reindex", m, "Rebuild the index from the meeting folders on disk", "reindex", true, cmd_reindex});
    r.add({"rename", m, "Name a raw speaker label of a meeting",
           "rename <meetingId> <rawLabel> <name|personId>", true, cmd_rename});
    r.add({"tags", m, "Tags of meetings and of people",
           "tags list [<meetingId>]\ntags add|remove <meetingId> <name>\ntags suggest <meetingId> [--llm]\n"
           "tags people <tag>\ntags person <name|personId>\ntags set-person <name|personId> <tag,...>", true, cmd_tags});
    r.add({"align", m, "Set track start offsets and regenerate the mixdown",
           "align <id> (--auto | --track <trackId>=<ms> ...)", true, cmd_align});
}

} // namespace tanara::cli
