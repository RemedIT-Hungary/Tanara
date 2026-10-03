#pragma once
//
// AnalyzerSingleton — kérés átadása a FUTÓ elemzőnek (a RecorderSingleton kistestvére).
//
// Az új főablak figyel egy lokális socketen. A `tanara --meeting <id>` (az önálló felvevő
// „Megnyitás az elemzőben” gombja indítja) előbb megpróbálja átadni a kérést a futó
// elemzőnek — ha sikerül, kilép, így nem nyílik második elemző ugyanazon az adaton.
// A név TANARA_HOME mellett a metaadat-mappa hash-ével egészül ki
// (tanara::instanceScopeSuffix), mint a felvevőé: a homokozó-példány és a felhasználó valódi
// elemzője sosem látja egymást.
//
#include "tanara/detect/RecordingLock.h"

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

// Átadja az args-ot a futó elemzőnek. true = átadva és nyugtázva (a hívó kiléphet).
inline bool forwardToExisting(const QStringList& args, int timeoutMs = 1500)
{
    QLocalSocket sock;
    sock.connectToServer(serverName());
    if (!sock.waitForConnected(qMin(timeoutMs, 500)))
        return false;   // nincs futó elemző (vagy stale socket)
    QByteArray payload;
    { QDataStream ds(&payload, QIODevice::WriteOnly); ds << args; }
    sock.write(payload);
    if (!sock.waitForBytesWritten(timeoutMs) || !sock.waitForReadyRead(timeoutMs))
        return false;
    return sock.readAll().startsWith("OK");
}

// Figyelés a kérésekre; a szerver a parent gyereke. nullptr, ha a nevet másik ÉLŐ elemző
// fogja (nem vesszük el tőle); a crash után maradt socket-fájlt letakarítja.
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

// A `--meeting <id>` értéke az argumentumokból ("" ha nincs).
inline QString meetingArg(const QStringList& args)
{
    const int i = args.indexOf(QStringLiteral("--meeting"));
    return i >= 0 && i + 1 < args.size() ? args.at(i + 1) : QString();
}

} // namespace tanara_gui::analyzer_singleton
