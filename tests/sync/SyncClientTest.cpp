// SyncClient against an in-process server speaking the collaborative
// server's y-websocket protocol with its own SyncDoc (sync phase P3c):
// first sync, local and remote edits, offline edits delivered on
// reconnect, and sessions the server ends for good.

#include "SyncClient.h"
#include "SyncDoc.h"
#include "SyncProtocol.h"
#include "WebModel.h"

#include "core/LayerBrick.h"
#include "core/Map.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QWebSocket>
#include <QWebSocketServer>

#include <functional>
#include <memory>
#include <vector>

using namespace bld;
using namespace std::chrono_literals;
namespace protocol = bld::sync::protocol;
using protocol::Kind;

namespace {

QByteArray serverDoc() {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/sync/layers.ydoc"));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool waitFor(const std::function<bool()>& done, int ms = 5000) {
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

core::Brick& firstBrick(core::Map& m) {
    for (auto& l : m.layers())
        if (l->kind() == core::LayerKind::Brick) return static_cast<core::LayerBrick&>(*l).bricks[0];
    throw std::runtime_error("no bricks");
}

QRectF firstBrickArea(const sync::SyncDoc& d) { return firstBrick(*sync::mapFromDocJson(d.toJson())).displayArea; }

}  // namespace

TEST(SyncClient, SyncsBothWaysOverTheServerProtocol) {
    FakeServer server;
    sync::SyncDoc local;
    sync::SyncClient client(local);
    int remoteChanges = 0;
    QObject::connect(&client, &sync::SyncClient::remoteChange, [&] { ++remoteChanges; });
    client.open(server.url(), QStringLiteral("bld_pat_test"));

    // First sync: an empty desktop doc receives the whole layout.
    ASSERT_TRUE(waitFor([&] { return client.status() == sync::SyncClient::Status::Synced; }));
    EXPECT_EQ(local.toJson(), server.doc.toJson());
    EXPECT_GT(remoteChanges, 0);
    ASSERT_EQ(server.authHeaders.size(), 1u);
    EXPECT_EQ(server.authHeaders[0], QStringLiteral("Bearer bld_pat_test"));

    // A desktop edit reaches the server.
    auto map = sync::mapFromDocJson(local.toJson());
    firstBrick(*map).displayArea.translate(24, 0);
    client.sendLocal(local.writeMap(*map));
    ASSERT_TRUE(waitFor([&] { return firstBrickArea(server.doc) == firstBrick(*map).displayArea; }));

    // Someone else's edit reaches the desktop.
    const int before = remoteChanges;
    server.remoteEdit([](core::Map& m) { firstBrick(m).orientation = 45.0f; });
    ASSERT_TRUE(waitFor([&] { return remoteChanges > before; }));
    EXPECT_EQ(firstBrick(*sync::mapFromDocJson(local.toJson())).orientation, 45.0f);
}

TEST(SyncClient, EditsMadeWhileOfflineArriveOnReconnect) {
    FakeServer server;
    sync::SyncDoc local;
    sync::SyncClient client(local);
    client.setReconnectDelays(50ms, 200ms);
    client.open(server.url(), {});
    ASSERT_TRUE(waitFor([&] { return client.status() == sync::SyncClient::Status::Synced; }));

    server.dropAll(QWebSocketProtocol::CloseCodeGoingAway);
    ASSERT_TRUE(waitFor([&] { return client.status() != sync::SyncClient::Status::Synced; }));
    // Edited while the connection is down: nothing can be sent now.
    auto map = sync::mapFromDocJson(local.toJson());
    firstBrick(*map).displayArea.translate(0, 40);
    client.sendLocal(local.writeMap(*map));
    // The retry reconnects, and the handshake delivers the offline edit.
    ASSERT_TRUE(waitFor([&] { return firstBrickArea(server.doc) == firstBrick(*map).displayArea; }));
    EXPECT_EQ(client.status(), sync::SyncClient::Status::Synced);
    EXPECT_EQ(server.authHeaders.size(), 2u);
}

TEST(SyncClient, StopsWhenTheServerEndsTheSession) {
    for (const auto code : { QWebSocketProtocol::CloseCodePolicyViolated, static_cast<QWebSocketProtocol::CloseCode>(4404) }) {
        FakeServer server;
        sync::SyncDoc local;
        sync::SyncClient client(local);
        client.setReconnectDelays(20ms, 20ms);
        int finalCode = 0;
        QObject::connect(&client, &sync::SyncClient::closedForGood, [&](int c, const QString&) { finalCode = c; });
        client.open(server.url(), {});
        ASSERT_TRUE(waitFor([&] { return client.status() == sync::SyncClient::Status::Synced; }));
        server.dropAll(code, QStringLiteral("access_revoked"));
        ASSERT_TRUE(waitFor([&] { return finalCode != 0; }));
        EXPECT_EQ(finalCode, static_cast<int>(code));
        EXPECT_EQ(client.status(), sync::SyncClient::Status::Offline);
        // No retry follows.
        waitFor([] { return false; }, 200);
        EXPECT_EQ(server.authHeaders.size(), 1u);
    }
}
