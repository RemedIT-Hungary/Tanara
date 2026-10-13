#pragma once
// AnalyzerSingleton: hands a request to the RUNNING analyzer (younger sibling of RecorderSingleton).
//
// The new main window listens on a local socket. `tanara --meeting <id>` (started by the
// standalone recorder's "Open in the analyzer" button) first tries to pass the request to the
// running analyzer; if that works it exits, so no second analyzer opens on the same data.
// With TANARA_HOME the name gets the metadata folder's hash appended
// (tanara::instanceScopeSuffix), like the recorder's: a sandbox instance and the user's real
// analyzer never see each other.
#include "tanara/foundation/ProcessLock.h"

#include <QDataStream>
#include <QLocalServer>
#include <QLocalSocket>
#include <QString>
#include <QStringList>

#include <functional>

#if !defined(Q_OS_WIN)
#include <unistd.h>
#endif

namespace tanara_gui::analyzer_singleton {

inline QString serverName()
{
    const QString scope = tanara::instanceScopeSuffix();
#if defined(Q_OS_WIN)
    return QStringLiteral("tanara-analyzer") + scope;
#else
    return QStringLiteral("tanara-analyzer-%1").arg(getuid()) + scope;
#endif
}

// Passes args to the running analyzer. true = delivered and acknowledged (the caller may exit).
inline bool forwardToExisting(const QStringList& args, int timeoutMs = 1500)
{
    QLocalSocket sock;
    sock.connectToServer(serverName());
    if (!sock.waitForConnected(qMin(timeoutMs, 500)))
        return false;   // no running analyzer (or a stale socket)
    QByteArray payload;
    { QDataStream ds(&payload, QIODevice::WriteOnly); ds << args; }
    sock.write(payload);
    if (!sock.waitForBytesWritten(timeoutMs) || !sock.waitForReadyRead(timeoutMs))
        return false;
    return sock.readAll().startsWith("OK");
}

// Listens for requests; the server is a child of parent. nullptr if another LIVE analyzer
// holds the name (we do not take it over); cleans up a socket file left after a crash.
inline QLocalServer* listen(QObject* parent, std::function<void(const QStringList&)> handler)
{
    {
        QLocalSocket probe;
        probe.connectToServer(serverName());
        if (probe.waitForConnected(300)) {
            probe.disconnectFromServer();
            return nullptr;
        }
    }
    QLocalServer::removeServer(serverName());
    auto* server = new QLocalServer(parent);
    if (!server->listen(serverName())) {
        delete server;
        return nullptr;
    }
    QObject::connect(server, &QLocalServer::newConnection, server, [server, handler]() {
        while (QLocalSocket* c = server->nextPendingConnection()) {
            if (!c->waitForReadyRead(500)) { c->deleteLater(); continue; }
            const QByteArray data = c->readAll();
            QStringList args;
            { QDataStream ds(data); ds >> args; }
            c->write("OK");
            c->flush();
            c->disconnectFromServer();
            c->deleteLater();
            handler(args);
        }
    });
    return server;
}

// The value of `--meeting <id>` from the arguments ("" if absent).
inline QString meetingArg(const QStringList& args)
{
    const int i = args.indexOf(QStringLiteral("--meeting"));
    return i >= 0 && i + 1 < args.size() ? args.at(i + 1) : QString();
}

} // namespace tanara_gui::analyzer_singleton
