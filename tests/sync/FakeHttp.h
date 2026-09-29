#pragma once

// An in-process HTTP server for the ServerApi and ConnectDialog tests:
// answers each request from a per-path queue of (status, JSON) replies and
// records what it was sent.

#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <deque>
#include <utility>
#include <vector>

namespace bld::synctest {

struct Request {
    QByteArray method, path, authorization, body;
    qint64 atMs = 0; // when it arrived
};

class FakeHttp : public QObject {
public:
    FakeHttp() {
        clock_.start();
        server_.listen(QHostAddress::LocalHost, 0);
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = server_.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, this, [this, s] { onData(s); });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }
    QUrl base() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort())); }
    void reply(const QByteArray& path, int status, const QJsonObject& body) {
        replies_[path].push_back({ status, body });
    }

    std::vector<Request> requests;

private:
    void onData(QTcpSocket* s) {
        QByteArray& buf = pending_[s];
        buf += s->readAll();
        const qsizetype headerEnd = buf.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        const QList<QByteArray> lines = buf.left(headerEnd).split('\n');
        Request req;
        const QList<QByteArray> first = lines[0].trimmed().split(' ');
        req.method = first.value(0);
        req.path = first.value(1);
        qsizetype length = 0;
        for (const QByteArray& l : lines.mid(1)) {
            const qsizetype colon = l.indexOf(':');
            const QByteArray name = l.left(colon).trimmed().toLower(), value = l.mid(colon + 1).trimmed();
            if (name == "content-length") length = value.toLongLong();
            if (name == "authorization") req.authorization = value;
        }
        if (buf.size() < headerEnd + 4 + length) return;
        req.body = buf.mid(headerEnd + 4, length);
        req.atMs = clock_.elapsed();
        pending_.remove(s);
        requests.push_back(req);
        auto& q = replies_[req.path];
        const auto [status, json] =
            q.empty()
                ? std::pair{ 404, QJsonObject{ { QStringLiteral("error"), QStringLiteral("not_found") } } }
                : q.front();
        if (q.size() > 1) q.pop_front(); // the last reply repeats
        const QByteArray body = QJsonDocument(json).toJson(QJsonDocument::Compact);
        s->write("HTTP/1.1 " + QByteArray::number(status)
                 + " X\r\nContent-Type: application/json\r\nContent-Length: "
                 + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        s->disconnectFromHost();
    }

    QTcpServer server_;
    QElapsedTimer clock_;
    QHash<QTcpSocket*, QByteArray> pending_;
    QHash<QByteArray, std::deque<std::pair<int, QJsonObject>>> replies_;
};

} // namespace bld::synctest
