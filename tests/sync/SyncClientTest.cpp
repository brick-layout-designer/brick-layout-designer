// SyncClient against an in-process server speaking the collaborative
// server's y-websocket protocol with its own SyncDoc (sync phase P3c):
// first sync, local and remote edits, offline edits delivered on
// reconnect, and sessions the server ends for good.

#include "Compat.h"
#include "FakeSyncServer.h"
#include "ServerApi.h"
#include "ServerRefusal.h"
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

namespace {
// 4 billion clients in 8 bytes (y-crdt issue 675): refused by UpdateGuard.
const QByteArray kUnreadable("\xff\xff\xff\xff\x0f\x00\x00\x00", 8);
}  // namespace

// An update that can't be read changes nothing; the client asks once for
// the whole layout again, and carries on.
TEST(SyncClient, AnUnreadableUpdateAsksForTheWholeLayoutAgain) {
    FakeServer server;
    sync::SyncDoc local;
    sync::SyncClient client(local);
    int closed = 0;
    QObject::connect(&client, &sync::SyncClient::closedForGood, [&] { ++closed; });
    client.open(server.url(), {});
    ASSERT_TRUE(waitFor([&] { return client.status() == sync::SyncClient::Status::Synced; }));
    const QJsonObject before = local.toJson();
    const size_t step1s = server.step1s.size();

    server.sendRaw(protocol::encode(Kind::Update, kUnreadable));
    ASSERT_TRUE(waitFor([&] { return server.step1s.size() > step1s; }));
    EXPECT_EQ(server.step1s.back(), QByteArray(1, '\0'));  // "send me everything"
    waitFor([] { return false; }, 100);
    EXPECT_EQ(local.toJson(), before);
    EXPECT_EQ(client.status(), sync::SyncClient::Status::Synced);
    EXPECT_EQ(closed, 0);

    // Still live: the next change arrives, and a later bad one asks again.
    server.remoteEdit([](core::Map& m) { firstBrick(m).orientation = 30.0f; });
    ASSERT_TRUE(waitFor([&] { return firstBrick(*sync::mapFromDocJson(local.toJson())).orientation == 30.0f; }));
    server.sendRaw(protocol::encode(Kind::Update, kUnreadable));
    ASSERT_TRUE(waitFor([&] { return server.step1s.size() > step1s + 1; }));
    EXPECT_EQ(closed, 0);
}

// When the whole layout can't be read either, the session ends and says why.
TEST(SyncClient, EndsTheSessionWhenTheResyncCantBeReadEither) {
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
    const QJsonObject before = local.toJson();

    server.step2Override = kUnreadable;
    server.sendRaw(protocol::encode(Kind::Update, kUnreadable));
    ASSERT_TRUE(waitFor([&] { return finalCode != 0; }));
    EXPECT_EQ(finalCode, sync::kUnreadableUpdateCode);
    EXPECT_EQ(finalReason, QStringLiteral("unreadable_update"));
    EXPECT_FALSE(sync::liveCloseText(finalCode, finalReason).isEmpty());
    EXPECT_EQ(client.status(), sync::SyncClient::Status::Offline);
    EXPECT_EQ(local.toJson(), before);
    // No retry follows.
    waitFor([] { return false; }, 200);
    EXPECT_EQ(server.authHeaders.size(), 1u);
}

// A message over the size limit: Qt drops it unread and closes; the
// reconnect is the resync, and when its answer is over the limit too the
// session ends.
TEST(SyncClient, AMessageOverTheSizeLimitReconnectsThenEnds) {
    FakeServer server;
    sync::SyncDoc local;
    sync::SyncClient client(local);
    client.setReconnectDelays(20ms, 20ms);
    client.setMaxMessageSize(1024 * 1024);
    int finalCode = 0;
    QObject::connect(&client, &sync::SyncClient::closedForGood, [&](int c, const QString&) { finalCode = c; });
    client.open(server.url(), {});
    ASSERT_TRUE(waitFor([&] { return client.status() == sync::SyncClient::Status::Synced; }));
    const QByteArray huge = protocol::encode(Kind::Update, QByteArray(2 * 1024 * 1024, '\0'));

    server.sendRaw(huge);
    ASSERT_TRUE(waitFor([&] { return server.authHeaders.size() == 2 && client.status() == sync::SyncClient::Status::Synced; }));
    EXPECT_EQ(finalCode, 0);

    server.step2Override = QByteArray(2 * 1024 * 1024, '\0');
    server.sendRaw(huge);
    ASSERT_TRUE(waitFor([&] { return finalCode != 0; }));
    EXPECT_EQ(finalCode, sync::kUnreadableUpdateCode);
    EXPECT_EQ(server.authHeaders.size(), 3u);
}
