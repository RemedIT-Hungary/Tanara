// tanara-cli, editing module: review, lines, reslice, participants, track-sides. All output is English, no tr().
#include "Commands.h"
#include "ParticipantsCommand.h"
#include "ResliceCommand.h"
#include "ReviewCommand.h"
#include "TrackSidesCommand.h"

#include "tanara/app/AppController.h"
#include "tanara/library/store/PeopleStore.h"

#include <QFileInfo>

namespace tanara::cli {

namespace {

// TODO(P12d): the meeting-id form runs on editing::ParticipantService.
int cmdParticipantsById(CliContext& ctx, const QStringList& args)
{
    app::AppController& app = ctx.app();
    const QString id = args.value(2);
    if (id.isEmpty()) { err() << QStringLiteral("Usage: participants <meetingId>") << "\n"; return 1; }
    out() << QStringLiteral("Identifying participants (before transcription, locally)…") << "\n"; out().flush();
    const auto guesses = app.identifyParticipants(id);
    if (guesses.isEmpty()) { out() << QStringLiteral("  (no match — no model/active track, or silence)") << "\n"; out().flush(); return 0; }
    for (const auto& g : guesses) {
        const QString who = g.personId.isNull()
            ? QStringLiteral("UNKNOWN")
            : QStringLiteral("%1 (%2%)").arg(g.displayName).arg(int(g.score * 100 + 0.5));
        out() << plural(g.windows, "  • %1  →  %2   [%n window, sample: %3]", "  • %1  →  %2   [%n windows, sample: %3]")
                   .arg(g.deviceName, who, g.sampleRef)
            << "\n";
    }
    out().flush();
    return 0;
}

// participants <meeting-folder> (folder-based, no AppController); the older <meetingId> form needs it.
int cmdParticipants(CliContext& ctx, const QStringList& args)
{
    if (QFileInfo(args.value(2)).isDir()) return runParticipantsCommand(args);
    return cmdParticipantsById(ctx, args);
}

} // namespace

void registerEditingCommands(CommandRegistry& r)
{
    const QString m = QStringLiteral("editing");
    r.add({"review", m, "Review queue: groups and candidates with evidence (read-only)",
           "review <meeting-folder> [--json] [--models a,b]", false,
           [](CliContext&, const QStringList& a) { return runReviewCommand(a); }});
    r.add({"lines", m, "Transcript lines with markers; ignore / unignore lines",
           "lines <meeting-folder> [--json]\nlines ignore|unignore <meeting-folder> <id...>", false,
           [](CliContext&, const QStringList& a) { return runLinesCommand(a); }});
    r.add({"reslice", m, "Re-pad Soniox lines, carrying the line ids over",
           "reslice <meeting-folder> [--dry-run] [--json]", false,
           [](CliContext&, const QStringList& a) { return runResliceCommand(a); }});
    r.add({"participants", m, "Participants and candidates of a meeting",
           "participants <meeting-folder> [--analyze [--write] | --remove <id>] [--json]\nparticipants <meetingId>", false,
           cmdParticipants});
    r.add({"track-sides", m, "Local / remote side check of the tracks",
           "track-sides <meeting-folder> [--frame-ms 50] [--json] [--no-cache] [--write-cache] [--legacy]", false,
           [](CliContext&, const QStringList& a) { return runTrackSidesCommand(a); }});
}

} // namespace tanara::cli
