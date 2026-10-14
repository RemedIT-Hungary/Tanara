#pragma once
//
// MediaToolLocator: resolves an external media tool (ffmpeg / ffprobe) to a program path.
// Order (the first existing, executable file wins):
//   1. the configured path (settings.media.ffmpegPath / ffprobePath), if not empty;
//   2. the application's folder (QCoreApplication::applicationDirPath(); the Windows package puts
//      ffmpeg.exe there);
//   3. bundle locations: macOS <app>/Contents/Resources/bin and <app>/Contents/MacOS (= ../Resources/bin,
//      ../MacOS from the executable's folder), AppImage $APPDIR/usr/bin;
//   4. the PATH (QStandardPaths::findExecutable).
// On Windows the file name gets the ".exe" suffix. Pure apart from the file-system checks, so the
// order is unit-tested with temporary folders (LocatorEnv).
#include "tanara/media/MediaSettings.h"

#include <QString>
#include <QStringList>

namespace tanara::media {

// The environment of a lookup (systemEnv() for the running process; tests build their own).
struct LocatorEnv {
    QString appDir;            // applicationDirPath(); empty = skip
    QString appImageDir;       // $APPDIR; empty = not an AppImage
    QStringList pathDirs;      // the PATH folders; empty = the process PATH
    bool windows = false;      // ".exe" suffix
    bool macos = false;        // macOS bundle locations

    static LocatorEnv systemEnv();
};

enum class ToolSource { None, Settings, AppDir, Bundle, Path };

struct ToolResolution {
    QString path;              // absolute path; empty = not found
    ToolSource source = ToolSource::None;
    bool found() const { return !path.isEmpty(); }
};

class MediaToolLocator {
public:
    // tool: the base name ("ffmpeg" / "ffprobe"); configured: the settings value (may be empty or
    // start with "~").
    static ToolResolution locate(const QString& tool, const QString& configured, const LocatorEnv& env);
    // The candidate files of steps 1-3 in order (step 4 is the PATH search). For diagnostics / tests.
    static QStringList candidates(const QString& tool, const QString& configured, const LocatorEnv& env);
    static QString executableName(const QString& tool, bool windows);
    static QString sourceName(ToolSource s);   // "settings" | "app-dir" | "bundle" | "path" | "-"
};

} // namespace tanara::media
