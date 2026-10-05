#pragma once
//
// Minimális, folyamaton belüli ál-HTTP-szerver a tesztekhez (valódi hálózati / LM Studio hívás
// NINCS): minden kérést naplóz, a választ a handler adja. A test_llm_context.cpp mintája.
//
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QVector>

#include <functional>
#include <memory>

namespace fakehttp {

struct Request { QByteArray method; QString path; QByteArray head; QByteArray body; };
struct Reply { int status = 200; QByteArray body = "{}"; bool hold = false; };

class Server : public QObject {
public:
    std::function<Reply(const Request&)> handler;
    QVector<Request> log;

    Server() {
        connect(&m_srv, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* s = m_srv.nextPendingConnection()) {
                auto buf = std::make_shared<QByteArray>();
                connect(s, &QTcpSocket::readyRead, this, [this, s, buf]() { feed(s, *buf); });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
        m_srv.listen(QHostAddress::LocalHost);
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1/v1").arg(m_srv.serverPort()); }
    int count(const QByteArray& method, const QString& pathPart) const {
        int n = 0;
        for (const Request& r : log)
            if (r.method == method && r.path.contains(pathPart)) ++n;
        return n;
    }
    QJsonObject lastBody(const QString& pathPart) const {
        for (int i = int(log.size()) - 1; i >= 0; --i)
            if (log[i].path.contains(pathPart)) return QJsonDocument::fromJson(log[i].body).object();
        return {};
    }

private:
    void feed(QTcpSocket* s, QByteArray& buf) {
        buf += s->readAll();
        const int headEnd = int(buf.indexOf("\r\n\r\n"));
        if (headEnd < 0) return;
        const QByteArray head = buf.left(headEnd);
        qint64 len = 0;
        for (const QByteArray& line : head.split('\n')) {
            const QByteArray l = line.trimmed().toLower();
            if (l.startsWith("content-length:")) len = l.mid(15).trimmed().toLongLong();
        }
        if (buf.size() < headEnd + 4 + len) return;
        Request req;
        const QList<QByteArray> first = head.left(head.indexOf('\r')).split(' ');
        req.method = first.value(0);
        req.path = QString::fromUtf8(first.value(1));
        req.head = head;
        req.body = buf.mid(headEnd + 4, len);
        buf.clear();
        log.append(req);
        const Reply rep = handler ? handler(req) : Reply{};
        if (rep.hold) return;
        s->write("HTTP/1.1 " + QByteArray::number(rep.status) + " X\r\nContent-Type: application/json\r\n"
                 "Content-Length: " + QByteArray::number(rep.body.size()) + "\r\nConnection: close\r\n\r\n"
                 + rep.body);
        s->disconnectFromHost();
    }
    QTcpServer m_srv;
};

} // namespace fakehttp
