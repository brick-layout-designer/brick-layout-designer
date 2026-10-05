// SyncClient against an in-process server speaking the collaborative
// server's y-websocket protocol with its own SyncDoc (sync phase P3c):
// first sync, local and remote edits, offline edits delivered on
// reconnect, and sessions the server ends for good.

#include "FakeSyncServer.h"
#include "ServerApi.h"
#include "SyncClient.h"

#include <gtest/gtest.h>

using namespace bld;
using namespace bld::synctest;
using namespace std::chrono_literals;

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
    EXPECT_EQ(server.userAgents[0].toUtf8(), sync::userAgent());

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
    // The server may get our answer before we get its: Synced follows shortly.
    EXPECT_TRUE(waitFor([&] { return client.status() == sync::SyncClient::Status::Synced; }));
    EXPECT_EQ(server.authHeaders.size(), 2u);
}

TEST(SyncClient, StopsWhenTheServerEndsTheSession) {
    // As ints: 4404 is outside CloseCode's named values (UBSan flags a load of it).
    for (const int code : { static_cast<int>(QWebSocketProtocol::CloseCodePolicyViolated), 4404 }) {
        FakeServer server;
        sync::SyncDoc local;
        sync::SyncClient client(local);
        client.setReconnectDelays(20ms, 20ms);
        int finalCode = 0;
        QObject::connect(&client, &sync::SyncClient::closedForGood, [&](int c, const QString&) { finalCode = c; });
        client.open(server.url(), {});
        ASSERT_TRUE(waitFor([&] { return client.status() == sync::SyncClient::Status::Synced; }));
        server.dropAll(static_cast<QWebSocketProtocol::CloseCode>(code), QStringLiteral("access_revoked"));
        ASSERT_TRUE(waitFor([&] { return finalCode != 0; }));
        EXPECT_EQ(finalCode, code);
        EXPECT_EQ(client.status(), sync::SyncClient::Status::Offline);
        // No retry follows.
        waitFor([] { return false; }, 200);
        EXPECT_EQ(server.authHeaders.size(), 1u);
    }
}

// The server's limit on people in one layout: stop (retrying is refused
// again) and say why; other 4429s (too many tabs) still retry.
TEST(SyncClient, StopsAtTheLiveEditorLimit) {
    FakeServer server;
    sync::SyncDoc local;
    sync::SyncClient client(local);
    client.setReconnectDelays(20ms, 20ms);
    int finalCode = 0;
    QString finalReason;
    QObject::connect(&client, &sync::SyncClient::closedForGood, [&](int c, const QString& r) {
        finalCode = c;
        finalReason = r;
    });
    client.open(server.url(), {});
    ASSERT_TRUE(waitFor([&] { return client.status() == sync::SyncClient::Status::Synced; }));
    server.dropAll(static_cast<QWebSocketProtocol::CloseCode>(4429), QStringLiteral("limit_reached"));
    ASSERT_TRUE(waitFor([&] { return finalCode != 0; }));
    EXPECT_EQ(finalCode, 4429);
    EXPECT_EQ(finalReason, QStringLiteral("limit_reached"));
    waitFor([] { return false; }, 200);
    EXPECT_EQ(server.authHeaders.size(), 1u);
}
