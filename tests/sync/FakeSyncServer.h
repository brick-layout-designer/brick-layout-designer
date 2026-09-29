#pragma once

// Test helpers shared by the sync tests: an in-process server speaking the
// collaborative server's y-websocket protocol, and small map accessors.

#include "SyncDoc.h"
#include "SyncProtocol.h"
#include "WebModel.h"

#include "core/LayerBrick.h"
#include "core/Map.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QWebSocket>
#include <QWebSocketServer>

#include <functional>
#include <stdexcept>
#include <vector>

namespace bld::synctest {

namespace protocol = bld::sync::protocol;
using protocol::Kind;


inline QByteArray serverDoc() {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/sync/layers.ydoc"));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

inline bool waitFor(const std::function<bool()>& done, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

// The collaborative server's handler (apps/server/src/ws/handler.ts) in
// miniature: step 1 on connect, answers step 1, applies and relays updates.
class FakeServer : public QObject {
public:
    FakeServer() : server_(QStringLiteral("test"), QWebSocketServer::NonSecureMode) {
        doc.applyUpdate(serverDoc());
        server_.listen(QHostAddress::LocalHost, 0);
        connect(&server_, &QWebSocketServer::newConnection, this, [this] {
            QWebSocket* ws = server_.nextPendingConnection();
            authHeaders.push_back(QString::fromUtf8(ws->request().rawHeader("Authorization")));
            peers.push_back(ws);
            connect(ws, &QWebSocket::binaryMessageReceived, this, [this, ws](const QByteArray& m) { onMessage(ws, m); });
            connect(ws, &QWebSocket::disconnected, this, [this, ws] { std::erase(peers, ws); ws->deleteLater(); });
            ws->sendBinaryMessage(protocol::encode(Kind::SyncStep1, doc.stateVector()));
        });
    }

    QUrl url() const { return QUrl(QStringLiteral("ws://127.0.0.1:%1/ws/layout/L1").arg(server_.serverPort())); }

    // An edit made on the server side (another user), relayed to clients.
    void remoteEdit(const std::function<void(core::Map&)>& edit) {
        auto map = sync::mapFromDocJson(doc.toJson());
        edit(*map);
        const QByteArray update = doc.writeMap(*map);
        for (QWebSocket* p : peers) p->sendBinaryMessage(protocol::encode(Kind::Update, update));
    }

    void dropAll(QWebSocketProtocol::CloseCode code, const QString& reason = {}) {
        for (QWebSocket* p : std::vector<QWebSocket*>(peers)) p->close(code, reason);
    }

    sync::SyncDoc doc;
    std::vector<QWebSocket*> peers;
    std::vector<QString> authHeaders;

private:
    void onMessage(QWebSocket* from, const QByteArray& message) {
        const auto m = protocol::decode(message);
        if (!m) return;
        if (m->kind == Kind::SyncStep1) {
            from->sendBinaryMessage(protocol::encode(Kind::SyncStep2, doc.diffSince(m->payload)));
        } else if (m->kind != Kind::Awareness) {
            doc.applyUpdate(m->payload);
            for (QWebSocket* p : peers)
                if (p != from) p->sendBinaryMessage(protocol::encode(Kind::Update, m->payload));
        }
    }

    QWebSocketServer server_;
};

inline core::Brick& firstBrick(core::Map& m) {
    for (auto& l : m.layers())
        if (l->kind() == core::LayerKind::Brick) return static_cast<core::LayerBrick&>(*l).bricks[0];
    throw std::runtime_error("no bricks");
}

inline QRectF firstBrickArea(const sync::SyncDoc& d) { return firstBrick(*sync::mapFromDocJson(d.toJson())).displayArea; }


}  // namespace bld::synctest
