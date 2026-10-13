#pragma once
//
// Path helpers: the single source of truth for the metadata folder.
//
// Default: ~/.tanara. The TANARA_HOME environment variable overrides it; the app then
// reads and writes NOTHING under ~/.tanara (settings, keys, people, voiceprints, logs,
// prompts, models, state.json, lock files, index.db). This allows safe testing on
// sample data: TANARA_HOME=/tmp/sandbox/home tanara
//
// RULE: never compute `homePath() + ".tanara"` anywhere else; always use this.
#include <QString>

namespace tanara {
namespace paths {

// Value of TANARA_HOME as an absolute path with "~" expanded; empty if unset.
QString homeOverride();

// The default metadata folder: TANARA_HOME, otherwise ~/.tanara.
QString defaultMetadataDir();

// Expands "~" / "~/x" to the user's home (other paths unchanged).
QString expandHome(const QString& path);

// Resolves a metadata folder (from settings; may be empty or start with "~"):
//  - if TANARA_HOME is set, it ALWAYS wins (a settings.json value cannot override it);
//  - empty -> defaultMetadataDir(); otherwise the "~"-expanded value.
QString resolveMetadataDir(const QString& configured);

// A file/subfolder in the metadata folder (e.g. metadataFile("people.json")).
QString metadataFile(const QString& relative, const QString& configuredDir = QString());

// With TANARA_HOME, the DEFAULT location of recordings and notes also moves into the
// sandbox (<TANARA_HOME>/recordings, <TANARA_HOME>/notes), otherwise ~/Tanara/recordings|notes.
// (A folder set explicitly in settings.json is not overridden.)
QString defaultAudioDir();
QString defaultNotesDir();

// (The speaker-embedding model file resolution lives in voiceid/VoiceModelRegistry:
// VoiceModelRegistry::defaultModelFileName() / resolveDefaultModelPath().)

} // namespace paths
} // namespace tanara
