#pragma once
//
// The "media" section of settings.json: where the external media tools are. An empty value means
// "automatic" (MediaToolLocator: next to the app, in the bundle, then on the PATH).
//
//   "media": { "ffmpegPath": "", "ffprobePath": "" }
#include <QJsonObject>
#include <QString>

namespace tanara::media {

struct MediaSettings {
    QString ffmpegPath;    // empty = automatic
    QString ffprobePath;   // empty = automatic

    QJsonObject toJson() const;
    static MediaSettings fromJson(const QJsonObject& o);
    bool operator==(const MediaSettings&) const = default;
};

} // namespace tanara::media
