#pragma once
// tanara-cli remote-lines: the remote lines' owners from the meeting's own voices
// (editing/review/RemoteLineClusters.h, through SpeakerEditSession::planRemoteAssignment). A dry run by
// default: it works on a temporary copy of the meeting folder (the audio files are linked, not copied) and
// prints what would change; --apply writes the meeting (one editor step, automatic assignments). With
// --truth <file> it evaluates against hand-reviewed speakers (always on the copy). Output is English and
// contains no transcript text or names: speaker keys and numbers only.
#include <QStringList>

namespace tanara::cli {

// args[1] == "remote-lines".
int runRemoteLinesCommand(const QStringList& args);

} // namespace tanara::cli
