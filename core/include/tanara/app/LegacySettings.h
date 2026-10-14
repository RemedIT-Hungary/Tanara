#pragma once
//
// One-time helpers for taking over the OLD (flat, pre-v2) settings.json and the legacy folder
// layout (~/.tanara + ~/Tanara). Used only by `tanara-cli paths adopt-legacy` and
// `tanara-cli settings import-legacy`; deletable before 1.0-alpha. The app itself never reads the
// old format (no backward compatibility, D8).
#include "tanara/app/AppSettings.h"

#include <QJsonObject>
#include <QString>

namespace tanara::app::legacy {

// The default legacy folders under `home`: ~/.tanara, ~/.tanara/models, ~/Tanara/recordings,
// ~/Tanara/notes. Values present in the old flat settings (audioDir, notesDir, metadataDir) win.
paths::PathSettings legacyPaths(const QJsonObject& flat, const QString& home);

// Every non-path value of the old flat settings mapped onto the sections of `base` (keys missing
// from the old file keep `base`'s value). The "paths" and "secrets" sections are not touched.
AppSettings importFlat(const QJsonObject& flat, const AppSettings& base);

// Whether a JSON object looks like the old flat format (no "version": 2).
bool isFlat(const QJsonObject& o);

} // namespace tanara::app::legacy
