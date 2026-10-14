#pragma once
//
// The "paths" section of settings.json (foundation): the user-adjustable locations. An empty value
// means "the platform default" (see AppPaths). TANARA_HOME overrides every value here.
//
//   "paths": { "dataDir": "", "modelsDir": "", "notesDir": "",
//              "recordingRoots": [ { "path": "~/Tanara/recordings", "primary": true } ] }
//
// The legacy layout (~/.tanara + ~/Tanara) is simply a special case of this section
// (`tanara-cli paths adopt-legacy` fills it in).
#include <QJsonObject>
#include <QList>
#include <QString>

namespace tanara::paths {

struct RecordingRoot {
    QString path;          // may start with "~"
    bool primary = false;  // new recordings go into the primary root
    bool operator==(const RecordingRoot&) const = default;
};

struct PathSettings {
    QString dataDir;                    // people, voiceprints, tags, index.db, cloud-state, prompts, secrets.json
    QString modelsDir;                  // speaker-embedding models (*.onnx)
    QString notesDir;                   // Markdown copies of the summaries
    QList<RecordingRoot> recordingRoots; // only the primary one is used for now (more roots: P8)

    // The primary root's path (the first marked primary, else the first one); empty = default.
    QString primaryRecordingRoot() const;
    // Sets the primary root (replaces the primary entry, keeps the others); empty path = remove it.
    void setPrimaryRecordingRoot(const QString& path);

    QJsonObject toJson() const;
    static PathSettings fromJson(const QJsonObject& o);
    bool operator==(const PathSettings&) const = default;
};

} // namespace tanara::paths
