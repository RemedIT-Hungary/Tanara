#pragma once
// RecorderSingleton: ONE recorder instance per machine.
//
// Whoever shows a recorder (the analyzer with its recorder window, or the `tanara --record`
// process) listens on a local socket. Every newer `tanara --record ...` call (watcher, tray
// click, notification button) FIRST tries to pass its arguments to the running instance; if
// that works, it exits. So no more than one recorder opens, and the tray always brings up the
// one that is recording.
#include <QObject>
#include <QString>
#include <QStringList>

class QLocalServer;

namespace tanara_gui {

class RecorderSingleton : public QObject {
    Q_OBJECT
public:
    // "tanara-recorder-<uid>"; with TANARA_HOME a "-<folder hash>" suffix (isolation).
    static QString serverName();

    // Passes args to a running instance. true = delivered (the caller may exit).
    static bool forwardToExisting(const QStringList& args, int timeoutMs = 1500);

    explicit RecorderSingleton(QObject* parent = nullptr);
    ~RecorderSingleton() override;

    // false if another LIVE instance holds the name (a stale socket file is cleaned up automatically).
    bool listen();
    bool isListening() const;
    void close();

signals:
    void requestReceived(QStringList args);   // the forwarded `--record ...` arguments

private:
    QLocalServer* m_server = nullptr;
};

// Shared parsing of the `--record` arguments (main.cpp recorder mode + forwarded request).
struct RecorderArgs {
    QString title, context;
    QString app;           // --app: the detected call app (for the automatic name)
    QList<int> deviceIdx;
    bool noStart = false;
    bool stop = false;     // --stop: stop the running recording (sent by the tray menu)
};
RecorderArgs parseRecorderArgs(const QStringList& args);

} // namespace tanara_gui
