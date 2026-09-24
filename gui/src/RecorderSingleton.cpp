#include "RecorderSingleton.h"
#include "tanara/Logging.h"

#include <QDataStream>
#include <QLocalServer>
#include <QLocalSocket>

#if !defined(Q_OS_WIN)
#include <unistd.h>
#endif

namespace tanara_gui {

QString RecorderSingleton::serverName()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("tanara-recorder");
#else
    return QStringLiteral("tanara-recorder-%1").arg(getuid());
#endif
}

bool RecorderSingleton::forwardToExisting(const QStringList& args, int timeoutMs)
{
    QLocalSocket sock;
    sock.connectToServer(serverName());
    if (!sock.waitForConnected(qMin(timeoutMs, 500)))
        return false;   // nincs élő példány (vagy stale socket) → mi leszünk az
    QByteArray payload;
    { QDataStream ds(&payload, QIODevice::WriteOnly); ds << args; }
    sock.write(payload);
    if (!sock.waitForBytesWritten(timeoutMs))
        return false;
    // Nyugta: a szerver "OK"-ot ír vissza, miután feldolgozta. E nélkül egy lefagyott
    // példányt élőnek hinnénk és elveszne a kérés.
    if (!sock.waitForReadyRead(timeoutMs))
        return false;
    const bool ok = sock.readAll().startsWith("OK");
    qCInfo(tanara::lcApp).noquote() << "Felvevő-kérés átadva a futó példánynak:" << ok;
    return ok;
}

RecorderSingleton::RecorderSingleton(QObject* parent) : QObject(parent) {}

RecorderSingleton::~RecorderSingleton() { close(); }

bool RecorderSingleton::listen()
{
    if (m_server)
        return true;
    // Ha él egy példány, NEM vesszük át a nevet. Ha nem él (stale socket-fájl a crash
    // után), letakarítjuk és mi figyelünk.
    {
        QLocalSocket probe;
        probe.connectToServer(serverName());
        if (probe.waitForConnected(300)) {
            probe.disconnectFromServer();
            return false;
        }
    }
    QLocalServer::removeServer(serverName());
    m_server = new QLocalServer(this);
    if (!m_server->listen(serverName())) {
        qCWarning(tanara::lcApp).noquote()
            << "RecorderSingleton: nem tud figyelni:" << m_server->errorString();
        delete m_server; m_server = nullptr;
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, [this]() {
        while (QLocalSocket* c = m_server->nextPendingConnection()) {
            // Kis payload (argumentum-lista) → rövid blokkoló olvasás elegendő.
            if (!c->waitForReadyRead(500)) { c->deleteLater(); continue; }
            QByteArray data = c->readAll();
            QStringList args;
            { QDataStream ds(data); ds >> args; }
            c->write("OK");
            c->flush();
            c->disconnectFromServer();
            c->deleteLater();
            emit requestReceived(args);
        }
    });
    return true;
}

bool RecorderSingleton::isListening() const { return m_server != nullptr; }

void RecorderSingleton::close()
{
    if (!m_server) return;
    m_server->close();
    QLocalServer::removeServer(serverName());
    delete m_server; m_server = nullptr;
}

RecorderArgs parseRecorderArgs(const QStringList& args)
{
    RecorderArgs r;
    for (int i = 0; i < args.size(); ++i) {
        if (args[i] == QStringLiteral("--title") && i + 1 < args.size()) r.title = args[++i];
        else if (args[i] == QStringLiteral("--context") && i + 1 < args.size()) r.context = args[++i];
        else if (args[i] == QStringLiteral("--device") && i + 1 < args.size()) r.deviceIdx << args[++i].toInt();
        else if (args[i] == QStringLiteral("--no-start")) r.noStart = true;
    }
    return r;
}

} // namespace tanara_gui
