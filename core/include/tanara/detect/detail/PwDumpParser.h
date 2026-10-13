#pragma once
//
// PwDumpParser: decides from the JSON output of `pw-dump` (PipeWire) whether a call is going on.
// A PURE function: no process start, no I/O, so it is unit-testable on its own (LinuxCaptureDetector
// runs pw-dump and passes the raw JSON here).
//
// The signal: an ACTIVE microphone-holding stream, a PipeWire Node with
//   info.props["media.class"] == "Stream/Input/Audio"  (an app holds the microphone)
//   info.state == "running"                             (not paused)
// and the owner binary is a KNOWN call app, but NOT ours (self-exclusion).
#include "tanara/detect/IMeetingDetector.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace tanara::detail {

// json: the full output of `pw-dump`. knownApps: binary/app-name fragments that mark a call app
// (e.g. "zoom","teams","discord"). selfBinary: our own process's binary (e.g. "tanara"), which is never
// taken as a meeting. No match -> {active:false}.
MeetingSignal parsePwDump(const QByteArray& json,
                          const QStringList& knownApps,
                          const QString& selfBinary);

// A human-readable app name from the (lowercase) binary, or from application.name: it gives a pretty name
// for known apps ("teams" -> "Microsoft Teams"), otherwise application.name, failing that the binary.
// (The playback-route watcher, audio/PlaybackRouting.h, uses this too.)
QString prettyAppName(const QString& binaryLower, const QString& appNameProp);

} // namespace tanara::detail
