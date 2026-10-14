#pragma once
// tanara-cli migrate person-ids [--dry-run] [--backup-dir <dir>]
//
// One-time converter of the person data from the old, NAME-keyed formats to the PersonId formats
// (cleanup phase 2, P11): people.json v3, voiceprints.json v3, tags.json v2 (person rejections),
// every recording root's meeting.json v3 / transcript.speakers.json v3 / participants.analysis.json
// (and the transcript-backup-* copies). Run WITHOUT AppController (close the app first). Before
// writing anything it copies every file it will change into a backup folder (default
// <data>/backup-person-ids-<ISO time>/, with a manifest.json). Idempotent: already converted files
// are skipped. --dry-run computes and reports, and writes nothing. Output is English.
// The old-format reader lives in cli/legacy/ only: the core does not know the old formats.
#include <QStringList>

namespace tanara::cli {

// args[1] == "migrate": "person-ids [--dry-run] [--backup-dir <dir>]".
int runMigrateCommand(const QStringList& args);

} // namespace tanara::cli
