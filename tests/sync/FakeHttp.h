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
    QByteArray method, path, authorization, userAgent, body;
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
        replies_[path].push_back({ status, QJsonDocument(body).toJson(QJsonDocument::Compact),
                                   QByteArrayLiteral("application/json") });
    }
    // Forget the replies queued for a path (to answer differently from now on).
    void clear(const QByteArray& path) { replies_.remove(path); }
    // Any bytes, e.g. a part's sprite.
    void replyRaw(const QByteArray& path, int status, const QByteArray& body, const QByteArray& contentType) {
        replies_[path].push_back({ status, body, contentType });
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
            if (name == "user-agent") req.userAgent = value;
        }
        if (buf.size() < headerEnd + 4 + length) return;
        req.body = buf.mid(headerEnd + 4, length);
        req.atMs = clock_.elapsed();
        pending_.remove(s);
        requests.push_back(req);
        auto& q = replies_[req.path];
        const Reply r = q.empty() ? Reply{ 404, QByteArrayLiteral("{\"error\":\"not_found\"}"),
                                           QByteArrayLiteral("application/json") }
                                  : q.front();
        if (q.size() > 1) q.pop_front(); // the last reply repeats
        const QByteArray& body = r.body;
        const int status = r.status;
        s->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: " + r.contentType
                 + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n"
                 + body);
        s->disconnectFromHost();
    }

    QTcpServer server_;
    QElapsedTimer clock_;
    QHash<QTcpSocket*, QByteArray> pending_;
    struct Reply {
        int status;
        QByteArray body, contentType;
    };
    QHash<QByteArray, std::deque<Reply>> replies_;
};

} // namespace bld::synctest
