// SyncClient against an in-process server speaking the collaborative
// server's y-websocket protocol with its own SyncDoc (sync phase P3c):
// first sync, local and remote edits, offline edits delivered on
// reconnect, and sessions the server ends for good.

#include "FakeSyncServer.h"
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
