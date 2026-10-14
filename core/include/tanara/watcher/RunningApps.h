#pragma once
//
// RunningApps: the processes running now, by executable name (read only). The Settings window
// offers call apps to watch from it, and `tanara-cli running-apps` lists the watched ones that run.
// It is NOT the watcher's call signal (that is an app holding the microphone, see detect/).
//
// Platform port: processes() / supported() are defined in core/src/watcher/platform/<os>/
// (Linux: /proc, Windows: Toolhelp32 snapshot, otherwise: not supported, empty list).
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace tanara::watcher {

struct RunningProcess {
    qint64  pid = 0;
    QString name;   // lowercase executable base name (Linux: argv[0]'s file name; Windows: no ".exe")
};

class RunningApps {
public:
    // Whether this platform can list processes at all.
    static bool supported();
    // The running processes (several per name are possible). Empty if !supported().
    static QVector<RunningProcess> processes();
    // The distinct names of processes(), sorted.
    static QStringList names();
};

} // namespace tanara::watcher
